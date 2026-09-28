#!/usr/bin/env bash
# Builds the WP-18 Android bring-up APK.
#   tools/android_build.sh
# Env:
#   AS3D_DATA_ROOT   where third_party_local/ and the original game data live
#                    (default: this repo's root; set this from a worktree to
#                    point at the main checkout).
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

echo "== copying pak archives into assets (uncompressed via noCompress) =="
mkdir -p "${ASSETS_DIR}"
for pak in pak0.apk pak1.apk pak2.apk; do
    SRC="${DATA_DIR}/${pak}"
    if [ ! -f "${SRC}" ]; then
        echo "android_build: missing game data file ${SRC}" >&2
        echo "  Set AS3D_DATA_ROOT to a checkout that has third_party_local/original/data/." >&2
        exit 1
    fi
    cp -f "${SRC}" "${ASSETS_DIR}/${pak}"
done
ls -la "${ASSETS_DIR}"

echo "== writing android/local.properties (gitignored) =="
cat > "${REPO_ROOT}/android/local.properties" <<EOF
sdk.dir=${ANDROID_HOME}
EOF

echo "== gradlew assembleDebug =="
cd "${REPO_ROOT}/android"
./gradlew --console=plain assembleDebug

APK="${REPO_ROOT}/android/app/build/outputs/apk/debug/app-debug.apk"
if [ ! -f "${APK}" ]; then
    echo "android_build: expected APK not found at ${APK}" >&2
    exit 1
fi
SIZE="$(du -h "${APK}" | awk '{print $1}')"
echo "== done =="
echo "APK: ${APK} (${SIZE})"
