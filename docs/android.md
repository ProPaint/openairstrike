# Android

The game (`apps/game`) runs on Android as a native SDL2 + OpenGL ES 3.0 app with touch
controls. It starts straight into mission 1 (the menus are not written yet) and moves on to
the next mission when one is complete, like `as3d_game --level N` on desktop.

**Copyright.** The APK contains the original AirStrike 3D game data (the three pak archives,
copied from your own copy of the game). It is for the owner's personal use only: do not
share it or upload it anywhere. No APK, pak, keystore or screenshot is ever committed: the
paks copied into `android/app/src/main/assets/`, `android/app/build/` and `out/` are
gitignored, and `tools/android_build.sh` refuses to build if a pak is tracked by git. The
debug keystore is Android's default one in `~/.android/`, outside the repository.

## Layout

- `android/`: Gradle project (wrapper committed). Package `org.as3dport.game`, one `app`
  module, minSdk 26, target/compileSdk 36, ABIs `arm64-v8a` (phones) and `x86_64` (emulator).
  `GameActivity.java` extends SDL's `SDLActivity`: it names the native libraries, turns intent
  extras into program arguments, lays the surface out under display cutouts and reports the
  cutout insets to native code.
- `android/app/src/main/cpp/CMakeLists.txt`: builds SDL2 from source, the engine, and
  `apps/game` (whose `CMakeLists.txt` makes the `main` shared library on Android).
- `apps/game/`: the game. `game_loop.cpp` is the windowed main loop shared by the desktop
  executable and the Android app (session, renderer, audio, keyboard and touch input,
  lifecycle, loading screen, frame statistics); `android_main.cpp` is the Android entry point;
  `touch_overlay.cpp` draws the buttons with the 2D layer.
- `engine/src/input/touch_mapper.cpp`: the platform-independent `TouchMapper`
  (`as3d/input.h`), unit-tested on desktop (`apps/tests/touch_test.cpp`).
- `engine/src/platform/rw_stream.cpp`: `as3d::openPlatformStream`, an SDL_RWops byte stream;
  on Android it reads APK assets in place.
- Scripts: `tools/android_env.sh`, `tools/android_build.sh`, `tools/android_smoke.sh`,
  `tools/fetch_third_party.sh`.

The WP-18 bring-up program `apps/android_boot` has been retired; its pak stream moved into
the platform module.

## What is bundled

`tools/android_build.sh` copies `pak0.apk`, `pak1.apk`, `pak2.apk` (the original archives;
the `.apk` extension is the original game's naming) from
`$AS3D_DATA_ROOT/third_party_local/original/data/` into the APK's assets. `build.gradle`
keeps them stored uncompressed (`noCompress "apk"`), so the engine's pak reader
(`docs/spec/pak.md`) mounts them straight from the APK through the asset manager, seeking to
the offsets of the pak's file table: nothing is extracted to storage. They are mounted in
name order (later paks override earlier ones). About 25 MB of data; the APK with both ABIs
is about 45 MB.

Not bundled: `texts_v170.txt` (texts from the original executable, issue 080; nothing reads
it yet) and the loose `gfx/logo2s.tga`.

## Building

```bash
AS3D_DATA_ROOT=/path/to/main/checkout tools/android_build.sh
```

This fetches SDL2 and libopenmpt sources into `$AS3D_DATA_ROOT/third_party_local/` if
needed, copies the paks, writes `android/local.properties` (gitignored) and runs
`./gradlew assembleDebug` with the Gradle JVM capped at 1.5 GB and 2 workers. Native code is
built `RelWithDebInfo` even in the debug APK. `AS3D_ANDROID_ABIS=x86_64` builds only the
emulator ABI (faster); `AS3D_NATIVE_JOBS` sets the parallel compile jobs (default 4).
Output: `android/app/build/outputs/apk/debug/app-debug.apk`. Stop the Gradle daemon
afterwards on a shared machine: `(cd android && ./gradlew --stop)`.

Toolchain: Android SDK 36, NDK 28.2.13676358, CMake 3.31.6, JDK 17, Android Gradle Plugin
9.4.1 with Gradle 9.7.0, SDL 2.30.12 (SHA-256 pinned in `fetch_third_party.sh`).

## Installing on a phone over USB

1. On the phone: Settings, About phone, tap "Build number" seven times; then in Developer
   options enable "USB debugging".
2. Connect the phone, accept the computer's key on the phone, then:

```bash
source tools/android_env.sh
adb devices                       # the phone must show as "device"
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n org.as3dport.game/.GameActivity
adb logcat -s AS3D:* SDL:*        # optional: the game's log
```

Any arm64 phone with Android 8.0 or later and OpenGL ES 3.0 should run it. To remove it:
`adb uninstall org.as3dport.game`.

### Intent extras

`adb shell am start -n org.as3dport.game/.GameActivity` accepts:

| Extra | Meaning |
|---|---|
| `--ez bot true` | the scripted test pilot plays (same as `as3d_game --bot`); touch still works |
| `--ei level N` | first mission, 1..20 |
| `--ei frames N` | quit after N simulation frames |
| `--ei difficulty D` | 0..4 |
| `--ez no_audio true` | no sound |
| `--ez rebuild_on_resume true` | test hook: rebuild every GL resource after each resume |

The activity is `singleInstance`: extras only apply when the app is not already running
(`adb shell am force-stop org.as3dport.game` first).

## Controls

Landscape only (either way round), full screen, the screen stays on.

- **Move: drag anywhere** outside the buttons. The movement is relative: the helicopter moves
  by the finger's displacement (times 1.5), it does not jump to the finger, so your finger
  never has to cover it. Lift and put the finger down again to continue from wherever the
  helicopter is.
- **Fire: automatic** while any finger is on the screen (except on the pause button).
- **Buttons** (right-hand column, bottom up): missile (red rocket), power-up (yellow
  diamond), next missile type, next weapon, next power-up. They can be pressed while another
  finger drags.
- **Pause**: the pause button (top left on wide screens, top centre on 4:3), or the Back
  key. A tap anywhere continues.
- A tap also closes a tutorial hint box.

On screens wider than 4:3 (most phones) the buttons sit outside the 4:3 play-field, at the
sides; on 4:3 tablets they are drawn translucent inside it. They stay clear of display
cutouts. The design and the reasons are in `docs/spec/issues/100-touch-controls.md`.

On desktop, `as3d_game --touch` draws the same controls and makes the left mouse button one
finger (drag with the mouse; the window is resizable to try other aspect ratios). The
keyboard keeps working.

## App lifecycle

- Home, the app switcher or a call: the game pauses, fingers are released, sound stops.
  Coming back shows the paused game; tap to continue.
- If the GL context is lost while in the background, every GL resource (renderer, HUD
  textures, shadow silhouettes) is rebuilt from the paks before the first frame.
- The simulation runs at a fixed 60 Hz, independent of the display rate; after a stall at
  most 5 steps are caught up, the rest is dropped.
- Level loads show a loading bar.

## Log markers and the smoke test

The app logs under the tag `AS3D` (`adb logcat -s AS3D:*`):

| Marker | When |
|---|---|
| `AS3D_ARGS` | at start: the arguments from the intent |
| `AS3D_GAME_START size=WxH load_ms=...` | the first level is loaded and playing |
| `AS3D_LAYOUT size=... insets=... buttons=outside|inside missile=x,y ...` | button centres in pixels, when the screen or the insets change |
| `AS3D_GAME_FRAME n=600 mission=1 score=...` | every 600 simulation frames (10 s of game time) |
| `AS3D_PERF avg_ms max_ms fps work_ms sim_steps dropped_steps` | every 5 s: frame interval average and maximum, CPU time per frame, steps run and dropped |
| `AS3D_LEVEL_LOADED mission=N ms=...` | after a later level load |
| `AS3D_PAUSED reason=input|back|background`, `AS3D_RESUMED` | pause changes |
| `AS3D_BACKGROUND`, `AS3D_FOREGROUND`, `AS3D_GL_REBUILD reason=... ms=...` | lifecycle |
| `AS3D_TOUCH down/up ... on=field|missile|...` | every finger down and up |
| `AS3D_HITCH part=step|draw ms=...` | a single step or draw took longer than 50 ms |
| `FATAL: ...` (error level) | the game cannot continue |

`tools/android_smoke.sh` uses an online device, or boots the `atticpad-test` AVD headless
(`-no-window -no-audio -no-boot-anim -no-snapshot-save -memory 2048 -gpu
swiftshader_indirect`, only with at least 5 GB of host memory available; the AVD belongs to
another project and is never wiped or reconfigured). It installs the APK, launches it with
the bot pilot and `rebuild_on_resume`, waits for `AS3D_GAME_FRAME n=600`, injects a drag, a
missile and a next-weapon tap, the pause button and a tap, the Back key and a tap, sends the
app to the background and brings it back (checking `AS3D_GL_REBUILD`), taps to continue,
and plays on to frame 3600. It fails on a `FATAL` marker, a Java exception or a native
crash, and on timeouts. Screenshots and the logcat capture go to
`$AS3D_DATA_ROOT/out/m8/` (gitignored). `adb shell input` has no multi-touch, so
multi-touch is covered by the unit tests only.

## Known limits

- No menus: the app starts in mission 1; after mission 20 the campaign restarts. No save.
- Model textures load on first draw; a new enemy type can cause a short hitch on a slow
  device (`AS3D_HITCH`). Terrain, water and the map objects' shadow silhouettes are built at
  level load.
- Two players, gamepads and the hardware keyboard's rebinding are not supported on Android
  (a Bluetooth keyboard works with the desktop keys).
- The emulator renders with SwiftShader (a CPU GLES implementation): its frame times say
  nothing about a phone's. Measured on the `atticpad-test` AVD (Pixel 6 profile, 2400x1080,
  x86_64, KVM, SwiftShader), emulator numbers only: 6 to 9 displayed frames per second
  (`avg_ms` 105 to 170, work 100 to 160 ms per frame, almost all of it drawing), about 40 of
  the 60 simulation steps per second run and the rest dropped (the game runs at about 2/3
  speed there), first level load 9 to 12 s from the APK, a GL rebuild after resume 2.5 to
  6.5 s. `-gpu host` does not start headless on the development machine (no X display), so
  `AS3D_EMU_GPU=host` only helps where a display is available. On desktop (GTX 1060) the
  same loop holds 60 fps with 1.3 to 2.9 ms of work per frame.
- A pause request (button, P, Back) is ignored while a tutorial hint box holds the pause,
  as in the original; the smoke test retries it.
- Under heavy load the emulator's System UI may show "isn't responding"; the smoke test
  closes system dialogs before tapping.
- No app icon yet.
