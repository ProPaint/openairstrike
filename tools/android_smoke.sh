#!/usr/bin/env bash
# Installs and runs the WP-18 APK on a device/emulator and checks the boot
# sequence in logcat. Boots the `atticpad-test` AVD headless if no
# device/emulator is already online (and shuts it down again at the end, but
# only if this script started it -- it belongs to another project and must
# not be wiped or reconfigured).
#
#   tools/android_smoke.sh
#
# Exit non-zero on any failure (build missing, emulator fails to boot, app
# crashes, timeout waiting for the expected log lines).
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
# shellcheck source=./android_env.sh
source "${SCRIPT_DIR}/android_env.sh"

APP_ID="org.as3dport.game"
ACTIVITY="${APP_ID}/org.as3dport.game.GameActivity"
APK="${REPO_ROOT}/android/app/build/outputs/apk/debug/app-debug.apk"
AVD_NAME="atticpad-test"
OUT_DIR="${REPO_ROOT}/out"
SCREENSHOT="${OUT_DIR}/android_smoke.png"
BOOT_TIMEOUT=180
LOGCAT_TIMEOUT=120

mkdir -p "${OUT_DIR}"

if [ ! -f "${APK}" ]; then
    echo "android_smoke: APK not found at ${APK}; run tools/android_build.sh first" >&2
    exit 1
fi

STARTED_EMULATOR=0
EMULATOR_PID=""
SERIAL=""

cleanup() {
    if [ "${STARTED_EMULATOR}" = "1" ] && [ -n "${SERIAL}" ]; then
        echo "android_smoke: shutting down emulator ${SERIAL} (we started it)"
        adb -s "${SERIAL}" emu kill >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

echo "== checking for an online device/emulator =="
adb start-server >/dev/null 2>&1
ONLINE="$(adb devices | awk 'NR>1 && $2=="device" {print $1}' | head -n1)"

if [ -n "${ONLINE}" ]; then
    SERIAL="${ONLINE}"
    echo "android_smoke: using already-online device ${SERIAL}"
else
    echo "android_smoke: no device online, booting AVD '${AVD_NAME}' headless"
    if ! "${ANDROID_HOME}/emulator/emulator" -list-avds | grep -qx "${AVD_NAME}"; then
        echo "android_smoke: AVD '${AVD_NAME}' not found" >&2
        exit 1
    fi

    GPU_MODE="swiftshader_indirect"
    if [ -r /dev/kvm ] && [ -w /dev/kvm ]; then
        echo "android_smoke: /dev/kvm accessible, hardware acceleration available"
    else
        echo "android_smoke: WARNING /dev/kvm not accessible, emulator will be slow"
    fi

    "${ANDROID_HOME}/emulator/emulator" -avd "${AVD_NAME}" \
        -no-window -no-audio -no-boot-anim -no-snapshot-save \
        -gpu "${GPU_MODE}" \
        >"${OUT_DIR}/emulator.log" 2>&1 &
    EMULATOR_PID=$!
    STARTED_EMULATOR=1

    echo "android_smoke: waiting for emulator to appear (pid ${EMULATOR_PID})"
    SERIAL=""
    for _ in $(seq 1 60); do
        if ! kill -0 "${EMULATOR_PID}" 2>/dev/null; then
            echo "android_smoke: emulator process exited early; see ${OUT_DIR}/emulator.log" >&2
            cat "${OUT_DIR}/emulator.log" >&2
            exit 1
        fi
        CAND="$(adb devices | awk '$2=="offline" || $2=="device" {print $1}' | grep '^emulator-' | head -n1)"
        if [ -n "${CAND}" ]; then
            SERIAL="${CAND}"
            break
        fi
        sleep 2
    done
    if [ -z "${SERIAL}" ]; then
        echo "android_smoke: emulator never registered with adb" >&2
        exit 1
    fi

    echo "android_smoke: waiting for device ${SERIAL} (adb wait-for-device)"
    adb -s "${SERIAL}" wait-for-device

    echo "android_smoke: waiting up to ${BOOT_TIMEOUT}s for sys.boot_completed"
    BOOTED=0
    for i in $(seq 1 "${BOOT_TIMEOUT}"); do
        BOOT_PROP="$(adb -s "${SERIAL}" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r\n')"
        if [ "${BOOT_PROP}" = "1" ]; then
            BOOTED=1
            break
        fi
        if ! kill -0 "${EMULATOR_PID}" 2>/dev/null; then
            echo "android_smoke: emulator process died while waiting for boot" >&2
            cat "${OUT_DIR}/emulator.log" >&2
            exit 1
        fi
        sleep 1
    done
    if [ "${BOOTED}" != "1" ]; then
        echo "android_smoke: timed out after ${BOOT_TIMEOUT}s waiting for boot" >&2
        exit 1
    fi
    echo "android_smoke: emulator booted (${i}s)"
    # On a freshly-booted (cold) AVD, display metrics/orientation can still be
    # settling for a few seconds after sys.boot_completed=1, which has been
    # observed to make our landscape-locked activity recreate itself once
    # right after launch (a second, otherwise-harmless run of SDL_main in the
    # same process). Give it a moment to settle to make that less likely; the
    # retry loop below also tolerates it if it still happens.
    sleep 8
fi

echo "== installing APK on ${SERIAL} =="
adb -s "${SERIAL}" uninstall "${APP_ID}" >/dev/null 2>&1 || true
if ! adb -s "${SERIAL}" install -r "${APK}"; then
    echo "android_smoke: adb install failed" >&2
    exit 1
fi

LOG_FILE="${OUT_DIR}/android_smoke_logcat.txt"
SAW_BOOT_OK=0
SAW_FRAME_300=0
SAW_AUDIO_OK=0
FAILED=0

run_and_wait() {
    echo "== clearing logcat =="
    adb -s "${SERIAL}" logcat -c

    echo "== launching ${ACTIVITY} =="
    adb -s "${SERIAL}" shell am start -W -n "${ACTIVITY}" || {
        echo "android_smoke: am start failed" >&2
        return 1
    }

    echo "== waiting up to ${LOGCAT_TIMEOUT}s for AS3D_BOOT_OK, AS3D_FRAME 300 and AS3D_AUDIO_OK =="
    : > "${LOG_FILE}"
    DEADLINE=$((SECONDS + LOGCAT_TIMEOUT))

    adb -s "${SERIAL}" logcat -v time >"${LOG_FILE}" &
    LOGCAT_PID=$!

    SAW_BOOT_OK=0
    SAW_FRAME_300=0
    SAW_AUDIO_OK=0
    FAILED=0
    while [ "${SECONDS}" -lt "${DEADLINE}" ]; do
        if grep -q "AS3D_BOOT_OK" "${LOG_FILE}"; then SAW_BOOT_OK=1; fi
        if grep -q "AS3D_FRAME 300" "${LOG_FILE}"; then SAW_FRAME_300=1; fi
        if grep -q "AS3D_AUDIO_OK" "${LOG_FILE}"; then SAW_AUDIO_OK=1; fi
        if grep -qE "FATAL EXCEPTION|SIGSEGV|Fatal signal|AS3D_AUDIO_FAIL" "${LOG_FILE}"; then
            FAILED=1
            break
        fi
        if [ "${SAW_BOOT_OK}" = "1" ] && [ "${SAW_FRAME_300}" = "1" ] && [ "${SAW_AUDIO_OK}" = "1" ]; then
            break
        fi
        sleep 1
    done

    kill "${LOGCAT_PID}" >/dev/null 2>&1 || true
    wait "${LOGCAT_PID}" 2>/dev/null || true

    if [ "${FAILED}" = "1" ]; then
        return 2
    fi
    if [ "${SAW_BOOT_OK}" = "1" ] && [ "${SAW_FRAME_300}" = "1" ] && [ "${SAW_AUDIO_OK}" = "1" ]; then
        return 0
    fi
    return 1
}

# A cold AVD's display metrics can still be settling for a few seconds after
# boot, which has been observed to make our landscape-locked activity
# recreate itself once right after launch; that eats into the wait budget
# without producing AS3D_FRAME 300 in time. Retry once (force-stopping and
# relaunching) before declaring failure -- it never happened on a
# warm/already-settled emulator in testing.
RESULT=1
for attempt in 1 2; do
    echo "== attempt ${attempt}/2 =="
    adb -s "${SERIAL}" shell am force-stop "${APP_ID}" >/dev/null 2>&1 || true
    if run_and_wait; then
        RESULT=0
        break
    else
        RESULT=$?
        if [ "${RESULT}" = "2" ]; then
            break # crash signature: no point retrying
        fi
        echo "android_smoke: attempt ${attempt} timed out without a crash; retrying" >&2
    fi
done

echo "== screenshot =="
adb -s "${SERIAL}" exec-out screencap -p > "${SCREENSHOT}" 2>/dev/null
if [ -s "${SCREENSHOT}" ]; then
    echo "android_smoke: screenshot saved to ${SCREENSHOT}"
else
    echo "android_smoke: WARNING failed to capture screenshot"
fi

echo "== relevant logcat lines =="
grep -E "AS3D_|GL_VENDOR|GL_RENDERER|GL_VERSION|FATAL EXCEPTION|SIGSEGV|Fatal signal" "${LOG_FILE}" || true

if [ "${RESULT}" = "2" ]; then
    echo "android_smoke: FAILED - crash signature or AS3D_AUDIO_FAIL seen in logcat" >&2
    exit 1
fi
if [ "${RESULT}" != "0" ]; then
    echo "android_smoke: FAILED - timed out waiting for AS3D_BOOT_OK / AS3D_FRAME 300 / AS3D_AUDIO_OK" >&2
    exit 1
fi

echo "android_smoke: PASSED"
