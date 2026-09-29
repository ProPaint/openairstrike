#!/usr/bin/env bash
# Builds the AirStrike 3D game APK (apps/game with touch controls, docs/android.md).
#   tools/android_build.sh
# Env:
#   AS3D_DATA_ROOT     where third_party_local/ (SDL2, libopenmpt, the original game data of
#                      every bundled game) lives (default: this repo's root; set it from a worktree to point at
#                      the main checkout).
#   AS3D_ANDROID_GAMES comma-separated game keys to bundle (as3d, as2, gulf; default as3d, to
#                      become all three when the sequels play). Each game's paks, Settings.xml,
#                      logo and texts file go under assets/<key>/ (tools/games.json lists them).
#   AS3D_ANDROID_ABIS  comma-separated ABIs (default arm64-v8a,x86_64).
#   AS3D_NATIVE_JOBS   parallel native compile jobs (default 4).
#   AS3D_ICON_FROM_DATA  1 (default): launcher icon foreground rendered from the game data
#                      with the desktop viewer when it and the data are available (else our
#                      own vector icon); 0: always our own icon.
#   AS3D_VIEWER        the as3d_viewer binary (default: build/apps/viewer/as3d_viewer here or
#                      under AS3D_DATA_ROOT).
#
# The APK embeds the original, copyrighted pak archives: it is for the owner's personal
# use only and must never be committed or shared (android/app/build/ and the copied paks
# are gitignored).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
# shellcheck source=./android_env.sh
source "${SCRIPT_DIR}/android_env.sh"

export AS3D_DATA_ROOT="${AS3D_DATA_ROOT:-${REPO_ROOT}}"

echo "== fetch_third_party =="
"${SCRIPT_DIR}/fetch_third_party.sh"

SDL_VERSION="2.30.12"
export AS3D_SDL2_DIR="${AS3D_SDL2_DIR:-${AS3D_DATA_ROOT}/third_party_local/SDL2-${SDL_VERSION}}"

ASSETS_DIR="${REPO_ROOT}/android/app/src/main/assets"
GAMES_JSON="${SCRIPT_DIR}/games.json"

# The games bundled in the APK: their keys, comma separated (as3d, as2, gulf).
GAMES="${AS3D_ANDROID_GAMES:-as3d}"
IFS=',' read -r -a GAME_KEYS <<< "${GAMES}"

# One line per game from tools/games.json: key|paks (space separated)|texts file.
game_line() {
    python3 - "${GAMES_JSON}" "$1" <<'PY'
import json, sys
for g in json.load(open(sys.argv[1]))["games"]:
    if g["key"] == sys.argv[2]:
        print(g["key"] + "|" + " ".join(g["paks"]) + "|" + g["texts"])
        break
else:
    sys.exit(1)
PY
}
# Where a game's install data and extracted files are (docs/spec/README.md, data layout).
game_data_dir() { if [ "$1" = as3d ]; then echo "${AS3D_DATA_ROOT}/third_party_local/original/data"; else echo "${AS3D_DATA_ROOT}/third_party_local/games/$1/data"; fi; }
game_extracted_dir() { if [ "$1" = as3d ]; then echo "${AS3D_DATA_ROOT}/assets_extracted"; else echo "${AS3D_DATA_ROOT}/assets_extracted_games/$1"; fi; }

echo "== copying the paks and front-end files of: ${GAMES} into assets/<key>/ (paks stored uncompressed, read in place) =="
mkdir -p "${ASSETS_DIR}"
# Only what is asked for stays: drop other games' directories and the flat layout of older builds.
for entry in "${ASSETS_DIR}"/* ; do
    [ -e "${entry}" ] || continue
    keep=0
    for key in "${GAME_KEYS[@]}"; do [ "${entry}" = "${ASSETS_DIR}/${key}" ] && keep=1; done
    [ "${keep}" = 1 ] || rm -rf "${entry}"
done

# Only copy when changed, so Gradle does not repackage 25 MB for nothing.
copy_if_changed() {
    if ! cmp -s "$1" "$2"; then cp -f "$1" "$2"; fi
}
# Optional: without them the menus still work (no intro pages or logo; Information pages show
# a notice). assets/.gitignore keeps all of it out of git.
copy_optional() {
    if [ -f "$1" ]; then
        copy_if_changed "$1" "$2"
    else
        echo "android_build: note: $1 not found, the APK goes without it"
        rm -f "$2"
    fi
}

TRACKED=()
for key in "${GAME_KEYS[@]}"; do
    line="$(game_line "${key}")" || { echo "android_build: unknown game '${key}' in AS3D_ANDROID_GAMES (see tools/games.json)" >&2; exit 1; }
    IFS='|' read -r _ paks texts <<< "${line}"
    DATA_DIR="$(game_data_dir "${key}")"
    OUT="${ASSETS_DIR}/${key}"
    mkdir -p "${OUT}"
    for pak in ${paks}; do
        if [ ! -f "${DATA_DIR}/${pak}" ]; then
            echo "android_build: missing game data file ${DATA_DIR}/${pak}" >&2
            echo "  Set AS3D_DATA_ROOT to a checkout that has the data of '${key}' (tools/setup_data.sh)." >&2
            exit 1
        fi
        copy_if_changed "${DATA_DIR}/${pak}" "${OUT}/${pak}"
        TRACKED+=("${key}/${pak}")
    done
    copy_optional "${DATA_DIR}/Settings.xml" "${OUT}/Settings.xml"
    copy_optional "${DATA_DIR}/gfx/logo2s.tga" "${OUT}/logo2s.tga"
    copy_optional "$(game_extracted_dir "${key}")/${texts}" "${OUT}/${texts}"
    TRACKED+=("${key}/Settings.xml" "${key}/logo2s.tga" "${key}/${texts}")
done
ls -laR "${ASSETS_DIR}"

# Safety net: nothing copyrighted may be tracked by git (every file of every game, and
# anything else under assets/ except its .gitignore).
for f in "${TRACKED[@]}"; do
    if git -C "${REPO_ROOT}" ls-files --error-unmatch "android/app/src/main/assets/${f}" >/dev/null 2>&1; then
        echo "android_build: android/app/src/main/assets/${f} is tracked by git; remove it from the index first" >&2
        exit 1
    fi
done
if [ -n "$(git -C "${REPO_ROOT}" ls-files android/app/src/main/assets | grep -v '^android/app/src/main/assets/\.gitignore$' || true)" ]; then
    echo "android_build: files other than .gitignore are tracked under android/app/src/main/assets; remove them from the index first" >&2
    exit 1
fi

echo "== launcher icon =="
# Our own vector icon (android/app/src/main/res) is always there. With AS3D_ICON_FROM_DATA=1
# (the default when the desktop viewer and the game data are available) the foreground is
# replaced by the default player helicopter rendered from above from the owner's data, into
# android/app/src/icon_from_data/ (gitignored; build.gradle adds it when it exists). Any
# failure falls back to our own icon silently.
ICON_DIR="${REPO_ROOT}/android/app/src/icon_from_data"
rm -rf "${ICON_DIR}"
make_data_icon() {
    local viewer="${AS3D_VIEWER:-}"
    if [ -z "${viewer}" ]; then
        for c in "${REPO_ROOT}/build/apps/viewer/as3d_viewer" "${AS3D_DATA_ROOT}/build/apps/viewer/as3d_viewer"; do
            if [ -x "${c}" ]; then viewer="${c}"; break; fi
        done
    fi
    [ -n "${viewer}" ] && [ -x "${viewer}" ] || return 1
    python3 -c "import PIL" 2>/dev/null || return 1
    local tmp
    tmp="$(mktemp -d)"
    # Four views turned by 90 degrees: the viewer's square ground grid and backdrop look the
    # same in all of them, the helicopter does not, which separates it from the background.
    local yaw
    for yaw in 0 90 180 270; do
        ( ulimit -v 4000000; AS3D_DATA_ROOT="${AS3D_DATA_ROOT}" timeout 120 "${viewer}" object p_comanche \
            --out "${tmp}/h${yaw}.png" --size 512x512 --pitch 89.9 --yaw "${yaw}" --dist 0.8 ) >/dev/null 2>&1 \
            || { rm -rf "${tmp}"; return 1; }
    done
    mkdir -p "${ICON_DIR}/res/drawable-nodpi" "${ICON_DIR}/res/mipmap-anydpi-v26"
    python3 - "${ICON_DIR}/res/drawable-nodpi/ic_launcher_data_fg.png" \
        "${tmp}/h0.png" "${tmp}/h90.png" "${tmp}/h180.png" "${tmp}/h270.png" <<'PY' || { rm -rf "${tmp}"; return 1; }
import sys
from PIL import Image

out, paths = sys.argv[1], sys.argv[2:]
ims = [Image.open(p).convert("RGB") for p in paths]
a = ims[0]
w, h = a.size
pa = a.load()
others = [im.load() for im in ims[1:]]
res = Image.new("RGBA", (w, h), (0, 0, 0, 0))
pr = res.load()
for y in range(h):
    for x in range(w):
        c = pa[x, y]
        # Background: the same colour in another view (the grid and backdrop do not turn).
        if any(max(abs(c[k] - o[x, y][k]) for k in range(3)) <= 6 for o in others):
            continue
        r, g, b = c
        # The translucent rotor disc over the grey backdrop is left out.
        if b >= r + 7 and abs(g - (r + b) / 2) <= 6 and max(c) - min(c) <= 24:
            continue
        pr[x, y] = c + (255,)
# The largest connected piece is the helicopter (stray rotor-tip arcs go).
seen = bytearray(w * h)
best = []
for y0 in range(h):
    for x0 in range(w):
        if not pr[x0, y0][3] or seen[y0 * w + x0]:
            continue
        comp, stack = [], [(x0, y0)]
        seen[y0 * w + x0] = 1
        while stack:
            x, y = stack.pop()
            comp.append((x, y))
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < w and 0 <= ny < h and not seen[ny * w + nx] and pr[nx, ny][3]:
                        seen[ny * w + nx] = 1
                        stack.append((nx, ny))
        if len(comp) > len(best):
            best = comp
if len(best) < 500:
    sys.exit("icon: no helicopter found")
keep = Image.new("RGBA", (w, h), (0, 0, 0, 0))
pk = keep.load()
for x, y in best:
    pk[x, y] = pr[x, y]
crop = keep.crop(keep.getbbox()).rotate(90, expand=True, resample=Image.BICUBIC)  # nose up
S = 432  # 108 dp at xxxhdpi; the helicopter spans 60 % (inside the 66 dp safe circle)
scale = S * 0.60 / max(crop.size)
crop = crop.resize((max(1, int(crop.size[0] * scale)), max(1, int(crop.size[1] * scale))), Image.LANCZOS)
canvas = Image.new("RGBA", (S, S), (0, 0, 0, 0))
canvas.alpha_composite(crop, ((S - crop.size[0]) // 2, (S - crop.size[1]) // 2))
canvas.save(out)
PY
    rm -rf "${tmp}"
    local name
    for name in ic_launcher ic_launcher_round; do
        cat > "${ICON_DIR}/res/mipmap-anydpi-v26/${name}.xml" <<'XML'
<?xml version="1.0" encoding="utf-8"?>
<!-- Generated by tools/android_build.sh from the owner's game data: never commit. -->
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@drawable/ic_launcher_background" />
    <foreground android:drawable="@drawable/ic_launcher_data_fg" />
    <monochrome android:drawable="@drawable/ic_launcher_monochrome" />
</adaptive-icon>
XML
    done
    return 0
}
if [ "${AS3D_ICON_FROM_DATA:-1}" = "1" ] && make_data_icon; then
    echo "android_build: launcher icon rendered from the game data (${ICON_DIR}, gitignored)"
else
    rm -rf "${ICON_DIR}"
    echo "android_build: launcher icon: our own vector drawing"
fi
if git -C "${REPO_ROOT}" ls-files --error-unmatch "android/app/src/icon_from_data" >/dev/null 2>&1; then
    echo "android_build: android/app/src/icon_from_data is tracked by git; remove it from the index first" >&2
    exit 1
fi

echo "== writing android/local.properties (gitignored) =="
cat > "${REPO_ROOT}/android/local.properties" <<EOF
sdk.dir=${ANDROID_HOME}
EOF

echo "== gradlew assembleDebug (JVM capped at 1.5 GB, 2 workers) =="
cd "${REPO_ROOT}/android"
./gradlew --console=plain -Dorg.gradle.jvmargs=-Xmx1536m --max-workers=2 assembleDebug

APK="${REPO_ROOT}/android/app/build/outputs/apk/debug/app-debug.apk"
if [ ! -f "${APK}" ]; then
    echo "android_build: expected APK not found at ${APK}" >&2
    exit 1
fi
SIZE="$(du -h "${APK}" | awk '{print $1}')"
echo "== APK contents (paks must be stored, not deflated) =="
unzip -lv "${APK}" | grep -E "assets/.*(pak[0-9]\.apk|Settings\.xml|logo2s\.tga|texts_)|libmain\.so|libSDL2\.so" || true
echo "== done =="
echo "APK: ${APK} (${SIZE})"
