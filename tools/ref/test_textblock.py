#!/usr/bin/env python3
"""Golden test for the text-block reference parser (tools/ref/textblock.py).

Parses all 42 brace-block game text files, checks there are zero parse
errors, and writes/validates testdata/golden/textblock_counts.json (per-file
block/statement/token counts -- metadata only, never game text).

Run directly, or via tools/ci.sh which runs every tools/ref/test_*.py.
Exits 0 on success, non-zero on any failure. If the game data directory is
missing, prints a loud SKIPPED message and exits 0 (the data is gitignored
and not present in a fresh checkout or a worktree).
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import textblock as tb

GOLDEN_PATH_PARTS = ("testdata", "golden", "textblock_counts.json")


def repo_root() -> str:
    # tools/ref/test_textblock.py -> tools/ref -> tools -> repo root.
    return os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def golden_path() -> str:
    return os.path.join(repo_root(), *GOLDEN_PATH_PARTS)


def main() -> int:
    root = tb.extracted_dir()
    if not os.path.isdir(root):
        print(f"SKIPPED (no game data): {root} does not exist.", file=sys.stderr)
        print(
            "SKIPPED: set AS3D_DATA_ROOT or run tools/setup_data.sh + "
            "tools/paktool.py extract first.",
            file=sys.stderr,
        )
        return 0

    files = tb.list_text_block_files(root)
    if len(files) != 42:
        print(
            f"FAIL: expected 42 text-block files, found {len(files)}: {files}",
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
