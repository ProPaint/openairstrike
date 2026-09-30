#!/usr/bin/env python3
"""What differs between two builds of the same code base (AirStrike 2 and Gulf Thunder).

    AS3D_DATA_ROOT=<main checkout> python3 re/tools/sequel_diff.py --from as2 --to gulf \
        --pairs PAIRS.json [--data-map OUT.json] [--functions] [--data] [--consts]

PAIRS.json is the pair list written by `match_symbols.py --pairs`. Three reports:

--functions  every matched pair classified as `same` (identical normalised instruction
             sequence), `same-opcodes` (same mnemonics, other constants, offsets or registers)
             or `changed`, with the aligned differences of the non-identical ones (instruction
             index, both instructions with addresses masked) so that a reader knows where to look.
--consts     for pairs of the same length: aligned operands whose masked values differ in
             content although the shape is the same: a read-only constant (float, double,
             string) with other content, an immediate that is not an address but differs
             (a float or a large integer written into the code). These are the differences the
             shape comparison cannot see.
--data       the read-only and initialised data sections of both executables, compared as
             sequences of 4-byte words with pointers masked: every inserted, removed or changed
             run, with the functions that reference an address inside it (in both builds).

Only addresses, counts and short constant values are printed, never code.
Standard library only.
"""
import argparse
import difflib
import json
import os
import re
import struct
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_export as R  # noqa: E402

HEX = re.compile(r"-?0x[0-9a-f]+")


def insns(tag, addr, _idx={}):
    d = os.path.join(R.DATA_ROOT, "re", "out", tag, "disasm")
    idx = _idx.setdefault(tag, {f[:8]: f for f in os.listdir(d)})
    fn = idx.get("%08x" % addr)
    out = []
    if not fn:
        return out
    with open(os.path.join(d, fn), encoding="latin1") as f:
        for line in f:
            m = R.LINE_RE.match(line)
            if m:
                out.append((int(m.group(1), 16), m.group(3), m.group(4)))
    return out


def norm(mn, ops):
    def rep(m):
        s = m.group(0)
        if s.startswith("-"):
            return s
        return "A" if int(s, 16) >= 0x400000 else s
    return mn + " " + ("J" if mn.startswith("J") and ops.startswith("0x") else HEX.sub(rep, ops))


def section(pe, va):
    for name, sva, vsize, rptr, rsize in pe.sections:
        if sva <= va < sva + max(vsize, rsize):
            return name, va - sva < rsize
    return None, False


def describe_const(pe, va, kind):
    """Short printable content of a read-only operand."""
    b = pe.read(va, 8)
    if b is None:
        return "?"
    i = struct.unpack("<I", b[:4])[0]
    if 0x400000 <= i < 0x2300000 or (kind != "qword" and 0x90000 <= i < 0xa0000):
        return "ptr"  # a pointer (or an import-name RVA): shifted, not a content difference
    if kind == "qword":
        return "%r" % struct.unpack("<d", b)[0]
    f = struct.unpack("<f", b[:4])[0]
    if f == f and (f == 0 or 1e-6 < abs(f) < 1e9) and pe.cstr(va, 8) is not None and len(pe.cstr(va, 8)) < 4:
        return "%g" % f
    s = pe.cstr(va, 64)
    if s and len(s) >= 2 and all(32 <= ord(c) < 127 for c in s):
        return repr(s)
    return "%g / 0x%x" % (f, i) if abs(f) < 1e9 and (f == 0 or abs(f) > 1e-6) else "0x%x" % i


def const_diffs(pa, pb, A, B):
    """Aligned operand values of two same-length functions whose content differs."""
    out = []
    for (ia, ma, oa), (ib, mb, ob) in zip(A, B):
        va = HEX.findall(oa)
        vb = HEX.findall(ob)
        if len(va) != len(vb) or ma.startswith("J") or ma == "CALL":
            continue
        kind = "qword" if "qword" in oa else "dword"
        for x, y in zip(va, vb):
            if x == y or x.startswith("-") or y.startswith("-"):
                continue
            x, y = int(x, 16), int(y, 16)
            sa, ia_init = section(pa, x)
            sb, ib_init = section(pb, y)
            if sa and sb:
                # read-only data only: initial values of writable globals are compared by --data
                if sa == ".rdata" and sb == ".rdata":
                    n = 8 if kind == "qword" else 4
                    if pa.read(x, n) == pb.read(y, n) and describe_const(pa, x, kind) != "ptr":
                        # same bytes: equal unless it is a longer string that differs further on
                        s1, s2 = pa.cstr(x, 256), pb.cstr(y, 256)
                        texty = lambda s: s and len(s) >= 6 and all(32 <= ord(c) < 127 for c in s)
                        if s1 == s2 or not (texty(s1) and texty(s2)):
                            continue
                    ca, cb = describe_const(pa, x, kind), describe_const(pb, y, kind)
                    if ca != cb:
                        out.append((ia, ib, "0x%08x=%s" % (x, ca), "0x%08x=%s" % (y, cb)))
            elif not sa and not sb:
                out.append((ia, ib, "imm 0x%x" % x, "imm 0x%x" % y))
    return out


def word_seq(pe, name):
    for n, sva, vsize, rptr, rsize in pe.sections:
        if n == name:
            size = min(vsize, rsize)
            data = pe.data[rptr:rptr + size]
            words = struct.unpack("<%dI" % (size // 4), data[:size // 4 * 4])
            toks = ["P" if 0x400000 <= w < 0x2300000 else "%x" % w for w in words]
            return sva, toks, words
    return 0, [], []


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--from", dest="src", default="as2")
    ap.add_argument("--to", dest="dst", default="gulf")
    ap.add_argument("--pairs", required=True)
    ap.add_argument("--functions", action="store_true")
    ap.add_argument("--consts", action="store_true")
    ap.add_argument("--data", action="store_true")
    ap.add_argument("--max-lines", type=int, default=40)
    ap.add_argument("--names", help="CSV with address,name columns of the source (for labels)")
    a = ap.parse_args()
    pairs = {int(k, 16): int(v["to"], 16) for k, v in json.load(open(a.pairs)).items()}
    pa = R.PE(os.path.join(R.DATA_ROOT, R.EXES[a.src]))
    pb = R.PE(os.path.join(R.DATA_ROOT, R.EXES[a.dst]))
    names = {}
    if a.names:
        import csv
        for r in csv.DictReader(open(a.names)):
            names[int(r["address"], 16)] = r["name"]
    counts = Counter()
    refs_a, refs_b = defaultdict(set), defaultdict(set)
    for s, d in sorted(pairs.items()):
        A, B = insns(a.src, s), insns(a.dst, d)
        for fa, L in ((s, A), (d, B)):
            pass
        for _, _, o in A:
            for v in HEX.findall(o):
                if not v.startswith("-"):
                    refs_a[int(v, 16)].add(s)
        for _, _, o in B:
            for v in HEX.findall(o):
                if not v.startswith("-"):
                    refs_b[int(v, 16)].add(d)
        na = [norm(m, o) for _, m, o in A]
        nb = [norm(m, o) for _, m, o in B]
        if na == nb:
            cls = "same"
        elif [m for _, m, _ in A] == [m for _, m, _ in B]:
            cls = "same-opcodes"
        else:
            cls = "changed"
        cd = const_diffs(pa, pb, A, B) if len(A) == len(B) else []
        if cls == "same" and cd:
            cls = "same-shape-other-constants"
        counts[cls] += 1
        label = names.get(s, "")
        if a.functions and cls != "same":
            print("## %s 0x%08x -> 0x%08x %s (%d / %d insns)" % (cls, s, d, label, len(A), len(B)))
            if cls != "same-shape-other-constants":
                sm = difflib.SequenceMatcher(None, na, nb, autojunk=False)
                n = 0
                for op, i1, i2, j1, j2 in sm.get_opcodes():
                    if op == "equal":
                        continue
                    print("  %s as2[%d:%d] @0x%x  gulf[%d:%d] @0x%x" % (
                        op, i1, i2, A[i1][0] if i1 < len(A) else 0, j1, j2, B[j1][0] if j1 < len(B) else 0))
                    for k in range(i1, min(i2, i1 + 6)):
                        print("    - %s" % na[k])
                    for k in range(j1, min(j2, j1 + 6)):
                        print("    + %s" % nb[k])
                    n += 1
                    if n >= a.max_lines:
                        print("  ...")
                        break
        if a.consts and cd:
            if not a.functions or cls == "same":
                print("## %s 0x%08x -> 0x%08x %s" % (cls, s, d, label))
            for ia, ib, x, y in cd:
                print("  const @0x%x / @0x%x: %s  ->  %s" % (ia, ib, x, y))
    print("# classes:", dict(counts), file=sys.stderr)
    if a.data:
        for sec in (".rdata", ".data"):
            ba, ta, wa = word_seq(pa, sec)
            bb, tb, wb = word_seq(pb, sec)
            sm = difflib.SequenceMatcher(None, ta, tb, autojunk=False)
            print("# section %s: %d / %d words" % (sec, len(ta), len(tb)))
            for op, i1, i2, j1, j2 in sm.get_opcodes():
                if op == "equal":
                    continue
                xa, xb = ba + 4 * i1, bb + 4 * j1
                fa = set().union(*[refs_a.get(x, set()) for x in range(xa, ba + 4 * max(i2, i1 + 1), 4)]) if True else set()
                fb = set().union(*[refs_b.get(x, set()) for x in range(xb, bb + 4 * max(j2, j1 + 1), 4)])
                ta_s = pa.cstr(xa, 48) or ""
                tb_s = pb.cstr(xb, 48) or ""
                print("%-7s %s 0x%08x+%-4d %s 0x%08x+%-4d  refs %s | %s  %r | %r" % (
                    op, a.src, xa, 4 * (i2 - i1), a.dst, xb, 4 * (j2 - j1),
                    ",".join("%x" % f for f in sorted(fa)[:4]), ",".join("%x" % f for f in sorted(fb)[:4]),
                    ta_s[:30] if ta_s.isprintable() else "", tb_s[:30] if tb_s.isprintable() else ""))


if __name__ == "__main__":
    main()
