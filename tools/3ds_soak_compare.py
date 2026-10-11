#!/usr/bin/env python3
"""tools/3ds_soak_compare.py <dir or log.txt>: the end states of a 3DS run against the desktop
simulation (docs/3ds-port.md "Soak mode and the log push (round 2, P7)").

For every stage of the log (an AS3D_SOAK_STAGE line of mode=soak, or the config line of a
mode=bot / mode=sim run: game, level, frames, bot_weapons) the end-state lines that follow
("level N: F frames, map_pos X, list entities E, slots S (max M)", "created C, freed F, refused R,
script errors S, stalls T") are compared with

    as3d_sim --game G --level N --frames F --bot [--bot-weapons]

on map_pos, list entities, slots, created, freed, refused and script errors. One PASS/FAIL line
per stage; exit status 1 if any stage fails or has no end state.

  --sim PATH     the binary (default $AS3D_SIM, else build/apps/sim_tool/as3d_sim of this checkout)
  --data ROOT    the game data (default $AS3D_DATA_ROOT, else this checkout)
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

STAGE_RE = re.compile(r"AS3D_SOAK_STAGE n=(\d+) of=\d+ game=(\w+) level=(\d+) eyes=(\w+) slider=[\d.]+ frames=(\d+) "
                      r"bot_weapons=(\d)")
CONFIG_RE = re.compile(r"openairstrike 3ds: mode=(bot|sim) game=(\w+) level=(\d+) frames=(\d+) perf=\d bot_weapons=(\d)")
LEVEL_3DS_RE = re.compile(r"level (\d+): (\d+) frames, map_pos (-?[\d.]+), list entities (\d+), slots (\d+) \(max \d+\)")
LEVEL_SIM_RE = re.compile(r"level (\d+): (\d+) frames, map_pos (-?[\d.]+), list entities (\d+) \(max \d+\), slots (\d+) "
                          r"\(max \d+\)")
CREATED_RE = re.compile(r"created (\d+), freed (\d+), refused (\d+), script errors (\d+), stalls (\d+)")


def parse_stages(log_path):
    """The stages of the log with their end states (None when the stage has none)."""
    stages = []
    cur = None
    soak = False
    for line in open(log_path, errors="replace"):
        m = STAGE_RE.search(line)
        if m:
            soak = True
            cur = {"name": "%s:%s:%s" % (m.group(2), m.group(3), m.group(4)), "n": int(m.group(1)), "game": m.group(2),
                   "level": int(m.group(3)), "frames": int(m.group(5)), "weapons": m.group(6) == "1", "end": None}
            stages.append(cur)
            continue
        m = CONFIG_RE.search(line)
        if m and not soak:
            cur = {"name": "%s:%s:%s" % (m.group(2), m.group(3), m.group(1)), "n": len(stages) + 1, "game": m.group(2),
                   "level": int(m.group(3)), "frames": int(m.group(4)), "weapons": m.group(5) == "1", "end": None}
            stages.append(cur)
            continue
        if cur is None:
            continue
        m = LEVEL_3DS_RE.search(line)
        if m and cur["end"] is None:
            cur["end"] = {"steps": int(m.group(2)), "map_pos": m.group(3), "list": int(m.group(4)),
                          "slots": int(m.group(5))}
            continue
        m = CREATED_RE.search(line)
        if m and cur["end"] is not None and "created" not in cur["end"]:
            cur["end"].update(created=int(m.group(1)), freed=int(m.group(2)), refused=int(m.group(3)),
                              errors=int(m.group(4)))
    return stages


def run_sim(sim, data, game, level, steps, weapons):
    cmd = [sim, "--game", game, "--level", str(level), "--frames", str(steps), "--bot"]
    if weapons:
        cmd.append("--bot-weapons")
    env = dict(os.environ, AS3D_DATA_ROOT=data)
    proc = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=600)
    out = proc.stdout
    end = {}
    for line in out.splitlines():
        m = LEVEL_SIM_RE.search(line)
        if m:
            end.update(steps=int(m.group(2)), map_pos=m.group(3), list=int(m.group(4)), slots=int(m.group(5)))
        m = CREATED_RE.search(line)
        if m:
            end.update(created=int(m.group(1)), freed=int(m.group(2)), refused=int(m.group(3)), errors=int(m.group(4)))
    if not end:
        err = proc.stderr.strip().splitlines()
        return end, " ".join(cmd) + (" -> " + err[-1] if err else "")
    return end, " ".join(cmd)


def compare(log_path, sim, data, quiet=False):
    stages = parse_stages(log_path)
    if not stages:
        print("no bot stage in %s" % log_path)
        return 1
    if not os.access(sim, os.X_OK):
        print("as3d_sim not built: %s (cmake --build build --target as3d_sim)" % sim)
        return 2
    fails = 0
    keys = ("map_pos", "list", "slots", "created", "freed", "refused", "errors")
    for st in stages:
        label = "stage %d %s frames=%d weapons=%d" % (st["n"], st["name"], st["frames"], 1 if st["weapons"] else 0)
        end = st["end"]
        if not end or "created" not in end:
            print("FAIL  %s: no end state in the log (the stage did not finish)" % label)
            fails += 1
            continue
        ref, cmd = run_sim(sim, data, st["game"], st["level"], end["steps"], st["weapons"])
        if not ref:
            print("FAIL  %s: the sim gave no end state (%s)" % (label, cmd))
            fails += 1
            continue
        diff = [k for k in keys if str(end.get(k)) != str(ref.get(k))]
        summary = "map_pos %s, entities %s, slots %s, created %s, freed %s" % (
            end["map_pos"], end["list"], end["slots"], end["created"], end["freed"])
        if diff:
            fails += 1
            print("FAIL  %s steps=%d: %s" % (label, end["steps"], ", ".join(
                "%s 3ds=%s sim=%s" % (k, end.get(k), ref.get(k)) for k in diff)))
        else:
            print("PASS  %s steps=%d: %s (= %s)" % (label, end["steps"], summary, "as3d_sim" if quiet else cmd))
    print("end states: %d of %d stages equal to the desktop sim" % (len(stages) - fails, len(stages)))
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("path", help="a run directory (its log.txt) or a log file")
    ap.add_argument("--sim", default=os.environ.get("AS3D_SIM", os.path.join(ROOT, "build/apps/sim_tool/as3d_sim")))
    ap.add_argument("--data", default=os.environ.get("AS3D_DATA_ROOT", ROOT))
    a = ap.parse_args()
    log = os.path.join(a.path, "log.txt") if os.path.isdir(a.path) else a.path
    return compare(log, a.sim, a.data)


if __name__ == "__main__":
    sys.exit(main())
