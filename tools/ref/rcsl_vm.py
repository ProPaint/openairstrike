#!/usr/bin/env python3
"""Reference interpreter for RCSL scripts. Implements docs/spec/rcsl-vm.md.

Usage:
  rcsl_vm.py trace <file.scr> [--frames N] [--dt SECONDS] [--events SPEC] [--no-init]
                   [--out FILE]

SPEC is a comma-separated list of events, each `kind@frame[:arg...]`:
  touch@120            run the touch handler with `other` set to the mock "other" entity
  damage@240[:amount]  subtract amount (default 10) from self health, run the damage handler
  callback@360:msg[:p1[:p2]]   set cb_msg, cb_parm1, cb_parm2 and run the callback handler
  init@F               run the init handler again at frame F
Each frame: init (frame 0 only), the main update, then the frame's events in the order
touch, damage, callback (as the engine's touch and projectile passes follow the entity
pass).

The host is pluggable (class Host); the built-in MockHost is fully deterministic and is the
one the golden trace hashes and the C++ VM tests use. The trace format is specified in
docs/spec/rcsl-vm.md ("Trace format").
"""
import json
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "tools"))

import rcsl_disasm as D  # noqa: E402
import gamesel  # noqa: E402

# --- constants of the original VM (VERIFIED-CODE, see rcsl-vm.md) -----------------
STACK_SIZE = 512            # thread stack entries
STALL_BUDGET = 10000        # instructions per interpreter invocation
NUM_TEMPS = 16
ENTRY_NAMES = D.ENTRY_NAMES  # init main damage touch callback
EV_INIT, EV_MAIN, EV_DAMAGE, EV_TOUCH, EV_CALLBACK = range(5)

# Engine global table order (0x457220). Index = position in that table.
ENGINE_GLOBALS_V170 = [
    "self", "other", "cb_msg", "cb_parm1", "cb_parm2", "player", "p_action", "p_scores",
    "p_lives", "p_stars", "p_speedfactor", "p_counter1", "p_counter2", "p_counter3",
    "p_weapon", "l_night", "l_water", "l_waterlevel", "frametime", "time", "camera",
    "g_map_pos", "g_damage_factor", "g_health_factor",
]
# as2 and gulf: 28 names in the order of as2@0x49d908 (docs/spec/as2/rcsl-vm.delta.md, "Globals").
ENGINE_GLOBALS_AS2 = [
    "self", "other", "cb_msg", "cb_parm1", "cb_parm2", "player", "player1", "player2",
    "p_action", "p_maxHealth", "p_scores", "p_lives", "p_stars", "p_speedfactor",
    "p_counter1", "p_counter2", "p_counter3", "p_weapon", "l_night", "l_water",
    "l_waterlevel", "frametime", "time", "camera", "cameramode", "g_map_pos",
    "g_damage_factor", "g_health_factor",
]


def engine_globals():
    """The engine global table of the selected game (gamesel.game_key())."""
    return ENGINE_GLOBALS_V170 if gamesel.game_key() == "as3d" else ENGINE_GLOBALS_AS2

# --- mock address space (documented in rcsl-vm.md, "Mock host") ----------------------
FRAME_BASE = 0x10000000      # frame slot i of the (single) thread: FRAME_BASE + 4*i
GLOBAL_BASE = 0x20000000     # engine global g: GLOBAL_BASE + 0x10*g (16 bytes each)
ENTITY_BASE = 0x30000000     # mock entity n: reference value ENTITY_BASE + 0x1000*n
ENTITY_FIELDS = 90           # fields 0..89: an entity is 0x1E3 bytes, fields start at +0x7B
HEAP_BASE = 0x40000000       # ALLOC results

MOCK_SELF, MOCK_PLAYER, MOCK_CAMERA, MOCK_OTHER = 0, 1, 2, 3
HEALTH_FIELD = 34
DEAD_FIELD = 4


UNMAPPED = 0xDEAD0000        # address given to an out-of-range DEFS operand


class VMError(Exception):
    pass


# --- float helpers -------------------------------------------------------------------

def f2b(x):
    """Rounds a Python float to IEEE single and returns its bits."""
    try:
        return struct.unpack("<I", struct.pack("<f", x))[0]
    except OverflowError:
        return 0x7F800000 if x > 0 else 0xFF800000


def b2f(b):
    return struct.unpack("<f", struct.pack("<I", b & 0xFFFFFFFF))[0]


SNAN_QUIETED = [0]           # statistics: how often a signalling NaN was quieted


def quiet(b):
    """fld/fstp of a single: a signalling NaN becomes quiet. Everything else is copied."""
    if (b & 0x7F800000) == 0x7F800000 and (b & 0x007FFFFF) and not (b & 0x00400000):
        SNAN_QUIETED[0] += 1
        return b | 0x00400000
    return b


def ftol(b):
    """Float bits -> int32 with truncation; NaN and out of range give 0x80000000."""
    x = b2f(b)
    if math.isnan(x) or math.isinf(x) or not (-2147483649.0 < x < 2147483648.0):
        return -0x80000000
    return int(x)


def itof(i):
    return f2b(float(i))


def fdiv(a, b):
    if b == 0.0:
        if a == 0.0 or math.isnan(a):
            return float("nan")
        return math.copysign(math.inf, a) * math.copysign(1.0, b)
    return a / b


def fop(op, a, b):
    """Arithmetic on Python floats with IEEE semantics for inf/nan."""
    try:
        if op == "mul":
            return a * b
        if op == "add":
            return a + b
        if op == "sub":
            return a - b
        return fdiv(a, b)
    except (OverflowError, ValueError):
        return float("nan")


def is_nan(b):
    return (b & 0x7F800000) == 0x7F800000 and (b & 0x007FFFFF) != 0


INDEFINITE = 0xFFC00000   # x87 default NaN for invalid operations


def farith(op, ab, bb):
    """MUL/DIV/ADD/SUB on float bits the way the x87 code produces them (rcsl-vm.md)."""
    if is_nan(ab) or is_nan(bb):
        # x87: a NaN operand propagates (quieted); with two NaNs the larger significand wins
        if is_nan(ab) and is_nan(bb):
            return quiet(ab if (ab & 0x7FFFFF) >= (bb & 0x7FFFFF) else bb)
        return quiet(ab if is_nan(ab) else bb)
    x, y = b2f(ab), b2f(bb)
    r = fop({1: "mul", 2: "div", 3: "add", 4: "sub"}[op], x, y)
    if math.isnan(r):
        return INDEFINITE
    return f2b(r)


def hx(b):
    return f"{b & 0xFFFFFFFF:08x}"


# --- builtin table -------------------------------------------------------------------

def load_builtins(path=None):
    """The builtin table of the selected game (gulf reads the as2 table)."""
    with open(path or gamesel.builtins_path()) as f:
        data = json.load(f)
    return {b["name"]: b for b in data["builtins"]}


# --- memory ----------------------------------------------------------------------------

class Memory:
    """Flat 32-bit address space of 4-byte cells, split into mapped regions."""

    def __init__(self):
        self.regions = []    # (base, size_bytes, name)
        self.cells = {}

    def map(self, base, size, name):
        self.regions.append((base, size, name))

    def region(self, addr):
        for base, size, name in self.regions:
            if base <= addr < base + size:
                return name
        return None

    def check(self, addr):
        if addr & 3 or self.region(addr) is None:
            raise VMError(f"access to unmapped address {addr & 0xFFFFFFFF:#010x}")

    def read(self, addr):
        self.check(addr)
        return self.cells.get(addr, 0)

    def write(self, addr, bits):
        self.check(addr)
        self.cells[addr] = bits & 0xFFFFFFFF


# --- thread ----------------------------------------------------------------------------

class Thread:
    def __init__(self, script, mem):
        self.s = script
        self.frame_size = script.frame_slots
        mem.map(FRAME_BASE, 4 * self.frame_size, "frame")
        for i in range(self.frame_size):
            mem.write(FRAME_BASE + 4 * i, 0)
        for kind, slot, value in script.data:          # VERIFIED-CODE 0x41c862
            if kind == 2:
                mem.write(FRAME_BASE + 4 * slot, value)
            elif kind == 3:
                mem.write(FRAME_BASE + 4 * slot, FRAME_BASE + 4 * value)
        self.stack = []
        self.timeout = 0             # bits of a float (thread +0x820)
        self.waiting = -1            # pc of the last waiting LCALL (reference VM only)
        main = script.entries[EV_MAIN]
        self.pc = main if main != D.NO_ENTRY else -1


class Host:
    """Engine side of the VM. Subclasses provide globals, entities and builtins."""

    def global_address(self, name):
        raise NotImplementedError

    def call_builtin(self, vm, name, latent):
        """Runs a builtin. Returns the done flag (only meaningful when latent)."""
        raise NotImplementedError


class VM:
    def __init__(self, script, host, trace=None, builtins=None):
        self.s = script
        self.host = host
        self.mem = host.mem
        self.trace = trace if trace is not None else []
        self.builtins = builtins or load_builtins()
        self.th = Thread(script, self.mem)
        self.retreg = 0          # 0x1fa7dfc
        self.done_flag = 0       # 0x1fa7e00
        self.frame = 0
        self.entry = "-"
        self.depth = 0
        self.counts = {"instructions": 0, "builtin_calls": 0}
        self.defs_addr = []
        for name in script.defs:
            self.defs_addr.append(host.global_address(name))
        self.written = set()     # temporaries written since the last call (arity check)
        self.arity_problems = []
        self.nested_latent = 0   # LCALL left pending inside a CALLed subroutine
        self.max_invocation = 0  # most instructions executed by one invocation

    # -- operand decoding (VERIFIED-CODE 0x419c18) --
    def slot_addr(self, v):
        # The address is computed without a range check (as in the original); an
        # out-of-frame slot faults only when it is actually accessed.
        return (FRAME_BASE + 4 * v) & 0xFFFFFFFF

    def glob_addr(self, v):
        i = -v - 1
        if i >= len(self.defs_addr):
            return UNMAPPED
        return self.defs_addr[i]

    def decode(self, ins):
        """Operand decoder of 0x419c18. Indirections are performed for every operand, as
        in the original; for operands the handler does not use, a read from unmapped
        memory yields 0 instead of faulting (the original reads whatever is there)."""
        op, m = ins.op, ins.mode
        roles = D.OPCODES[op][1]
        # A
        if m & D.M_IMM1:
            a = ("imm", quiet(ins.a & 0xFFFFFFFF))
        elif ins.a < 0 and op not in (0x15, 0x1B, 0x1C, 0x1D):
            a = ("addr", self.glob_addr(ins.a))
        else:
            a = ("addr", self.slot_addr(ins.a))
        if m & D.M_IND1:
            a = ("addr", self.deref(a, roles[0] != "-"))
        # B
        if m & D.M_IMM2:
            b = ("imm", quiet(ins.b & 0xFFFFFFFF))
        elif ins.b < 0 and op not in (0x19, 0x1A):
            b = ("addr", self.glob_addr(ins.b))
        else:
            b = ("addr", self.slot_addr(ins.b))
        if m & D.M_IND2:
            b = ("addr", self.deref(b, roles[1] not in ("-", "fld", "rel")))
        # C (never immediate)
        if ins.c < 0:
            c = ("addr", self.glob_addr(ins.c))
        else:
            c = ("addr", self.slot_addr(ins.c))
        if m & D.M_IND3:
            c = ("addr", self.deref(c, roles[2] != "-"))
        return a, b, c

    def deref(self, loc, used):
        if used:
            return self.load(loc)
        kind, v = loc
        if kind == "imm":
            return v
        return self.mem.cells.get(v, 0) if self.mem.region(v) else 0

    def load(self, loc):
        kind, v = loc
        if kind == "imm":
            return v
        if kind == "none":
            raise VMError("read of an operand that has no location")
        return self.mem.read(v)

    def store(self, loc, bits):
        kind, v = loc
        if kind == "imm":
            return           # original writes into a scratch copy; no effect
        if kind == "none":
            raise VMError("write to an operand that has no location")
        self.mem.write(v, bits)
        if FRAME_BASE <= v < FRAME_BASE + 4 * NUM_TEMPS:
            self.written.add((v - FRAME_BASE) // 4)

    # -- trace --
    def t_ins(self, pc, op, val):
        self.trace.append(f"I {self.frame} {self.entry} {self.depth} {pc} {op:02x} "
                          f"{'-' if val is None else hx(val)}")

    # -- interpreter invocation (0x419be0). Returns 0 or 1. --
    def run(self):
        s, th = self.s, self.th
        cell = [STALL_BUDGET]
        try:
            return self._run(s, th, cell)
        finally:
            self.max_invocation = max(self.max_invocation, STALL_BUDGET - cell[0])

    def _run(self, s, th, cell):
        budget = cell[0]
        while True:
            pc = th.pc
            if not 0 <= pc < len(s.code):
                raise VMError(f"pc {pc} out of range")
            ins = s.code[pc]
            budget -= 1
            cell[0] = budget
            self.counts["instructions"] += 1
            op = ins.op
            if op not in D.OPCODES:
                raise VMError(f"unknown opcode {op:#04x} at {pc}")
            a, b, c = self.decode(ins)
            val = None
            nxt = pc + 1
            if op == 0x00:
                self.t_ins(pc, op, None)
                return 0
            elif op in (0x01, 0x02, 0x03, 0x04):
                val = farith(op, self.load(a), self.load(b))
                self.store(c, val)
            elif op in (0x05, 0x06, 0x07, 0x08, 0x09, 0x0A):
                x, y = ftol(self.load(a)), ftol(self.load(b))
                r = {5: x & y, 6: x | y, 7: int(x != 0 or y != 0), 8: int(x != 0 and y != 0),
                     9: int(x == y), 10: int(x != y)}[op]
                val = itof(r)
                self.store(c, val)
            elif op in (0x0B, 0x0C, 0x0D, 0x0E):
                x, y = b2f(self.load(a)), b2f(self.load(b))
                r = {11: x > y, 12: x < y, 13: x >= y, 14: x <= y}[op]
                val = itof(int(r))
                self.store(c, val)
            elif op == 0x0F:
                val = itof(int(ftol(self.load(a)) == 0))
                self.store(c, val)
            elif op == 0x10:
                val = quiet(self.load(a)) ^ 0x80000000
                self.store(c, val)
            elif op == 0x11:
                val = quiet(self.load(b))
                self.store(a, val)
            elif op == 0x12:
                val = (self.load(a) + 4 * ins.b) & 0xFFFFFFFF
                self.store(c, val)
            elif op == 0x13:
                if len(th.stack) >= STACK_SIZE:
                    raise VMError("thread stack overflow")
                val = quiet(self.load(a))
                th.stack.append(val)
            elif op == 0x14:
                if not th.stack:
                    raise VMError("thread stack underflow")
                val = th.stack.pop()
                self.store(b, val)
            elif op == 0x15:
                raise VMError("ALLOC is not supported by the reference VM")
            elif op in (0x16, 0x17):
                pass
            elif op == 0x18:
                val = quiet(self.load(a))
                self.retreg = val
                self.t_ins(pc, op, val)
                return 1
            elif op in (0x19, 0x1A):
                x = b2f(self.load(a))
                zero = (x == 0.0)
                if (op == 0x19 and not zero) or (op == 0x1A and zero):
                    nxt = pc + ins.b
            elif op == 0x1B:
                nxt = pc + ins.a
            elif op == 0x1C:
                if ins.a < 0:
                    self.builtin(ins.a, latent=False)
                else:
                    self.subroutine(ins.a)
                self.written = set()
                val = quiet(self.retreg)
                self.store(b, val)
                if ins.a >= 0:
                    th.pc = pc          # restored by the handler
            elif op == 0x1D:
                self.done_flag = 0
                if ins.a < 0:
                    self.done_flag = 1 if self.builtin(ins.a, latent=True) else 0
                else:
                    self.done_flag = self.subroutine(ins.a)
                    th.pc = pc
                self.written = set()
                complete = False
                if self.done_flag:
                    complete = True
                elif th.timeout == 0:
                    complete = False
                else:
                    t = b2f(th.timeout) - b2f(self.mem.read(self.host.global_address("frametime")))
                    th.timeout = f2b(t)
                    complete = not (b2f(th.timeout) > 0.0)
                if complete:
                    th.waiting = -1
                    th.pc = pc + 1
                    th.timeout = 0
                    val = quiet(self.retreg)
                    self.store(b, val)
                    self.t_ins(pc, op, val)
                else:
                    th.waiting = pc
                    th.pc = pc
                    self.t_ins(pc, op, None)
                return 0
            elif op == 0x1E:
                val = quiet(self.load(a))
                th.timeout = val
            if op != 0x1D:
                self.t_ins(pc, op, val)
            if op in (0x19, 0x1A, 0x1B) and not 0 <= nxt < len(s.code):
                raise VMError(f"jump target {nxt} out of range at {pc}")
            th.pc = nxt
            if budget <= 0:
                raise VMError("Script stall detected.")

    def subroutine(self, target):
        """Nested interpreter invocation for CALL/LCALL of a script subroutine."""
        if not 0 <= target < len(self.s.code):
            raise VMError(f"call target {target} out of range")
        self.th.pc = target
        self.depth += 1
        try:
            status = self.run()
        finally:
            self.depth -= 1
        if status == 0 and self.s.code[self.th.pc].op == 0x1D:
            self.nested_latent += 1
        return status

    def builtin(self, a, latent):
        i = -a - 1
        if i >= len(self.s.funcs):
            raise VMError(f"FUNC index {i} out of range")
        name = self.s.funcs[i]
        info = self.builtins.get(name)
        if info is None:
            raise VMError(f"builtin {name} not in the table")
        n = info["arity"]
        missing = [k for k in range(n) if k not in self.written]
        # a waiting LCALL is re-executed on later updates without its arguments being
        # written again: check only the first execution
        rerun = latent and self.th.waiting == self.th.pc
        if missing and not rerun:
            self.arity_problems.append((self.th.pc, name, missing))
        args = [self.mem.read(FRAME_BASE + 4 * k) for k in range(n)]
        self.counts["builtin_calls"] += 1
        done = self.host.call_builtin(self, name, info, args, latent)
        self.trace.append(f"B {self.frame} {self.entry} {self.th.pc} {name}"
                          + "".join(" " + hx(x) for x in args)
                          + f" -> {hx(self.retreg)}")
        return done

    # -- dispatch (0x41a200) --
    def dispatch(self, kind):
        th, s = self.th, self.s
        e = s.entries[kind]
        if e == D.NO_ENTRY:
            return None
        self.entry = ENTRY_NAMES[kind]
        depth0 = len(th.stack)
        self.written = set()
        self.trace.append(f"E {self.frame} {self.entry}")
        if kind == EV_MAIN:
            if th.pc < 0:
                return None
            status = self.run()
            if s.code[th.pc].op in (0x00, 0x18):
                th.pc = e
        else:
            saved_pc = th.pc
            saved = [self.mem.read(FRAME_BASE + 4 * i) for i in range(NUM_TEMPS)]
            th.pc = e
            status = self.run()
            th.pc = saved_pc
            for i, v in enumerate(saved):
                self.mem.write(FRAME_BASE + 4 * i, v)
        self.trace.append(f"R {self.frame} {self.entry} {status} {len(th.stack) - depth0}")
        return len(th.stack) - depth0


# --- mock host ---------------------------------------------------------------------------

class Xorshift32:
    def __init__(self, seed=1):
        self.state = seed or 1

    def next(self):
        x = self.state
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self.state = x
        return x

    def uniform_bits(self):
        return f2b((self.next() >> 8) / 16777216.0)


class MockHost(Host):
    """Deterministic host. See rcsl-vm.md, "Mock host"."""

    def __init__(self, dt_bits):
        self.mem = Memory()
        self.rng = Xorshift32(1)
        self.entities = 0
        self.calls = []
        self.globals = engine_globals()
        self.mem.map(GLOBAL_BASE, 0x10 * len(self.globals), "globals")
        for n in (MOCK_SELF, MOCK_PLAYER, MOCK_CAMERA, MOCK_OTHER):
            self.new_entity()
        self.set_global("self", self.entity_ref(MOCK_SELF))
        self.set_global("player", self.entity_ref(MOCK_PLAYER))
        self.set_global("camera", self.entity_ref(MOCK_CAMERA))
        self.set_global("frametime", dt_bits)

    def entity_ref(self, n):
        return ENTITY_BASE + 0x1000 * n

    def new_entity(self):
        n = self.entities
        self.entities += 1
        ref = self.entity_ref(n)
        self.mem.map(ref, 4 * ENTITY_FIELDS, f"entity{n}")
        self.mem.write(ref, ref - 0x7B)      # field 0: back-pointer to the entity
        return ref

    def global_address(self, name):
        if name not in self.globals:
            raise VMError(f"unknown global {name}")
        return GLOBAL_BASE + 0x10 * self.globals.index(name)

    def set_global(self, name, bits):
        self.mem.write(self.global_address(name), bits)

    def get_global(self, name):
        return self.mem.read(self.global_address(name))

    def damage(self, vm, amount):
        """Damage routine 0x404ae0 on self: skipped when field 4 (dead) is non-zero;
        health -= amount; damage handler; if health <= 0 then field 4 = 1.0.
        Returns the handler's stack delta (None if not run)."""
        ref = self.entity_ref(MOCK_SELF)
        if b2f(self.mem.read(ref + 4 * DEAD_FIELD)) != 0.0:
            vm.trace.append(f"D {vm.frame} skipped")
            return None
        h = ref + 4 * HEALTH_FIELD
        self.mem.write(h, f2b(b2f(self.mem.read(h)) - amount))
        vm.trace.append(f"D {vm.frame} {hx(f2b(amount))}")
        d = vm.dispatch(EV_DAMAGE)
        if b2f(self.mem.read(h)) <= 0.0:
            self.mem.write(ref + 4 * DEAD_FIELD, f2b(1.0))
        return d

    def call_builtin(self, vm, name, info, args, latent):
        # consume arguments: validate what the documented types say is read
        for k, spec in enumerate(info["args"]):
            t = spec["type"]
            if t in ("vec", "vec_out"):
                for j in range(3):
                    self.mem.read(args[k] + 4 * j)
            elif t == "string":
                if args[k] not in vm.s.string_starts():
                    raise VMError(f"{name}: argument {k} is not a STRG string offset "
                                  f"({args[k]:#x})")
        ret = info["returns"]
        if name == "random":
            vm.retreg = self.rng.uniform_bits()
        elif name == "crandom":
            u = b2f(self.rng.uniform_bits())
            vm.retreg = f2b(f2b_f(u * 2.0) - 1.0)
        elif ret == "entity":
            vm.retreg = self.new_entity()
        elif ret in ("float", "int_as_float", "pointer"):
            vm.retreg = 0
        # "none": return register untouched
        if not latent:
            return 1
        return 1 if vm.th.timeout == 0 else 0


def f2b_f(x):
    return b2f(f2b(x))


# --- runner -------------------------------------------------------------------------------

def parse_events(spec):
    ev = []
    if not spec:
        return ev
    for item in spec.split(","):
        item = item.strip()
        if not item:
            continue
        kind, rest = item.split("@", 1)
        parts = rest.split(":")
        frame = int(parts[0])
        ev.append((frame, kind, [float(x) for x in parts[1:]]))
    return ev


STANDARD_EVENTS = "touch@120,damage@240,callback@360:1"


def run_script(script, frames=600, dt=1.0 / 60.0, events=STANDARD_EVENTS, init=True,
               builtins=None):
    """Runs one script under the mock host.

    Returns (vm, stack_deltas, error): stack_deltas lists (handler, stack depth change) per
    dispatch; error is the VMError that stopped the run, or None. The trace is vm.trace;
    on error it ends with an `X` line."""
    dt_bits = f2b(dt)
    host = MockHost(dt_bits)
    vm = VM(script, host, builtins=builtins)
    deltas = []
    evs = parse_events(events)
    time_acc = 0.0
    try:
        for frame in range(frames):
            vm.frame = frame
            host.set_global("time", f2b(time_acc))
            if frame == 0 and init:
                # map spawner: the thread exists, init runs before the first entity pass
                deltas.append(("init", vm.dispatch(EV_INIT)))
            deltas.append(("main", vm.dispatch(EV_MAIN)))
            # after the entity pass: touch pass, then projectiles/area damage; callbacks
            # come from other scripts' main and are placed last
            order = {"init": 0, "touch": 1, "damage": 2, "callback": 3}
            for f, kind, args in sorted((e for e in evs if e[0] == frame),
                                        key=lambda e: order.get(e[1], 9)):
                if kind == "touch":
                    prev = host.get_global("other")
                    host.set_global("other", host.entity_ref(MOCK_OTHER))
                    deltas.append((kind, vm.dispatch(EV_TOUCH)))
                    host.set_global("other", prev)
                elif kind == "damage":
                    amount = args[0] if args else 10.0
                    deltas.append((kind, host.damage(vm, amount)))
                elif kind == "callback":
                    vals = list(args) + [0.0, 0.0, 0.0]
                    for name, v in zip(("cb_msg", "cb_parm1", "cb_parm2"), vals):
                        host.set_global(name, f2b(v))
                    deltas.append((kind, vm.dispatch(EV_CALLBACK)))
                elif kind == "init":
                    deltas.append((kind, vm.dispatch(EV_INIT)))
                else:
                    raise ValueError(f"unknown event kind {kind}")
            time_acc = b2f(f2b(time_acc + b2f(dt_bits)))
    except VMError as e:
        vm.trace.append(f"X {vm.frame} {vm.entry} {vm.th.pc} {e}")
        return vm, deltas, e
    return vm, deltas, None


def main(argv):
    import argparse
    gamesel.parse_game_arg(argv)
    ap = argparse.ArgumentParser(prog="rcsl_vm.py")
    sub = ap.add_subparsers(dest="cmd")
    t = sub.add_parser("trace")
    t.add_argument("file")
    t.add_argument("--frames", type=int, default=600)
    t.add_argument("--dt", type=float, default=1.0 / 60.0)
    t.add_argument("--events", default="")
    t.add_argument("--no-init", action="store_true")
    t.add_argument("--out")
    args = ap.parse_args(argv[1:])
    if args.cmd != "trace":
        ap.print_help()
        return 2
    s = D.load(args.file)
    vm, _, err = run_script(s, args.frames, args.dt, args.events, not args.no_init)
    out = open(args.out, "w") if args.out else sys.stdout
    for line in vm.trace:
        out.write(line + "\n")
    if err is not None:
        print(f"error: {err}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
