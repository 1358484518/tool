#!/usr/bin/env python3
# 用 bl2.bin 里的程序，加上默认 OTP / 计数初值，生成和 objcopy 相同的 bl2.hex。
# OTP 里的 ROTPK 不从 bin 抄，而是用传入公钥的 SHA-256 现算。
# 传入仓库当前的 root-EC-P256.pem 和 root-EC-P256_1.pem 时，结果与编译出的 bl2.hex 一致。
# 段长和入口仍要 bl2.elf：bin 里看不出 objcopy 在节尾切断记录的位置。

import argparse
import hashlib
import struct
import sys
from pathlib import Path

try:
    from cryptography.hazmat.backends import default_backend
    from cryptography.hazmat.primitives import serialization
except ImportError as exc:
    print("错误: 需要 cryptography。pip install cryptography", file=sys.stderr)
    raise SystemExit(2) from exc

# 默认密钥仍指向仓库里的 root-EC-P256。单独运行时用 --key-s / --key-ns 传入即可。
REPO_ROOT = Path(__file__).resolve().parents[1]

ELF_MAGIC = b"\x7fELF"
ELFCLASS32 = 1
ELFDATA2LSB = 1
ET_EXEC = 2
SHT_NOBITS = 8
SHF_ALLOC = 0x2
PT_LOAD = 1

# Intel HEX 记录类型，和 arm-none-eabi-objcopy -O ihex 相同。
IHEX_DATA = 0x00
IHEX_EOF = 0x01
IHEX_EXT_LINEAR = 0x04
IHEX_START_LINEAR = 0x05
IHEX_RECORD_BYTES = 16

# 这三节不是程序，脚本按默认值重写。
PROVISION_SECTIONS = {".BL2_OTP", ".BL2_NVMCNT", ".BL2_NVM"}

# flash_otp_nv_counters_backend.h / otp_provision.c 里的魔数和生命周期。
OTP_INIT = 0xC0DE8112
NV_INIT = 0xC0DE0042
LCS_SECURED = 0x3
# psa/crypto_values.h 的 PSA_ECC_FAMILY_SECP_R1。
IAK_TYPE_P256 = 0x12
ROTPK_LEN = 32


# 读 PEM。公钥直接用，私钥先取出对应公钥。给 convert 算 ROTPK 用。
def load_public_key(path):
    data = Path(path).read_bytes()
    try:
        return serialization.load_pem_public_key(data, backend=default_backend())
    except ValueError:
        pass
    try:
        priv = serialization.load_pem_private_key(data, password=None, backend=default_backend())
    except ValueError as exc:
        raise SystemExit(f"错误: {path} 不是 PEM 公钥或私钥") from exc
    return priv.public_key()


# EC 对 SubjectPublicKeyInfo 做 SHA-256/384，RSA 对 PKCS#1 公钥做 SHA-256。和 OTP 里的 ROTPK 相同。
def pubkey_hash_bytes(pub, sig_type):
    if sig_type.startswith("EC-P"):
        der = pub.public_bytes(
            serialization.Encoding.DER,
            serialization.PublicFormat.SubjectPublicKeyInfo,
        )
        if sig_type == "EC-P384":
            return hashlib.sha384(der).digest()
        return hashlib.sha256(der).digest()
    if sig_type.startswith("RSA-"):
        der = pub.public_bytes(
            serialization.Encoding.DER,
            serialization.PublicFormat.PKCS1,
        )
        return hashlib.sha256(der).digest()
    raise SystemExit(f"错误: 不支持的签名类型 {sig_type}")


# 读 ELF32 的入口、LOAD 段和已分配节。节地址是运行地址，加载地址要再换算。
def load_elf32(path):
    data = Path(path).read_bytes()
    if data[:4] != ELF_MAGIC or data[4] != ELFCLASS32 or data[5] != ELFDATA2LSB:
        raise SystemExit(f"错误: {path} 不是 ELF32 小端文件")
    e_type, _, _, entry, phoff, shoff, _, _, phentsize, phnum, shentsize, shnum, shstrndx = struct.unpack_from(
        "<HHIIIIIHHHHHH", data, 16
    )
    if e_type != ET_EXEC or phentsize != 32 or shentsize != 40:
        raise SystemExit(f"错误: {path} 不是 ELF32 可执行文件")
    loads = []
    for i in range(phnum):
        off = phoff + i * phentsize
        p_type, _, p_vaddr, p_paddr, _, p_memsz, _, _ = struct.unpack_from("<IIIIIIII", data, off)
        if p_type == PT_LOAD and p_memsz:
            loads.append((p_vaddr, p_paddr, p_memsz))
    shstr = shoff + shstrndx * shentsize
    _, _, _, _, str_off, str_size, _, _, _, _ = struct.unpack_from("<IIIIIIIIII", data, shstr)
    names = data[str_off:str_off + str_size]
    sections = []
    for i in range(shnum):
        off = shoff + i * shentsize
        sh_name, sh_type, sh_flags, sh_addr, _, sh_size, _, _, _, _ = struct.unpack_from(
            "<IIIIIIIIII", data, off
        )
        if sh_type == SHT_NOBITS or sh_size == 0 or not (sh_flags & SHF_ALLOC):
            continue
        end = names.find(b"\x00", sh_name)
        name = names[sh_name:end].decode("ascii", "replace")
        sections.append((name, sh_addr, sh_size))
    return entry, loads, sections


# 运行地址换成 Flash 加载地址。.data 的运行地址在 SRAM，hex 里必须写 LMA。
def section_lma(loads, vma):
    for p_vaddr, p_paddr, p_memsz in loads:
        if p_vaddr <= vma < p_vaddr + p_memsz:
            return p_paddr + (vma - p_vaddr)
    return None


def u32(value):
    return struct.pack("<I", value)


def pad(data, size):
    raw = data.encode("ascii") if isinstance(data, str) else data
    if len(raw) > size:
        raise SystemExit(f"错误: 字段超过 {size} 字节")
    return raw + bytes(size - len(raw))


def c_string(text, size):
    # C 字符串会补一个 0，剩余字节也是 0。
    return pad(text.encode("ascii") + b"\x00", size)


# 默认 OTP，布局与 otp_provision.c 的 flash_otp_nv_counters_region_t 一致，共 772 字节。
# rotpk_0 用 S 公钥哈希，rotpk_1 和 rotpk_2 用 NS 公钥哈希，和 otp_rotpk_hashes.inc 相同。
def build_otp(rotpk_s, rotpk_ns):
    if len(rotpk_s) != ROTPK_LEN or len(rotpk_ns) != ROTPK_LEN:
        raise SystemExit("错误: EC-P256 公钥哈希必须是 32 字节")
    huk = bytes([
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    ])
    boot_seed = bytes(range(0xA0, 0xC0))
    implementation_id = pad("TMX-AC", 6) + bytes.fromhex(
        "4E91C3176AD802B57CE43950AB168F21"
        "D0639E44F20B78C6153A"
    )
    entropy = bytes.fromhex(
        "121323340A058978A3668C0D975553CAB576186229C6B67975C85A8D9E118F85"
        "DEC45F662152F939D9779328B05E02FA58B416C80F3891BB2817CD8AC9537266"
    )
    body = b"".join([
        u32(OTP_INIT),
        huk,
        bytes([0xFF] * 32),          # IAK 空槽，第一次启动再写成 P-256 私钥
        u32(32),                     # iak_len
        u32(IAK_TYPE_P256),
        pad("stm.example.xcube!", 32),
        boot_seed,
        u32(LCS_SECURED),
        implementation_id,
        pad("060456527282910010", 32),
        c_string("chargers.evselink.top", 32),
        c_string("PSA_IOT_PROFILE_1", 32),
        rotpk_s,
        rotpk_ns,
        bytes(64 * 4),               # bl2_nv_counter_0..3
        rotpk_ns,                    # bl2_rotpk_2，与 NS 密钥相同
        bytes(32),                   # bl2_rotpk_3 留空
        u32(0),                      # bl2_rotpk_policies
        entropy,
        bytes(32),                   # secure_debug_pk 留空
        u32(0) + u32(0) + u32(0),    # flash_nv_counters
        u32(1),                      # swap_count
    ])
    if len(body) != 772:
        raise SystemExit(f"错误: OTP 长度 {len(body)}，应为 772")
    return body


# nv_counters.h：checksum 和 init_value 都是 0xC0DE0042，6 个计数为 0，共 32 字节。
def build_nv_counters():
    return u32(NV_INIT) + u32(NV_INIT) + bytes(24)


# 组一条 Intel HEX。addr 是记录内的 16 位地址。
def ihex_record(rec_type, addr, payload):
    body = bytes((len(payload), (addr >> 8) & 0xFF, addr & 0xFF, rec_type)) + payload
    checksum = (-sum(body)) & 0xFF
    return ":" + body.hex().upper() + f"{checksum:02X}"


# 16 字节一条，不跨节，也不跨 64KB。换窗口时先写 04 记录。行尾用 CR LF，和 objcopy 相同。
def sections_to_ihex(chunks, entry):
    lines = []
    upper = None
    for lma, blob in chunks:
        pos = 0
        while pos < len(blob):
            addr = lma + pos
            hi = addr >> 16
            if hi != upper:
                lines.append(ihex_record(IHEX_EXT_LINEAR, 0, hi.to_bytes(2, "big")))
                upper = hi
            room = 0x10000 - (addr & 0xFFFF)
            size = min(IHEX_RECORD_BYTES, len(blob) - pos, room)
            lines.append(ihex_record(IHEX_DATA, addr & 0xFFFF, blob[pos:pos + size]))
            pos += size
    lines.append(ihex_record(IHEX_START_LINEAR, 0, entry.to_bytes(4, "big")))
    lines.append(ihex_record(IHEX_EOF, 0, b""))
    return ("\r\n".join(lines) + "\r\n").encode("ascii")


# 从 bin 取出程序节。完整的编译 bin 从最低加载地址铺开；只含程序的 bin 从代码段起始铺开。
def program_bytes(blob, lma, size, full_base, prog_base, full_size, prog_size):
    if len(blob) == full_size:
        off = lma - full_base
    elif len(blob) == prog_size:
        off = lma - prog_base
    else:
        raise SystemExit(
            f"错误: bl2.bin 长度 {len(blob)}。完整编译产物应为 {full_size}，"
            f"只含程序应为 {prog_size}。"
        )
    return blob[off:off + size]


def convert(bin_path, elf_path, key_s, key_ns, sig_type):
    image = Path(bin_path).read_bytes()
    entry, loads, sections = load_elf32(elf_path)
    placed = []
    for name, vma, size in sections:
        lma = section_lma(loads, vma)
        if lma is None:
            raise SystemExit(f"错误: 节 {name} 地址 0x{vma:08X} 不在任何 LOAD 段里")
        placed.append((lma, size, name))
    placed.sort()
    full_base = placed[0][0]
    full_end = max(lma + size for lma, size, _ in placed)
    program = [item for item in placed if item[2] not in PROVISION_SECTIONS]
    if not program:
        raise SystemExit("错误: ELF 里没有程序节")
    prog_base = program[0][0]
    prog_end = max(lma + size for lma, size, _ in program)
    rotpk_s = pubkey_hash_bytes(load_public_key(Path(key_s)), sig_type)
    rotpk_ns = pubkey_hash_bytes(load_public_key(Path(key_ns)), sig_type)
    otp = build_otp(rotpk_s, rotpk_ns)
    nv = build_nv_counters()
    provision = {
        ".BL2_OTP": otp,
        ".BL2_NVMCNT": nv,
        ".BL2_NVM": nv,
    }
    chunks = []
    for lma, size, name in placed:
        if name in provision:
            blob = provision[name]
            if len(blob) != size:
                raise SystemExit(f"错误: {name} 默认长度 {len(blob)}，ELF 中是 {size}")
            print(f"{name}  由脚本生成  LMA 0x{lma:08X}  {size} 字节", file=sys.stderr)
        else:
            blob = program_bytes(
                image, lma, size, full_base, prog_base, full_end - full_base, prog_end - prog_base
            )
            print(f"{name}  来自 bin    LMA 0x{lma:08X}  {size} 字节", file=sys.stderr)
        chunks.append((lma, blob))
    print(f"ROTPK-S  {rotpk_s.hex()}", file=sys.stderr)
    print(f"ROTPK-NS {rotpk_ns.hex()}", file=sys.stderr)
    print(f"入口 0x{entry:08X}", file=sys.stderr)
    return sections_to_ihex(chunks, entry)


def main():
    key_dir = REPO_ROOT / "trusted-firmware-m" / "bl2" / "ext" / "mcuboot"
    parser = argparse.ArgumentParser(
        description="用程序 bin 和公钥生成与 objcopy 一致的 bl2.hex"
    )
    parser.add_argument("bin", help="bl2.bin。可以是完整编译产物，或只含程序的那一段")
    parser.add_argument("--elf", help="同一次编译的 bl2.elf。省略时用 bin 同目录的 bl2.elf")
    parser.add_argument("--key-s", type=Path, default=key_dir / "root-EC-P256.pem",
                        help="S 镜像公钥或私钥，写入 bl2_rotpk_0")
    parser.add_argument("--key-ns", type=Path, default=key_dir / "root-EC-P256_1.pem",
                        help="NS 镜像公钥或私钥，写入 bl2_rotpk_1 和 bl2_rotpk_2")
    parser.add_argument("--sig-type", default="EC-P256", help="EC-P256 / EC-P384 / RSA-2048 / RSA-3072")
    parser.add_argument("-o", "--output", help="输出 bl2.hex。省略则写到标准输出")
    args = parser.parse_args()
    bin_path = Path(args.bin)
    elf_path = Path(args.elf) if args.elf else bin_path.with_suffix(".elf")
    if not elf_path.is_file():
        raise SystemExit(f"错误: 找不到 {elf_path}。节边界和入口在 elf 里")
    for key in (args.key_s, args.key_ns):
        if not key.is_file():
            raise SystemExit(f"错误: 找不到密钥 {key}")
    text = convert(bin_path, elf_path, args.key_s, args.key_ns, args.sig_type)
    if args.output:
        Path(args.output).write_bytes(text)
        print(f"已写入 {args.output}  {len(text)} 字节", file=sys.stderr)
    else:
        sys.stdout.buffer.write(text)


if __name__ == "__main__":
    main()
