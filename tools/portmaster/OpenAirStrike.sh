#!/bin/bash
# OpenAirStrike for PortMaster (tools/portmaster/, README.md "Linux handhelds (PortMaster)").
# The player copies their own game files into ports/openairstrike/gamefiles/; the game imports
# them at the next start (as3d_game --import-dir) into ports/openairstrike/data/.
# Everything the game prints goes to ports/openairstrike/log.txt, after a header that says what
# device, firmware and libraries it ran on: what a tester sends back (TESTING.md).
# Extra arguments for the game (for example --fps --perf) can be put on one line in
# ports/openairstrike/args.txt.

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source $controlfolder/control.txt
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"

get_controls

GAMEDIR=/$directory/ports/openairstrike
CONFDIR="$GAMEDIR/conf"

mkdir -p "$CONFDIR" "$GAMEDIR/gamefiles" "$GAMEDIR/data"
cd "$GAMEDIR" || exit 1

> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

# Older PortMaster releases (still shipped by some firmwares, e.g. AmberELEC on the RG351P) have a
# control.txt that sets neither DEVICE_ARCH nor the display size, and has no pm_* helpers.
DEVICE_ARCH="${DEVICE_ARCH:-$(uname -m)}"
command -v pm_platform_helper >/dev/null 2>&1 || pm_platform_helper() { :; }
command -v pm_finish >/dev/null 2>&1 || pm_finish() { :; }

BINARY="as3d_game.${DEVICE_ARCH}"

# What a tester needs to know about this device (TESTING.md).
echo "=== OpenAirStrike $(cat "$GAMEDIR/VERSION" 2>/dev/null) on $(date)"
echo "device: ${DEVICE_NAME:-?} cfw: ${CFW_NAME:-?} ${CFW_VERSION:-} arch: ${DEVICE_ARCH:-?} ram: ${DEVICE_RAM:-?} GB"
echo "display: ${DISPLAY_WIDTH:-?}x${DISPLAY_HEIGHT:-?} analog sticks: ${ANALOG_STICKS:-?}"
echo "kernel: $(uname -srm)"
echo "PortMaster: ${controlfolder} $(cat "$controlfolder/version" 2>/dev/null || echo '(no version file)')"
echo "glibc: $(getconf GNU_LIBC_VERSION 2>/dev/null || echo '?')"
echo "SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-} SDL_AUDIODRIVER=${SDL_AUDIODRIVER:-}"
if command -v ldd >/dev/null 2>&1; then
  echo "libraries:"
  ldd "$GAMEDIR/$BINARY" 2>&1 | sed 's/^/  /'
fi
echo "game files folder:"
ls -la "$GAMEDIR/gamefiles" 2>&1 | sed 's/^/  /'
echo "==="

# Profiles and saves in the port folder (the game keeps them in $XDG_DATA_HOME/airstrike3d).
export XDG_DATA_HOME="$CONFDIR"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
# The paks' reference name lists (names only, no game data) for the importer.
export AS3D_PAK_ENTRIES="$GAMEDIR/pak_entries"

EXTRA_ARGS=""
[ -f "$GAMEDIR/args.txt" ] && EXTRA_ARGS="$(head -n 1 "$GAMEDIR/args.txt")"

$ESUDO chmod +x "$GAMEDIR/$BINARY"

# The game reads the pad itself (SDL game controller, with the mapping above); gptokeyb is
# started without a key map, for its Select + Start hotkey that closes the game.
$GPTOKEYB "$BINARY" &
pm_platform_helper "$GAMEDIR/$BINARY"
# shellcheck disable=SC2086 # EXTRA_ARGS is a list of arguments
./$BINARY --data "$GAMEDIR/data" --import-dir "$GAMEDIR/gamefiles" --fullscreen \
  --size "${DISPLAY_WIDTH:-640}x${DISPLAY_HEIGHT:-480}" $EXTRA_ARGS
echo "=== exit code $?"

pm_finish
