#!/usr/bin/env bash
# Fetches SDL2 sources needed by the Android (and optionally desktop) build
# into third_party_local/ (gitignored), rather than committing SDL's source
# tree to this repository. See docs/android.md.
#
# Usage: tools/fetch_third_party.sh
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

if [ -f "${SDL_DIR}/include/SDL.h" ]; then
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

echo "fetch_third_party: AS3D_SDL2_DIR=${SDL_DIR}"
