#!/usr/bin/env bash
# Simulation step timing table (docs/3ds-port.md "M4.2 Simulation hot spots").
# Runs `as3d_sim --level N --frames F --bot --time` for a set of missions and prints the
# per-step milliseconds: mean, median, p99 and worst, plus the per-phase means.
# Each mission runs R times; the table keeps the run with the lowest mean (the least
# disturbed by the rest of the machine). Desktop numbers only: the 3DS numbers come from
# the hardware run (apps/3ds mode=spike or mode=bot step times).
#
#   tools/sim_bench.sh                    the standard set (below), 3600 frames, 3 runs
#   tools/sim_bench.sh --all [FRAMES]     every mission of every game present, 1 run
#                                         (default 600 frames): finds the heaviest missions
#   AS3D_SIM=path/to/as3d_sim             the binary (default build/apps/sim_tool/as3d_sim)
#   AS3D_BENCH_FRAMES=3600 AS3D_BENCH_RUNS=3
#   AS3D_BENCH_SET="as3d:1,6,14 as2:1,6,14"   the set (default: missions 1, 6, 14 of each game
#                                         and the three heaviest of each by --all 600 on 2026-10-08)
#   AS3D_DATA_ROOT                        the game data, as for as3d_sim
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SIM="${AS3D_SIM:-$ROOT/build/apps/sim_tool/as3d_sim}"
FRAMES="${AS3D_BENCH_FRAMES:-3600}"
RUNS="${AS3D_BENCH_RUNS:-3}"
SET="${AS3D_BENCH_SET:-as3d:1,6,14,16,19,20 as2:1,3,6,8,14 gulf:1,6,7,14,18,23}"
[ -x "$SIM" ] || { echo "sim_bench: $SIM not built (cmake --build build --target as3d_sim)" >&2; exit 2; }

if [ "${1:-}" = "--all" ]; then
  FRAMES="${2:-600}"
  RUNS=1
  SET=""
  for game in $(python3 "$ROOT/tools/ref/gamesel.py" present); do
    n="$(python3 -c 'import json,sys; print(next(g["missions"] for g in json.load(open(sys.argv[1]))["games"] if g["key"] == sys.argv[2]))' "$ROOT/tools/games.json" "$game")"
    SET="$SET $game:$(seq -s, 1 "$n")"
  done
fi

printf '%-5s %4s %9s %9s %9s %9s  %s\n' game lvl mean_ms median p99 worst "phases (mean ms: camera spawn player entities collisions after)"
for entry in $SET; do
  game="${entry%%:*}"
  for level in $(echo "${entry#*:}" | tr ',' ' '); do
    best=""
    bestMean=""
    for _ in $(seq 1 "$RUNS"); do
      out="$("$SIM" --game "$game" --level "$level" --frames "$FRAMES" --bot --time 2>/dev/null)" || { best="FAILED"; break; }
      t="$(grep '^time:' <<<"$out")"
      p="$(grep '^phases:' <<<"$out")"
      mean="$(awk '{print $5}' <<<"$t")"
      if [ -z "$bestMean" ] || awk -v a="$mean" -v b="$bestMean" 'BEGIN{exit !(a < b)}'; then
        bestMean="$mean"
        best="$(awk '{printf "%9s %9s %9s %9s", $5, $8, $11, $14}' <<<"$t")  $(awk '{printf "%s %s %s %s %s %s", $3, $5, $7, $9, $11, $13}' <<<"$p")"
      fi
    done
    printf '%-5s %4s %s\n' "$game" "$level" "$best"
  done
done
