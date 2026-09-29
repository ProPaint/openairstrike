#!/usr/bin/env python3
"""Runs front-end code of the AirStrike 2 executable (or v1.70, Gulf Thunder) in a CPU emulator
and records what it queues for the 2D layer, to read screen layouts without hand-decoding
floating-point code.

Not part of the build and not needed by the engine: a reverse-engineering aid for the front-end
specs (docs/spec/as2/frontend.md). It needs the Unicorn engine (pip install unicorn; keep it
outside the repository, e.g. a venv under ~/tools) and the owner's executable under
$AS3D_DATA_ROOT/third_party_local/.

How it works: the PE sections are mapped at their virtual addresses (uninitialised data zeroed,
no CRT start-up, no Windows), every import slot points at a stub that returns 0, and a small
set of functions is replaced by Python (texture and sound registration, the C runtime's rand
and sprintf, the 2D list flush). A test calls a function with chosen registers and stack
arguments and runs it until it returns. The 2D queue that the game fills (count and records at
addresses given per game in GAMES) is then read back as a list of quads, and every call of the
text routines is logged with its string, position, colour and flags.

Library use:
    from emu2d import Emu
    e = Emu('as2')
    e.call(0x42b1f0)                      # UI_LoadAssets: registers the menu textures
    e.call(0x42b950)                      # builds the main menu
    ...
    for q in e.frames[-1]: print(q)       # quads of the last flushed 2D list
    for t in e.texts: print(t)            # strings drawn

Only what the front-end drawing code needs is emulated; code that reaches Direct3D, files or
the window returns 0 from the stubs, which may take other paths than the real game.
"""
import os
import struct
import sys

from unicorn import Uc, UcError, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_UNMAPPED
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
                               UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP,
                               UC_X86_REG_EIP)

DATA_ROOT = os.environ.get("AS3D_DATA_ROOT", os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))

# Per game: executable, the 2D queue (count, records, stride), texture registry (base of entry
# 0, stride, offsets of width and height), and the functions replaced by Python.
GAMES = {
    "as2": {
        "exe": "third_party_local/games/as2/AirStrike3D II.exe",
        "queue_count": 0x021133FC, "queue": 0x02114420, "stride": 0x54,
        "tex_table": 0x021D3138, "tex_stride": 0x8C, "tex_w": 0x80, "tex_h": 0x84, "tex_count": 0x022191C0,
        "register_texture": [0x004384B0, 0x00438550],   # name in ECX, returns handle
        "register_sound": 0x00422AF0,                   # name in ECX (checked at run time)
        "play_sound": [0x00422A50, 0x00422950],
        "rand": 0x0043ADAA, "sprintf": 0x004394CC, "flush2d": 0x004309B0,
        "strings": {0x00425D50: "add", 0x00426230: "alpha", 0x004263E0: "number"},
        "stubs": [0x00422880, 0x004223B0, 0x00422760, 0x004156B0],  # music, stop, log
        "malloc": [0x0043945D, 0x0043A07E], "calloc": 0x00445D32, "free": [0x00439956],
        "fopen": 0x0043A29A,
    },
}

STACK_TOP = 0x7FF00000
STACK_SIZE = 0x100000
STUB_BASE = 0x7E000000
RET_SENTINEL = 0x7E0FFFF0
HEAP_BASE = 0x60000000
HEAP_SIZE = 0x01000000


class Quad:
    FIELDS = ("x", "y", "w", "h", "s0", "t0", "s1", "t1", "s0b", "t0b", "s1b", "t1b")

    def __init__(self, raw, tex_names):
        f = struct.unpack_from("<12f", raw, 0)
        for k, v in zip(self.FIELDS, f):
            setattr(self, k, v)
        self.tex, self.tex2 = struct.unpack_from("<ii", raw, 0x30)
        self.rgba = struct.unpack_from("<4f", raw, 0x38)
        self.rot = struct.unpack_from("<f", raw, 0x48)[0]
        self.blend, self.combine = struct.unpack_from("<ii", raw, 0x4C)
        self.tex_name = tex_names.get(self.tex, {0: "fill", -1: "line", -2: "outline"}.get(self.tex, str(self.tex)))
        self.tex2_name = tex_names.get(self.tex2, "") if self.tex2 else ""

    def __repr__(self):
        c = "rgba(%.3g,%.3g,%.3g,%.3g)" % self.rgba
        s = "%-26s (%.2f, %.2f, %.2f, %.2f) uv(%.4f, %.4f, %.4f, %.4f) %s blend %d" % (
            self.tex_name, self.x, self.y, self.w, self.h, self.s0, self.t0, self.s1, self.t1, c, self.blend)
        if self.rot:
            s += " rot %.3f" % self.rot
        if self.tex2:
            s += " +%s uv(%.4f, %.4f, %.4f, %.4f) combine %d" % (self.tex2_name, self.s0b, self.t0b, self.s1b,
                                                                self.t1b, self.combine)
        return s


class Emu:
    def __init__(self, game="as2", trace_strings=True):
        self.g = GAMES[game]
        path = os.path.join(DATA_ROOT, self.g["exe"])
        self.data = open(path, "rb").read()
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.hooks = {}
        self.frames = []       # snapshots of the 2D queue at each flush
        self.texts = []        # (kind, string, stack args (x, y, colour, ..), ECX flags, EDX)
        self.text_ptrs = []    # address of each logged string
        self.sounds = []
        self.tex_names = {}
        self.tex_by_name = {}
        self.snd_names = {}
        self.rand_state = 1
        self.trace_strings = trace_strings
        self._load()
        self._install()

    # -- memory ----------------------------------------------------------------------------
    def _load(self):
        d = self.data
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        nsec = struct.unpack_from("<H", d, pe + 6)[0]
        opt = struct.unpack_from("<H", d, pe + 20)[0]
        self.base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
        imp_rva = struct.unpack_from("<I", d, pe + 24 + 104)[0]
        self.uc.mem_map(self.base, 0x1000)
        self.uc.mem_write(self.base, d[:0x1000])
        o = pe + 24 + opt
        self.sections = []
        for _ in range(nsec):
            name = d[o:o + 8].rstrip(b"\0").decode("latin-1")
            vsize, va, rsize, roff = struct.unpack_from("<IIII", d, o + 8)
            size = (max(vsize, rsize) + 0xFFF) & ~0xFFF
            self.uc.mem_map(self.base + va, size)
            self.uc.mem_write(self.base + va, d[roff:roff + min(rsize, size)])
            self.sections.append((name, self.base + va, size))
            o += 40
        self.uc.mem_map(STACK_TOP - STACK_SIZE, STACK_SIZE)
        self._setup_fs()
        self.uc.mem_map(STUB_BASE, 0x100000)
        self.uc.mem_write(STUB_BASE, b"\xC3" * 0x100000)
        # import slots -> stubs
        self.imports = {}
        n = 0
        while imp_rva:
            desc = self._rva_off(imp_rva)
            ilt, _, _, name_rva, iat = struct.unpack_from("<IIIII", d, desc)
            if not iat:
                break
            dll = d[self._rva_off(name_rva):].split(b"\0")[0].decode()
            k = 0
            while True:
                ent = struct.unpack_from("<I", d, self._rva_off((ilt or iat) + 4 * k))[0]
                if not ent:
                    break
                fname = ("#%d" % (ent & 0xFFFF)) if ent & 0x80000000 else \
                    d[self._rva_off(ent) + 2:].split(b"\0")[0].decode()
                stub = STUB_BASE + 16 * n
                self.uc.mem_write(self.base + iat + 4 * k, struct.pack("<I", stub))
                self.imports[stub] = "%s!%s" % (dll, fname)
                n += 1
                k += 1
            imp_rva += 20

    def _setup_fs(self):
        """A flat GDT with an FS segment over a zeroed thread block (SEH list head at fs:[0])."""
        from unicorn.x86_const import UC_X86_REG_GDTR, UC_X86_REG_FS, UC_X86_REG_DS, UC_X86_REG_SS, \
            UC_X86_REG_CS, UC_X86_REG_ES
        gdt, teb = 0x7D000000, 0x7D010000
        self.uc.mem_map(gdt, 0x10000)
        self.uc.mem_map(teb, 0x10000)

        def desc(base, limit, access, flags):
            return struct.pack("<HHBBBB", limit & 0xFFFF, base & 0xFFFF, (base >> 16) & 0xFF, access,
                               ((limit >> 16) & 0xF) | (flags << 4), (base >> 24) & 0xFF)
        entries = [b"\0" * 8,
                   desc(0, 0xFFFFF, 0x9B, 0xC),   # 1 code
                   desc(0, 0xFFFFF, 0x93, 0xC),   # 2 data
                   desc(teb, 0xFFF, 0x93, 0x4)]   # 3 fs
        self.uc.mem_write(gdt, b"".join(entries))
        self.uc.reg_write(UC_X86_REG_GDTR, (0, gdt, len(entries) * 8 - 1, 0))
        self.uc.reg_write(UC_X86_REG_FS, 3 << 3)
        for r in (UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS):
            self.uc.reg_write(r, 2 << 3)
        self.w32(teb, 0xFFFFFFFF)
        self.w32(teb + 0x18, teb)

    def _rva_off(self, rva):
        d = self.data
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        nsec = struct.unpack_from("<H", d, pe + 6)[0]
        opt = struct.unpack_from("<H", d, pe + 20)[0]
        o = pe + 24 + opt
        for _ in range(nsec):
            vsize, va, rsize, roff = struct.unpack_from("<IIII", d, o + 8)
            if va <= rva < va + max(vsize, rsize):
                return roff + rva - va
            o += 40
        raise ValueError("rva 0x%x" % rva)

    def r32(self, a):
        return struct.unpack("<I", self.uc.mem_read(a, 4))[0]

    def ri32(self, a):
        return struct.unpack("<i", self.uc.mem_read(a, 4))[0]

    def rf32(self, a):
        return struct.unpack("<f", self.uc.mem_read(a, 4))[0]

    def w32(self, a, v):
        self.uc.mem_write(a, struct.pack("<I", v & 0xFFFFFFFF))

    def wf32(self, a, v):
        self.uc.mem_write(a, struct.pack("<f", v))

    def w8(self, a, v):
        self.uc.mem_write(a, bytes([v & 0xFF]))

    def cstr(self, a, lim=512):
        if not a:
            return None
        try:
            b = bytes(self.uc.mem_read(a, lim))
        except UcError:
            return None
        return b.split(b"\0")[0].decode("latin-1")

    def reg(self, r):
        return self.uc.reg_read(r)

    def arg(self, i):
        """i-th dword on the stack at function entry (0 = first argument)."""
        return self.r32(self.reg(UC_X86_REG_ESP) + 4 + 4 * i)

    # -- hooks -----------------------------------------------------------------------------
    def hook(self, addr, fn):
        self.hooks[addr] = fn

    def _ret(self, eax=None, pop=0):
        if eax is not None:
            self.uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        esp = self.reg(UC_X86_REG_ESP)
        ret = self.r32(esp)
        self.uc.reg_write(UC_X86_REG_ESP, esp + 4 + pop)
        self.uc.reg_write(UC_X86_REG_EIP, ret)

    def _code(self, uc, addr, size, user):
        fn = self.hooks.get(addr)
        if fn is not None:
            r = fn(self)
            if r != "continue":
                if isinstance(r, tuple):
                    self._ret(*r)
                else:
                    self._ret(r)
            return
        if STUB_BASE <= addr < STUB_BASE + 0x100000:
            if addr == RET_SENTINEL:
                uc.emu_stop()
                return
            name = self.imports.get(addr, "?")
            self.unknown_imports.append(name)
            self._ret(self.import_results.get(name.split("!")[-1], 0), self.import_pops.get(name.split("!")[-1], 0))

    def _install(self):
        g = self.g
        self.unknown_imports = []
        self.import_results = {"GetTickCount": 0, "timeGetTime": 0, "ShowCursor": 0}
        self.import_pops = {"ShowCursor": 4, "Sleep": 4}
        self.uc.hook_add(UC_HOOK_CODE, self._code)
        for a in g["register_texture"]:
            self.hook(a, Emu._h_register_texture)
        self.hook(g["register_sound"], Emu._h_register_sound)
        for a in g["play_sound"]:
            self.hook(a, Emu._h_play_sound)
        self.hook(g["rand"], Emu._h_rand)
        self.hook(g["sprintf"], Emu._h_sprintf)
        self.hook(g["flush2d"], Emu._h_flush)
        for a in g["stubs"]:
            self.hook(a, lambda e: 0)
        self.heap = HEAP_BASE
        self.uc.mem_map(HEAP_BASE, HEAP_SIZE)
        for a in g["malloc"]:
            self.hook(a, lambda e: e.alloc(e.arg(0)))
        self.hook(g["calloc"], lambda e: e.alloc(e.arg(0) * e.arg(1)))
        for a in g["free"]:
            self.hook(a, lambda e: 0)
        self.hook(g["fopen"], lambda e: 0)
        if self.trace_strings:
            for a, kind in g["strings"].items():
                self.hook(a, (lambda k: (lambda e: e._h_text(k)))(kind))

    def empty_list(self, head_ptr):
        """Makes the std::list whose head pointer is stored at head_ptr empty (static initialiser)."""
        node = self.alloc(12)
        self.w32(node, node)
        self.w32(node + 4, node)
        self.w32(head_ptr, node)
        self.w32(head_ptr + 4, 0)

    def alloc(self, n):
        p = self.heap
        self.heap += (max(n, 1) + 15) & ~15
        if self.heap > HEAP_BASE + HEAP_SIZE:
            raise RuntimeError("emulated heap exhausted")
        self.uc.mem_write(p, b"\0" * max(n, 1))
        return p

    def _h_register_texture(self):
        name = self.cstr(self.reg(UC_X86_REG_ECX)) or ""
        key = name.lower().replace("/", "\\")
        if key in self.tex_by_name:
            return self.tex_by_name[key]
        h = len(self.tex_names) + 1
        self.tex_names[h] = name
        self.tex_by_name[key] = h
        w, hh = self.image_size(name)
        ent = self.g["tex_table"] + h * self.g["tex_stride"]
        self.w32(ent + self.g["tex_w"], w)
        self.w32(ent + self.g["tex_h"], hh)
        self.w32(self.g["tex_count"], h)
        return h

    def image_size(self, name):
        rel = name.replace("\\", "/")
        root = os.path.join(DATA_ROOT, "assets_extracted_games", "as2")
        cands = [rel, rel + ".tga"]
        for c in cands:
            p = os.path.join(root, c)
            if not os.path.exists(p):
                d, f = os.path.split(p)
                if os.path.isdir(d):
                    for x in os.listdir(d):
                        if x.lower() == f.lower():
                            p = os.path.join(d, x)
                            break
            if os.path.exists(p):
                with open(p, "rb") as fh:
                    hdr = fh.read(18)
                return struct.unpack_from("<HH", hdr, 12)
        return (256, 256)

    def _h_register_sound(self):
        name = self.cstr(self.reg(UC_X86_REG_ECX)) or self.cstr(self.reg(UC_X86_REG_EAX)) or "?"
        h = len(self.snd_names) + 1
        self.snd_names[h] = name
        return h

    def _h_play_sound(self):
        self.sounds.append((self.reg(UC_X86_REG_EAX), self.reg(UC_X86_REG_ECX)))
        return 0

    def _h_rand(self):
        self.rand_state = (self.rand_state * 214013 + 2531011) & 0xFFFFFFFF
        return (self.rand_state >> 16) & 0x7FFF

    def _h_sprintf(self):
        buf, fmt = self.arg(0), self.cstr(self.arg(1)) or ""
        out, i, ai = "", 0, 2
        while i < len(fmt):
            c = fmt[i]
            if c != "%":
                out += c
                i += 1
                continue
            j = i + 1
            while j < len(fmt) and fmt[j] in "-+ #0123456789.l":
                j += 1
            conv = fmt[j] if j < len(fmt) else ""
            spec = fmt[i:j + 1].replace("l", "")
            if conv == "%":
                out += "%"
            elif conv in "di":
                out += (spec[:-1] + "d") % struct.unpack("<i", struct.pack("<I", self.arg(ai)))[0]
                ai += 1
            elif conv in "uxX":
                out += spec % self.arg(ai)
                ai += 1
            elif conv == "s":
                out += spec % (self.cstr(self.arg(ai)) or "")
                ai += 1
            elif conv == "c":
                out += chr(self.arg(ai) & 0xFF)
                ai += 1
            elif conv in "fgeF":
                v = struct.unpack("<d", struct.pack("<II", self.arg(ai), self.arg(ai + 1)))[0]
                out += spec % v
                ai += 2
            i = j + 1
        self.uc.mem_write(buf, out.encode("latin-1") + b"\0")
        return len(out)

    def _h_flush(self):
        n = self.ri32(self.g["queue_count"])
        raw = bytes(self.uc.mem_read(self.g["queue"], n * self.g["stride"])) if n > 0 else b""
        self.frames.append([Quad(raw[i * self.g["stride"]:(i + 1) * self.g["stride"]], self.tex_names)
                            for i in range(n)])
        return 0

    def _h_text(self, kind):
        ptr = self.reg(UC_X86_REG_EAX)
        s = self.cstr(ptr)
        esp = self.reg(UC_X86_REG_ESP)
        st = [self.r32(esp + 4 + 4 * i) for i in range(4)]
        self.texts.append((kind, s, st, self.reg(UC_X86_REG_ECX), self.reg(UC_X86_REG_EDX)))
        self.text_ptrs.append(ptr)
        return "continue"

    # -- running ---------------------------------------------------------------------------
    def call(self, addr, regs=None, stack=(), max_insns=5_000_000):
        esp = STACK_TOP - 0x1000
        for v in reversed(list(stack)):
            esp -= 4
            self.w32(esp, v)
        esp -= 4
        self.w32(esp, RET_SENTINEL)
        self.uc.reg_write(UC_X86_REG_ESP, esp)
        self.uc.reg_write(UC_X86_REG_EBP, STACK_TOP - 0x800)
        names = {"eax": UC_X86_REG_EAX, "ebx": UC_X86_REG_EBX, "ecx": UC_X86_REG_ECX, "edx": UC_X86_REG_EDX,
                 "esi": UC_X86_REG_ESI, "edi": UC_X86_REG_EDI}
        for k, v in (regs or {}).items():
            self.uc.reg_write(names[k], v & 0xFFFFFFFF)
        try:
            self.uc.emu_start(addr, RET_SENTINEL, count=max_insns)
        except UcError as ex:
            eip = self.reg(UC_X86_REG_EIP)
            raise RuntimeError("emulation stopped at 0x%08x: %s" % (eip, ex))
        return self.reg(UC_X86_REG_EAX)

    def texts_since(self, n):
        return self.texts[n:]


def fmt_text(t):
    kind, s, st, ecx, edx = t
    x, y = struct.unpack("<ii", struct.pack("<II", st[0], st[1]))
    return "%-6s x %d y %d col 0x%08x flags 0x%x %r" % (kind, x, y, st[2], ecx, s)


if __name__ == "__main__":
    print(__doc__)
    sys.exit(0)
