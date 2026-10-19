#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 STM32 DA 用的 ECDSA P-256 密钥对。

私钥是 PKCS#8（BEGIN PRIVATE KEY），公钥是 SubjectPublicKeyInfo（BEGIN PUBLIC KEY）。
和 ROT_Provisioning/DA/Keys 里的 key_1_root.pem、key_1_root_pub.pem 同一格式。
gen_da_obk.py 可以直接吃这里写出的公钥。

用法：
  python gen_p256_keypair.py
  python gen_p256_keypair.py -Name key_1_root -OutDir D:\\keys
"""
import argparse
import os
import sys

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec


def generate_pair():
    """生成一对 P-256 密钥，返回 (私钥 PEM, 公钥 PEM)。"""
    private_key = ec.generate_private_key(ec.SECP256R1())
    private_pem = private_key.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.PKCS8,
        encryption_algorithm=serialization.NoEncryption(),
    )
    public_pem = private_key.public_key().public_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PublicFormat.SubjectPublicKeyInfo,
    )
    return private_pem, public_pem


def write_new(path, data):
    """只在文件不存在时写入。已存在就停，避免盖掉原来的私钥。"""
    if os.path.exists(path):
        raise FileExistsError(path)
    with open(path, "wb") as handle:
        handle.write(data)


def main():
    parser = argparse.ArgumentParser(description="生成 ECDSA P-256 私钥和公钥 PEM")
    parser.add_argument("-Name", dest="name", default="key", help="文件名前缀，默认 key")
    parser.add_argument(
        "-OutDir",
        dest="outdir",
        default=os.path.dirname(os.path.abspath(__file__)),
        help="输出目录，默认脚本所在目录",
    )
    parser.add_argument("-Force", dest="force", action="store_true", help="已有同名文件时覆盖")
    args = parser.parse_args()

    outdir = os.path.abspath(args.outdir)
    os.makedirs(outdir, exist_ok=True)
    private_path = os.path.join(outdir, args.name + ".pem")
    public_path = os.path.join(outdir, args.name + "_pub.pem")
    private_pem, public_pem = generate_pair()
    if args.force:
        with open(private_path, "wb") as handle:
            handle.write(private_pem)
        with open(public_path, "wb") as handle:
            handle.write(public_pem)
    else:
        write_new(private_path, private_pem)
        write_new(public_path, public_pem)
    print("PRIVATE=%s" % private_path)
    print("PUBLIC=%s" % public_path)


if __name__ == "__main__":
    try:
        main()
    except FileExistsError as exc:
        sys.exit("文件已存在，不覆盖：%s" % exc)
    except Exception as exc:
        sys.exit(str(exc))
