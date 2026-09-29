#!/usr/bin/env bash
# Builds the web version (apps/web, docs/web.md) with Emscripten and assembles its site.
# Never part of tools/ci.sh.
#
#   AS3D_DATA_ROOT=/path/to/main/checkout tools/web_build.sh [bundled|byo]
#
#   bundled (default)  the owner's game data is copied into the site (data/): for the owner's
#                      own use on their own network. NEVER host it publicly.
#   byo                "bring your own": the site holds only the engine; the player picks
#                      their own pak files on first start (kept in browser storage). The
#                      script checks that no game data ended up in it.
#
# Emscripten: $EMSDK if set, else ~/tools/emsdk. Output (all gitignored):
#   build-web/                          the CMake build (shared by both modes)
#   $AS3D_DATA_ROOT/out/web/site/       bundled   (AS3D_WEB_SITE overrides)
#   $AS3D_DATA_ROOT/out/web/site-byo/   byo       (AS3D_WEB_SITE overrides)
# The site is assembled beside the target and swapped in at the end, so a server running on
# the target directory serves either the old or the new site. AS3D_WEB_FULL_ES3=1 links with
# -sFULL_ES3.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DATA_ROOT="${AS3D_DATA_ROOT:-$ROOT}"
MODE="${1:-bundled}"
case "$MODE" in
  bundled|byo) ;;
  *) echo "usage: tools/web_build.sh [bundled|byo]" >&2; exit 2 ;;
esac
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

ORIG="$DATA_ROOT/third_party_local/original/data"
TEXTS="$DATA_ROOT/assets_extracted/texts_v170.txt"
if [ "$MODE" = bundled ]; then
  for k in 0 1 2; do
    if [ ! -f "$ORIG/pak$k.apk" ]; then
      echo "web_build: missing $ORIG/pak$k.apk (set AS3D_DATA_ROOT, or build byo)" >&2
      exit 1
    fi
    if git -C "$ROOT" ls-files --error-unmatch "third_party_local/original/data/pak$k.apk" >/dev/null 2>&1; then
      echo "web_build: pak$k.apk is tracked by git: refusing to build" >&2
      exit 1
    fi
  done
  SITE="${AS3D_WEB_SITE:-$DATA_ROOT/out/web/site}"
else
  SITE="${AS3D_WEB_SITE:-$DATA_ROOT/out/web/site-byo}"
fi

BUILD="${AS3D_WEB_BUILD_DIR:-$ROOT/build-web}"
export AS3D_DATA_ROOT="$DATA_ROOT"  # libopenmpt's source under third_party_local

emcmake cmake -S "$ROOT/apps/web" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
  -DAS3D_WEB_FULL_ES3="${AS3D_WEB_FULL_ES3:-0}" >/dev/null
cmake --build "$BUILD" -j"${AS3D_BUILD_JOBS:-4}"

# Assemble the new site beside the old one.
if [ -e "$SITE" ] && [ ! -f "$SITE/index.html" ]; then
  echo "web_build: $SITE exists and is not a site: refusing to replace it" >&2
  exit 1
fi
NEW="$SITE.new.$$"
rm -rf "$NEW"
mkdir -p "$NEW"
trap 'rm -rf "$NEW"' EXIT
STAMP="$(date +%Y%m%d%H%M%S)"
REV="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
cp -r "$ROOT/apps/web/site/." "$NEW/"
cp "$BUILD/as3d_web.js" "$BUILD/as3d_web.wasm" "$NEW/"
# The build stamp keeps browsers from mixing cached files of two builds.
sed -i "s/__BUILD__/$STAMP/g" "$NEW/index.html"
cat > "$NEW/build.js" <<EOF
// Written by tools/web_build.sh.
window.AS3D_BUILD = { mode: "$MODE", stamp: "$STAMP", rev: "$REV" };
EOF

if [ "$MODE" = bundled ]; then
  mkdir -p "$NEW/data"
  cp "$ORIG/pak0.apk" "$ORIG/pak1.apk" "$ORIG/pak2.apk" "$NEW/data/"
  [ -f "$ORIG/Settings.xml" ] && cp "$ORIG/Settings.xml" "$NEW/data/"
  [ -f "$ORIG/gfx/logo2s.tga" ] && cp "$ORIG/gfx/logo2s.tga" "$NEW/data/logo2s.tga"
  [ -f "$TEXTS" ] && cp "$TEXTS" "$NEW/data/texts_v170.txt"
  ls "$NEW/data" > "$NEW/data/index.txt"
else
  # Bring your own: nothing of the game may be in the site. Every file must be one the build
  # makes or apps/web/site holds, and no file may contain a pak header, a known game file or
  # the extracted texts.
  bad=0
  while IFS= read -r -d '' f; do
    rel="${f#$NEW/}"
    case "$rel" in
      index.html|app.js|files.js|style.css|build.js|as3d_web.js|as3d_web.wasm|manifest.webmanifest|known_files.json|icons/*.png|icons/*.svg) ;;
      *) echo "web_build: unexpected file in the byo site: $rel" >&2; bad=1 ;;
    esac
  done < <(find "$NEW" -type f -print0)
  if python3 - "$NEW" "$ORIG" "$TEXTS" <<'PY'
import hashlib, os, sys
site, orig, texts = sys.argv[1:4]
magic = bytes.fromhex("0000803f99990000")  # docs/spec/pak.md
known = {}
for name in ("pak0.apk", "pak1.apk", "pak2.apk", "Settings.xml", "gfx/logo2s.tga"):
    p = os.path.join(orig, name)
    if os.path.isfile(p):
        known[hashlib.sha256(open(p, "rb").read()).hexdigest()] = name
probes = []
if os.path.isfile(texts):
    # A few long lines of the extracted texts: none may appear in the site.
    lines = [l.split("=", 1)[1].strip().encode() for l in open(texts, encoding="utf-8")
             if "=" in l and not l.startswith("#") and len(l) > 60]
    probes = lines[:20]
bad = False
for d, _, files in os.walk(site):
    for n in files:
        p = os.path.join(d, n)
        b = open(p, "rb").read()
        rel = os.path.relpath(p, site)
        if magic in b:
            print(f"web_build: {rel} contains a pak header", file=sys.stderr); bad = True
        h = hashlib.sha256(b).hexdigest()
        if h in known:
            print(f"web_build: {rel} is the game's {known[h]}", file=sys.stderr); bad = True
        for pr in probes:
            if pr in b:
                print(f"web_build: {rel} contains the extracted texts", file=sys.stderr); bad = True
                break
sys.exit(1 if bad else 0)
PY
  then :; else bad=1; fi
  if [ "$bad" != 0 ]; then
    echo "web_build: the byo site would contain game data: not installed" >&2
    exit 1
  fi
  echo "web_build: byo site checked: no game data"
fi

# Swap the new site in.
if [ -d "$SITE" ]; then
  OLD="$SITE.old.$$"
  mv "$SITE" "$OLD"
  mv "$NEW" "$SITE"
  rm -rf "$OLD"
else
  mkdir -p "$(dirname "$SITE")"
  mv "$NEW" "$SITE"
fi
trap - EXIT
echo "web_build: $("$EMSCRIPTEN_DIR/emcc" --version 2>/dev/null | head -1)"
echo "web_build: $MODE site in $SITE ($REV, build $STAMP)"
ls -l "$SITE"
if [ "$MODE" = bundled ]; then
  echo "web_build: it contains the original game data: serve it only to yourself (tools/web_serve.sh)"
fi
