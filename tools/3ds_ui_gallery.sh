#!/usr/bin/env bash
# tools/3ds_ui_gallery.sh: the 3DS screen gallery (docs/3ds-ui-design.md "UI foundation and the
# screen gallery (round 4, R4.4)"). Renders every screen of a 3DS build on Azahar (serialised: it
# waits for any other Azahar run, restores the SD's run.ini and profiles, kills only its own
# emulator) and writes out/ui3ds/gallery/<label>/<game>/<NN>_<screen>_{top,bottom,both}[@2x].png
# and one contact sheet per game, out/ui3ds/gallery/<label>/<game>_sheet.png.
#   tools/3ds_ui_gallery.sh [BINARY.3dsx] [LABEL] [--only run1,run2] [--tag NAME]
# BINARY defaults to build-3ds/apps/3ds/openairstrike.3dsx, LABEL to the current commit.
# Needs Azahar (flatpak org.azahar_emu.Azahar) with the games on its SD card, an X display and PIL.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${1:-$ROOT/build-3ds/apps/3ds/openairstrike.3dsx}"
[ $# -gt 0 ] && shift
ARGS=(--bin "$BIN")
if [ $# -gt 0 ] && [ "${1#--}" = "$1" ]; then
  ARGS+=(--label "$1")
  shift
fi
export DISPLAY="${DISPLAY:-:0}"
exec python3 -I "$ROOT/tools/3ds_ui_gallery.py" "${ARGS[@]}" "$@"
