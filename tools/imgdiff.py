#!/usr/bin/env python3
"""Compares two PNGs with a per-channel tolerance and a maximum allowed fraction of
differing pixels. PIL only (no numpy).

Two images are considered a match if the fraction of pixels whose largest single-channel
(R/G/B/A) absolute difference exceeds --tolerance is at most --max-diff-fraction. This is
meant for comparing a freshly rendered PNG (out/, testdata/golden_png/ -- both gitignored,
since rendered game-asset images are copyrighted derivatives, see docs/graphics.md)
against a previously captured reference, tolerating the small pixel-level differences a
different GPU/driver (e.g. llvmpipe vs. an NVIDIA driver) can produce for the same draw
calls.

Usage:
    python3 tools/imgdiff.py A.png B.png [--tolerance N] [--max-diff-fraction F]
                                          [--diff-out out.png]

Exit status: 0 if the images match within tolerance, 1 on a mismatch, 2 on a usage/IO
error (missing file, size mismatch, ...).
"""
import argparse
import sys

from PIL import Image, ImageChops


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("image_a")
    parser.add_argument("image_b")
    parser.add_argument(
        "--tolerance", type=int, default=8,
        help="max per-channel absolute difference (0-255) still counted as matching (default: 8)")
    parser.add_argument(
        "--max-diff-fraction", type=float, default=0.01,
        help="max fraction (0-1) of pixels allowed to differ beyond --tolerance (default: 0.01)")
    parser.add_argument("--diff-out", default=None, help="optional path to write a visual diff PNG")
    args = parser.parse_args(argv)

    try:
        image_a = Image.open(args.image_a).convert("RGBA")
    except OSError as e:
        print(f"imgdiff: could not open '{args.image_a}': {e}", file=sys.stderr)
        return 2
    try:
        image_b = Image.open(args.image_b).convert("RGBA")
    except OSError as e:
        print(f"imgdiff: could not open '{args.image_b}': {e}", file=sys.stderr)
        return 2

    if image_a.size != image_b.size:
        print(f"imgdiff: size mismatch: {args.image_a} is {image_a.size}, "
              f"{args.image_b} is {image_b.size}", file=sys.stderr)
        return 2

    diff = ImageChops.difference(image_a, image_b)  # per-channel absolute difference, RGBA
    bands = diff.split()  # (R, G, B, A) difference bands, each mode "L"

    # A pixel "differs" if ANY channel's absolute difference exceeds --tolerance.
    # point() with a plain function builds a 256-entry lookup table (fast: no python
    # loop over pixels), so this scales fine to full-size renders without numpy.
    thresholded = [band.point(lambda v: 255 if v > args.tolerance else 0) for band in bands]
    exceeds = thresholded[0]
    for band in thresholded[1:]:
        exceeds = ImageChops.lighter(exceeds, band)

    histogram = exceeds.histogram()
    diff_pixel_count = histogram[255] if len(histogram) > 255 else 0
    total_pixels = image_a.size[0] * image_a.size[1]
    diff_fraction = (diff_pixel_count / total_pixels) if total_pixels else 0.0
    max_channel_diff = max(band.getextrema()[1] for band in bands)

    print(f"imgdiff: {args.image_a} vs {args.image_b}: {image_a.size[0]}x{image_a.size[1]}")
    print(f"imgdiff: tolerance={args.tolerance} max-diff-fraction={args.max_diff_fraction}")
    print(f"imgdiff: differing pixels: {diff_pixel_count}/{total_pixels} "
          f"({diff_fraction * 100:.4f}%)")
    print(f"imgdiff: max single-channel difference seen: {max_channel_diff}")

    if args.diff_out:
        # Visualise: near-black where pixels match, a boosted per-channel difference
        # where they don't (forced opaque so the file is viewable directly).
        boosted = diff.point(lambda v: min(255, v * 4))
        r, g, b, _ = boosted.split()
        Image.merge("RGB", (r, g, b)).save(args.diff_out)
        print(f"imgdiff: wrote diff image to {args.diff_out}")

    if diff_fraction > args.max_diff_fraction:
        print("imgdiff: MISMATCH", file=sys.stderr)
        return 1
    print("imgdiff: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
