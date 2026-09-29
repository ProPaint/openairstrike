#!/usr/bin/env bash
# Plays mission 1 of the game APK on a device or emulator, unattended (the bot pilot,
# through the `bot` intent extra), and checks it in logcat:
#
#   1. launches with --ez bot true --ez rebuild_on_resume true and waits for
#      AS3D_GAME_START and AS3D_GAME_FRAME n=600;
#   2. injects touch gestures with `adb shell input`: a drag in the play-field, taps on the
#      missile and next-weapon buttons, the pause button then a tap to continue, the back key
#      then a tap;
#   3. sends the app to the background (HOME), brings it back, checks it resumes paused with
#      its GL resources rebuilt, taps to continue and waits for more frames;
#   4. waits for AS3D_GAME_FRAME n=3600 and prints the AS3D_PERF lines.
#
# Fails on a FATAL marker, a Java exception or a native crash, or a timeout. Screenshots and
# the logcat capture go to $AS3D_SMOKE_OUT (default: <AS3D_DATA_ROOT or repo>/out/m8).
# Screenshots show copyrighted game art: they stay in the gitignored out/ directory.
#
# Boots the `atticpad-test` AVD headless if no device is online (it belongs to another
# project: never wiped or reconfigured; only shut down again if this script started it),
# with at most 2048 MB guest memory and only when at least 5 GB of host memory is available.
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
    if [ "${STARTED_EMULATOR}" = "1" ] && [ -n "${SERIAL}" ]; then
        echo "android_smoke: shutting down emulator ${SERIAL} (we started it)"
        adb -s "${SERIAL}" emu kill >/dev/null 2>&1 || true
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
    echo "android_smoke: booting '${AVD_NAME}' headless (2048 MB, SwiftShader GLES; ${AVAIL_MB} MB host memory available)"
    "${ANDROID_HOME}/emulator/emulator" -avd "${AVD_NAME}" \
        -no-window -no-audio -no-boot-anim -no-snapshot-save -memory 2048 \
        -gpu swiftshader_indirect \
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
    sleep 8 # display metrics settle after boot_completed
fi
adb -s "${SERIAL}" shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1 || true
adb -s "${SERIAL}" shell wm dismiss-keyguard >/dev/null 2>&1 || true

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

tap() { adb -s "${SERIAL}" shell input tap "$1" "$2"; }

echo "== launch (bot pilot) =="
adb -s "${SERIAL}" shell am start -W -n "${ACTIVITY}" --ez bot true --ez rebuild_on_resume true || fail "am start failed"
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
shot 01_playing

echo "== touch: drag in the play-field, missile and next-weapon buttons =="
BEFORE="$(count "AS3D_TOUCH down")"
adb -s "${SERIAL}" shell input swipe "${FCX}" "${FCY}" "$((FCX + (FX1 - FX0) / 6))" "${FCY}" 1500
tap "${MISSILE_X}" "${MISSILE_Y}"
tap "${NEXTW_X}" "${NEXTW_Y}"
wait_more "AS3D_TOUCH down" "$((BEFORE + 2))" 20
grep -qE "AS3D_TOUCH down .* on=field" "${LOG_FILE}" || fail "the drag did not land in the play-field"
grep -qE "AS3D_TOUCH down .* on=missile" "${LOG_FILE}" || fail "the missile button was not hit"
grep -qE "AS3D_TOUCH down .* on=next_weapon" "${LOG_FILE}" || fail "the next-weapon button was not hit"
shot 02_after_touch

echo "== pause button, then a tap to continue =="
BEFORE="$(count "AS3D_PAUSED")"
tap "${PAUSE_X}" "${PAUSE_Y}"
wait_more "AS3D_PAUSED" "${BEFORE}" 20
sleep 2
shot 03_paused
BEFORE="$(count "AS3D_RESUMED")"
tap "${FCX}" "${FCY}"
wait_more "AS3D_RESUMED" "${BEFORE}" 20

echo "== back key pauses =="
BEFORE="$(count "AS3D_PAUSED reason=back")"
adb -s "${SERIAL}" shell input keyevent KEYCODE_BACK
wait_more "AS3D_PAUSED reason=back" "${BEFORE}" 20
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

echo "== summary =="
grep -E "AS3D_(ARGS|GAME_START|LAYOUT|INSETS|LEVEL_LOADED|GL_REBUILD|PAUSED|RESUMED|BACKGROUND|FOREGROUND|HITCH)|GL_RENDERER" "${LOG_FILE}" | sed -E 's/^[0-9-]+ [0-9:.]+ //' | head -n 40
echo "-- frame markers --"
grep -E "AS3D_GAME_FRAME" "${LOG_FILE}" | sed -E 's/^[0-9-]+ [0-9:.]+ //'
echo "-- performance (emulator numbers if this ran on the emulator) --"
grep -E "AS3D_PERF" "${LOG_FILE}" | sed -E 's/^[0-9-]+ [0-9:.]+ //'
echo "android_smoke: logcat in ${LOG_FILE}, screenshots in ${OUT_DIR}"
echo "android_smoke: PASSED"
