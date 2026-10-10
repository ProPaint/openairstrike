#!/usr/bin/env bash
# Fetches SDL2 sources needed by the Android (and optionally desktop) build
# into third_party_local/ (gitignored), rather than committing SDL's source
# tree to this repository. See docs/android.md.
#
# Usage: tools/fetch_third_party.sh [openmpt|ctrtools]
#        (openmpt: libopenmpt only, for tools/3ds_build.sh;
#         ctrtools: makerom and bannertool only, for tools/3ds_package.sh)
# Env:   AS3D_DATA_ROOT   repo checkout to put third_party_local/ under
#                          (default: this repo's root; use the main checkout
#                          from a worktree so the download is shared).
set -euo pipefail

SDL_VERSION="2.30.12"
SDL_TARBALL="SDL2-${SDL_VERSION}.tar.gz"
SDL_URL="https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VERSION}/${SDL_TARBALL}"
# Verified by this script against the file downloaded from the URL above on
# 2026-09-28; see docs/android.md.
SDL_SHA256="ac356ea55e8b9dd0b2d1fa27da40ef7e238267ccf9324704850d5d47375b48ea"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DATA_ROOT="${AS3D_DATA_ROOT:-${REPO_ROOT}}"
TPL_DIR="${DATA_ROOT}/third_party_local"
DL_DIR="${TPL_DIR}/_dl"
SDL_DIR="${TPL_DIR}/SDL2-${SDL_VERSION}"

mkdir -p "${DL_DIR}"

# --- makerom and bannertool (the 3DS CIA, tools/3ds_package.sh; docs/3ds-port.md "Packaging") ---
# Neither is in the pinned devkitPro image. Every download is untrusted: each goes into an empty
# directory of its own under third_party_local/_dl/, is checked against the pinned SHA-256 before
# anything reads it, and the tools only ever run inside the pinned devkitPro container without
# network (tools/3ds_package.sh). makerom: the Linux release binary of 3DSGuy/Project_CTR.
# bannertool: Steveice10/bannertool is gone from GitHub (404) and the maintained fork
# carstene1ns/3ds-bannertool ships a Linux binary that needs glibc 2.38 (the image has 2.36), so
# its release source tarball is compiled here, in the image, with the image's g++.
MAKEROM_VERSION="0.18.4"
MAKEROM_URL="https://github.com/3DSGuy/Project_CTR/releases/download/makerom-v${MAKEROM_VERSION}/makerom-v${MAKEROM_VERSION}-ubuntu_x86_64.zip"
# Verified by this script against the file downloaded from the URL above on 2026-10-10.
MAKEROM_SHA256="dd596854718c195c6e3229286be485b122921715555af8ae5cf8e9a465d9f970"
BANNERTOOL_VERSION="1.2.3"
BANNERTOOL_URL="https://github.com/carstene1ns/3ds-bannertool/releases/download/${BANNERTOOL_VERSION}/bannertool-${BANNERTOOL_VERSION}.tar.gz"
# Verified by this script against the file downloaded from the URL above on 2026-10-10.
BANNERTOOL_SHA256="3ae61ff21c7762e75ba37a57f248cc0d6a9d4bff92207c57f7420a86dfbd08e6"
CTR_IMAGE="$(sed -n 's/^IMAGE=\(.*\)$/\1/p' "${SCRIPT_DIR}/3ds_build.sh" | head -1 | tr -d '"')"

# fetch_verified <url> <sha256> <empty dir> <file name>: downloads into the directory (made
# empty first when the file is not there yet) and checks the hash.
fetch_verified() {
    local url="$1" sha="$2" dir="$3" name="$4"
    if [ ! -f "${dir}/${name}" ]; then
        rm -rf "${dir}"
        mkdir -p "${dir}"
        echo "fetch_third_party: downloading ${url}"
        curl -fL --retry 3 -o "${dir}/${name}.part" "${url}"
        mv "${dir}/${name}.part" "${dir}/${name}"
    fi
    local actual
    actual="$(sha256sum "${dir}/${name}" | awk '{print $1}')"
    if [ "${actual}" != "${sha}" ]; then
        echo "fetch_third_party: SHA-256 mismatch for ${dir}/${name}" >&2
        echo "  expected: ${sha}" >&2
        echo "  actual:   ${actual}" >&2
        rm -f "${dir}/${name}"
        exit 1
    fi
}

fetch_ctrtools() {
    local mk_dir="${TPL_DIR}/makerom-${MAKEROM_VERSION}"
    if [ -x "${mk_dir}/makerom" ]; then
        echo "fetch_third_party: makerom ${MAKEROM_VERSION} already present at ${mk_dir}"
    else
        fetch_verified "${MAKEROM_URL}" "${MAKEROM_SHA256}" "${DL_DIR}/makerom-${MAKEROM_VERSION}" makerom.zip
        rm -rf "${mk_dir}"
        mkdir -p "${mk_dir}"
        unzip -q -o "${DL_DIR}/makerom-${MAKEROM_VERSION}/makerom.zip" makerom -d "${mk_dir}"
        chmod 755 "${mk_dir}/makerom"
    fi
    local bt_dir="${TPL_DIR}/bannertool-${BANNERTOOL_VERSION}"
    if [ -x "${bt_dir}/bannertool" ]; then
        echo "fetch_third_party: bannertool ${BANNERTOOL_VERSION} already present at ${bt_dir}"
    else
        fetch_verified "${BANNERTOOL_URL}" "${BANNERTOOL_SHA256}" "${DL_DIR}/bannertool-${BANNERTOOL_VERSION}" source.tar.gz
        local src="${TPL_DIR}/bannertool-${BANNERTOOL_VERSION}-src"
        rm -rf "${src}" "${bt_dir}"
        mkdir -p "${src}" "${bt_dir}"
        tar xzf "${DL_DIR}/bannertool-${BANNERTOOL_VERSION}/source.tar.gz" -C "${src}" --strip-components=1 --no-same-owner
        echo "fetch_third_party: compiling bannertool ${BANNERTOOL_VERSION} in ${CTR_IMAGE%%@*} (no network)"
        docker run --rm --network none --user "$(id -u):$(id -g)" -v "${src}:/src:ro" -v "${bt_dir}:/out" -w /src \
            "${CTR_IMAGE}" g++ -std=c++17 -O2 -DVERSION=\"${BANNERTOOL_VERSION}\" \
            -DSTBI_ONLY_PNG -DSTBI_NO_LINEAR -DSTBI_NO_STDIO -DSTB_VORBIS_NO_PUSHDATA_API -DSTB_VORBIS_NO_STDIO \
            -DDR_WAV_NO_STDIO -Isource -Isource/pc source/main.cpp source/log.cpp source/types.cpp source/utils.cpp \
            source/3ds/cbmd.cpp source/3ds/cwav.cpp source/3ds/lz11.cpp source/pc/stb_image.cpp \
            source/pc/stb_vorbis.cpp source/pc/dr_wav.cpp -o /out/bannertool
    fi
    echo "fetch_third_party: AS3D_MAKEROM=${mk_dir}/makerom AS3D_BANNERTOOL=${bt_dir}/bannertool"
}

if [ "${1:-}" = "ctrtools" ]; then
    fetch_ctrtools
    exit 0
fi

if [ "${1:-}" = "openmpt" ]; then
    echo "fetch_third_party: libopenmpt only"
elif [ -f "${SDL_DIR}/include/SDL.h" ]; then
    echo "fetch_third_party: SDL2 ${SDL_VERSION} already present at ${SDL_DIR}"
else
    TARBALL_PATH="${DL_DIR}/${SDL_TARBALL}"
    if [ ! -f "${TARBALL_PATH}" ]; then
        echo "fetch_third_party: downloading ${SDL_URL}"
        curl -fL --retry 3 -o "${TARBALL_PATH}.part" "${SDL_URL}"
        mv "${TARBALL_PATH}.part" "${TARBALL_PATH}"
    fi

    echo "fetch_third_party: verifying sha256"
    ACTUAL_SHA256="$(sha256sum "${TARBALL_PATH}" | awk '{print $1}')"
    if [ "${ACTUAL_SHA256}" != "${SDL_SHA256}" ]; then
        echo "fetch_third_party: SHA-256 mismatch for ${TARBALL_PATH}" >&2
        echo "  expected: ${SDL_SHA256}" >&2
        echo "  actual:   ${ACTUAL_SHA256}" >&2
        rm -f "${TARBALL_PATH}"
        exit 1
    fi

    echo "fetch_third_party: unpacking to ${TPL_DIR}"
    tar xzf "${TARBALL_PATH}" -C "${TPL_DIR}"
fi

[ "${1:-}" = "openmpt" ] || echo "fetch_third_party: AS3D_SDL2_DIR=${SDL_DIR}"

# --- libopenmpt (tracker/MO3 music decoding; see docs/audio.md) ---
# The "+release.makefile" source flavour (as opposed to "+release.autotools")
# bundles the small vendored decoders under include/ (stb_vorbis, minimp3,
# miniz) that the autotools tarball expects to find on the system instead.
# We only use the bundled stb_vorbis.c fallback (no system libvorbis dev
# headers are installed on the reference host); engine/src/audio/module.cmake
# builds libopenmpt from this source tree itself (see that file for the
# exact source list and defines), it is not built by this script.
OPENMPT_VERSION="0.8.9"
OPENMPT_TARBALL="libopenmpt-${OPENMPT_VERSION}+release.makefile.tar.gz"
OPENMPT_URL="https://lib.openmpt.org/files/libopenmpt/src/${OPENMPT_TARBALL}"
# Verified by this script against the file downloaded from the URL above on
# 2026-09-28; see docs/audio.md.
OPENMPT_SHA256="9273b88b67973cc69e54d748ab1b749399d6d07695f1c37d0c59f88b4106074f"
OPENMPT_DIR="${TPL_DIR}/libopenmpt-${OPENMPT_VERSION}+release"

if [ -f "${OPENMPT_DIR}/libopenmpt/libopenmpt.h" ]; then
    echo "fetch_third_party: libopenmpt ${OPENMPT_VERSION} already present at ${OPENMPT_DIR}"
else
    TARBALL_PATH="${DL_DIR}/${OPENMPT_TARBALL}"
    if [ ! -f "${TARBALL_PATH}" ]; then
        echo "fetch_third_party: downloading ${OPENMPT_URL}"
        curl -fL --retry 3 -o "${TARBALL_PATH}.part" "${OPENMPT_URL}"
        mv "${TARBALL_PATH}.part" "${TARBALL_PATH}"
    fi

    echo "fetch_third_party: verifying sha256"
    ACTUAL_SHA256="$(sha256sum "${TARBALL_PATH}" | awk '{print $1}')"
    if [ "${ACTUAL_SHA256}" != "${OPENMPT_SHA256}" ]; then
        echo "fetch_third_party: SHA-256 mismatch for ${TARBALL_PATH}" >&2
        echo "  expected: ${OPENMPT_SHA256}" >&2
        echo "  actual:   ${ACTUAL_SHA256}" >&2
        rm -f "${TARBALL_PATH}"
        exit 1
    fi

    echo "fetch_third_party: unpacking to ${TPL_DIR}"
    tar xzf "${TARBALL_PATH}" -C "${TPL_DIR}"
fi

echo "fetch_third_party: AS3D_OPENMPT_DIR=${OPENMPT_DIR}"
