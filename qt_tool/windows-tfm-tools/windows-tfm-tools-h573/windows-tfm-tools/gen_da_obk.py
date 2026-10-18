#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""由 ECDSA P-256 公钥生成 STM32H573 的 DA_Config.obk。

对照本目录 ROT_Provisioning/DA/Binary/DA_Config.obk，
以及 ST AN6008 表 13、STM32H5_DA_EmbeddedProvisioning 说明。
H573 是带硬件加密的型号，文件里加密标记写 1。
载荷本身仍是明文，烧录时由芯片用 DHUK 加密后存进 OBK。
"""
import argparse
import base64
import hashlib
import struct
import sys

# HDPL1 里 DA 配置的写入地址。AN6008：H573 存在 0x0FFD0100。
DEST_ADDR = 0x0FFD0100
# 烧进 OBK 的数据长度。32 字节完整性哈希 + 64 字节载荷 = 0x60。
OBK_DATA_SIZE = 0x60
# 1：烧录时由芯片加密。本目录现有 DA_Config.obk 和 DA_Config.xml 都是 1。
ENCRYPT_FLAG = 1
# SOC 权限。和 DA_Config.xml 里的 0x00005077 相同，小端放在 16 字节掩码的低 4 字节。
PERMISSION = 0x00005077
# 权限掩码宽度、保留区宽度，单位字节。AN6008：各 16 字节。
PERM_WIDTH = 16
RESERVED_WIDTH = 16


def load_spki(path):
    """读 PEM 公钥或私钥，取出 SubjectPublicKeyInfo 的 DER。给后面拆 ECC 点用。"""
    text = open(path, "r", encoding="ascii", errors="ignore").read()
    body = []
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("-----"):
            continue
        body.append(line)
    if not body:
        raise ValueError("PEM 里没有 Base64：%s" % path)
    return base64.b64decode("".join(body))


def uncompressed_point(der):
    """从 DER 里取出 65 字节未压缩点（0x04 || X || Y）。"""
    # P-256 的 BIT STRING 固定是 03 42 00 04，后面 64 字节是 X 和 Y。
    mark = b"\x03\x42\x00\x04"
    at = der.find(mark)
    if at >= 0:
        point = der[at + 3 : at + 3 + 65]
    else:
        at = der.rfind(b"\x04")
        point = der[at : at + 65]
    if len(point) != 65 or point[0] != 0x04:
        raise ValueError("不是 P-256 未压缩公钥点")
    return point


def st_pubkey_hash(pem_path):
    """按 ST 规则算根公钥哈希。

    丢掉未压缩点开头的 0x04，对 X||Y 共 64 字节做 SHA-256。
    芯片和 Trusted Package Creator 都按这个结果核对根证书。
    """
    point = uncompressed_point(load_spki(pem_path))
    return hashlib.sha256(point[1:]).digest()


def build_obk(pub_hash, permission=PERMISSION, encrypt=ENCRYPT_FLAG):
    """拼 DA obk。

    布局（小端）：
      0  目标地址 0x0FFD0100
      4  数据长度 0x60
      8  加密标记
      12 后 64 字节的 SHA-256
      44 公钥哈希 32 字节
      76 权限掩码 16 字节
      92 保留 16 字节
    """
    if len(pub_hash) != 32:
        raise ValueError("公钥哈希必须是 32 字节")
    # 权限只占掩码低 4 字节，其余为 0，和现有 DA_Config.obk 一致。
    perm = struct.pack("<I", permission & 0xFFFFFFFF) + bytes(PERM_WIDTH - 4)
    payload = pub_hash + perm + bytes(RESERVED_WIDTH)
    if len(payload) != OBK_DATA_SIZE - 32:
        raise ValueError("载荷长度不是 64 字节")
    integrity = hashlib.sha256(payload).digest()
    header = struct.pack("<III", DEST_ADDR, OBK_DATA_SIZE, encrypt)
    return header + integrity + payload


def main():
    """命令行入口。默认用本目录的根公钥，生成后可与现有 obk 对比。"""
    here = __import__("os").path.dirname(__import__("os").path.abspath(__file__))
    default_pem = __import__("os").path.join(
        here, "ROT_Provisioning", "DA", "Keys", "key_1_root_pub.pem"
    )
    parser = argparse.ArgumentParser(description="由 P-256 公钥生成 STM32H573 DA obk")
    parser.add_argument("-InFile", dest="infile", default=default_pem, help="PEM 公钥或私钥")
    parser.add_argument("-OutFile", dest="outfile", required=True, help="输出的 .obk")
    parser.add_argument(
        "-Permission",
        dest="permission",
        default="0x%08X" % PERMISSION,
        help="SOC 权限，默认 0x00005077",
    )
    parser.add_argument(
        "-Encrypt",
        dest="encrypt",
        type=int,
        default=ENCRYPT_FLAG,
        help="加密标记。H573 用 1",
    )
    args = parser.parse_args()
    pub_hash = st_pubkey_hash(args.infile)
    blob = build_obk(pub_hash, int(args.permission, 0), args.encrypt)
    with open(args.outfile, "wb") as out:
        out.write(blob)
    print("PUBHASH=%s" % pub_hash.hex().upper())
    print("SIZE=%d OUT=%s" % (len(blob), args.outfile))


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        sys.exit(str(exc))
