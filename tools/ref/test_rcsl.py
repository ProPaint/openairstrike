#!/usr/bin/env python3
"""Acceptance test for the RCSL script format (docs/spec/rcsl-container.md,
docs/spec/rcsl-opcodes-v0.md) over every shipped .scr file.

Exits non-zero on failure, prints SKIPPED and exits 0 when the game data is absent.
`--game <key>` (else $AS3D_GAME, else as3d) picks the game: its scripts, its (opcode, mode)
pairs (the base table for as3d; the base table plus the as2 delta's seven new pairs for the
sequels) and its counts (testdata/golden/<key>/expected.json, "scripts").
Set AS3D_REGEN_GOLDEN=1 (or pass --regen) to rewrite testdata/golden/<key>/rcsl_summary.json.
"""
import collections
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "tools"))

sys.path.insert(0, HERE)
import gamesel  # noqa: E402

gamesel.parse_game_arg()
import rcsl_disasm as R  # noqa: E402

GOLDEN = gamesel.golden_path("rcsl_summary.json")
OPCODE_DOC = os.path.join(REPO, "docs", "spec", "rcsl-opcodes-v0.md")

failures = []


def fail(msg):
    failures.append(msg)
    if len(failures) <= 50:
        print("FAIL:", msg)


def documented_tables():
    """Reads the opcode table and the (opcode, mode) table from the spec."""
    text = open(OPCODE_DOC, encoding="utf-8").read()
    ops = {}
    combos = {}
    section = None
    for line in text.splitlines():
        if line.startswith("## "):
            section = line[3:].strip().lower()
            continue
        m = re.match(r"^\|\s*0x([0-9A-Fa-f]{2})\s*\|\s*(`?)([A-Z0-9]+)\2\s*\|", line)
        if section and section.startswith("opcode table") and m:
            ops[int(m.group(1), 16)] = m.group(3)
        m = re.match(r"^\|\s*0x([0-9A-Fa-f]{2})\s*\|\s*0x([0-9A-Fa-f]{2})\s*\|\s*(\d+)\s*\|", line)
        if section and section.startswith("mode combinations") and m:
            combos[(int(m.group(1), 16), int(m.group(2), 16))] = int(m.group(3))
    return ops, combos


def main():
    regen = "--regen" in sys.argv or os.environ.get("AS3D_REGEN_GOLDEN") == "1"
    if gamesel.skip_no_data("test_rcsl"):
        return 0
    root = R.scripts_dir()
    want = gamesel.expected()["scripts"]
    first_game = gamesel.game_key() == "as3d"

    files = R.corpus(root)
    if len(files) != want["files"]:
        fail(f"expected {want['files']} scripts, found {len(files)}")

    doc_ops, doc_combos = documented_tables()
    if not doc_ops or not doc_combos:
        fail("could not read opcode tables from rcsl-opcodes-v0.md")
    for op, (mnem, *_rest) in R.OPCODES.items():
        if doc_ops.get(op) != mnem:
            fail(f"opcode {op:#04x}: module says {mnem}, spec says {doc_ops.get(op)}")
    if doc_combos != R.SEEN_MODES:
        fail("mode-combination table in spec differs from rcsl_disasm.SEEN_MODES")
    allowed = R.allowed_modes()
    no_code = []

    combos = collections.Counter()
    summary = {}
    total_instr = 0
    for path in files:
        name = R.pak_name(path, root)
        with open(path, "rb") as f:
            raw = f.read()
        try:
            s = R.parse(raw)   # strict: magic, sections, counts, lengths, encodings, entries
        except R.RcslError as e:
            fail(f"{name}: {e}")
            continue
        h = s.header
        if h[1] != 16:
            fail(f"{name}: header field 1 = {h[1]}, expected 16")
        if h[6] < R.NUM_TEMPS:
            fail(f"{name}: frame size {h[6]} < {R.NUM_TEMPS}")
        # every byte accounted for: the sections tile the file exactly
        off = R.HEADER_SIZE
        for tag in s.order:
            ln = struct.unpack_from("<I", raw, off + 4)[0]
            if raw[off:off + 4].decode("latin1") != tag:
                fail(f"{name}: section order mismatch")
            if ln != R._section_len(s, tag):
                fail(f"{name}: {tag} length {ln} != decoded {R._section_len(s, tag)}")
            off += 8 + ln
        if off != len(raw):
            fail(f"{name}: sections end at {off}, file size {len(raw)}")
        unknown = [t for t in s.order if t not in R.KNOWN_TAGS]
        if unknown:
            fail(f"{name}: unknown sections {unknown}")
        if len(raw) - R.HEADER_SIZE - 8 * len(s.order) - sum(
                R._section_len(s, t) for t in s.order) != 0:
            fail(f"{name}: byte accounting mismatch")
        if h[8] * R.INSTR_SIZE != len(s.code) * R.INSTR_SIZE or len(s.code) != h[8]:
            fail(f"{name}: instruction count mismatch")
        for e in s.entries:
            if e != R.NO_ENTRY and e >= len(s.code):
                fail(f"{name}: entry {e} out of range")
        if R.serialise(s) != raw:
            fail(f"{name}: re-serialisation is not byte-identical")
        for k, _ in s.cash:
            if k not in R.CASH_KINDS:
                fail(f"{name}: unknown CASH kind {k}")
        for k, _, _ in s.data:
            if k not in R.DATA_KINDS:
                fail(f"{name}: unknown DATA kind {k}")
        if "CODE" not in s.order:
            no_code.append(name)
        for ins in s.code:
            combos[(ins.op, ins.mode)] += 1
            if ins.op not in doc_ops:
                fail(f"{name}: opcode {ins.op:#04x} not documented")
            elif (ins.op, ins.mode) not in allowed:
                fail(f"{name}: opcode {ins.op:#04x} mode {ins.mode:#04x} not documented")
        for p in R.check_bounds(s):
            fail(f"{name}: {p}")
        # listing must render without error
        import io
        R.dump(s, name, out=io.StringIO())
        total_instr += len(s.code)
        summary[name] = R.summary(s, raw)

    if first_game:
        if dict(combos) != doc_combos:
            fail("corpus (opcode, mode) counts differ from the documented table")
    elif len(combos) != want["mode_pairs"]:
        fail(f"corpus has {len(combos)} (opcode, mode) pairs, expected {want['mode_pairs']}")
    if sorted(no_code) != sorted(want["no_code"]):
        fail(f"scripts without a CODE section: {sorted(no_code)}, expected {sorted(want['no_code'])}")

    if regen:
        with open(GOLDEN, "w") as f:
            json.dump(summary, f, indent=1, sort_keys=True)
            f.write("\n")
        print(f"test_rcsl: wrote {GOLDEN}")
    else:
        try:
            with open(GOLDEN) as f:
                golden = json.load(f)
        except FileNotFoundError:
            golden = None
            fail(f"missing {GOLDEN} (run with --regen)")
        if golden is not None and golden != json.loads(json.dumps(summary)):
            diff = [k for k in set(golden) | set(summary) if golden.get(k) != json.loads(
                json.dumps(summary.get(k)))]
            fail(f"golden summary mismatch for {len(diff)} scripts, e.g. {sorted(diff)[:5]}")

    if failures:
        print(f"test_rcsl: {len(failures)} failures")
        return 1
    print(f"test_rcsl: OK ({len(files)} scripts, {total_instr} instructions, "
          f"{len(combos)} opcode/mode pairs)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
