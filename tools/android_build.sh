#!/usr/bin/env bash
# Builds the AirStrike 3D game APK (apps/game with touch controls, docs/android.md).
#   tools/android_build.sh
# Env:
#   AS3D_DATA_ROOT     where third_party_local/ (SDL2, libopenmpt, the original game data)
#                      lives (default: this repo's root; set it from a worktree to point at
#                      the main checkout).
#   AS3D_ANDROID_ABIS  comma-separated ABIs (default arm64-v8a,x86_64).
#   AS3D_NATIVE_JOBS   parallel native compile jobs (default 4).
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
