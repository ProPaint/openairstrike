#!/usr/bin/env python3
"""Match the functions of one AirStrike executable against another.

    re/tools/match_symbols.py --from v170 --to as2 [--out re/symbols_as2.csv]
    re/tools/match_symbols.py --check re/symbols_as2.csv [--to as2]

Reads the Ghidra exports in $AS3D_DATA_ROOT/re/out/<tag>/ (functions.json,
imports.json, disasm/*.asm) and the executables (for table and float-constant
reads), plus the named symbols of the source executable
(re/symbols_<from>.csv, re/symbols_<from>_render.csv).

Method, in order (each pair keeps the strongest evidence that produced it):
  1. anchors (confidence high): builtin table names, strings referenced by
     exactly one function in each executable, imports with a single caller,
     identical library names given by Ghidra's function ID, unique identical
     instruction shape (address-free normalised disassembly) of >= 10 insns;
  2. call-graph propagation (medium): unmatched callees/callers of matched
     pairs, scored by shape, size, constants, strings and callee consistency,
     accepted greedily when the pair is the mutual best candidate;
  3. global shape search (low): remaining functions against all remaining
     functions, mutual best above a threshold.
Output: one row per function of the target executable. See
docs/spec/as2/symbol-map.md for the columns and the statistics.
"""
import argparse
import csv
import json
import math
import os
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_export as R  # noqa: E402

REPO = R.REPO


# ---------------------------------------------------------------- features

def cosine(c1, c2):
    if not c1 or not c2:
        return 0.0
    dot = sum(v * c2.get(k, 0) for k, v in c1.items())
    n1 = math.sqrt(sum(v * v for v in c1.values()))
    n2 = math.sqrt(sum(v * v for v in c2.values()))
    return dot / (n1 * n2)


def mjacc(c1, c2):
    """Multiset Jaccard; None when both empty."""
    if not c1 and not c2:
        return None
    keys = set(c1) | set(c2)
    inter = sum(min(c1.get(k, 0), c2.get(k, 0)) for k in keys)
    union = sum(max(c1.get(k, 0), c2.get(k, 0)) for k in keys)
    return inter / union if union else None


def is_library_name(n):
    """Names Ghidra's function ID gives to C runtime / library code."""
    return (n.startswith("_") or n.startswith("@") or n.startswith("~") or "::" in n
            or n.startswith("Unwind@") or n.startswith("Catch") or n[:1].islower())


class Matcher:
    def __init__(self, src, dst):
        self.S, self.pe_s = R.load(src)
        self.D, self.pe_d = R.load(dst)
        self.src, self.dst = src, dst
        self.m = {}      # src addr -> dst addr
        self.rm = {}     # dst addr -> src addr
        self.info = {}   # src addr -> (confidence, evidence)
        for fs in (self.S, self.D):
            for f in fs.values():
                f.strings_c = Counter(s for s in f.strings)
                f.imports_c = Counter(f.imports)
                f.consts_big = Counter({k: v for k, v in f.consts.items() if abs(k) > 16})
                f.callee_set = set(f.calls)

    # a pair is accepted only once per side
    def add(self, s, d, conf, ev):
        if s in self.m or d in self.rm:
            return False
        self.m[s] = d
        self.rm[d] = s
        self.info[s] = (conf, ev)
        return True

    def sim(self, s, d):
        a, b = self.S[s], self.D[d]
        if a.exact == b.exact:
            return 1.0
        if a.ninsn == 0 or b.ninsn == 0:
            return 0.0
        base = 0.5 * cosine(a.mnem, b.mnem) + 0.5 * (min(a.ninsn, b.ninsn) / max(a.ninsn, b.ninsn))
        if a.loose == b.loose:
            base = max(base, 0.97)
        feats = []
        for x, y, w in ((a.strings_c, b.strings_c, 2.0), (a.imports_c, b.imports_c, 1.5),
                        (a.consts_big, b.consts_big, 1.0), (a.fconsts, b.fconsts, 1.5),
                        (a.offsets, b.offsets, 1.0)):
            j = mjacc(x, y)
            if j is not None:
                feats.append((j, w))
        g = self.graph(s, d)
        if g is not None:
            feats.append((g, 2.0))
        if not feats:
            return base * 0.9
        fs = sum(j * w for j, w in feats) / sum(w for _, w in feats)
        return 0.5 * base + 0.5 * fs

    def graph(self, s, d):
        """Call-graph consistency: share of already matched callees and
        callers of s whose partners are callees/callers of d. None if no
        neighbour on either side is matched yet."""
        a, b = self.S[s], self.D[d]
        hit = tot = 0
        for ns, nd, mp, rmp in ((a.callee_set, b.callee_set, self.m, self.rm),
                                (set(a.callers), set(b.callers), self.m, self.rm)):
            mc = [mp[c] for c in ns if c in mp and c != s]
            rc = [rmp[c] for c in nd if c in rmp and c != d]
            if mc or rc:
                hit += sum(1 for c in mc if c in nd)
                tot += max(len(mc), len(rc))
        return hit / tot if tot else None

    # ------------------------------------------------------------ anchors
    def apply_overrides(self, path):
        """Manual decisions (re/tools/overrides_<from>_<to>.csv)."""
        self.removed = {}
        if not os.path.exists(path):
            return
        with open(path) as f:
            rows = [l for l in f if l.strip() and not l.startswith("#")]
        for r in csv.DictReader(rows):
            s = int(r["from_address"], 16)
            if r["to_address"] == "removed":
                self.removed[s] = (r["confidence"], r["evidence"])
                continue
            d = int(r["to_address"], 16)
            if d not in self.D or s not in self.S:
                print("override skipped (not a function):", r, file=sys.stderr)
                continue
            if not self.add(s, d, r["confidence"], "manual: " + r["evidence"]):
                print("override conflict:", r, file=sys.stderr)

    def anchor_tables(self):
        if self.src not in R.TABLES or self.dst not in R.TABLES:
            return
        s0, n0 = R.TABLES[self.src]["builtin"]
        d0, n1 = R.TABLES[self.dst]["builtin"]
        ts = {n: v for n, v, _ in R.read_table(self.pe_s, s0, n0)}
        td = {n: v for n, v, _ in R.read_table(self.pe_d, d0, n1)}
        self.builtins_src, self.builtins_dst = ts, td
        for name, v in ts.items():
            if name in td and v in self.S and td[name] in self.D:
                self.add(v, td[name], "high", "builtin table entry '%s'" % name)

    def anchor_strings(self):
        bys, byd = defaultdict(set), defaultdict(set)
        for f in self.S.values():
            for t in f.strings:
                bys[t].add(f.addr)
        for f in self.D.values():
            for t in f.strings:
                byd[t].add(f.addr)
        votes = defaultdict(Counter)
        why = defaultdict(list)
        for t, ss in bys.items():
            if len(t.strip()) < 4 or t not in byd:
                continue
            dd = byd[t]
            if len(ss) == 1 and len(dd) == 1:
                s, d = next(iter(ss)), next(iter(dd))
                votes[s][d] += 1
                why[(s, d)].append(t)
        rvotes = defaultdict(Counter)
        for s, c in votes.items():
            for d, n in c.items():
                rvotes[d][s] += n
        for s, c in votes.items():
            d, n = c.most_common(1)[0]
            if len(c) > 1 and n < 2 * sum(c.values()) / 2:
                continue
            if rvotes[d].most_common(1)[0][0] != s:
                continue
            sm = self.sim(s, d)
            if n < 2 and sm < 0.3:
                continue
            txt = why[(s, d)][0].strip().replace("\n", " ")[:40]
            self.add(s, d, "high", "unique string '%s' (%d shared)" % (txt, n))

    def anchor_imports(self):
        bys, byd = defaultdict(set), defaultdict(set)
        for f in self.S.values():
            if not f.is_thunk:
                for t in set(f.imports):
                    bys[t].add(f.addr)
        for f in self.D.values():
            if not f.is_thunk:
                for t in set(f.imports):
                    byd[t].add(f.addr)
        for t, ss in bys.items():
            dd = byd.get(t, ())
            if len(ss) == 1 and len(dd) == 1:
                s, d = next(iter(ss)), next(iter(dd))
                if self.sim(s, d) >= 0.35:
                    self.add(s, d, "high", "only caller of import %s" % t)

    def anchor_names(self):
        """Thunks and Ghidra function-ID (library) names present once in each."""
        cs = Counter(f.name for f in self.S.values())
        cd = Counter(f.name for f in self.D.values())
        dn = {f.name: f.addr for f in self.D.values()}
        for f in self.S.values():
            n = f.name
            if n.startswith("FUN_") or cs[n] != 1 or cd.get(n) != 1:
                continue
            if not (f.is_thunk or n.startswith("_") or n.startswith("~") or "::" in n
                    or n[:1].islower() or n.startswith("BASS_")):
                continue  # string-derived Ghidra names are handled by the string anchors
            d = dn[n]
            if f.is_thunk or self.sim(f.addr, d) >= 0.5:
                self.add(f.addr, d, "high", "same library/thunk name '%s' (Ghidra function ID)" % n)

    def anchor_exact(self, min_insn=10):
        cs = Counter(f.exact for f in self.S.values())
        cd = Counter(f.exact for f in self.D.values())
        dh = {f.exact: f.addr for f in self.D.values()}
        for f in self.S.values():
            if f.ninsn >= min_insn and cs[f.exact] == 1 and cd.get(f.exact) == 1:
                self.add(f.addr, dh[f.exact], "high",
                         "identical normalised instruction shape (%d insns, unique in both)" % f.ninsn)

    # -------------------------------------------------------- propagation
    def propagate(self, threshold=0.62, rounds=30, conf="medium", min_graph=None):
        total = 0
        for rnd in range(rounds):
            cand = {}
            for s, d in list(self.m.items()):
                fs, fd = self.S[s], self.D[d]
                for attr, what in (("calls", "callee"), ("coderefs", "callback pointer taken by")):
                    us = [c for c in dict.fromkeys(getattr(fs, attr)) if c not in self.m and c in self.S]
                    ud = [c for c in dict.fromkeys(getattr(fd, attr)) if c not in self.rm and c in self.D]
                    if len(us) * len(ud) > 900:
                        continue
                    same_len = len(us) == len(ud)
                    for i, a in enumerate(us):
                        for j, b in enumerate(ud):
                            sc = self.sim(a, b)
                            if same_len and i == j:
                                sc += 0.08 if attr == "calls" else 0.15
                            if sc > cand.get((a, b), (0, None))[0]:
                                cand[(a, b)] = (sc, "%s %s" % (what, self.label(s)))
                us = [c for c in fs.callers if c not in self.m and c in self.S]
                ud = [c for c in fd.callers if c not in self.rm and c in self.D]
                if len(us) * len(ud) > 400:
                    continue
                for a in us:
                    for b in ud:
                        sc = self.sim(a, b)
                        if sc > cand.get((a, b), (0, None))[0]:
                            cand[(a, b)] = (sc, "caller of %s" % self.label(s))
            if min_graph:
                cand = {k: v for k, v in cand.items() if (self.graph(*k) or 0) >= min_graph}
            n = self._accept(cand, threshold, conf)
            total += n
            if n == 0:
                break
        return total

    def _accept(self, cand, threshold, conf, margin=0.03):
        best_s, best_d = {}, {}
        for (a, b), (sc, _) in cand.items():
            if sc > best_s.get(a, (0,))[0]:
                best_s[a] = (sc, b)
            if sc > best_d.get(b, (0,))[0]:
                best_d[b] = (sc, a)
        # second-best margins
        second_s, second_d = Counter(), Counter()
        for (a, b), (sc, _) in cand.items():
            if best_s[a][1] != b:
                second_s[a] = max(second_s[a], sc)
            if best_d[b][1] != a:
                second_d[b] = max(second_d[b], sc)
        n = 0
        for (a, b), (sc, why) in sorted(cand.items(), key=lambda kv: -kv[1][0]):
            if sc < threshold or best_s[a][1] != b or best_d[b][1] != a:
                continue
            if sc - second_s[a] < margin or sc - second_d[b] < margin:
                continue
            tag = "identical shape, " if self.S[a].exact == self.D[b].exact else ""
            if self.add(a, b, conf, "%s%s (score %.2f)" % (tag, why, min(sc, 1.0))):
                n += 1
        return n

    def neighbour_signature_pass(self):
        """Unmatched functions whose matched callers (or matched callees) form
        the same set on both sides, and the only such pair: rewritten
        functions keep their place in the call graph."""
        n = 0
        for kind in ("callers", "calls"):
            sig_s, sig_d = defaultdict(list), defaultdict(list)
            for f in self.S.values():
                if f.addr in self.m:
                    continue
                nb = set(f.callers) if kind == "callers" else set(f.calls)
                if nb and all(c in self.m for c in nb):
                    sig_s[frozenset(self.m[c] for c in nb)].append(f.addr)
            for f in self.D.values():
                if f.addr in self.rm:
                    continue
                nb = set(f.callers) if kind == "callers" else set(f.calls)
                if nb and all(c in self.rm for c in nb):
                    sig_d[frozenset(nb)].append(f.addr)
            for sig, ss in sig_s.items():
                dd = sig_d.get(sig, [])
                if len(ss) != 1 or len(dd) != 1:
                    continue
                s, d = ss[0], dd[0]
                need = 0.3 if len(sig) >= 2 else 0.45
                if all(is_library_name(self.D[c].name) for c in sig):
                    need = 0.7
                sc = self.sim(s, d)
                if sc < need:
                    continue
                names = ", ".join(sorted(self.label(self.rm[c]) for c in sig))[:80]
                if self.add(s, d, "low", "only unmatched function with the same matched %s (%s) (score %.2f)"
                            % (kind, names, sc)):
                    n += 1
        return n

    def coderef_pass(self):
        """Callback addresses taken at the same position by matched pairs with
        the same number of callback references. Pairs of functions are
        matched (medium); addresses that are not function entries in the
        source export are recorded in self.codemap (menu callbacks that Ghidra
        left inside a neighbouring function)."""
        if not hasattr(self, "codemap"):
            self.codemap = {}
        n = 0
        for s, d in list(self.m.items()):
            cs, cd = self.S[s].coderefs, self.D[d].coderefs
            if not cs or len(cs) != len(cd):
                continue
            for a, b in zip(cs, cd):
                ev = "callback pointer at the same position in %s" % self.label(s)
                if a in self.S and b in self.D:
                    if a in self.m or b in self.rm:
                        continue
                    if self.sim(a, b) >= 0.3 and self.add(a, b, "medium", ev + " (score %.2f)" % self.sim(a, b)):
                        n += 1
                elif a not in self.S and a not in self.codemap:
                    self.codemap[a] = (b, b in self.D, ev)
        return n

    def global_pass(self, threshold=0.8, min_insn=8):
        us = [f for f in self.S.values() if f.addr not in self.m and f.ninsn >= min_insn]
        ud = [f for f in self.D.values() if f.addr not in self.rm and f.ninsn >= min_insn]
        cand = {}
        for a in us:
            for b in ud:
                r = min(a.ninsn, b.ninsn) / max(a.ninsn, b.ninsn)
                if r < 0.6:
                    continue
                sc = self.sim(a.addr, b.addr)
                if sc >= threshold - 0.1:
                    cand[(a.addr, b.addr)] = (sc, "global shape search")
        return self._accept(cand, threshold, "low", margin=0.05)

    def label(self, s):
        return self.names.get(s, (self.S[s].name,))[0] if hasattr(self, "names") else self.S[s].name


# ------------------------------------------------------------- names in/out

def load_src_names(tag):
    """addr -> (name, subsystem, description, alias). Render names win."""
    names = {}
    main = os.path.join(REPO, "re", "symbols_%s.csv" % tag)
    rend = os.path.join(REPO, "re", "symbols_%s_render.csv" % tag)
    seen_names = Counter()
    if os.path.exists(main):
        for r in csv.DictReader(open(main)):
            a = int(r["address"], 16)
            if a in names:
                names[a] = (names[a][0], names[a][1], names[a][2], (names[a][3] + " " + r["name"]).strip())
                continue
            names[a] = (r["name"], r["subsystem"], r["description"], "")
    if os.path.exists(rend):
        for r in csv.DictReader(open(rend)):
            a = int(r["address"], 16)
            old = names.get(a)
            alias = old[0] if old and old[0] != r["name"] else ""
            if old and old[3]:
                alias = (alias + " " + old[3]).strip()
            names[a] = (r["name"], r["subsystem"], r["description"], alias)
    # de-duplicate names: second and later get _<addr>
    out = {}
    for a in sorted(names):
        n, sub, desc, alias = names[a]
        if seen_names[n]:
            n = "%s_%06x" % (n, a & 0xFFFFFF)
        seen_names[n] += 1
        out[a] = (n, sub, desc, alias)
    return out


def load_auto(tag):
    p = os.path.join(REPO, "re", "symbols_%s_auto.csv" % tag)
    out = {}
    if os.path.exists(p):
        for r in csv.DictReader(open(p)):
            if r["confidence"] == "renamed":
                out[int(r["address"], 16)] = r["name"]
    return out


COLUMNS = ["address", "name", "subsystem", "description", "confidence", "evidence", "v170_address"]


def check(path, dst, removed_doc=None):
    D = {int(x["entry"], 16): x for x in json.load(open(os.path.join(R.DATA_ROOT, "re", "out", dst, "functions.json")))}
    rows = list(csv.DictReader(open(path)))
    ok = True
    if list(rows[0].keys()) != COLUMNS:
        print("FAIL columns", list(rows[0].keys()))
        ok = False
    addrs, names = Counter(), Counter()
    for r in rows:
        a = int(r["address"], 16)
        addrs[a] += 1
        names[r["name"]] += 1
        if a not in D:
            print("FAIL address not in functions.json:", r["address"])
            ok = False
        if r["confidence"] not in ("high", "medium", "low", "none"):
            print("FAIL confidence", r["address"], r["confidence"])
            ok = False
    for a, c in addrs.items():
        if c > 1:
            print("FAIL duplicate address", hex(a))
            ok = False
    for n, c in names.items():
        if c > 1 or not n:
            print("FAIL duplicate or empty name", repr(n))
            ok = False
    # statistics
    src_named = load_src_names("v170")
    mapped = {int(r["v170_address"], 16): r for r in rows if r["v170_address"]}
    removed = set()
    removed_doc = removed_doc or os.path.join(REPO, "docs", "spec", dst, "symbol-map.md")
    if os.path.exists(removed_doc):
        for line in open(removed_doc):
            m = re.match(r"^\|\s*`?(0x[0-9a-f]{8})`?\s*\|.*\|\s*removed", line)
            if m:
                removed.add(int(m.group(1), 16))
    conf = Counter()
    for a in src_named:
        if a in mapped:
            conf[mapped[a]["confidence"]] += 1
        elif a in removed:
            conf["removed"] += 1
        else:
            conf["unaccounted"] += 1
    n = len(src_named)
    print("rows: %d of %d %s functions" % (len(rows), len(D), dst))
    print("v170 named functions (symbols_v170.csv + _render.csv, unique addresses): %d" % n)
    for k in ("high", "medium", "low", "removed", "unaccounted"):
        print("  %-12s %4d" % (k, conf[k]))
    cov = (n - conf["unaccounted"]) / n * 100
    print("  coverage (mapped or removed): %.1f%%" % cov)
    sub = Counter(r["subsystem"] for r in rows)
    kind = Counter()
    size_unnamed = 0
    for r in rows:
        a = int(r["address"], 16)
        if r["v170_address"]:
            kind["matched"] += 1
        elif r["name"].startswith("FUN_"):
            kind["unnamed"] += 1
            size_unnamed += D[a]["size"]
        else:
            kind["new (named)"] += 1
    print("target rows by kind:", dict(kind), "unnamed total size %d bytes" % size_unnamed)
    print("confidence:", dict(Counter(r["confidence"] for r in rows)))
    print("subsystems:", ", ".join("%s %d" % kv for kv in sorted(sub.items(), key=lambda kv: -kv[1])))
    if cov < 80:
        print("FAIL coverage below 80%")
        ok = False
    print("CHECK", "OK" if ok else "FAILED")
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--from", dest="src", default="v170")
    ap.add_argument("--to", dest="dst", default="as2")
    ap.add_argument("--out")
    ap.add_argument("--pairs", help="write raw pair list (json) here")
    ap.add_argument("--check")
    ap.add_argument("--removed-doc")
    args = ap.parse_args()
    if args.check:
        sys.exit(0 if check(args.check, args.dst, args.removed_doc) else 1)
    M = Matcher(args.src, args.dst)
    M.names = load_src_names(args.src)
    ov = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      "overrides_%s_%s.csv" % (args.src, args.dst))
    steps = [("manual overrides", lambda: M.apply_overrides(ov)),
             ("builtin tables", M.anchor_tables), ("strings", M.anchor_strings),
             ("imports", M.anchor_imports), ("library names", M.anchor_names),
             ("identical shape", M.anchor_exact)]
    for label, fn in steps:
        before = len(M.m)
        fn()
        print("%-18s +%d  (total %d)" % (label, len(M.m) - before, len(M.m)), file=sys.stderr)
    for i in range(4):
        n = M.propagate()
        print("propagation        +%d  (total %d)" % (n, len(M.m)), file=sys.stderr)
        g = M.global_pass()
        print("global shape       +%d  (total %d)" % (g, len(M.m)), file=sys.stderr)
        k = M.propagate(threshold=0.5, conf="low", min_graph=0.6)
        print("graph-led (low)    +%d  (total %d)" % (k, len(M.m)), file=sys.stderr)
        q = M.neighbour_signature_pass()
        print("same neighbours    +%d  (total %d)" % (q, len(M.m)), file=sys.stderr)
        c = M.coderef_pass()
        print("callback positions +%d  (total %d)" % (c, len(M.m)), file=sys.stderr)
        if n == 0 and g == 0 and k == 0 and q == 0 and c == 0:
            break
    if args.pairs:
        with open(args.pairs, "w") as f:
            json.dump({"%08x" % s: {"to": "%08x" % d, "confidence": M.info[s][0], "evidence": M.info[s][1],
                                    "sim": round(M.sim(s, d), 3)}
                       for s, d in sorted(M.m.items())}, f, indent=1)
    named = M.names
    matched_named = sum(1 for a in named if a in M.m)
    print("v170 named matched: %d of %d" % (matched_named, len(named)), file=sys.stderr)


if __name__ == "__main__":
    main()
