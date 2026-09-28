# Android (WP-18)

An Android bring-up skeleton for the AirStrike 3D engine: a Gradle project
(`android/`) that builds a native library through CMake, links SDL2, opens an
OpenGL ES 3.0 context, reads the original pak archives from the APK's assets,
and draws a rotating triangle. The same native program
(`apps/android_boot/`) also builds and runs on desktop Linux as a quick
sanity check.

**The debug APK embeds the original, copyrighted AirStrike 3D game data
(the three pak archives). It is for personal use / local testing only — do
not redistribute it.**

## Layout

- `android/` — Gradle project. Package `org.as3dport.game`, one `app` module,
  minSdk 26, target/compileSdk 36, ABIs `arm64-v8a` and `x86_64`.
- `apps/android_boot/` — the native `SDL_main` entry point shared by the
  Android build and a standalone desktop build. Includes a **temporary** pak
  reader (`src/pak_boot.h/.cpp`) — see below.
- `tools/android_env.sh`, `tools/android_build.sh`, `tools/android_smoke.sh`,
  `tools/fetch_third_party.sh`.

## Versions chosen

- **SDL 2.30.12** (`release-2.30.12` tag), the latest 2.30.x point release as
  of 2026-09-28. Fetched by `tools/fetch_third_party.sh` from
  `https://github.com/libsdl-org/SDL/releases/download/release-2.30.12/SDL2-2.30.12.tar.gz`
  and verified against SHA-256
  `ac356ea55e8b9dd0b2d1fa27da40ef7e238267ccf9324704850d5d47375b48ea`
  (computed from the tarball actually downloaded on this machine on
  2026-09-28; re-verify if you re-pin to a different release). SDL's whole
  source tree is **not** committed to this repository; it lives under the
  gitignored `third_party_local/SDL2-2.30.12/`, shared across worktrees via
  `AS3D_DATA_ROOT`. The Java glue in
  `third_party_local/SDL2-2.30.12/android-project/app/src/main/java/org/libsdl/app`
  is referenced in place from `android/app/build.gradle`'s `sourceSets`, not
  copied into this repo.
- **Android Gradle Plugin 9.4.1** with **Gradle 9.7.0** (wrapper committed:
  `android/gradlew`, `android/gradle/wrapper/gradle-wrapper.jar` and
  `.properties`). Gradle 9.7.0 was already cached on this machine
  (`~/.gradle/wrapper/dists`), so the wrapper was generated from that
  distribution rather than downloading a new one; AGP's own versioning has
  moved to track Gradle's major version, so AGP 9.x is the version meant to
  pair with Gradle 9.x here.
- NDK `28.2.13676358`, CMake `3.31.6`, compileSdk/targetSdk `36`, JDK 17
  (`$HOME/Android/jdk`) — all pre-existing in the SDK install this was
  built against (`$HOME/Android/sdk`).

## How the Android build fits into the CMake tree

The root `CMakeLists.txt` is owned by the orchestrator and skips
`add_subdirectory(apps)` entirely when `ANDROID` is set (which the NDK
toolchain file sets automatically), and `apps/CMakeLists.txt` only
`add_subdirectory()`s `game`, `viewer` and `rcsl_tool` — not
`android_boot`. So:

- **Android**: `android/app/src/main/cpp/CMakeLists.txt` is a small,
  Android-specific CMake file (referenced by `externalNativeBuild` in
  `android/app/build.gradle`). It `add_subdirectory()`s SDL2's own CMake
  project (built from source — there is no prebuilt SDL2 for Android here)
  and the repository root (`AS3D_REPO_ROOT`, which resolves to just
  `engine/` on Android since `apps/` is skipped), then compiles
  `apps/android_boot/src/{main,pak_boot}.cpp` directly into a `main` SHARED
  library — the name `SDLActivity.getLibraries()` expects
  (`android/app/src/main/java/org/as3dport/game/GameActivity.java` overrides
  it explicitly as `{"SDL2", "main"}` for clarity, matching the default).
- **Desktop**: `apps/android_boot/CMakeLists.txt` builds the same two source
  files into a normal executable, either standalone
  (`cmake -S apps/android_boot -B build-boot`, which pulls in `engine/`
  itself since it isn't nested under the main tree) or, if the orchestrator
  later wires it in, as part of the full tree.
  **One-line change needed in `apps/CMakeLists.txt`** (which this workstream
  was not allowed to edit) to build it automatically alongside `game`,
  `viewer` and `rcsl_tool`:
  ```cmake
  foreach(app game viewer rcsl_tool android_boot)
  ```

## Temporary pak reader (to be replaced)

`apps/android_boot/src/pak_boot.h` / `.cpp` implement just enough of
`docs/spec/pak.md` (header, XOR-decrypted file table, per-file XOR
decryption) to open the three paks and read `maps\levels.txt`. This is
**temporary, deliberately minimal, and clearly marked** in both files: the
real `as3d_vfs` module (`engine/src/vfs`, `as3d::IFileSource` /
`as3d::makePakSource()`) is being implemented in parallel in another
worktree and was not present in this one. Once it lands, `apps/android_boot`
should mount the paks through `as3d::Vfs` with an SDL_RWops-backed
`as3d::IStream`, and `pak_boot.h/.cpp` should be deleted.

## Asset handling: why the paks are stored uncompressed

The three original pak archives (despite the `.apk` extension — that's the
original game's own naming, unrelated to Android) are copied by
`tools/android_build.sh` into `android/app/src/main/assets/pak{0,1,2}.apk`
at build time, from `$AS3D_DATA_ROOT/third_party_local/original/data/`. They
are **not renamed**: `pak0.apk` etc. already match the existing `.gitignore`
pattern `android/app/src/main/assets/*.apk` verbatim, so no changes to
`.gitignore` (which this workstream was not allowed to edit) were needed.

By default AGP's asset packager may compress files it doesn't recognize as
already-compressed media. `android/app/build.gradle` sets:
```groovy
androidResources {
    noCompress += ["apk"]
}
```
so the three pak files are stored (`STORED`, 0% "compression") in the APK.
`unzip -lv app-debug.apk` confirms this (see below). This matters because
our (and eventually `as3d_vfs`'s) pak reader seeks to byte offsets recorded
in the pak's file table; `SDL_RWFromFile` on Android resolves a relative path
through the asset manager either way, but uncompressed storage keeps that
access O(1)-ish rather than requiring the asset manager to decompress and
buffer from the start of the stream on every seek.

## Building

```bash
source tools/android_env.sh   # or just run the scripts below directly
AS3D_DATA_ROOT=/path/to/checkout/with/third_party_local tools/android_build.sh
```

This fetches SDL2 (if not already present), copies the three paks into
`android/app/src/main/assets/`, writes `android/local.properties`
(gitignored), and runs `./gradlew assembleDebug`. From a worktree, set
`AS3D_DATA_ROOT` to the main checkout (as the top-level `README.md`
describes for tests) so both the shared SDL2 download and the game data are
found; `tools/fetch_third_party.sh` puts SDL2 under
`$AS3D_DATA_ROOT/third_party_local/SDL2-2.30.12/` for exactly this reason —
so it survives worktree removal and isn't re-downloaded by every worktree.

Output: `android/app/build/outputs/apk/debug/app-debug.apk`.

## Running the smoke test

```bash
tools/android_smoke.sh
```

Uses an already-online device/emulator if there is one; otherwise boots the
pre-existing `atticpad-test` AVD headless (`-no-window -no-audio
-no-boot-anim -no-snapshot-save -gpu swiftshader_indirect`), waits for boot,
installs the APK, clears logcat, launches `GameActivity`, and waits up to
120 s for both `AS3D_BOOT_OK` and `AS3D_FRAME 300` to appear in logcat
(failing fast on `FATAL EXCEPTION` / `SIGSEGV` / `Fatal signal`). Saves a
screenshot to `out/android_smoke.png` and the full logcat capture to
`out/android_smoke_logcat.txt`. Only shuts the emulator down at the end if
the script itself started it — it never wipes, reconfigures, or deletes the
AVD (which belongs to another project on this machine).

## Installing on a physical phone

```bash
source tools/android_env.sh
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n org.as3dport.game/org.as3dport.game.GameActivity
adb logcat -s AS3D:* SDL:*
```
minSdk is 26 (Android 8.0); any arm64-v8a phone from roughly 2018 onward
should run it (there's no armeabi-v7a build in this skeleton — WP-18 only
asked for arm64-v8a and x86_64).

## Known issues / follow-ups

- `pak_boot.h/.cpp` is temporary scaffolding (see above) and duplicates logic
  that belongs in `as3d_vfs`; it should be deleted once that module lands.
- No app icon is set (`android:icon` is omitted from the manifest); the
  system falls back to a generic default. Fine for a bring-up skeleton, but
  worth adding real mipmap resources before this becomes a real "game" APK.
- The build emits a harmless CMake deprecation warning from SDL2's own
  `CMakeLists.txt` (`cmake_minimum_required` version floor) and a handful of
  NDK `-Wdeprecated-declarations` warnings from SDL's Android sensor backend
  (`ASensorManager_getInstance`); both are inside SDL2's own sources, not
  ours, and not actionable here.
- Only `arm64-v8a` and `x86_64` are built, per the work package. Real
  physical-device coverage beyond arm64-v8a (e.g. armeabi-v7a for very old
  devices) was out of scope.
- The desktop build of `apps/android_boot` requests a GLES 3.0 context via
  SDL on X11/EGL/Mesa; on a host without a GLES-capable EGL backend (e.g. no
  GPU driver, no Mesa llvmpipe) it may fail to create a context. It was
  verified working on this machine (Mesa/Intel).
- On a genuinely cold `atticpad-test` AVD boot, the very first launch of
  `GameActivity` was observed once to recreate itself a few seconds in
  (visible as a second `GL_VENDOR/GL_RENDERER/GL_VERSION` + `AS3D_BOOT_OK`
  sequence in logcat, with no crash), and the resulting run then stalled
  without reaching `AS3D_FRAME 300` inside the 120 s window — most likely two
  overlapping SDL/EGL contexts in the same process racing on SwiftShader
  while the emulator's display metrics were still settling right after
  `sys.boot_completed=1`. It did not reproduce on an already-booted/warm
  emulator. `tools/android_smoke.sh` mitigates this with an 8 s settle delay
  after boot and a single force-stop-and-relaunch retry if the first attempt
  times out without a crash signature; this was sufficient in testing (the
  retry was not even needed on the run this was verified with). If it
  reproduces on a slower machine, the same technique should still recover
  the app since it comes up cleanly on relaunch.
- The `out/android_smoke.png` screenshot from the emulator run looks
  portrait-shaped despite the manifest's `android:screenOrientation="landscape"`;
  the triangle renders correctly but is stretched by the window's aspect
  ratio (the shader does no aspect correction, out of scope for this
  bring-up milestone). Whether this is the AVD's headless
  (`-no-window`) mode not rotating its virtual display, or something else,
  was not root-caused; worth checking on a physical device or a windowed
  emulator run.
