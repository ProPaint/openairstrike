#!/usr/bin/env python3
"""Checks an Azahar screenshot of the PICA core test (apps/3ds/source/pica_test.cpp).

Usage: tools/3ds_picatest_check.py <screenshot.png> <picatest_checks.txt>

The checks file has one line per probe, "name x y r g b tol": the expected colour at pixel
(x, y) of the upright 400x240 top screen (origin top-left). The top screen is located in the
screenshot by the three 6x6 corner markers the test draws (top-left (250,10,130), top-right
(10,250,130), bottom-left (130,10,250)), so any window size or emulator scale works. Each
probe reads the 3x3 median around its pixel. Exit status 0 when every probe is within its
tolerance (per channel). A negative tolerance inverts the probe: the pixel must differ from
(r, g, b) by more than -tol in some channel (apps/3ds/source/mesh_test.cpp: "a model covers
this pixel"). The same checks work on an upright 400x240 image of the top screen (the mesh
test's .ppm read-back), where the markers sit at their own positions.
"""
import sys

from PIL import Image

MARKERS = {"tl": (250, 10, 130), "tr": (10, 250, 130), "bl": (130, 10, 250)}
MARKER_CENTRES = {"tl": (3.0, 3.0), "tr": (397.0, 3.0), "bl": (3.0, 237.0)}


def find_marker(img, colour, tol=12, xr=None, yr=None):
    """Centroid of the pixels within `tol` of `colour`, inside the optional x/y ranges."""
    w, h = img.size
    px = img.load()
    xs, ys = [], []
    for y in range(*(yr or (0, h))):
        for x in range(*(xr or (0, w))):
            p = px[x, y]
            if all(abs(p[i] - colour[i]) <= tol for i in range(3)):
                xs.append(x)
                ys.append(y)
    if not xs:
        return None
    return (sum(xs) / len(xs), sum(ys) / len(ys), len(xs))


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    img = Image.open(sys.argv[1]).convert("RGB")
    # The top-left marker anywhere, then the other two on its row and column only (the
    # emulator's status bar has similar greens).
    w, h = img.size
    found = {"tl": find_marker(img, MARKERS["tl"])}
    if found["tl"]:
        x0, y0 = int(found["tl"][0]), int(found["tl"][1])
        found["tr"] = find_marker(img, MARKERS["tr"], xr=(x0 + 8, w), yr=(max(y0 - 12, 0), min(y0 + 12, h)))
        found["bl"] = find_marker(img, MARKERS["bl"], xr=(max(x0 - 12, 0), min(x0 + 12, w)), yr=(y0 + 8, h))
    for name in MARKERS:
        if not found.get(name):
            print(f"marker {name} {MARKERS[name]} not found")
            return 1
    (tlx, tly, _), (trx, _, _), (_, bly, _) = found["tl"], found["tr"], found["bl"]
    sx = (trx - tlx) / (MARKER_CENTRES["tr"][0] - MARKER_CENTRES["tl"][0])
    sy = (bly - tly) / (MARKER_CENTRES["bl"][1] - MARKER_CENTRES["tl"][1])
    ox, oy = tlx - 3.0 * sx, tly - 3.0 * sy
    print(f"top screen at ({ox:.1f}, {oy:.1f}), scale {sx:.3f} x {sy:.3f}")
    px = img.load()

    def sample(x, y):
        cx, cy = ox + (x + 0.5) * sx, oy + (y + 0.5) * sy
        vals = [px[int(cx) + dx, int(cy) + dy] for dx in (-1, 0, 1) for dy in (-1, 0, 1)]
        return tuple(sorted(v[i] for v in vals)[4] for i in range(3))

    failures = 0
    total = 0
    with open(sys.argv[2]) as f:
        for line in f:
            parts = line.split()
            if len(parts) != 7:
                continue
            name, x, y, r, g, b, tol = parts[0], *map(int, parts[1:])
            got = sample(x, y)
            diff = max(abs(got[0] - r), abs(got[1] - g), abs(got[2] - b))
            ok = diff > -tol if tol < 0 else diff <= tol
            total += 1
            failures += 0 if ok else 1
            print(f"{'ok  ' if ok else 'FAIL'} {name:20s} ({x:3d},{y:3d}) expected ({r:3d},{g:3d},{b:3d}) "
                  f"got ({got[0]:3d},{got[1]:3d},{got[2]:3d}) diff {diff} tol {tol}")
    print(f"{total - failures}/{total} probes within tolerance")
    return 0 if failures == 0 and total > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
