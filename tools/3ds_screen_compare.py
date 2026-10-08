#!/usr/bin/env python3
"""Compares the top screen of an Azahar screenshot with a 400x240 desktop reference.

Usage: tools/3ds_screen_compare.py <screenshot.png> <reference.png> [--crop-out top.png]
                                   [--diff-out diff.png] [--side-out side.png] [--mask]
                                   [--ignore X0,Y0,X1,Y1 ...]

The top screen is located by the three 6x6 corner markers the 3DS test modes draw (as
tools/3ds_picatest_check.py: top-left (250,10,130), top-right (10,250,130), bottom-left
(130,10,250)) and resampled to 400x240 (nearest). The 8x8 corners holding the markers are left
out of the metrics. Prints the mean absolute error per channel (0..255) and the fraction of
pixels whose largest channel difference is within 8, 16 and 32. --mask also prints the same over
the non-background pixels (those where the reference or the screenshot differs from the
reference's most common colour by more than 8: the effects, for images that are mostly a flat
clear colour). PIL only. The images show game
art: keep them under out/ (gitignored).
"""
import argparse
import importlib.util
import os
import sys

from PIL import Image


def load_checker():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "3ds_picatest_check.py")
    spec = importlib.util.spec_from_file_location("picatest_check", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def locate(img):
    chk = load_checker()
    w, h = img.size
    tl = chk.find_marker(img, chk.MARKERS["tl"])
    if not tl:
        return None
    x0, y0 = int(tl[0]), int(tl[1])
    tr = chk.find_marker(img, chk.MARKERS["tr"], xr=(x0 + 8, w), yr=(max(y0 - 12, 0), min(y0 + 12, h)))
    bl = chk.find_marker(img, chk.MARKERS["bl"], xr=(max(x0 - 12, 0), min(x0 + 12, w)), yr=(y0 + 8, h))
    if not tr or not bl:
        return None
    sx = (tr[0] - tl[0]) / 394.0
    sy = (bl[1] - tl[1]) / 234.0
    return tl[0] - 3.0 * sx, tl[1] - 3.0 * sy, sx, sy


def top_screen(img):
    loc = locate(img)
    if not loc:
        return None
    ox, oy, sx, sy = loc
    out = Image.new("RGB", (400, 240))
    src, dst = img.load(), out.load()
    w, h = img.size
    for y in range(240):
        for x in range(400):
            px = min(max(int(ox + (x + 0.5) * sx), 0), w - 1)
            py = min(max(int(oy + (y + 0.5) * sy), 0), h - 1)
            dst[x, y] = src[px, py]
    return out


IGNORE = []  # rectangles (x0, y0, x1, y1) left out of the metrics, from --ignore


def in_marker(x, y):
    """True for the corner marker squares and for the --ignore rectangles."""
    if (x < 8 or x >= 392) and (y < 8 or y >= 232):
        return True
    return any(x0 <= x < x1 and y0 <= y < y1 for x0, y0, x1, y1 in IGNORE)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("screenshot")
    ap.add_argument("reference")
    ap.add_argument("--crop-out")
    ap.add_argument("--diff-out")
    ap.add_argument("--side-out")
    ap.add_argument("--mask", action="store_true")
    ap.add_argument("--ignore", action="append", default=[], metavar="X0,Y0,X1,Y1")
    a = ap.parse_args(argv)
    IGNORE.extend(tuple(int(v) for v in r.split(",")) for r in a.ignore)
    shot = Image.open(a.screenshot).convert("RGB")
    ref = Image.open(a.reference).convert("RGB")
    if ref.size != (400, 240):
        print(f"reference is {ref.size}, expected 400x240")
        return 2
    top = shot if shot.size == (400, 240) else top_screen(shot)
    if top is None:
        print("top screen markers not found")
        return 2
    if a.crop_out:
        top.save(a.crop_out)
    p, q = top.load(), ref.load()
    n = 0
    err = [0, 0, 0]
    within = {8: 0, 16: 0, 32: 0}
    diff = Image.new("RGB", (400, 240))
    d = diff.load()
    for y in range(240):
        for x in range(400):
            if in_marker(x, y):
                continue
            a_, b_ = p[x, y], q[x, y]
            dd = [abs(a_[i] - b_[i]) for i in range(3)]
            for i in range(3):
                err[i] += dd[i]
            m = max(dd)
            for t in within:
                if m <= t:
                    within[t] += 1
            n += 1
            v = min(255, m * 4)
            d[x, y] = (v, v, v)
    if a.mask:
        counts = {}
        for y in range(240):
            for x in range(400):
                if not in_marker(x, y):
                    counts[q[x, y]] = counts.get(q[x, y], 0) + 1
        bg = max(counts, key=counts.get)
        mn, merr, mw = 0, [0, 0, 0], {8: 0, 16: 0, 32: 0}
        for y in range(240):
            for x in range(400):
                if in_marker(x, y):
                    continue
                a_, b_ = p[x, y], q[x, y]
                if max(abs(b_[i] - bg[i]) for i in range(3)) <= 8 and max(abs(a_[i] - bg[i]) for i in range(3)) <= 8:
                    continue
                dd = [abs(a_[i] - b_[i]) for i in range(3)]
                for i in range(3):
                    merr[i] += dd[i]
                for t in mw:
                    if max(dd) <= t:
                        mw[t] += 1
                mn += 1
        if mn:
            mm = [e / mn for e in merr]
            print(f"non-background mask: {mn} pixels (background {bg}); mean abs error R {mm[0]:.2f} G {mm[1]:.2f} "
                  f"B {mm[2]:.2f} (all {sum(mm) / 3:.2f}); within 8: {mw[8] / mn:.3f}, within 16: {mw[16] / mn:.3f}, "
                  f"within 32: {mw[32] / mn:.3f}")
        else:
            print("non-background mask: empty")
    mae = [e / n for e in err]
    print(f"mean abs error R {mae[0]:.2f} G {mae[1]:.2f} B {mae[2]:.2f} (all {sum(mae) / 3:.2f})")
    print("within 8: {:.3f}, within 16: {:.3f}, within 32: {:.3f}".format(*(within[t] / n for t in (8, 16, 32))))
    if a.diff_out:
        diff.save(a.diff_out)
    if a.side_out:
        side = Image.new("RGB", (1200, 240))
        side.paste(top, (0, 0))
        side.paste(ref, (400, 0))
        side.paste(diff, (800, 0))
        side.save(a.side_out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
