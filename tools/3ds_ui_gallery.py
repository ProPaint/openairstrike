#!/usr/bin/env python3
"""The 3DS screen gallery (docs/3ds-ui-design.md "UI foundation and the screen gallery (round 4,
R4.4)"): renders every screen of a 3DS build on Azahar and writes them as pictures.

    python3 -I tools/3ds_ui_gallery.py --bin build-3ds/apps/3ds/openairstrike.3dsx [--label NAME]
        [--only run1,run2] [--out out/ui3ds/gallery] [--tag gallery] [--keep-raw] [--convert-only]

Each run of RUNS below is a run.ini (the front end with dump_screens / dump_every / dump_loading,
taps on menu items, the bot) launched through a serialised Azahar run: it waits until no Azahar runs,
installs the binary as sdmc:/3ds/openairstrike/<tag>.3dsx and the run.ini (the SD's own run.ini and,
for a fresh-profile run, the game's profile.bin saved and restored), waits for the app's exit line,
kills only its own launcher and emulator, and moves the PPMs written during the run into
<out>/<label>/raw/<run>/. The screens are then picked from the dumps and written as
<out>/<label>/<game>/<NN>_<screen>_{top,bottom,both}.png and @2x.png (nearest), plus one contact
sheet per game, <out>/<label>/<game>_sheet.png (and @2x).

Taps: a menu item by id (the AS3D_MENU ids). A binary that knows run.ini ui_taps (R4.4 on) taps the
item wherever its layout put it; an older one (the drops before R4.4) gets the pixel of LEGACY_RECTS
(the item rectangles those builds log). Pictures are game art: out/ is never committed.
Unattended: about 12 minutes for the whole set on Azahar; no Azahar process is left behind.
"""
import argparse
import os
import re
import shutil
import signal
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SD = os.path.expanduser("~/.var/app/org.azahar_emu.Azahar/data/azahar-emu/sdmc/3ds/openairstrike")

COMMON = {"attract": "1", "slider": "0", "audio": "0", "ui_fixed_dt": "1", "dump_screens": "1"}

# The item rectangles (virtual 800x600) the builds before R4.4 lay out, by game, screen and item id
# (their AS3D_MENU lines): the pixel of a tap there is 0.4 x the centre.
LEGACY_RECTS = {
    "as3d": {
        "main": {1: (240, 250, 320, 33), 2: (240, 283, 320, 35), 3: (240, 318, 320, 35), 4: (240, 353, 320, 35),
                 5: (240, 388, 320, 35)},
        "info": {10: (335, 440, 163, 16), 1: (50, 450, 128, 64)},
        "start": {1: (50, 450, 128, 64), 2: (605, 450, 150, 64)},
        "ingame": {1: (240, 270, 320, 35), 2: (240, 305, 320, 35), 3: (240, 340, 320, 35)},
    },
    "as2": {
        "main": {1: (277, 230, 246, 30), 2: (277, 275, 246, 30), 3: (277, 320, 246, 30), 4: (277, 365, 246, 30),
                 7: (277, 410, 246, 30), 5: (277, 455, 246, 30)},
        "start": {1: (40, 520, 111, 30), 2: (645, 520, 111, 30)},
        "heli": {1: (643, 520, 117, 30), 2: (40, 520, 111, 30)},
        "ingame": {1: (297, 250, 206, 30), 2: (297, 295, 206, 30), 4: (297, 340, 206, 30), 3: (297, 385, 206, 30)},
    },
    "gulf": {
        "main": {1: (310, 250, 180, 37), 2: (310, 295, 180, 37), 3: (310, 340, 180, 37), 4: (310, 385, 180, 37),
                 7: (310, 430, 180, 37), 5: (310, 475, 180, 37)},
        "start": {1: (40, 520, 105, 37), 2: (645, 520, 105, 37)},
        "heli": {1: (669, 520, 91, 37), 2: (40, 520, 105, 37)},
        "ingame": {1: (320, 300, 160, 37), 2: (320, 345, 160, 37), 4: (320, 390, 160, 37), 3: (320, 435, 160, 37)},
    },
}

# A run: its game (for the taps' legacy rectangles and the output folder), run.ini keys, timed inputs
# (frame, "@screen/id" tap or a pad_script press), whether the game's profile starts fresh, and the
# screens to keep: (output name, selector), the selector one of
#   ("screen", name, k)        the k-th dump_screens dump of that screen (k from 1)
#   ("after", name, k, frames) the first dump_every dump at or after the k-th change to that screen + frames
#   ("load", k)                the k-th loading-screen dump (dump_loading)
#   ("frame", f)               the first dump_every dump at or after frame f (R4.8)
#   ("sel", key)               the selector with that game's card focused
#   ("last",)                  the last dump_every dump (R4.12)
RUNS = [
    {"name": "selector", "game": "selector", "fresh": [], "frames": 200,
     "ini": {"game": "select", "logo": "0"},
     "inputs": [(50, "RIGHT"), (100, "RIGHT"), (150, "B")],
     "keep": [("01_selector_as3d", ("sel", "as3d")), ("02_selector_as2", ("sel", "as2")),
              ("03_selector_gulf", ("sel", "gulf"))]},
    {"name": "as3d_menus", "game": "as3d", "fresh": [], "frames": 3000,
     "ini": {"game": "as3d", "logo": "0", "menu_bot": "1", "hint_hold": "150", "dump_every": "50", "dump_loading": "1",
             "end_at": "1500", "end_kind": "complete"},
     "inputs": [(40, "@main/3"), (140, "B"), (200, "@main/2"), (300, "B"), (360, "@main/4"), (440, "@info/10"),
                (520, "@info/10"), (600, "@info/10"), (680, "B"), (740, "@main/5"), (840, "B"), (900, "@main/1"),
                (1000, "@start/2"), (1200, "START"), (1300, "@ingame/1"), (1500, "@hint/1")],
     "keep": [("01_main_menu", ("screen", "main", 1)), ("02_options", ("screen", "options", 1)),
              ("03_top_scores", ("screen", "scores", 1)), ("04_information_1", ("screen", "info", 1)),
              ("05_information_2", ("after", "info", 1, 110)), ("06_information_3", ("after", "info", 1, 190)),
              ("07_information_4", ("after", "info", 1, 270)), ("08_exit_dialog", ("screen", "exit", 1)),
              ("09_start_game", ("screen", "start", 1)), ("10_loading", ("load", -1)),
              ("11_play_level_name", ("after", "playing", 1, 100)), ("12_play_early", ("after", "playing", 1, 50)),
              ("13_paused", ("screen", "ingame", 1)), ("14_hint", ("screen", "hint", 1)),
              ("15_play_midfight", ("after", "playing", 1, 1100)), ("16_mission_complete", ("after", "complete", 1, 150))]},
    {"name": "as3d_gameover", "game": "as3d", "fresh": [], "frames": 1200,
     "ini": {"game": "as3d", "logo": "0", "menu_bot": "1", "end_at": "300", "end_kind": "gameover", "end_score": "60000",
             "name_entry": "touch", "dump_every": "50"},
     "inputs": [(40, "@main/1"), (100, "@start/2"), (700, "A"), (860, "DOWN"), (880, "DOWN"), (1000, "A")],
     "keep": [("17_game_over", ("screen", "gameover", 1)), ("18_game_over_menu", ("after", "gameover", 1, 250)),
              ("19_name_entry", ("screen", "name", 1))]},
    # R4.7: the HUD tiles by scripted taps (pad_script T presses at the tiles' centres, the console
    # layout of hud_layouts.cpp makeV170Bottom), without the bot: the player stocked by hud_stock
    # (missiles 0, 1, 3 and power-ups 0, 1), missile 3 selected by a tap and fired by a second
    # one, power-up 1 selected, the next weapon, the pause tile; A closes a tutorial hint that
    # comes up between the taps. Playing starts one frame after the start tap (frame 101).
    {"name": "as3d_play", "game": "as3d", "fresh": [], "frames": 760,
     "ini": {"game": "as3d", "logo": "0", "hud_stock": "5", "dump_every": "10",
             "log_input": "1"},
     "inputs": [(40, "@main/1"), (100, "@start/2"), (300, "T281/175"), (330, "A"), (360, "T281/175"), (400, "A"),
                (420, "T205/95"), (450, "A"), (480, "T39/79"), (520, "A"), (560, "T20/18"), (640, "B")],
     "keep": [("20_play_stocked", ("after", "playing", 1, 140)), ("21_tap_select_missile", ("after", "playing", 1, 230)),
              ("22_tap_fire_missile", ("after", "playing", 1, 262)), ("23_tap_select_powerup", ("after", "playing", 1, 350)),
              ("24_tap_next_weapon", ("after", "playing", 1, 410)), ("25_tap_pause", ("screen", "ingame", 1))]},
    {"name": "as2_intro", "game": "as2", "fresh": ["as2"], "frames": 2000,
     "ini": {"game": "as2", "logo": "1", "dump_every": "150"},
     "inputs": [],
     "keep": [("01_intro_logo", ("screen", "intro1", 1)), ("02_intro_page2", ("screen", "intro2", 1)),
              ("03_intro_page3", ("screen", "intro3", 1)), ("04_intro_page4", ("screen", "intro4", 1)),
              ("05_intro_page4_late", ("after", "intro4", 1, 250))]},
    # R4.8: the sequels' runs on a fresh profile (the SD's profiles keep earlier runs' checkpoints,
    # which start mission 3: no start dialogue, no early hint); the profiles are restored after.
    {"name": "as2_menus", "game": "as2", "fresh": ["as2"], "frames": 3000,
     "ini": {"game": "as2", "logo": "0", "menu_bot": "1", "hint_hold": "150", "unlock_heli": "1", "dump_every": "50",
             "dump_loading": "1", "end_at": "1100", "end_kind": "complete"},
     "inputs": [(40, "@main/3"), (140, "B"), (200, "@main/2"), (300, "B"), (360, "@main/4"), (440, "R"), (500, "B"),
                (540, "@main/7"), (600, "B"), (640, "@main/5"), (700, "B"),
                (740, "@main/1"), (840, "@start/2"), (940, "@heli/6"), (1000, "@heli/1")] +
               [(f, "A") for f in range(1140, 1341, 20)] + [(1550, "START"), (1650, "B")],
     "keep": [("06_main_menu", ("screen", "main", 1)), ("07_options", ("screen", "options", 1)),
              ("08_top_scores", ("screen", "scores", 1)), ("09_information", ("screen", "info", 1)),
              ("09b_information_2", ("after", "info", 1, 90)), ("09c_credits", ("screen", "credits", 1)),
              ("09d_exit", ("screen", "exit", 1)),
              ("10_start_game", ("screen", "start", 1)), ("11_helicopter_select", ("screen", "heli", 1)),
              ("11b_helicopter_next", ("after", "heli", 1, 70)),
              ("12_loading", ("load", -1)), ("13_start_dialogue", ("after", "dialogue", 1, 60)),
              ("14_play_early", ("after", "playing", 1, 60)), ("15_paused", ("screen", "ingame", 1)),
              ("16_hint", ("screen", "hint", 1)), ("17_play_midfight", ("after", "playing", 1, 900)),
              ("18_mission_complete", ("after", "complete", 1, 150))]},
    {"name": "as2_gameover", "game": "as2", "fresh": ["as2"], "frames": 1500,
     "ini": {"game": "as2", "logo": "0", "menu_bot": "1", "end_at": "300", "end_kind": "gameover", "end_score": "60000",
             "name_entry": "touch", "dump_every": "50"},
     "inputs": [(40, "@main/1"), (140, "@start/2"), (240, "@heli/1")] + [(f, "A") for f in range(380, 581, 20)] +
               [(f, "@gameover/2") for f in range(700, 1301, 50)],
     "keep": [("19_game_over", ("screen", "gameover", 1)), ("20_name_entry", ("screen", "name", 1))]},
    {"name": "as2_play", "game": "as2", "fresh": ["as2"], "frames": 1500,
     "ini": {"game": "as2", "logo": "0", "hud_stock": "5", "dump_every": "10", "log_input": "1"},
     "inputs": [(40, "@main/1"), (140, "@start/2"), (240, "@heli/1")] + [(f, "A") for f in range(380, 581, 20)] +
               [(800, "T281/175"), (830, "A"), (860, "T281/175"), (900, "A"), (920, "T205/95"), (950, "A"),
                (980, "T39/79"), (1020, "A"), (1060, "T20/18"), (1140, "B")],
     "keep": [("21_play_stocked", ("frame", 790)), ("22_tap_select_missile", ("frame", 850)),
              ("23_tap_fire_missile", ("frame", 880)), ("24_tap_select_powerup", ("frame", 970)),
              ("25_tap_next_weapon", ("frame", 1010)), ("26_tap_pause", ("screen", "ingame", 1))]},
    # The bot with the weapon tour (the soak's), where AirStrike 2's burning wreckage floods the
    # particle cap (docs/3ds-port.md "Eighth console soak"): the thinning's picture at the peak (step
    # 2293 of the desktop sim; a bot run's frames= are steps, the dumps are displayed frames: the last).
    {"name": "as2_fire", "game": "as2", "fresh": ["as2"], "frames": 2300,
     "ini": {"game": "as2", "level": "1", "mode": "bot", "bot_weapons": "1", "logo": "0", "dump_every": "10"},
     "inputs": [],
     "keep": [("30_fire_peak", ("last",))]},
    # The boss of mission 12 under the console's build (no picture: the end-state lines of the log
    # against the desktop's as3d_sim, tools/3ds_soak_compare.py): the owner saw the cabin keep its
    # health after both turbines; the desktop bot kills it at step 4274.
    {"name": "as2_boss12", "game": "as2", "fresh": ["as2"], "frames": 4600,
     "ini": {"game": "as2", "level": "12", "mode": "sim", "bot_weapons": "1", "logo": "0", "dump_every": "100000"},
     "inputs": [],
     "keep": []},
    {"name": "gulf_intro", "game": "gulf", "fresh": ["gulf"], "frames": 600,
     "ini": {"game": "gulf", "logo": "1"},
     "inputs": [],
     "keep": [("01_intro_logo", ("screen", "intro1", 1))]},
    {"name": "gulf_menus", "game": "gulf", "fresh": ["gulf"], "frames": 2700,
     "ini": {"game": "gulf", "logo": "0", "menu_bot": "1", "hint_hold": "150", "unlock_heli": "1", "dump_every": "50",
             "dump_loading": "1", "end_at": "900", "end_kind": "gameover", "end_score": "60000", "name_entry": "touch"},
     "inputs": [(40, "@main/2"), (140, "B"), (200, "@main/3"), (300, "B"), (360, "@main/4"), (460, "B"),
                (520, "@main/7"), (620, "B"), (680, "@main/1"), (780, "@start/2"), (880, "@heli/1"),
                (1150, "START"), (1250, "B")],
     "keep": [("02_main_menu", ("screen", "main", 1)), ("03_top_scores", ("screen", "scores", 1)),
              ("04_options", ("screen", "options", 1)), ("05_information", ("screen", "info", 1)),
              ("06_credits", ("screen", "credits", 1)), ("07_start_game", ("screen", "start", 1)),
              ("08_helicopter_select", ("screen", "heli", 1)), ("09_loading", ("load", -1)),
              ("10_play_level_name", ("after", "playing", 1, 120)), ("11_paused", ("screen", "ingame", 1)),
              ("12_hint", ("screen", "hint", 1)), ("13_play_midfight", ("after", "playing", 1, 350)),
              ("14_game_over", ("screen", "gameover", 1)), ("15_game_over_menu", ("after", "gameover", 1, 300))]},
    {"name": "gulf_gameover", "game": "gulf", "fresh": ["gulf"], "frames": 1300,
     "ini": {"game": "gulf", "logo": "0", "menu_bot": "1", "end_at": "300", "end_kind": "gameover", "end_score": "60000",
             "name_entry": "touch", "dump_every": "50"},
     "inputs": [(40, "@main/1"), (140, "@start/2"), (240, "@heli/1")] + [(f, "@gameover/2") for f in range(600, 1101, 50)],
     "keep": [("16_name_entry", ("screen", "name", 1))]},
]

KILL_PATTERNS = ["^/bin/bash -e /app/bin/azahar-launcher .*{tag}\\.3dsx", "^azahar .*{tag}\\.3dsx"]


def log(msg):
    print("gallery: " + msg, flush=True)


def busy():
    """True while an Azahar emulator runs or a launcher (not a stale one showing a crash dialog) starts one."""
    if subprocess.run(["pgrep", "-f", "^azahar "], capture_output=True).returncode == 0:
        return True
    r = subprocess.run(["pgrep", "-f", "^/bin/bash -e /app/bin/azahar-launcher"], capture_output=True, text=True)
    for pid in r.stdout.split():
        kids = subprocess.run(["pgrep", "-P", pid, "-a"], capture_output=True, text=True).stdout
        if "zenity" not in kids:
            return True
    return False


def pids(pattern):
    r = subprocess.run(["pgrep", "-f", pattern], capture_output=True, text=True)
    return [int(p) for p in r.stdout.split()]


def kill_own(tag):
    for pat in KILL_PATTERNS:
        for p in pids(pat.format(tag=re.escape(tag))):
            try:
                os.kill(p, signal.SIGTERM)
            except OSError:
                pass
        time.sleep(2)
    for p in pids(KILL_PATTERNS[1].format(tag=re.escape(tag))):
        try:
            os.kill(p, signal.SIGKILL)
        except OSError:
            pass
    for p in pids("bwrap .*azahar-launcher .*%s\\.3dsx" % re.escape(tag)):
        try:
            os.kill(p, signal.SIGTERM)
        except OSError:
            pass


def supports(binary, key):
    with open(binary, "rb") as f:
        return key.encode() in f.read()


def legacy_tap(game, screen, item):
    x, y, w, h = LEGACY_RECTS[game][screen][item]
    return "T%d/%d" % (round((x + w / 2) * 0.4), round((y + h / 2) * 0.4))


EXTRA_INI = {}  # --ini: extra keys for every run


def run_ini(run, new_taps, prefix):
    keys = dict(COMMON)
    keys.update(run["ini"])
    keys.update(EXTRA_INI)
    if "mode" not in run["ini"]: keys["mode"] = "play"  # a run may be the bot (as2_fire)
    keys["frames"] = str(run["frames"])
    keys["dump_prefix"] = prefix
    pad, taps = [], []
    for frame, what in run["inputs"]:
        if what.startswith("@"):
            screen, item = what[1:].split("/")
            if new_taps:
                taps.append("%d:@%s/%s" % (frame, screen, item))
            else:
                pad.append("%d:%s" % (frame, legacy_tap(run["game"], screen, int(item))))
        else:
            pad.append("%d:%s" % (frame, what))
    if pad:
        keys["pad_script"] = ",".join(pad)
    if taps:
        keys["ui_taps"] = ",".join(taps)
    lines = ["%s=%s" % (k, v) for k, v in keys.items()]
    return "\n".join(lines) + "\n"


def azahar_run(binary, ini_text, raw, tag, fresh, timeout):
    """One serialised Azahar run; returns True when the app reached its exit line."""
    os.makedirs(raw, exist_ok=True)
    # Free twice, a few seconds apart: another agent's run that is starting has shown by then.
    while True:
        while busy():
            time.sleep(10)
        time.sleep(6)
        if not busy():
            break
    saved_ini = os.path.join(raw, "runini.saved")
    shutil.copy(os.path.join(SD, "run.ini"), saved_ini)
    saved_profiles = []
    for g in fresh:
        p = os.path.join(SD, g, "profile.bin")
        if os.path.exists(p):
            dst = os.path.join(raw, "%s.profile.bin.saved" % g)
            shutil.copy(p, dst)
            saved_profiles.append((p, dst))
            os.remove(p)
    sd_bin = os.path.join(SD, tag + ".3dsx")
    shutil.copy(binary, sd_bin)
    with open(os.path.join(SD, "run.ini"), "w") as f:
        f.write(ini_text)
    with open(os.path.join(raw, "run.ini"), "w") as f:
        f.write(ini_text)
    start = time.time()
    time.sleep(1)
    env = dict(os.environ)
    auth = sorted(x for x in os.listdir("/run/user/%d" % os.getuid()) if x.startswith(".mutter-Xwaylandauth."))
    if auth:
        env["XAUTHORITY"] = "/run/user/%d/%s" % (os.getuid(), auth[0])
    env["DISPLAY"] = env.get("DISPLAY", ":0") or ":0"
    out = open(os.path.join(raw, "azahar.out"), "w")
    proc = subprocess.Popen(["flatpak", "run", "org.azahar_emu.Azahar", "--fullscreen=false", sd_bin], stdout=out,
                            stderr=subprocess.STDOUT, env=env, start_new_session=True)
    ok = False
    sd_log = os.path.join(SD, "log.txt")
    try:
        end = time.time() + timeout
        while time.time() < end:
            time.sleep(5)
            if os.path.exists(sd_log) and os.path.getmtime(sd_log) > start:
                with open(sd_log, "rb") as f:
                    text = f.read()
                    if b"AS3D_CMDBUF max usage" in text or b"selector: Exit" in text:
                        ok = True
                        time.sleep(3)
                        break
    finally:
        kill_own(tag)
        try:
            proc.terminate()
        except OSError:
            pass
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
        out.close()
        if os.path.exists(sd_log):
            shutil.copy(sd_log, os.path.join(raw, "log.txt"))
        for name in os.listdir(SD):
            p = os.path.join(SD, name)
            if name.endswith(".ppm") and os.path.getmtime(p) > start:
                shutil.move(p, os.path.join(raw, name))
        # The SD's run.ini back, unless someone else's run has replaced ours meanwhile.
        try:
            current = open(os.path.join(SD, "run.ini")).read()
        except OSError:
            current = ini_text
        if current == ini_text:
            shutil.copy(saved_ini, os.path.join(SD, "run.ini"))
        else:
            log("  run.ini on the SD is not ours any more: left as it is")
        for p, dst in saved_profiles:
            shutil.copy(dst, p)
        if os.path.exists(sd_bin):
            os.remove(sd_bin)
    return ok


def screen_changes(logtext):
    """[(screen, frame)] in order, from the AS3D_SCREEN lines."""
    return [(m.group(1), int(m.group(2))) for m in re.finditer(r"AS3D_SCREEN name=(\S+) frame=(\d+)", logtext)]


def tap_through(logtext):
    """The tap-through check (docs/3ds-ui-design.md problem 10): every return to play from a menu
    closed by a tap (AS3D_UI_TAP on ingame, hint, gameover, complete) must not open the in-game menu
    again within 30 frames. Returns lines to log."""
    taps = [(m.group(2), int(m.group(1))) for m in re.finditer(r"AS3D_UI_TAP frame=(\d+) screen=(\S+) item=\d+ tapped", logtext)]
    seq = screen_changes(logtext)
    out = []
    for screen, frame in taps:
        if screen not in ("ingame", "hint", "gameover", "complete"):
            continue
        after = [(s, f) for s, f in seq if f >= frame]
        if not after:
            out.append("tap on %s at %d: no screen change after it" % (screen, frame))
            continue
        s1, f1 = after[0]
        reopened = [(s, f) for s, f in after[1:] if f <= f1 + 30 and s in ("ingame", "hint")]
        if s1 == "playing" and reopened:
            out.append("TAP-THROUGH: tap on %s at %d -> playing at %d -> %s at %d" % (screen, frame, f1, reopened[0][0],
                                                                                 reopened[0][1]))
        else:
            out.append("tap on %s at %d -> %s at %d, no menu within 30 frames: ok" % (screen, frame, s1, f1))
    return out


def pick(raw, prefix, logtext, sel):
    """The dump stem (path without _top.ppm) the selector names, or None."""
    files = sorted(f for f in os.listdir(raw) if f.endswith("_top.ppm"))
    kind = sel[0]
    if kind == "screen":
        _, name, k = sel
        hits = [f for f in files if re.match(re.escape(prefix) + r"\d\d_" + re.escape(name) + r"_top\.ppm$", f)]
        return os.path.join(raw, hits[k - 1][:-8]) if len(hits) >= k else None
    if kind == "after":
        _, name, k, offset = sel
        frames = [f for s, f in screen_changes(logtext) if s == name]
        if len(frames) < k:
            return None
        want = frames[k - 1] + offset
        best = None
        for f in files:
            m = re.match(re.escape(prefix) + r"f(\d+)_top\.ppm$", f)
            if m and int(m.group(1)) >= want and (best is None or int(m.group(1)) < best[0]):
                best = (int(m.group(1)), f)
        return os.path.join(raw, best[1][:-8]) if best else None
    if kind == "frame":  # R4.8: the first dump_every dump at or after an absolute frame
        _, want = sel
        best = None
        for f in files:
            m = re.match(re.escape(prefix) + r"f(\d+)_top\.ppm$", f)
            if m and int(m.group(1)) >= want and (best is None or int(m.group(1)) < best[0]):
                best = (int(m.group(1)), f)
        return os.path.join(raw, best[1][:-8]) if best else None
    if kind == "last":  # R4.12: the last dump_every dump (a bot run's frames= counts steps, not dumps)
        best = None
        for f in files:
            m = re.match(re.escape(prefix) + r"f(\d+)_top\.ppm$", f)
            if m and (best is None or int(m.group(1)) > best[0]):
                best = (int(m.group(1)), f)
        return os.path.join(raw, best[1][:-8]) if best else None
    if kind == "load":
        _, k = sel
        hits = [f for f in files if re.match(re.escape(prefix) + r"load\d\d_top\.ppm$", f)]
        if k < 0:  # R4.8: from the end (-1: the last, the mission's own at 1.00)
            return os.path.join(raw, hits[k][:-8]) if len(hits) >= -k else None
        return os.path.join(raw, hits[k - 1][:-8]) if len(hits) >= k else None
    if kind == "sel":
        _, key = sel
        hits = [f for f in files if re.match(re.escape(prefix) + r"sel_\d\d_" + re.escape(key) + r"_top\.ppm$", f)]
        return os.path.join(raw, hits[0][:-8]) if hits else None
    return None


def write_screen(Image, stem, dest_base):
    def save(img, base):
        img.save(base + ".png")
        img.resize((img.width * 2, img.height * 2), Image.NEAREST).save(base + "@2x.png")

    top = Image.open(stem + "_top.ppm").convert("RGB")
    bottom = Image.open(stem + "_bottom.ppm").convert("RGB")
    save(top, dest_base + "_top")
    save(bottom, dest_base + "_bottom")
    w = max(top.width, bottom.width)
    both = Image.new("RGB", (w, top.height + 8 + bottom.height), (24, 24, 24))
    both.paste(top, ((w - top.width) // 2, 0))
    both.paste(bottom, ((w - bottom.width) // 2, top.height + 8))
    save(both, dest_base + "_both")
    return both


def contact_sheet(Image, ImageDraw, cells, title, dest, cols=4):
    if not cells:
        return
    cw, ch = 400, 488 + 16
    rows = (len(cells) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (cw + 6) + 6, 24 + rows * (ch + 6)), (12, 12, 12))
    d = ImageDraw.Draw(sheet)
    d.text((6, 6), title, fill=(255, 220, 120))
    for i, (name, img) in enumerate(cells):
        x = 6 + (i % cols) * (cw + 6)
        y = 24 + (i // cols) * (ch + 6)
        sheet.paste(img, (x, y))
        d.text((x + 2, y + img.height + 2), name, fill=(255, 255, 0))
    sheet.save(dest + ".png")
    sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST).save(dest + "@2x.png")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--bin", required=True)
    ap.add_argument("--label", default=None, help="gallery folder name (default: the binary's build commit)")
    ap.add_argument("--out", default=os.path.join(REPO, "out", "ui3ds", "gallery"))
    ap.add_argument("--only", default="", help="comma-separated run names")
    ap.add_argument("--ini", default="", help="extra run.ini keys for every run, comma-separated key=value (e.g. ui_merge=0)")
    ap.add_argument("--tag", default="gallery", help="the SD file name of the binary (<tag>.3dsx)")
    ap.add_argument("--timeout", type=int, default=240, help="seconds per run")
    ap.add_argument("--convert-only", action="store_true", help="pick and convert the existing raw dumps again")
    a = ap.parse_args()
    for kv in [x for x in a.ini.split(",") if x]:
        k, _, v = kv.partition("=")
        EXTRA_INI[k.strip()] = v.strip()
    from PIL import Image, ImageDraw  # noqa: E402 (PIL only where pictures are made)

    binary = os.path.abspath(a.bin)
    label = a.label
    if not label:
        try:
            label = subprocess.run(["git", "-C", REPO, "rev-parse", "--short", "HEAD"], capture_output=True,
                                   text=True).stdout.strip() or "unknown"
        except OSError:
            label = "unknown"
    root = os.path.join(a.out, label)
    only = set(x for x in a.only.split(",") if x)
    new_taps = supports(binary, "ui_taps")
    log("binary %s, label %s, %s taps" % (binary, label, "ui_taps" if new_taps else "legacy pixel"))
    t0 = time.time()
    sheets = {}
    for run in RUNS:
        if only and run["name"] not in only:
            continue
        raw = os.path.join(root, "raw", run["name"])
        prefix = "g_%s_" % run["name"]
        if not a.convert_only:
            if os.path.isdir(raw):
                shutil.rmtree(raw)
            t = time.time()
            for attempt in range(3):
                ok = azahar_run(binary, run_ini(run, new_taps, prefix), raw, a.tag, run["fresh"], a.timeout)
                lg = os.path.join(raw, "log.txt")
                mine = os.path.exists(lg) and prefix.encode() in open(lg, "rb").read()
                if mine or any(f.startswith(prefix) for f in os.listdir(raw)):
                    break
                log("%s: the log is another run's (a concurrent Azahar run?), again" % run["name"])
                shutil.rmtree(raw)
            log("%s: %s in %.0f s" % (run["name"], "ok" if ok else "TIMEOUT (killed)", time.time() - t))
        logtext = open(os.path.join(raw, "log.txt"), errors="replace").read() if os.path.exists(
            os.path.join(raw, "log.txt")) else ""
        for line in tap_through(logtext):
            log("  %s: %s" % (run["name"], line))
        game_dir = os.path.join(root, run["game"])
        os.makedirs(game_dir, exist_ok=True)
        for name, sel in run["keep"]:
            stem = pick(raw, prefix, logtext, sel) if os.path.isdir(raw) else None
            if not stem or not os.path.exists(stem + "_bottom.ppm"):
                log("  %s/%s: no dump (%s)" % (run["game"], name, sel))
                continue
            write_screen(Image, stem, os.path.join(game_dir, name))
            sheets[run["game"]] = True
    # One sheet per game from every screen in its folder (runs of earlier invocations included).
    for game in sheets:
        game_dir = os.path.join(root, game)
        cells = [(f[:-len("_both.png")], Image.open(os.path.join(game_dir, f)).convert("RGB"))
                 for f in sorted(os.listdir(game_dir)) if f.endswith("_both.png")]
        contact_sheet(Image, ImageDraw, cells, "%s  %s  (top 400x240 over bottom 320x240, 1x)" % (game, label),
                      os.path.join(root, game + "_sheet"))
        log("%s: %d screens, sheet %s" % (game, len(cells), os.path.join(root, game + "_sheet.png")))
    log("done in %.0f s; left: %s" % (time.time() - t0, "azahar running!" if pids("^azahar .*%s" % a.tag) else "no own Azahar"))


if __name__ == "__main__":
    sys.exit(main())
