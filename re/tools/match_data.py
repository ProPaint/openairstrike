#!/usr/bin/env python3
"""Map global data addresses of one executable to another.

    re/tools/match_data.py --from v170 --to as2 --pairs PAIRS.json --out re/symbols_as2_data.csv

PAIRS.json is the pair list written by match_symbols.py --pairs. For every
matched function pair the two disassemblies are aligned instruction by
instruction (difflib over address-free normalised instructions); aligned
instructions with the same opcode vote for "source data address -> target
data address" for every absolute address >= the data section they use (memory
operands and immediates). A mapping is kept when its votes dominate.

The curated list of globals (names, descriptions) is re/tools/globals_<from>.csv;
the script-global table of both executables adds its own names.
"""
import argparse
import csv
import difflib
import json
import os
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_export as R  # noqa: E402

HEX = re.compile(r"0x[0-9a-f]+")
LINE = R.LINE_RE


def data_lo(pe):
    for s in pe.sections:
        if s[0] == ".rdata":
            return s[1]
    return 0x00440000


def read_insns(tag, addr, lo):
    """[(normalised text, [data addresses])] of one function."""
    d = os.path.join(R.DATA_ROOT, "re", "out", tag, "disasm")
    idx = read_insns.idx.setdefault(tag, {f[:8]: f for f in os.listdir(d)})
    fn = idx.get("%08x" % addr)
    out = []
    if not fn:
        return out
    with open(os.path.join(d, fn), encoding="latin1") as f:
        for line in f:
            m = LINE.match(line)
            if not m:
                continue
            mn, ops = m.group(3), m.group(4)
            addrs = []

            def rep(x):
                v = int(x.group(0), 16)
                if v >= lo:
                    addrs.append(v)
                    return "D"
                if v >= 0x400000:
                    return "A"
                return x.group(0)
            norm = mn + " " + HEX.sub(rep, ops)
            out.append((norm, addrs))
    return out


read_insns.idx = {}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--from", dest="src", default="v170")
    ap.add_argument("--to", dest="dst", default="as2")
    ap.add_argument("--pairs", required=True)
    ap.add_argument("--out")
    ap.add_argument("--dump", help="write every accepted mapping (json)")
    args = ap.parse_args()
    pe_s = R.PE(os.path.join(R.DATA_ROOT, R.EXES[args.src]))
    pe_d = R.PE(os.path.join(R.DATA_ROOT, R.EXES[args.dst]))
    lo_s, lo_d = data_lo(pe_s), data_lo(pe_d)
    pairs = json.load(open(args.pairs))
    votes = defaultdict(Counter)
    where = defaultdict(set)
    for s_hex, v in pairs.items():
        s, d = int(s_hex, 16), int(v["to"], 16)
        a = read_insns(args.src, s, lo_s)
        b = read_insns(args.dst, d, lo_d)
        if not a or not b or len(a) * len(b) > 4_000_000:
            continue
        sm = difflib.SequenceMatcher(None, [x[0] for x in a], [x[0] for x in b], autojunk=False)
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if tag == "equal" or (tag == "replace" and i2 - i1 == j2 - j1):
                for k in range(i2 - i1):
                    xa, xb = a[i1 + k], b[j1 + k]
                    if xa[0].split(" ")[0] != xb[0].split(" ")[0] or len(xa[1]) != len(xb[1]):
                        continue
                    for p, q in zip(xa[1], xb[1]):
                        votes[p][q] += 1 if tag == "equal" else 0.25
                        where[p].add(s)
    mapping = {}
    for p, c in votes.items():
        (q, n), = c.most_common(1)
        tot = sum(c.values())
        if n >= 1 and n / tot >= 0.6:
            mapping[p] = (q, n, tot, len(where[p]))
    print("data addresses mapped: %d of %d seen" % (len(mapping), len(votes)), file=sys.stderr)
    if args.dump:
        json.dump({"%08x" % p: ["%08x" % q, n, t, w] for p, (q, n, t, w) in sorted(mapping.items())},
                  open(args.dump, "w"), indent=0)
    if args.out:
        write_csv(args, mapping, pe_s, pe_d)


def nearest(mapping, p, maxdelta=0x40):
    """Map an address inside a structure from a mapped base below it."""
    best = None
    for delta in range(0, maxdelta + 1):
        if p - delta in mapping:
            q = mapping[p - delta][0] + delta
            best = (q, delta)
            break
    return best


def map_one(mapping, p):
    """(address, confidence, evidence) of source data address p in the target."""
    if p in mapping:
        q, n, tot, w = mapping[p]
        conf = "high" if n >= 5 and w >= 2 else "medium" if n >= 2 else "low"
        return q, conf, ("aligned instructions of matched functions: %d of %d votes from %d function pair(s)"
                         % (n, tot, w))
    nb = nearest(mapping, p)
    if nb:
        q, delta = nb
        return q, "low", ("offset +0x%x from the mapped address 0x%08x (%d votes); structure layout assumed unchanged"
                          % (delta, p - delta, mapping[p - delta][1]))
    return None, "none", "no aligned reference found"


def write_csv_sequel(args, mapping, pe_s, pe_d):
    """A sequel against another sequel: the rows of re/symbols_<src>_data.csv (names corrected
    by re/symbols_<src>_game.csv, whose table rows win), plus re/tools/globals_<src>_<dst>.csv
    (further source tables named by the <dst> package), mapped to <dst>."""
    here = os.path.dirname(os.path.abspath(__file__))
    src_rows = {}
    order = []
    for path in (os.path.join(R.REPO, "re", "symbols_%s_data.csv" % args.src),
                 os.path.join(R.REPO, "re", "symbols_%s_game.csv" % args.src),
                 os.path.join(here, "globals_%s_%s.csv" % (args.src, args.dst))):
        if not os.path.exists(path):
            continue
        with open(path) as f:
            lines = [l for l in f if l.strip() and not l.startswith("#")]
        for r in csv.DictReader(lines):
            if "kind" not in r:
                # a functions file: only its data rows (kind in the subsystem column)
                if r.get("subsystem") not in ("table", "variable", "array", "struct"):
                    continue
                r = dict(r, kind=r["subsystem"])
            if not r["address"]:
                continue
            a = int(r["address"], 16)
            key = (a, r["name"]) if r["kind"] == "script global" else a
            if key not in src_rows:
                order.append(key)
            src_rows[key] = r
    ts = {n: v for n, v, _ in R.read_table(pe_s, *R.TABLES[args.src]["global"], stride=12)}
    td = {n: (v, rec) for n, v, rec in R.read_table(pe_d, *R.TABLES[args.dst]["global"], stride=12)}
    tables = {R.TABLES[args.src]["builtin"][0]: R.TABLES[args.dst]["builtin"][0],
              R.TABLES[args.src]["global"][0]: R.TABLES[args.dst]["global"][0]}
    out = []
    for key in order:
        r = src_rows[key]
        p = int(r["address"], 16)
        row = {"name": r["name"], "kind": r["kind"], "description": r["description"],
               "%s_address" % args.src: "0x%08x" % p, "v170_address": r.get("v170_address", "")}
        if r["kind"] == "script global" and r["name"].startswith("sg_") and r["name"][3:] in td:
            v, rec = td[r["name"][3:]]
            row.update(address="0x%08x" % v, confidence="high",
                       evidence="script-global table record 0x%08x" % rec)
        elif p in tables:
            row.update(address="0x%08x" % tables[p], confidence="high",
                       evidence="probe result (re/probes/%s.json), re_export.TABLES" % args.dst)
        else:
            q, conf, ev = map_one(mapping, p)
            row.update(address="0x%08x" % q if q is not None else "", confidence=conf, evidence=ev)
        out.append(row)
    cols = ["address", "name", "kind", "description", "confidence", "evidence",
            "%s_address" % args.src, "v170_address"]
    with open(args.out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols, lineterminator="\n")
        w.writeheader()
        for r in out:
            w.writerow({k: r.get(k, "") for k in cols})
    print("wrote %s (%d rows)" % (args.out, len(out)), file=sys.stderr)


def write_csv(args, mapping, pe_s, pe_d):
    if args.src != "v170":
        return write_csv_sequel(args, mapping, pe_s, pe_d)
    here = os.path.dirname(os.path.abspath(__file__))
    cur = os.path.join(here, "globals_%s.csv" % args.src)
    rows = []
    seen = set()
    # script-global tables: names and addresses straight from both tables
    if args.src in R.TABLES and args.dst in R.TABLES:
        ts = {n: v for n, v, _ in R.read_table(pe_s, *R.TABLES[args.src]["global"], stride=12)}
        td = R.read_table(pe_d, *R.TABLES[args.dst]["global"], stride=12)
        for n, v, rec in td:
            s = ts.get(n)
            ev = "script-global table record 0x%08x" % rec
            if s is not None and s in mapping and mapping[s][0] != v:
                ev += " (the code alignment disagrees: 0x%08x)" % mapping[s][0]
            rows.append({"address": "0x%08x" % v, "name": "sg_" + n, "kind": "script global",
                         "description": "script-visible global '%s'%s" % (n, "" if s is not None else " (new in %s)" % args.dst),
                         "confidence": "high", "evidence": ev,
                         "v170_address": "0x%08x" % s if s is not None else ""})
            seen.add(v)
    with open(cur) as f:
        lines = [l for l in f if l.strip() and not l.startswith("#")]
    for r in csv.DictReader(lines):
        if not r["v170_address"]:
            # global of the target only (no v1.70 counterpart)
            rows.append({"address": r["as2_address"], "name": r["name"], "kind": r["kind"],
                         "description": r["description"], "confidence": r["confidence"],
                         "evidence": r["evidence"], "v170_address": ""})
            continue
        p = int(r["v170_address"], 16)
        row = {"name": r["name"], "kind": r["kind"], "description": r["description"],
               "v170_address": "0x%08x" % p}
        if r.get("as2_address"):
            q = int(r["as2_address"], 16)
            row.update(address="0x%08x" % q, confidence=r["confidence"], evidence=r["evidence"])
        elif p in mapping:
            q, n, tot, w = mapping[p]
            conf = "high" if n >= 5 and w >= 2 else "medium" if n >= 2 else "low"
            row.update(address="0x%08x" % q, confidence=conf,
                       evidence="aligned instructions of matched functions: %d of %d votes from %d function pair(s)"
                       % (n, tot, w))
        else:
            nb = nearest(mapping, p)
            if nb:
                q, delta = nb
                n = mapping[p - delta][1]
                row.update(address="0x%08x" % q, confidence="low",
                           evidence="offset +0x%x from the mapped address 0x%08x (%d votes); structure layout assumed unchanged"
                           % (delta, p - delta, n))
            else:
                row.update(address="", confidence="none", evidence="no aligned reference found")
        rows.append(row)
    cols = ["address", "name", "kind", "description", "confidence", "evidence", "v170_address"]
    with open(args.out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols, lineterminator="\n")
        w.writeheader()
        for r in rows:
            w.writerow({k: r.get(k, "") for k in cols})
    print("wrote %s (%d rows)" % (args.out, len(rows)), file=sys.stderr)


if __name__ == "__main__":
    main()
