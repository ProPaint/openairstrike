#!/usr/bin/env python3
"""Scan a game's shipped data for everything the renderer's state depends on.

Used by the AirStrike 2 render delta (docs/spec/as2/render-pipeline.delta.md) to count
material flag combinations, particle keywords and texture formats, and to compare them
with the first game. Reads only the extracted data (gitignored); prints counts, never
asset content.

    AS3D_DATA_ROOT=/path/to/airstrike3d \
        python3 re/tools/scan_render_data.py --game as2 [--json out.json] [--section all]

Sections: objects, particles, textures, levels, shadows. `--game as3d` scans the first
game's `assets_extracted/` for comparison.
"""
import argparse
import collections
import glob
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "ref"))
import textblock as tb  # noqa: E402
import mdl as mdlmod  # noqa: E402

RFLAGS = {"RF_NOLIGHTING": 0x1, "RF_NOCULLING": 0x2, "RF_NODEPTHTEST": 0x4,
          "RF_NODEPTHWRITE": 0x8, "RF_NODLIGHT": 0x200, "RF_BANNER": 0x10000}
BLENDS = {"BLEND_ALPHA": "alpha", "BLEND_ADD": "add", "BLEND_FILTER": "filter"}


def roots(game):
    base = os.environ.get("AS3D_DATA_ROOT", os.path.join(HERE, "..", ".."))
    if game == "as3d":
        return [os.path.join(base, "assets_extracted")]
    out = [os.path.join(base, "assets_extracted_games", game)]
    loose = os.path.join(base, "third_party_local", "games", game, "data")
    if os.path.isdir(loose):
        out.append(loose)
    return out


class Files:
    """Case-insensitive file index over the data roots (Windows semantics)."""

    def __init__(self, dirs):
        self.index = {}
        for d in dirs:
            for dp, _, fns in os.walk(d):
                for fn in fns:
                    full = os.path.join(dp, fn)
                    rel = os.path.relpath(full, d).replace(os.sep, "/").lower()
                    self.index.setdefault(rel, full)

    def find(self, name):
        """The engine's lookup: the name as given, else with .tga (then .jpg for AS2)."""
        n = name.replace("\\", "/").lower().lstrip("/")
        if n in self.index:
            return self.index[n]
        stem = n.rsplit(".", 1)[0] if "." in os.path.basename(n) else n
        for ext in (".tga", ".jpg"):
            if stem + ext in self.index:
                return self.index[stem + ext]
        return None


def tga_info(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 18:
        return {"kind": "short"}
    idlen, cmtype, itype = data[0], data[1], data[2]
    cmlen, cmdepth = struct.unpack_from("<H", data, 5)[0], data[7]
    w, h = struct.unpack_from("<HH", data, 12)
    bpp, desc = data[16], data[17]
    footer = data[-18:-2] == b"TRUEVISION-XFILE"
    kind = {1: "paletted", 2: "truecolour", 3: "grey", 9: "rle-paletted",
            10: "rle-truecolour", 11: "rle-grey"}.get(itype, f"type{itype}")
    fmt = kind
    if itype in (2, 10):
        fmt = f"{bpp}-bit"
    elif itype in (1, 9):
        fmt = f"paletted({cmdepth})"
    elif itype in (3, 11):
        fmt = "8-bit grey"
    return {"kind": kind, "fmt": fmt, "w": w, "h": h, "bpp": bpp, "itype": itype,
            "top_origin": bool(desc & 0x20), "alpha_bits": desc & 0xF,
            "idlen": idlen, "footer": footer, "pow2": (w & (w - 1)) == 0 and (h & (h - 1)) == 0}


def alpha_kind(info):
    if info is None:
        return "missing"
    if info.get("fmt") == "32-bit":
        return "32-bit"
    return info.get("fmt", "?")


def parse_objects(root):
    objs = []
    for path in sorted(glob.glob(os.path.join(root, "objects", "*.obj"))):
        tf = tb.parse_file(path)
        for b in tf.blocks:
            o = {"name": b.name, "file": os.path.basename(path), "keys": collections.Counter(),
                 "type": "TYPE_MODEL", "blend": "none", "rflag": [], "sort": "SORT_OPAQUE",
                 "shadow": None, "envmode": None, "envmap": None, "skin": None, "model": None,
                 "flags": [], "skid": [], "light": False, "min": None, "max": None,
                 "frames": None}
            for s in b.statements:
                k = s.key.lower()
                a = [t.text for t in s.args]
                o["keys"][k] += 1
                if k == "type" and a:
                    o["type"] = a[0]
                elif k == "blend" and a:
                    o["blend"] = BLENDS.get(a[0], a[0])
                elif k == "rflag":
                    o["rflag"] += a
                elif k == "sort" and a:
                    o["sort"] = a[0]
                elif k == "shadow" and a:
                    o["shadow"] = a[0]
                elif k == "envmode" and a:
                    o["envmode"] = a[0]
                elif k == "envmap" and a:
                    o["envmap"] = a[0]
                elif k == "skin" and a:
                    o["skin"] = a[0]
                elif k == "model" and a:
                    o["model"] = a[0]
                elif k == "flag":
                    o["flags"] += a
                elif k == "skid_mark":
                    o["skid"].append(a)
                elif k in ("light", "light_dir"):
                    o["light"] = True
                elif k == "min":
                    o["min"] = a
                elif k == "max":
                    o["max"] = a
                elif k == "frames":
                    o["frames"] = a
            objs.append(o)
    return objs


def model_texture(files, model_name, cache):
    if model_name in cache:
        return cache[model_name]
    p = files.find(model_name)
    tex = None
    if p:
        try:
            with open(p, "rb") as f:
                m = mdlmod.parse(f.read(), p)
            tex = m.texture_path
        except Exception:  # noqa: BLE001  (empty or odd models: no texture)
            tex = None
    cache[model_name] = tex
    return tex


def scan_objects(game, files, out):
    objs = parse_objects(roots(game)[0])
    out["objects_total"] = len(objs)
    keys = collections.Counter()
    for o in objs:
        for k in o["keys"]:
            keys[k] += 1
    out["object_keys"] = dict(sorted(keys.items()))
    combos = collections.Counter()
    rflag_use = collections.Counter()
    flag_use = collections.Counter()
    shadow_use = collections.Counter()
    type_blend = collections.Counter()
    missing_skins = []
    mcache = {}
    for o in objs:
        for r in o["rflag"]:
            rflag_use[r] += 1
        for fl in o["flags"]:
            flag_use[fl] += 1
        if o["shadow"]:
            shadow_use[o["shadow"]] += 1
        tex_name = o["skin"] or (model_texture(files, o["model"], mcache) if o["model"] else None)
        info = tga_info(files.find(tex_name)) if tex_name and files.find(tex_name) else None
        if tex_name and info is None:
            missing_skins.append((o["name"], tex_name))
        skin_kind = alpha_kind(info) if tex_name else "no texture"
        if o["type"] == "TYPE_MODEL" and not o["model"]:
            skin_kind = "no model"
        rf = "+".join(sorted(set(r for r in o["rflag"] if r in RFLAGS))) or "-"
        env = o["envmode"] or ("envmap-no-mode" if o["envmap"] else "-")
        combos[(o["type"], o["blend"], rf, o["sort"], env, skin_kind)] += 1
        type_blend[(o["type"], o["blend"])] += 1
    out["object_rflag_use"] = dict(rflag_use)
    out["object_flag_use"] = dict(flag_use)
    out["object_shadow_use"] = dict(shadow_use)
    out["object_type_blend"] = {"|".join(k): v for k, v in sorted(type_blend.items())}
    out["object_state_combos"] = [list(k) + [v] for k, v in sorted(combos.items())]
    out["object_missing_textures"] = len(missing_skins)
    out["object_missing_examples"] = missing_skins[:15]
    # skid marks
    skid = [(o["name"], s) for o in objs for s in o["skid"]]
    out["skid_mark_statements"] = len(skid)
    out["skid_mark_objects"] = len({n for n, _ in skid})
    out["skid_mark_textures"] = dict(collections.Counter(s[-1].lower().replace("\\", "/")
                                                         for _, s in skid))
    out["skid_mark_args"] = dict(collections.Counter(" ".join(s[:-1]) for _, s in skid))
    marks = [o for o in objs if o["type"] == "TYPE_MARK"]
    out["mark_objects"] = [(o["name"], o["blend"], o["skin"], o["min"], o["max"]) for o in marks]
    sprites = [o for o in objs if o["type"] in ("TYPE_SPRITE", "TYPE_HSPRITE", "TYPE_VSPRITE")]
    out["sprite_objects_by_type_blend"] = dict(collections.Counter(
        (o["type"] + "|" + o["blend"] + "|" + ("+".join(sorted(o["rflag"])) or "-"))
        for o in sprites))
    out["envmap_objects"] = dict(collections.Counter(
        (o["envmode"] or "-") + "|" + (o["envmap"] or "") for o in objs if o["envmap"]))
    return objs


def scan_particles(game, files, out):
    root = roots(game)[0]
    keys = collections.Counter()
    combos = collections.Counter()
    values = collections.defaultdict(collections.Counter)
    n = 0
    for path in sorted(glob.glob(os.path.join(root, "particles", "*.ps"))):
        tf = tb.parse_file(path)
        for b in tf.blocks:
            n += 1
            d = {"blend_mode": "none", "rflag": [], "texture": None, "cols": 1, "rows": 1}
            for s in b.statements:
                k = s.key.lower()
                a = [t.text for t in s.args]
                keys[k] += 1
                if k in ("blend_mode", "coords", "draw_mode", "emit_mode", "fade_mode",
                         "anim_mode"):
                    values[k][a[0] if a else ""] += 1
                if k == "blend_mode" and a:
                    d["blend_mode"] = a[0]
                elif k == "rflag":
                    d["rflag"] += a
                elif k == "texture" and a:
                    d["texture"] = a[0]
                    if len(a) >= 3:
                        d["cols"], d["rows"] = a[1], a[2]
                elif k == "texture_set":
                    values["texture_set"][str(len(a))] += 1
                elif k == "emit_rate" and a:
                    values["emit_rate_integer"]["yes" if a[0].lstrip("-").isdigit() else "no"] += 1
            info = tga_info(files.find(d["texture"])) if d["texture"] and files.find(
                d["texture"]) else None
            grid = f'{d["cols"]}x{d["rows"]}'
            values["grid"][grid] += 1
            combos[(d["blend_mode"], "+".join(sorted(d["rflag"])) or "-", alpha_kind(info))] += 1
    out["particle_systems"] = n
    out["particle_keys"] = dict(sorted(keys.items()))
    out["particle_values"] = {k: dict(v) for k, v in values.items()}
    out["particle_state_combos"] = [list(k) + [v] for k, v in sorted(combos.items())]


def scan_textures(game, files, out):
    fmts = collections.Counter()
    npot, top, rle, nofooter, idfield, other = [], [], [], [], [], collections.Counter()
    for rel, full in sorted(files.index.items()):
        ext = rel.rsplit(".", 1)[-1]
        if ext not in ("tga",):
            if ext in ("jpg", "jpeg", "png", "bmp", "dds"):
                other[ext] += 1
            continue
        info = tga_info(full)
        fmts[(rel.split("/")[0], info.get("fmt"))] += 1
        if not info.get("pow2", True):
            npot.append((rel, info["w"], info["h"]))
        if info.get("top_origin"):
            top.append(rel)
        if info.get("itype", 0) >= 9:
            rle.append(rel)
        if not info.get("footer"):
            nofooter.append(rel)
        if info.get("idlen"):
            idfield.append(rel)
    out["texture_formats"] = {f"{d}|{f}": v for (d, f), v in sorted(fmts.items())}
    out["texture_npot"] = npot
    out["texture_top_origin"] = top
    out["texture_rle"] = rle
    out["texture_without_footer"] = len(nofooter)
    out["texture_with_id_field"] = idfield[:20]
    out["other_image_files"] = dict(other)


def scan_levels(game, out):
    root = roots(game)[0]
    tf = tb.parse_file(os.path.join(root, "maps", "levels.txt"))
    rows = []
    for b in tf.blocks:
        r = {"level": b.name}
        for s in b.statements:
            k = s.key.lower()
            a = [t.text for t in s.args]
            if k in ("fog", "sun", "water", "textures", "night"):
                r[k] = a if a else True
        rows.append(r)
    out["levels"] = rows


def scan_shadows(game, files, objs, out):
    """Projected-shadow models with vertices below z = 0 (base spec 5.2 step 4)."""
    below = 0
    total = 0
    mcache = {}
    for o in objs:
        if not o["shadow"] or not o["model"] or not o["shadow"].startswith("SHADOW_PROJECTED"):
            continue
        p = files.find(o["model"])
        if not p:
            continue
        total += 1
        key = p
        if key not in mcache:
            try:
                with open(p, "rb") as f:
                    m = mdlmod.parse(f.read(), p)
                mcache[key] = min(v[2] for v in m.vertices) if m.vertices else 0.0
            except Exception:  # noqa: BLE001
                mcache[key] = 0.0
        if mcache[key] < 0.0:
            below += 1
    out["projected_shadow_objects"] = total
    out["projected_shadow_objects_below_z0"] = below


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default="as2")
    ap.add_argument("--json")
    ap.add_argument("--section", default="all")
    ns = ap.parse_args()
    files = Files(roots(ns.game))
    out = {"game": ns.game}
    objs = []
    if ns.section in ("all", "objects", "shadows"):
        objs = scan_objects(ns.game, files, out)
    if ns.section in ("all", "shadows"):
        scan_shadows(ns.game, files, objs, out)
    if ns.section in ("all", "particles"):
        scan_particles(ns.game, files, out)
    if ns.section in ("all", "textures"):
        scan_textures(ns.game, files, out)
    if ns.section in ("all", "levels"):
        scan_levels(ns.game, out)
    text = json.dumps(out, indent=1, default=str)
    if ns.json:
        with open(ns.json, "w") as f:
            f.write(text)
    print(text)


if __name__ == "__main__":
    main()
