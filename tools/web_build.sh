#!/usr/bin/env bash
# Builds the web version (apps/web, docs/web.md) with Emscripten and assembles its site.
# Never part of tools/ci.sh.
#
#   AS3D_DATA_ROOT=/path/to/main/checkout tools/web_build.sh [bundled|byo]
#
#   bundled (default)  the owner's game data is copied into the site (data/<key>/, one directory
#                      per game of AS3D_WEB_GAMES): for the owner's own use on their own
#                      network. NEVER host it publicly.
#   byo                "bring your own": the site holds only the engine; the player picks
#                      their own pak files on first start (kept in browser storage). The
#                      script checks that no game data ended up in it (no pak header, no file
#                      or text of any game in tools/games.json). known_files.json, the
#                      checksums by game that identify a dropped file, is generated into the
#                      site by tools/web_known_files.py.
#
# Env:
#   AS3D_WEB_GAMES     comma-separated game keys the bundled build contains (as3d, as2, gulf;
#                      default as3d,as2, the playable games). data/games.txt lists them: with
#                      more than one the page's start screen offers a choice and downloads only
#                      the chosen game's files (docs/spec/issues/163); ?game= forces one.
#   AS3D_WEB_SITE      where the site is assembled (see below).
#
#   AS3D_DESKTOP_BUILD_DIR   the desktop build (default build/) whose as3d_game renders the
#                      selector's marquees into the bundled site (marquee/<key>.webp, about 1 MB in
#                      all, docs/web.md: renders of the games' own art, so bundled only).
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

# Where a game's install directory and extracted files are (docs/spec/README.md, data layout).
install_dir() { if [ "$1" = as3d ]; then echo "$DATA_ROOT/third_party_local/original"; else echo "$DATA_ROOT/third_party_local/games/$1"; fi; }
extracted_dir() { if [ "$1" = as3d ]; then echo "$DATA_ROOT/assets_extracted"; else echo "$DATA_ROOT/assets_extracted_games/$1"; fi; }
# "key|pak names|texts file" of a game of tools/games.json.
game_line() {
  python3 - "$ROOT/tools/games.json" "$1" <<'PY'
import json, sys
for g in json.load(open(sys.argv[1]))["games"]:
    if g["key"] == sys.argv[2]:
        print(g["key"] + "|" + " ".join(g["paks"]) + "|" + g["texts"])
        break
else:
    sys.exit(1)
PY
}
GAMES="${AS3D_WEB_GAMES:-as3d,as2,gulf}"
IFS=',' read -r -a GAME_KEYS <<< "$GAMES"

if [ "$MODE" = bundled ]; then
  for key in "${GAME_KEYS[@]}"; do
    line="$(game_line "$key")" || { echo "web_build: unknown game '$key' in AS3D_WEB_GAMES (see tools/games.json)" >&2; exit 2; }
    IFS='|' read -r _ paks _ <<< "$line"
    inst="$(install_dir "$key")"
    for pak in $paks; do
      if [ ! -f "$inst/data/$pak" ]; then
        echo "web_build: missing $inst/data/$pak (set AS3D_DATA_ROOT, or build byo)" >&2
        exit 1
      fi
      rel="${inst#$DATA_ROOT/}/data/$pak"
      if git -C "$ROOT" ls-files --error-unmatch "$rel" >/dev/null 2>&1; then
        echo "web_build: $rel is tracked by git: refusing to build" >&2
        exit 1
      fi
    done
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
# The checksums by game: from tools/games.json and the game files on this machine (what is not
# on the disk stays what the committed apps/web/site/known_files.json says).
python3 "$ROOT/tools/web_known_files.py" --root "$DATA_ROOT" --previous "$ROOT/apps/web/site/known_files.json" \
  --out "$NEW/known_files.json" >/dev/null
# The build stamp keeps browsers from mixing cached files of two builds.
sed -i "s/__BUILD__/$STAMP/g" "$NEW/index.html"
cat > "$NEW/build.js" <<EOF
// Written by tools/web_build.sh.
window.AS3D_BUILD = { mode: "$MODE", stamp: "$STAMP", rev: "$REV" };
EOF

if [ "$MODE" = bundled ]; then
  # The selector's marquees, rendered by the desktop engine (the same code that draws the
  # selector on desktop and Android): frames, then one looping WebP per game. The page's cards
  # show them; without them (no GL on this machine) the cards show their titles in text.
  DESK="${AS3D_DESKTOP_BUILD_DIR:-$ROOT/build}"
  FRAMES="$(mktemp -d)"
  trap 'rm -rf "$NEW" "$FRAMES"' EXIT
  cmake -S "$ROOT" -B "$DESK" -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null
  cmake --build "$DESK" -j"${AS3D_BUILD_JOBS:-4}" --target as3d_game_app >/dev/null
  if (ulimit -v 4000000; AS3D_USER_DATA_DIR="$FRAMES/user" timeout 600 "$DESK/apps/game/as3d_game" --data "$DATA_ROOT" --headless --quiet \
        --selector-marquees "$FRAMES/frames" --selector-games "$GAMES" --size 800x600); then
    python3 "$ROOT/tools/web_marquees.py" "$FRAMES/frames" "$NEW/marquee"
    rm -rf "$FRAMES"
  else
    echo "web_build: WARNING: could not render the selector's marquees: the cards show titles in text" >&2
  fi
  for key in "${GAME_KEYS[@]}"; do
    IFS='|' read -r _ paks texts <<< "$(game_line "$key")"
    inst="$(install_dir "$key")"
    out="$NEW/data/$key"
    mkdir -p "$out"
    for pak in $paks; do cp "$inst/data/$pak" "$out/"; done
    [ -f "$inst/data/Settings.xml" ] && cp "$inst/data/Settings.xml" "$out/"
    [ -f "$inst/data/gfx/logo2s.tga" ] && cp "$inst/data/gfx/logo2s.tga" "$out/logo2s.tga"
    [ -f "$(extracted_dir "$key")/$texts" ] && cp "$(extracted_dir "$key")/$texts" "$out/$texts"
    ls "$out" > "$out/index.txt"
    echo "$key" >> "$NEW/data/games.txt"
  done
else
  # Bring your own: nothing of the game may be in the site. Every file must be one the build
  # makes or apps/web/site holds, and no file may contain a pak header, a known game file or
  # the extracted texts.
  bad=0
  while IFS= read -r -d '' f; do
    rel="${f#$NEW/}"
    case "$rel" in
      index.html|app.js|files.js|marquee.js|style.css|build.js|as3d_web.js|as3d_web.wasm|manifest.webmanifest|known_files.json|icons/*.png|icons/*.svg) ;;
      *) echo "web_build: unexpected file in the byo site: $rel" >&2; bad=1 ;;
    esac
  done < <(find "$NEW" -type f -print0)
  if python3 - "$NEW" "$DATA_ROOT" "$ROOT/tools/games.json" <<'PY'
import hashlib, json, os, sys
site, root, gj = sys.argv[1:4]
magic = bytes.fromhex("0000803f99990000")  # docs/spec/pak.md
known = {}   # sha256 -> what it is, for every file of every game that is on this machine
probes = []  # a few long lines of every game's extracted texts
for g in json.load(open(gj))["games"]:
    key = g["key"]
    inst = os.path.join(root, "third_party_local", "original" if key == "as3d" else os.path.join("games", key))
    ext = os.path.join(root, "assets_extracted" if key == "as3d" else os.path.join("assets_extracted_games", key))
    for name, sha in g["pak_sha256"].items():
        known[sha] = key + "/" + name  # the checksums themselves are facts, no file needed
    for rel in ["data/Settings.xml", "data/gfx/logo2s.tga", g["exe"]] + ["data/" + p for p in g["paks"]]:
        p = os.path.join(inst, rel)
        if os.path.isfile(p):
            known[hashlib.sha256(open(p, "rb").read()).hexdigest()] = key + "/" + rel
    t = os.path.join(ext, g["texts"])
    if os.path.isfile(t):
        lines = [l.split("=", 1)[1].strip().encode() for l in open(t, encoding="utf-8")
                 if "=" in l and not l.startswith("#") and len(l) > 60]
        probes += lines[:20]
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
            print(f"web_build: {rel} is the game file {known[h]}", file=sys.stderr); bad = True
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
