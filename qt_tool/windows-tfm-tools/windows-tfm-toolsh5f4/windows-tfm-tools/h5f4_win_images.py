#!/usr/bin/env python3
"""Locate and verify STM32H5F4 TF-M images for the Windows flash bats.

BL2 must contain H5F4BL2 and H5F4SWP2. Old H573 hex in this folder does not.
Intel HEX 0x0Cxxxxxx records are remapped to 0x08xxxxxx across the full 4 MB.
"""
from __future__ import annotations

import os
import sys
import tempfile

BL2_MARKERS = (b"H5F4BL2", b"H5F4SWP2")
FLASH_NS_BASE = 0x08000000
FLASH_S_BASE = 0x0C000000
FLASH_BYTES = 4 * 1024 * 1024
FLASH_S_END = FLASH_S_BASE + FLASH_BYTES
REQUIRED_SLOTS = {
    "boot": "0xc00e000",
    "slot0": "0xc078000",
    "slot1": "0xc0f8000",
    "slot2": "0xc200000",
    "slot3": "0xc280000",
}


def repo_root():
    return os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


def hex_records(path):
    ela = 0
    with open(path, "r", encoding="ascii", errors="ignore") as f:
        for raw in f:
            t = raw.strip()
            if not t.startswith(":") or len(t) < 11:
                continue
            length = int(t[1:3], 16)
            offset = int(t[3:7], 16)
            typ = int(t[7:9], 16)
            if typ == 4:
                ela = int(t[9:13], 16)
                continue
            if typ != 0:
                continue
            addr = (ela << 16) + offset
            data = bytes(int(t[9 + 2 * i : 11 + 2 * i], 16) for i in range(length))
            yield addr, data


def remap_secure_alias(addr):
    if FLASH_S_BASE <= addr < FLASH_S_END:
        return addr - (FLASH_S_BASE - FLASH_NS_BASE)
    return addr


def decode_image_bytes(path):
    lower = path.lower()
    if lower.endswith(".hex"):
        recs = list(hex_records(path))
        if not recs:
            return b""
        min_a = min(a for a, _ in recs)
        max_a = max(a + len(d) - 1 for a, d in recs)
        size = max_a - min_a + 1
        if size <= 0 or size > FLASH_BYTES:
            recs.sort(key=lambda x: x[0])
            return b"".join(d for _, d in recs)
        blob = bytearray(size)
        for addr, data in recs:
            off = addr - min_a
            blob[off : off + len(data)] = data
        return bytes(blob)
    with open(path, "rb") as f:
        return f.read()


def bl2_marker_error(path):
    try:
        blob = decode_image_bytes(path)
    except OSError as exc:
        return "cannot read %s: %s" % (path, exc)
    missing = [m.decode("ascii") for m in BL2_MARKERS if m not in blob]
    if missing:
        return "%s missing %s (old H573 image or wrong file)" % (
            path,
            ", ".join(missing),
        )
    return None


def parse_update_slots(path):
    slots = {}
    with open(path, "r", encoding="ascii", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if "=" not in line:
                continue
            key, val = line.split("=", 1)
            if key in REQUIRED_SLOTS:
                slots[key] = val.strip()
    return slots


def update_sh_error(path):
    slots = parse_update_slots(path)
    bad = []
    for key, want in REQUIRED_SLOTS.items():
        got = slots.get(key)
        if got != want:
            bad.append("%s=%s (want %s)" % (key, got or "<missing>", want))
    if bad:
        return "%s flash map is not H5F4: %s" % (path, "; ".join(bad))
    return None


def search_dirs(extra_cwd=None):
    script = os.path.dirname(os.path.abspath(__file__))
    cwd = os.path.abspath(extra_cwd or os.getcwd())
    dirs = [
        cwd,
        script,
        os.path.join(script, "build_s"),
        os.path.join(script, "build_s", "bin"),
        os.path.join(script, "build_s", "api_ns", "bin"),
        os.path.join(script, "build_ns"),
        os.path.join(script, "build_ns", "bin"),
    ]
    seen = set()
    out = []
    for d in dirs:
        ap = os.path.abspath(d)
        if ap in seen:
            continue
        seen.add(ap)
        out.append(ap)
    return out


def first_existing(dirs, names):
    for d in dirs:
        for name in names:
            p = os.path.join(d, name)
            if os.path.isfile(p):
                yield p


def locate(extra_cwd=None):
    dirs = search_dirs(extra_cwd)
    info = {
        "STATUS": "OK",
        "ERROR": "",
        "BL2": "",
        "S_SIGNED": "",
        "S_NS_SIGNED": "",
        "NS_SIGNED": "",
        "UPDATE_SH": "",
        "REPO": repo_root(),
    }

    for cand in first_existing(dirs, ("bl2.bin",)):
        info["BL2"] = cand
        break
    if not info["BL2"]:
        for cand in first_existing(dirs, ("bl2.hex",)):
            info["BL2"] = cand
            break
    if not info["BL2"]:
        info["STATUS"] = "FAIL"
        info["ERROR"] = "bl2.bin / bl2.hex not found in cwd or windows-tfm-tools (build_s/build_ns)"
        return info

    for cand in first_existing(dirs, ("tfm_s_signed.bin",)):
        info["S_SIGNED"] = cand
        break
    if not info["S_SIGNED"]:
        for cand in first_existing(dirs, ("tfm_s_signed.hex",)):
            info["S_SIGNED"] = cand
            break
    for cand in first_existing(dirs, ("tfm_s_ns_signed.bin",)):
        info["S_NS_SIGNED"] = cand
        break
    if not info["S_NS_SIGNED"]:
        for cand in first_existing(dirs, ("tfm_s_ns_signed.hex",)):
            info["S_NS_SIGNED"] = cand
            break
    for cand in first_existing(dirs, ("tfm_ns_signed.bin",)):
        info["NS_SIGNED"] = cand
        break
    if not info["NS_SIGNED"]:
        for cand in first_existing(dirs, ("tfm_ns_signed.hex",)):
            info["NS_SIGNED"] = cand
            break

    have_s = bool(info["S_SIGNED"] or info["S_NS_SIGNED"])
    have_ns = bool(info["NS_SIGNED"] or info["S_NS_SIGNED"])
    if not have_s:
        info["STATUS"] = "FAIL"
        info["ERROR"] = "no tfm_s_signed.bin/.hex and no tfm_s_ns_signed.bin/.hex"
        return info
    if not have_ns:
        info["STATUS"] = "FAIL"
        info["ERROR"] = "no tfm_ns_signed.bin/.hex and no tfm_s_ns_signed.bin/.hex"
        return info
    return info


def print_locate(info):
    for key in (
        "STATUS",
        "ERROR",
        "REPO",
        "BL2",
        "S_SIGNED",
        "S_NS_SIGNED",
        "NS_SIGNED",
        "UPDATE_SH",
    ):
        print("%s=%s" % (key, info.get(key, "")))


def hex_to_bin(infile, outfile, remap=False):
    recs = list(hex_records(infile))
    if remap:
        recs = [(remap_secure_alias(a), d) for a, d in recs]
    if not recs:
        sys.exit("No data records in %s" % infile)
    min_a = min(a for a, d in recs)
    max_a = max(a + len(d) - 1 for a, d in recs)
    size = max_a - min_a + 1
    if size <= 0 or size > FLASH_BYTES:
        sys.exit("HEX span 0x%08X-0x%08X size=%d too large" % (min_a, max_a, size))
    if remap and FLASH_S_BASE <= min_a < FLASH_S_END:
        sys.exit("converted address still 0x0C: 0x%08X" % min_a)
    blob = bytearray([0xFF]) * size
    for addr, data in recs:
        off = addr - min_a
        blob[off : off + len(data)] = data
    with open(outfile, "wb") as out:
        out.write(blob)
    with open(outfile + ".addr", "w", encoding="ascii") as out:
        out.write("0x%08X\n" % min_a)
    print("LOAD=0x%08X SIZE=%d" % (min_a, size))


def hex_to_ns_bin(infile, outfile):
    hex_to_bin(infile, outfile, remap=True)


def cmd_self_test():
    # Bank 2 NS secondary 0x0C258000 must remap (old script stopped at 0x0C200000).
    recs = [
        (0x0C00E000, b"\x11"),
        (0x0C258000, b"\x22"),
        (0x0C3FFFF0, b"\x33"),
        (0x08078000, b"\x44"),
    ]
    mapped = [remap_secure_alias(a) for a, _ in recs]
    assert mapped == [0x0800E000, 0x08258000, 0x083FFFF0, 0x08078000], mapped
    assert remap_secure_alias(0x0C400000) == 0x0C400000

    with tempfile.TemporaryDirectory() as td:
        good = os.path.join(td, "bl2.bin")
        with open(good, "wb") as f:
            f.write(b"pad H5F4BL2 pad H5F4SWP2")
        assert bl2_marker_error(good) is None

        bad = os.path.join(td, "old.bin")
        with open(bad, "wb") as f:
            f.write(b"Starting bootloader")
        err = bl2_marker_error(bad)
        assert err and "H5F4BL2" in err, err

        hex_path = os.path.join(td, "bank2.hex")
        # type 04 ELA 0x0C25, offset 0x8000 -> 0x0C258000, one data byte 0xA5
        rec = bytearray.fromhex("0108000000A5")
        ela = bytearray.fromhex("020000040C25")
        def csum(payload):
            s = (-sum(payload)) & 0xFF
            return payload + bytes([s])
        # :02 0000 04 0C25 checksum
        ela_pl = bytes([0x02, 0x00, 0x00, 0x04, 0x0C, 0x25])
        data_pl = bytes([0x01, 0x80, 0x00, 0x00, 0xA5])
        with open(hex_path, "w", encoding="ascii") as f:
            f.write(":" + csum(ela_pl).hex().upper() + "\n")
            f.write(":" + csum(data_pl).hex().upper() + "\n")
            f.write(":00000001FF\n")
        out_bin = os.path.join(td, "out.bin")
        hex_to_ns_bin(hex_path, out_bin)
        with open(out_bin + ".addr", encoding="ascii") as f:
            addr = f.read().strip()
        assert addr.lower() == "0x08258000", addr
        with open(out_bin, "rb") as f:
            assert f.read() == b"\xa5"
        out_plain = os.path.join(td, "plain.bin")
        hex_to_bin(hex_path, out_plain, remap=False)
        with open(out_plain + ".addr", encoding="ascii") as f:
            addr = f.read().strip()
        assert addr.lower() == "0x0c258000", addr
        rc = main(["hex2bin", hex_path, os.path.join(td, "cli.bin")])
        assert rc == 0, rc
        with open(os.path.join(td, "cli.bin"), "rb") as f:
            assert f.read() == b"\xa5"

        upd = os.path.join(td, "TFM_UPDATE.sh")
        with open(upd, "w", encoding="ascii") as f:
            f.write("slot0=0xc078000\nslot1=0xc088000\nslot2=0xc200000\n")
            f.write("slot3=0xc258000\nboot=0xc00e000\n")
        err = update_sh_error(upd)
        assert err and "slot1" in err, err

    real_bl2 = os.path.join(
        repo_root(), "trusted-firmware-m", "build_s", "api_ns", "bin", "bl2.bin"
    )
    if os.path.isfile(real_bl2):
        err = bl2_marker_error(real_bl2)
        assert err is None, err
    script_dir = os.path.dirname(os.path.abspath(__file__))
    env = open(os.path.join(script_dir, "h5f4_env.bat"), encoding="utf-8").read()
    assert 'set "H5F4_ADDR_NS_S=0x0C0F8000"' in env
    assert 'set "H5F4_ADDR_NS_NS=0x080F8000"' in env
    assert "WRPSG11=0xffffffff" in env
    assert 'set "H5F4_SECWM_FULL=SECWM1_STRT=0 SECWM1_END=255' in env
    assert "WRPSGn1=" not in env
    assert "STM32_Programmer_CLI.exe" in env
    assert "call :" not in env
    assert "goto :eof" not in env
    setup = open(os.path.join(script_dir, "h5f4_setup.bat"), encoding="utf-8").read()
    assert "call :pick_python" not in setup
    assert "call :try_python" not in setup
    assert "goto :eof" not in setup
    assert "--locate" not in setup
    assert "LOCATE_OUT" not in setup
    find_bat = open(os.path.join(script_dir, "h5f4_find_images.bat"), encoding="utf-8").read()
    assert "bl2.bin" in find_bat
    assert "hex2bin" in find_bat
    assert "%~dp0build_s" in find_bat
    assert "%~dp0build_ns" in find_bat
    assert "trusted-firmware-m" not in find_bat
    assert "call :" not in find_bat
    assert "goto :eof" not in find_bat
    update = open(os.path.join(script_dir, "tfm_update.bat"), encoding="utf-8").read()
    assert "H5F4_INNER" in update
    assert 'cmd /c ""%~f0" %*"' in update
    assert "erase_flash.bat" in update
    assert "h5f4_find_images.bat" in update
    assert "regression.bat" in update
    assert not os.path.isfile(os.path.join(script_dir, "jlink_tfm_update.bat"))
    assert not os.path.isfile(os.path.join(script_dir, "flash_h5f4.bat"))
    assert not os.path.isfile(os.path.join(script_dir, "h5f4_win_images.ps1"))
    for name in (
        "regression.bat",
        "tfm_update.bat",
        "erase_flash.bat",
    ):
        text = open(os.path.join(script_dir, name), encoding="utf-8", errors="ignore").read()
        assert "WRPSG11" in text, name
        assert "WRPSGn1=" not in text, name
        assert "0x08088000" not in text, name
        assert "STM32H573I-DK" not in text, name
        if name in ("regression.bat", "erase_flash.bat"):
            assert "call :run_cli %" not in text, (
                "%s still uses call :run_cli %%args%% which strips '='" % name
            )
            assert "h5f4_run_cli.bat" in text, name
            assert "CLI_ARGS=" in text, name
    run_cli = open(os.path.join(script_dir, "h5f4_run_cli.bat"), encoding="utf-8").read()
    assert "CLI_ARGS" in run_cli
    assert "STM32_Programmer_CLI %CLI_ARGS%" in run_cli
    assert "call :" not in run_cli
    erase = open(os.path.join(script_dir, "erase_flash.bat"), encoding="utf-8").read()
    assert " -e all" in erase
    assert "SECWM1_STRT=255" in erase
    assert "H5F4_SECWM_FULL" not in erase
    assert "h5f4-20260901h" in env
    src = open(os.path.join(script_dir, "h5f4_win_images.py"), encoding="utf-8").read()
    assert all(not ln.strip().startswith("import argparse") for ln in src.splitlines())
    with tempfile.TemporaryDirectory() as td:
        for name in ("bl2.bin", "tfm_s_signed.bin", "tfm_ns_signed.bin"):
            open(os.path.join(td, name), "wb").write(b"x")
        loc = locate(extra_cwd=td)
        assert loc["STATUS"] == "OK", loc
        assert loc["BL2"] == os.path.join(td, "bl2.bin"), loc
    rc = main(["--locate"])
    assert rc in (0, 1), rc
    print("h5f4_win_images.py self-test OK")


def main(argv=None):
    # Parse argv by hand. argparse + "-InFile" on Chinese Windows Python
    # aborts with "此时不应有 。" ("ignored explicit argument") before locate.
    argv = list(sys.argv[1:] if argv is None else argv)
    infile = None
    outfile = None
    self_test = False
    rest = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a is None or str(a).strip() == "":
            i += 1
            continue
        if a in ("-h", "--help"):
            print("h5f4_win_images.py hex2bin in.hex out.bin")
            print("h5f4_win_images.py --self-test")
            print("h5f4_win_images.py -InFile in.hex -OutFile out.bin")
            return 0
        if a in ("-InFile", "--InFile", "--in-file"):
            if i + 1 >= len(argv):
                print("ERROR=-InFile needs a path")
                return 2
            infile = argv[i + 1]
            i += 2
            continue
        if a in ("-OutFile", "--OutFile", "--out-file"):
            if i + 1 >= len(argv):
                print("ERROR=-OutFile needs a path")
                return 2
            outfile = argv[i + 1]
            i += 2
            continue
        if a in ("--self-test", "self-test"):
            self_test = True
            i += 1
            continue
        rest.append(a)
        i += 1

    if infile and outfile:
        hex_to_ns_bin(infile, outfile)
        return 0
    if self_test:
        cmd_self_test()
        return 0

    cmd = rest[0].lower() if rest else ""
    if cmd == "hex2bin":
        if len(rest) < 3:
            print("ERROR=hex2bin in.hex out.bin")
            return 2
        hex_to_bin(rest[1], rest[2], remap=False)
        return 0
    if cmd in ("locate", "--locate"):
        info = locate()
        print_locate(info)
        return 0 if info["STATUS"] == "OK" else 1
    if cmd == "check":
        if len(rest) < 2:
            print("ERROR=check requires a file path")
            return 1
        err = bl2_marker_error(rest[1])
        if err:
            print(err, file=sys.stderr)
            return 1
        print("OK %s" % rest[1])
        return 0
    if cmd == "remap":
        print("ERROR=use -InFile / -OutFile")
        return 2
    if not rest:
        info = locate()
        print_locate(info)
        return 0 if info["STATUS"] == "OK" else 1
    print("ERROR=unknown command %s" % rest[0])
    return 2


if __name__ == "__main__":
    sys.exit(main())
