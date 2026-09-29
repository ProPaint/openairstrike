#!/usr/bin/env bash
# Proves that AirStrike 3D behaves as it did before the engine learnt to run several games.
#
#   tools/regress_as3d.sh capture   record the baseline from the current build
#   tools/regress_as3d.sh check     compare the current build with the baseline (the default)
#
# Compared byte for byte: the simulation's state dump of missions 1, 10 and 20 after 3000
# frames under the scripted bot, and of mission 1 under the pilot in god mode after 6000.
# Compared as images (tools/imgdiff.py): headless frames of mission 1 and front end screens.
# The baseline lives in $AS3D_DATA_ROOT/out/regress/baseline (gitignored: it is derived from
# the game data); AS3D_REGRESS_BASELINE names another place.
# Needs a finished build (tools/ci.sh) and the game data under AS3D_DATA_ROOT.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${AS3D_BUILD_DIR:-$ROOT/build}"
export AS3D_DATA_ROOT="${AS3D_DATA_ROOT:-$ROOT}"
MODE="${1:-check}"
# In a git worktree the baseline is the main checkout's, found through AS3D_DATA_ROOT.
BASE="${AS3D_REGRESS_BASELINE:-$AS3D_DATA_ROOT/out/regress/baseline}"
NOW="$ROOT/out/regress/current"
SIM="$BUILD/apps/sim_tool/as3d_sim"
GAME="$BUILD/apps/game/as3d_game"
VIEWER="$BUILD/apps/viewer/as3d_viewer"
for b in "$SIM" "$GAME" "$VIEWER"; do
  [ -x "$b" ] || { echo "regress: $b is missing, build first (tools/ci.sh)" >&2; exit 2; }
done

run() { ( ulimit -v "${AS3D_TEST_MEM_KB:-4000000}"; timeout 600 "$@" ) >/dev/null 2>&1; }

produce() {
  local out="$1"
  rm -rf "$out"
  mkdir -p "$out/frames" "$out/screens"
  for m in 1 10 20; do
    run "$SIM" --level "$m" --frames 3000 --bot --seed 1 --dump-state "$out/bot_m$m.json"
  done
  run "$SIM" --level 1 --frames 6000 --pilot --god --seed 1 --dump-state "$out/pilot_m1.json"
  run "$GAME" --headless --level 1 --bot --seed 1 --frames 1800 --no-audio --quiet \
      --screenshot-every 600 --out-dir "$out/frames" --dump-state "$out/game_m1.json"
  for s in main start options scores complete gameover ingame; do
    run "$VIEWER" screen "$s" --out "$out/screens/$s.png" || true
  done
  run "$VIEWER" hud --out "$out/screens/hud.png" || true
}

case "$MODE" in
  capture)
    produce "$BASE"
    git -C "$ROOT" rev-parse HEAD > "$BASE/commit.txt"
    echo "regress: baseline of $(cat "$BASE/commit.txt" | cut -c1-7): $(find "$BASE" -type f | wc -l) files"
    ;;
  check)
    [ -f "$BASE/commit.txt" ] || { echo "regress: no baseline, run '$0 capture' on a known good commit" >&2; exit 2; }
    produce "$NOW"
    fail=0
    while IFS= read -r f; do
      rel="${f#"$BASE"/}"
      [ "$rel" = "commit.txt" ] && continue
      if [ ! -f "$NOW/$rel" ]; then echo "regress: MISSING $rel"; fail=1; continue; fi
      case "$rel" in
        *.png)
          if ! cmp -s "$f" "$NOW/$rel"; then
            if ! python3 "$ROOT/tools/imgdiff.py" "$f" "$NOW/$rel" >/dev/null 2>&1; then
              echo "regress: DIFFERS $rel"; fail=1
            fi
          fi ;;
        *) cmp -s "$f" "$NOW/$rel" || { echo "regress: DIFFERS $rel"; fail=1; } ;;
      esac
    done < <(find "$BASE" -type f | sort)
    if [ "$fail" = 0 ]; then
      echo "regress: OK, same as the baseline of $(cut -c1-7 "$BASE/commit.txt")"
    else
      echo "regress: FAILED" >&2; exit 1
    fi
    ;;
  *) echo "usage: $0 [capture|check]" >&2; exit 2 ;;
esac
