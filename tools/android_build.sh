#!/usr/bin/env bash
# Builds the AirStrike 3D game APK (apps/game with touch controls, docs/android.md).
#   tools/android_build.sh
# Env:
#   AS3D_DATA_ROOT     where third_party_local/ (SDL2, libopenmpt, the original game data)
#                      lives (default: this repo's root; set it from a worktree to point at
#                      the main checkout).
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

DATA_DIR="${AS3D_DATA_ROOT}/third_party_local/original/data"
ASSETS_DIR="${REPO_ROOT}/android/app/src/main/assets"

echo "== copying the original pak archives into assets (stored uncompressed, read in place) =="
mkdir -p "${ASSETS_DIR}"
for pak in pak0.apk pak1.apk pak2.apk; do
    SRC="${DATA_DIR}/${pak}"
    if [ ! -f "${SRC}" ]; then
        echo "android_build: missing game data file ${SRC}" >&2
        echo "  Set AS3D_DATA_ROOT to a checkout that has third_party_local/original/data/." >&2
        exit 1
    fi
    # Only copy when changed, so Gradle does not repackage 25 MB for nothing.
    if ! cmp -s "${SRC}" "${ASSETS_DIR}/${pak}"; then cp -f "${SRC}" "${ASSETS_DIR}/${pak}"; fi
done

echo "== copying the front end's files (Settings.xml, the menu logo, the texts imported from the exe) =="
# Optional: without them the menus still work (no intro pages or logo; Information pages show
# a notice). assets/.gitignore keeps all of it out of git.
copy_optional() {
    if [ -f "$1" ]; then
        if ! cmp -s "$1" "${ASSETS_DIR}/$2"; then cp -f "$1" "${ASSETS_DIR}/$2"; fi
    else
        echo "android_build: note: $1 not found, the APK goes without it"
        rm -f "${ASSETS_DIR}/$2"
    fi
}
copy_optional "${DATA_DIR}/Settings.xml" Settings.xml
copy_optional "${DATA_DIR}/gfx/logo2s.tga" logo2s.tga
copy_optional "${AS3D_DATA_ROOT}/assets_extracted/texts_v170.txt" texts_v170.txt
ls -la "${ASSETS_DIR}"

# Safety net: nothing copyrighted may be tracked by git.
for f in pak0.apk pak1.apk pak2.apk Settings.xml logo2s.tga texts_v170.txt; do
    if git -C "${REPO_ROOT}" ls-files --error-unmatch "android/app/src/main/assets/${f}" >/dev/null 2>&1; then
        echo "android_build: android/app/src/main/assets/${f} is tracked by git; remove it from the index first" >&2
        exit 1
    fi
done

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
unzip -lv "${APK}" | grep -E "pak[0-2]\.apk|libmain\.so|libSDL2\.so" || true
echo "== done =="
echo "APK: ${APK} (${SIZE})"
