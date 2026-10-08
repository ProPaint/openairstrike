#!/usr/bin/env python3
"""Compares the 3DS mesh test's top screen with the desktop renderer (M1.5).

Usage: tools/3ds_meshtest_compare.py <desktop.png> <pica.ppm|png> <side_by_side.png> [--clear R,G,B]

<desktop.png>: as3d_game --headless --level 1 --bot --frames 120 --size 400x240
               --screenshot-every 120 --render-only models   (frame_000120.png)
<pica>:        the mesh test's read-back of test b (meshtest_b.ppm, 400x240 upright) or any
               400x240 image of the top screen.

The mask is the desktop pixels that differ from the clear colour (default: the most common
colour of the desktop image), minus the 10x10 corners where the 3DS test draws its markers.
Prints the mean absolute error over the mask (per channel, 0..255) and the fraction of mask
pixels whose largest channel difference is within 16/255; the same for the whole image. Writes
desktop | 3DS | 4 x |difference| side by side.
"""
import sys
from collections import Counter

from PIL import Image


def main():
    args = sys.argv[1:]
    clear = None
    if "--clear" in args:
        i = args.index("--clear")
        clear = tuple(int(v) for v in args[i + 1].split(","))
        del args[i:i + 2]
    if len(args) != 3:
        print(__doc__)
        return 2
    desk = Image.open(args[0]).convert("RGB")
    pica = Image.open(args[1]).convert("RGB")
    if desk.size != pica.size:
        print(f"sizes differ: {desk.size} vs {pica.size}")
        return 1
    w, h = desk.size
    d, p = desk.load(), pica.load()
    if clear is None:
        clear = Counter(desk.getdata()).most_common(1)[0][0]
    diff_img = Image.new("RGB", (w, h))
    q = diff_img.load()
    n = within = 0
    err = 0
    n_all = within_all = 0
    err_all = 0
    for y in range(h):
        for x in range(w):
            a, b = d[x, y], p[x, y]
            e = [abs(a[k] - b[k]) for k in range(3)]
            q[x, y] = tuple(min(255, 4 * v) for v in e)
            corner = (x < 10 or x >= w - 10) and (y < 10 or y >= h - 10)
            if corner:
                continue
            n_all += 1
            err_all += sum(e)
            within_all += max(e) <= 16
            if a != clear:
                n += 1
                err += sum(e)
                within += max(e) <= 16
    print(f"clear colour {clear}")
    if n:
        print(f"model pixels {n}: mean abs error {err / (3 * n):.2f}/255, within 16/255: {100.0 * within / n:.1f}%")
    else:
        print("no model pixels in the desktop image")
    print(f"whole image {n_all}: mean abs error {err_all / (3 * n_all):.2f}/255, "
          f"within 16/255: {100.0 * within_all / n_all:.1f}%")
    out = Image.new("RGB", (3 * w + 8, h), (255, 255, 255))
    out.paste(desk, (0, 0))
    out.paste(pica, (w + 4, 0))
    out.paste(diff_img, (2 * w + 8, 0))
    out.save(args[2])
    print(f"wrote {args[2]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
