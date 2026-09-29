#!/usr/bin/env python3
"""Golden test for the text-block reference parser (tools/ref/textblock.py).

Parses all brace-block game text files (42 in as3d, the count is in
testdata/golden/<game>/expected.json), checks there are zero parse
errors, and writes/validates testdata/golden/<game>/textblock_counts.json (per-file
block/statement/token counts -- metadata only, never game text).

Run directly (`--game <key>`, else $AS3D_GAME, else as3d), or via tools/ci.sh which runs every
tools/ref/test_*.py once per game.
Exits 0 on success, non-zero on any failure. If the game data directory is
missing, prints a loud SKIPPED message and exits 0 (the data is gitignored
and not present in a fresh checkout or a worktree).
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gamesel

gamesel.parse_game_arg()
import textblock as tb  # noqa: E402


def golden_path() -> str:
    return gamesel.golden_path("textblock_counts.json")


def main() -> int:
    if gamesel.skip_no_data("test_textblock"):
        return 0
    root = tb.extracted_dir()
    want = gamesel.expected()["text_blocks"]

    files = tb.list_text_block_files(root)
    if len(files) != want["files"]:
        print(
            f"FAIL: expected {want['files']} text-block files, found {len(files)}: {files}",
            file=sys.stderr,
        )
        return 1

    failures = 0
    counts = {}
    obj_names = []
    for rel in files:
        path = os.path.join(root, *rel.split("/"))
        tf = tb.parse_file(path)
        if tf.errors:
            failures += 1
            print(f"FAIL: {rel}: {len(tf.errors)} parse error(s):", file=sys.stderr)
            for e in tf.errors[:10]:
                print(f"  {e}", file=sys.stderr)
        counts[rel] = tf.counts()
        if rel.startswith("objects/"):
            obj_names.extend(b.name for b in tf.blocks)

    summary = {
        "objects_total_named_blocks": len(obj_names),
        "objects_distinct_names": len(set(obj_names)),
        "objects_duplicate_names": sorted(
            {n for n in obj_names if obj_names.count(n) > 1}
        ),
    }
    golden = {"files": counts, "summary": summary}
    if summary["objects_total_named_blocks"] != want["object_blocks"] or \
            summary["objects_distinct_names"] != want["distinct_object_names"]:
        failures += 1
        print(f"FAIL: object blocks {summary['objects_total_named_blocks']} "
              f"({summary['objects_distinct_names']} distinct), expected {want['object_blocks']} "
              f"({want['distinct_object_names']})", file=sys.stderr)

    gpath = golden_path()
    if not os.path.exists(gpath) and failures:
        print("not writing the golden file while checks fail", file=sys.stderr)
    elif not os.path.exists(gpath):
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
            print(f"FAIL: {gpath} does not match freshly parsed counts.", file=sys.stderr)
            for rel in sorted(set(expected.get("files", {})) | set(golden["files"])):
                if expected.get("files", {}).get(rel) != golden["files"].get(rel):
                    print(
                        f"  {rel}: golden={expected.get('files', {}).get(rel)} "
                        f"actual={golden['files'].get(rel)}",
                        file=sys.stderr,
                    )
            if expected.get("summary") != golden["summary"]:
                print(
                    f"  summary: golden={expected.get('summary')} "
                    f"actual={golden['summary']}",
                    file=sys.stderr,
                )

    if failures:
        print(f"FAIL: {failures} failure(s).", file=sys.stderr)
        return 1

    print(
        f"OK: {len(files)} files, 0 parse errors, "
        f"{summary['objects_total_named_blocks']} object blocks "
        f"({summary['objects_distinct_names']} distinct names)."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
