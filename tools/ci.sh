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
cmake --build "$BUILD" -j"$(nproc)"
"$BUILD/apps/as3d_tests" "$@"
for t in "$ROOT"/tools/ref/test_*.py; do
  [ -e "$t" ] || continue
  echo "ci: python $t"
  python3 "$t"
done
echo "ci: OK"
