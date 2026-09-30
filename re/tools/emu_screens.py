#!/usr/bin/env python3
"""Draws the front-end screens of AirStrike 2 or Gulf Thunder in the 2D emulator (emu2d.py)
and prints what each screen queues: every quad (texture, rectangle, texel rectangle on its
atlas, colour, blend) and every text call. Used to write docs/spec/gulf/frontend.delta.md by
running the same screen in both games and comparing.

    AS3D_DATA_ROOT=<main checkout> ~/tools/re-venv/bin/python re/tools/emu_screens.py \
        --game gulf --pairs PAIRS.json --datamap DATAMAP.json main start options ...
    ... --list          the screen names

Addresses are the AirStrike 2 ones (docs/spec/as2/frontend.md); for Gulf Thunder they are
translated with the pair list of `match_symbols.py --pairs` and the data map of
`match_data.py --dump`. Sound, skin-image and music helpers are stubbed; std::list heads that
the game initialises at start-up are created empty when the code first touches them.
Prints numbers only; no code.
"""
import argparse
import csv
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu2d import Emu, fmt_text  # noqa: E402

AS2 = {
    "UI_LoadAssets": 0x42b1f0, "UI_Frame": 0x42b3c0, "UI_PushMenu": 0x42b2c0,
    "M_ShowMainMenu": 0x42bbc0, "M_MainMenuAction": 0x42b7c0,
    "frametime": 0x0049f920, "flush2d": 0x4309b0,
}
# screen -> list of steps: ("act", id) main-menu action, ("build", builder, menu record),
# ("call", function, regs, stack), ("frames", n)
SCREENS = {
    "main": [("frames", 60)],
    "start": [("act", 1), ("frames", 60)],
    "topscores": [("act", 2), ("frames", 60)],
    "options": [("act", 3), ("frames", 60)],
    "info": [("act", 4), ("frames", 60)],
    "exit": [("act", 5), ("frames", 60)],
    "credits": [("act", 7), ("frames", 60)],
    "heli": [("call", 0x4297d0, {}, (0,)), ("frames", 60)],
    "ingame": [("build", 0x42aa20, 0x21123a8), ("frames", 60)],
    "gameover": [("build", 0x428db0, 0x2111ac0), ("frames", 60)],
    "missioncomplete": [("build", 0x427db0, 0x21110a0), ("frames", 90)],
    "gamecomplete": [("build", 0x4289c0, 0x2111868), ("frames", 90)],
    "controls": [("act", 3), ("frames", 10), ("build", 0x423ac0, None), ("frames", 60)],
    "tutorial": [("call", 0x42dd20, {"eax": "TEXT"}, ()), ("frames", 60)],
    "header": [("header", 30)],
    "loading": [("loading", 0)],
}


class Screens:
    def __init__(self, game, pairs, datamap):
        self.game = game
        self.e = Emu(game)
        self.pairs = {int(k, 16): int(v["to"], 16) for k, v in json.load(open(pairs)).items()} if pairs else {}
        self.dm = {int(k, 16): int(v[0], 16) for k, v in json.load(open(datamap)).items()} if datamap else {}
        e = self.e
        # skin images of Settings.xml; resolution list (Direct3D modes); frame begin and end
        for a in (0x422cb0, 0x422e30, 0x422eb0, 0x42bda0, 0x42bfa0, 0x430e50, 0x431110):
            e.hook(self.F(a), lambda e: 0)
        here = os.path.dirname(os.path.abspath(__file__))
        rows = os.path.join(os.path.dirname(os.path.dirname(here)), "re", "symbols_as2.csv")
        for r in csv.DictReader(open(rows)):
            if r["subsystem"] == "sound" and not r["name"].startswith("BASS"):
                a = int(r["address"], 16)
                if (self.game == "as2" or a in self.pairs) and self.F(a) not in e.hooks:
                    e.hook(self.F(a), lambda e: 0)
        self.run(self.F(AS2["UI_LoadAssets"]))
        e.wf32(self.D(AS2["frametime"]), 0.02)
        # state a running game has: a current level record and player entities (zeroed)
        for ptr in (0x00543288, 0x020c5ad0, 0x020c5c34, 0x02219178):
            e.w32(self.D(ptr), e.alloc(0x800))

    def F(self, a):
        return a if self.game == "as2" else self.pairs[a]

    def D(self, a):
        return a if self.game == "as2" else self.dm[a]

    def run(self, addr, regs=None, stack=()):
        from unicorn.x86_const import UC_X86_REG_EIP
        for _ in range(30):
            try:
                return self.e.call(addr, regs=regs, stack=stack)
            except RuntimeError as ex:
                eip = self.e.reg(UC_X86_REG_EIP)
                try:
                    code = bytes(self.e.uc.mem_read(eip - 16, 16))
                except Exception:
                    raise ex
                head = None
                for k in range(10, -1, -1):
                    if code[k] == 0x8B and code[k + 1] in (0x0D, 0x15, 0x05, 0x1D, 0x35, 0x3D):
                        head = struct.unpack("<I", code[k + 2:k + 6])[0]
                        break
                if not head:
                    raise
                self.e.empty_list(head)
        raise RuntimeError("list initialisation did not converge")

    def show(self, name, text=None):
        e = self.e
        self.run(self.F(AS2["M_ShowMainMenu"]))
        mark = len(e.texts)
        for step in SCREENS[name]:
            k = step[0]
            if k == "frames":
                for _ in range(step[1]):
                    mark = len(e.texts)
                    self.run(self.F(AS2["UI_Frame"]))
            elif k == "act":
                item = e.alloc(0x80)
                e.w32(item + 4, step[1])
                self.run(self.F(AS2["M_MainMenuAction"]), stack=(item, 1))
            elif k == "build":
                r = self.run(self.F(step[1]))
                menu = self.D(step[2]) if step[2] else r
                self.run(self.F(AS2["UI_PushMenu"]), regs={"eax": menu})
            elif k == "call":
                regs = {}
                for rk, rv in step[2].items():
                    if rv == "TEXT":
                        p = e.alloc(256)
                        e.uc.mem_write(p, (text or "Tutorial text").encode() + b"\0")
                        rv = p
                    regs[rk] = rv
                self.run(self.F(step[1]), regs=regs, stack=step[3])
                for ptr in (0x02219178,):  # a preview entity the menu may have failed to create
                    if not e.r32(self.D(ptr)):
                        e.w32(self.D(ptr), e.alloc(0x800))
            elif k == "header":
                for _ in range(step[1]):
                    mark = len(e.texts)
                    self.run(self.F(0x42b610))
                self.run(self.F(AS2["flush2d"]))
            elif k == "loading":
                self.run(self.F(0x40acc0))
                mark = len(e.texts)
                self.run(self.F(0x40adf0))
                self.run(self.F(AS2["flush2d"]))
        nonempty = [f for f in e.frames if f]
        return (nonempty[-1] if nonempty else []), e.texts[mark:]


def texel(q, e):
    """Texel rectangle (x, y, w, h) of a quad on its texture, t = 1 at the top row."""
    info = e.tex_by_name.get(q.tex_name) if hasattr(e, "tex_by_name") else None
    w = h = None
    if q.tex in e.tex_names:
        w, h = e.image_size(e.tex_names[q.tex])
    if not w:
        return ""
    x0, x1 = q.s0 * w, q.s1 * w
    y0, y1 = (1 - max(q.t0, q.t1)) * h, (1 - min(q.t0, q.t1)) * h
    return "texel(%.1f, %.1f, %.1f, %.1f)" % (x0, y0, x1 - x0, y1 - y0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default="gulf")
    ap.add_argument("--pairs")
    ap.add_argument("--datamap")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--no-glyphs", action="store_true", help="omit the font quads")
    ap.add_argument("screens", nargs="*")
    a = ap.parse_args()
    if a.list:
        print(" ".join(SCREENS))
        return
    for name in a.screens:
        s = Screens(a.game, a.pairs, a.datamap)
        quads, texts = s.show(name)
        print("### %s %s: %d quads, %d texts" % (a.game, name, len(quads), len(texts)))
        for q in quads:
            if a.no_glyphs and q.tex_name.endswith("font.tga"):
                continue
            print("  %s %s" % (q, texel(q, s.e)))
        for t in texts:
            print("  " + fmt_text(t))


if __name__ == "__main__":
    main()
