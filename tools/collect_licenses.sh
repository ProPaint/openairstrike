#!/usr/bin/env bash
# Copies the licence texts of what a desktop build of as3d_game links (the project's own and the
# third-party code fetched by tools/fetch_third_party.sh) into DEST, one file per component.
# Used by the AppImage (tools/appimage/build.sh) and the PortMaster package
# (tools/portmaster/package.sh). Run after the build: the trees are those under third_party_local/.
#
# Usage: tools/collect_licenses.sh DEST [--with-sdl]
#        (--with-sdl: SDL2's too, for a package that ships libSDL2)
# Env:   AS3D_DATA_ROOT   where third_party_local/ is (default: this repository's root)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
TPL="${AS3D_DATA_ROOT:-${REPO_ROOT}}/third_party_local"
DEST="${1:?usage: collect_licenses.sh DEST [--with-sdl]}"
mkdir -p "${DEST}"

copy() { # copy <source file> <name in DEST>
    if [ ! -f "$1" ]; then
        echo "collect_licenses: missing $1" >&2
        exit 1
    fi
    cp "$1" "${DEST}/$2"
}

copy "${REPO_ROOT}/LICENSE" LICENSE.openairstrike.txt
copy "${TPL}/libopenmpt-0.8.9+release/LICENSE" LICENSE.libopenmpt.txt
copy "${TPL}/libarchive-3.8.11/COPYING" LICENSE.libarchive.txt
copy "${TPL}/xz-5.8.4/COPYING.0BSD" LICENSE.liblzma.txt
copy "${TPL}/libdatachannel-0.24.6/LICENSE" LICENSE.libdatachannel.txt
copy "${TPL}/libdatachannel-0.24.6/deps/libjuice/LICENSE" LICENSE.libjuice.txt
copy "${TPL}/libdatachannel-0.24.6/deps/usrsctp/LICENSE.md" LICENSE.usrsctp.txt
copy "${TPL}/libdatachannel-0.24.6/deps/plog/LICENSE" LICENSE.plog.txt
copy "${TPL}/mbedtls-3.6.8/LICENSE" LICENSE.mbedtls.txt
if [ "${2:-}" = "--with-sdl" ]; then
    copy "${TPL}/SDL2-2.30.12/LICENSE.txt" LICENSE.SDL2.txt
fi
cat > "${DEST}/THIRD-PARTY.txt" <<'EOF'
OpenAirStrike (MIT, LICENSE.openairstrike.txt) is built with this third-party code, compiled
from the unmodified release sources:

  libopenmpt 0.8.9 (BSD 3-clause; with its bundled stb_vorbis, public domain / MIT)
  libarchive 3.8.11 (BSD 2-clause) and liblzma from xz 5.8.4 (0BSD)
  libdatachannel 0.24.6 (MPL-2.0; source: https://github.com/paullouisageneau/libdatachannel)
    with libjuice (MPL-2.0), usrsctp (BSD 3-clause) and plog (MIT)
  Mbed TLS 3.6.8 (Apache-2.0)
  SDL2 2.30.12 (zlib licence), where the package ships libSDL2
  stb_image_write (public domain / MIT), in the OpenAirStrike source tree

No game data is included: the game files come from the player's own copy of each game.
EOF
echo "collect_licenses: licences in ${DEST}"
