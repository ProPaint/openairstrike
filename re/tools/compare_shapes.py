#!/usr/bin/env python3
"""Finds, for given functions of one export, the functions of another export with the same
instruction shape (addresses masked), to tell whether code is unchanged between two games.

Usage:
    AS3D_DATA_ROOT=<main checkout> python3 re/tools/compare_shapes.py --from as2 --to gulf 0x42b950 ...
    AS3D_DATA_ROOT=<main checkout> python3 re/tools/compare_shapes.py --from as2 --to gulf --csv re/symbols_as2_frontend.csv

For each address: `same` = a function of the other export has the identical normalised
instruction sequence (re_export.Func.exact); `same-opcodes` = only the mnemonic sequence
(Func.loose) matches, i.e. other constants, offsets or registers; `changed` = neither. The
candidate address is printed (unique match) or the number of candidates. With --csv, the
first column of every row of the file is used as the address list.
Standard library only; needs the Ghidra exports of both games (re/out/<tag>/).
"""
import argparse
import csv
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_export  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--from", dest="src", default="as2")
    ap.add_argument("--to", dest="dst", default="gulf")
    ap.add_argument("--csv")
    ap.add_argument("addrs", nargs="*")
    a = ap.parse_args()
    addrs = [int(x, 16) for x in a.addrs]
    if a.csv:
        with open(a.csv) as f:
            for row in csv.reader(f):
                if row and row[0].startswith("0x"):
                    addrs.append(int(row[0], 16))
    src, _ = re_export.load(a.src)
    dst, _ = re_export.load(a.dst)
    by_exact, by_loose = {}, {}
    for f in dst.values():
        by_exact.setdefault(f.exact, []).append(f.addr)
        by_loose.setdefault(f.loose, []).append(f.addr)
    for addr in addrs:
        f = src.get(addr)
        if f is None:
            print("0x%08x not-a-function" % addr)
            continue
        ex = by_exact.get(f.exact, [])
        lo = by_loose.get(f.loose, [])
        if ex:
            verdict, cands = "same", ex
        elif lo:
            verdict, cands = "same-opcodes", lo
        else:
            verdict, cands = "changed", []
        where = ("0x%08x" % cands[0]) if len(cands) == 1 else ("%d candidates" % len(cands) if cands else "-")
        print("0x%08x %-24s %-12s %s (%d insns)" % (addr, f.name, verdict, where, f.ninsn))


if __name__ == "__main__":
    main()
