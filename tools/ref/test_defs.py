#!/usr/bin/env python3
"""Golden test for the typed def loader (tools/ref/defs.py).

Loads objects/*.obj, weapons/*.wpn, particles/*.ps and maps/levels.txt,
validates every reference, and writes/validates
testdata/golden/<game>/defs_summary.json: per definition, its name and a sha1 over
its canonical serialization (see docs/spec/obj.md &c., "Canonical
serialization"), plus totals and the full list of unresolved references.

Run directly (`--game <key>`, else $AS3D_GAME, else as3d), or via tools/ci.sh which runs every
tools/ref/test_*.py once per game. The counts and the known unresolved references are in
testdata/golden/<game>/expected.json ("definitions").
Exits 0 on success, non-zero on any failure. Prints a loud SKIPPED message
and exits 0 if the game data directory is missing.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gamesel

gamesel.parse_game_arg()
import defs  # noqa: E402


def golden_path() -> str:
    return gamesel.golden_path("defs_summary.json")


def main() -> int:
    if gamesel.skip_no_data("test_defs"):
        return 0
    root = defs.extracted_dir()
    want = gamesel.expected()["definitions"]

    db = defs.DefDatabase()
    if not db.load(root):
        print(f"FAIL: DefDatabase.load({root!r}) failed: {db.warnings}", file=sys.stderr)
        return 1

    unresolved = db.validate(root)

    obj_names = [o.name for o in db.objects]
    summary = {
        "total_objects": len(db.objects),
        "distinct_object_names": len({n.lower() for n in obj_names if n}),
        "duplicate_object_names": sorted(
            {n for n in obj_names if obj_names.count(n) > 1}
        ),
        "total_weapons": len(db.weapons),
        "total_particle_systems": len(db.particle_systems),
        "total_levels": len(db.levels),
        "total_load_warnings": len(db.warnings) - len(unresolved),
    }

    golden = {
        "objects": [
            {"name": o.name, "hash": defs.sha1_of(defs.canonical_object(o))}
            for o in db.objects
        ],
        "weapons": [
            {"name": w.name, "hash": defs.sha1_of(defs.canonical_weapon(w))}
            for w in db.weapons
        ],
        "particle_systems": [
            {"name": p.name, "hash": defs.sha1_of(defs.canonical_particle_system(p))}
            for p in db.particle_systems
        ],
        "levels": [
            {"name": lv.id, "hash": defs.sha1_of(defs.canonical_level(lv))}
            for lv in db.levels
        ],
        "summary": summary,
        "unresolved_references": sorted(unresolved),
    }

    failures = 0

    for key, field in (("objects", "total_objects"), ("distinct_object_names", "distinct_object_names"),
                       ("weapons", "total_weapons"), ("particle_systems", "total_particle_systems"),
                       ("levels", "total_levels")):
        if summary[field] != want[key]:
            failures += 1
            print(f"FAIL: expected {want[key]} {key}, got {summary[field]}", file=sys.stderr)
    if sorted(unresolved) != sorted(want["unresolved_references"]):
        failures += 1
        print("FAIL: unresolved references differ from expected.json:", file=sys.stderr)
        for u in sorted(set(unresolved) - set(want["unresolved_references"])):
            print(f"  unexpected: {u}", file=sys.stderr)
        for u in sorted(set(want["unresolved_references"]) - set(unresolved)):
            print(f"  gone: {u}", file=sys.stderr)

    gpath = golden_path()
    if not os.path.exists(gpath):
        os.makedirs(os.path.dirname(gpath), exist_ok=True)
        with open(gpath, "w") as f:
            json.dump(golden, f, indent=1, sort_keys=True)
            f.write("\n")
        print(f"WROTE golden file: {gpath}")
    else:
        with open(gpath) as f:
            expected = json.load(f)
        if expected != golden:
            failures += 1
            print(f"FAIL: {gpath} does not match freshly loaded data.", file=sys.stderr)
            for section in ("objects", "weapons", "particle_systems", "levels"):
                exp = expected.get(section, [])
                act = golden.get(section, [])
                if exp != act:
                    print(f"  section '{section}' differs ({len(exp)} vs {len(act)} entries)",
                          file=sys.stderr)
                    for i, (e, a) in enumerate(zip(exp, act)):
                        if e != a:
                            print(f"    [{i}] golden={e} actual={a}", file=sys.stderr)
                            break
            if expected.get("summary") != golden["summary"]:
                print(f"  summary: golden={expected.get('summary')} actual={golden['summary']}",
                      file=sys.stderr)
            if expected.get("unresolved_references") != golden["unresolved_references"]:
                print("  unresolved_references differ:", file=sys.stderr)
                print(f"    golden: {expected.get('unresolved_references')}", file=sys.stderr)
                print(f"    actual: {golden['unresolved_references']}", file=sys.stderr)

    if failures:
        print(f"FAIL: {failures} failure(s).", file=sys.stderr)
        return 1

    print(
        f"OK: {summary['total_objects']} objects "
        f"({summary['distinct_object_names']} distinct names), "
        f"{summary['total_weapons']} weapons, "
        f"{summary['total_particle_systems']} particle systems, "
        f"{summary['total_levels']} levels, "
        f"{len(golden['unresolved_references'])} unresolved reference(s)."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
