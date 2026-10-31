#!/usr/bin/env python3
"""Pack the signed CCU NS image into a FWMP file."""

from pathlib import Path
import hashlib
import struct


ROOT = Path(__file__).resolve().parent
INPUT_PATH = ROOT / "sign_kit/tfm_ns_signed.bin"
OUTPUT_PATH = ROOT / "sign_kit/tfm_ns_signed.fwmp"
IMAGE_TYPE = 0
TARGET_COMPONENT = 0


def pack_fwmp():
    if not INPUT_PATH.is_file():
        raise FileNotFoundError(f"input file not found: {INPUT_PATH}")

    payload = INPUT_PATH.read_bytes()
    payload_hash = hashlib.sha256(payload).digest()
    header = bytearray()
    header += b"FWMP"
    header += struct.pack("<B H B", 1, 64, 0)
    header += struct.pack("<B B B B H B B", IMAGE_TYPE, 0, 0, 0, 0, TARGET_COMPONENT, 0)
    header += bytes(4)
    header += struct.pack("<I", len(payload))
    header += payload_hash
    header += bytes(8)

    if len(header) != 64:
        raise RuntimeError(f"invalid FWMP header size: {len(header)}")

    OUTPUT_PATH.write_bytes(header + payload)
    print(f"input       : {INPUT_PATH}")
    print(f"output      : {OUTPUT_PATH}")
    print(f"image type  : {IMAGE_TYPE} (NS)")
    print(f"board type  : 0 (CCU)")
    print(f"target comp : {TARGET_COMPONENT} (NS)")
    print(f"payload     : {len(payload)} bytes")
    print(f"payload sha : {payload_hash.hex()}")
    print(f"total       : {OUTPUT_PATH.stat().st_size} bytes")


if __name__ == "__main__":
    exit_code = 0
    try:
        pack_fwmp()
        print("\nFWMP file generated successfully.")
    except Exception as exc:
        exit_code = 1
        print(f"\nFWMP generation failed: {exc}")
    finally:
        try:
            input("Press Enter to exit...")
        except EOFError:
            pass
    raise SystemExit(exit_code)
