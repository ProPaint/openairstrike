#!/usr/bin/env python3
"""Scripted walks through the web version in a headless browser (docs/web.md, "Tests").

    walk.py --url http://127.0.0.1:8766/ [--byo-url http://127.0.0.1:8767/] --shots DIR
            [--engine chromium|firefox] [--gl gpu|swiftshader] [--only NAME,...]

Scenarios (each in a fresh browser context, so fresh storage). The bundled site holds as3d,
as2 and gulf (tools/web_build.sh); every scenario but `choose`, `gulf` and `byo` forces ?game=as3d.
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
  gulf       Gulf Thunder chosen on the start screen (its own menus, gulf/frontend.delta.md): only
             its files fetched; the main menu's six buttons 180 wide and 37 high at y 250 to 475;
             Information, Credits, Options, Top Scores opened and left; operation 1 started
             through Start Game, Next and the helicopter selection plays 30 s under the pilot; Esc (the in-game
             menu), Resume, Esc, Quit; then /persist/gulf/profile.bin exists with the key gulf and
             the other games' saves are untouched; then on a touch phone the same by taps only.
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
             page lists and offers both, AirStrike 2's logo drawn by the page and AirStrike
             3D's banner by the engine from the stored paks (a moving canvas), kept and shown
             again after a reload without a new render, gone with "Remove them"; files stored
             by the first version of the page (no game key) still found.
  byo_3ds    "Files for the 3DS" on the bring-your-own site (not in the default list: renders
             every game's music, several minutes): the files of each game on this machine
             stored, the dialog, a run cancelled, a whole run, the zip downloaded and checked
             (3ds/openairstrike/<key>/music/*.ogg at 32728 Hz, texts, paks, Settings.xml), the
             music compared with the desktop's audio_tool render-music --all (same files, same
             frames; same bytes when both encode with libvorbis; $AS3D_AUDIO_TOOL or
             build/apps/audio_tool/audio_tool or build-audio-tool/audio_tool).
  twoplayer  two players at 1280x720 (docs/web.md "Two players"), with two fake "standard"
             gamepads (navigator.getGamepads stubbed): a fresh profile gives player 2 W A S D
             and V to fire; Start Game, 2 Players, Start opens the Players screen (keyboard
             only); the pads plugged in take one player each (AS3D_PAD ... player=); a pad
             pushed right and left changes sides (AS3D_PAD side id=); A starts mission 1; the
             split keyboard flies both players (AS3D_INPUT players=2 p1= p2=, V asks for no
             full screen), then each pad flies its own player.
  twoplayer_as2  AirStrike 2's "Cooperative" on the web: Start Game, Cooperative, Next, the
             helicopter selection, the start dialogue, then the split keyboard flies both
             players (AS3D_INPUT players=2).
  online     online co-op, a desktop host against a Chromium guest over loopback ICE (docs/spec/issues/180):
             as3d_game --online host --headless --bot --stun none is started by the scenario (the
             binary: $AS3D_GAME_BIN, else build/apps/game/as3d_game), its invite code is read from
             the --code-out file and passed to the page as ?join=; the page's dialog shows the
             guest's reply code, which the scenario writes to the host's --reply-in file; the guest
             readies in the lobby by a tap; about 60 s of play; both sides' AS3D_NET hash lines
             agree at every common frame, the AS3D_GAME_FRAME lines at the same frame agree, no
             AS3D_DESYNC, the host exits 0. Needs a network interface with an address (the codes
             carry no loopback candidates).
  online_web online co-op between two Chromium pages (host and guest), the codes carried by the
             test from the one page's dialog to the other's: the host through Start Game, Game mode
             Online, Host a game; the guest by ?join=; the dialog's Copy, Paste field and Continue;
             Start in the lobby; the same hash and frame checks.
  migration  a version 1 profile at the old path /persist/profile.bin (Screen 4:3 and Show
             FPS set): after a reload the settings are in effect, /persist/as3d/profile.bin
             and /persist/profile.v1.bak exist and /persist/profile.bin is gone; the same
             after a second reload (the directory and the rename reached browser storage).
Screenshots go to DIR; a JSON report beside them (walk_<engine>.json). Exit code 1 on failure.
"""
import argparse
import os
import shutil
import re
import struct
import subprocess
import sys
import tempfile
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
        w.step("phone: the online code dialog fits the screen; Copy and Close by touch do not throw")
        p.page.evaluate("window.as3dPage.onOnlineCode('invite', 'ABCDEFGH-' + 'ABCDEFGH-'.repeat(40))")
        p.page.wait_for_timeout(300)
        box = p.page.locator("#online").bounding_box()
        assert box and box["y"] >= 0 and box["y"] + box["height"] <= vp["height"] and box["x"] >= 0 \
            and box["x"] + box["width"] <= vp["width"], box
        w.shot(p, "phone_online_dialog")
        n_err = len(p.errors())
        p.page.tap("#online-copy")
        p.page.wait_for_timeout(300)
        p.page.tap("#online-close")
        p.page.wait_for_timeout(300)
        assert not p.page.locator("#online").evaluate("(d) => d.open"), "Close left the dialog open"
        assert len(p.errors()) == n_err, p.errors()
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

# The keys of the page's cache beside the files (files.js: AirStrike 3D's banner frames); [] when
# the store is not there.
IDB_CACHE_KEYS = """() => new Promise((ok, fail) => {
    const r = indexedDB.open('as3d-game-files');
    r.onsuccess = () => {
        if (!r.result.objectStoreNames.contains('cache')) { r.result.close(); ok([]); return; }
        const q = r.result.transaction('cache').objectStore('cache').getAllKeys();
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
        p.page.wait_for_function("/not the file of any game|the contents differ from the file of every game/.test(document.getElementById('files-status').textContent)", timeout=20000)
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
            w.step("byo: 'Add another game's files' over the Play button; Back returns; AirStrike 3D's files added: both kept, the page reloads")
            assert p.page.locator("#add-files").is_visible(), "no way to add another game's files"
            p.page.click("#add-files")
            p.page.wait_for_selector("#files:not([hidden])", timeout=20000)
            assert p.page.locator("#files-back").is_visible() and p.page.locator("#play-row").is_hidden()
            assert p.page.locator("#files-list li.missing").count() > 0, "the picker should list the files still needed"
            p.page.click("#files-back")
            assert p.page.locator("#files").is_hidden() and p.page.locator("#play-row").is_visible()
            p.page.click("#add-files")
            p.page.wait_for_selector("#files:not([hidden])", timeout=20000)
            p.page.set_input_files("#pick-files", files)
            p.page.wait_for_selector("#games:not([hidden])", timeout=180000)  # stored, reloaded, both offered
            r["byo_added_second_game"] = True
            w.step("byo: without ?game= the page lists the games whose files it holds and offers both")
            p.goto("")
            p.page.wait_for_selector("#games:not([hidden])", timeout=60000)
            offered = p.page.evaluate("Array.from(document.querySelectorAll('#game-list button')).map((b) => b.dataset.game)")
            r["byo_offered"] = offered
            assert offered[:2] == ["as3d", "as2"] and "gulf" not in offered, offered
            w.step("byo: the stored files draw the marquees in the browser: AirStrike 2's logo with its emblem, animated; AirStrike 3D's banner by the engine")
            p.page.wait_for_function("document.querySelector('#game-list button[data-game=as2]').dataset.marquee === 'canvas'", timeout=30000)
            grab = """(g) => { const c = document.querySelector('#game-list button[data-game=' + g + '] canvas');
                const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data; let lit = 0, sum = 0;
                for (let i = 0; i < d.length; i += 4) { const v = d[i] + d[i + 1] + d[i + 2]; if (v > 90) lit++; sum += v; }
                return [lit, sum, c.width, c.height]; }"""
            a1 = p.page.evaluate(grab, "as2")
            p.page.wait_for_timeout(1500)
            a2 = p.page.evaluate(grab, "as2")
            r["byo_marquee"] = [a1, a2]
            assert a1[0] > 1500, "AirStrike 2's logo is not drawn: %s" % a1
            assert a1[1] != a2[1], "the byo marquee does not move"
            # The text card first, the engine's loop once rendered (the page is not held up).
            p.page.wait_for_function("document.querySelector('#game-list button[data-game=as3d]').dataset.marquee === 'canvas'", timeout=180000)
            m = p.wait_line(r"AS3D_WEB banner_render frames=(\d+) .*total_ms=(\d+)", 10)
            r["byo_banner_render"] = m.group(0)
            r["byo_banner_render_ms"] = int(m.group(2))
            assert int(m.group(1)) == 75, m.group(0)
            b1 = p.page.evaluate(grab, "as3d")
            p.page.wait_for_timeout(700)
            b2 = p.page.evaluate(grab, "as3d")
            r["byo_banner"] = [b1, b2]
            assert b1[2:] == [352, 162], "the banner loop is not the bundled build's size: %s" % b1
            assert b1[0] > 1500, "AirStrike 3D's banner is not drawn: %s" % b1
            assert b1[1] != b2[1], "the banner does not move: %s %s %s %s" % (b1, b2, p.page.evaluate("document.querySelector('#game-list button[data-game=as3d] canvas').dataset.shown"), [t for t in p.texts() if "banner" in t])
            p.wait_line(r"AS3D_WEB banner_stored frames=75", 60)
            assert p.page.evaluate(IDB_CACHE_KEYS) == ["as3d/marquee"], p.page.evaluate(IDB_CACHE_KEYS)
            assert not any(k.startswith("as3d/marquee") for k in p.page.evaluate(IDB_KEYS)), "the banner is not a game file"
            w.step("byo: the game starts and draws after the banner's render (its WebGL context is gone, the game's is its own)")
            p.lines.clear()
            p.page.click("#game-list button[data-game=as3d]")
            p.wait_ready(180)
            to_main_menu(w, p)
            w.shot(p, "byo_main_after_banner")
            w.step("byo: reload: the banner comes from the cache, without a new render")
            mk = p.mark()
            p.goto("")
            p.page.wait_for_selector("#games:not([hidden])", timeout=60000)
            p.page.wait_for_function("document.querySelector('#game-list button[data-game=as3d]').dataset.marquee === 'canvas'", timeout=30000)
            p.wait_line(r"AS3D_WEB banner_cached frames=75", 10, after=mk)
            assert not [t for t in p.texts()[mk:] if "AS3D_WEB banner_render" in t], "rendered again despite the cache"
            p.page.wait_for_timeout(300)
            c1 = p.page.evaluate(grab, "as3d")
            p.page.wait_for_timeout(700)
            c2 = p.page.evaluate(grab, "as3d")
            r["byo_banner_cached"] = [c1, c2]
            assert c1[0] > 1500 and c1[1] != c2[1], "the cached banner is not drawn or does not move: %s %s" % (c1, c2)
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
            assert p.page.evaluate(IDB_CACHE_KEYS) == [], "the banner frames stayed after removing the files"
            assert p.page.evaluate("document.querySelectorAll('#files-cards canvas').length") == 0
            r["byo_banner_removed"] = True
        else:
            r["as2"] = "skipped: no AirStrike 2 data"
        r["errors"] = p.errors()
    finally:
        p.close()
    return r


def byo_archive(w, b):
    """docs/spec/issues/170 section 5: an installer the engine cannot open is refused with its
    note, then the owner's AirStrike 3D as one zip of the game folder (made here, in a temporary
    directory, from the install; nothing is kept) goes through as3d_unpack and starts."""
    import tempfile
    import zipfile
    orig = os.path.join(DATA_ROOT, "third_party_local", "original")
    p = b.page(w.a.byo_url, "", viewport={"width": 1280, "height": 720})
    r = {}
    tmp = tempfile.mkdtemp(prefix="as3d_walk_zip_")
    try:
        p.page.wait_for_selector("#files:not([hidden])", timeout=60000)
        w.step("byo_archive: an NSIS installer is refused with a note that names it")
        nsis = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "testdata", "archives", "nsis_dummy.pe")
        p.page.set_input_files("#pick-files", [{"name": "Setup.exe", "mimeType": "application/octet-stream",
                                                "buffer": open(nsis, "rb").read()}])
        p.page.wait_for_function("document.getElementById('files-list').textContent.includes('NSIS installer')", timeout=60000)
        r["nsis_note"] = [t for t in p.page.locator("#files-list li").all_text_contents() if "NSIS" in t]
        w.step("byo_archive: the game folder as one zip")
        z = os.path.join(tmp, "AirStrike 3D.zip")
        with zipfile.ZipFile(z, "w", zipfile.ZIP_STORED) as zf:
            for rel in ("data/pak0.apk", "data/pak1.apk", "data/pak2.apk", "data/Settings.xml", "data/gfx/logo2s.tga", "AirStrike3D.exe"):
                zf.write(os.path.join(orig, rel), "Air Strike 3D/" + rel)
        t0 = time.time()
        p.page.set_input_files("#pick-files", [z])
        p.wait_ready(240)
        r["unpack_seconds"] = round(time.time() - t0, 1)
        r["unpack_line"] = [t for t in p.texts() if "AS3D_WEB unpack" in t]
        r["data_line"] = [t for t in p.texts() if "AS3D_WEB data=" in t]
        assert r["unpack_line"] and "games=as3d" in r["unpack_line"][-1], r["unpack_line"]
        assert r["data_line"] and "game=as3d" in r["data_line"][-1] and "texts_v170.txt" in r["data_line"][-1], r["data_line"]
        p.page.click("#play")
        to_main_menu(w, p)
        w.shot(p, "byo_archive_main")
        p.page.evaluate("AS3DFiles.clearStored()")  # leave the browser's storage empty
        r["errors"] = p.errors()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
        p.close()
    return r


def audio_tool_bin():
    """The desktop audio_tool for byo_3ds's comparison: $AS3D_AUDIO_TOOL, else the usual builds."""
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..")
    for c in [os.environ.get("AS3D_AUDIO_TOOL", ""), os.path.join(root, "build", "apps", "audio_tool", "audio_tool"),
              os.path.join(root, "build-audio-tool", "audio_tool")]:
        if c and os.access(c, os.X_OK):
            return c
    return None


def byo_3ds(w, b):
    """docs/web.md "Files for the 3DS": with the files of every game on this machine stored, the
    "Files for the 3DS" dialog renders the music in the page's worker (started, cancelled, started
    again), the zip downloads and holds 3ds/openairstrike/<key>/ with music/*.ogg (Ogg Vorbis,
    32728 Hz, stereo), the texts file and, by default, the paks and Settings.xml. The music is
    compared with the desktop's audio_tool render-music --all of the same paks (when a build of
    it is there): the same files, the same number of frames, and the bytes when both encode with
    the same libvorbis. The zip and the desktop files go to a temporary directory, removed after."""
    import tempfile
    import zipfile
    games = []
    for key, inst, exe, paks in (("as3d", os.path.join(DATA_ROOT, "third_party_local", "original"), "AirStrike3D.exe", 3),
                                 ("as2", os.path.join(DATA_ROOT, "third_party_local", "games", "as2"), "AirStrike3D II.exe", 3),
                                 ("gulf", os.path.join(DATA_ROOT, "third_party_local", "games", "gulf"), "AirStrike3D II - Gulf.exe", 5)):
        data = os.path.join(inst, "data")
        names = [n for n in sorted(os.listdir(data)) if re.match(r"pak\d+\.apk$", n)] if os.path.isdir(data) else []
        if not names:
            continue
        files = [os.path.join(data, n) for n in names] + [os.path.join(data, "Settings.xml"), os.path.join(inst, exe)]
        games.append((key, data, [f for f in files if os.path.isfile(f)]))
    assert games, "no game files on this machine"
    p = b.page(w.a.byo_url, "", viewport={"width": 1280, "height": 720}, accept_downloads=True)
    r = {"games": [g[0] for g in games]}
    tmp = tempfile.mkdtemp(prefix="as3d_walk_3ds_")
    try:
        p.page.wait_for_selector("#files:not([hidden])", timeout=60000)
        assert p.page.locator("#sd3ds-open").is_hidden(), "offered before any game's files are stored"
        w.step("byo_3ds: the files of %s through the file input" % ", ".join(r["games"]))
        p.page.set_input_files("#pick-files", [f for _, _, fs in games for f in fs])
        p.page.wait_for_selector("#sd3ds-open:not([hidden])", timeout=240000)
        w.step("byo_3ds: the dialog lists the games; a first run is cancelled while it renders")
        p.page.click("#sd3ds-open")
        p.page.wait_for_selector("#sd3ds[open]", timeout=10000)
        listed = p.page.evaluate("Array.from(document.querySelectorAll('#sd3ds-games input')).map((c) => c.value)")
        r["listed"] = listed
        assert listed == [g[0] for g in games], listed
        w.shot(p, "byo_3ds_dialog")
        p.page.click("#sd3ds-make")
        p.page.wait_for_function("/Rendering music 2\\//.test(document.getElementById('sd3ds-status').textContent)", timeout=180000)
        p.page.click("#sd3ds-cancel")
        p.page.wait_for_function("window.as3dSd3ds.cancelled && !window.as3dSd3ds.running", timeout=10000)
        assert p.page.locator("#sd3ds-status").text_content() == "Cancelled.", p.page.locator("#sd3ds-status").text_content()
        assert p.page.locator("#sd3ds-save").is_hidden()
        w.step("byo_3ds: the whole run")
        mk = p.mark()
        t0 = time.time()
        p.page.click("#sd3ds-make")
        p.page.wait_for_function("/Rendering music/.test(document.getElementById('sd3ds-status').textContent)", timeout=60000)
        w.shot(p, "byo_3ds_rendering")
        p.page.wait_for_function("window.as3dSd3ds.done || window.as3dSd3ds.error", timeout=1800000)
        st = p.page.evaluate("window.as3dSd3ds")
        assert not st["error"], st["error"]
        r["seconds"] = round(time.time() - t0, 1)
        r["zip_bytes"] = st["zipBytes"]
        r["status"] = p.page.locator("#sd3ds-status").text_content()
        by_game = {}
        for x in st["renders"]:
            g = by_game.setdefault(x["game"], {"files": 0, "render_ms": 0, "audio_s": 0.0, "ogg_bytes": 0})
            g["files"] += 1
            g["render_ms"] += x["ms"]
            g["audio_s"] = round(g["audio_s"] + x["seconds"], 1)
            g["ogg_bytes"] += x["bytes"]
        r["by_game"] = by_game
        w.shot(p, "byo_3ds_done")
        with p.page.expect_download() as dl:
            p.page.click("#sd3ds-save")
        zpath = os.path.join(tmp, "openairstrike-3ds.zip")
        dl.value.save_as(zpath)
        r["download_name"] = dl.value.suggested_filename
        with zipfile.ZipFile(zpath) as zf:
            assert zf.testzip() is None, "a CRC in the zip is wrong"
            names = zf.namelist()
            zf.extractall(os.path.join(tmp, "zip"))
        r["zip_entries"] = len(names)
        texts = {"as3d": "texts_v170.txt", "as2": "texts_as2.txt", "gulf": "texts_gulf.txt"}
        cmp = {}
        tool = audio_tool_bin()
        for key, data, files in games:
            base = "3ds/openairstrike/%s/" % key
            mine = sorted(n[len(base) + 6:] for n in names if n.startswith(base + "music/"))
            assert mine and all(n.endswith(".ogg") for n in mine), (key, mine)
            assert base + texts[key] in names, (key, "no texts file")
            for f in files:
                if f.endswith(".apk") or f.endswith("Settings.xml"):
                    n = base + os.path.basename(f)
                    assert n in names, n
                    assert open(os.path.join(tmp, "zip", n), "rb").read() == open(f, "rb").read(), n + " differs"
            frames = {x["file"]: x["frames"] for x in st["renders"] if x["game"] == key}
            c = {"files": mine, "frames": frames}
            if tool:
                out = os.path.join(tmp, "desk", key)
                os.makedirs(out)
                run = subprocess.run(["bash", "-c", 'ulimit -v 4000000; exec "$@"', "audio_tool", tool, "--paks", data,
                                      "render-music", "--all", out], capture_output=True, text=True, timeout=1800)
                assert run.returncode == 0, run.stderr[-2000:]
                desk = sorted(os.listdir(out))
                assert desk == mine, (key, desk, mine)
                dframes = {}
                for line in run.stdout.splitlines():
                    m = re.search(r"-> \S+/([^/ ]+\.ogg) \(\S+ s at (\d+) Hz, (\d+) frames", line)
                    if m:
                        dframes[m.group(1)] = int(m.group(3))
                        assert int(m.group(2)) == 32728, line
                assert dframes == frames, (key, dframes, frames)
                same = [n for n in mine if open(os.path.join(out, n), "rb").read() ==
                        open(os.path.join(tmp, "zip", base, "music", n), "rb").read()]
                c["desktop_encoder"] = "libvorbis" if "libvorbis)" in run.stdout else "external"
                c["bytes_identical"] = "%d/%d" % (len(same), len(mine))
                if c["desktop_encoder"] == "libvorbis":
                    assert len(same) == len(mine), (key, "the Ogg bytes differ from audio_tool's", sorted(set(mine) - set(same)))
            else:
                c["desktop"] = "skipped: no audio_tool build"
            # The stream itself: Ogg Vorbis, 32728 Hz, stereo (the identification header).
            head = open(os.path.join(tmp, "zip", base, "music", mine[0]), "rb").read(64)
            assert head[:4] == b"OggS" and head[28:35] == b"\x01vorbis", head[:40]
            ch, rate = head[39], struct.unpack("<I", head[40:44])[0]
            assert (ch, rate) == (2, 32728), (ch, rate)
            cmp[key] = c
        r["compare"] = cmp
        r["lines"] = [t for t in p.texts()[mk:] if "AS3D_WEB sd3ds" in t]
        p.page.click("#sd3ds-close")
        p.page.evaluate("AS3DFiles.clearStored()")  # leave the browser's storage empty
        r["errors"] = p.errors()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
        p.close()
    return r


# Two fake "standard" gamepads (index 0 and 1), unplugged until window.__connect(i) (the
# browser shows a pad only after a press; Emscripten's SDL polls navigator.getGamepads).
TWO_PADS_JS = """
(() => {
  const mk = (index, id) => ({ id, index, connected: false, mapping: 'standard', timestamp: 1,
    buttons: Array.from({ length: 17 }, () => ({ pressed: false, touched: false, value: 0 })),
    axes: [0, 0, 0, 0] });
  window.__pads = [mk(0, 'Fake Xbox Controller (STANDARD GAMEPAD Vendor: 045e Product: 02ea)'),
                   mk(1, 'Fake DualSense (STANDARD GAMEPAD Vendor: 054c Product: 0ce6)')];
  navigator.getGamepads = () => {
    const out = [null, null, null, null];
    for (const p of window.__pads) if (p.connected) { p.timestamp = performance.now(); out[p.index] = p; }
    return out;
  };
  window.__connect = (i) => {
    const p = window.__pads[i];
    p.connected = true;
    const e = new Event('gamepadconnected');
    Object.defineProperty(e, 'gamepad', { value: p });
    window.dispatchEvent(e);
  };
  window.__btn = (i, b, on) => { const x = window.__pads[i].buttons[b]; x.pressed = on; x.touched = on; x.value = on ? 1 : 0; };
})();
"""
PAD = {"A": 0, "B": 1, "START": 9, "UP": 12, "DOWN": 13, "LEFT": 14, "RIGHT": 15}


def twoplayer(w, b):
    p = b.page(w.a.url, None, viewport={"width": 1280, "height": 720})
    p.ctx.add_init_script(TWO_PADS_JS)
    p.goto("game=as3d")
    r = {}

    def pad_press(i, button, ms=150):
        p.page.evaluate("([i, b]) => window.__btn(i, b, true)", [i, PAD[button]])
        p.page.wait_for_timeout(ms)
        p.page.evaluate("([i, b]) => window.__btn(i, b, false)", [i, PAD[button]])
        p.page.wait_for_timeout(200)

    def input_line(mk, rx, timeout=10):
        return p.wait_line(r"AS3D_INPUT players=2 " + rx, timeout, after=mk)

    try:
        p.wait_ready()
        w.step("twoplayer: Play, main menu")
        p.page.click("#play")
        to_main_menu(w, p)

        w.step("twoplayer: a fresh web profile gives player 2 W A S D and V")
        mk = p.mark()
        p.tap_item("main", 3)
        wait_new_screen(p, "options", mk)
        p.page.wait_for_timeout(300)
        mk = p.mark()
        p.tap_item("options", 1)    # Back (saves)
        wait_new_screen(p, "main", mk)
        p.wait_line(r"AS3D_WEB profile_synced", 10, after=mk)
        prof = parse_profile(read_profile(p))
        keys = {k: prof["settings"].get(k) for k in ("key0.0.0", "key1.0.0", "key1.2.0", "key1.4.0", "key1.6.0", "key1.8.0")}
        r["profile_keys"] = keys
        assert keys["key0.0.0"] == 32, "player 1 does not fire with Space"
        assert keys["key1.0.0"] == ord("V") and keys["key1.2.0"] == ord("G") and keys["key1.4.0"] == ord("H"), keys
        assert keys["key1.6.0"] == ord("W") and keys["key1.8.0"] == ord("A"), keys

        w.step("twoplayer: Start Game, 2 Players, Start: the Players screen")
        mk = p.mark()
        p.tap_item("main", 1)
        wait_new_screen(p, "start", mk)
        p.page.wait_for_timeout(400)
        assert 5 in p.menu_items("start"), "no Game mode spinner with a keyboard"
        p.tap_item("start", 5)      # 1 Player -> 2 Players
        p.page.wait_for_timeout(300)
        mk = p.mark()
        p.tap_item("start", 2)
        wait_new_screen(p, "players", mk)
        p.page.wait_for_timeout(600)
        w.shot(p, "twoplayer_players_keyboard")

        w.step("twoplayer: two pads plugged in take one player each")
        mk = p.mark()
        p.page.evaluate("window.__connect(0)")
        m0 = p.wait_line(r"AS3D_PAD added id=(\d+) .* player=(\d)", 15, after=mk)
        mk1 = p.mark()
        p.page.evaluate("window.__connect(1)")
        m1 = p.wait_line(r"AS3D_PAD added id=(\d+) .* player=(\d)", 15, after=mk1)
        r["pad_sides"] = [m0.group(2), m1.group(2)]
        assert m0.group(2) == "1" and m1.group(2) == "2", r["pad_sides"]
        id0 = m0.group(1)

        w.step("twoplayer: pad 1 pushed right goes to player 2, pushed left back to player 1")
        mk = p.mark()
        pad_press(0, "RIGHT")
        p.wait_line(r"AS3D_PAD side id=%s player=2" % id0, 5, after=mk)
        mk = p.mark()
        pad_press(0, "LEFT")
        p.wait_line(r"AS3D_PAD side id=%s player=1" % id0, 5, after=mk)
        assert p.state()["screen"] == "players", "a push left the Players screen"
        p.page.evaluate("window.__btn(1, 3, true)")   # Y held on pad 2: its row lights up
        p.page.wait_for_timeout(400)
        w.shot(p, "twoplayer_players_pads")
        p.page.evaluate("window.__btn(1, 3, false)")
        p.page.wait_for_timeout(200)

        w.step("twoplayer: A on pad 1 starts mission 1 for two players")
        mk = p.mark()
        pad_press(0, "A")
        p.wait_line(r"AS3D_LEVEL_LOADED mission=1", 90, after=mk)
        if wait_new_screen(p, "(playing|hint)", mk, 60).group(1) == "hint":
            p.key("Enter")
            p.page.wait_for_timeout(500)
        p.page.wait_for_timeout(1200)
        p.page.locator("#canvas").focus()

        w.step("twoplayer: the split keyboard flies both players")
        fs0 = p.state()["fullscreenRequests"]
        mk = p.mark()
        for k in ("Space", "ArrowRight", "v", "d"):
            p.page.keyboard.down(k)
        p.page.wait_for_timeout(600)
        m = input_line(mk, r"p1=0x081 p2=0x081")
        r["keyboard_input"] = m.group(0)
        w.shot(p, "twoplayer_play_keys")
        for k in ("Space", "ArrowRight", "v", "d"):
            p.page.keyboard.up(k)
        p.page.wait_for_timeout(300)
        assert p.state()["fullscreenRequests"] == fs0, "V asked for full screen"
        mk = p.mark()
        p.page.keyboard.down("a")    # player 2 alone: left
        p.page.wait_for_timeout(400)
        input_line(mk, r"p1=0x000 p2=0x040")
        p.page.keyboard.up("a")
        p.page.wait_for_timeout(300)

        w.step("twoplayer: each pad flies its own player")
        mk = p.mark()
        p.page.evaluate("window.__btn(0, 0, true); window.__btn(0, 15, true)")   # pad 1: A + right
        p.page.wait_for_timeout(500)
        input_line(mk, r"p1=0x081 p2=0x000")
        mk = p.mark()
        p.page.evaluate("window.__btn(1, 0, true); window.__btn(1, 14, true)")   # pad 2: A + left
        p.page.wait_for_timeout(500)
        m = input_line(mk, r"p1=0x081 p2=0x041")
        r["pad_input"] = m.group(0)
        w.shot(p, "twoplayer_play_pads")
        p.page.evaluate("for (const i of [0, 1]) for (const b of [0, 14, 15]) window.__btn(i, b, false)")
        p.page.wait_for_timeout(400)
        assert p.state()["screen"] == "playing", p.state()["screen"]
        r["errors"] = p.errors()
    finally:
        p.close()
    return r


def twoplayer_as2(w, b):
    p = b.page(w.a.url, "game=as2", viewport={"width": 1280, "height": 720})
    r = {}
    try:
        p.wait_ready()
        p.page.click("#play")
        to_main_menu(w, p)
        w.step("twoplayer_as2: Start Game, Cooperative, Next, the selection, Start")
        mk = p.mark()
        p.tap_item("main", 1)
        wait_new_screen(p, "start", mk)
        p.page.wait_for_timeout(600)
        p.tap_item("start", 4)      # Game mode: Single Player -> Cooperative
        p.page.wait_for_timeout(300)
        mk = p.mark()
        p.tap_item("start", 2)      # Next
        wait_new_screen(p, "heli", mk)
        p.page.wait_for_timeout(600)
        w.shot(p, "twoplayer_as2_heli")
        p.tap_item("heli", 1)       # Start
        p.wait_line(r"AS3D_LEVEL_LOADED mission=1", 90, after=mk)
        if wait_new_screen(p, "(playing|hint|dialogue)", mk, 60).group(1) == "dialogue":
            for _ in range(40):
                if any(re.search(r"AS3D_SCREEN name=(playing|hint)\b", t) for t in p.texts()[mk:]):
                    break
                p.tap_virtual(400, 300)
                p.page.wait_for_timeout(700)
            wait_new_screen(p, "(playing|hint)", mk, 60)
        if p.state()["screen"] == "hint":
            p.key("Enter")
        p.page.wait_for_timeout(1000)
        p.page.locator("#canvas").focus()
        w.step("twoplayer_as2: the split keyboard flies both players")
        mk = p.mark()
        for k in ("Space", "ArrowRight", "v", "d"):
            p.page.keyboard.down(k)
        p.page.wait_for_timeout(600)
        m = p.wait_line(r"AS3D_INPUT players=2 p1=0x081 p2=0x081", 10, after=mk)
        r["keyboard_input"] = m.group(0)
        w.shot(p, "twoplayer_as2_play")
        for k in ("Space", "ArrowRight", "v", "d"):
            p.page.keyboard.up(k)
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
        w.step("choose: the start screen offers the bundled games, nothing downloaded yet")
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


GULF_PROFILE = "/persist/gulf/profile.bin"


def gulf(w, b):
    """Gulf Thunder through its own menus (docs/spec/gulf/frontend.delta.md): chosen on the start
    screen, then by mouse and by touch: the menus, operation 1, the in-game menu, Quit."""
    p = b.page(w.a.url, None, viewport={"width": 1280, "height": 720})
    p.requests = []
    p.page.on("request", lambda req: p.requests.append(req.url))
    p.goto("bot=1&menus=1")  # the pilot flies once the menus have started the mission
    r = {}
    try:
        w.step("gulf: chosen on the start screen; only its files are fetched")
        p.page.wait_for_selector("#games:not([hidden])", timeout=60000)
        p.page.wait_for_timeout(1500)
        offered = p.page.evaluate("Array.from(document.querySelectorAll('#game-list button')).map((b) => b.dataset.game)")
        r["offered"] = offered
        assert "gulf" in offered, "the start screen does not offer Gulf Thunder: %s" % offered
        assert data_requests(p) == [], data_requests(p)
        p.page.click("#game-list button[data-game=gulf]")
        p.wait_ready(180)
        # The first game's save, if any, is what it is before Gulf Thunder runs.
        before = read_profile(p)
        to_main_menu(w, p)
        got = data_requests(p)
        r["requests"] = got
        assert got and all(g.startswith("gulf/") for g in got), got
        w.shot(p, "gulf_main")

        w.step("gulf: the main menu: six text buttons 180 wide and 37 high at y 250 to 475")
        items = p.menu_items("main")
        ys = {1: 250, 2: 295, 3: 340, 4: 385, 7: 430, 5: 475}
        for iid, y in ys.items():
            assert iid in items, "no item %d on Gulf Thunder's main menu: %s" % (iid, sorted(items))
            x_, y_, w_, h_, _ = items[iid]
            assert (x_, y_, w_, h_) == (310, y, 180, 37), (iid, items[iid])
        assert 60 in items, "no Change game on the main menu"

        for name, iid in (("info", 4), ("credits", 7), ("options", 3), ("scores", 2)):
            w.step("gulf: %s and back" % name)
            mk = p.mark()
            p.tap_item("main", iid)
            wait_new_screen(p, name, mk)
            p.page.wait_for_timeout(700)
            if name == "info":
                items = p.menu_items("info")
                assert 10 in items, "no page spinner on Information"
            w.shot(p, "gulf_" + name)
            mk = p.mark()
            p.tap_item(name, 1)                     # Back (the Information page spinner is 10)
            wait_new_screen(p, "main", mk)
            p.page.wait_for_timeout(400)

        w.step("gulf: operation 1 through Start Game, Next, the helicopter selection; 30 s of play under the pilot")
        start_mission(w, p)
        w.shot(p, "gulf_loaded")
        mk = p.mark()
        t0 = time.time()
        while time.time() - t0 < 30:
            p.page.wait_for_timeout(2000)
        frames = [int(m.group(1)) for t in p.texts()[mk:] for m in [re.search(r"AS3D_GAME_FRAME n=(\d+) mission=1 ", t)] if m]
        r["frame_markers"] = frames
        assert len(set(frames)) >= 2, "Gulf Thunder's operation 1 did not run: %s" % frames
        w.shot(p, "gulf_playing")

        w.step("gulf: Esc (in-game menu), Resume, Esc, Quit")
        mk = p.mark()
        p.key("Escape")
        wait_new_screen(p, "ingame", mk)
        p.page.wait_for_timeout(600)
        items = p.menu_items("ingame")
        for iid, y in {1: 300, 2: 345, 4: 390, 3: 435}.items():
            assert items[iid][:4] == (320, y, 160, 37), (iid, items[iid])
        w.shot(p, "gulf_ingame")
        mk = p.mark()
        p.tap_item("ingame", 1)                     # Resume
        p.wait_line(r"AS3D_SCREEN name=playing\b", 30, after=mk)
        p.page.wait_for_timeout(1500)
        mk = p.mark()
        p.key("Escape")
        wait_new_screen(p, "ingame", mk)
        p.page.wait_for_timeout(500)
        mk = p.mark()
        p.tap_item("ingame", 3)                     # Quit
        wait_new_screen(p, "main", mk)
        p.page.wait_for_timeout(500)

        w.step("gulf: the saves: gulf's has the key gulf; the first game's is untouched")
        mk = p.mark()
        p.tap_item("main", 60)
        p.wait_line(r"AS3D_WEB change_game", 30, after=mk)
        p.wait_line(r"AS3D_WEB chooser games=", 60, after=mk)
        p.goto("game=as3d")
        p.wait_ready(180)
        after = read_profile(p)
        mine = read_profile(p, GULF_PROFILE)
        assert mine, "Gulf Thunder wrote no /persist/gulf/profile.bin"
        assert parse_profile(mine)["key"] == "gulf"
        r["gulf_save_bytes"] = len(mine)
        assert after == before, "the first game's save changed while Gulf Thunder was played"
        r["errors"] = p.errors()
        assert not r["errors"], r["errors"]
    finally:
        p.close()

    # The same by taps only, on a touch phone (landscape 20:9).
    dev = b.device("Pixel 7")
    vp = {"width": 915, "height": 412}
    ctx = {k: v for k, v in dev.items() if k not in ("viewport", "screen", "default_browser_type")}
    ctx.update(viewport=vp, screen={"width": 915, "height": 412}, device_scale_factor=2.625, is_mobile=True,
               has_touch=True)
    q = b.page(w.a.url, "game=gulf", **ctx)
    try:
        w.step("gulf: on a touch phone: Play, the main menu by taps")
        q.wait_ready()
        assert q.state()["touch"], "a touch phone did not start in touch mode"
        q.page.tap("#play")
        q.page.wait_for_timeout(800)
        to_main_menu(w, q, by="touch")
        w.shot(q, "gulf_phone_main")
        mk = q.mark()
        q.tap_item("main", 4, "touch")              # Information: the spinner goes back by a tap
        wait_new_screen(q, "info", mk)
        q.page.wait_for_timeout(600)
        x_, y_, w_, h_, _ = q.menu_items("info")[10]
        q.tap_virtual(x_ - 10, y_ + 8, "touch")
        q.page.wait_for_timeout(400)
        w.shot(q, "gulf_phone_info")
        mk = q.mark()
        q.tap_item("info", 1, "touch")
        wait_new_screen(q, "main", mk)
        q.page.wait_for_timeout(400)
        w.step("gulf: on a touch phone: operation 1, the pause button, Quit")
        start_mission(w, q, by="touch")
        q.page.wait_for_timeout(1500)
        if q.state()["screen"] == "hint":
            q.tap_item("hint", 1, "touch")
            q.page.wait_for_timeout(500)
        L = touch_layout(q)
        mk = q.mark()
        px, py = q.fb_to_css(L["pause"][0], L["pause"][1])
        q.tap_css(px, py, "touch")
        wait_new_screen(q, "ingame", mk)
        q.page.wait_for_timeout(500)
        w.shot(q, "gulf_phone_ingame")
        mk = q.mark()
        q.tap_item("ingame", 3, "touch")            # Quit
        wait_new_screen(q, "main", mk)
        r["touch"] = "ok"
        r["touch_errors"] = q.errors()
        assert not r["touch_errors"], r["touch_errors"]
    finally:
        q.close()
    return r



# ------------------------------------------------------------------------------------------
# Online co-op (docs/spec/issues/180-online-coop.md 13 T6, 14 item 4)
# ------------------------------------------------------------------------------------------
# Chromium hides local addresses behind mDNS names, which the codes cannot carry: the guest and
# the web host need the real ones.
ONLINE_FLAGS = ["--disable-features=WebRtcHideLocalIpsWithMdns"]
ONLINE_FRAMES = 3700   # about a minute of play at 60 Hz


def game_bin():
    here = os.path.dirname(os.path.abspath(__file__))
    for c in (os.environ.get("AS3D_GAME_BIN"), os.path.join(here, "..", "..", "..", "build", "apps", "game", "as3d_game")):
        if c and os.access(c, os.X_OK):
            return os.path.abspath(c)
    raise AssertionError("no desktop build: set AS3D_GAME_BIN or build build/apps/game/as3d_game")


def wait_file(path, timeout):
    end = time.time() + timeout
    while time.time() < end:
        if os.path.exists(path) and os.path.getsize(path) > 0:
            with open(path) as f:
                return f.read().strip()
        time.sleep(0.1)
    raise AssertionError("timeout waiting for " + path)


def write_atomic(path, text):
    with open(path + ".tmp", "w") as f:
        f.write(text)
    os.rename(path + ".tmp", path)


def net_hashes(lines):
    out = {}
    for t in lines:
        m = re.search(r"AS3D_NET hash frame=(\d+) h=([0-9a-f]+)", t)
        if m:
            out[int(m.group(1))] = m.group(2)
    return out


def game_frames(lines):
    out = {}
    for t in lines:
        m = re.search(r"AS3D_GAME_FRAME n=(\d+) mission=(\d+) score=(-?\d+) lives=(\S+) map_pos=(\S+) paused=(\d)", t)
        if m:
            out[int(m.group(1))] = m.groups()[1:5]
    return out


def compare_online(a_lines, b_lines, r, min_hashes=100):
    """The two sides' AS3D_NET hash lines and AS3D_GAME_FRAME lines agree where both have them."""
    for lines in (a_lines, b_lines):
        bad = [t for t in lines if "AS3D_DESYNC" in t]
        assert not bad, "a desync: " + bad[0]
    ha, hb = net_hashes(a_lines), net_hashes(b_lines)
    common = sorted(set(ha) & set(hb))
    r["common_hashes"] = len(common)
    assert len(common) >= min_hashes, "only %d common hash frames (%d / %d)" % (len(common), len(ha), len(hb))
    bad = [f for f in common if ha[f] != hb[f]]
    assert not bad, "hashes differ at frames %s" % bad[:5]
    fa, fb = game_frames(a_lines), game_frames(b_lines)
    fcommon = sorted(set(fa) & set(fb))
    r["common_frame_markers"] = fcommon
    assert fcommon and fcommon[-1] >= 3000, "AS3D_GAME_FRAME lines at the same frame: %s" % fcommon
    for n in fcommon:
        assert fa[n] == fb[n], "AS3D_GAME_FRAME n=%d differs: %s vs %s" % (n, fa[n], fb[n])
    r["frame_marker_sample"] = {n: fa[n] for n in fcommon[-1:]}


def web_page_start(w, bb, url, query):
    p = bb.page(url, query, viewport={"width": 1024, "height": 640})
    p.wait_ready()
    p.page.click("#play")
    to_main_menu(w, p)
    return p


def wait_dialog_code(p, kind, timeout=90):
    """The page's dialog showed this side's code: its text, from the dialog's textarea."""
    p.page.wait_for_function("window.as3dState.online.shown.some((s) => s.kind === '%s')" % kind, timeout=timeout * 1000)
    code = p.page.evaluate("document.getElementById('online-code').value")
    assert p.page.locator("#online").evaluate("(d) => d.open"), "the code dialog is not open"
    assert len(code) > 100, "a short code: %r" % code
    return code


def lobby_ready_and_play(w, p, role, timeout=60):
    p.wait_screen("online_lobby", timeout)
    p.page.wait_for_timeout(1500)
    if role == "guest":
        w.step("online: the guest taps Ready")
        p.tap_item("online_lobby", 14)
    else:
        w.step("online: the host taps Start once the guest is ready")
        end = time.time() + 30
        while time.time() < end:
            if not p.menu_items("online_lobby")[13][4]:
                break
            p.page.wait_for_timeout(500)
        p.tap_item("online_lobby", 13)


def online(w, b):
    """A desktop host process and a Chromium guest over loopback ICE (docs/spec/issues/180, 14 item 4)."""
    exe = game_bin()
    T = tempfile.mkdtemp(prefix="as3d_online_")
    r = {"dir": T}
    out = open(os.path.join(T, "host.log"), "w")
    host = subprocess.Popen([exe, "--online", "host", "--headless", "--bot", "--level", "1", "--seed", "7",
                             "--frames", str(ONLINE_FRAMES), "--stun", "none", "--markers",
                             "--code-out", os.path.join(T, "invite"), "--reply-in", os.path.join(T, "reply")],
                            stdout=out, stderr=subprocess.STDOUT, env=dict(os.environ))
    bb = None
    p = None
    try:
        w.step("online: the host's invite code from its --code-out file")
        invite = wait_file(os.path.join(T, "invite"), 60)
        assert len(invite) > 100, invite
        bb = Browser(w.a.engine, w.a.gl, extra_flags=ONLINE_FLAGS, pw=b.pw)
        w.step("online: the guest page opens ?join=<invite>, skips the intro, the reply code comes up in the dialog")
        p = web_page_start(w, bb, w.a.url, "game=as3d&bot=1&menus=1&noaudio=1&stun=none&join=" + invite)
        reply = wait_dialog_code(p, "reply")
        assert "join=" not in p.page.url, "?join= was left in the address"
        w.shot(p, "online_reply_dialog")
        w.step("online: the reply code goes to the host's --reply-in file")
        write_atomic(os.path.join(T, "reply"), reply)
        lobby_ready_and_play(w, p, "guest", 60)
        w.step("online: about a minute of play, the host ends the run at frame %d" % ONLINE_FRAMES)
        end = time.time() + 150
        while host.poll() is None and time.time() < end:
            p.page.wait_for_timeout(500)
            assert not p.state()["failed"], p.state()["failed"]
        assert host.poll() == 0, "the host exited with %s" % host.poll()
        p.page.wait_for_timeout(1500)
        w.shot(p, "online_end")
        out.flush()
        with open(os.path.join(T, "host.log")) as f:
            host_lines = f.read().splitlines()
        compare_online(host_lines, p.texts(), r)
        r["errors"] = p.errors()
        assert not r["errors"], r["errors"]
    finally:
        if host.poll() is None:
            host.kill()
        out.close()
        if p:
            p.close()
        if bb:
            bb.close()
    return r


def online_web(w, b):
    """Two Chromium pages, one hosting and one joining; the test carries the codes between the dialogs."""
    r = {}
    bb = Browser(w.a.engine, w.a.gl, extra_flags=ONLINE_FLAGS, pw=b.pw)
    h = g = None
    base = "game=as3d&bot=1&menus=1&noaudio=1&stun=none&frames=%d" % ONLINE_FRAMES
    try:
        w.step("online_web: the host page: Start Game, Game mode Online, Start, Host a game")
        h = web_page_start(w, bb, w.a.url, base)
        mk = h.mark()
        h.tap_item("main", 1)
        wait_new_screen(h, "start", mk)
        h.page.wait_for_timeout(500)
        h.tap_item("start", 5)      # 1 Player -> 2 Players -> Online
        h.page.wait_for_timeout(250)
        h.tap_item("start", 5)
        h.page.wait_for_timeout(250)
        mk = h.mark()
        h.tap_item("start", 2)
        wait_new_screen(h, "online", mk)
        h.page.wait_for_timeout(2500)   # "Checking game files..."
        mk = h.mark()
        h.tap_item("online", 2)
        wait_new_screen(h, "online_host", mk)
        invite = wait_dialog_code(h, "invite")
        assert h.page.evaluate("window.as3dState.online.asked.includes('reply')"), "the host's dialog did not ask for the reply"
        assert h.page.locator("#online-ask").is_visible() and h.page.locator("#online-copy").is_visible()
        w.shot(h, "online_web_host_dialog")
        w.step("online_web: Copy in the dialog (the clipboard may refuse: the page says so, no error)")
        h.page.click("#online-copy")
        h.page.wait_for_timeout(300)
        assert any("AS3D_WEB online_copy" in t for t in h.texts())

        w.step("online_web: the guest page joins with ?join=")
        g = web_page_start(w, bb, w.a.url, base + "&join=" + invite)
        reply = wait_dialog_code(g, "reply")
        w.shot(g, "online_web_guest_dialog")

        w.step("online_web: the reply is pasted into the host's dialog and Continue pressed")
        h.page.fill("#online-paste", reply)
        h.page.click("#online-continue")
        lobby_ready_and_play(w, g, "guest", 60)
        lobby_ready_and_play(w, h, "host", 60)
        w.step("online_web: play until the frames run out")
        end = time.time() + 240
        while time.time() < end and not (h.state()["finished"] or g.state()["finished"]):
            h.page.wait_for_timeout(1000)
            for pg in (h, g):
                assert not pg.state()["failed"], pg.state()["failed"]
        h.page.wait_for_timeout(1500)
        w.shot(h, "online_web_end")
        compare_online(h.texts(), g.texts(), r)
        r["errors"] = h.errors() + g.errors()
        assert not r["errors"], r["errors"]
    finally:
        for pg in (h, g):
            if pg:
                pg.close()
        bb.close()
    return r

SCENARIOS = {"desktop": desktop, "phone": phone, "iphone": iphone, "byo": byo, "byo_archive": byo_archive, "byo_3ds": byo_3ds,
             "complete": complete,
             "gameover": gameover, "migration": migration, "choose": choose, "gulf": gulf,
             "twoplayer": twoplayer, "twoplayer_as2": twoplayer_as2, "online": online, "online_web": online_web}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8766/")
    ap.add_argument("--byo-url", default="http://127.0.0.1:8767/")
    ap.add_argument("--shots", required=True)
    ap.add_argument("--engine", default="chromium", choices=["chromium", "firefox"])
    ap.add_argument("--gl", default="gpu", choices=["gpu", "swiftshader"])
    ap.add_argument("--only", default="choose,gulf,desktop,phone,iphone,byo,complete,gameover,migration,twoplayer,twoplayer_as2,online,online_web")
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
