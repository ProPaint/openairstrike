#!/usr/bin/env bash
# Environment for the Android build/smoke scripts. Source this, don't run it:
#   source tools/android_env.sh
#
# There is no system-wide java/gradle or ANDROID_HOME on this machine, so
# every script that touches the Android SDK/NDK/JDK sources this file first.
# Override any of these by exporting them before sourcing, e.g. a different
# ANDROID_HOME for another machine.

export ANDROID_HOME="${ANDROID_HOME:-$HOME/Android/sdk}"
export ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-${ANDROID_HOME}}"
export JAVA_HOME="${JAVA_HOME:-$HOME/Android/jdk}"

if [ -d "${ANDROID_HOME}/ndk" ]; then
    # Pick the highest installed NDK version (there is normally exactly one).
    NDK_VERSION="$(ls -1 "${ANDROID_HOME}/ndk" | sort -V | tail -n1)"
    export ANDROID_NDK_HOME="${ANDROID_NDK_HOME:-${ANDROID_HOME}/ndk/${NDK_VERSION}}"
fi

export PATH="${JAVA_HOME}/bin:${ANDROID_HOME}/platform-tools:${ANDROID_HOME}/cmdline-tools/latest/bin:${ANDROID_HOME}/emulator:${PATH}"
