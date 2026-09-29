#!/usr/bin/env python3
"""Checks every builtin call site of a game's scripts against its builtin table.

For every CALL (0x1C) and LCALL (0x1D) of a builtin (operand A < 0) in every script, derives
how many argument slots the script wrote before the call, and compares that number with the
arity in the game's machine-readable builtin table.

How the argument count is derived (see docs/spec/rcsl-vm.md and rcsl-opcodes-v0.md): a
builtin reads argument k from frame slot t<k> (k = 0, 1, ...). The compiler writes the
arguments into t0, t1, ... right before the call. The checker runs a "written since the last
call" must-analysis over the control-flow graph of each script:

- an instruction writes the slot its handler writes, unless that operand is indirect (then it
  writes memory through a pointer, not a slot): C for arithmetic, logic, comparisons, NOT,
  NEG, LEA and ALLOC; A for MOV; B for POP (the POP quirk), CALL and LCALL;
- a builtin CALL or LCALL clears the set, then adds its result slot B; a CALL of a script
  subroutine clears the set (the subroutine shares the frame and may write anything);
- at a join the sets of all predecessors are intersected; the entry points and the
  subroutine targets start with an empty set;
- the argument count of a call site is the largest n such that t0 .. t(n-1) are all in the
  set before the call.

A call that passes FEWER arguments than the table's arity is an error (exit status 1).
More is allowed: the first game's scripts pass unused arguments to some builtins (e.g.
`Lightning`, see rcsl-builtins-table.md, "Call-site cross-check").

Usage:
  check_builtin_calls.py [--game as3d|as2|gulf] [--scripts DIR] [--table JSON] [--quiet]
(--game defaults to $AS3D_GAME, then as3d.)

Data: $AS3D_DATA_ROOT (default: the repository root); game `as3d` reads
<root>/assets_extracted/scripts, any other game <root>/assets_extracted_games/<key>/scripts.
Table: testdata/golden/<key>/rcsl_builtins.json; `gulf` falls back to the `as2` table when it
has none of its own.

Every regular file under the scripts directory is read, whatever its extension (the second
game ships one script as `.sc`). A file without a CODE section has no call sites.
Standard library only.
"""
import argparse
import collections
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))

MAGIC = 0x4C534352
HEADER_SIZE = 0x38
INSTR_SIZE = 14
NO_ENTRY = 0xFFFFFFFF
NUM_TEMPS = 16

OP_END, OP_POP, OP_RET, OP_JNZ, OP_JZ, OP_JMP, OP_CALL, OP_LCALL = (
    0x00, 0x14, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D)
# Opcodes whose handler writes operand C (see rcsl-opcodes-v0.md, opcode table).
WRITES_C = set(range(0x01, 0x11)) | {0x12, 0x15}
FALLBACK = {"gulf": "as2"}


class ScriptError(ValueError):
    pass


def data_root():
    return os.environ.get("AS3D_DATA_ROOT") or REPO


def scripts_dir(game):
    if game == "as3d":
        return os.path.join(data_root(), "assets_extracted", "scripts")
    return os.path.join(data_root(), "assets_extracted_games", game, "scripts")


def table_path(game):
    p = os.path.join(REPO, "testdata", "golden", game, "rcsl_builtins.json")
    if not os.path.exists(p) and game in FALLBACK:
        return table_path(FALLBACK[game])
    return p


def read_names(payload, count, with_kind):
    out, o = [], 0
    for _ in range(count):
        if with_kind:
            o += 1
        n = payload[o]
        out.append(payload[o + 1:o + n].decode("cp1251"))
        o += 1 + n
    return out


def parse(data):
    """Returns (header, funcs, code); code is a list of (op, mode, a, b, c)."""
    if len(data) < HEADER_SIZE:
        raise ScriptError("shorter than the header")
    h = struct.unpack_from("<14I", data, 0)
    if h[0] != MAGIC:
        raise ScriptError("bad magic")
    off, sections = HEADER_SIZE, {}
    while off + 8 <= len(data):
        tag = data[off:off + 4].decode("latin1")
        ln = struct.unpack_from("<I", data, off + 4)[0]
        sections[tag] = data[off + 8:off + 8 + ln]
        off += 8 + ln
    if off != len(data):
        raise ScriptError("sections do not tile the file")
    funcs = read_names(sections["FUNC"], h[4], False) if "FUNC" in sections else []
    p = sections.get("CODE", b"")
    code = [struct.unpack_from("<BBiii", p, i) for i in range(0, INSTR_SIZE * h[8], INSTR_SIZE)] \
        if "CODE" in sections else []
    return h, funcs, code


def written_slot(ins):
    """Frame temporary (0..15) written by this instruction, or None."""
    op, mode, a, b, c = ins
    if op in WRITES_C:
        slot, indirect = c, mode & 0x80
    elif op == 0x11:
        slot, indirect = a, mode & 0x02
    elif op in (OP_POP, OP_CALL, OP_LCALL):
        slot, indirect = b, mode & 0x20
    else:
        return None
    if indirect or not 0 <= slot < NUM_TEMPS:
        return None
    return slot


def successors(i, ins, n):
    op, mode, a, b, c = ins
    if op in (OP_END, OP_RET):
        return []
    if op == OP_JMP:
        return [i + a]
    if op in (OP_JZ, OP_JNZ):
        return [i + 1, i + b]
    return [i + 1]


def call_sites(h, funcs, code):
    """Yields (index, opcode, builtin name, argument count) for each builtin call."""
    n = len(code)
    if not n:
        return
    starts = {e for e in h[9:14] if e != NO_ENTRY and e < n}
    starts |= {ins[2] for ins in code if ins[0] in (OP_CALL, OP_LCALL) and 0 <= ins[2] < n}
    preds = collections.defaultdict(list)
    for i, ins in enumerate(code):
        for s in successors(i, ins, n):
            if 0 <= s < n:
                preds[s].append(i)
    full = frozenset(range(NUM_TEMPS))
    before = [full] * n          # must-analysis: start from "everything", shrink to fixpoint
    after = [full] * n
    changed = True
    while changed:
        changed = False
        for i, ins in enumerate(code):
            if i in starts:
                s_in = frozenset()
            else:
                ps = preds.get(i)
                s_in = frozenset.intersection(*[after[p] for p in ps]) if ps else frozenset()
            op, mode, a, b, c = ins
            if op in (OP_CALL, OP_LCALL):
                s_out = frozenset()
            else:
                s_out = s_in
            w = written_slot(ins)
            if w is not None:
                s_out = s_out | {w}
            if s_in != before[i] or s_out != after[i]:
                before[i], after[i] = s_in, s_out
                changed = True
    for i, ins in enumerate(code):
        op, mode, a, b, c = ins
        if op in (OP_CALL, OP_LCALL) and a < 0:
            k = 0
            while k in before[i]:
                k += 1
            idx = -a - 1
            name = funcs[idx] if idx < len(funcs) else "<FUNC %d out of range>" % idx
            yield i, op, name, k


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--game", default=os.environ.get("AS3D_GAME") or "as3d")
    ap.add_argument("--scripts", help="scripts directory (default: from --game)")
    ap.add_argument("--table", help="builtin table JSON (default: from --game)")
    ap.add_argument("--quiet", action="store_true", help="print only the summary and errors")
    args = ap.parse_args(argv[1:])
    root = args.scripts or scripts_dir(args.game)
    tpath = args.table or table_path(args.game)
    if not os.path.isdir(root):
        print("scripts directory not found: %s" % root)
        return 2
    table = json.load(open(tpath))["builtins"]
    arity = {b["name"]: b["arity"] for b in table}
    stats = collections.OrderedDict((b["name"], {"scripts": set(), "call": 0, "lcall": 0,
                                                 "seen": collections.Counter()}) for b in table)
    errors, unknown, bad_files, files, sites = [], [], [], 0, 0
    paths = []
    for dp, dn, fn in os.walk(root):
        dn.sort()
        for f in fn:
            paths.append(os.path.join(dp, f))
    paths.sort(key=lambda p: os.path.relpath(p, root).lower())
    for path in paths:
        rel = os.path.relpath(path, root)
        try:
            h, funcs, code = parse(open(path, "rb").read())
        except (ScriptError, KeyError, IndexError, struct.error) as e:
            bad_files.append((rel, str(e)))
            continue
        files += 1
        for i, op, name, k in call_sites(h, funcs, code):
            sites += 1
            if name not in arity:
                unknown.append((rel, i, name))
                continue
            st = stats[name]
            st["scripts"].add(rel)
            st["call" if op == OP_CALL else "lcall"] += 1
            st["seen"][k] += 1
            if k < arity[name]:
                errors.append((rel, i, name, k, arity[name]))
    print("game %s: %d scripts read from %s, table %s" % (args.game, files, root,
                                                          os.path.relpath(tpath, REPO)))
    if not args.quiet:
        print("%-4s %-20s %7s %6s %6s %6s  %s" % ("#", "builtin", "scripts", "calls", "lcalls",
                                                   "arity", "argument slots written: sites"))
        for i, (name, st) in enumerate(stats.items()):
            seen = ", ".join("%d: %d" % kv for kv in sorted(st["seen"].items())) or "-"
            flag = "" if all(k >= arity[name] for k in st["seen"]) else "  <-- FEWER"
            print("%-4d %-20s %7d %6d %6d %6d  %s%s" % (i, name, len(st["scripts"]), st["call"],
                                                         st["lcall"], arity[name], seen, flag))
    used = sum(1 for st in stats.values() if st["scripts"])
    more = sum(1 for n, st in stats.items() if any(k > arity[n] for k in st["seen"]))
    print("%d builtin call sites, %d distinct builtins used, %d of them with sites passing "
          "more slots than the arity" % (sites, used, more))
    for rel, err in bad_files:
        print("UNREADABLE %s: %s" % (rel, err))
    for rel, i, name in unknown:
        print("UNKNOWN BUILTIN %s @%d: %s" % (rel, i, name))
    for rel, i, name, k, ar in errors:
        print("FEWER ARGUMENTS %s @%d: %s gets %d, arity %d" % (rel, i, name, k, ar))
    if errors or unknown or bad_files:
        print("FAILED: %d call sites with fewer arguments than the arity, %d unknown builtins, "
              "%d unreadable files" % (len(errors), len(unknown), len(bad_files)))
        return 1
    print("OK: no call site passes fewer arguments than the table's arity")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
