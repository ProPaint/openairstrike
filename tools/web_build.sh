#!/usr/bin/env bash
# Builds the web spike (apps/web_spike, docs/web-spike.md) with Emscripten and assembles the
# page with the owner's game data. Never part of tools/ci.sh.
#
#   AS3D_DATA_ROOT=/path/to/main/checkout tools/web_build.sh
#
# Emscripten: $EMSDK if set, else ~/tools/emsdk. Output (all gitignored):
#   build-web/                  the CMake build
#   $AS3D_DATA_ROOT/out/web/site/  index.html, as3d_web_{game,level}.{js,wasm},
#                                  game_data.{data,js} (the three paks, copyrighted: never
#                                  publish this directory)
# AS3D_WEB_SITE overrides the site directory; AS3D_WEB_FULL_ES3=1 links with -sFULL_ES3.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DATA_ROOT="${AS3D_DATA_ROOT:-$ROOT}"
EMSDK_DIR="${EMSDK:-$HOME/tools/emsdk}"
EMSCRIPTEN_DIR="$EMSDK_DIR/upstream/emscripten"
if [ ! -x "$EMSCRIPTEN_DIR/emcc" ]; then
  echo "web_build: no Emscripten at $EMSDK_DIR (set EMSDK, or install emsdk in ~/tools/emsdk)" >&2
  exit 1
fi
export EMSDK="$EMSDK_DIR"
export EM_CONFIG="$EMSDK_DIR/.emscripten"
NODE_DIR="$(ls -d "$EMSDK_DIR"/node/*/bin 2>/dev/null | head -1 || true)"
export PATH="$EMSCRIPTEN_DIR:$EMSDK_DIR/upstream/bin:${NODE_DIR:+$NODE_DIR:}$PATH"

PAKS="$DATA_ROOT/third_party_local/original/data"
for k in 0 1 2; do
  if [ ! -f "$PAKS/pak$k.apk" ]; then
    echo "web_build: missing $PAKS/pak$k.apk (set AS3D_DATA_ROOT)" >&2
    exit 1
  fi
  if git -C "$ROOT" ls-files --error-unmatch "third_party_local/original/data/pak$k.apk" >/dev/null 2>&1; then
    echo "web_build: pak$k.apk is tracked by git: refusing to build" >&2
    exit 1
  fi
done

BUILD="${AS3D_WEB_BUILD_DIR:-$ROOT/build-web}"
SITE="${AS3D_WEB_SITE:-$DATA_ROOT/out/web/site}"
export AS3D_DATA_ROOT="$DATA_ROOT"  # libopenmpt's source under third_party_local

emcmake cmake -S "$ROOT/apps/web_spike" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
  -DAS3D_WEB_FULL_ES3="${AS3D_WEB_FULL_ES3:-0}" >/dev/null
cmake --build "$BUILD" -j"${AS3D_BUILD_JOBS:-4}"

mkdir -p "$SITE"
# The game data: the three original paks under /data, preloaded into memory before main().
python3 "$EMSCRIPTEN_DIR/tools/file_packager.py" "$SITE/game_data.data" \
  --preload "$PAKS/pak0.apk@/data/pak0.apk" "$PAKS/pak1.apk@/data/pak1.apk" "$PAKS/pak2.apk@/data/pak2.apk" \
  --js-output="$SITE/game_data.js" >/dev/null
for app in game level; do
  cp "$BUILD/as3d_web_$app.js" "$BUILD/as3d_web_$app.wasm" "$SITE/"
done
cp "$ROOT/apps/web_spike/web/index.html" "$SITE/"
echo "web_build: $("$EMSCRIPTEN_DIR/emcc" --version 2>/dev/null | head -1)"
ls -l "$SITE"
echo "web_build: serve with tools/web_serve.sh, then open http://127.0.0.1:8080/"
