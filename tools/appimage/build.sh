#!/usr/bin/env bash
# Builds the x86_64 AppImage of the game (Steam Deck and other Linux desktops; README.md
# "Steam Deck") into out/appimage/OpenAirStrike-<version>-x86_64.AppImage. No game data.
#
# Meant for an Ubuntu 22.04 system or container (the release workflow's linux job; glibc 2.35
# is then the minimum), with the packages listed in .github/workflows/release.yml (appimage).
# What the AppImage holds:
#   usr/bin/as3d_game            libstdc++ and libgcc linked statically
#   usr/lib/libSDL2-2.0.so.0     SDL 2.30.12 from the pinned source (fetch_third_party.sh), its
#                                X11, Wayland, PulseAudio, PipeWire and ALSA backends loaded at
#                                run time; the host's GL ES / EGL (libglvnd, Mesa) are used
#   usr/share/openairstrike/     the paks' reference name lists (tools/pak_entries) and the
#                                README put into the player's import folder
#   AppRun                       tools/appimage/AppRun: data root, import folder, full screen
# appimagetool and its runtime are pinned by SHA-256 (fetch_third_party.sh appimage).
#
# Usage: tools/appimage/build.sh [VERSION]      (default: dev)
# Env:   AS3D_BUILD_JOBS (default 2), AS3D_DATA_ROOT (where third_party_local/ is)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
VERSION="${1:-dev}"
JOBS="${AS3D_BUILD_JOBS:-2}"
TPL="${AS3D_DATA_ROOT:-${REPO_ROOT}}/third_party_local"
OUT="${REPO_ROOT}/out/appimage"
SDL_VERSION="2.30.12"

"${REPO_ROOT}/tools/fetch_third_party.sh"          # SDL2 and libopenmpt sources
"${REPO_ROOT}/tools/fetch_third_party.sh" appimage # appimagetool and the runtime
APPIMAGETOOL="${TPL}/_dl/appimagetool-1.9.0/appimagetool-x86_64.AppImage"
RUNTIME="${TPL}/_dl/appimage-runtime-20251108/runtime-x86_64"

# --- SDL2, shared, every desktop backend it finds headers for (all loaded with dlopen) ---
if [ ! -f "${OUT}/sdl/lib/libSDL2-2.0.so.0" ]; then
    rm -rf "${OUT}/sdl-build" "${OUT}/sdl"
    cmake -S "${TPL}/SDL2-${SDL_VERSION}" -B "${OUT}/sdl-build" -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="${OUT}/sdl" -DCMAKE_INSTALL_LIBDIR=lib \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF -DSDL_RPATH=OFF \
        -DSDL_X11_SHARED=ON -DSDL_WAYLAND_SHARED=ON -DSDL_PULSEAUDIO_SHARED=ON \
        -DSDL_PIPEWIRE_SHARED=ON -DSDL_ALSA_SHARED=ON -DSDL_KMSDRM=OFF
    cmake --build "${OUT}/sdl-build" -j"${JOBS}"
    cmake --install "${OUT}/sdl-build" >/dev/null
fi

# --- the game, against that SDL ---
PKG_CONFIG_PATH="${OUT}/sdl/lib/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}" \
    cmake -S "${REPO_ROOT}" -B "${OUT}/build" -DCMAKE_BUILD_TYPE=Release -DAS3D_BUILD_TESTS=OFF \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"
cmake --build "${OUT}/build" -j"${JOBS}" --target as3d_game_app

# --- the AppDir ---
APPDIR="${OUT}/AppDir"
rm -rf "${APPDIR}"
mkdir -p "${APPDIR}/usr/bin" "${APPDIR}/usr/lib" "${APPDIR}/usr/share/openairstrike/pak_entries" \
    "${APPDIR}/usr/share/doc/openairstrike"
cp "${OUT}/build/apps/game/as3d_game" "${APPDIR}/usr/bin/"
strip "${APPDIR}/usr/bin/as3d_game"
cp -L "${OUT}/sdl/lib/libSDL2-2.0.so.0" "${APPDIR}/usr/lib/"
strip --strip-unneeded "${APPDIR}/usr/lib/libSDL2-2.0.so.0"
cp "${REPO_ROOT}"/tools/pak_entries/*.txt "${APPDIR}/usr/share/openairstrike/pak_entries/"
cp "${SCRIPT_DIR}/IMPORT-README.txt" "${APPDIR}/usr/share/openairstrike/"
cp "${REPO_ROOT}/README.md" "${APPDIR}/usr/share/doc/openairstrike/"
"${REPO_ROOT}/tools/collect_licenses.sh" "${APPDIR}/usr/share/doc/openairstrike/licenses" --with-sdl
install -m 755 "${SCRIPT_DIR}/AppRun" "${APPDIR}/AppRun"
cp "${SCRIPT_DIR}/openairstrike.desktop" "${APPDIR}/"
cp "${REPO_ROOT}/apps/web/site/icons/icon-512.png" "${APPDIR}/openairstrike.png"
ln -s openairstrike.png "${APPDIR}/.DirIcon"

# Nothing from a game may be in there.
if find "${APPDIR}" -iname '*.apk' -o -iname 'texts_*.txt' -o -iname '*.exe' -o -iname 'Settings.xml' | grep -q .; then
    echo "appimage: game files in the AppDir" >&2
    exit 1
fi

# The binary needs nothing beyond SDL (bundled), GL ES / EGL, zlib and glibc.
echo "appimage: as3d_game needs:"
readelf -d "${APPDIR}/usr/bin/as3d_game" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/  \1/p'
echo "appimage: highest glibc symbol version: $(objdump -T "${APPDIR}/usr/bin/as3d_game" "${APPDIR}/usr/lib/libSDL2-2.0.so.0" \
    | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)"

TARGET="${OUT}/OpenAirStrike-${VERSION}-x86_64.AppImage"
rm -f "${TARGET}"
APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 VERSION="${VERSION}" \
    "${APPIMAGETOOL}" --no-appstream --runtime-file "${RUNTIME}" "${APPDIR}" "${TARGET}"
echo "appimage: ${TARGET}"
