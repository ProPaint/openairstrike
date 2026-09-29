#!/usr/bin/env bash
# Builds the desktop tree and runs every test, once per game present on this machine.
# Run from anywhere.
#
#   AS3D_CI_GAMES="as3d as2 gulf"   the games to run (default: every game with extracted data,
#                                   in tools/games.json order; commas or spaces); a game named
#                                   here without data has its data tests skipped, loudly
#   AS3D_TEST_MEM_KB=0              no address-space cap (needed for sanitizer builds)
#   AS3D_BUILD_JOBS, AS3D_BUILD_DIR, AS3D_CMAKE_ARGS   as before
#
# Per game: the doctest binary with AS3D_GAME=<key>, then tools/ref/test_*.py --game <key>.
# "ci: OK" only if all passed. The play tests skip themselves for a game whose
# testdata/golden/<key>/expected.json says "playable": false.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${AS3D_BUILD_DIR:-$ROOT/build}"
if [ ! -d "$ROOT/assets_extracted" ] && [ -z "${AS3D_DATA_ROOT:-}" ]; then
  echo "ci: WARNING: assets_extracted/ is missing, golden data tests will be SKIPPED." >&2
  echo "ci: run tools/setup_data.sh and tools/paktool.py extract first." >&2
fi
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=RelWithDebInfo ${AS3D_CMAKE_ARGS:-} >/dev/null
cmake --build "$BUILD" -j"${AS3D_BUILD_JOBS:-4}"

# The games to run: the games with data (tools/ref/gamesel.py present), or AS3D_CI_GAMES.
if [ -n "${AS3D_CI_GAMES:-}" ]; then
  GAMES="$(echo "$AS3D_CI_GAMES" | tr ',' ' ')"
else
  GAMES="$(python3 "$ROOT/tools/ref/gamesel.py" present | tr '\n' ' ')"
  # No data anywhere: run the first game once so that the data tests report their SKIPs.
  [ -n "${GAMES// /}" ] || GAMES="as3d"
fi

# Address-space cap for the tests: a runaway allocation must fail as bad_alloc, not
# take the machine (and the terminal session) down through the kernel OOM killer.
# AS3D_TEST_MEM_KB=0 disables it (needed for sanitizer builds).
MEM_KB="${AS3D_TEST_MEM_KB:-4000000}"

for game in $GAMES; do
  echo
  echo "ci: ==================== game $game ===================="
  (
    [ "$MEM_KB" = 0 ] || ulimit -v "$MEM_KB"
    AS3D_GAME="$game" "$BUILD/apps/as3d_tests" "$@"
  )
  for t in "$ROOT"/tools/ref/test_*.py; do
    [ -e "$t" ] || continue
    echo "ci: [$game] python $t"
    AS3D_GAME="$game" python3 "$t" --game "$game"
  done
done
echo "ci: games run: $GAMES"
echo "ci: OK"
