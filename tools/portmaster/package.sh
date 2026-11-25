#!/usr/bin/env bash
# Makes the PortMaster package of the game (README.md "Linux handhelds (PortMaster)") from an
# aarch64 build of as3d_game: out/portmaster/openairstrike-<version>-portmaster.zip. No game data.
#
# The zip is laid out as a port folder of the PortMaster-New repository (ports/openairstrike/):
#   OpenAirStrike.sh           the launcher (control.txt, get_controls, $GPTOKEYB, log.txt)
#   port.json, gameinfo.xml    PortMaster's metadata
#   README.md                  the port's page (notes, controls, how it is built)
#   TESTING.md                 what a tester tries and sends back (not part of a submission)
#   openairstrike/             the port directory a player copies into ports/:
#     as3d_game.aarch64        the game (libstdc++ static; needs the device's SDL2, GL ES 3 and EGL)
#     pak_entries/             the paks' reference name lists (names only) for the importer
#     gamefiles/README.txt     where the player's own game files go
#     licenses/                OpenAirStrike's licence and the third-party code's
#     cover.png, VERSION
# A screenshot.png (640x480, gameplay) must be added before a submission: it would show game
# art, so it is never made or committed here.
#
# Usage: tools/portmaster/package.sh BINARY [VERSION]   (VERSION default: dev)
# Env:   AS3D_DATA_ROOT (where third_party_local/ is, for the licence texts)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BINARY="${1:?usage: package.sh BINARY [VERSION]}"
VERSION="${2:-dev}"
OUT="${REPO_ROOT}/out/portmaster"
STAGE="${OUT}/stage"

machine="$(readelf -h "${BINARY}" | sed -n 's/^ *Machine: *//p')"
case "${machine}" in
    *AArch64*) ;;
    *) echo "package.sh: ${BINARY} is not an aarch64 binary (${machine})" >&2; exit 1 ;;
esac

rm -rf "${STAGE}"
PORT="${STAGE}/openairstrike"
mkdir -p "${PORT}/pak_entries" "${PORT}/gamefiles"
install -m 755 "${SCRIPT_DIR}/OpenAirStrike.sh" "${STAGE}/OpenAirStrike.sh"
cp "${SCRIPT_DIR}/port.json" "${SCRIPT_DIR}/gameinfo.xml" "${SCRIPT_DIR}/README.md" "${SCRIPT_DIR}/TESTING.md" "${STAGE}/"
install -m 755 "${BINARY}" "${PORT}/as3d_game.aarch64"
"${STRIP:-strip}" "${PORT}/as3d_game.aarch64" 2>/dev/null || echo "package.sh: binary not stripped (no aarch64 strip)"
cp "${REPO_ROOT}"/tools/pak_entries/*.txt "${PORT}/pak_entries/"
cp "${SCRIPT_DIR}/gamefiles-README.txt" "${PORT}/gamefiles/README.txt"
cp "${REPO_ROOT}/apps/web/site/icons/icon-512.png" "${PORT}/cover.png"
echo "${VERSION}" > "${PORT}/VERSION"
"${REPO_ROOT}/tools/collect_licenses.sh" "${PORT}/licenses"

# Nothing from a game may be in there.
if find "${STAGE}" -iname '*.apk' -o -iname 'texts_*.txt' -o -iname '*.exe' -o -iname 'Settings.xml' | grep -q .; then
    echo "package.sh: game files in the package" >&2
    exit 1
fi
python3 -c 'import json, sys; json.load(open(sys.argv[1]))' "${STAGE}/port.json"
python3 -c 'import sys, xml.dom.minidom as m; m.parse(sys.argv[1])' "${STAGE}/gameinfo.xml"

ZIP="${OUT}/openairstrike-${VERSION}-portmaster.zip"
rm -f "${ZIP}"
# Python's zipfile keeps the files' modes (the launcher and the binary stay executable).
(cd "${STAGE}" && python3 -m zipfile -c "${ZIP}" OpenAirStrike.sh port.json gameinfo.xml README.md TESTING.md openairstrike)
echo "package.sh: ${ZIP}"
echo "package.sh: highest glibc symbol version: $(readelf -W --dyn-syms "${PORT}/as3d_game.aarch64" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)"
