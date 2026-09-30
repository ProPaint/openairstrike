#!/usr/bin/env python3
"""Assembles the game selector's marquee animations for the web page (docs/web.md).

  tools/web_marquees.py FRAMES_DIR OUT_DIR

FRAMES_DIR is what `as3d_game --headless --selector-marquees` wrote (<key>_NNN.png and
index.txt, "key frames ms"); OUT_DIR gets <key>.webp, one looping animated WebP per game, and
marquees.json ({key: {"file", "width", "height"}}). These are renders of the games' own art:
the bundled build only (tools/web_build.sh never calls this for the bring-your-own site).
"""
import json
import os
import sys

from PIL import Image


def main():
    frames_dir, out_dir = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)
    meta = {}
    for line in open(os.path.join(frames_dir, "index.txt")):
        key, count, ms = line.split()
        frames = [Image.open(os.path.join(frames_dir, "%s_%03d.png" % (key, i))).convert("RGB")
                  for i in range(int(count))]
        out = os.path.join(out_dir, key + ".webp")
        frames[0].save(out, save_all=True, append_images=frames[1:], duration=int(ms), loop=0,
                       quality=82, method=4)
        meta[key] = {"file": key + ".webp", "width": frames[0].width, "height": frames[0].height}
        print("web_marquees: %s: %d frames, %d ms each, %d KB" % (key, len(frames), int(ms), os.path.getsize(out) // 1024))
    with open(os.path.join(out_dir, "marquees.json"), "w") as f:
        json.dump(meta, f, indent=1)


if __name__ == "__main__":
    main()
