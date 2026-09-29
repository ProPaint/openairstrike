#!/usr/bin/env bash
# Builds the desktop tree and runs every test. Run from anywhere.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${AS3D_BUILD_DIR:-$ROOT/build}"
if [ ! -d "$ROOT/assets_extracted" ]; then
  echo "ci: WARNING: assets_extracted/ is missing, golden data tests will be SKIPPED." >&2
  echo "ci: run tools/setup_data.sh and tools/paktool.py extract first." >&2
fi
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=RelWithDebInfo ${AS3D_CMAKE_ARGS:-} >/dev/null
cmake --build "$BUILD" -j"${AS3D_BUILD_JOBS:-4}"
# Address-space cap for the tests: a runaway allocation must fail as bad_alloc, not
# take the machine (and the terminal session) down through the kernel OOM killer.
# AS3D_TEST_MEM_KB=0 disables it (needed for sanitizer builds).
MEM_KB="${AS3D_TEST_MEM_KB:-4000000}"
(
  [ "$MEM_KB" = 0 ] || ulimit -v "$MEM_KB"
  "$BUILD/apps/as3d_tests" "$@"
)
for t in "$ROOT"/tools/ref/test_*.py; do
  [ -e "$t" ] || continue
  echo "ci: python $t"
  python3 "$t"
done
echo "ci: OK"
