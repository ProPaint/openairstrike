#!/usr/bin/env bash
# Plays mission 1 of the game APK on a device or emulator, unattended (the bot pilot,
# through the `bot` intent extra), then goes through the menus, and checks it in logcat:
#
#   1. launches with --ez bot true --ei level 1 --ez rebuild_on_resume true (straight into
#      the mission, no menus) and waits for AS3D_GAME_START and AS3D_GAME_FRAME n=600;
#   2. injects touch gestures with `adb shell input`: a drag in the play-field, taps on the
#      missile and next-weapon buttons, the pause button then a tap to continue, the back key
#      then a tap;
#   3. sends the app to the background (HOME), brings it back, checks it resumes paused with
#      its GL resources rebuilt, taps to continue and waits for more frames;
#   4. waits for AS3D_GAME_FRAME n=3600;
#   5. restarts the app without extras (the front end) and taps through it, waiting for the
#      AS3D_SCREEN markers: main menu (after the intro pages), Start Game, Start, 600 frames of
#      mission 1, the pause button (the in-game menu), Resume, pause again, Quit to the main
#      menu, the back key (exit confirmation), No; screenshots menu_*.png;
#   6. prints the AS3D_PERF lines.
#
# Fails on a FATAL marker, a Java exception or a native crash, or a timeout. Screenshots and
# the logcat capture go to $AS3D_SMOKE_OUT (default: <AS3D_DATA_ROOT or repo>/out/m8).
# Screenshots show copyrighted game art: they stay in the gitignored out/ directory.
#
# Boots the `atticpad-test` AVD headless if no device is online (it belongs to another
# project: never wiped or reconfigured; only shut down again if this script started it),
# with at most 2048 MB guest memory and only when at least 5 GB of host memory is available.
# AS3D_EMU_GPU picks the emulator's GPU mode (default swiftshader_indirect, a CPU renderer;
# "host" uses the host GPU when one is reachable).
#
#   tools/android_smoke.sh
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
# shellcheck source=./android_env.sh
source "${SCRIPT_DIR}/android_env.sh"

APP_ID="org.as3dport.game"
ACTIVITY="${APP_ID}/org.as3dport.game.GameActivity"
APK="${REPO_ROOT}/android/app/build/outputs/apk/debug/app-debug.apk"
AVD_NAME="atticpad-test"
DATA_ROOT="${AS3D_DATA_ROOT:-${REPO_ROOT}}"
OUT_DIR="${AS3D_SMOKE_OUT:-${DATA_ROOT}/out/m8}"
LOG_FILE="${OUT_DIR}/logcat.txt"
BOOT_TIMEOUT=240
START_TIMEOUT=240
MIN_FREE_MB=5000

mkdir -p "${OUT_DIR}"
rm -f "${OUT_DIR}"/*.png "${LOG_FILE}"

if [ ! -f "${APK}" ]; then
    echo "android_smoke: APK not found at ${APK}; run tools/android_build.sh first" >&2
    exit 1
fi

STARTED_EMULATOR=0
EMULATOR_PID=""
SERIAL=""
LOGCAT_PID=""

cleanup() {
    if [ -n "${LOGCAT_PID}" ]; then kill "${LOGCAT_PID}" >/dev/null 2>&1 || true; fi
    if [ "${STARTED_EMULATOR}" = "1" ]; then
        echo "android_smoke: shutting down the emulator (we started it)"
        if [ -n "${SERIAL}" ]; then adb -s "${SERIAL}" emu kill >/dev/null 2>&1 || true; fi
        for _ in $(seq 1 30); do
            kill -0 "${EMULATOR_PID}" 2>/dev/null || break
            sleep 1
        done
        kill "${EMULATOR_PID}" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

fail() {
    echo "android_smoke: FAILED - $*" >&2
    if [ -f "${LOG_FILE}" ]; then
        echo "== last AS3D / crash lines ==" >&2
        grep -E "AS3D|FATAL|Fatal signal|AndroidRuntime|DEBUG   :" "${LOG_FILE}" | tail -n 40 >&2
    fi
    exit 1
}

# ---------------------------------------------------------------------------------------
echo "== device =="
adb start-server >/dev/null 2>&1
ONLINE="$(adb devices | awk 'NR>1 && $2=="device" {print $1}' | head -n1)"
if [ -n "${ONLINE}" ]; then
    SERIAL="${ONLINE}"
    echo "android_smoke: using already-online device ${SERIAL}"
else
    AVAIL_MB="$(awk '/MemAvailable/ {print int($2/1024)}' /proc/meminfo)"
    if [ "${AVAIL_MB}" -lt "${MIN_FREE_MB}" ]; then
        echo "android_smoke: only ${AVAIL_MB} MB of host memory available, need ${MIN_FREE_MB}; not starting the emulator" >&2
        exit 1
    fi
    if ! "${ANDROID_HOME}/emulator/emulator" -list-avds | grep -qx "${AVD_NAME}"; then
        echo "android_smoke: AVD '${AVD_NAME}' not found" >&2
        exit 1
    fi
    if [ -r /dev/kvm ] && [ -w /dev/kvm ]; then
        echo "android_smoke: /dev/kvm accessible"
    else
        echo "android_smoke: WARNING /dev/kvm not accessible, the emulator will be very slow"
    fi
    echo "android_smoke: booting '${AVD_NAME}' headless (2048 MB, -gpu ${AS3D_EMU_GPU:-swiftshader_indirect}; ${AVAIL_MB} MB host memory available)"
    "${ANDROID_HOME}/emulator/emulator" -avd "${AVD_NAME}" \
        -no-window -no-audio -no-boot-anim -no-snapshot-save -memory 2048 \
        -gpu "${AS3D_EMU_GPU:-swiftshader_indirect}" \
        >"${OUT_DIR}/emulator.log" 2>&1 &
    EMULATOR_PID=$!
    STARTED_EMULATOR=1
    for _ in $(seq 1 90); do
        kill -0 "${EMULATOR_PID}" 2>/dev/null || { cat "${OUT_DIR}/emulator.log" >&2; fail "emulator exited early"; }
        CAND="$(adb devices | awk '$2=="offline" || $2=="device" {print $1}' | grep '^emulator-' | head -n1)"
        if [ -n "${CAND}" ]; then SERIAL="${CAND}"; break; fi
        sleep 2
    done
    [ -n "${SERIAL}" ] || fail "emulator never registered with adb"
    adb -s "${SERIAL}" wait-for-device
    BOOTED=0
    for i in $(seq 1 "${BOOT_TIMEOUT}"); do
        if [ "$(adb -s "${SERIAL}" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r\n')" = "1" ]; then
            BOOTED=1
            break
        fi
        kill -0 "${EMULATOR_PID}" 2>/dev/null || fail "emulator died while booting"
        sleep 1
    done
    [ "${BOOTED}" = "1" ] || fail "timed out waiting for boot"
    echo "android_smoke: booted in ${i}s"
    # A cold emulator keeps working for a while after boot_completed; starting the game
    # (software GLES) on top of that has made System UI stop responding once.
    sleep 25
fi
adb -s "${SERIAL}" shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1 || true
adb -s "${SERIAL}" shell wm dismiss-keyguard >/dev/null 2>&1 || true
# The one-time "Viewing full screen" hint would sit over the game and swallow the taps.
adb -s "${SERIAL}" shell settings put secure immersive_mode_confirmations confirmed >/dev/null 2>&1 || true
# An overloaded emulator can show "System UI isn't responding" over the game; such dialogs
# would swallow the taps. (The emulator's data partition is temporary.)
adb -s "${SERIAL}" shell settings put global hide_error_dialogs 1 >/dev/null 2>&1 || true

echo "== install =="
adb -s "${SERIAL}" uninstall "${APP_ID}" >/dev/null 2>&1 || true
adb -s "${SERIAL}" install -r "${APK}" || fail "adb install failed"

# ---------------------------------------------------------------------------------------
adb -s "${SERIAL}" logcat -c
adb -s "${SERIAL}" logcat -v time >"${LOG_FILE}" 2>/dev/null &
LOGCAT_PID=$!

check_crash() {
    if grep -qE "AS3D.*FATAL|FATAL EXCEPTION|Fatal signal|SIGSEGV|SIGABRT" "${LOG_FILE}"; then
        fail "crash or FATAL marker in logcat"
    fi
}

# wait_for PATTERN TIMEOUT: waits until PATTERN (extended regex) appears in logcat.
wait_for() {
    local pattern="$1" timeout="$2" deadline=$((SECONDS + $2))
    while [ "${SECONDS}" -lt "${deadline}" ]; do
        check_crash
        if grep -qE "${pattern}" "${LOG_FILE}"; then return 0; fi
        sleep 1
    done
    fail "timed out after ${timeout}s waiting for /${pattern}/"
}

# count PATTERN: occurrences so far (to wait for the next one).
count() { grep -cE "$1" "${LOG_FILE}" || true; }
wait_more() {
    local pattern="$1" before="$2" timeout="$3" deadline=$((SECONDS + $3))
    while [ "${SECONDS}" -lt "${deadline}" ]; do
        check_crash
        if [ "$(count "${pattern}")" -gt "${before}" ]; then return 0; fi
        sleep 1
    done
    fail "timed out after ${timeout}s waiting for another /${pattern}/"
}

shot() {
    adb -s "${SERIAL}" exec-out screencap -p >"${OUT_DIR}/$1.png" 2>/dev/null
    if [ -s "${OUT_DIR}/$1.png" ]; then echo "android_smoke: screenshot ${OUT_DIR}/$1.png"; else echo "android_smoke: WARNING screenshot $1 failed"; fi
}

# An overloaded emulator (software GLES) sometimes shows "System UI isn't responding"; the
# dialog would swallow the taps. Closing system dialogs dismisses it.
dismiss_dialogs() {
    if adb -s "${SERIAL}" shell dumpsys window 2>/dev/null | grep -q "Application Not Responding"; then
        echo "android_smoke: dismissing an ANR dialog of the system (emulator overload)"
        adb -s "${SERIAL}" shell am broadcast -a android.intent.action.CLOSE_SYSTEM_DIALOGS >/dev/null 2>&1 || true
        sleep 2
    fi
}
tap() { dismiss_dialogs; adb -s "${SERIAL}" shell input tap "$1" "$2"; }

echo "== launch (bot pilot) =="
adb -s "${SERIAL}" shell am start -W -n "${ACTIVITY}" --ez bot true --ei level 1 --ez rebuild_on_resume true || fail "am start failed"
wait_for "AS3D_GAME_START" "${START_TIMEOUT}"
wait_for "AS3D_LAYOUT" 30
grep -E "AS3D_GAME_START|AS3D_LAYOUT|AS3D_INSETS" "${LOG_FILE}" | tail -n 4

# Button centres (framebuffer pixels = screen pixels, the surface is full screen).
LAYOUT="$(grep -E "AS3D_LAYOUT" "${LOG_FILE}" | tail -n1)"
pos() { echo "${LAYOUT}" | sed -nE "s/.* $1=([0-9]+),([0-9]+).*/\1 \2/p"; }
FIELD="$(echo "${LAYOUT}" | sed -nE 's/.* field=([0-9]+),([0-9]+),([0-9]+),([0-9]+).*/\1 \2 \3 \4/p')"
read -r FX0 FY0 FX1 FY1 <<<"${FIELD}"
[ -n "${FX1:-}" ] || fail "no play-field in the layout line"
FCX=$(((FX0 + FX1) / 2))
FCY=$(((FY0 + FY1) * 2 / 3))
read -r PAUSE_X PAUSE_Y <<<"$(pos pause)"
read -r MISSILE_X MISSILE_Y <<<"$(pos missile)"
read -r NEXTW_X NEXTW_Y <<<"$(pos next_weapon)"
[ -n "${PAUSE_Y:-}" ] && [ -n "${MISSILE_Y:-}" ] || fail "cannot parse the button layout"

wait_for "AS3D_GAME_FRAME n=600 " 240
dismiss_dialogs
shot 01_playing

echo "== touch: drag in the play-field, missile and next-weapon buttons =="
BEFORE="$(count "AS3D_TOUCH down")"
dismiss_dialogs
adb -s "${SERIAL}" shell input swipe "${FCX}" "${FCY}" "$((FCX + (FX1 - FX0) / 6))" "${FCY}" 1500
tap "${MISSILE_X}" "${MISSILE_Y}"
tap "${NEXTW_X}" "${NEXTW_Y}"
wait_more "AS3D_TOUCH down" "$((BEFORE + 2))" 20
grep -qE "AS3D_TOUCH down .* on=field" "${LOG_FILE}" || fail "the drag did not land in the play-field"
grep -qE "AS3D_TOUCH down .* on=missile" "${LOG_FILE}" || fail "the missile button was not hit"
grep -qE "AS3D_TOUCH down .* on=next_weapon" "${LOG_FILE}" || fail "the next-weapon button was not hit"
shot 02_after_touch

echo "== pause button, then a tap to continue =="
# try_until PATTERN TRIES COMMAND...: runs the command until PATTERN occurs once more. The
# game ignores a pause request while a tutorial hint box holds the pause (the bot closes
# those one frame later), so a single tap can legitimately do nothing.
try_until() {
    local pattern="$1" tries="$2" before
    shift 2
    before="$(count "${pattern}")"
    for _ in $(seq 1 "${tries}"); do
        "$@"
        for _ in $(seq 1 8); do
            check_crash
            if [ "$(count "${pattern}")" -gt "${before}" ]; then return 0; fi
            sleep 1
        done
    done
    fail "no new /${pattern}/ after ${tries} attempts"
}
try_until "AS3D_PAUSED" 3 tap "${PAUSE_X}" "${PAUSE_Y}"
sleep 2
shot 03_paused
BEFORE="$(count "AS3D_RESUMED")"
tap "${FCX}" "${FCY}"
wait_more "AS3D_RESUMED" "${BEFORE}" 20

echo "== back key pauses =="
try_until "AS3D_PAUSED reason=back" 3 adb -s "${SERIAL}" shell input keyevent KEYCODE_BACK
BEFORE="$(count "AS3D_RESUMED")"
tap "${FCX}" "${FCY}"
wait_more "AS3D_RESUMED" "${BEFORE}" 20

echo "== background and back: resumes paused, GL resources rebuilt =="
FRAMES_BEFORE="$(count "AS3D_GAME_FRAME")"
adb -s "${SERIAL}" shell input keyevent KEYCODE_HOME
wait_for "AS3D_BACKGROUND" 30
sleep 4
shot 04_home
adb -s "${SERIAL}" shell am start -n "${ACTIVITY}" >/dev/null || fail "relaunch failed"
wait_for "AS3D_FOREGROUND" 60
wait_for "AS3D_GL_REBUILD" 120
sleep 3
shot 05_resumed_paused
BEFORE="$(count "AS3D_RESUMED")"
tap "${FCX}" "${FCY}"
wait_more "AS3D_RESUMED" "${BEFORE}" 20
wait_more "AS3D_GAME_FRAME" "${FRAMES_BEFORE}" 120
shot 06_playing_after_resume

echo "== playing on to frame 3600 =="
wait_for "AS3D_GAME_FRAME n=3600 " 600
shot 07_frame3600
check_crash

# ---------------------------------------------------------------------------------------
echo "== menus: main menu, Start Game, Start, play, pause, in-game menu, quit =="
adb -s "${SERIAL}" shell am force-stop "${APP_ID}"
sleep 2
BEFORE="$(count "AS3D_SCREEN name=main")"
adb -s "${SERIAL}" shell am start -W -n "${ACTIVITY}" || fail "am start (menus) failed"
# Intro pages (about 16 s), then the attract level loads.
wait_more "AS3D_SCREEN name=main" "${BEFORE}" "${START_TIMEOUT}"
grep -qE "AS3D_ARGS .*menus=1" "${LOG_FILE}" || fail "the app did not start on the front end"
VIEW="$(grep -E "AS3D_VIEW" "${LOG_FILE}" | tail -n1)"
read -r VSCALE VX VY <<<"$(echo "${VIEW}" | sed -nE 's/.*scale=([0-9.]+) x=([0-9.-]+) y=([0-9.-]+).*/\1 \2 \3/p')"
[ -n "${VY:-}" ] || fail "no AS3D_VIEW line"
# vtap X Y: taps the point (X, Y) of the virtual 800x600 screen.
vtap() {
    local p
    p="$(awk -v x="$1" -v y="$2" -v s="${VSCALE}" -v ox="${VX}" -v oy="${VY}" 'BEGIN { printf "%d %d", x * s + ox, y * s + oy }')"
    # shellcheck disable=SC2086
    tap ${p}
}
sleep 3
shot menu_01_main
try_until "AS3D_SCREEN name=start" 3 vtap 400 266       # Start Game
sleep 2
shot menu_02_start
try_until "AS3D_SCREEN name=playing" 3 vtap 680 482     # Start
PLAY_FROM="$(grep -E "AS3D_SCREEN name=playing" "${LOG_FILE}" | tail -n1 | sed -nE 's/.* frame=([0-9]+).*/\1/p')"
PLAY_LINE="$(grep -nE "AS3D_SCREEN name=playing" "${LOG_FILE}" | tail -n1 | cut -d: -f1)"
echo "android_smoke: mission 1 started at frame ${PLAY_FROM}"
# 600 frames of play: a frame marker of mission 1 at least 600 frames later.
DEADLINE=$((SECONDS + 300))
while :; do
    check_crash
    # Only markers logged after the mission started (the first part's run logged its own).
    LAST="$(tail -n +"${PLAY_LINE}" "${LOG_FILE}" | grep -E "AS3D_GAME_FRAME n=[0-9]+ mission=1 " | tail -n1 | sed -nE 's/.*AS3D_GAME_FRAME n=([0-9]+).*/\1/p')"
    if [ -n "${LAST}" ] && [ "${LAST}" -ge "$((PLAY_FROM + 600))" ]; then break; fi
    [ "${SECONDS}" -lt "${DEADLINE}" ] || fail "mission 1 did not run 600 frames"
    sleep 2
done
shot menu_03_playing
LAYOUT="$(grep -E "AS3D_LAYOUT" "${LOG_FILE}" | tail -n1)"
read -r PAUSE_X PAUSE_Y <<<"$(pos pause)"
# The pause button opens the in-game menu (a first tap may close a tutorial hint box).
try_until "AS3D_SCREEN name=ingame" 4 tap "${PAUSE_X}" "${PAUSE_Y}"
sleep 2
shot menu_04_ingame
try_until "AS3D_SCREEN name=playing" 3 vtap 400 287     # Resume
sleep 2
try_until "AS3D_SCREEN name=ingame" 4 tap "${PAUSE_X}" "${PAUSE_Y}"
try_until "AS3D_SCREEN name=main" 3 vtap 400 357        # Quit
sleep 3
shot menu_05_main_after_quit
try_until "AS3D_SCREEN name=exit" 3 adb -s "${SERIAL}" shell input keyevent KEYCODE_BACK
sleep 1
shot menu_06_exit_confirmation
try_until "AS3D_SCREEN name=main" 3 vtap 507 352        # No
check_crash

echo "== summary =="
grep -E "AS3D_(ARGS|GAME_START|LAYOUT|INSETS|LEVEL_LOADED|GL_REBUILD|PAUSED|RESUMED|BACKGROUND|FOREGROUND|HITCH|SCREEN|VIEW)|GL_RENDERER" "${LOG_FILE}" | sed -E 's/^[0-9-]+ [0-9:.]+ //' | head -n 80
echo "-- frame markers --"
grep -E "AS3D_GAME_FRAME" "${LOG_FILE}" | sed -E 's/^[0-9-]+ [0-9:.]+ //'
echo "-- performance (emulator numbers if this ran on the emulator) --"
grep -E "AS3D_PERF" "${LOG_FILE}" | sed -E 's/^[0-9-]+ [0-9:.]+ //'
echo "android_smoke: logcat in ${LOG_FILE}, screenshots in ${OUT_DIR}"
echo "android_smoke: PASSED"
