"""Loader for the Ghidra exports in re/out/<tag>/ plus a minimal PE reader.

Used by match_symbols.py. Reads functions.json, strings.json, imports.json and
streams the per-function disassembly files one at a time, turning each function
into a feature record (no decompiled text is kept).

Data root: $AS3D_DATA_ROOT (the main checkout that holds the gitignored
re/out/ and third_party_local/), else the repository root.
"""
import hashlib
import json
import os
import re
import struct
from collections import Counter

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DATA_ROOT = os.environ.get("AS3D_DATA_ROOT", REPO)

# tag -> executable path relative to DATA_ROOT
EXES = {
    "v170": "third_party_local/original/AirStrike3D.exe",
    "as2": "third_party_local/games/as2/AirStrike3D II.exe",
    "gulf": "third_party_local/games/gulf/AirStrike3D II - Gulf.exe",
}

# builtin table (name ptr, func ptr) and script-global table, from re/probes/*.json
TABLES = {
    "v170": {"builtin": (0x00456F70, 85), "global": (0x00457220, 24)},
    "as2": {"builtin": (0x0049D5D0, 101), "global": (0x0049D908, 28)},
    "gulf": {"builtin": (0x0049B448, 101), "global": (0x0049B780, 28)},
}


def h32(x):
    return int(x, 16) if isinstance(x, str) else x


class PE:
    """Just enough PE parsing to read bytes at a virtual address."""

    def __init__(self, path):
        with open(path, "rb") as f:
            self.data = f.read()
        d = self.data
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        nsec = struct.unpack_from("<H", d, pe + 6)[0]
        optsz = struct.unpack_from("<H", d, pe + 20)[0]
        self.base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
        self.sections = []
        off = pe + 24 + optsz
        for i in range(nsec):
            name = d[off:off + 8].rstrip(b"\0").decode("latin1")
            vsize, va, rsize, rptr = struct.unpack_from("<IIII", d, off + 8)
            self.sections.append((name, self.base + va, vsize, rptr, rsize))
            off += 40

    def section_of(self, va):
        for s in self.sections:
            if s[1] <= va < s[1] + max(s[2], s[4]):
                return s[0]
        return None

    def read(self, va, n):
        for name, sva, vsize, rptr, rsize in self.sections:
            if sva <= va < sva + max(vsize, rsize):
                o = va - sva
                if o + n <= rsize:
                    return self.data[rptr + o:rptr + o + n]
                return None
        return None

    def u32(self, va):
        b = self.read(va, 4)
        return struct.unpack("<I", b)[0] if b else None

    def cstr(self, va, maxlen=256):
        b = self.read(va, maxlen)
        if b is None:
            return None
        return b.split(b"\0", 1)[0].decode("latin1")


LINE_RE = re.compile(r"^0x([0-9a-f]{8}) ((?:[0-9a-f]{2} )+)\s*(\S+)\s*(.*)$")
HEX_RE = re.compile(r"-?0x[0-9a-f]+")
MEM_RE = re.compile(r"(byte|word|dword|qword|tword) ptr \[0x([0-9a-f]+)\]")
VLOAD_RE = re.compile(r"^(E[A-Z]X),dword ptr \[E[A-Z]{2}(?: \+ (0x[0-9a-f]+))?\]$")
DISP_RE =re.compile(r"\[(E[A-Z]{2})(?: \+ E[A-Z]{2}\*\d)? \+ (0x[0-9a-f]+)\]")


class Func:
    def __init__(self):
        self.calls = []       # internal call targets, in order of appearance
        self.imports = []     # imported API names called, in order
        self.consts = Counter()
        self.fconsts = Counter()
        self.offsets = Counter()
        self.globals = []
        self.coderefs = []    # code addresses loaded as values, in order
        self.vcalls = Counter()  # indirect calls through [reg + off]: off -> count


def _norm_operand(ops, fn_set):
    def rep(m):
        v = int(m.group(0), 16) if not m.group(0).startswith("-") else -int(m.group(0)[1:], 16)
        if v >= 0x400000:
            return "A" if v not in fn_set else "F"
        return m.group(0)
    return HEX_RE.sub(rep, ops)


def load(tag, root=None):
    """Return dict addr(int) -> Func for export tag, plus aux info."""
    root = root or os.path.join(DATA_ROOT, "re", "out", tag)
    with open(os.path.join(root, "functions.json")) as f:
        fj = json.load(f)
    with open(os.path.join(root, "imports.json")) as f:
        imp = json.load(f)
    iat = {}
    thunk_names = {}
    for dll, lst in imp.items():
        for e in lst:
            iat[h32(e["iat_slot_address"])] = e["name"]
            for t in e.get("thunk_stub_addresses", []):
                thunk_names[h32(t)] = e["name"]
    exe = os.path.join(DATA_ROOT, EXES[tag])
    pe = PE(exe) if os.path.exists(exe) else None
    funcs = {}
    for x in fj:
        fn = Func()
        fn.tag = tag
        fn.addr = h32(x["entry"])
        fn.name = x["name"]
        fn.size = x["size"]
        fn.is_thunk = x["is_thunk"]
        fn.callers = [h32(c) for c in x["callers"]]
        fn.callees = [h32(c["address"]) for c in x["callees"]]
        fn.strings = [s["text"] for s in x["strings"]]
        fn.data_refs = [h32(d) for d in x["data_refs"] if not d.startswith("0xStack")
                        and re.match(r"^0x[0-9a-f]+$", d)]
        funcs[fn.addr] = fn
    fn_set = set(funcs)
    text_lo = text_hi = 0
    if pe is not None:
        for s in pe.sections:
            if s[0] == ".text":
                text_lo, text_hi = s[1], s[1] + s[2]
    ddir = os.path.join(root, "disasm")
    for fname in os.listdir(ddir):
        addr = int(fname[:8], 16)
        fn = funcs.get(addr)
        if fn is None:
            continue
        exact = hashlib.sha1()
        loose = hashlib.sha1()
        mnem = Counter()
        lastload = {}
        n = 0
        with open(os.path.join(ddir, fname), encoding="latin1") as f:
            for line in f:
                m = LINE_RE.match(line)
                if not m:
                    continue
                n += 1
                mn, ops = m.group(3), m.group(4)
                mnem[mn] += 1
                loose.update(mn.encode() + b";")
                if mn == "MOV":
                    mm = VLOAD_RE.match(ops)
                    if mm:
                        lastload[mm.group(1)] = int(mm.group(2) or "0", 16)
                    else:
                        lastload.pop(ops.split(",")[0], None)
                if mn == "CALL":
                    mm = re.match(r"^(E[A-Z]X)$", ops)
                    if mm and mm.group(1) in lastload:
                        fn.vcalls[lastload[mm.group(1)]] += 1
                    mm = re.match(r"^dword ptr \[E[A-Z]X(?: \+ (0x[0-9a-f]+))?\]$", ops)
                    if mm:
                        fn.vcalls[int(mm.group(1) or "0", 16)] += 1
                    mm = re.match(r"^0x([0-9a-f]+)$", ops)
                    if mm:
                        t = int(mm.group(1), 16)
                        if t in thunk_names:
                            fn.imports.append(thunk_names[t])
                        elif t in funcs and funcs[t].is_thunk and not funcs[t].name.startswith("FUN_"):
                            fn.imports.append(funcs[t].name)
                            fn.calls.append(t)
                        else:
                            fn.calls.append(t)
                    mm = re.match(r"^dword ptr \[0x([0-9a-f]+)\]$", ops)
                    if mm and int(mm.group(1), 16) in iat:
                        fn.imports.append(iat[int(mm.group(1), 16)])
                elif not mn.startswith("J") and pe is not None:
                    # code addresses used as values: callbacks (menu actions,
                    # key and draw handlers, thread entry points)
                    for v in HEX_RE.findall(ops):
                        if v.startswith("-"):
                            continue
                        iv = int(v, 16)
                        if (text_lo <= iv < text_hi and not (fn.addr <= iv < fn.addr + fn.size)):
                            fn.coderefs.append(iv)
                norm = mn + " " + (("J" if mn.startswith("J") and ops.startswith("0x")
                                    else _norm_operand(ops, fn_set)))
                exact.update(norm.encode() + b";")
                for v in HEX_RE.findall(ops):
                    iv = int(v, 16) if not v.startswith("-") else -int(v[1:], 16)
                    if -0x10000 < iv < 0x400000 and not mn.startswith("J") and mn != "CALL":
                        if "ESP" in ops or "EBP" in ops:
                            continue
                        fn.consts[iv] += 1
                for dm in DISP_RE.finditer(ops):
                    if dm.group(1) not in ("ESP", "EBP"):
                        fn.offsets[int(dm.group(2), 16)] += 1
                for mm in MEM_RE.finditer(ops):
                    va = int(mm.group(2), 16)
                    fn.globals.append(va)
                    if mn.startswith("F") or mn.endswith("SS") or mn.endswith("SD"):
                        if pe:
                            kind = mm.group(1)
                            if kind in ("dword", "qword") and pe.section_of(va) == ".rdata":
                                b = pe.read(va, 4 if kind == "dword" else 8)
                                if b:
                                    val = struct.unpack("<f" if kind == "dword" else "<d", b)[0]
                                    if val == val and abs(val) < 1e30:
                                        fn.fconsts[round(val, 5)] += 1
                # MOV [x], imm float-looking constants
                if mn in ("MOV", "PUSH"):
                    for v in HEX_RE.findall(ops):
                        if v.startswith("-"):
                            continue
                        iv = int(v, 16)
                        if 0x3A000000 <= iv <= 0x4B000000 or 0xBA000000 <= iv <= 0xCB000000:
                            val = struct.unpack("<f", struct.pack("<I", iv))[0]
                            if val == round(val, 3):
                                fn.fconsts[round(val, 5)] += 1
        fn.ninsn = n
        fn.exact = exact.hexdigest()
        fn.loose = loose.hexdigest()
        fn.mnem = mnem
    return funcs, pe


def read_table(pe, start, count, stride=8):
    """Read (name, value) records: char* name at +0, u32 at +4."""
    out = []
    for i in range(count):
        va = start + i * stride
        np = pe.u32(va)
        v = pe.u32(va + 4)
        out.append((pe.cstr(np), v, va))
    return out
