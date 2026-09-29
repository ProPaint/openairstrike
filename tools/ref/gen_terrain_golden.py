#!/usr/bin/env python3
"""Generates testdata/golden/<game>/terrain_heights.json from tools/ref/hmap.py (`--game <key>`,
else $AS3D_GAME, else as3d).

For every shipped map: the sha1 of the resampled (W+1)x(H+1) raw vertex heights
(hmap.vertex_heights) and the sha1 of the generated 256x256 RGB base texture of block 0
(rows 0..31, the height-banded blend of texture1..4, docs/spec/hmap.md "Terrain
texturing"), with texel (0,0) at the top row of the decoded TGA (row 0 = top, as
as3d::Image). Hashes and metadata only; no game data is stored.

  AS3D_DATA_ROOT=/path/to/checkout python3 tools/ref/gen_terrain_golden.py
"""
import hashlib
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gamesel  # noqa: E402

gamesel.parse_game_arg()
import hmap  # noqa: E402

OUT = gamesel.golden_path("terrain_heights.json")


def base_texture_block(m, tex_dir, block):
    from PIL import Image
    texs = []
    for i in range(1, 5):
        img = Image.open(os.path.join(tex_dir, f"texture{i}.tga")).convert("RGB")
        texs.append((img.load(), img.size))
    hs = hmap.resample(m.heights, m.width, hmap.MAPTEX_ROWS, hmap.MAPTEX_SIZE, hmap.MAPTEX_SIZE,
                       src_offset=block * hmap.MAPTEX_ROWS * m.width)
    out = bytearray()
    for r in range(hmap.MAPTEX_SIZE):
        for c in range(hmap.MAPTEX_SIZE):
            t = hs[r * hmap.MAPTEX_SIZE + c] / hmap.HEIGHT_BAND
            i0, i1 = int(math.floor(t)), int(math.ceil(t))
            fr = t - i0
            a = texs[i0][0][c % texs[i0][1][0], r % texs[i0][1][1]]
            b = texs[i1][0][c % texs[i1][1][0], r % texs[i1][1][1]]
            for k in range(3):
                out.append(int(a[k] * (1 - fr) + b[k] * fr) & 0xFF)
    return bytes(out)


def main():
    levels = hmap.load_level_list()
    entries = []
    for path in hmap.list_maps():
        m = hmap.parse_file(path)
        rec = hmap.level_for_map(path, levels) or {}
        vh = hmap.vertex_heights(m)
        e = {"file": "maps/" + os.path.basename(path), "width": m.width, "height": m.height,
             "vertex_heights_sha1": hashlib.sha1(bytes(vh)).hexdigest(),
             "textures": rec.get("textures", "")}
        if rec.get("textures"):
            tex = base_texture_block(m, hmap.asset_path(rec["textures"]), 0)
            e["base_texture_block0_sha1"] = hashlib.sha1(tex).hexdigest()
        entries.append(e)
    entries.sort(key=lambda e: e["file"])
    with open(OUT, "w") as f:
        json.dump(entries, f, indent=1, sort_keys=True)
        f.write("\n")
    print(f"wrote {OUT} ({len(entries)} entries)")


if __name__ == "__main__":
    main()
