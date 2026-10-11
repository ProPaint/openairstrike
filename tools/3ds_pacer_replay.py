#!/usr/bin/env python3
"""Offline replay of the 3DS frame pacer's rules (pace=auto, mono) on a console perf.csv.

Usage:
  tools/3ds_pacer_replay.py <stage dir or perf.csv> [...] [--margins 1,2,3] [--lags 3] [--kick-at MS]
  tools/3ds_pacer_replay.py --legacy <stage dir or perf.csv> [...] [--threshold-ms 20.5] [--seed 1]

By default (R4.2) a frame simulator replays the loop frame by frame with each row's costs, for the
catch-up rules of apps/3ds/source/catch_up.h (immediate, deferred) times the drop rules, and prints
per pair the share at 60 Hz, drops, misses, GPU-late frames, stutters, two-step frames and how many
of them were late, the frames behind, repaid and forced steps, steps per frame (see "The frame
simulator" below and docs/3ds-port.md "Mono at 60 and the level start (round 4, R4.2)"). --kick-at
is a what-if of the GPU starting the frame that many ms after C3D_FrameBegin. --legacy is R3.3's
study, described next.

For each stage (a directory with perf.csv, e.g. out/3ds/<run>/stages/03-as3d-2-mono/, or a perf.csv)
it rebuilds which displayed frames would have missed their vblank at 60 Hz, then replays candidate
drop rules (60 -> 30 Hz) and return rules (30 -> 60 Hz) and prints, per rule: the share of the frames
after the 300-frame grace spent at 60 Hz, the number of drops to 30 Hz and the stutters (frames that
miss their vblank while at 60 Hz: a frame shown for 33 ms). docs/3ds-port.md "Third console soak"
has the study; apps/3ds/source/pace_rule.h the rules the console runs (run.ini pace_rule=).

The method and its limits:
  * A frame at 60 Hz missed its vblank when its loop period (sync + step + prepare + wait + submit +
    end + after: the columns add up to the frame's period, checked against the console's own count of
    misses) is over --threshold-ms (20.5: the periods cluster at 16.7 and at 33). These are the
    console's real misses.
  * A frame the console drew at 30 Hz says nothing about whether it would have fit at 60. Two models:
    "sum" (the brief's): it misses when its CPU path (step per simulation step + prepare + submit +
    end) plus the GPU tail (gpu_ms of the next row: C3D_GetDrawingTime reports the frame before) is
    over 16.7 ms. At 60 Hz this predictor is far too pessimistic (the GPU runs during the CPU's
    recording under gpu_kick): the tool prints its confusion against the real misses. "calibrated":
    the 60 Hz frames of the same stage give the miss rate per 1 ms bucket of that sum, and a 30 Hz
    frame misses with that probability (a seeded draw, the same for every rule). The real misses come
    in part at a fixed period (32 frames, the audio feeder's cadence; the tool prints the gaps), which
    a per-frame bucket rate cannot reproduce in time, only in rate.
  * The probe frames of mode=soak (every 30th frame and the next, excused by the console's pacer)
    and the first 300 frames (the grace) are not counted, as on the console.
  * A replayed rule changes which frames run at 30 Hz but not the simulation: the console steps twice
    in a 30 Hz frame, here every frame stays one row. Shares are of rows, not of seconds.
"""
import argparse
import csv
import os
import random
import sys

GRACE = 300
PROBE_EVERY = 30
WINDOW = 120


def load(path):
    p = os.path.join(path, "perf.csv") if os.path.isdir(path) else path
    with open(p) as f:
        rows = list(csv.DictReader(f))
    return p, rows


def fnum(r, k):
    try:
        return float(r[k])
    except (KeyError, ValueError):
        return 0.0


def frame_model(rows, threshold, seed):
    """Per row: (counted, miss_real_or_None, miss_sum, miss_cal, fits30)."""
    n = len(rows)
    period = [sum(fnum(r, k) for k in ("sync_ms", "step_ms", "prepare_ms", "wait_ms", "submit_ms", "end_ms", "after_ms"))
              for r in rows]
    cpu = [fnum(r, "step_ms") / max(1, int(fnum(r, "sim_steps") or 1)) + fnum(r, "prepare_ms") + fnum(r, "submit_ms") +
           fnum(r, "end_ms") for r in rows]
    gpu_next = [fnum(rows[i + 1], "gpu_ms") if i + 1 < n else fnum(rows[i], "gpu_ms") for i in range(n)]
    load_ms = [cpu[i] + gpu_next[i] for i in range(n)]
    hz = [int(fnum(r, "render_hz") or 60) for r in rows]
    frame = [int(fnum(r, "frame")) for r in rows]
    counted = [i >= GRACE and frame[i] % PROBE_EVERY not in (0, 1) for i in range(n)]
    real = [period[i] > threshold if hz[i] == 60 else None for i in range(n)]
    # The calibration: miss rate per 1 ms bucket of load_ms over the counted 60 Hz frames.
    buckets = {}
    for i in range(n):
        if counted[i] and real[i] is not None:
            b = int(load_ms[i])
            hit, tot = buckets.get(b, (0, 0))
            buckets[b] = (hit + (1 if real[i] else 0), tot + 1)
    rng = random.Random(seed)
    out = []
    for i in range(n):
        b = int(load_ms[i])
        hit, tot = buckets.get(b, (0, 0))
        if tot < 20:  # a thin bucket: the rate of all frames at or above it
            hit = sum(h for k, (h, t) in buckets.items() if k >= b)
            tot = sum(t for k, (h, t) in buckets.items() if k >= b)
        rate = hit / tot if tot else (1.0 if load_ms[i] > 16.7 else 0.0)
        draw = rng.random()
        miss_sum = real[i] if real[i] is not None else load_ms[i] > 16.7
        miss_cal = real[i] if real[i] is not None else draw < rate
        fits30 = cpu[i] < 13.0 and gpu_next[i] < 14.0
        out.append((counted[i], miss_sum, miss_cal, fits30))
    conf = [0, 0, 0, 0]  # tp fn fp tn of the sum predictor on the real 60 Hz frames
    for i in range(n):
        if counted[i] and real[i] is not None:
            pred = load_ms[i] > 16.7
            conf[(0 if pred else 1) if real[i] else (2 if pred else 3)] += 1
    misses = [i for i in range(n) if counted[i] and real[i]]
    gaps = {}
    for a, b in zip(misses, misses[1:]):
        gaps[b - a] = gaps.get(b - a, 0) + 1
    return out, conf, gaps, sum(1 for i in range(n) if counted[i] and real[i] is not None), len(misses)


# Drop rules: called per counted frame at 60 Hz with its miss; True = go to 30 Hz.
class Window:
    def __init__(self, need):
        self.need = need
        self.ring = [0] * WINDOW
        self.at = 0
        self.count = 0

    def reset(self):
        self.ring = [0] * WINDOW
        self.at = 0
        self.count = 0

    def frame(self, miss):
        m = 1 if miss else 0
        self.count += m - self.ring[self.at]
        self.ring[self.at] = m
        self.at = (self.at + 1) % WINDOW
        return self.count >= self.need


class Consecutive:
    def __init__(self, need):
        self.need = need
        self.run = 0

    def reset(self):
        self.run = 0

    def frame(self, miss):
        self.run = self.run + 1 if miss else 0
        return self.run >= self.need


class Never:
    def reset(self):
        pass

    def frame(self, miss):
        return False


DROP_RULES = {
    "12/120": lambda: Window(12),
    "24/120": lambda: Window(24),
    "30/120": lambda: Window(30),
    "3-in-a-row": lambda: Consecutive(3),
    "never": lambda: Never(),
}
# Return rules: (fitting frames needed, double after a fall within 600 frames of the return).
RETURN_RULES = {"fit60x2": (60, True), "fit30": (30, False), "fit120": (120, False)}


def replay(model, drop_name, ret_name, use_cal):
    drop = DROP_RULES[drop_name]()
    need0, doubling = RETURN_RULES[ret_name]
    need = need0
    hz = 60
    streak = 0
    promoted = None
    at60 = at30 = drops = stutters = 0
    for i, (counted, miss_sum, miss_cal, fits30) in enumerate(model):
        miss = miss_cal if use_cal else miss_sum
        if not counted:
            continue
        if hz == 60:
            at60 += 1
            if miss:
                stutters += 1
            if drop.frame(miss):
                if doubling and promoted is not None and i - promoted < 600:
                    need = min(need * 2, 3600)
                hz = 30
                drops += 1
                streak = 0
                drop.reset()
        else:
            at30 += 1
            streak = streak + 1 if fits30 and not miss else 0
            if streak >= need:
                hz = 60
                promoted = i
                streak = 0
    total = at60 + at30
    return 100.0 * at60 / total if total else 0.0, drops, stutters, total


# ---------------------------------------------------------------------------------------------
# The frame simulator (R4.2, the default output): replays the console's loop frame by frame with
# the costs each row recorded, so that a rule which changes how many steps a frame runs (the
# catch-up) or the render rate (the pacer) changes when the next frames start.
#
# Per frame i (one perf.csv row): after the previous frame's sync the CPU runs that frame's
# wait + submit + end + after (its "fixed" path), then frame i's steps (run x the row's cost per
# step) and prepare: the arrival A at C3D_FrameSync, in ms after the previous sync. The sync waits
# for the next vblank of the rate: A <= T_LATE (16.4 ms, fitted on the fourth soak: 0 false
# alarms, 10 of 191 late frames under it, all at the heap sample every 600 frames) is on time,
# else one more vblank per 16.7 ms. The wait at C3D_FrameBegin is what is left of the previous
# frame's GPU (its wait + submit + end + GPU tail, gpu_ms of the next row) after the interval
# (exact on the soak: |error| > 1 ms in 4 of 10053 frames). The picture of frame i is shown at the
# first vblank of the rate after its GPU end (citro3d swaps the buffers when the queue finishes,
# the screen takes them at the next vblank): a frame whose GPU ends after the next vblank is
# shown late although its sync was on time (a "GPU-late" frame the console's pacer does not see).
# The catch-up rules mirror apps/3ds/source/catch_up.h, the drop rules pace_rule.h, the return
# rule ctr_loop.cpp updatePacer (60 frames whose CPU path < 13 ms and GPU from the kick < 14 ms,
# doubled after a fall back within 600 frames). Not modelled: a step's cost when it runs in another
# frame than the console ran it (the row's cost per step is used), the excuses of SD writes and
# late loads (the probe frames and the grace are excused as on the console).
VB = 1000.0 / 60.0
T_LATE = 16.4


class CatchUp:
    """apps/3ds/source/catch_up.h CatchUpState."""

    def __init__(self, mode, max_lag=3, margin=2.0):
        self.mode = mode
        self.max_lag = max_lag
        self.margin = margin
        self.owed = 0

    def plan(self, due, base, cap, fixed, prep, step):
        self.owed += max(1, due)
        keep = 0
        repaid = forced = 0
        if self.mode == "immediate":
            run = min(self.owed, cap)
        else:
            keep = self.max_lag
            run = min(self.owed, min(base, cap))
            budget = VB * base - self.margin
            while run < cap and run < self.owed:
                force = self.owed - run > keep
                room = step > 0 and fixed + prep + (run + 1) * step <= budget
                if not force and not room:
                    break
                if force:
                    forced += 1
                else:
                    repaid += 1
                run += 1
        dropped = max(0, self.owed - run - keep)
        self.owed -= run + dropped
        return run, dropped, repaid, forced


def sim_rows(rows, kick_at=None):
    """The per-row costs the simulator needs. kick_at (ms after C3D_FrameBegin): a what-if of the GPU
    starting the top screen's commands that early (a mid-frame kick in mono) instead of after the top
    screen: the GPU's work from the kick (gpu_kick_ms, measured from today's kick, so pure GPU work)
    then ends at kick_at + work, or 1 ms after the recording when the CPU is the slower."""
    n = len(rows)
    out = []
    last_c = 0.0
    for i, r in enumerate(rows):
        steps = int(fnum(r, "sim_steps"))
        c = fnum(r, "step_ms") / steps if steps > 0 else last_c
        last_c = c
        nxt = rows[i + 1] if i + 1 < n else r
        out.append({
            "frame": int(fnum(r, "frame")),
            "submit": fnum(r, "submit_ms") + fnum(r, "end_ms"),
            "after": fnum(r, "after_ms"),
            "prep": fnum(r, "prepare_ms"),
            "c": c,
            "tail": fnum(nxt, "gpu_ms") if kick_at is None else max(
                1.0, kick_at + (fnum(nxt, "gpu_kick_ms") or fnum(nxt, "gpu_ms")) - fnum(r, "submit_ms") - fnum(r, "end_ms")),
            "kick": fnum(nxt, "gpu_kick_ms") or fnum(nxt, "gpu_ms"),
        })
    return out


def simulate(cost, catchup="immediate", drop_name="24/120", ret_name="fit60x2", max_lag=3, margin=2.0, start_hz=60,
             gpu_counts=False):
    """One pass over the rows; returns a dict of counts. gpu_counts: a GPU-late frame counts as a miss for the drop rule."""
    import math
    cu = CatchUp(catchup, max_lag, margin)
    drop = DROP_RULES[drop_name]()
    need0, doubling = RETURN_RULES[ret_name]
    need = need0
    hz = start_hz
    streak = 0
    promoted = None
    s = {"frames": 0, "at60": 0, "at30": 0, "drops": 0, "miss60": 0, "gpu_late60": 0, "hitch60": 0, "miss30": 0,
         "steps": {0: 0, 1: 0, 2: 0, 3: 0}, "lag_frames": 0, "lag_max": 0, "repaid": 0, "forced": 0, "dropped": 0,
         "late_frames60": 0, "two_step_late": 0, "two_step": 0, "first_late": 0}
    fixed_prev = 0.0  # wait + submit + end + after of the previous frame
    prep_prev = 0.0
    c_prev = 0.0
    gpu_end_prev = 0.0  # the previous frame's GPU end, ms after its sync
    present_prev = None
    t_sync = 0.0
    due = 1
    late_prev = False
    for i, k in enumerate(cost):
        base = 1 if hz == 60 else 2
        cap = 2 if hz == 60 else 4
        run, dropped, repaid, forced = cu.plan(due if i else base, base, cap, fixed_prev, prep_prev, c_prev)
        arrive = fixed_prev + run * k["c"] + k["prep"]
        if i == 0:
            vb = base
        elif hz == 60:
            vb = 1 if arrive <= T_LATE else 1 + int(math.ceil((arrive - T_LATE) / VB))
        else:
            over = arrive - (T_LATE + VB)
            vb = 2 if over <= 0 else 2 + 2 * int(math.ceil(over / (2 * VB)))
        t_sync += vb * VB
        wait = max(0.0, gpu_end_prev - vb * VB) if i else 0.0
        gpu_end = wait + k["submit"] + k["tail"]
        present = t_sync + max(base, int(math.ceil(gpu_end / VB - 1e-9)) if base == 1 else 2 * int(math.ceil(gpu_end / (2 * VB) - 1e-9))) * VB
        counted = i >= GRACE and k["frame"] % PROBE_EVERY not in (0, 1)
        late = vb > base
        gpu_late = gpu_end > base * VB
        hitch = present_prev is not None and present - present_prev > base * VB + 0.5
        present_prev = present
        s["steps"][min(run, 3)] += 1
        s["repaid"] += repaid
        s["forced"] += forced
        s["dropped"] += dropped
        if cu.owed > 0:
            s["lag_frames"] += 1
        s["lag_max"] = max(s["lag_max"], cu.owed)
        if counted:
            s["frames"] += 1
            if hz == 60:
                s["at60"] += 1
                if run >= 2:
                    s["two_step"] += 1
                    s["two_step_late"] += 1 if late else 0
                if late:
                    s["miss60"] += 1
                    if not late_prev:
                        s["first_late"] += 1
                if gpu_late and not late:
                    s["gpu_late60"] += 1
                if hitch:
                    s["hitch60"] += 1
                miss_for_rule = late or (gpu_counts and gpu_late)
                if drop.frame(miss_for_rule):
                    if doubling and promoted is not None and i - promoted < 600:
                        need = min(need * 2, 3600)
                    hz = 30
                    s["drops"] += 1
                    streak = 0
                    drop.reset()
            else:
                s["at30"] += 1
                if late:
                    s["miss30"] += 1
                cpu = k["c"] + k["prep"] + k["submit"]
                streak = streak + 1 if (cpu < 13.0 and k["kick"] < 14.0) else 0
                if streak >= need:
                    hz = 60
                    promoted = i
                    streak = 0
                    drop.reset()
        late_prev = late
        fixed_prev = wait + k["submit"] + k["after"]
        prep_prev = k["prep"]
        c_prev = k["c"]
        gpu_end_prev = gpu_end
        due = vb
    return s


def print_sim(path, rows, catchups, drops, kick_at=None):
    cost = sim_rows(rows, kick_at)
    print("== %s (frame simulator%s)" % (path, "" if kick_at is None else ", what-if: the GPU kicked %.1f ms into the frame" % kick_at))
    print("   %-26s %-8s %7s %5s %7s %8s %8s %7s %6s %6s %5s %5s %8s" % (
        "catchup", "drop", "at60", "drops", "miss60", "gpulate", "hitch60", "2st/late", "lagfr", "lagmax", "rep", "forc",
        "steps0/1/2/3"))
    for cname, cargs in catchups:
        for d in drops:
            s = simulate(cost, drop_name=d, **cargs)
            share = 100.0 * s["at60"] / max(1, s["frames"])
            print("   %-26s %-8s %6.1f%% %5d %7d %8d %8d %3d/%-4d %6d %6d %5d %5d %s" % (
                cname, d, share, s["drops"], s["miss60"], s["gpu_late60"], s["hitch60"], s["two_step"], s["two_step_late"],
                s["lag_frames"], s["lag_max"], s["repaid"], s["forced"],
                "/".join(str(s["steps"][j]) for j in range(4))))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--threshold-ms", type=float, default=20.5)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--legacy", action="store_true", help="the R3.3 study (real misses plus a model of the 30 Hz rows) instead of the frame simulator")
    ap.add_argument("--margins", default="1,2,3", help="catch-up margins (ms) to try with catchup=deferred")
    ap.add_argument("--lags", default="3", help="catch-up lag bounds to try with catchup=deferred")
    ap.add_argument("--kick-at", type=float, default=None, help="what-if: the GPU starts the frame this many ms after C3D_FrameBegin")
    a = ap.parse_args()
    if not a.legacy:
        catchups = [("immediate", {"catchup": "immediate"})]
        for lag in [int(x) for x in a.lags.split(",") if x]:
            for m in [float(x) for x in a.margins.split(",") if x]:
                catchups.append(("deferred lag=%d margin=%.1f" % (lag, m), {"catchup": "deferred", "max_lag": lag, "margin": m}))
        for path in a.paths:
            p, rows = load(path)
            if not rows or int(fnum(rows[0], "eyes") or 1) != 1:
                print("== %s: not a mono stage, skipped" % p)
                continue
            print_sim(p, rows, catchups, list(DROP_RULES), a.kick_at)
        return 0
    for path in a.paths:
        p, rows = load(path)
        if not rows or int(fnum(rows[0], "eyes") or 1) != 1:
            print("== %s: not a mono stage (eyes=%s), skipped" % (p, rows[0].get("eyes") if rows else "?"))
            continue
        model, conf, gaps, n60, nmiss = frame_model(rows, a.threshold_ms, a.seed)
        top = sorted(gaps.items(), key=lambda kv: -kv[1])[:6]
        print("== %s" % p)
        print("rows %d, counted 60 Hz rows %d, real misses %d (%.2f%%); gaps between misses (frames: count) %s" % (
            len(rows), n60, nmiss, 100.0 * nmiss / max(1, n60), ", ".join("%d: %d" % kv for kv in top)))
        print("the 'sum' predictor (cpu + gpu tail > 16.7 ms) on the real 60 Hz frames: tp %d fn %d fp %d tn %d" % tuple(conf))
        for use_cal, label in ((True, "calibrated"), (False, "sum")):
            print("-- 30 Hz frames by the %s model" % label)
            print("   %-11s %-8s %8s %6s %9s %10s" % ("drop", "return", "at60", "drops", "stutters", "stutter/s60"))
            for d in DROP_RULES:
                for r in RETURN_RULES:
                    if d == "never" and r != "fit60x2":
                        continue
                    share, drops, stut, total = replay(model, d, r, use_cal)
                    at60 = share * total / 100.0
                    print("   %-11s %-8s %7.1f%% %6d %9d %10.2f" % (d, r if d != "never" else "-", share, drops, stut,
                                                                   stut / (at60 / 60.0) if at60 else 0.0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
