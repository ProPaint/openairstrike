#!/usr/bin/env python3
"""Scripted walks through the web version in a headless browser (docs/web.md, "Tests").

    walk.py --url http://127.0.0.1:8766/ [--byo-url http://127.0.0.1:8767/] --shots DIR
            [--engine chromium|firefox] [--gl gpu|swiftshader] [--only NAME,...]

Scenarios (each in a fresh browser context, so fresh storage). The bundled site holds as3d
and as2 (tools/web_build.sh); every scenario but `choose` and `byo` forces ?game=as3d.
  choose     the start screen is the game selector (docs/spec/issues/164: the cards' marquees,
             the pulsing current card, Left/Right and Enter) offering the playable games of the
             bundled site, and downloads no game file before a choice (the marquees are the
             build-time renders, about a megabyte in all);
             AirStrike 3D chosen: only its files fetched, "Change game" on its main menu goes
             back to the start screen (the choice remembered); AirStrike 2 chosen with the
             pilot (bot=1&menus=1): only its files, mission 1 started through its own menus
             (Start Game, Next, the helicopter selection, the start dialogue) plays 30 s; then
             /persist/as3d/profile.bin is byte for byte what it was before AirStrike 2 ran and
             /persist/as2/profile.bin exists (docs/spec/issues/163).
  desktop    mouse and keyboard at 1280x720: Play, intro, main menu, Options (Show FPS),
             Top Scores, Information, Start Game, play with keys, Esc, in-game menu, Resume,
             F full screen, context loss and restore, tab hidden and shown, reload: the
             profile (settings, web key bindings) survived.
  phone      a 20:9 Android phone (touch, dpr 2.625) in landscape: Play asks for full screen
             and the landscape lock, the menus by synthesized touch, multi-touch in play (one
             finger drags while another holds the missile button), the pause button, the full
             screen toggle, leaving full screen pauses, portrait shows the rotate notice.
  iphone     an iPhone profile (Chromium with the iPhone viewport, touch and user agent): the
             layout in landscape, the rotate notice in portrait.
  complete   the bot with god mode plays mission 1 from the menus to Mission Complete, then
             Continue loads mission 2 (about 4 minutes).
  gameover   a profile with a zero high-score table; mission 2 at the hardest difficulty with
             nobody flying until Game Over, Quit, name entry with the touch keyboard, Top Scores.
  byo        the bring-your-own site: text cards until files are dropped, the owner's files through the file input, stored,
             used again after a reload, removed; files of AirStrike 2 told apart by their
             contents, stored under their own key, and started; with both games' files the
             page lists and offers both; files stored by the first version of the page (no
             game key) still found.
  migration  a version 1 profile at the old path /persist/profile.bin (Screen 4:3 and Show
             FPS set): after a reload the settings are in effect, /persist/as3d/profile.bin
             and /persist/profile.v1.bak exist and /persist/profile.bin is gone; the same
             after a second reload (the directory and the rename reached browser storage).
Screenshots go to DIR; a JSON report beside them (walk_<engine>.json). Exit code 1 on failure.
"""
import argparse
import os
import re
import struct
import sys
import time
import traceback
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from webtest import Browser, dump  # noqa: E402

DATA_ROOT = os.environ.get("AS3D_DATA_ROOT", os.path.join(os.path.dirname(__file__), "..", "..", ".."))


class Walk:
    def __init__(self, a):
        self.a = a
        self.results = {}
        self.steps = []

    def step(self, text):
        line = f"[{time.strftime('%H:%M:%S')}] {text}"
        print(line, flush=True)
        self.steps.append(line)

    def shot(self, p, name):
        path = os.path.join(self.a.shots, f"{self.a.engine}_{name}.png")
        p.shot(path)
        return path


# ------------------------------------------------------------------------------------------
# Profile helpers (docs/spec/issues/160-save-format-v2.md: version 2 has the game key, saves live
# in /persist/<key>/profile.bin; version 1 was /persist/profile.bin of the first game)
# ------------------------------------------------------------------------------------------
def parse_profile(b):
    assert b[:8] == b"AS3DPROF", "not a profile"
    version = struct.unpack_from("<I", b, 8)[0]
    size, crc = struct.unpack_from("<II", b, 12)
    key = b""
    start = 20
    if version >= 2:
        start = 21 + b[20]
        key = b[21:start]
    payload = b[start:start + size]
    assert zlib.crc32(key + payload) == crc, "profile CRC"
    chunks, o = {}, 0
    while o + 8 <= len(payload):
        tag = payload[o:o + 4].decode()
        n = struct.unpack_from("<I", payload, o + 4)[0]
        chunks[tag] = payload[o + 8:o + 8 + n]
        o += 8 + n
    settings = {}
    s = chunks.get("SETT", b"")
    if s:
        count, o = struct.unpack_from("<H", s, 0)[0], 2
        for _ in range(count):
            k = s[o + 1:o + 1 + s[o]].decode()
            o += 1 + s[o]
            settings[k] = struct.unpack_from("<i", s, o)[0]
            o += 4
    scores = []
    p = chunks.get("PROG", b"")
    if p:
        o = 1
        for _ in range(p[0]):
            name = p[o + 1:o + 1 + p[o]].decode("latin-1")
            o += 1 + p[o]
            score, rank = struct.unpack_from("<qB", p, o)
            o += 9
            scores.append((name, score, rank))
    return {"version": version, "key": key.decode(), "chunks": chunks, "settings": settings, "scores": scores}


def build_profile(version, key, chunks):
    """A profile file of the given version (1: no key) from {tag: data}."""
    payload = b""
    for tag, data in chunks.items():
        payload += tag.encode() + struct.pack("<I", len(data)) + data
    kb = key.encode() if version >= 2 else b""
    head = bytes([len(kb)]) + kb if version >= 2 else b""
    return b"AS3DPROF" + struct.pack("<III", version, len(payload), zlib.crc32(kb + payload)) + head + payload


def profile_with_scores(b, scores):
    """The profile `b` with its high-score table replaced (keeps the flags and settings)."""
    prof = parse_profile(b)
    prog = prof["chunks"]["PROG"]
    o = 1
    for _ in range(prog[0]):
        o += 1 + prog[o] + 9
    rest = prog[o:]
    newp = bytes([len(scores)])
    for name, score, rank in scores:
        nb = name.encode("latin-1")
        newp += bytes([len(nb)]) + nb + struct.pack("<qB", score, rank)
    newp += rest
    chunks = {tag: (newp if tag == "PROG" else data) for tag, data in prof["chunks"].items()}
    return build_profile(prof["version"], prof["key"], chunks)


PROFILE = "/persist/as3d/profile.bin"      # version 2, the first game
OLD_PROFILE = "/persist/profile.bin"       # version 1, before the save moved
BACKUP = "/persist/profile.v1.bak"


def read_profile(p, path=PROFILE):
    arr = p.page.evaluate("(path) => { try { return Array.from(Module.FS.readFile(path)); }"
                          " catch (e) { return null; } }", path)
    return bytes(arr) if arr else None


def fs_exists(p, path):
    return p.page.evaluate("(path) => Module.FS.analyzePath(path).exists", path)


# ------------------------------------------------------------------------------------------
# Common steps
# ------------------------------------------------------------------------------------------
def to_main_menu(w, p, by="mouse"):
    """From the start of the game (intro pages or main menu) to the main menu."""
    m = p.wait_line(r"AS3D_SCREEN name=(intro|main)\b", 90)
    # AirStrike 2 adds four comic pages to the intro (every tap makes the page 4 times faster).
    for _ in range(80):
        if any(re.search(r"AS3D_SCREEN name=main\b", t) for t in p.texts()):
            break
        box = p.page.locator("#canvas").bounding_box()
        p.tap_css(box["x"] + box["width"] / 2, box["y"] + box["height"] * 0.9, by)
        p.page.wait_for_timeout(500)
    p.wait_screen("main", 30)
    p.wait_line(r"AS3D_LEVEL_LOADED", 60)
    p.page.wait_for_timeout(800)
    return m


def wait_new_screen(p, name, mark, timeout=60):
    return p.wait_line(r"AS3D_SCREEN name=" + name + r"\b", timeout, after=mark)


def start_mission(w, p, by="mouse", difficulty_taps=0, mission_row=1):
    mk = p.mark()
    p.tap_item("main", 1, by)                       # Start Game
    wait_new_screen(p, "start", mk)
    p.page.wait_for_timeout(400)
    if mission_row > 1:
        x, y, w_, h_, _ = p.menu_items("start")[3]  # the mission list, rows of 20
        p.tap_virtual(x + 40, y + 4 + 20 * (mission_row - 1) + 5, by)
        p.page.wait_for_timeout(250)
    for _ in range(difficulty_taps):
        p.tap_item("start", 4, by)                  # the Difficulty spinner
        p.page.wait_for_timeout(250)
    mk = p.mark()
    p.tap_item("start", 2, by)                      # Start (AirStrike 2: Next)
    if p.wait_line(r"AS3D_SCREEN name=(heli|playing|hint|dialogue)\b", 60, after=mk).group(1) == "heli":
        # AirStrike 2's helicopter selection, then its Start (as2/frontend.md 3.18).
        p.page.wait_for_timeout(500)
        p.tap_item("heli", 1, by)
    p.wait_line(r"AS3D_LEVEL_LOADED mission=%d" % mission_row, 90, after=mk)
    if wait_new_screen(p, "(playing|hint|dialogue)", mk, 60).group(1) == "dialogue":
        # AirStrike 2's start dialogue holds the mission: taps complete and turn its pages.
        for _ in range(40):
            if any(re.search(r"AS3D_SCREEN name=(playing|hint)\b", t) for t in p.texts()[mk:]):
                break
            p.tap_virtual(400, 300, by)
            p.page.wait_for_timeout(700)
        wait_new_screen(p, "(playing|hint)", mk, 60)


def resize(p, size):
    """Headless Chromium really enters full screen, and then refuses viewport changes: leave
    it first (a real phone turned sideways keeps full screen; the page only sees a resize)."""
    if p.state()["fullscreen"]:
        p.page.evaluate("document.exitFullscreen()")
        p.page.wait_for_function("!document.fullscreenElement")
        p.page.wait_for_timeout(300)
    p.page.set_viewport_size(size)


def touch_layout(p):
    m = None
    for t in p.texts():
        mm = re.search(r"AS3D_LAYOUT size=(\d+)x(\d+) .*", t)
        if mm:
            m = mm
    assert m, "no AS3D_LAYOUT"
    btn = {k: tuple(int(v) for v in vals.split(",")) for k, vals in re.findall(r" (\w+)=(-?\d+,-?\d+,-?\d+(?:,-?\d+)?)", m.group(0))}
    return btn


# ------------------------------------------------------------------------------------------
# Scenarios
# ------------------------------------------------------------------------------------------
def desktop(w, b):
    p = b.page(w.a.url, "game=as3d", viewport={"width": 1280, "height": 720})
    r = {}
    try:
        p.wait_ready()
        w.shot(p, "desktop_start")
        st = p.state()
        assert not st["touch"], "desktop started in touch mode"
        w.step("desktop: Play (mouse)")
        p.page.click("#play")
        to_main_menu(w, p)
        st = p.state()
        r["canvas"] = st["canvas"]
        assert st["fullscreenRequests"] == 0, "Play asked for full screen on the desktop"
        w.shot(p, "desktop_main")

        w.step("desktop: Options, Show FPS on, Back")
        mk = p.mark()
        p.tap_item("main", 3)
        wait_new_screen(p, "options", mk)
        p.page.wait_for_timeout(400)
        items = p.menu_items("options")
        r["options_items"] = sorted(items)
        assert 20 not in items and 23 not in items, "video options offered on the web"
        p.tap_item("options", 43)   # Show FPS: Off -> On
        p.page.wait_for_timeout(300)
        w.shot(p, "desktop_options")
        mk = p.mark()
        p.tap_item("options", 1)    # Back (saves)
        wait_new_screen(p, "main", mk)
        p.wait_line(r"AS3D_WEB profile_synced", 10, after=mk)
        prof = parse_profile(read_profile(p))
        r["profile_after_options"] = {k: prof["settings"].get(k) for k in ("showFps", "key0.0.0", "key0.2.0", "key0.4.0")}
        assert prof["settings"].get("showFps") == 1, "Show FPS not saved"
        assert prof["settings"].get("key0.0.0") == 32, "fire is not Space on a fresh web profile"

        w.step("desktop: Top Scores, Information")
        mk = p.mark()
        p.tap_item("main", 2)
        wait_new_screen(p, "scores", mk)
        p.page.wait_for_timeout(500)
        w.shot(p, "desktop_scores")
        mk = p.mark()
        p.tap_item("scores", 1)
        wait_new_screen(p, "main", mk)
        mk = p.mark()
        p.tap_item("main", 4)
        wait_new_screen(p, "info", mk)
        p.page.wait_for_timeout(500)
        w.shot(p, "desktop_info")
        mk = p.mark()
        p.key("Escape")
        wait_new_screen(p, "main", mk)

        w.step("desktop: Controls screen shows the web keys")
        mk = p.mark()
        p.tap_item("main", 3)
        wait_new_screen(p, "options", mk)
        p.page.wait_for_timeout(300)
        mk = p.mark()
        p.tap_item("options", 2)
        wait_new_screen(p, "controls", mk)
        p.page.wait_for_timeout(500)
        w.shot(p, "desktop_controls")
        mk = p.mark()
        p.key("Escape")
        wait_new_screen(p, "options", mk)
        mk = p.mark()
        p.key("Escape")
        wait_new_screen(p, "main", mk)

        w.step("desktop: Start Game, play with keys")
        start_mission(w, p)
        if p.state()["screen"] == "hint":
            p.key("Enter")
        p.page.wait_for_timeout(1500)
        canvas = p.page.locator("#canvas")
        canvas.focus()
        score0 = None
        for k in range(6):
            p.page.keyboard.down("Space")
            p.page.keyboard.down("ArrowLeft" if k % 2 else "ArrowRight")
            p.page.wait_for_timeout(500)
            p.page.keyboard.up("ArrowLeft")
            p.page.keyboard.up("ArrowRight")
            p.page.keyboard.up("Space")
            if p.state()["screen"] == "hint":
                p.key("Enter")
        w.shot(p, "desktop_play")
        w.step("desktop: F asks for full screen, the layout follows a screen-sized viewport")
        n0 = p.state()["fullscreenRequests"]
        p.key("f")
        p.page.wait_for_timeout(500)
        st = p.state()
        r["fullscreen_after_f"] = st["fullscreen"]
        assert st["fullscreenRequests"] == n0 + 1, "F did not ask for full screen"
        if st["fullscreen"]:
            r["canvas_fullscreen"] = st["canvas"]
            w.shot(p, "desktop_fullscreen")
            mk = p.mark()
            p.page.evaluate("document.exitFullscreen()")
            p.wait_line(r"AS3D_WEB fullscreen=0", 5, after=mk)
            p.wait_line(r"AS3D_SCREEN name=ingame", 5, after=mk)
            r["fullscreen_exit_paused"] = True
            p.page.wait_for_timeout(500)
            mk = p.mark()
            p.tap_item("ingame", 1)
            wait_new_screen(p, "playing", mk)
        p.page.set_viewport_size({"width": 1920, "height": 1080})
        p.page.wait_for_timeout(1200)
        st = p.state()
        r["canvas_1080"] = st["canvas"]
        assert st["canvas"]["h"] == 1080 and st["canvas"]["w"] == 1920, st["canvas"]
        p.wait_line(r"AS3D_VIEW scale=1\.8000", 5)
        w.shot(p, "desktop_play_1080")
        p.page.set_viewport_size({"width": 1280, "height": 720})
        p.page.wait_for_timeout(600)

        w.step("desktop: Esc, in-game menu, Resume")
        mk = p.mark()
        p.key("Escape")
        wait_new_screen(p, "ingame", mk)
        p.page.wait_for_timeout(400)
        w.shot(p, "desktop_ingame")
        mk = p.mark()
        p.tap_item("ingame", 1)
        wait_new_screen(p, "playing", mk)

        w.step("desktop: WebGL context loss and restore")
        mk = p.mark()
        p.page.evaluate("""(() => {
            const gl = document.getElementById('canvas').getContext('webgl2');
            const ext = gl.getExtension('WEBGL_lose_context');
            ext.loseContext();
            setTimeout(() => ext.restoreContext(), 1000);
        })()""")
        p.wait_line(r"AS3D_WEB context_lost", 5, after=mk)
        p.wait_line(r"AS3D_SCREEN name=ingame", 5, after=mk)
        p.wait_line(r"AS3D_GL_REBUILD reason=context_restored ms=(\d+)", 30, after=mk)
        p.page.wait_for_timeout(1500)
        r["gl_rebuild"] = [t for t in p.texts()[mk:] if "AS3D_GL_REBUILD" in t]
        w.shot(p, "desktop_after_context_restore")
        mk = p.mark()
        p.tap_item("ingame", 1)
        wait_new_screen(p, "playing", mk)
        p.page.wait_for_timeout(1500)
        w.shot(p, "desktop_play_after_restore")

        w.step("desktop: tab hidden and shown (visibilitychange)")
        mk = p.mark()
        p.page.evaluate("""(() => {
            Object.defineProperty(document, 'visibilityState', { configurable: true, get: () => 'hidden' });
            document.dispatchEvent(new Event('visibilitychange'));
        })()""")
        p.wait_line(r"AS3D_BACKGROUND", 5, after=mk)
        p.wait_line(r"AS3D_WEB profile_synced", 10, after=mk)
        audio = p.page.evaluate("(Module.SDL2 && Module.SDL2.audioContext) ? Module.SDL2.audioContext.state : 'none'")
        p.page.evaluate("""(() => {
            Object.defineProperty(document, 'visibilityState', { configurable: true, get: () => 'visible' });
            document.dispatchEvent(new Event('visibilitychange'));
        })()""")
        p.wait_line(r"AS3D_FOREGROUND", 5, after=mk)
        p.page.wait_for_timeout(800)
        r["audio_when_hidden"] = audio
        r["screen_after_hidden"] = p.state()["screen"]
        assert r["screen_after_hidden"] == "ingame", "the game did not come back paused"

        w.step("desktop: reload, the profile is still there")
        p.reload()
        p.wait_ready()
        prof = parse_profile(read_profile(p))
        assert prof["version"] == 2 and prof["key"] == "as3d", "the web profile is not version 2 / as3d"
        assert not fs_exists(p, OLD_PROFILE), "a profile.bin appeared at the old location"
        r["profile_after_reload"] = {k: prof["settings"].get(k) for k in ("showFps", "key0.0.0")}
        assert prof["settings"].get("showFps") == 1, "Show FPS lost after reload"
        r["errors"] = p.errors()
    finally:
        p.close()
    return r


def phone(w, b, device="Pixel 7"):
    dev = b.device(device)
    # A 20:9 phone in landscape.
    vp = {"width": 915, "height": 412}
    ctx = {k: v for k, v in dev.items() if k not in ("viewport", "screen", "default_browser_type")}
    ctx.update(viewport=vp, screen={"width": 915, "height": 412}, device_scale_factor=2.625, is_mobile=True,
               has_touch=True)
    p = b.page(w.a.url, "game=as3d", **ctx)
    r = {"device": device, "viewport": vp}
    try:
        p.wait_ready()
        st = p.state()
        assert st["touch"], "a touch phone did not start in touch mode"
        w.shot(p, "phone_start")
        w.step("phone: tap Play (touch): full screen and the landscape lock are requested")
        p.page.tap("#play")
        p.page.wait_for_timeout(800)
        st = p.state()
        r["fullscreen_requests"] = st["fullscreenRequests"]
        r["fullscreen"] = st["fullscreen"]
        r["orientation_locks"] = st["orientationLocks"]
        r["canvas"] = st["canvas"]
        assert st["fullscreenRequests"] == 1, "Play did not ask for full screen"
        assert st["canvas"]["h"] == 1080, st["canvas"]  # 412 x 2.625 = 1081.5 lines, capped at 1080
        to_main_menu(w, p, by="touch")
        w.shot(p, "phone_main")
        fs = p.page.locator("#fs-toggle")
        assert fs.is_visible(), "no full screen toggle in touch mode"

        w.step("phone: Start Game by touch")
        start_mission(w, p, by="touch")
        p.page.wait_for_timeout(1200)
        if p.state()["screen"] == "hint":
            p.tap_item("hint", 1, "touch")
            p.page.wait_for_timeout(500)
        L = touch_layout(p)
        r["layout"] = L
        pb = fs.bounding_box()
        px, py = p.fb_to_css(L["pause"][0], L["pause"][1])
        r["fs_button"] = pb
        assert abs((pb["y"] + pb["height"] / 2) - py) < 4, "full screen toggle not level with the pause button"

        w.step("phone: multi-touch: one finger drags, another holds the missile button")
        fx0, fy0 = p.fb_to_css(L["field"][0] + (L["field"][2] - L["field"][0]) * 0.5, L["field"][3] * 0.7)
        mx, my = p.fb_to_css(L["missile"][0], L["missile"][1])
        mk = p.mark()
        p.touch("touchStart", [(fx0, fy0, 1)])
        for k in range(8):
            p.touch("touchMove", [(fx0 - 6 * k, fy0 - 3 * k, 1)])
            p.page.wait_for_timeout(30)
        p.touch("touchStart", [(fx0 - 42, fy0 - 21, 1), (mx, my, 2)])
        for k in range(8):
            p.touch("touchMove", [(fx0 - 42 + 8 * k, fy0 - 21, 1), (mx, my, 2)])
            p.page.wait_for_timeout(30)
        w.shot(p, "phone_multitouch")
        p.touch("touchEnd", [(fx0 + 14, fy0 - 21, 1)])
        p.page.wait_for_timeout(100)
        p.touch("touchEnd", [])
        p.page.wait_for_timeout(300)
        touches = [t for t in p.texts()[mk:] if "AS3D_TOUCH" in t]
        r["touch_lines"] = touches
        downs = [t for t in touches if "down" in t]
        assert any("on=missile" in t for t in downs), "the second finger did not reach the missile button"
        ids = {re.search(r"id=(\d+)", t).group(1) for t in downs}
        assert len(ids) >= 2, "only one finger seen"

        w.step("phone: pause button, in-game menu, Resume by touch")
        mk = p.mark()
        p.tap_css(px, py, "touch")
        wait_new_screen(p, "ingame", mk)
        w.shot(p, "phone_ingame")
        mk = p.mark()
        p.tap_item("ingame", 1, "touch")
        wait_new_screen(p, "playing", mk)

        w.step("phone: the full screen toggle; leaving full screen pauses")
        n0 = p.state()["fullscreenRequests"]
        mk = p.mark()
        if p.state()["fullscreen"]:
            fs.tap()  # leaves
            p.wait_line(r"AS3D_WEB fullscreen=0", 5, after=mk)
            p.wait_line(r"AS3D_SCREEN name=ingame", 5, after=mk)
            r["left_fullscreen_paused"] = True
            p.page.wait_for_timeout(300)
            fs.tap()  # enters again
        else:
            fs.tap()
        p.page.wait_for_timeout(500)
        r["fullscreen_requests_after_toggle"] = p.state()["fullscreenRequests"]
        assert p.state()["fullscreenRequests"] == n0 + 1, "the toggle did not ask for full screen"

        w.step("phone: portrait shows the rotate notice and pauses")
        resize(p, vp)  # out of full screen first (headless limitation), which pauses
        p.page.wait_for_timeout(500)
        if p.state()["screen"] == "ingame":
            mk = p.mark()
            p.tap_item("ingame", 1, "touch")
            wait_new_screen(p, "playing", mk)
        mk = p.mark()
        p.page.set_viewport_size({"width": 412, "height": 915})
        p.page.wait_for_timeout(1200)
        assert p.page.locator("#rotate").is_visible(), "no rotate notice in portrait"
        p.wait_line(r"AS3D_SCREEN name=ingame", 5, after=mk)
        w.shot(p, "phone_portrait")
        p.page.set_viewport_size(vp)
        p.page.wait_for_timeout(1200)
        assert not p.page.locator("#rotate").is_visible(), "rotate notice stays in landscape"
        w.shot(p, "phone_back_to_landscape")
        r["errors"] = p.errors()
    finally:
        p.close()
    return r


def iphone(w, b):
    dev = b.device("iPhone 13 landscape")
    ctx = {k: v for k, v in dev.items() if k != "default_browser_type"}
    p = b.page(w.a.url, "game=as3d", **ctx)
    r = {"device": "iPhone 13 landscape", "viewport": dev["viewport"]}
    try:
        p.wait_ready()
        assert p.state()["touch"]
        p.page.tap("#play")
        to_main_menu(w, p, by="touch")
        r["canvas"] = p.state()["canvas"]
        w.shot(p, "iphone_main")
        start_mission(w, p, by="touch")
        p.page.wait_for_timeout(1500)
        w.shot(p, "iphone_play")
        vp = dev["viewport"]
        resize(p, {"width": vp["height"], "height": vp["width"]})
        p.page.wait_for_timeout(1000)
        assert p.page.locator("#rotate").is_visible()
        w.shot(p, "iphone_portrait")
        r["errors"] = p.errors()
    finally:
        p.close()
    return r


def complete(w, b):
    p = b.page(w.a.url, "game=as3d&bot=1&menus=1&god=1", viewport={"width": 1280, "height": 720})
    r = {}
    try:
        p.wait_ready()
        p.page.click("#play")
        to_main_menu(w, p)
        w.step("complete: Start Game, the bot plays mission 1 with god mode")
        start_mission(w, p)
        t0 = time.time()
        p.wait_line(r"AS3D_SCREEN name=complete", 420)
        r["mission_seconds"] = round(time.time() - t0)
        p.page.wait_for_timeout(2500)
        w.shot(p, "mission_complete")
        w.step("complete: Continue to mission 2")
        mk = p.mark()
        p.tap_item("complete", 3)
        p.wait_line(r"AS3D_LEVEL_LOADED mission=2", 90, after=mk)
        wait_new_screen(p, "(playing|hint)", mk)
        p.page.wait_for_timeout(2000)
        w.shot(p, "mission2")
        r["load_lines"] = [t for t in p.texts() if "AS3D_LEVEL_LOAD" in t]
        r["errors"] = p.errors()
    finally:
        p.close()
    return r


def gameover(w, b):
    # Touch mode, so the name is typed on the front end's own keyboard.
    p = b.page(w.a.url, "game=as3d&touch=1", viewport={"width": 1280, "height": 720}, has_touch=True)
    r = {}
    try:
        p.wait_ready()
        p.page.click("#play")
        to_main_menu(w, p, by="touch")
        w.step("gameover: a low high-score table in the profile")
        mk = p.mark()
        p.tap_item("main", 3, "touch")
        wait_new_screen(p, "options", mk)
        mk = p.mark()
        p.tap_item("options", 1, "touch")   # Back writes the profile
        p.wait_line(r"AS3D_WEB profile_synced", 10, after=mk)
        prof = read_profile(p)
        low = [("Low %d" % i, 0, 0) for i in range(15)]
        data = profile_with_scores(prof, low)
        # Reloading hides the page, and the game saves its own profile then; that save can win
        # against ours in browser storage (a race of the test's, not of the game): try again.
        for attempt in range(4):
            p.page.evaluate("(bytes) => { Module.FS.writeFile('/persist/as3d/profile.bin', new Uint8Array(bytes));"
                            " return new Promise((ok) => Module.FS.syncfs(false, ok)); }", list(data))
            p.reload()
            p.wait_ready()
            got = parse_profile(read_profile(p))
            if got["scores"] and got["scores"][0][0] == "Low 0":
                break
            r["profile_race_retries"] = attempt + 1
        assert got["scores"] and got["scores"][0][0] == "Low 0", got["scores"][:3]
        p.page.click("#play")
        to_main_menu(w, p, by="touch")
        w.step("gameover: mission 2 at the hardest difficulty, nobody flying")
        start_mission(w, p, by="touch", difficulty_taps=2, mission_row=2)
        t0 = time.time()
        try:
            p.wait_line(r"AS3D_SCREEN name=gameover", 600)
        finally:
            r["screens"] = [t for t in p.texts() if "AS3D_SCREEN" in t or "AS3D_PAUSED" in t or "AS3D_WEB" in t][-30:]
            w.shot(p, "gameover_wait_end")
        r["seconds_to_game_over"] = round(time.time() - t0)
        p.page.wait_for_timeout(2600)   # the buttons appear after 2 s
        w.shot(p, "game_over")
        mk = p.mark()
        # Quit (banks, then the high-score check); the buttons show up after 2 s, after the
        # screen's AS3D_MENU line, so by the position of engine/src/ui/screens_game.cpp.
        p.tap_virtual(542 + 64, 450 + 32, "touch")
        wait_new_screen(p, "name", mk, 30)
        p.page.wait_for_timeout(600)
        w.step("gameover: name entry with the touch keyboard")
        for ch in "WEB":
            p.tap_item("name", 200 + ord(ch), "touch")
            p.page.wait_for_timeout(150)
        w.shot(p, "name_entry")
        mk = p.mark()
        p.tap_item("name", 1, "touch")   # OK
        wait_new_screen(p, "scores", mk)
        p.wait_line(r"AS3D_WEB profile_synced", 10, after=mk)
        p.page.wait_for_timeout(600)
        w.shot(p, "high_scores")
        scores = parse_profile(read_profile(p))["scores"]
        r["top_scores"] = scores[:3]
        assert scores[0][0] == "WEB", scores[:3]
        r["errors"] = p.errors()
    finally:
        p.close()
    return r


IDB_KEYS = """() => new Promise((ok, fail) => {
    const r = indexedDB.open('as3d-game-files');
    r.onsuccess = () => {
        const q = r.result.transaction('files').objectStore('files').getAllKeys();
        q.onsuccess = () => { r.result.close(); ok(q.result); };
    };
    r.onerror = () => fail(r.error);
})"""

# Renames every stored "as3d/<name>" to "<name>", as the first version of the page stored them.
IDB_MAKE_LEGACY = """() => new Promise((ok, fail) => {
    const r = indexedDB.open('as3d-game-files');
    r.onsuccess = () => {
        const t = r.result.transaction('files', 'readwrite');
        const s = t.objectStore('files');
        const c = s.openCursor();
        c.onsuccess = () => {
            const cur = c.result;
            if (!cur) return;
            if (String(cur.key).startsWith('as3d/')) { s.put(cur.value, String(cur.key).slice(5)); cur.delete(); }
            cur.continue();
        };
        t.oncomplete = () => { r.result.close(); ok(); };
        t.onerror = () => fail(t.error);
    };
})"""


def byo(w, b):
    orig = os.path.join(DATA_ROOT, "third_party_local", "original")
    files = [os.path.join(orig, "data", n) for n in ("pak0.apk", "pak1.apk", "pak2.apk", "Settings.xml")]
    files += [os.path.join(orig, "data", "gfx", "logo2s.tga"), os.path.join(orig, "AirStrike3D.exe")]
    p = b.page(w.a.byo_url, "", viewport={"width": 1280, "height": 720})
    r = {}
    try:
        p.page.wait_for_selector("#files:not([hidden])", timeout=60000)
        assert p.page.locator("#play").is_disabled()
        w.step("byo: until files are dropped the cards are text, none can be played, none shows game art")
        cards = p.page.evaluate("Array.from(document.querySelectorAll('#files-cards button')).map((b) => [b.dataset.game, b.disabled, b.querySelector('.stage').className, b.querySelector('.info').textContent, b.querySelectorAll('img, canvas').length])")
        r["byo_text_cards"] = cards
        assert len(cards) >= 2 and cards[0][0] == "as3d" and cards[1][0] == "as2", cards
        assert all(c[1] and c[2] == "stage text" and c[3] == "Files needed" and c[4] == 0 for c in cards), cards
        assert p.page.evaluate("document.querySelectorAll('img[src*=marquee]').length") == 0
        w.shot(p, "byo_picker")
        w.step("byo: a wrong file is refused")
        p.page.set_input_files("#pick-files", [{"name": "pak1.apk", "mimeType": "application/octet-stream",
                                                "buffer": b"\0" * 1000}])
        p.page.wait_for_function("document.getElementById('files-status').textContent.includes('not the file of any game')", timeout=20000)
        r["wrong_file_message"] = p.page.locator("#files-status").text_content()
        w.step("byo: the owner's files through the file input")
        t0 = time.time()
        p.page.set_input_files("#pick-files", files)
        p.wait_ready(180)
        r["check_and_store_seconds"] = round(time.time() - t0, 1)
        r["stored_text"] = p.page.locator("#stored-text").text_content()
        r["data_line"] = [t for t in p.texts() if "AS3D_WEB data=" in t]
        assert "texts_v170.txt" in r["data_line"][0], "the exe's texts were not used"
        assert "game=as3d" in r["data_line"][0], r["data_line"]
        p.page.click("#play")
        to_main_menu(w, p)
        w.shot(p, "byo_main")
        mk = p.mark()
        p.tap_item("main", 4)
        wait_new_screen(p, "info", mk)
        p.page.wait_for_timeout(500)
        w.shot(p, "byo_info")   # the Information pages from the exe's texts
        w.step("byo: reload: the stored files are used without asking")
        p.reload()
        p.wait_ready(120)
        assert p.page.locator("#files").is_hidden()
        assert p.page.locator("#stored").is_visible()
        keys = p.page.evaluate(IDB_KEYS)
        r["stored_keys"] = keys
        assert keys and all(k.startswith("as3d/") for k in keys), keys
        assert "as3d/texts_v170.txt" in keys and "as3d/pak0.apk" in keys, keys
        w.step("byo: files stored by the first version of the page (no game key) are still found")
        p.page.evaluate(IDB_MAKE_LEGACY)
        assert "pak0.apk" in p.page.evaluate(IDB_KEYS)
        p.reload()
        p.wait_ready(120)
        assert p.page.locator("#files").is_hidden(), "the old-style stored files were not found"
        keys = p.page.evaluate(IDB_KEYS)
        assert keys and all(k.startswith("as3d/") for k in keys), "the old-style keys were not renamed: %s" % keys
        w.step("byo: remove them")
        p.page.once("dialog", lambda d: d.accept())
        p.page.click("#forget")
        p.page.wait_for_selector("#files:not([hidden])", timeout=60000)
        r["picker_after_remove"] = True
        assert p.page.evaluate(IDB_KEYS) == [], "storage not empty after removing"
        w.step("byo: the files of AirStrike 2 are told apart by their contents, kept under their own key, and start it")
        as2 = os.path.join(DATA_ROOT, "third_party_local", "games", "as2")
        if os.path.isdir(as2):
            as2_files = [os.path.join(as2, "data", n) for n in ("pak0.apk", "pak1.apk", "pak2.apk", "Settings.xml")] \
                + [os.path.join(as2, "AirStrike3D II.exe")]
            p.page.set_input_files("#pick-files", as2_files)
            p.wait_ready(180)  # the one game with all its files: no choice to make
            data = [t for t in p.texts() if "AS3D_WEB data=" in t]
            assert data and "game=as2" in data[-1], data
            keys = p.page.evaluate(IDB_KEYS)
            r["as2_keys"] = keys
            assert "as2/pak0.apk" in keys and all(k.startswith("as2/") for k in keys), keys
            # The texts are read from the executable (tools/exe_texts/as2.json) and kept.
            assert "as2/texts_as2.txt" in keys, "the texts of AirStrike 2's executable were not kept"
            w.step("byo: AirStrike 3D's files added (?game=as3d asks for them): both kept")
            p.goto("game=as3d")
            p.page.wait_for_selector("#files:not([hidden])", timeout=60000)
            p.page.set_input_files("#pick-files", files)
            p.wait_ready(180)
            w.step("byo: without ?game= the page lists the games whose files it holds and offers both")
            p.goto("")
            p.page.wait_for_selector("#games:not([hidden])", timeout=60000)
            offered = p.page.evaluate("Array.from(document.querySelectorAll('#game-list button')).map((b) => b.dataset.game)")
            r["byo_offered"] = offered
            assert offered[:2] == ["as3d", "as2"] and "gulf" not in offered, offered
            w.step("byo: the stored files draw the marquees in the browser: AirStrike 2's logo with its emblem, animated; the 3D banner stays text")
            p.page.wait_for_function("document.querySelector('#game-list button[data-game=as2]').dataset.marquee === 'canvas'", timeout=30000)
            assert p.page.evaluate("document.querySelector('#game-list button[data-game=as3d]').dataset.marquee") is None
            assert "stage text" == p.page.get_attribute("#game-list button[data-game=as3d] .stage", "class")
            grab = """() => { const c = document.querySelector('#game-list button[data-game=as2] canvas');
                const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data; let lit = 0, sum = 0;
                for (let i = 0; i < d.length; i += 4) { const v = d[i] + d[i + 1] + d[i + 2]; if (v > 90) lit++; sum += v; }
                return [lit, sum]; }"""
            a1 = p.page.evaluate(grab)
            p.page.wait_for_timeout(1500)
            a2 = p.page.evaluate(grab)
            r["byo_marquee"] = [a1, a2]
            assert a1[0] > 1500, "AirStrike 2's logo is not drawn: %s" % a1
            assert a1[1] != a2[1], "the byo marquee does not move"
            stored = p.page.locator("#stored-text").text_content()
            assert "AirStrike 3D" in stored and "AirStrike 2" in stored, stored
            w.shot(p, "byo_chooser")
            mk = p.mark()
            p.page.click("#game-list button[data-game=as2]")
            p.wait_ready(180)
            p.wait_line(r"AS3D_WEB data=.* game=as2", 60, after=mk)
            p.goto("")
            p.page.wait_for_selector("#games:not([hidden])", timeout=60000)
            p.page.once("dialog", lambda d: d.accept())
            p.page.click("#forget")
            p.page.wait_for_selector("#files:not([hidden])", timeout=60000)
        else:
            r["as2"] = "skipped: no AirStrike 2 data"
        r["errors"] = p.errors()
    finally:
        p.close()
    return r


def profile_paths(p):
    return {"old": fs_exists(p, OLD_PROFILE), "new": fs_exists(p, PROFILE), "bak": fs_exists(p, BACKUP),
            "dir": fs_exists(p, "/persist/as3d")}


def migration(w, b):
    p = b.page(w.a.url, "game=as3d", viewport={"width": 1280, "height": 720})
    r = {}
    try:
        p.wait_ready()
        w.step("migration: the game writes a profile with Show FPS on and Screen 4:3")
        p.page.click("#play")
        to_main_menu(w, p)
        mk = p.mark()
        p.tap_item("main", 3)
        wait_new_screen(p, "options", mk)
        p.page.wait_for_timeout(400)
        p.tap_item("options", 43)   # Show FPS: Off -> On
        p.page.wait_for_timeout(300)
        mk = p.mark()
        p.tap_virtual(440, 168)     # Screen: Wide -> 4:3 (the row at y 160)
        p.wait_line(r"AS3D_LAYOUT .*screen=4x3", 10, after=mk)
        mk = p.mark()
        p.tap_item("options", 1)    # Back (saves)
        wait_new_screen(p, "main", mk)
        p.wait_line(r"AS3D_WEB profile_synced", 10, after=mk)
        v2 = parse_profile(read_profile(p))
        assert v2["version"] == 2 and v2["key"] == "as3d"
        settings = v2["settings"]
        assert settings.get("showFps") == 1
        r["settings"] = settings

        w.step("migration: put it back as the version 1 file of the old location")
        v1 = build_profile(1, "", v2["chunks"])
        assert parse_profile(v1)["settings"] == settings
        p.page.evaluate("""(bytes) => {
            Module.FS.unlink('/persist/as3d/profile.bin');
            Module.FS.rmdir('/persist/as3d');
            Module.FS.writeFile('/persist/profile.bin', new Uint8Array(bytes));
            return new Promise((ok) => Module.FS.syncfs(false, ok)); }""", list(v1))
        p.reload()
        p.wait_ready()
        st = profile_paths(p)
        r["before_migration"] = st
        assert st == {"old": True, "new": False, "bak": False, "dir": False}, st

        def check_migrated(when):
            st = profile_paths(p)
            r["state_" + when] = st
            assert st == {"old": False, "new": True, "bak": True, "dir": True}, (when, st)
            assert read_profile(p, BACKUP) == v1, when + ": profile.v1.bak is not the old file"
            now = parse_profile(read_profile(p))
            assert now["version"] == 2 and now["key"] == "as3d", (when, now["version"], now["key"])
            assert now["settings"] == settings, (when, "settings changed")

        w.step("migration: start: the old settings are in effect, the file moved")
        mk = p.mark()
        p.page.click("#play")
        to_main_menu(w, p)
        p.wait_line(r"AS3D_LAYOUT .*screen=4x3", 10, after=mk)
        p.wait_line(r"AS3D_WEB profile_synced", 10, after=mk)
        assert not [t for t in p.texts() if "using the defaults" in t], "the old profile was not used"
        check_migrated("after_start")
        for n in (1, 2):
            w.step("migration: reload %d: what the migration did is in browser storage" % n)
            p.reload()
            p.wait_ready()
            check_migrated("after_reload_%d" % n)
            mk = p.mark()
            p.page.click("#play")
            to_main_menu(w, p)
            p.wait_line(r"AS3D_LAYOUT .*screen=4x3", 10, after=mk)
            assert not [t for t in p.texts() if "using the defaults" in t]
            check_migrated("running_after_reload_%d" % n)
        r["errors"] = p.errors()
    finally:
        p.close()
    return r


AS2_PROFILE = "/persist/as2/profile.bin"


def data_requests(p):
    """The game data files the page fetched so far: ['as3d/pak0.apk', ...]."""
    return sorted(set(re.sub(r"^.*/data/", "", u).split("?")[0] for u in p.requests
                      if "/data/" in u and "games.txt" not in u))


# The playable games of the bundled site, in the order of data/games.txt.
PLAYABLE_BUNDLED = """async () => {
    const keys = (await (await fetch('data/games.txt')).text()).split(/\\s+/).filter(Boolean);
    const kn = (await (await fetch('known_files.json')).json()).games;
    return keys.filter((k) => kn[k] && kn[k].playable);
}"""


def choose(w, b):
    """The start screen offers both games of the bundled build; only the chosen game's files
    are downloaded; "Change game" comes back to it; AirStrike 2's mission 1 plays under the
    pilot for 30 s; the first game's save is untouched by it (docs/spec/issues/163)."""
    p = b.page(w.a.url, None, viewport={"width": 1280, "height": 720})
    p.requests = []
    p.page.on("request", lambda req: p.requests.append(req.url))
    p.goto("")
    r = {}
    try:
        w.step("choose: the start screen offers both games, nothing downloaded yet")
        p.page.wait_for_selector("#games:not([hidden])", timeout=60000)
        p.page.wait_for_timeout(1500)
        offered = p.page.evaluate("Array.from(document.querySelectorAll('#game-list button')).map((b) => b.dataset.game)")
        r["offered"] = offered
        expected = p.page.evaluate(PLAYABLE_BUNDLED)
        assert offered == expected and len(offered) >= 2 and offered[:2] == ["as3d", "as2"], (offered, expected)
        assert not p.state()["ready"], "the engine started before a game was chosen"
        assert data_requests(p) == [], data_requests(p)
        assert p.page.locator("#play-row").is_hidden()
        w.step("choose: every card shows its game's own marquee (the build-time render), small to fetch")
        p.page.wait_for_function("Array.from(document.querySelectorAll('#game-list button')).every((b) => b.dataset.marquee === 'img')", timeout=30000)
        marquees = sorted(set(re.sub(r"\?.*$", "", re.sub(r"^.*/marquee/", "", u)) for u in p.requests if "/marquee/" in u))
        r["marquee_requests"] = marquees
        assert marquees == sorted(["marquees.json"] + [g + ".webp" for g in offered]), marquees
        sizes = p.page.evaluate("Array.from(document.querySelectorAll('.stage img')).map((i) => [i.naturalWidth, i.naturalHeight])")
        assert all(s == [352, 162] for s in sizes), sizes
        w.step("choose: the current card pulses and moves with the arrows; Enter would play it")
        cur = lambda: p.page.evaluate("Array.from(document.querySelectorAll('#game-list button')).findIndex((b) => b.classList.contains('current'))")
        assert cur() == 0, cur()
        shadows = set()
        for _ in range(8):
            shadows.add(p.page.evaluate("getComputedStyle(document.querySelector('#game-list button.current')).boxShadow"))
            p.page.wait_for_timeout(130)
        assert len(shadows) >= 3, "the current card does not pulse: %s" % shadows
        p.page.keyboard.press("ArrowRight")
        assert cur() == 1, cur()
        p.page.keyboard.press("ArrowLeft")
        assert cur() == 0, cur()
        p.page.keyboard.press("ArrowLeft")
        assert cur() == 0, "Left went past the first card"
        assert data_requests(p) == [], "a key started a game's download"
        w.shot(p, "choose_start")

        w.step("choose: AirStrike 3D; only its files are fetched; its main menu has Change game")
        p.page.click("#game-list button[data-game=as3d]")
        p.wait_ready(180)
        to_main_menu(w, p)
        got = data_requests(p)
        r["as3d_requests"] = got
        assert got and all(g.startswith("as3d/") for g in got), got
        items = p.menu_items("main")
        assert 60 in items, "no Change game on the first game's main menu: %s" % sorted(items)
        w.shot(p, "choose_as3d_main")

        w.step("choose: Change game (saves): back to the start screen, AirStrike 3D marked as the last one")
        mk = p.mark()
        p.tap_item("main", 60)
        p.wait_line(r"AS3D_WEB change_game", 30, after=mk)
        p.wait_line(r"AS3D_WEB chooser games=", 60, after=mk)
        p.page.wait_for_selector("#games:not([hidden])", timeout=60000)
        assert "last" in (p.page.get_attribute("#game-list button[data-game=as3d]", "class") or "")
        w.shot(p, "choose_again")

        w.step("choose: AirStrike 2 with the pilot (bot=1&menus=1): only its files; mission 1 plays 30 s")
        p.requests.clear()
        p.lines.clear()  # the lines of the first game's load would satisfy the waits below
        p.goto("bot=1&menus=1")
        p.page.wait_for_selector("#games:not([hidden])", timeout=60000)
        p.page.click("#game-list button[data-game=as2]")
        p.wait_ready(180)
        # The first game's save as browser storage has it, before AirStrike 2 does anything.
        before = read_profile(p)
        assert before, "the first game's save is not in browser storage"
        assert parse_profile(before)["key"] == "as3d"
        r["as3d_save_bytes"] = len(before)
        to_main_menu(w, p)
        got = data_requests(p)
        r["as2_requests"] = got
        assert got and all(g.startswith("as2/") for g in got), got
        assert 60 in p.menu_items("main"), "no Change game on AirStrike 2's main menu"
        w.shot(p, "choose_as2_main")
        start_mission(w, p)
        mk = p.mark()
        t0 = time.time()
        while time.time() - t0 < 30:
            p.page.wait_for_timeout(2000)
        frames = [int(m.group(1)) for t in p.texts()[mk:] for m in [re.search(r"AS3D_GAME_FRAME n=(\d+) mission=1 ", t)] if m]
        r["as2_frame_markers"] = frames
        assert len(set(frames)) >= 2, "AirStrike 2's mission 1 did not run: %s" % frames
        assert not any("AS3D_SCREEN name=gameover" in t for t in p.texts()[mk:]), "game over within 30 s"
        w.shot(p, "choose_as2_playing")

        w.step("choose: in-game menu, Quit, Change game; then the saves")
        mk = p.mark()
        p.key("Escape")
        wait_new_screen(p, "ingame", mk)
        mk = p.mark()
        p.tap_item("ingame", 3)
        wait_new_screen(p, "main", mk)
        p.page.wait_for_timeout(500)
        mk = p.mark()
        p.tap_item("main", 60)
        p.wait_line(r"AS3D_WEB change_game", 30, after=mk)
        p.wait_line(r"AS3D_WEB chooser games=", 60, after=mk)
        p.page.wait_for_selector("#games:not([hidden])", timeout=60000)
        assert "last" in (p.page.get_attribute("#game-list button[data-game=as2]", "class") or "")
        # A fresh engine reads browser storage as it is now.
        p.goto("game=as3d")
        p.wait_ready(180)
        after = read_profile(p)
        as2 = read_profile(p, AS2_PROFILE)
        assert as2, "AirStrike 2 wrote no /persist/as2/profile.bin"
        assert parse_profile(as2)["key"] == "as2"
        r["as2_save_bytes"] = len(as2)
        assert after == before, "the first game's save changed while AirStrike 2 was played"
        r["as3d_save_untouched"] = True
        r["errors"] = p.errors()
        assert not r["errors"], r["errors"]
    finally:
        p.close()
    return r


SCENARIOS = {"desktop": desktop, "phone": phone, "iphone": iphone, "byo": byo, "complete": complete,
             "gameover": gameover, "migration": migration, "choose": choose}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8766/")
    ap.add_argument("--byo-url", default="http://127.0.0.1:8767/")
    ap.add_argument("--shots", required=True)
    ap.add_argument("--engine", default="chromium", choices=["chromium", "firefox"])
    ap.add_argument("--gl", default="gpu", choices=["gpu", "swiftshader"])
    ap.add_argument("--only", default="choose,desktop,phone,iphone,byo,complete,gameover,migration")
    a = ap.parse_args()
    os.makedirs(a.shots, exist_ok=True)
    w = Walk(a)
    b = Browser(a.engine, a.gl)
    failed = []
    try:
        for name in a.only.split(","):
            w.step(f"=== {name} ({a.engine}, {a.gl})")
            t0 = time.time()
            try:
                w.results[name] = SCENARIOS[name](w, b)
                w.results[name]["seconds"] = round(time.time() - t0)
                w.step(f"=== {name}: OK")
            except Exception as e:  # noqa: BLE001 - report and go on with the next scenario
                failed.append(name)
                w.results[name] = {"failed": str(e), "trace": traceback.format_exc()}
                w.step(f"=== {name}: FAILED: {e}")
    finally:
        b.close()
    w.results["steps"] = w.steps
    dump(w.results, os.path.join(a.shots, f"walk_{a.engine}.json"))
    print("walk:", "FAILED " + ",".join(failed) if failed else "OK")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
