#!/usr/bin/env python3
"""Compares a screenshot of an original game (docs/running-originals.md) with our render of the
same place, region by region, with numbers. PIL only (no numpy).

    python3 tools/compare_reference.py ORIGINAL.png OURS.png [--region x0,y0,x1,y1[,name]]...
        [--side out.png] [--diff out.png] [--hist] [--labels "Original,Ours"]
    python3 tools/compare_reference.py ORIGINAL.png --best-of OURS1.png OURS2.png ...
        [--region x0,y0,x1,y1] [--shift 40]

Per region (the whole picture when none is given) it prints, for the original (O) and ours (U):
mean R G B, standard deviation, mean luma, the ratio O/U of each channel mean, the saturation
(max - min of the mean colour), and with --hist a 8-bin histogram per channel. --diff writes the
absolute difference image (x4); --side writes the two pictures side by side with the regions
outlined, labelled, and a strip of their mean colours below each.

--best-of aligns: for each candidate it searches a vertical shift of up to --shift pixels
(steps of 4) and reports the normalised correlation of the luma, blurred and scaled down, over
the region (the static scenery, keep the HUD and moving things out of it). Correlation ignores
brightness and contrast, so it finds the same place even when the colours differ; the best
candidate and shift are printed last.

Coordinates are pixels of the ORIGINAL picture; ours is resized to its size if it differs.
Screenshots and outputs are never committed (out/ is gitignored).
"""
import argparse
import math
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageStat


def parse_region(text, w, h):
    parts = text.split(",")
    if len(parts) < 4:
        raise ValueError("region needs x0,y0,x1,y1[,name]: %r" % text)
    x0, y0, x1, y1 = (int(p) for p in parts[:4])
    name = parts[4] if len(parts) > 4 else "%d,%d,%d,%d" % (x0, y0, x1, y1)
    x0, x1 = max(0, min(x0, x1)), min(w, max(x0, x1))
    y0, y1 = max(0, min(y0, y1)), min(h, max(y0, y1))
    if x1 <= x0 or y1 <= y0:
        raise ValueError("empty region %r" % text)
    return (x0, y0, x1, y1, name)


def stats(img, box):
    crop = img.crop(box[:4])
    st = ImageStat.Stat(crop)
    mean = st.mean[:3]
    std = st.stddev[:3]
    luma = 0.299 * mean[0] + 0.587 * mean[1] + 0.114 * mean[2]
    hist = crop.histogram()
    bins = []
    for c in range(3):
        h = hist[c * 256:(c + 1) * 256]
        total = float(sum(h)) or 1.0
        bins.append([sum(h[i * 32:(i + 1) * 32]) / total for i in range(8)])
    return {"mean": mean, "std": std, "luma": luma, "sat": max(mean) - min(mean), "bins": bins}


def fmt3(v, f="%6.1f"):
    return " ".join(f % x for x in v)


def report(orig, ours, regions, show_hist):
    for box in regions:
        a, b = stats(orig, box), stats(ours, box)
        ratio = [(x / y) if y > 0.5 else float("nan") for x, y in zip(a["mean"], b["mean"])]
        print("region %s (%d,%d)-(%d,%d)" % (box[4], box[0], box[1], box[2], box[3]))
        print("  O mean %s  std %s  luma %6.1f  sat %5.1f" % (fmt3(a["mean"]), fmt3(a["std"], "%5.1f"), a["luma"], a["sat"]))
        print("  U mean %s  std %s  luma %6.1f  sat %5.1f" % (fmt3(b["mean"]), fmt3(b["std"], "%5.1f"), b["luma"], b["sat"]))
        print("  O/U    %s  luma %5.3f" % (fmt3(ratio, "%6.3f"), a["luma"] / b["luma"] if b["luma"] > 0.5 else float("nan")))
        if show_hist:
            for c, name in enumerate("RGB"):
                print("  %s O %s" % (name, " ".join("%4.2f" % x for x in a["bins"][c])))
                print("  %s U %s" % (name, " ".join("%4.2f" % x for x in b["bins"][c])))


def side_by_side(orig, ours, regions, path, labels):
    w, h = orig.size
    strip = 24
    out = Image.new("RGB", (w * 2 + 8, h + 18 + strip * max(1, len(regions))), (20, 20, 20))
    out.paste(orig, (0, 18))
    out.paste(ours, (w + 8, 18))
    d = ImageDraw.Draw(out)
    d.text((4, 3), labels[0], fill=(255, 220, 90))
    d.text((w + 12, 3), labels[1], fill=(120, 220, 255))
    for i, box in enumerate(regions):
        for ox, img in ((0, orig), (w + 8, ours)):
            d.rectangle((ox + box[0], 18 + box[1], ox + box[2] - 1, 18 + box[3] - 1), outline=(255, 0, 255))
            d.text((ox + box[0] + 2, 18 + box[1] + 2), box[4], fill=(255, 0, 255))
            m = stats(img, box)["mean"]
            y = h + 18 + i * strip
            d.rectangle((ox, y + 2, ox + 60, y + strip - 2), fill=tuple(int(round(x)) for x in m))
            d.text((ox + 66, y + 6), "%s: %s" % (box[4], fmt3(m, "%.0f")), fill=(230, 230, 230))
    out.save(path)


def luma_small(img, box, scale):
    crop = img.crop(box).convert("L").filter(ImageFilter.GaussianBlur(2))
    sw, sh = max(1, (box[2] - box[0]) // scale), max(1, (box[3] - box[1]) // scale)
    return list(crop.resize((sw, sh), Image.BILINEAR).getdata())


def ncc(a, b):
    n = len(a)
    ma, mb = sum(a) / n, sum(b) / n
    sab = saa = sbb = 0.0
    for x, y in zip(a, b):
        dx, dy = x - ma, y - mb
        sab += dx * dy
        saa += dx * dx
        sbb += dy * dy
    return sab / math.sqrt(saa * sbb) if saa > 0 and sbb > 0 else 0.0


def best_of(orig, candidates, box, max_shift):
    ref = luma_small(orig, box[:4], 4)
    best = None
    for path in candidates:
        img = Image.open(path).convert("RGB")
        if img.size != orig.size:
            img = img.resize(orig.size, Image.BILINEAR)
        cand_best = None
        for dy in range(-max_shift, max_shift + 1, 4):
            b = (box[0], box[1] + dy, box[2], box[3] + dy)
            if b[1] < 0 or b[3] > img.size[1]:
                continue
            score = ncc(ref, luma_small(img, b, 4))
            if cand_best is None or score > cand_best[0]:
                cand_best = (score, dy)
        print("%-60s ncc %6.3f  shift %+d" % (path, cand_best[0], cand_best[1]))
        if best is None or cand_best[0] > best[0]:
            best = (cand_best[0], cand_best[1], path)
    print("best: %s ncc %.3f shift %+d (ours y = original y %+d)" % (best[2], best[0], best[1], best[1]))


def main(argv):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("original")
    p.add_argument("ours", nargs="?")
    p.add_argument("--region", action="append", default=[])
    p.add_argument("--side")
    p.add_argument("--diff")
    p.add_argument("--hist", action="store_true")
    p.add_argument("--labels", default="ORIGINAL,OURS")
    p.add_argument("--best-of", nargs="+")
    p.add_argument("--shift", type=int, default=0)
    a = p.parse_args(argv)
    try:
        orig = Image.open(a.original).convert("RGB")
    except OSError as e:
        print("error: %s" % e, file=sys.stderr)
        return 2
    w, h = orig.size
    try:
        regions = [parse_region(r, w, h) for r in a.region] or [(0, 0, w, h, "all")]
    except ValueError as e:
        print("error: %s" % e, file=sys.stderr)
        return 2
    if a.best_of:
        best_of(orig, a.best_of, regions[0], a.shift)
        return 0
    if not a.ours:
        print("error: need OURS.png (or --best-of)", file=sys.stderr)
        return 2
    ours = Image.open(a.ours).convert("RGB")
    if ours.size != orig.size:
        ours = ours.resize(orig.size, Image.BILINEAR)
    report(orig, ours, regions, a.hist)
    if a.diff:
        ImageChops.difference(orig, ours).point(lambda v: min(255, v * 4)).save(a.diff)
    if a.side:
        side_by_side(orig, ours, regions, a.side, (a.labels.split(",") + ["", ""])[:2])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
