#!/usr/bin/env python3
"""Runs every shipped script under the reference VM (tools/ref/rcsl_vm.py) with the mock
host and checks the runtime invariants of docs/spec/rcsl-vm.md.

Standard run: 600 frames at dt = 1/60, init at creation, touch at frame 120, damage at
frame 240, callback with cb_msg = 1 at frame 360 (each only if the entry point exists).

Asserts per script: no VM error (unknown opcode, access outside the frame / an entity /
the globals, jump or call out of range, stack overflow or underflow, stall detector),
the PUSH/POP stack balanced at the end of every handler, and every builtin called with
its documented number of arguments written. Writes or validates
testdata/golden/rcsl_trace_hashes.json (sha1 of the trace, instruction and builtin call
counts). Set AS3D_REGEN_GOLDEN=1 or pass --regen to rewrite it.
Exits 0 with a loud SKIPPED when the game data is absent.
"""
import hashlib
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(REPO, "tools"))

import rcsl_disasm as D  # noqa: E402
import rcsl_vm as V  # noqa: E402

GOLDEN = os.path.join(REPO, "testdata", "golden", "rcsl_trace_hashes.json")
FRAMES = 600
DT = 1.0 / 60.0

# Scripts allowed to violate a specific assertion, with the reason. Keys are pak names,
# values map the assertion ("arity") to an explanation. Keep this list short and justified.
ALLOW = {
}


def run_one(s):
    vm, deltas, err = V.run_script(s, FRAMES, DT, V.STANDARD_EVENTS, True)
    problems = []
    if err is not None:
        problems.append(("error", str(err)))
    for kind, delta in deltas:
        if delta not in (None, 0):
            problems.append(("stack", f"{kind} handler left the stack at {delta:+d}"))
            break
    for pc, name, missing in sorted(set((p, n, tuple(m)) for p, n, m in vm.arity_problems)):
        problems.append(("arity", f"{name} at {pc}: argument slots {missing} not written"))
    text = "\n".join(vm.trace) + "\n"
    return vm, problems, text


def main():
    regen = "--regen" in sys.argv or os.environ.get("AS3D_REGEN_GOLDEN") == "1"
    root = D.scripts_dir()
    if not os.path.isdir(root):
        print("=" * 70)
        print(f"test_rcsl_vm: SKIPPED: no script data at {root}")
        print("test_rcsl_vm: set AS3D_DATA_ROOT to a checkout with assets_extracted/")
        print("=" * 70)
        return 0
    failures = []
    hashes = {}
    totals = [0, 0]
    max_inv = 0
    allowed_used = set()
    for path in D.corpus(root):
        name = D.pak_name(path, root)
        s = D.load(path)
        vm, problems, text = run_one(s)
        for kind, msg in problems:
            if kind in ALLOW.get(name, {}):
                allowed_used.add((name, kind))
                continue
            failures.append(f"{name}: {kind}: {msg}")
        hashes[name] = {"sha1": hashlib.sha1(text.encode()).hexdigest(),
                        "instructions": vm.counts["instructions"],
                        "builtin_calls": vm.counts["builtin_calls"]}
        max_inv = max(max_inv, vm.max_invocation)
        totals[0] += vm.counts["instructions"]
        totals[1] += vm.counts["builtin_calls"]
    for name, kinds in ALLOW.items():
        for kind in kinds:
            if (name, kind) not in allowed_used:
                failures.append(f"{name}: allowlisted '{kind}' no longer occurs; remove it")
    if regen:
        with open(GOLDEN, "w") as f:
            json.dump(hashes, f, indent=1, sort_keys=True)
            f.write("\n")
        print(f"test_rcsl_vm: wrote {GOLDEN}")
    else:
        try:
            with open(GOLDEN) as f:
                golden = json.load(f)
        except FileNotFoundError:
            golden = None
            failures.append(f"missing {GOLDEN} (run with --regen)")
        if golden is not None and golden != hashes:
            diff = sorted(k for k in set(golden) | set(hashes) if golden.get(k) != hashes.get(k))
            failures.append(f"trace hashes differ for {len(diff)} scripts, e.g. {diff[:5]}")
    for f in failures[:100]:
        print("FAIL:", f)
    if failures:
        print(f"test_rcsl_vm: {len(failures)} failures")
        return 1
    print(f"test_rcsl_vm: OK ({len(hashes)} scripts, {totals[0]} instructions, "
          f"{totals[1]} builtin calls, {len(allowed_used)} allowlisted exceptions; "
          f"longest invocation {max_inv} of {V.STALL_BUDGET} instructions; "
          f"signalling NaNs quieted: {V.SNAN_QUIETED[0]})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
