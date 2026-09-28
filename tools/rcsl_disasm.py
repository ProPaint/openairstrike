#!/usr/bin/env python3
"""Reader, disassembler and re-serialiser for AirStrike 3D compiled scripts (.scr, "RCSL").

See docs/spec/rcsl-container.md and docs/spec/rcsl-opcodes-v0.md.

Usage:
  rcsl_disasm.py dump      <file.scr>     readable listing
  rcsl_disasm.py stats     [dir]          corpus statistics (default: <data>/scripts)
  rcsl_disasm.py roundtrip <file.scr>...  parse, re-serialise, check byte identity
  rcsl_disasm.py summary   <out.json> [dir]  metadata summary (golden file format)

Data is located through AS3D_DATA_ROOT (default: the repository root), then
<root>/assets_extracted/scripts.
"""
import collections
import hashlib
import json
import math
import os
import struct
import sys

MAGIC = 0x4C534352          # "RCSL"
HEADER_SIZE = 0x38          # 14 u32
INSTR_SIZE = 14
NUM_ENTRIES = 5
NO_ENTRY = 0xFFFFFFFF
NUM_TEMPS = 16              # slots 0..15 are saved/restored around event handlers

KNOWN_TAGS = ("CASH", "DEFS", "FUNC", "DATA", "STRG", "CODE")

# Entry point slots (header fields 9..13). See rcsl-container.md for evidence.
ENTRY_NAMES = ("init", "main", "damage", "touch", "callback")

# CASH entry kinds (VERIFIED-CODE: create looks up kind 0, StartSound kind 3).
CASH_KINDS = {0: "object", 3: "sound"}

# DATA entry kinds (VERIFIED-CODE, thread creation 0x41c7d0).
DATA_KINDS = {2: "init", 3: "ref"}

# Mode byte bits (VERIFIED-CODE, operand decoder at 0x419c18).
M_IMM1, M_IND1, M_IMM2, M_IND2, M_IND3 = 0x01, 0x02, 0x10, 0x20, 0x80

# Opcode table. Each entry: mnemonic, operand roles (op1, op2, op3), pseudo-code template,
# confidence tag. Roles:
#   "r"   value read through the generic operand decoder
#   "w"   location written through the generic operand decoder
#   "p"   op1 read as a pointer (LEA base)
#   "fld" raw integer field index (dword units)
#   "rel" raw signed instruction offset relative to this instruction
#   "call" raw callee: negative = FUNC index -(i+1), non-negative = instruction index
#   "raw" raw integer
#   "-"   not used by the handler (still decoded by the generic decoder)
OPCODES = {
    0x00: ("END",   ("-", "-", "-"),      "end (return 0, pc stays here)",  "VERIFIED-CODE"),
    0x01: ("MUL",   ("r", "r", "w"),      "{c} = {a} * {b}",                  "VERIFIED-CODE"),
    0x02: ("DIV",   ("r", "r", "w"),      "{c} = {a} / {b}",                  "VERIFIED-CODE"),
    0x03: ("ADD",   ("r", "r", "w"),      "{c} = {a} + {b}",                  "VERIFIED-CODE"),
    0x04: ("SUB",   ("r", "r", "w"),      "{c} = {a} - {b}",                  "VERIFIED-CODE"),
    0x05: ("BAND",  ("r", "r", "w"),      "{c} = int({a}) & int({b})",        "VERIFIED-CODE"),
    0x06: ("BOR",   ("r", "r", "w"),      "{c} = int({a}) | int({b})",        "VERIFIED-CODE"),
    0x07: ("LOR",   ("r", "r", "w"),      "{c} = int({a}) || int({b})",       "VERIFIED-CODE"),
    0x08: ("LAND",  ("r", "r", "w"),      "{c} = int({a}) && int({b})",       "VERIFIED-CODE"),
    0x09: ("EQ",    ("r", "r", "w"),      "{c} = int({a}) == int({b})",       "VERIFIED-CODE"),
    0x0A: ("NE",    ("r", "r", "w"),      "{c} = int({a}) != int({b})",       "VERIFIED-CODE"),
    0x0B: ("GT",    ("r", "r", "w"),      "{c} = {a} > {b}",                  "VERIFIED-CODE"),
    0x0C: ("LT",    ("r", "r", "w"),      "{c} = {a} < {b}",                  "VERIFIED-CODE"),
    0x0D: ("GE",    ("r", "r", "w"),      "{c} = {a} >= {b}",                 "VERIFIED-CODE"),
    0x0E: ("LE",    ("r", "r", "w"),      "{c} = {a} <= {b}",                 "VERIFIED-CODE"),
    0x0F: ("NOT",   ("r", "-", "w"),      "{c} = !int({a})",                  "VERIFIED-CODE"),
    0x10: ("NEG",   ("r", "-", "w"),      "{c} = -{a}",                       "VERIFIED-CODE"),
    0x11: ("MOV",   ("w", "r", "-"),      "{a} = {b}",                        "VERIFIED-CODE"),
    0x12: ("LEA",   ("p", "fld", "w"),    "{c} = &{a}[{b}]",                  "VERIFIED-CODE"),
    0x13: ("PUSH",  ("r", "-", "-"),      "push {a}",                         "VERIFIED-CODE"),
    0x14: ("POP",   ("-", "w", "-"),      "{b} = pop",                        "VERIFIED-CODE"),
    0x15: ("ALLOC", ("raw", "-", "w"),    "{c} = alloc({a} bytes)",           "VERIFIED-CODE"),
    0x16: ("NOP16", ("-", "-", "-"),      "no operation",                     "VERIFIED-CODE"),
    0x17: ("NOP17", ("-", "-", "-"),      "no operation",                     "VERIFIED-CODE"),
    0x18: ("RET",   ("r", "-", "-"),      "return {a} (status 1)",            "VERIFIED-CODE"),
    0x19: ("JNZ",   ("r", "rel", "-"),    "if {a} != 0 goto {b}",             "VERIFIED-CODE"),
    0x1A: ("JZ",    ("r", "rel", "-"),    "if {a} == 0 goto {b}",             "VERIFIED-CODE"),
    0x1B: ("JMP",   ("rel", "-", "-"),    "goto {a}",                         "VERIFIED-CODE"),
    0x1C: ("CALL",  ("call", "w", "-"),   "{b} = {a}(t0...)",                 "VERIFIED-CODE"),
    0x1D: ("LCALL", ("call", "w", "-"),   "{b} = latent {a}(t0...)",          "VERIFIED-CODE"),
    0x1E: ("TMO",   ("r", "-", "-"),      "timeout = {a} (for next LCALL)",   "VERIFIED-CODE"),
}

# Every (opcode, mode) combination found in the v1.70 corpus, with its count.
# Must match docs/spec/rcsl-opcodes-v0.md (checked by tools/ref/test_rcsl.py).
SEEN_MODES = {
    (0x00, 0x00): 1208,
    (0x01, 0x00): 41, (0x01, 0x01): 400, (0x01, 0x02): 6, (0x01, 0x10): 123,
    (0x01, 0x12): 24, (0x01, 0x20): 1, (0x01, 0x21): 93, (0x01, 0x92): 2,
    (0x02, 0x00): 20, (0x02, 0x10): 48, (0x02, 0x12): 103,
    (0x03, 0x00): 94, (0x03, 0x01): 95, (0x03, 0x02): 32, (0x03, 0x10): 98,
    (0x03, 0x12): 15, (0x03, 0x82): 122, (0x03, 0x92): 136, (0x03, 0xA2): 3,
    (0x04, 0x00): 1, (0x04, 0x01): 26, (0x04, 0x02): 24, (0x04, 0x10): 41,
    (0x04, 0x12): 33, (0x04, 0x20): 1, (0x04, 0x21): 31, (0x04, 0x22): 34,
    (0x04, 0x82): 98, (0x04, 0x92): 3,
    (0x05, 0x10): 96,
    (0x07, 0x20): 3,
    (0x08, 0x00): 14,
    (0x09, 0x10): 112, (0x09, 0x12): 40,
    (0x0A, 0x10): 6,
    (0x0B, 0x00): 4, (0x0B, 0x02): 28, (0x0B, 0x10): 54, (0x0B, 0x12): 165,
    (0x0B, 0x20): 2, (0x0B, 0x22): 2,
    (0x0C, 0x00): 19, (0x0C, 0x02): 47, (0x0C, 0x10): 154, (0x0C, 0x12): 121,
    (0x0C, 0x20): 2, (0x0C, 0x22): 2,
    (0x0D, 0x02): 3, (0x0D, 0x10): 148, (0x0D, 0x12): 102, (0x0D, 0x22): 17,
    (0x0E, 0x00): 24, (0x0E, 0x12): 11, (0x0E, 0x22): 1,
    (0x0F, 0x00): 110, (0x0F, 0x02): 18,
    (0x10, 0x01): 123, (0x10, 0x02): 2,
    (0x11, 0x00): 1619, (0x11, 0x10): 2909, (0x11, 0x20): 193, (0x11, 0x82): 469,
    (0x11, 0x92): 740, (0x11, 0xA2): 17,
    (0x12, 0x00): 5521,
    (0x13, 0x00): 450, (0x13, 0x80): 170,
    (0x14, 0x00): 424, (0x14, 0x80): 196,
    (0x18, 0x00): 502,
    (0x1A, 0x00): 1317, (0x1A, 0x02): 179,
    (0x1B, 0x00): 391,
    (0x1C, 0x00): 3189,
    (0x1D, 0x00): 624,
    (0x1E, 0x00): 12, (0x1E, 0x01): 341, (0x1E, 0x02): 26,
}

# Entity field indices (dword offsets from the value held by `self`), only those confirmed
# from builtins in the executable. See rcsl-opcodes-v0.md.
ENTITY_FIELDS = {
    1: "age", 2: "class", 3: "flags", 4: "dead", 5: "origin", 8: "attach_offset",
    11: "prev_origin", 14: "angles", 17: "velocity", 20: "field20", 23: "wp_speed",
    24: "wp_turn_rate", 25: "wp_bank", 28: "color", 32: "scale", 33: "frame?",
    34: "health", 35: "damage", 36: "score?", 37: "wp_wait",
}   # rcsl-vm.md "Entity fields"; names ending in "?" are GUESS
CAMERA_FIELDS = {0: "position", 3: "angles", 6: "vel_x", 7: "scroll_speed",
                 9: "scroll_factor"}
ENTITY_GLOBALS = ("self", "other", "player")


class RcslError(ValueError):
    pass


def data_root():
    env = os.environ.get("AS3D_DATA_ROOT")
    if env:
        return env
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def scripts_dir():
    return os.path.join(data_root(), "assets_extracted", "scripts")


def f32(raw):
    """Reinterprets a u32/i32 bit pattern as an IEEE single."""
    return struct.unpack("<f", struct.pack("<I", raw & 0xFFFFFFFF))[0]


class Instr:
    __slots__ = ("op", "mode", "a", "b", "c")

    def __init__(self, op, mode, a, b, c):
        self.op, self.mode, self.a, self.b, self.c = op, mode, a, b, c

    def pack(self):
        return struct.pack("<BBiii", self.op, self.mode, self.a, self.b, self.c)

    def __repr__(self):
        return f"Instr({self.op:#04x},{self.mode:#04x},{self.a},{self.b},{self.c})"


class Script:
    """In-memory model of an RCSL file."""

    def __init__(self):
        self.header = []        # 14 u32
        self.order = []         # section tags in file order
        self.raw_sections = {}  # tag -> bytes, for tags we do not decode
        self.cash = []          # [(kind, name)]
        self.defs = []          # [name]
        self.funcs = []         # [name]
        self.data = []          # [(kind, slot, value_u32)]
        self.strg = b""
        self.code = []          # [Instr]

    # header accessors
    @property
    def frame_slots(self):
        return self.header[6]

    @property
    def entries(self):
        return list(self.header[9:9 + NUM_ENTRIES])

    def string_starts(self):
        starts = set()
        if self.strg:
            starts.add(0)
            for i, ch in enumerate(self.strg[:-1]):
                if ch == 0:
                    starts.add(i + 1)
        return starts

    def string_at(self, off):
        end = self.strg.index(b"\0", off)
        return self.strg[off:end].decode("cp1251")


def _read_names(payload, count, with_kind, tag):
    out = []
    o = 0
    for _ in range(count):
        kind = None
        if with_kind:
            if o >= len(payload):
                raise RcslError(f"{tag}: truncated")
            kind = payload[o]
            o += 1
        if o >= len(payload):
            raise RcslError(f"{tag}: truncated")
        n = payload[o]
        o += 1
        s = payload[o:o + n]
        if n < 1 or len(s) != n or s[-1] != 0 or 0 in s[:-1]:
            raise RcslError(f"{tag}: bad name encoding at {o - 1}")
        out.append((kind, s[:-1].decode("cp1251")) if with_kind else s[:-1].decode("cp1251"))
        o += n
    if o != len(payload):
        raise RcslError(f"{tag}: {len(payload) - o} trailing bytes")
    return out


def _write_name(name):
    b = name.encode("cp1251") + b"\0"
    return bytes([len(b)]) + b


def parse(d):
    """Parses an RCSL file. Raises RcslError on anything not matching the spec."""
    if len(d) < HEADER_SIZE:
        raise RcslError("file shorter than header")
    s = Script()
    s.header = list(struct.unpack_from("<14I", d, 0))
    if s.header[0] != MAGIC:
        raise RcslError("bad magic")
    h = s.header
    off = HEADER_SIZE
    payloads = {}
    while off < len(d):
        if off + 8 > len(d):
            raise RcslError(f"truncated section header at {off:#x}")
        tag = d[off:off + 4].decode("latin1")
        ln = struct.unpack_from("<I", d, off + 4)[0]
        if off + 8 + ln > len(d):
            raise RcslError(f"section {tag} at {off:#x} overruns file")
        if tag in payloads:
            raise RcslError(f"duplicate section {tag}")
        payloads[tag] = d[off + 8:off + 8 + ln]
        s.order.append(tag)
        off += 8 + ln
    for tag, p in payloads.items():
        if tag not in KNOWN_TAGS:
            s.raw_sections[tag] = p
    if "CASH" in payloads:
        s.cash = _read_names(payloads["CASH"], h[2], True, "CASH")
    elif h[2]:
        raise RcslError("CASH count without CASH section")
    if "DEFS" in payloads:
        s.defs = _read_names(payloads["DEFS"], h[3], False, "DEFS")
    elif h[3]:
        raise RcslError("DEFS count without DEFS section")
    if "FUNC" in payloads:
        s.funcs = _read_names(payloads["FUNC"], h[4], False, "FUNC")
    elif h[4]:
        raise RcslError("FUNC count without FUNC section")
    p = payloads.get("DATA", b"")
    if len(p) != 8 * h[5]:
        raise RcslError("DATA length != 8 * count")
    s.data = [struct.unpack_from("<HhI", p, i) for i in range(0, len(p), 8)]
    s.strg = payloads.get("STRG", b"")
    if len(s.strg) != h[7]:
        raise RcslError("STRG length != header size")
    if s.strg and s.strg[-1] != 0:
        raise RcslError("STRG not NUL-terminated")
    p = payloads.get("CODE")
    if p is None:
        raise RcslError("no CODE section")
    if len(p) != INSTR_SIZE * h[8]:
        raise RcslError("CODE length != 14 * instruction count")
    s.code = [Instr(*struct.unpack_from("<BBiii", p, i)) for i in range(0, len(p), INSTR_SIZE)]
    for e in s.entries:
        if e != NO_ENTRY and e >= h[8]:
            raise RcslError(f"entry point {e} out of range")
    return s


def serialise(s):
    h = list(s.header)
    out = [struct.pack("<14I", *h)]
    for tag in s.order:
        if tag == "CASH":
            p = b"".join(bytes([k]) + _write_name(n) for k, n in s.cash)
        elif tag == "DEFS":
            p = b"".join(_write_name(n) for n in s.defs)
        elif tag == "FUNC":
            p = b"".join(_write_name(n) for n in s.funcs)
        elif tag == "DATA":
            p = b"".join(struct.pack("<HhI", *e) for e in s.data)
        elif tag == "STRG":
            p = s.strg
        elif tag == "CODE":
            p = b"".join(i.pack() for i in s.code)
        else:
            p = s.raw_sections[tag]
        out.append(tag.encode("latin1") + struct.pack("<I", len(p)) + p)
    return b"".join(out)


def load(path):
    with open(path, "rb") as f:
        return parse(f.read())


# ---------------------------------------------------------------------------
# Operand classification

class Operand:
    """kind: slot | global | imm | rel | func | sub | field | raw | unused"""
    __slots__ = ("kind", "value", "deref", "role")

    def __init__(self, kind, value, deref=False, role="-"):
        self.kind, self.value, self.deref, self.role = kind, value, deref, role


def _generic(v, imm, ind):
    if imm:
        return Operand("imm", v, ind)
    if v < 0:
        return Operand("global", -v - 1, ind)
    return Operand("slot", v, ind)


def operands(ins):
    """Classifies the three operands of an instruction following the interpreter's decoder.

    Operands the handler does not use are returned with role '-' but still classified the
    way the generic decoder would see them."""
    op, m = ins.op, ins.mode
    roles = OPCODES[op][1] if op in OPCODES else ("-", "-", "-")
    res = []
    # op1
    if roles[0] == "rel":
        o1 = Operand("rel", ins.a)
    elif roles[0] == "call":
        o1 = Operand("func", -ins.a - 1) if ins.a < 0 else Operand("sub", ins.a)
    elif roles[0] == "raw":
        o1 = Operand("raw", ins.a)
    else:
        o1 = _generic(ins.a, m & M_IMM1, m & M_IND1)
    o1.role = roles[0]
    res.append(o1)
    # op2
    if roles[1] == "rel":
        o2 = Operand("rel", ins.b)
    elif roles[1] == "fld":
        o2 = Operand("field", ins.b)
    else:
        o2 = _generic(ins.b, m & M_IMM2, m & M_IND2)
    o2.role = roles[1]
    res.append(o2)
    # op3: never immediate
    o3 = _generic(ins.c, False, m & M_IND3)
    o3.role = roles[2]
    res.append(o3)
    return res


def check_bounds(s):
    """Returns a list of problems with operands that refer to tables or code."""
    problems = []
    n = len(s.code)
    starts = s.string_starts()
    for i, ins in enumerate(s.code):
        if ins.op not in OPCODES:
            problems.append(f"{i}: unknown opcode {ins.op:#x}")
            continue
        for k, o in enumerate(operands(ins)):
            if o.role == "-":
                continue
            if o.kind == "global" and o.value >= len(s.defs):
                problems.append(f"{i}: op{k+1} DEFS index {o.value} out of range")
            elif o.kind == "slot" and o.value >= s.frame_slots:
                problems.append(f"{i}: op{k+1} slot {o.value} >= frame size {s.frame_slots}")
            elif o.kind == "func" and o.value >= len(s.funcs):
                problems.append(f"{i}: op{k+1} FUNC index {o.value} out of range")
            elif o.kind == "sub" and o.value >= n:
                problems.append(f"{i}: op{k+1} call target {o.value} out of range")
            elif o.kind == "rel" and not 0 <= i + o.value < n:
                problems.append(f"{i}: op{k+1} jump target {i + o.value} out of range")
            elif o.kind == "imm" and is_string_ref(s, o.value) is False:
                problems.append(f"{i}: op{k+1} immediate {o.value:#x} looks like a string "
                                "offset but is not a string start")
        if ins.op == 0x12 and ins.b < 0:
            problems.append(f"{i}: negative field index")
    for k, slot, val in s.data:
        if not NUM_TEMPS <= slot < s.frame_slots:
            problems.append(f"DATA slot {slot} out of range")
        if k == 3 and not NUM_TEMPS <= val < s.frame_slots:
            problems.append(f"DATA ref target {val} out of range")
        if k == 2 and is_string_ref(s, val) is False:
            problems.append(f"DATA value {val:#x} looks like a string offset but is not one")
    return problems


def is_string_ref(s, raw):
    """True if an immediate is a STRG offset, False if it has the shape of one (small raw
    integer, i.e. an IEEE denormal) but is not a string start, None if it is a float.
    Raw 0 is ambiguous (0.0 or offset 0) and returns None."""
    raw &= 0xFFFFFFFF
    if raw == 0 or raw >= 0x00800000:
        return None
    return raw < len(s.strg) and raw in s.string_starts()


# ---------------------------------------------------------------------------
# Listing

def fmt_float(raw):
    v = f32(raw)
    if math.isnan(v) or math.isinf(v):
        return f"0x{raw & 0xFFFFFFFF:08x}"
    # shortest decimal that round-trips through float32
    for prec in range(1, 10):
        t = float(f"{v:.{prec}g}")
        if struct.pack("<f", t) == struct.pack("<I", raw & 0xFFFFFFFF):
            return repr(t)
    return repr(v)


def slot_name(n):
    return f"t{n}" if n < NUM_TEMPS else f"v{n}"


def fmt_operand(s, o, index, labels):
    if o.kind == "slot":
        t = slot_name(o.value)
    elif o.kind == "global":
        t = "$" + (s.defs[o.value] if o.value < len(s.defs) else f"defs{o.value}?")
    elif o.kind == "imm":
        sr = is_string_ref(s, o.value)
        if sr:
            t = f'str@{o.value}"{s.string_at(o.value)}"'
        else:
            t = "#" + fmt_float(o.value)
    elif o.kind == "rel":
        t = labels.get(index + o.value, f"@{index + o.value}")
    elif o.kind == "func":
        t = s.funcs[o.value] if o.value < len(s.funcs) else f"func{o.value}?"
    elif o.kind == "sub":
        t = labels.get(o.value, f"sub_{o.value:04d}")
    elif o.kind == "field":
        t = str(o.value)
    else:
        t = str(o.value)
    if o.deref:
        t = "[" + t + "]"
    return t


def make_labels(s):
    labels = {}
    for k, e in enumerate(s.entries):
        if e != NO_ENTRY:
            labels.setdefault(e, f"on_{ENTRY_NAMES[k]}")
    for i, ins in enumerate(s.code):
        if ins.op not in OPCODES:
            continue
        for o in operands(ins):
            if o.kind == "rel" and o.role == "rel":
                labels.setdefault(i + o.value, f"L{i + o.value:04d}")
            elif o.kind == "sub":
                labels.setdefault(o.value, f"sub_{o.value:04d}")
    return labels


_BUILTINS = None


def builtin_info():
    """Builtin signatures from testdata/golden/rcsl_builtins.json (empty if missing)."""
    global _BUILTINS
    if _BUILTINS is None:
        path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                            "testdata", "golden", "rcsl_builtins.json")
        try:
            with open(path) as f:
                _BUILTINS = {b["name"]: b for b in json.load(f)["builtins"]}
        except (OSError, ValueError, KeyError):
            _BUILTINS = {}
    return _BUILTINS


def fmt_instr(s, i, ins, labels):
    if ins.op not in OPCODES:
        return f"??{ins.op:02x}  {ins.a} {ins.b} {ins.c}"
    mnem, roles, tmpl, _ = OPCODES[ins.op]
    ops = operands(ins)
    txt = [fmt_operand(s, o, i, labels) for o in ops]
    if ins.op in (0x1C, 0x1D) and ops[0].kind == "func":
        info = builtin_info().get(txt[0])
        if info is not None:
            args = ", ".join(f"t{k}:{a['name']}" for k, a in enumerate(info["args"]))
            txt[0] = f"{txt[0]}({args})"
        tmpl = tmpl.replace("(t0...)", "")
    body = tmpl.format(a=txt[0], b=txt[1], c=txt[2])
    notes = []
    if ins.op == 0x12:
        base = ops[0]
        if base.kind == "global" and not base.deref and base.value < len(s.defs) \
                and s.defs[base.value] in ENTITY_GLOBALS and ins.b in ENTITY_FIELDS:
            notes.append(f"{s.defs[base.value]}.{ENTITY_FIELDS[ins.b]}")
        elif base.kind == "global" and not base.deref and base.value < len(s.defs) \
                and s.defs[base.value] == "camera" and ins.b in CAMERA_FIELDS:
            notes.append(f"camera.{CAMERA_FIELDS[ins.b]}")
    if ins.op == 0x11 and ops[1].kind == "imm" and ins.b == 0 and s.strg:
        notes.append(f'or str@0"{s.string_at(0)}"')
    if ins.op == 0x14 and (ins.a or ins.c):
        notes.append(f"op1/op3={ins.a}/{ins.c} ignored")
    if ins.op == 0x1D and ops[0].kind == "sub":
        notes.append("repeat each frame until RET")
    unused = [k for k, o in enumerate(ops) if o.role == "-" and (ins.a, ins.b, ins.c)[k] != 0
              and not (ins.op == 0x14)]
    if unused:
        notes.append("unused nonzero op" + ",".join(str(k + 1) for k in unused))
    line = f"{mnem:<6}{body}"
    if notes:
        line = f"{line:<48} ; " + "; ".join(notes)
    return line


def dump(s, name="", out=sys.stdout):
    h = s.header
    p = lambda *a: print(*a, file=out)
    p(f"; {name}")
    p(f"; header: magic RCSL, field1={h[1]}, cash={h[2]}, defs={h[3]}, func={h[4]}, "
      f"data={h[5]}, frame_slots={h[6]}, strg_bytes={h[7]}, instructions={h[8]}")
    for k, e in enumerate(s.entries):
        p(f";   entry {k} {ENTRY_NAMES[k]:<8} = {'-' if e == NO_ENTRY else e}")
    p(f"; sections: " + " ".join(s.order))
    if s.cash:
        p("; CASH")
        for k, (kind, n) in enumerate(s.cash):
            p(f";   {k:3d} {CASH_KINDS.get(kind, f'kind{kind}'):<6} {n}")
    if s.defs:
        p("; DEFS (globals, operand -(i+1))")
        for k, n in enumerate(s.defs):
            p(f";   {k:3d} ${n}")
    if s.funcs:
        p("; FUNC (builtins, call operand -(i+1))")
        for k, n in enumerate(s.funcs):
            p(f";   {k:3d} {n}")
    if s.data:
        p("; DATA (slot initialisers)")
        for kind, slot, val in s.data:
            if kind == 2:
                sr = is_string_ref(s, val)
                v = f'str@{val}"{s.string_at(val)}"' if sr else fmt_float(val)
                p(f";   {slot_name(slot)} = {v}")
            elif kind == 3:
                p(f";   {slot_name(slot)} = &{slot_name(val)}")
            else:
                p(f";   kind{kind} slot {slot} value {val:#x}")
    if s.strg:
        p("; STRG")
        for off in sorted(s.string_starts()):
            p(f";   @{off:<5d} \"{s.string_at(off)}\"")
    p("; CODE")
    labels = make_labels(s)
    for i, ins in enumerate(s.code):
        if i in labels:
            p(f"{labels[i]}:")
        raw = ins.pack().hex()
        p(f"  {i:4d}  {raw}  {fmt_instr(s, i, ins, labels)}")


# ---------------------------------------------------------------------------
# Corpus

def corpus(root=None):
    root = root or scripts_dir()
    out = []
    for dp, dn, fn in os.walk(root):
        dn.sort()
        for f in sorted(fn):
            if f.lower().endswith(".scr"):
                out.append(os.path.join(dp, f))
    out.sort(key=lambda p: os.path.relpath(p, root).lower())
    return out


def pak_name(path, root):
    rel = os.path.relpath(path, root).replace(os.sep, "\\")
    return "scripts\\" + rel


def summary(s, raw):
    return {
        "size": len(raw),
        "sha1": hashlib.sha1(raw).hexdigest(),
        "sections": [[t, _section_len(s, t)] for t in s.order],
        "header1": s.header[1],
        "counts": {"cash": len(s.cash), "defs": len(s.defs), "func": len(s.funcs),
                   "data": len(s.data), "strg_bytes": len(s.strg),
                   "frame_slots": s.frame_slots, "instructions": len(s.code)},
        "entries": {ENTRY_NAMES[k]: (None if e == NO_ENTRY else e)
                    for k, e in enumerate(s.entries)},
        "func": list(s.funcs),
        "defs": list(s.defs),
    }


def _section_len(s, tag):
    if tag == "CASH":
        return sum(2 + len(n.encode("cp1251")) + 1 for _, n in s.cash)
    if tag in ("DEFS", "FUNC"):
        names = s.defs if tag == "DEFS" else s.funcs
        return sum(1 + len(n.encode("cp1251")) + 1 for n in names)
    if tag == "DATA":
        return 8 * len(s.data)
    if tag == "STRG":
        return len(s.strg)
    if tag == "CODE":
        return INSTR_SIZE * len(s.code)
    return len(s.raw_sections[tag])


def build_summary(root=None):
    root = root or scripts_dir()
    res = {}
    for path in corpus(root):
        with open(path, "rb") as f:
            raw = f.read()
        res[pak_name(path, root)] = summary(parse(raw), raw)
    return res


def stats(root=None, out=sys.stdout):
    root = root or scripts_dir()
    p = lambda *a: print(*a, file=out)
    files = corpus(root)
    ops = collections.Counter()
    combos = collections.Counter()
    seqs = collections.Counter()
    entries = collections.Counter()
    funcs = collections.Counter()
    defs = collections.Counter()
    cash_kinds = collections.Counter()
    data_kinds = collections.Counter()
    header1 = collections.Counter()
    problems = 0
    for path in files:
        s = load(path)
        seqs[" ".join(s.order)] += 1
        header1[s.header[1]] += 1
        for k, e in enumerate(s.entries):
            if e != NO_ENTRY:
                entries[ENTRY_NAMES[k]] += 1
        funcs.update(s.funcs)
        defs.update(s.defs)
        cash_kinds.update(k for k, _ in s.cash)
        data_kinds.update(k for k, _, _ in s.data)
        for ins in s.code:
            ops[ins.op] += 1
            combos[(ins.op, ins.mode)] += 1
        problems += len(check_bounds(s))
    p(f"scripts: {len(files)}")
    p(f"instructions: {sum(ops.values())}, opcodes: {len(ops)}, (opcode,mode) pairs: {len(combos)}")
    p(f"header field 1 values: {dict(header1)}")
    p("section sequences:")
    for k, v in seqs.most_common():
        p(f"  {v:4d}  {k}")
    p("entry points populated:")
    for k in ENTRY_NAMES:
        p(f"  {k:<9}{entries[k]}")
    p(f"CASH kinds: { {CASH_KINDS.get(k, k): v for k, v in cash_kinds.items()} }")
    p(f"DATA kinds: { {DATA_KINDS.get(k, k): v for k, v in data_kinds.items()} }")
    p(f"distinct FUNC names: {len(funcs)}; distinct DEFS names: {len(defs)}")
    p("opcodes:")
    for op in sorted(ops):
        m = OPCODES.get(op, ("??",))[0]
        modes = ", ".join(f"{md:02x}:{c}" for (o, md), c in sorted(combos.items()) if o == op)
        p(f"  {op:02x} {m:<6}{ops[op]:6d}   modes {modes}")
    p("builtins by script count:")
    for k, v in funcs.most_common():
        p(f"  {v:4d}  {k}")
    p("globals by script count:")
    for k, v in defs.most_common():
        p(f"  {v:4d}  {k}")
    p(f"bounds problems: {problems}")


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd = argv[1]
    if cmd == "dump" and len(argv) == 3:
        dump(load(argv[2]), argv[2])
        return 0
    if cmd == "stats" and len(argv) <= 3:
        stats(argv[2] if len(argv) == 3 else None)
        return 0
    if cmd == "roundtrip" and len(argv) >= 3:
        bad = 0
        for path in argv[2:]:
            with open(path, "rb") as f:
                raw = f.read()
            ok = serialise(parse(raw)) == raw
            bad += not ok
            print(f"{'OK  ' if ok else 'FAIL'} {path}")
        return 1 if bad else 0
    if cmd == "summary" and len(argv) in (3, 4):
        res = build_summary(argv[3] if len(argv) == 4 else None)
        with open(argv[2], "w") as f:
            json.dump(res, f, indent=1, sort_keys=True)
            f.write("\n")
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
