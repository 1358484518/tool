#!/usr/bin/env python3
"""Intel HEX -> bin, map STM32 secure alias 0x0Cxxxxxx to 0x08xxxxxx."""
import argparse
import sys

MAX_SIZE = 2 * 1024 * 1024


def records(path):
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
            if 0x0C000000 <= addr < 0x0C200000:
                addr -= 0x04000000
            data = bytes(int(t[9 + 2 * i : 11 + 2 * i], 16) for i in range(length))
            yield addr, data


def main():
    p = argparse.ArgumentParser()
    p.add_argument("-InFile", dest="infile", required=True)
    p.add_argument("-OutFile", dest="outfile", required=True)
    args = p.parse_args()

    recs = list(records(args.infile))
    if not recs:
        sys.exit("No data records in %s" % args.infile)
    min_a = min(a for a, d in recs)
    max_a = max(a + len(d) - 1 for a, d in recs)
    size = max_a - min_a + 1
    if size <= 0 or size > MAX_SIZE:
        sys.exit("HEX span 0x%08X-0x%08X size=%d too large" % (min_a, max_a, size))
    blob = bytearray([0xFF]) * size
    for addr, data in recs:
        off = addr - min_a
        blob[off : off + len(data)] = data
    with open(args.outfile, "wb") as out:
        out.write(blob)
    with open(args.outfile + ".addr", "w", encoding="ascii") as out:
        out.write("0x%08X\n" % min_a)
    print("LOAD=0x%08X SIZE=%d" % (min_a, size))


if __name__ == "__main__":
    main()
