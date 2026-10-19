#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""DA 回归的在线签名服务。只做第 4、5 步。

本地把芯片给出的整段待签名数据发来，本服务用同目录的 key_3_leaf.pem
做 ECDSA P-256 签名，只返回签名。私钥不出进程，也不写进日志。

默认对收到的字节先做 SHA-256 再签名。芯片若已经给了 32 字节摘要，
请求里带 "hash": false，则按这个摘要直接签名。

用法：
  python sign_server.py
  python sign_server.py --host 127.0.0.1 --port 8765

POST /sign
  {"data_b64": "<待签名字节的 Base64>", "hash": true}
应答：
  {"signature_rs_b64": "<64 字节 R||S>", "signature_der_b64": "<DER>"}

GET /health 只报告曲线和叶子公钥指纹，不返回私钥。
"""
import argparse
import base64
import hashlib
import json
import os
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

KEY_NAME = "key_3_leaf.pem"


def load_key(path):
    """读 PKCS#8 私钥。必须是未加密的 P-256。"""
    with open(path, "rb") as handle:
        key = serialization.load_pem_private_key(handle.read(), password=None)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(key.curve, ec.SECP256R1):
        raise ValueError("私钥不是 ECDSA P-256：%s" % path)
    return key


def leaf_fingerprint(key):
    """叶子公钥 X||Y 的 SHA-256，用来核对服务器上是不是这把叶子密钥。"""
    raw = key.public_key().public_bytes(
        serialization.Encoding.X962,
        serialization.PublicFormat.UncompressedPoint,
    )
    return hashlib.sha256(raw[1:]).hexdigest().upper()


def sign_message(key, data, do_hash):
    """签名。返回 (64 字节 R||S, DER)。"""
    if do_hash:
        der = key.sign(data, ec.ECDSA(hashes.SHA256()))
    else:
        if len(data) != 32:
            raise ValueError("hash 为 false 时，待签名数据必须是 32 字节摘要")
        der = key.sign(data, ec.ECDSA(utils.Prehashed(hashes.SHA256())))
    r_value, s_value = utils.decode_dss_signature(der)
    raw = r_value.to_bytes(32, "big") + s_value.to_bytes(32, "big")
    return raw, der


class SignHandler(BaseHTTPRequestHandler):
    """只接受 /health 和 /sign。签名用的私钥挂在 server 上。"""

    server_version = "DASign/1.0"

    def log_message(self, fmt, *args):
        """只记请求行，不记正文，避免待签名数据进日志。"""
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

    def _send_json(self, status, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.split("?", 1)[0] != "/health":
            self._send_json(404, {"error": "not found"})
            return
        self._send_json(200, {
            "ok": True,
            "curve": "P-256",
            "leaf_pub_sha256": self.server.leaf_pub_sha256,
        })

    def do_POST(self):
        if self.path.split("?", 1)[0] != "/sign":
            self._send_json(404, {"error": "not found"})
            return
        length = int(self.headers.get("Content-Length", "0"))
        if length <= 0 or length > 4096:
            self._send_json(400, {"error": "body length must be 1..4096"})
            return
        try:
            request = json.loads(self.rfile.read(length).decode("utf-8"))
            data = base64.b64decode(request["data_b64"], validate=True)
            do_hash = bool(request.get("hash", True))
            raw, der = sign_message(self.server.private_key, data, do_hash)
        except Exception as exc:
            self._send_json(400, {"error": str(exc)})
            return
        self._send_json(200, {
            "signature_rs_b64": base64.b64encode(raw).decode("ascii"),
            "signature_der_b64": base64.b64encode(der).decode("ascii"),
        })


def main():
    parser = argparse.ArgumentParser(description="用 key_3_leaf.pem 对 DA 待签名数据做 ECDSA P-256 签名")
    parser.add_argument("--host", default="127.0.0.1", help="监听地址，默认只本机")
    parser.add_argument("--port", type=int, default=8765, help="监听端口，默认 8765")
    parser.add_argument("--key", default="", help="私钥路径，默认脚本目录下的 key_3_leaf.pem")
    args = parser.parse_args()

    key_path = args.key or os.path.join(os.path.dirname(os.path.abspath(__file__)), KEY_NAME)
    key = load_key(key_path)
    httpd = ThreadingHTTPServer((args.host, args.port), SignHandler)
    httpd.private_key = key
    httpd.leaf_pub_sha256 = leaf_fingerprint(key)
    print("LISTEN=http://%s:%d" % (args.host, args.port))
    print("LEAF_PUB_SHA256=%s" % httpd.leaf_pub_sha256)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("stopped")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        sys.exit(str(exc))
