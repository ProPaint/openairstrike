#!/usr/bin/env python3
"""Acceptance test for the HMAP level parser (tools/ref/hmap.py).

Run directly, or via tools/ci.sh. Needs the extracted game data; set
AS3D_DATA_ROOT to a checkout that has assets_extracted/ (e.g. the main
checkout when running from a worktree). Skips loudly and exits 0 when the data
is not available.

  AS3D_DATA_ROOT=/path/to/airstrike3d python3 tools/ref/test_hmap.py

`--game <key>` (else $AS3D_GAME, else as3d) picks the game. Set AS3D_HMAP_REGENERATE_GOLDEN=1 to
overwrite testdata/golden/<key>/hmap_summary.json
instead of diffing against it (check the diff, then commit it).

Checks, for every maps/*.hsc (see docs/spec/hmap.md for the rules):
  - magic, version, parsed length == file length (inside hmap.parse)
  - the set of shipped maps is exactly the expected files (expected.json, "maps")
  - type/item indices in range, rotation < 12, placements inside the grid
  - every type and item name resolves to an objects/*.obj block
    (expected.json lists the documented exceptions)
  - every per-placement script file exists
  - grid layers inside the documented ranges; tile set 0 => no index/rotation;
    tile indices outside their atlas only in the documented cells
  - every waypoint path starts at its placement's cell (x, y-1)
  - every end_of_the_level marker next to a helipad (tile set "pad_tile_set") sits on its centre
  - each map is referenced by exactly one maps/levels.txt block
  - golden summary (header fields, names, per-layer sha1/min/max,
    placement counts per type, sha1 of the canonical placement list)
"""
import collections
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gamesel  # noqa: E402

gamesel.parse_game_arg()
import hmap  # noqa: E402

GOLDEN_PATH = gamesel.golden_path("hmap_summary.json")
EXP = gamesel.expected()["maps"]

EXPECTED_MAPS = set(EXP["names"])

# Names in a type or item table that do not match any objects/*.obj block: {(map, name), ...}.
EXPECTED_UNRESOLVED = {(m, n) for m, n in EXP["unresolved_names"]}

# Cells whose tile index lies outside the atlas, or whose rotation byte is not one of
# 0/3/6/9 (editor garbage; the engine wraps the UVs, see spec).
# {(map, col, row): (set, index, rotation)}
EXPECTED_ODD_TILES = {(t["map"], t["col"], t["row"]): (t["set"], t["index"], t["rotation"])
                      for t in EXP["odd_tiles"]}

TILE_ATLAS_TILES = {}  # set -> number of 64x64 tiles, filled from the data

# end_of_the_level markers within 3 cells of a 3x3 helipad (centre tile: set pad_tile_set,
# index pad_tile_index): all of them sit exactly on the pad's centre cell (x, y-1).
EXPECTED_MARKERS_ON_PADS = EXP["markers_on_pads"]
MAX_TILE_SET = EXP["tile_sets"]
PAD_SET, PAD_INDEX = EXP["pad_tile_set"], EXP["pad_tile_index"]


def data_available():
    return gamesel.has_data()


def atlas_tiles(s):
    if s not in TILE_ATLAS_TILES:
        p = os.path.join(hmap.extracted_dir(), "tiles", f"tiles{s}.tga")
        try:
            with open(p, "rb") as f:
                hdr = f.read(18)
            w = hdr[12] | hdr[13] << 8
            h = hdr[14] | hdr[15] << 8
            TILE_ATLAS_TILES[s] = (w // hmap.TILE_PIXELS) * (h // hmap.TILE_PIXELS)
        except OSError:
            TILE_ATLAS_TILES[s] = 0
    return TILE_ATLAS_TILES[s]


def check_map(name, m, defs, errors, odd_seen):
    W, H = m.width, m.height
    if len(m.type_names) != len(set(m.type_names)):
        errors.append(f"{name}: duplicate type names")
    for n in m.type_names + m.item_names:
        if n not in defs and (name, n) not in EXPECTED_UNRESOLVED:
            errors.append(f"{name}: name {n!r} does not resolve to an object definition")

    # Grid layers.
    cells = m.cells
    for i in range(W * H):
        _h, s, idx, rot = cells[4 * i:4 * i + 4]
        c, r = i % W, i // W
        if s == 0:
            if idx or rot:
                errors.append(f"{name}: cell ({c},{r}) has no tile set but index/rot {idx}/{rot}")
            continue
        if not 1 <= s <= MAX_TILE_SET:
            errors.append(f"{name}: cell ({c},{r}) tile set {s} outside 1..{MAX_TILE_SET}")
            continue
        n_tiles = atlas_tiles(s)
        if n_tiles == 0:
            errors.append(f"{name}: tiles{s}.tga missing")
        if idx >= n_tiles or rot not in (0, 3, 6, 9):
            key = (name, c, r)
            odd_seen[key] = (s, idx, rot)
            if EXPECTED_ODD_TILES.get(key) != (s, idx, rot):
                errors.append(f"{name}: undocumented odd tile at ({c},{r}): {(s, idx, rot)}")

    # Placements.
    for p in m.placements:
        tag = f"{name}: placement {p.index}"
        if not 0 <= p.type_index < len(m.type_names):
            errors.append(f"{tag}: type index {p.type_index} out of range")
            continue
        if p.item_index > len(m.item_names):
            errors.append(f"{tag}: item index {p.item_index} out of range")
        if not (0 <= p.x < W and 1 <= p.y <= H):
            errors.append(f"{tag}: position ({p.x},{p.y}) outside 0..{W-1} x 1..{H}")
        if p.rotation >= 12:
            errors.append(f"{tag}: rotation {p.rotation} >= 12")
        if p.script is not None:
            if not os.path.isfile(hmap.asset_path(p.script)):
                errors.append(f"{tag}: script {p.script!r} not found")
        if p.waypoints:
            w0 = p.waypoints[0]
            if (w0.x, w0.y) != (p.x, p.y - 1):
                errors.append(f"{tag}: path starts at ({w0.x},{w0.y}), not at the placement cell")
            for k, w in enumerate(p.waypoints):
                if not (-8 <= w.x <= W + 8 and 0 <= w.y < H):
                    errors.append(f"{tag}: waypoint {k} ({w.x},{w.y}) far outside the map")
                if w.unknown8 not in (0, 1, 2):
                    errors.append(f"{tag}: waypoint {k} field +8 = {w.unknown8}")
                vals = list(w.in_ctrl) + list(w.out_ctrl) + [w.delay]
                if any(v != v or abs(v) > 1e6 for v in vals):
                    errors.append(f"{tag}: waypoint {k} has non-finite floats")

    # Helipad / end marker consistency (evidence for the y-1 row convention).
    pads = set()
    for i in range(W * H):
        if cells[4 * i + 1] == PAD_SET and cells[4 * i + 2] == PAD_INDEX:  # centre tile of the 3x3 pad
            pads.add((i % W, i // W))
    on_pad = 0
    for p in m.placements:
        if m.type_name(p) == "end_of_the_level":
            near = [(c, r) for (c, r) in pads if abs(c - p.x) <= 3 and abs(r - p.row) <= 3]
            if near and (p.x, p.row) not in pads:
                errors.append(f"{name}: end_of_the_level at ({p.x},{p.y}) is next to but not on "
                              f"the helipad centre {near}")
            elif near:
                on_pad += 1
    return on_pad


def summarise(name, m):
    per_type = collections.Counter(m.type_name(p) for p in m.placements)
    return {
        "file": f"maps/{name}",
        "size": m.size,
        "version": m.version,
        "width": m.width,
        "height": m.height,
        "placement_count": len(m.placements),
        "type_names": m.type_names,
        "item_names": m.item_names,
        "layers": [{k: s[k] for k in ("layer", "min", "max", "nonzero", "sha1")}
                   for s in hmap.layer_stats(m)],
        "placements_per_type": dict(sorted(per_type.items())),
        "placements_with_path": sum(1 for p in m.placements if p.waypoints),
        "waypoints": sum(len(p.waypoints) for p in m.placements),
        "placements_sha1": hmap.placements_sha1(m),
    }


def main():
    if gamesel.skip_no_data("test_hmap"):
        return 0

    errors = []
    paths = hmap.list_maps()
    names = {os.path.basename(p) for p in paths}
    if names != EXPECTED_MAPS:
        errors.append(f"map set changed: missing {sorted(EXPECTED_MAPS - names)}, "
                      f"extra {sorted(names - EXPECTED_MAPS)}")

    defs = hmap.load_object_definitions()
    if not defs:
        errors.append("no object definitions found under objects/")
    levels = hmap.load_level_list()
    refs = collections.Counter(os.path.basename(r.get("map", "").replace("\\", "/")).lower()
                               for r in levels)

    summary = []
    odd_seen = {}
    total_pl = 0
    markers_on_pads = 0
    for path in paths:
        name = os.path.basename(path)
        try:
            m = hmap.parse_file(path)
        except hmap.HmapError as e:
            errors.append(f"{name}: {e}")
            continue
        total_pl += len(m.placements)
        markers_on_pads += check_map(name, m, defs, errors, odd_seen)
        if refs[name.lower()] != 1:
            errors.append(f"{name}: referenced {refs[name.lower()]} times by maps/levels.txt")
        summary.append(summarise(name, m))
    if markers_on_pads != EXPECTED_MARKERS_ON_PADS:
        errors.append(f"{markers_on_pads} end_of_the_level markers on helipad centres, "
                      f"expected {EXPECTED_MARKERS_ON_PADS}")
    missing_odd = set(EXPECTED_ODD_TILES) - set(odd_seen)
    if missing_odd:
        errors.append(f"documented odd tiles no longer present: {sorted(missing_odd)}")

    print(f"test_hmap: {len(paths)} maps, {total_pl} placements, "
          f"{len(defs)} object definitions, {len(odd_seen)} documented odd tile cells")

    summary.sort(key=lambda r: r["file"])
    regenerate = os.environ.get("AS3D_HMAP_REGENERATE_GOLDEN") == "1" or not os.path.isfile(GOLDEN_PATH)
    if regenerate and errors:
        print("test_hmap: not writing the golden file while checks fail", file=sys.stderr)
    elif regenerate:
        os.makedirs(os.path.dirname(GOLDEN_PATH), exist_ok=True)
        with open(GOLDEN_PATH, "w") as f:
            json.dump(summary, f, indent=1, sort_keys=True)
            f.write("\n")
        print(f"test_hmap: wrote golden file {GOLDEN_PATH} ({len(summary)} entries)")
    else:
        with open(GOLDEN_PATH) as f:
            golden = json.load(f)
        if golden != summary:
            g = {r["file"]: r for r in golden}
            a = {r["file"]: r for r in summary}
            for k in sorted(set(g) | set(a)):
                if g.get(k) != a.get(k):
                    errors.append(f"golden mismatch for {k}")

    if errors:
        print(f"test_hmap: FAILED with {len(errors)} error(s):", file=sys.stderr)
        for e in errors[:200]:
            print(f"  {e}", file=sys.stderr)
        return 1
    print("test_hmap: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
