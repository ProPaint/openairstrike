#!/usr/bin/env python3
"""Offline replay of the 3DS frame pacer's rules (pace=auto, mono) on a console perf.csv.

Usage:
  tools/3ds_pacer_replay.py <stage dir or perf.csv> [...] [--threshold-ms 20.5] [--seed 1]

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


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--threshold-ms", type=float, default=20.5)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
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
