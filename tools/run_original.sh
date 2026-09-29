#!/usr/bin/env bash
# Run the original DivoGames executables (as3d, as2, gulf) under a user-level Wine, inside a
# private virtual X display, and drive them for reference screenshots. See docs/running-originals.md.
#
#   tools/run_original.sh <key> [sub-commands] [-- game args]   start a game (as3d | as2 | gulf)
#   tools/run_original.sh [sub-commands]                         drive the running instance
#
# Sub-commands (any number, executed in order):
#   --shot <file.png>     capture the game window (whole display if no game window) as PNG
#   --shot-full <file>    capture the whole virtual display
#   --key <name>...       press keys (xdotool key names: Return, Escape, Up, space, F9, ctrl+q...);
#                         one --key takes one name; repeat --key for several
#   --hold <name> <s>     hold a key down for <s> seconds (games that poll key state)
#   --type <text>         type text (cheat words), one character every 50 ms
#   --click <x> <y>       left click at game-window coordinates (0,0 = top left of the 800x600 client)
#   --move <x> <y>        move the mouse to game-window coordinates
#   --wait <seconds>      sleep
#   --status              show whether the display and a game are running
#   --stop                kill the game (wineserver -k) and the virtual display
#   --reset               with <key>: recreate the private copy of the game before starting
#
# Environment: WINE_HOME (default ~/tools/wine), RUN_ORIGINAL_DISPLAY (default :77),
# RUN_ORIGINAL_TIMEOUT (seconds, default 600), RUN_ORIGINAL_MEM (default 3G),
# RUN_ORIGINAL_MIN_AVAIL_MB (default 3000).
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WINE_HOME="${WINE_HOME:-$HOME/tools/wine}"
DISP="${RUN_ORIGINAL_DISPLAY:-:77}"
TIMEOUT_S="${RUN_ORIGINAL_TIMEOUT:-600}"
MEM="${RUN_ORIGINAL_MEM:-3G}"
MIN_AVAIL_MB="${RUN_ORIGINAL_MIN_AVAIL_MB:-3000}"
RUN_DIR="$WINE_HOME/run"
X11="$WINE_HOME/x11/usr"
export WINEPREFIX="$WINE_HOME/prefix-airstrike"
export WINEDLLOVERRIDES="winemenubuilder.exe=d;mscoree=d;mshtml=d"
export WINEDEBUG="${WINEDEBUG:-err+all,fixme-all}"
export DISPLAY="$DISP"

die() { echo "run_original: $*" >&2; exit 1; }
log() { echo "run_original: $*" >&2; }

[ -d "$WINE_HOME" ] || die "no Wine setup at $WINE_HOME (set WINE_HOME); see docs/running-originals.md"
WINE_DIR="$(ls -d "$WINE_HOME"/wine-*/ 2>/dev/null | sort -V | tail -1)"
[ -n "$WINE_DIR" ] && [ -x "$WINE_DIR/bin/wine" ] || die "no wine-*/bin/wine under $WINE_HOME"
WINE="$WINE_DIR/bin/wine"
WINESERVER="$WINE_DIR/bin/wineserver"
XVFB="$X11/bin/Xvfb"
XDOTOOL="$X11/bin/xdotool"
X11_LIB="$X11/lib/x86_64-linux-gnu"
XWD2PNG="$WINE_HOME/bin/xwd2png.py"
mkdir -p "$RUN_DIR"

xdo() { LD_LIBRARY_PATH="$X11_LIB" "$XDOTOOL" "$@"; }

game_src() {
  case "$1" in
    as3d) echo "$REPO/third_party_local/original" ;;
    as2)  echo "$REPO/third_party_local/games/as2" ;;
    gulf) echo "$REPO/third_party_local/games/gulf" ;;
    *) die "unknown game key '$1' (as3d | as2 | gulf)" ;;
  esac
}
game_exe() {
  case "$1" in
    as3d) echo "AirStrike3D.exe" ;;
    as2)  echo "AirStrike3D II.exe" ;;
    gulf) echo "AirStrike3D II - Gulf.exe" ;;
  esac
}

display_running() {
  [ -f "$RUN_DIR/xvfb.pid" ] && kill -0 "$(cat "$RUN_DIR/xvfb.pid")" 2>/dev/null
}

start_display() {
  display_running && return 0
  local n="${DISP#:}"
  [ -e "/tmp/.X11-unix/X$n" ] && die "display $DISP is in use by someone else (set RUN_ORIGINAL_DISPLAY)"
  rm -f "/tmp/.X$n-lock"
  # 1024x768 so that the 800x600 client area plus Wine's window frame fits on screen.
  LD_LIBRARY_PATH="$X11_LIB" setsid "$XVFB" "$DISP" -screen 0 1024x768x24 -nolisten tcp \
    +extension GLX >"$RUN_DIR/xvfb.log" 2>&1 </dev/null &
  echo $! >"$RUN_DIR/xvfb.pid"
  for _ in $(seq 50); do [ -e "/tmp/.X11-unix/X$n" ] && return 0; sleep 0.1; done
  die "Xvfb did not start, see $RUN_DIR/xvfb.log"
}

init_prefix() {
  [ -f "$WINEPREFIX/system.reg" ] && return 0
  log "creating Wine prefix $WINEPREFIX"
  nice timeout 300 "$WINE_DIR/bin/wineboot" -i >/dev/null 2>&1 || true
  "$WINESERVER" -w
  local u
  for u in "$WINEPREFIX"/drive_c/users/"$USER"/{Desktop,Documents,Downloads,Music,Pictures,Videos}; do
    if [ -L "$u" ]; then rm "$u"; mkdir "$u"; fi
  done
  [ -f "$WINE_HOME/prefix-settings.reg" ] || die "missing $WINE_HOME/prefix-settings.reg"
  "$WINE" regedit /S "$(sed 's#/#\\#g; s#^#Z:#' <<<"$WINE_HOME/prefix-settings.reg")"
  "$WINESERVER" -w
}

prepare_copy() {
  local key="$1" dst="$WINE_HOME/games/$1" src
  src="$(game_src "$key")"
  if [ "${RESET:-0}" = 1 ] && [ -d "$dst" ]; then rm -rf "$dst"; fi
  [ -d "$dst" ] && return 0
  [ -d "$src" ] || die "original install not found: $src"
  log "copying $src to $dst"
  mkdir -p "$WINE_HOME/games"
  cp -a "$src" "$dst"
  # Never post scores (the games' WININET target; the prefix also points WININET at a dead proxy).
  sed -i 's/PostScores allow="1"/PostScores allow="0"/' "$dst/data/Settings.xml"
  # Windowed 800x600, sound off. VideoMode: as3d index 1 = 800x600; sequels -1 = pick 800x600.
  # config.ini is written back by the game at exit, so these edits persist in the copy.
  local vm=-1; [ "$key" = as3d ] && vm=1
  sed -i -e 's/^Fullscreen=.*/Fullscreen=0/' -e "s/^VideoMode=.*/VideoMode=$vm/" \
    -e 's/^RefreshRate=.*/RefreshRate=0/' -e 's/^FirstRun=.*/FirstRun=0/' \
    -e 's/^SfxVolume=.*/SfxVolume=0.0000/' -e 's/^MusicVolume=.*/MusicVolume=0.0000/' \
    -e 's/^ForceStdModes=.*/ForceStdModes=1/' -e 's/\r$//' "$dst/config.ini"
  # Under Xvfb Wine reports only the screen's own mode; the sequels' ForceStdModes=1 adds the
  # standard 4:3 modes so that VideoMode -1 finds 800x600.
}

game_running() {
  [ -f "$RUN_DIR/game.pid" ] && kill -0 "$(cat "$RUN_DIR/game.pid")" 2>/dev/null
}

start_game() {
  local key="$1"; shift
  game_running && die "a game is already running (use --stop first)"
  local avail
  avail="$(free -m | awk '/^Mem:/ {print $7}')"
  [ "$avail" -ge "$MIN_AVAIL_MB" ] || die "only ${avail} MB available, need $MIN_AVAIL_MB MB"
  prepare_copy "$key"
  start_display
  init_prefix
  local dir="$WINE_HOME/games/$key" exe
  exe="$(game_exe "$key")"
  log "starting $key ($exe $*) on $DISP, timeout ${TIMEOUT_S}s, MemoryMax=$MEM"
  local cap=()
  if systemd-run --user --scope -q -p MemoryMax=16M true 2>/dev/null; then
    cap=(systemd-run --user --scope -q -p "MemoryMax=$MEM" -p "MemorySwapMax=0")
  else
    log "systemd-run --user unavailable: running without a memory cap"
  fi
  (cd "$dir" && exec setsid "${cap[@]}" nice -n 5 timeout "$TIMEOUT_S" "$WINE" "$exe" "$@" \
     >"$RUN_DIR/wine-$key.log" 2>&1 </dev/null) &
  echo $! >"$RUN_DIR/game.pid"
  echo "$key" >"$RUN_DIR/game.key"
  for _ in $(seq 120); do
    if game_window >/dev/null; then log "window is up"; return 0; fi
    game_running || die "the game exited; see $RUN_DIR/wine-$key.log and $dir/game.log"
    sleep 0.5
  done
  log "no game window after 60 s (still starting?)"
}

# Prints "id x y w h" of the game's main window, fails if none.
game_window() {
  local w g
  # WM_CLASS is the exe name ("airstrike3d.exe", "airstrike3d ii.exe", ...); titles differ
  # ("Game" for as3d, "Airstrike II - Divo Games" for as2).
  for w in $(xdo search --onlyvisible --class "airstrike3d" 2>/dev/null); do
    g="$(xdo getwindowgeometry --shell "$w" 2>/dev/null)" || continue
    eval "$g"
    if [ "${WIDTH:-0}" -ge 320 ]; then echo "$w $X $Y $WIDTH $HEIGHT"; return 0; fi
  done
  return 1
}

shot() {
  local out="$1" full="${2:-0}" tmp
  display_running || die "no virtual display running"
  mkdir -p "$(dirname "$out")"
  tmp="$(mktemp "$RUN_DIR/shot.XXXXXX.xwd")"
  xwd -root -silent -display "$DISP" >"$tmp"
  local crop=""
  if [ "$full" = 0 ] && read -r _ x y w h < <(game_window); then crop="$x,$y,$w,$h"; fi
  python3 - "$XWD2PNG" "$tmp" "$out" "$crop" <<'EOF'
import importlib.util, sys
spec = importlib.util.spec_from_file_location("x", sys.argv[1]); m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
m.main(sys.argv[2], sys.argv[3])
if sys.argv[4]:
    from PIL import Image
    x, y, w, h = map(int, sys.argv[4].split(','))
    Image.open(sys.argv[3]).crop((x, y, x + w, y + h)).save(sys.argv[3])
EOF
  rm -f "$tmp"
  log "saved $out"
}

to_screen() {  # game-window coordinates to screen coordinates
  local x="$1" y="$2" wx=0 wy=0
  if read -r _ wx wy _ _ < <(game_window); then :; fi
  echo "$((x + wx)) $((y + wy))"
}

stop_all() {
  "$WINESERVER" -k 2>/dev/null || true
  if [ -f "$RUN_DIR/game.pid" ]; then kill "$(cat "$RUN_DIR/game.pid")" 2>/dev/null || true; rm -f "$RUN_DIR/game.pid"; fi
  if display_running; then kill "$(cat "$RUN_DIR/xvfb.pid")" 2>/dev/null || true; fi
  rm -f "$RUN_DIR/xvfb.pid"
  sleep 1
  log "stopped"
}

# ---- argument parsing ----
KEY=""
case "${1:-}" in as3d|as2|gulf) KEY="$1"; shift ;; esac
CMDS=()
GAME_ARGS=()
while [ $# -gt 0 ]; do
  if [ "$1" = "--" ]; then shift; GAME_ARGS=("$@"); break; fi
  CMDS+=("$1"); shift
done

# --reset must be seen before the start
RESET=0
for c in "${CMDS[@]:-}"; do [ "$c" = "--reset" ] && RESET=1; done

if [ -n "$KEY" ]; then start_game "$KEY" "${GAME_ARGS[@]}"; fi
[ -z "$KEY" ] && [ ${#CMDS[@]} -eq 0 ] && { sed -n '2,27p' "$0"; exit 0; }

i=0
while [ $i -lt ${#CMDS[@]} ]; do
  c="${CMDS[$i]}"
  a1="${CMDS[$((i + 1))]:-}"; a2="${CMDS[$((i + 2))]:-}"
  case "$c" in
    --shot) shot "$a1" 0; i=$((i + 2)) ;;
    --shot-full) shot "$a1" 1; i=$((i + 2)) ;;
    --key) xdo key --delay 80 "$a1"; i=$((i + 2)) ;;
    --hold) xdo keydown "$a1"; sleep "$a2"; xdo keyup "$a1"; i=$((i + 3)) ;;
    --type) xdo type --delay 50 "$a1"; i=$((i + 2)) ;;
    --click)
      # The games read relative mouse motion: hover first, nudge, then press and release.
      read -r sx sy < <(to_screen "$a1" "$a2")
      xdo mousemove "$sx" "$((sy - 1))" sleep 0.25 mousemove "$sx" "$sy" sleep 0.25 \
        mousedown 1 sleep 0.1 mouseup 1
      i=$((i + 3)) ;;
    --move) read -r sx sy < <(to_screen "$a1" "$a2"); xdo mousemove "$sx" "$sy"; i=$((i + 3)) ;;
    --wait) sleep "$a1"; i=$((i + 2)) ;;
    --status)
      display_running && echo "display $DISP: running" || echo "display $DISP: stopped"
      game_running && echo "game: running ($(cat "$RUN_DIR/game.key"))" || echo "game: not running"
      game_window || true
      i=$((i + 1)) ;;
    --stop) stop_all; i=$((i + 1)) ;;
    --reset) i=$((i + 1)) ;;
    *) die "unknown sub-command '$c'" ;;
  esac
done
