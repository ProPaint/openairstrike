#!/usr/bin/env python3
"""Acceptance test for the .mdl reference parser (tools/ref/mdl.py).

Run directly, or via tools/ci.sh. Needs the extracted game data; see README.md
for how to fetch it, and set AS3D_DATA_ROOT to point at a checkout that has it
(e.g. the main checkout, when running from a worktree). Skips loudly and exits
0 when the data is not available, so CI without game data still passes.

  AS3D_DATA_ROOT=/path/to/airstrike3d python3 tools/ref/test_mdl.py

Set AS3D_MDL_REGENERATE_GOLDEN=1 to overwrite testdata/golden/mdl_summary.json
with freshly computed data instead of diffing against it (do this deliberately,
after checking the diff makes sense, and commit the result).
"""
import hashlib
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mdl  # noqa: E402

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
GOLDEN_PATH = os.path.join(REPO_ROOT, "testdata", "golden", "mdl_summary.json")

# The 4 shipped .mdl files that are exactly zero bytes. None are referenced by
# any object in assets_extracted/objects/*.obj (checked by hand during
# development of docs/spec/mdl.md). If this set ever changes, that is a real
# regression worth looking at, not something to silently paper over.
EXPECTED_EMPTY = {
    "models/items/ammo/asec.mdl",
    "models/items/ammo/bonus.mdl",
    "models/jeeps/jeep_bug_cannon.mdl",
    "models/mapobjects/ruins/stone_comb1.mdl",
}

# The 2 shipped non-empty .mdl files whose own header counts do not add up to
# their file size: both contain runs of 0xFD/0xDD bytes (classic MSVC debug
# heap fill patterns) inside what should be vertex data, i.e. these are
# corrupted source assets, not evidence of a format variant. See
# docs/spec/mdl.md "Files that do not fit". If this set changes, treat it as
# a real finding (either a fixed asset, or a new corruption) and update both
# this test and the spec.
EXPECTED_BROKEN = {
    "models/mapobjects/bridges/bridge1.mdl": -24,
    "models/mapobjects/bridges/japbridge.mdl": -336,
}

# Chosen from the data (see docs/spec/mdl.md #normals): flat (per-face)
# normals are unit length to within float32 rounding in 819/821 shipped
# examples; the 2 exceptions are exactly zero (degenerate triangles in
# banner.mdl) and are reported, not hidden. Smooth (per-vertex) normals are
# the raw sum of adjacent face cross products with no averaging or
# renormalisation, so their length legitimately ranges from 0 up to 1.0
# (observed max across all shipped files: 1.0000146, i.e. 1.0 + float slop).
FLAT_NORMAL_TOL = 0.02
SMOOTH_NORMAL_MAX = 1.0 + 1e-3
BBOX_TOL = 0.05  # engine units; bbox is exact in the data, this covers float32 slop


def data_available():
    return os.path.isfile(os.path.join(mdl.extracted_dir(), "maps", "levels.txt"))


def finite3(t):
    return all(math.isfinite(c) for c in t)


def sha1_arrays(data):
    """sha1 over exactly the array bytes (from the end of the header to EOF):
    vertices, uvs, faces, normals, tags, in file order. This is a byte-exact,
    order-sensitive fingerprint of everything parse() extracts, without
    embedding any of that (possibly large) data in the golden file itself.
    Little-endian raw IEEE-754 floats and u16 indices, in array order, exactly
    as stored on disk -- i.e. before any engine-side conversion."""
    return hashlib.sha1(data[mdl.HEADER_SIZE:]).hexdigest()


def sha1_engine(m):
    """sha1 over the same arrays after the one engine-side conversion that is
    exactly, bit-for-bit reproducible across independent implementations: the
    V flip (v' = 1 - v, a single IEEE-754 float32 subtraction, per
    docs/spec/mdl.md "Texture V orientation"). Positions, face indices and
    tags are unchanged from the raw on-disk bytes.

    Deliberately excludes the smooth-normal recompute: that involves many
    float32 operations in a specific order (see docs/spec/mdl.md "Normals"),
    and this reference implementation does not guarantee the same rounding
    as a C++ compiler's instruction selection (x87 vs SSE, FMA, etc.), so an
    exact hash match is not "practical" for it as the spec's data model asks.
    The C++ test instead checks the recomputed/normalized normals numerically
    (direction and unit length within tolerance), not by hash.
    """
    parts = []
    for x, y, z in m.vertices:
        parts.append(struct.pack("<3f", x, y, z))
    for u, v in m.uvs:
        parts.append(struct.pack("<2f", u, 1.0 - v))
    for face in m.faces:
        parts.append(struct.pack("<6H", *face))
    for x, y, z in m.normals:
        parts.append(struct.pack("<3f", x, y, z))
    for t in m.tags:
        name = t.name.encode("ascii", "replace")[:mdl.TAG_NAME_SIZE]
        name = name + b"\0" * (mdl.TAG_NAME_SIZE - len(name))
        parts.append(name)
        parts.append(struct.pack("<3f", *t.pos))
        parts.append(struct.pack("<3f", *t.direction))
    return hashlib.sha1(b"".join(parts)).hexdigest()


def check_model(relpath, path, size, errors, warnings):
    with open(path, "rb") as f:
        data = f.read()

    try:
        m = mdl.parse(data, source=relpath)
    except mdl.MdlError as e:
        if relpath in EXPECTED_BROKEN:
            warnings.append(f"KNOWN-BROKEN {relpath}: {e}")
            return {"path": relpath, "size": size, "status": "broken", "note": str(e)}
        errors.append(f"{relpath}: does not parse and is not in EXPECTED_BROKEN: {e}")
        return None

    nverts, nuvs, nfaces, nnorms, ntags = (
        len(m.vertices), len(m.uvs), len(m.faces), len(m.normals), len(m.tags))

    # magic/version/byte-accounting were already checked inside mdl.parse().

    # Indices in range.
    for i, (a, b, c, ua, ub, uc) in enumerate(m.faces):
        if not (0 <= a < nverts and 0 <= b < nverts and 0 <= c < nverts):
            errors.append(f"{relpath}: face {i} vertex index out of range (nverts={nverts})")
        if not (0 <= ua < nuvs and 0 <= ub < nuvs and 0 <= uc < nuvs):
            errors.append(f"{relpath}: face {i} uv index out of range (nuvs={nuvs})")

    # All floats finite.
    if not finite3(m.bbox[0:3]) or not finite3(m.bbox[3:6]):
        errors.append(f"{relpath}: non-finite bbox {m.bbox}")
    for i, v in enumerate(m.vertices):
        if not finite3(v):
            errors.append(f"{relpath}: non-finite vertex {i}: {v}")
    for i, uv in enumerate(m.uvs):
        if not (math.isfinite(uv[0]) and math.isfinite(uv[1])):
            errors.append(f"{relpath}: non-finite uv {i}: {uv}")
    for i, n in enumerate(m.normals):
        if not finite3(n):
            errors.append(f"{relpath}: non-finite normal {i}: {n}")
    for i, t in enumerate(m.tags):
        if not (finite3(t.pos) and finite3(t.direction)):
            errors.append(f"{relpath}: tag {i} ({t.name!r}) has non-finite floats")

    # Normal length, per the smooth/flat rule (see module docstring above).
    # nverts == nfaces is possible (a tie), in which case either reading is
    # consistent with the counts and we cannot tell which one applies from
    # the counts alone; the header's smooth_normals flag is authoritative.
    per_vertex = nnorms == nverts
    per_face = nnorms == nfaces
    if m.smooth_normals and not per_vertex:
        errors.append(f"{relpath}: smooth_normals set but normals count {nnorms} != vertices {nverts}")
    if (not m.smooth_normals) and not per_face:
        errors.append(f"{relpath}: flat normals but normals count {nnorms} != faces {nfaces}")
    per_vertex = m.smooth_normals and per_vertex
    per_face = (not m.smooth_normals) and per_face
    for i, (x, y, z) in enumerate(m.normals):
        length = math.sqrt(x * x + y * y + z * z)
        if per_vertex:
            if length > SMOOTH_NORMAL_MAX:
                errors.append(f"{relpath}: smooth normal {i} length {length} exceeds {SMOOTH_NORMAL_MAX}")
        else:
            if not (1.0 - FLAT_NORMAL_TOL <= length <= 1.0 + FLAT_NORMAL_TOL):
                if length < 1e-6:
                    warnings.append(f"{relpath}: flat normal {i} is zero-length (degenerate triangle)")
                else:
                    errors.append(f"{relpath}: flat normal {i} length {length} outside "
                                   f"[{1-FLAT_NORMAL_TOL},{1+FLAT_NORMAL_TOL}]")

    # Vertices inside the bounding box (within tolerance).
    minx, miny, minz, maxx, maxy, maxz = m.bbox
    for i, (x, y, z) in enumerate(m.vertices):
        if not (minx - BBOX_TOL <= x <= maxx + BBOX_TOL and
                miny - BBOX_TOL <= y <= maxy + BBOX_TOL and
                minz - BBOX_TOL <= z <= maxz + BBOX_TOL):
            errors.append(f"{relpath}: vertex {i} {(x,y,z)} outside bbox {m.bbox} (tol {BBOX_TOL})")

    # Tag names printable ASCII.
    for i, t in enumerate(m.tags):
        if not t.name or not all(32 <= ord(ch) < 127 for ch in t.name):
            errors.append(f"{relpath}: tag {i} name not printable ASCII: {t.name!r}")

    return {
        "path": relpath,
        "size": size,
        "status": "ok",
        "version": m.version,
        "smooth_normals": m.smooth_normals,
        "counts": {"vertices": nverts, "uvs": nuvs, "faces": nfaces, "normals": nnorms, "tags": ntags},
        "bbox": list(m.bbox),
        "tag_names": [t.name for t in m.tags],
        "sha1_arrays": sha1_arrays(data),
        "sha1_engine": sha1_engine(m),
    }


def main():
    if not data_available():
        print("=" * 70, file=sys.stderr)
        print("SKIPPED test_mdl.py: game data not found "
              f"(looked under {mdl.extracted_dir()})", file=sys.stderr)
        print("Set AS3D_DATA_ROOT to a checkout with assets_extracted/, "
              "see README.md.", file=sys.stderr)
        print("=" * 70, file=sys.stderr)
        return 0

    errors = []
    warnings = []
    summary = []
    seen_empty = set()

    entries = sorted(mdl.iter_models(), key=lambda e: e[0])
    if not entries:
        errors.append("no .mdl files found under assets_extracted/models")

    for relpath, path, size in entries:
        relpath = relpath.replace(os.sep, "/")
        if size == 0:
            seen_empty.add(relpath)
            summary.append({"path": relpath, "size": 0, "status": "empty"})
            continue
        rec = check_model(relpath, path, size, errors, warnings)
        if rec is not None:
            summary.append(rec)

    if seen_empty != EXPECTED_EMPTY:
        errors.append(f"empty-file set changed: now {sorted(seen_empty)}, "
                       f"expected {sorted(EXPECTED_EMPTY)}")

    seen_broken = {r["path"] for r in summary if r.get("status") == "broken"}
    if seen_broken != set(EXPECTED_BROKEN):
        errors.append(f"broken-file set changed: now {sorted(seen_broken)}, "
                       f"expected {sorted(EXPECTED_BROKEN)}")

    print(f"test_mdl: {len(entries)} files, "
          f"{sum(1 for r in summary if r['status']=='ok')} ok, "
          f"{sum(1 for r in summary if r['status']=='empty')} empty, "
          f"{sum(1 for r in summary if r['status']=='broken')} broken")

    for w in warnings:
        print(f"test_mdl: warning: {w}")

    # Golden file: generate if missing (or on request), else diff exactly.
    summary.sort(key=lambda r: r["path"])
    regenerate = os.environ.get("AS3D_MDL_REGENERATE_GOLDEN") == "1"
    if regenerate or not os.path.isfile(GOLDEN_PATH):
        os.makedirs(os.path.dirname(GOLDEN_PATH), exist_ok=True)
        with open(GOLDEN_PATH, "w") as f:
            json.dump(summary, f, indent=1, sort_keys=True)
            f.write("\n")
        print(f"test_mdl: wrote golden file {GOLDEN_PATH} ({len(summary)} entries)")
    else:
        with open(GOLDEN_PATH) as f:
            golden = json.load(f)
        if golden != summary:
            golden_by_path = {r["path"]: r for r in golden}
            summary_by_path = {r["path"]: r for r in summary}
            for p in sorted(set(golden_by_path) | set(summary_by_path)):
                if golden_by_path.get(p) != summary_by_path.get(p):
                    errors.append(f"golden mismatch for {p}:\n"
                                   f"  golden: {golden_by_path.get(p)}\n"
                                   f"  actual: {summary_by_path.get(p)}")

    if errors:
        print(f"test_mdl: FAILED with {len(errors)} error(s):", file=sys.stderr)
        for e in errors[:200]:
            print(f"  {e}", file=sys.stderr)
        if len(errors) > 200:
            print(f"  ... and {len(errors) - 200} more", file=sys.stderr)
        return 1

    print("test_mdl: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
