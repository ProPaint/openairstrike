#!/usr/bin/env python3
"""Which builtins the scripts reachable from one mission call (the P0 set of a builtin
semantics delta).

    AS3D_DATA_ROOT=<main checkout> python3 re/tools/builtin_reach.py --game gulf --mission 1

Roots: the mission's placed objects, their items and per-placement script overrides (its map
from maps/levels.txt), the player helicopters (--heli, default player_1..player_6 that exist),
and the objects the native code creates by name (score_num, wavegun_hit). Closure: an object's
script, its attach targets and every other object named in its definition; a script's CASH
objects and every string of its STRG that names an object, a weapon (then the weapon's missile
and flash objects) or a script. The string rule over-approximates, so the result may be
slightly too large, never too small (as the AirStrike 2 delta's scan). Prints counts and the
builtin names; `--json` writes them. Standard library only; reads game data only.
"""
import argparse
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "tools", "ref"))
import hmap  # noqa: E402
import textblock  # noqa: E402


def norm(p):
    return p.replace("\\", "/").lower()


def script_sections(path):
    data = open(path, "rb").read()
    h = struct.unpack_from("<14I", data, 0)
    off, sec = 0x38, {}
    while off + 8 <= len(data):
        tag = data[off:off + 4].decode("latin1")
        ln = struct.unpack_from("<I", data, off + 4)[0]
        sec[tag] = data[off + 8:off + 8 + ln]
        off += 8 + ln

    def names(payload, count, kind):
        out, o = [], 0
        for _ in range(count):
            k = None
            if kind:
                k = payload[o]
                o += 1
            n = payload[o]
            out.append((k, payload[o + 1:o + n].decode("latin1")))
            o += 1 + n
        return out
    funcs = [n for _, n in names(sec["FUNC"], h[4], False)] if "FUNC" in sec else []
    cash = [n for _, n in names(sec["CASH"], h[2], True)] if "CASH" in sec else []
    strg = [s.decode("latin1") for s in sec.get("STRG", b"").split(b"\0") if s]
    return funcs, cash, strg


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default="gulf")
    ap.add_argument("--mission", type=int, default=1)
    ap.add_argument("--heli", nargs="*")
    ap.add_argument("--json")
    a = ap.parse_args()
    os.environ["AS3D_GAME"] = a.game
    root = os.path.join(os.environ.get("AS3D_DATA_ROOT", REPO), "assets_extracted_games", a.game) \
        if a.game != "as3d" else os.path.join(os.environ.get("AS3D_DATA_ROOT", REPO), "assets_extracted")
    objs = {}
    for fn in sorted(os.listdir(os.path.join(root, "objects"))):
        if fn.lower().endswith(".obj"):
            for b in textblock.parse_file(os.path.join(root, "objects", fn)).blocks:
                objs.setdefault(b.name, b)
    weapons = {}
    for fn in sorted(os.listdir(os.path.join(root, "weapons"))):
        if fn.lower().endswith(".wpn"):
            for b in textblock.parse_file(os.path.join(root, "weapons", fn)).blocks:
                weapons.setdefault(b.name, b)
    levels = textblock.parse_file(os.path.join(root, "maps", "levels.txt")).blocks
    lv = levels[a.mission - 1]
    mp = lv.find("map").args[0].text
    m = hmap.parse_file(os.path.join(root, *norm(mp).split("/")))
    todo_o, todo_s = set(), set()
    for p in m.placements:
        todo_o.add(m.type_name(p))
        if m.item_name(p):
            todo_o.add(m.item_name(p))
        if p.script:
            todo_s.add(norm(p.script))
    heli = a.heli if a.heli is not None else ["player_%d" % i for i in range(1, 7) if "player_%d" % i in objs]
    todo_o |= set(heli) | {"score_num", "wavegun_hit"}
    seen_o, seen_s, seen_w, missing = set(), set(), set(), set()
    builtins = {}
    lowobj = {k.lower(): k for k in objs}
    lowwpn = {k.lower(): k for k in weapons}

    def take_string(s):
        if s.lower() in lowobj:
            todo_o.add(lowobj[s.lower()])
        if s.lower() in lowwpn:
            w = lowwpn[s.lower()]
            if w not in seen_w:
                seen_w.add(w)
                for st in weapons[w].statements:
                    for t in st.args:
                        take_string(t.text)
        if norm(s).endswith(".scr"):
            todo_s.add(norm(s))
    while todo_o or todo_s:
        while todo_o:
            o = todo_o.pop()
            if o in seen_o or o not in objs:
                continue
            seen_o.add(o)
            for st in objs[o].statements:
                for t in st.args:
                    take_string(t.text)
        while todo_s:
            s = todo_s.pop()
            if s in seen_s:
                continue
            seen_s.add(s)
            path = os.path.join(root, *s.split("/"))
            if not os.path.exists(path):
                missing.add(s)
                continue
            funcs, cash, strg = script_sections(path)
            for f in funcs:
                builtins.setdefault(f, set()).add(s)
            for c in cash + strg:
                take_string(c)
    print("mission %d (%s, %s): %d objects, %d scripts, %d weapons, %d missing scripts; %d builtins"
          % (a.mission, lv.find("name").args[0].text, mp, len(seen_o), len(seen_s), len(seen_w),
             len(missing), len(builtins)))
    print(", ".join(sorted(builtins)))
    if missing:
        print("missing:", sorted(missing))
    if a.json:
        json.dump({"mission": a.mission, "objects": len(seen_o), "scripts": len(seen_s),
                   "weapons": len(seen_w), "missing": sorted(missing),
                   "builtins": sorted(builtins)}, open(a.json, "w"), indent=1)


if __name__ == "__main__":
    main()
