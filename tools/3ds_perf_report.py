#!/usr/bin/env python3
"""tools/3ds_perf_report.py <dir>: prints the summary of a 3DS run (log.txt with its PERF_SUMMARY
lines, perf.csv) and PASS/FAIL against the New 3DS acceptance thresholds of milestone M1.

The thresholds are parameters (defaults: the M1 gate of the port plan):
  --slider {0,max}      which gate applies: slider 0 (CPU avg <= 8, GPU p95 <= 11, over <= 1%)
                        or slider max (GPU p95 <= 15, over <= 3%); default 0
  --load-ms 3000        mission load time
  --max-frame-ms 50     no frame above this after frame --after-frame (default 60)
  --cpu-avg / --gpu-p95 / --over-pct / --linear-mb 20 / --tex-vram-kb 4300 (4.2 MB)
Frames over: a frame period above 17.0 ms (a missed vblank), as the log's PERF_SUMMARY counts it.

A check whose column is all zero (the renderer columns before the PICA backend and the frame
composer fill them) is reported SKIP, not PASS. Exit status 1 if any check FAILs.
"""
import argparse
import csv
import os
import re
import statistics
import sys


def pct(values, p):
    if not values:
        return 0.0
    v = sorted(values)
    return v[int(p * (len(v) - 1))]


def parse_log(path):
    out = {}
    if not os.path.exists(path):
        return out
    for line in open(path, errors="replace"):
        m = re.search(r"PERF_SUMMARY (.*)", line)
        if m:
            # "frames=N steps=N", "step_ms avg=.. p95=..": the group is the first word, or the
            # first key when the line starts with key=value.
            pairs = re.findall(r"(\w+)=(-?[\d.]+)", m.group(1))
            first = m.group(1).split()[0]
            group = first.split("=")[0]
            out[group] = {k: float(v) for k, v in pairs}
        m = re.search(r"mission \d+ loaded in (\d+) ms", line)
        if m:
            out.setdefault("load", {})["ms"] = float(m.group(1))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dir")
    ap.add_argument("--slider", choices=["0", "max"], default="0")
    ap.add_argument("--load-ms", type=float, default=3000)
    ap.add_argument("--max-frame-ms", type=float, default=50)
    ap.add_argument("--after-frame", type=int, default=60)
    ap.add_argument("--cpu-avg", type=float, default=8)
    ap.add_argument("--gpu-p95", type=float, default=None)
    ap.add_argument("--over-pct", type=float, default=None)
    ap.add_argument("--linear-mb", type=float, default=20)
    ap.add_argument("--tex-vram-kb", type=float, default=4300)
    a = ap.parse_args()
    if a.gpu_p95 is None:
        a.gpu_p95 = 11 if a.slider == "0" else 15
    if a.over_pct is None:
        a.over_pct = 1 if a.slider == "0" else 3

    log = parse_log(os.path.join(a.dir, "log.txt"))
    rows = []
    csv_path = os.path.join(a.dir, "perf.csv")
    if os.path.exists(csv_path):
        with open(csv_path) as f:
            rows = list(csv.DictReader(f))

    def col(name):
        out = []
        for r in rows:
            try:
                out.append(float(r[name]))
            except (KeyError, ValueError, TypeError):
                pass
        return out

    results = []

    def check(name, value, ok, unit="", skip=False, limit=""):
        status = "SKIP" if skip else ("PASS" if ok else "FAIL")
        results.append(status)
        print("%-4s  %-34s %s%s %s" % (status, name, "n/a" if value is None else "%.2f" % value, unit, limit))

    print("== %s" % a.dir)
    if "frames" in log:
        f = log["frames"]
        print("frames %d, steps %d, dropped %d" % (f.get("frames", 0), f.get("steps", 0), f.get("dropped", 0)))
    for key in ("step_ms", "frame_ms"):
        if key in log:
            s = log[key]
            print("%-9s avg %.3f  p95 %.3f  max %.3f" % (key, s.get("avg", 0), s.get("p95", 0), s.get("max", 0)))
    if "over_ms" in log:
        o = log["over_ms"]
        print("frames over %.1f ms: %d of %d" % (o.get("over_ms", 17), o.get("over", 0), o.get("of", 0)))
    if "heap_kb" in log:
        print("heap high-water %d KB (arena %d KB)" % (log["heap_kb"].get("high", 0), log["heap_kb"].get("arena", 0)))
    if not log:
        print("no log.txt or no PERF_SUMMARY block in it")
    print()

    # Checks.
    load = log.get("load_ms", {}).get("load_ms", log.get("load", {}).get("ms"))
    check("mission load", load, load is not None and load <= a.load_ms, " ms", load is None, "<= %g" % a.load_ms)
    lazy = log.get("lazy_loads", {}).get("max")
    lazy_col = col("lazy_loads")
    if lazy_col:
        lazy = max(lazy_col)
    check("lazy_loads", lazy, lazy == 0, "", lazy is None, "== 0")

    # No hitch after the warm-up: frame period from the CSV is not recorded, so use the log's
    # max and the CSV step time.
    steps = [float(r["step_ms"]) for r in rows if int(r["frame"]) >= a.after_frame] if rows else []
    if steps:
        check("max step after frame %d" % a.after_frame, max(steps), max(steps) <= a.max_frame_ms, " ms", False,
              "<= %g" % a.max_frame_ms)
    else:
        check("max step after frame %d" % a.after_frame, None, False, "", True)

    cpu, gpu = col("cpu_ms"), col("gpu_ms")
    eyes = col("eyes")
    renderer_filled = any(v > 0 for v in col("verts")) or any(v > 0 for v in col("draws"))
    if cpu and renderer_filled:
        avg = statistics.mean(cpu)
        check("CPU avg (slider %s)" % a.slider, avg, avg <= a.cpu_avg, " ms", False, "<= %g" % a.cpu_avg)
    else:
        check("CPU avg (slider %s)" % a.slider, None, False, "", True, "(renderer columns still zero)")
    if gpu and renderer_filled:
        p = pct(gpu, 0.95)
        check("GPU p95 (slider %s)" % a.slider, p, p <= a.gpu_p95, " ms", False, "<= %g" % a.gpu_p95)
    else:
        check("GPU p95 (slider %s)" % a.slider, None, False, "", True, "(renderer columns still zero)")
    ov = log.get("over_ms")
    if ov and ov.get("of", 0) > 0:
        frac = 100.0 * ov.get("over", 0) / ov["of"]
        check("frames over 16.7 ms (slider %s)" % a.slider, frac, frac <= a.over_pct, " %", not renderer_filled,
              "<= %g" % a.over_pct)
    else:
        check("frames over 16.7 ms", None, False, "", True)

    lin = col("linear_free_kb")
    if lin and min(lin) > 0:
        used = (32768.0 - min(lin)) / 1024.0  # linear heap of the 3DS app: 32 MB (docs/3ds-port.md)
        check("linear used (peak)", used, used <= a.linear_mb, " MB", False, "<= %g" % a.linear_mb)
    else:
        check("linear used (peak)", None, False, "", True)
    tv = col("tex_vram_kb")
    if tv and max(tv) > 0:
        check("texture VRAM", max(tv), max(tv) <= a.tex_vram_kb, " KB", False, "<= %g" % a.tex_vram_kb)
    else:
        check("texture VRAM", None, False, "", True, "(renderer columns still zero)")

    print()
    fails = results.count("FAIL")
    print("%d PASS, %d FAIL, %d SKIP -> %s" % (results.count("PASS"), fails, results.count("SKIP"),
                                              "FAIL" if fails else "PASS (checks that ran)"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
