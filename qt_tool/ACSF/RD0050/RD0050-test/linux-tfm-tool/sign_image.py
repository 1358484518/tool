#!/usr/bin/env python3
# 给未签名的 tfm_s.bin / tfm_ns.bin 做 MCUboot 签名。
# 本文件用于 G0、RD0047、RAT。这三支的 BL2 按 OVERWRITE_ONLY 编译，所以带 --overwrite-only。
# 签名实现在旁边的 mcuboot_sign（从编译用的 imgtool 拷出），本文件只组参数。
# Windows 和 Linux 都用 python 运行，不依赖 bash 或 bat。

import argparse
import importlib.util
import os
import sys
from pathlib import Path

# 和当前 BL2 编译选项一致：头 0x400、16 字节对齐、清单里放完整公钥。
HEADER_SIZE = "0x400"
ALIGN = "16"
ENC_KEY_LEN = "128"
PUBLIC_KEY_FORMAT = "full"

TOOL_DIR = Path(__file__).resolve().parent
REPO_ROOT = TOOL_DIR.parent
SIGN_ROOT = TOOL_DIR / "mcuboot_sign"
SCRIPTS_DIR = SIGN_ROOT / "scripts"
WRAPPER_PY = SCRIPTS_DIR / "wrapper.py"

# 槽大小写在这两个 .o 的 RE_SIGN_BIN_SIZE 里。文件名里的 _s / _ns 决定 measured boot 记录类型。
LAYOUT_S = SIGN_ROOT / "layout" / "signing_layout_s.o"
LAYOUT_NS = SIGN_ROOT / "layout" / "signing_layout_ns.o"

# 仓库默认私钥。keys/ 里放了同名用户密钥时优先用用户的。
DEFAULT_KEY_S = REPO_ROOT / "trusted-firmware-m" / "bl2" / "ext" / "mcuboot" / "root-EC-P256.pem"
DEFAULT_KEY_NS = REPO_ROOT / "trusted-firmware-m" / "bl2" / "ext" / "mcuboot" / "root-EC-P256_1.pem"
USER_KEY_S = REPO_ROOT / "keys" / "image_s_signing_private_key.pem"
USER_KEY_NS = REPO_ROOT / "keys" / "image_ns_signing_private_key.pem"

# 四个分支的 S 镜像版本默认都是 2.3.1。要改时用 --version。
DEFAULT_VERSION_S = "2.3.1"
# NS 镜像版本、安全计数器、依赖的最低版本。和各分支 versions/config 里除 S 版本以外的项相同。
DEFAULT_VERSION_NS = "0.0.0"
DEFAULT_SEC_CNT = "1"
DEFAULT_MIN_VER = "0.0.0+0"


# 从文件名判断是 NS 还是 S。先匹配 ns，避免 tfm_ns.bin 被当成 S。
def guess_kind(name):
    low = name.lower()
    if "ns" in low:
        return "ns"
    if "sapp" in low or "tfm_s" in low or low.endswith("_s.bin") or "_s_" in low:
        return "s"
    return ""


# 相对路径先按当前目录找，找不到再按脚本所在目录找。
def resolve_input(name):
    path = Path(name)
    if path.is_file():
        return path.resolve()
    alt = TOOL_DIR / name
    if alt.is_file():
        return alt.resolve()
    raise SystemExit(f"错误: 找不到文件 {name}")


# 用户 keys/ 优先，否则用 BL2 目录里的 root-EC-P256 私钥。
def default_key(kind):
    if kind == "s":
        return USER_KEY_S if USER_KEY_S.is_file() else DEFAULT_KEY_S
    return USER_KEY_NS if USER_KEY_NS.is_file() else DEFAULT_KEY_NS


# 按编译时 mcuboot_imagesign_wrapper 的参数组命令行。S 依赖 NS，NS 依赖 S。
def build_argv(kind, infile, outfile, key, version, counter, min_ver):
    if kind == "s":
        layout = LAYOUT_S
        ver = version or DEFAULT_VERSION_S
        sec = counter or DEFAULT_SEC_CNT
        dep_ver = min_ver or DEFAULT_MIN_VER
        # image_id 1 是 NS。
        dep = f"(1, {dep_ver})"
    else:
        layout = LAYOUT_NS
        ver = version or DEFAULT_VERSION_NS
        sec = counter or DEFAULT_SEC_CNT
        dep_ver = min_ver or DEFAULT_MIN_VER
        # image_id 0 是 S。
        dep = f"(0, {dep_ver})"
    argv = [
        str(WRAPPER_PY),
        "--version", ver,
        "--layout", str(layout),
        "--key", str(key),
        "--public-key-format", PUBLIC_KEY_FORMAT,
        "--align", ALIGN,
        "--pad",
        "--pad-header",
        "-H", HEADER_SIZE,
        "-s", str(sec),
        "-L", ENC_KEY_LEN,
        "-d", dep,
        "--overwrite-only",
        "--measured-boot-record",
        str(infile),
        str(outfile),
    ]
    return argv, ver, sec


# 先切到 scripts 再加载 wrapper.py，这样用的是拷出来的 imgtool，不是系统里的。
def run_imgtool(argv):
    os.chdir(SCRIPTS_DIR)
    sys.path.insert(0, str(SIGN_ROOT))
    spec = importlib.util.spec_from_file_location("mcuboot_imagesign_wrapper", WRAPPER_PY)
    if spec is None or spec.loader is None:
        raise SystemExit(f"错误: 无法加载 {WRAPPER_PY}")
    mod = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(mod)
    except ImportError as exc:
        raise SystemExit(
            "错误: 缺少 Python 依赖 click、cryptography、cbor2、intelhex。\n"
            f"  {exc}"
        ) from exc
    sys.argv = argv
    # wrapper.main() 成功时会 sys.exit(0)，接住后才能打印完成。
    try:
        mod.main()
    except SystemExit as exc:
        if exc.code not in (0, None):
            raise


# 命令行入口。s/ns 可以写在文件名前，也可以从文件名推断。
def main():
    raw = sys.argv[1:]
    kind = ""
    if raw and raw[0].lower() in ("s", "ns"):
        kind = raw[0].lower()
        raw = raw[1:]

    parser = argparse.ArgumentParser(
        description="用拷出来的 imgtool 给 TF-M 的 S / NS bin 签名。Windows 和 Linux 都用 python 运行。"
    )
    parser.add_argument("bin", help="未签名的 bin")
    parser.add_argument("-o", "--output", help="签名后的 bin。默认和输入同目录，文件名加 _signed")
    parser.add_argument("-k", "--key", help="私钥 PEM。不传则用 keys/，没有再用仓库 root-EC-P256")
    parser.add_argument("--version", help="镜像版本。S 默认 2.3.1，NS 默认 0.0.0")
    parser.add_argument("--security-counter", help="安全计数器，默认 1")
    parser.add_argument("--min-version", help="依赖的另一侧镜像最低版本")
    args = parser.parse_args(raw)

    infile = resolve_input(args.bin)
    kind = kind or guess_kind(infile.name)
    if kind not in ("s", "ns"):
        raise SystemExit(f"错误: 无法从文件名判断是 S 还是 NS，请写成: python sign_image.py s {infile.name}")

    key = Path(args.key).resolve() if args.key else default_key(kind)
    if not key.is_file():
        raise SystemExit(f"错误: 找不到私钥 {key}")
    for need in (WRAPPER_PY, LAYOUT_S, LAYOUT_NS, SCRIPTS_DIR / "imgtool" / "image.py"):
        if not need.is_file():
            raise SystemExit(f"错误: 缺少签名文件 {need}")

    # 输出路径在 chdir 之前定下来，避免相对路径被带到 scripts 目录。
    if args.output:
        outfile = Path(args.output)
        if not outfile.is_absolute():
            outfile = (Path.cwd() / outfile).resolve()
    else:
        outfile = infile.with_name(infile.stem + "_signed.bin")
    outfile.parent.mkdir(parents=True, exist_ok=True)

    argv, ver, sec = build_argv(
        kind, infile, outfile, key, args.version, args.security_counter, args.min_version
    )
    slot = "S 512KB overwrite-only" if kind == "s" else "NS 1024KB overwrite-only"
    print(f"签名 {kind.upper()} 镜像 ({slot})")
    print(f"  输入  {infile}")
    print(f"  输出  {outfile}")
    print(f"  密钥  {key}")
    print(f"  版本  {ver}  header {HEADER_SIZE}  align {ALIGN}  计数器 {sec}")
    run_imgtool(argv)
    print(f"完成: {outfile}")


if __name__ == "__main__":
    main()
