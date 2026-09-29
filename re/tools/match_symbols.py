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
        self.extra = {}
        self.codemap = {}
        if not os.path.exists(path):
            return
        with open(path) as f:
            rows = [l for l in f if l.strip() and not l.startswith("#")]
        for r in csv.DictReader(rows):
            s = int(r["from_address"], 16)
            if r["to_address"] == "removed":
                self.removed[s] = (r["confidence"], r["evidence"])
                continue
            if r["to_address"].startswith("code:"):
                # counterpart is code the target export does not define as a
                # function (a callback inside another function's range)
                self.codemap[s] = (int(r["to_address"][5:], 16), r["confidence"], r["evidence"])
                continue
            d = int(r["to_address"], 16)
            if d not in self.D:
                print("override skipped (target not a function):", r, file=sys.stderr)
                continue
            if s not in self.S:
                # named source address that the source export does not
                # define as a function (callback left inside another one)
                self.extra[s] = (d, r["confidence"], "manual: " + r["evidence"])
                self.rm[d] = s
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
        the same number of callback references. The order of the callbacks
        of a menu can change between the games, so a pair also needs a shape
        score of 0.5. Callback addresses that are not function entries in the
        source export are only matched by hand (overrides file)."""
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
                    sc = self.sim(a, b)
                    if sc >= 0.5 and self.add(a, b, "medium", ev + " (score %.2f)" % sc):
                        n += 1
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
            if old and r["confidence"] == "GUESS" and old[0] != r["name"]:
                # the hand-made file wins over a guessed render name
                names[a] = (old[0], old[1], old[2], (old[3] + " " + r["name"]).strip())
                continue
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

# Address ranges of the target executables (from the function layout):
# game code first, then the C runtime interleaved with statically linked
# library code. Used only to label functions that have no other evidence.
LAYOUT = {
    "as2": {"game_end": 0x004392F2,
            "lib": [(0x0044EF00, 0x00478D40,
                     "Direct3DX 8 static library (texture and image loading; includes libpng 1.0.5, libjpeg and zlib)")]},
}


def load_new_names(path):
    out = {}
    if os.path.exists(path):
        with open(path) as f:
            rows = [l for l in f if l.strip() and not l.startswith("#")]
        for r in csv.DictReader(rows):
            out[int(r["address"], 16)] = r
    return out


def build_rows(M, names_new):
    """One row per target function."""
    N = M.names
    D = M.D
    lay = LAYOUT.get(M.dst, {"game_end": 1 << 32, "lib": []})
    # builtins of the target: function -> names
    bnames = defaultdict(list)
    for n, v in getattr(M, "builtins_dst", {}).items():
        bnames[v].append(n)
    back = {}  # dst -> (src, conf, evidence)
    for s, d in M.m.items():
        back[d] = (s, M.info[s][0], M.info[s][1])
    for s, (d, conf, ev) in M.extra.items():
        back[d] = (s, conf, ev)
    rows = {}
    used = Counter()

    def uniq(name, addr):
        if used[name]:
            name = "%s_%06x" % (name, addr & 0xFFFFFF)
        used[name] += 1
        return name

    order = sorted(D)
    for a in order:
        f = D[a]
        row = {"address": "0x%08x" % a, "v170_address": ""}
        blist = bnames.get(a, [])
        if a in back:
            s, conf, ev = back[a]
            row["v170_address"] = "0x%08x" % s
            row["confidence"], row["evidence"] = conf, ev
            if s in N:
                n, sub, desc, alias = N[s]
                row["name"], row["subsystem"] = n, sub
                row["description"] = desc + (" (v1.70 alias: %s)" % alias if alias else "")
            else:
                sf = M.S[s]
                if not sf.name.startswith("FUN_"):
                    row["name"] = sf.name
                    row["subsystem"] = "crt" if (is_library_name(sf.name) or f.is_thunk) else ""
                    row["description"] = "v1.70 export name" + (" (library)" if row["subsystem"] == "crt" else "")
                else:
                    row["name"] = "FUN_%08x" % a
                    row["subsystem"] = ""
                    row["description"] = "counterpart of the unnamed v1.70 function FUN_%08x" % s
            if f.is_thunk and f.name.startswith("BASS_"):
                row["subsystem"] = "sound"
                row["description"] = "import thunk (BASS.DLL)"
        elif a in names_new:
            r = names_new[a]
            for k in ("name", "subsystem", "description", "confidence", "evidence"):
                row[k] = r[k]
        elif blist:
            row["name"] = "PF_" + blist[0]
            row["subsystem"] = "script-builtin"
            row["description"] = "builtin '%s' (new in %s)" % (blist[0], M.dst)
            row["confidence"] = "high"
            row["evidence"] = "builtin table entry '%s'" % blist[0]
        elif not f.name.startswith("FUN_") and (is_library_name(f.name) or a >= lay["game_end"]):
            row["name"] = f.name
            row["subsystem"] = "crt"
            row["description"] = "C runtime / compiler support (name from Ghidra function ID)"
            row["confidence"] = "high" if not f.name.startswith("FID_conflict") else "low"
            row["evidence"] = "Ghidra function ID name"
            for lo, hi, what in lay["lib"]:
                if lo <= a < hi and not f.name.startswith("Unwind@"):
                    row["subsystem"] = "lib"
                    row["description"] = what
        else:
            row["name"] = "FUN_%08x" % a
            row["subsystem"] = ""
            row["description"] = ""
            row["confidence"] = "none"
            row["evidence"] = ""
            if a >= lay["game_end"]:
                row["subsystem"] = "crt"
                row["description"] = "C runtime (unnamed)"
                row["evidence"] = "address range of the runtime and library code"
                row["confidence"] = "low"
                for lo, hi, what in lay["lib"]:
                    if lo <= a < hi:
                        row["subsystem"] = "lib"
                        row["description"] = what
            elif f.name.startswith("Catch_All@") or f.name.startswith("Unwind@"):
                row["name"] = f.name
                row["subsystem"] = "crt"
                row["description"] = "exception-handling funclet"
                row["evidence"] = "Ghidra name"
                row["confidence"] = "high"
            elif set(D[c].name if c in D else "" for c in f.calls) & {"__CxxThrowException@8", "_memmove_s"} \
                    or any(M.label(back[c][0]).startswith("std_") for c in f.calls if c in back and back[c][0] in M.S):
                row["subsystem"] = "crt"
                row["description"] = "C++ standard library template instance"
                row["evidence"] = "calls std:: helpers / throws length errors"
                row["confidence"] = "low"
        if blist:
            extra = "builtin%s %s" % ("s" if len(blist) > 1 else "", ", ".join("'%s'" % b for b in blist))
            row["description"] = (row["description"] + "; " if row["description"] else "") + extra
        row["name"] = uniq(row["name"], a)
        rows[a] = row
    # runtime/library range: rows still without a subsystem
    for a in order:
        r = rows[a]
        if not r["subsystem"] and a >= lay["game_end"]:
            r["subsystem"] = "crt"
            for lo, hi, what in lay["lib"]:
                if lo <= a < hi:
                    r["subsystem"] = "lib"
                    if not r["description"].startswith("counterpart"):
                        r["description"] = what
    # subsystem of unnamed game functions from their neighbours in the layout
    named = [a for a in order if rows[a]["subsystem"] and a < lay["game_end"]]
    import bisect
    for a in order:
        r = rows[a]
        if r["subsystem"] or a >= lay["game_end"]:
            continue
        i = bisect.bisect_left(named, a)
        prev = rows[named[i - 1]]["subsystem"] if i > 0 else ""
        nxt = rows[named[i]]["subsystem"] if i < len(named) else ""
        r["subsystem"] = prev or nxt
        note = "subsystem guessed from the neighbouring functions (%s / %s)" % (prev, nxt)
        r["description"] = (r["description"] + "; " if r["description"] else "") + note
        if r["confidence"] == "none":
            r["confidence"] = "low"
            r["evidence"] = "address layout only"
    return [rows[a] for a in order]


def write_rows(rows, path):
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=COLUMNS, lineterminator="\n")
        w.writeheader()
        for r in rows:
            w.writerow({k: r.get(k, "") for k in COLUMNS})


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
    # removed functions: the overrides file (source of the decisions) and
    # the table of the symbol-map document
    removed = set()
    removed_doc = removed_doc or os.path.join(REPO, "docs", "spec", dst, "symbol-map.md")
    ov = os.path.join(os.path.dirname(os.path.abspath(__file__)), "overrides_v170_%s.csv" % dst)
    codeaddr = {}
    if os.path.exists(ov):
        for line in open(ov):
            m = re.match(r"^(0x[0-9a-fA-F]+),removed,", line)
            if m:
                removed.add(int(m.group(1), 16))
            m = re.match(r"^(0x[0-9a-fA-F]+),code:(0x[0-9a-fA-F]+),(\w+),", line)
            if m:
                codeaddr[int(m.group(1), 16)] = m.group(3)
    if os.path.exists(removed_doc):
        for line in open(removed_doc):
            m = re.match(r"^\|\s*`?(0x[0-9a-f]{8})`?\s*\|.*\|\s*removed", line)
            if m:
                removed.add(int(m.group(1), 16))
    # builtins: every function pointer of the target builtin table has a row
    if dst in R.TABLES:
        exe = os.path.join(R.DATA_ROOT, R.EXES[dst])
        if os.path.exists(exe):
            t = R.read_table(R.PE(exe), *R.TABLES[dst]["builtin"])
            have = {int(r["address"], 16) for r in rows}
            miss = [(n, hex(v)) for n, v, _ in t if v not in have]
            print("builtins: %d table entries, %d distinct functions, missing rows: %d"
                  % (len(t), len({v for _, v, _ in t}), len(miss)))
            if miss:
                print("FAIL builtins without a row:", miss)
                ok = False
    conf = Counter()
    for a in src_named:
        if a in mapped:
            conf[mapped[a]["confidence"]] += 1
        elif a in removed:
            conf["removed"] += 1
        elif a in codeaddr:
            conf["code address (%s)" % codeaddr[a]] += 1
        else:
            conf["unaccounted"] += 1
    n = len(src_named)
    print("rows: %d of %d %s functions" % (len(rows), len(D), dst))
    print("v170 named functions (symbols_v170.csv + _render.csv, unique addresses): %d" % n)
    for k in ["high", "medium", "low"] + sorted(k for k in conf if k.startswith("code")) + ["removed", "unaccounted"]:
        print("  %-12s %4d" % (k, conf[k]))
    cov = (n - conf["unaccounted"]) / n * 100
    print("  coverage (mapped or removed): %.1f%%" % cov)
    sub = Counter(r["subsystem"] for r in rows)
    kind = Counter()
    size = Counter()
    for r in rows:
        a = int(r["address"], 16)
        lib = r["subsystem"] in ("crt", "lib")
        if r["v170_address"]:
            k = "matched"
        elif r["name"].startswith("FUN_"):
            k = "unnamed " + ("runtime/library" if lib else "game")
        else:
            k = "no counterpart, named " + ("runtime/library" if lib else "game")
        kind[k] += 1
        size[k] += D[a]["size"]
    print("target rows by kind (count, bytes):")
    for k in sorted(kind):
        print("  %-40s %5d %8d" % (k, kind[k], size[k]))
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
    ap.add_argument("--report", help="write markdown tables (removed, undecided, new) here")
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
    matched_named = sum(1 for a in named if a in M.m or a in M.extra)
    print("v170 named matched: %d of %d, removed %d" % (matched_named, len(named), len(M.removed)),
          file=sys.stderr)
    names_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "names_%s.csv" % args.dst)
    rows = build_rows(M, load_new_names(names_path))
    if args.out:
        write_rows(rows, args.out)
        print("wrote %s (%d rows)" % (args.out, len(rows)), file=sys.stderr)
    if args.report:
        write_report(M, rows, args.report)


def write_report(M, rows, path):
    """Markdown tables for docs/spec/<dst>/symbol-map.md (pasted by hand)."""
    N = M.names
    out = []
    out.append("### Removed v1.70 functions\n")
    out.append("| v1.70 address | v1.70 name | status | confidence | evidence |")
    out.append("|---|---|---|---|---|")
    for s in sorted(M.removed):
        conf, ev = M.removed[s]
        out.append("| `0x%08x` | %s | removed | %s | %s |" % (s, N.get(s, ("?",))[0], conf, ev))
    out.append("\n### Counterparts that are not function entries in the AS2 export\n")
    out.append("| v1.70 address | v1.70 name | AS2 code address | status | confidence | evidence |")
    out.append("|---|---|---|---|---|---|")
    for s in sorted(M.codemap):
        d, conf, ev = M.codemap[s]
        out.append("| `0x%08x` | %s | `0x%08x` | code | %s | %s |" % (s, N.get(s, ("?",))[0], d, conf, ev))
    out.append("\n### Named v1.70 functions without a decision\n")
    out.append("| v1.70 address | v1.70 name | subsystem |")
    out.append("|---|---|---|")
    for s in sorted(N):
        if s not in M.m and s not in M.extra and s not in M.removed and s not in M.codemap:
            out.append("| `0x%08x` | %s | %s |" % (s, N[s][0], N[s][1]))
    out.append("\n### New functions by subsystem\n")
    by = defaultdict(list)
    for r in rows:
        if not r["v170_address"] and r["subsystem"] not in ("crt", "lib"):
            by[r["subsystem"]].append(r)
    for sub in sorted(by):
        out.append("\n#### %s (%d)\n" % (sub, len(by[sub])))
        out.append("| AS2 address | name | description | confidence |")
        out.append("|---|---|---|---|")
        for r in by[sub]:
            out.append("| `%s` | %s | %s | %s |" % (r["address"], r["name"], r["description"], r["confidence"]))
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
