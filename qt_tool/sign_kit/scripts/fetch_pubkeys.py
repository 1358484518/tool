#!/usr/bin/env python3
"""从签名服务导出公钥，落盘为 `sign_kit/keys/<key_id>.pem`。

用途：新增/轮换密钥后，把服务端持有的公钥同步到工程仓库（公钥非敏感，可提交），
作为签名前的**指纹锚点**——签名时客户端会用它与服务端返回的 `pubkey_sha256` 强校验，
不一致立即中止，避免签出板上无法启动的镜像。

用法：
    # 按 key_id 导出（可重复）
    python scripts/fetch_pubkeys.py --key-id DEV-CCU-SPE --key-id DEV-CCU-NS

    # 按配置档导出（读取 remote.conf 里该档的 SPE/NS 映射）
    python scripts/fetch_pubkeys.py --profile DEV-CCU

    # 只预览不改文件
    python scripts/fetch_pubkeys.py --profile OCPP-FW-CCU --dry-run

注意：
- 已存在且内容相同的文件不会改动；内容**不同**时必须显式 `--force` 才会覆盖
  （避免公钥静默变更）。
- 导出后请人工 review 再提交，并与板上 BL2/OTP 的 ROTPK 核对。
"""

from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import remote_sign  # noqa: E402  (同目录模块)

DEFAULT_CONF = SCRIPT_DIR.parent / "remote.conf"
DEFAULT_OUT = SCRIPT_DIR.parent / "keys"


def _pem_from_der(der: bytes) -> str:
    from cryptography.hazmat.primitives import serialization

    public_key = serialization.load_der_public_key(der)
    return public_key.public_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PublicFormat.SubjectPublicKeyInfo,
    ).decode("ascii")


def _resolve_key_ids(conf: dict[str, str], args: argparse.Namespace) -> list[str]:
    key_ids: list[str] = list(args.key_id or [])
    if args.profile:
        spe, ns = remote_sign.resolve_profile(conf, args.profile)
        key_ids.extend([spe, ns])
    # 去重并保序
    seen: set[str] = set()
    ordered: list[str] = []
    for key_id in key_ids:
        if key_id not in seen:
            seen.add(key_id)
            ordered.append(key_id)
    return ordered


def main() -> int:
    parser = argparse.ArgumentParser(
        description="从签名服务导出公钥到 sign_kit/keys/<key_id>.pem",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--key-id", action="append", default=[], help="要导出的 key_id（可重复）")
    parser.add_argument("--profile", help="按 remote.conf 的配置档导出其 SPE/NS 两把公钥")
    parser.add_argument("--conf", default=str(DEFAULT_CONF), help="remote.conf 路径")
    parser.add_argument("--out-dir", default=str(DEFAULT_OUT), help="输出目录（默认 sign_kit/keys）")
    parser.add_argument("--timeout", type=float, help="覆盖请求超时（秒）")
    parser.add_argument("--dry-run", action="store_true", help="只预览，不写文件")
    parser.add_argument("--force", action="store_true", help="允许覆盖内容不同的同名文件")
    args = parser.parse_args()

    if not args.key_id and not args.profile:
        parser.error("必须给出 --key-id 或 --profile")

    try:
        conf = remote_sign.load_conf(args.conf)
    except remote_sign.RemoteSignError as exc:
        print(f"错误: {exc}", file=sys.stderr)
        return 2

    try:
        key_ids = _resolve_key_ids(conf, args)
    except remote_sign.RemoteSignError as exc:
        print(f"错误: {exc}", file=sys.stderr)
        return 2

    try:
        token = remote_sign.resolve_token(conf)
    except remote_sign.RemoteSignError as exc:
        print(f"错误: {exc}", file=sys.stderr)
        return 2

    out_dir = Path(args.out_dir)
    timeout = float(args.timeout or conf.get("TIMEOUT") or remote_sign.DEFAULT_TIMEOUT)

    failures = 0
    for key_id in key_ids:
        signer = remote_sign.RemoteSigner(
            server_url=conf.get("SIGN_SERVER_URL", ""),
            key_id=key_id,
            token=token,
            ca_bundle=conf.get("CA_BUNDLE") or None,
            timeout=timeout,
            allow_insecure_http=str(conf.get("ALLOW_INSECURE_HTTP", "")).strip()
            in ("1", "on", "true", "yes"),
        )
        try:
            der, fingerprint = signer.fetch_pubkey()
        except remote_sign.RemoteSignError as exc:
            failures += 1
            print(f"错误: {key_id}: {exc}", file=sys.stderr)
            continue

        pem = _pem_from_der(der)
        recomputed = hashlib.sha256(der).hexdigest()
        if recomputed != fingerprint:
            print(
                f"错误: {key_id}: 服务端指纹与本地重算不一致（{fingerprint} != {recomputed}）",
                file=sys.stderr,
            )
            failures += 1
            continue

        dest = out_dir / f"{key_id}.pem"
        old = dest.read_text(encoding="utf-8") if dest.is_file() else None

        if old == pem:
            print(f"[不变] {dest}  (pubkey_sha256={fingerprint})")
            continue

        if old is not None and not args.force:
            failures += 1
            print(
                f"错误: {key_id}: {dest} 已存在且内容与服务端公钥不同。\n"
                f"  本地文件指纹: {hashlib.sha256(_spki_of(old)).hexdigest()}\n"
                f"  服务端指纹  : {fingerprint}\n"
                "  这通常意味着密钥被轮换，或本地文件是错的。请确认后加 --force 覆盖。",
                file=sys.stderr,
            )
            continue

        if args.dry_run:
            print(f"[预演] 将写入 {dest}  (pubkey_sha256={fingerprint})")
            continue

        out_dir.mkdir(parents=True, exist_ok=True)
        dest.write_text(pem, encoding="utf-8")
        print(f"[写入] {dest}  (pubkey_sha256={fingerprint})")

    print()
    print(">>> 导出完成。请：")
    print("    1) 人工 review 后提交 sign_kit/keys/<key_id>.pem（公钥非敏感，应当入库）")
    print("    2) 与板上 BL2/OTP 的 ROTPK 核对（迁移现有 DEV-CCU 密钥时应与改动前一致）")
    print("    3) 若为新密钥，在 remote.conf 中补充对应的配置档映射")
    return 1 if failures else 0


def _spki_of(pem_text: str) -> bytes:
    """PEM -> SPKI DER（用于给出本地文件指纹）。"""
    from cryptography.hazmat.primitives import serialization

    public_key = serialization.load_pem_public_key(pem_text.encode("utf-8"))
    return public_key.public_bytes(
        serialization.Encoding.DER,
        serialization.PublicFormat.SubjectPublicKeyInfo,
    )


if __name__ == "__main__":
    raise SystemExit(main())
