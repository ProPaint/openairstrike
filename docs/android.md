# Android

The games (`apps/game`) run on Android as one native SDL2 + OpenGL ES 3.0 app, "AirStrike",
with touch controls. It holds AirStrike 3D and AirStrike 2 and opens on the game selector
(`docs/spec/issues/163`): one card per game with its save (missions open, best score); a tap
plays it, and each game's main menu has "Change game" to come back without restarting the
app. AirStrike 3D runs its original front end in touch mode (intro pages, main menu over the
attract level, Start Game, the in-game menu, Mission Complete, Game Over, Top Scores, Options,
Information; `docs/spec/frontend.md`, touch additions in issue 090, integration choices in
issue 130), AirStrike 2 its own menus (intro comic, text-button menus, helicopter selection,
portrait dialogues, the campaign's "Continue"; `docs/spec/as2/frontend.md`, issue as2/300: in
touch mode a small Skip button on the comic, taps turn the dialogues' pages, the pause control
opens its in-game menu). With the `level` or `bot` extra
it starts straight into a mission and moves on to the next one by itself, like
`as3d_game --level N` on desktop.

**Copyright.** The APK contains the original game data (the pak archives, copied from your own
copies of the games). It is for the owner's personal use only: do not
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
  lifecycle, loading screen, frame statistics); `game_flow.cpp` puts the game behind the front
  end (`engine/src/ui`); `android_main.cpp` is the Android entry point; `touch_overlay.cpp`
  draws the buttons with the 2D layer.
- `engine/src/input/touch_mapper.cpp`: the platform-independent `TouchMapper`
  (`as3d/input.h`), unit-tested on desktop (`apps/tests/touch_test.cpp`).
- `engine/src/platform/rw_stream.cpp`: `as3d::openPlatformStream`, an SDL_RWops byte stream;
  on Android it reads APK assets in place.
- Scripts: `tools/android_env.sh`, `tools/android_build.sh`, `tools/android_smoke.sh`,
  `tools/fetch_third_party.sh`.

The WP-18 bring-up program `apps/android_boot` has been retired; its pak stream moved into
the platform module.

## What is bundled

`tools/android_build.sh` bundles the games named in `AS3D_ANDROID_GAMES` (comma-separated keys
of `tools/games.json`: `as3d`, `as2`, `gulf`; default all three). Each
game's files go under `assets/<key>/` of the APK, and the first game is
under `as3d/` too (one layout; the flat layout of older builds is gone, the build script
removes it from the assets directory): the game's paks (`pak0.apk`, `pak1.apk`, `pak2.apk`,
plus `pak4.apk` for `gulf`; the `.apk` extension is the original game's naming) from
`$AS3D_DATA_ROOT/third_party_local/original/data/` (`as3d`) or
`third_party_local/games/<key>/data/`. `build.gradle` keeps them stored uncompressed
(`noCompress "apk"`), so the engine's pak reader (`docs/spec/pak.md`) mounts them straight
from the APK through the asset manager (`SDL_RWFromFile("as3d/pak0.apk")`,
`engine/src/platform/rw_stream.cpp`), seeking to the offsets of the pak's file table: nothing
is extracted to storage. They are mounted in the profile's order (later paks override
earlier ones). About 25 MB of data for `as3d` and 48 MB for `as2`; the APK with both ABIs is
37.9 MB for `as3d` alone, 86.1 MB (86 123 550 bytes) with the default `as3d,as2`, and 129 MB with
`as3d,as2,gulf`.

It also copies, when the data has them, the front end's loose files of each game:
`Settings.xml` and `gfx/logo2s.tga` from the same directory (intro pages, version line, the
main menu's logo) and the game's texts file (`texts` in `tools/games.json`: `texts_v170.txt`
from `$AS3D_DATA_ROOT/assets_extracted/`, `texts_as2.txt` / `texts_gulf.txt` from
`assets_extracted_games/<key>/`; the texts imported from the original executable by
`tools/extract_exe_texts.py`, issue 080: Information pages, rank names). Without them the
menus still work. Everything in `android/app/src/main/assets/` is gitignored there, and the
build script refuses to build if any file of any game (or anything but the `.gitignore`)
under it is tracked by git. The native side (`apps/game/android_main.cpp`) finds the games
the APK holds (`<key>/pak0.apk`) and reads each from `<key>/`
(`docs/spec/issues/162-packaging-by-game.md`): the selector lists the playable ones; with one
game it starts directly; the `game` extra forces one. The selector draws each card's own
animated title (AirStrike 3D's 3D banner, AirStrike 2's logo with its "2" emblem, Gulf Thunder's
logo; docs/spec/issues/164) and is the same screen, from the same code, as on the desktop and on
the web page.

The profile (unlocks, high scores, settings) is `<game key>/profile.bin` (`as3d/profile.bin`,
`as2/profile.bin`) in the app's internal files directory, one save per game; the selector's
last choice is `launcher.bin` beside them (it only preselects the card). A save of an earlier
release, `profile.bin` beside it, is moved there on the first start and kept as
`profile.v1.bak` (docs/spec/issues/160). It is written after a mission, a high score or a settings change and whenever the
app goes to the background. Uninstalling the app removes it. Updating the app over the old one
(`adb install -r`, or installing a new APK over it on the phone) keeps it: see
`AS3D_SMOKE_MIGRATION=1` below.

## Building

```bash
AS3D_DATA_ROOT=/path/to/main/checkout tools/android_build.sh
```

This fetches SDL2 and libopenmpt sources into `$AS3D_DATA_ROOT/third_party_local/` if
needed, copies the paks, writes `android/local.properties` (gitignored) and runs
`./gradlew assembleDebug` with the Gradle JVM capped at 1.5 GB and 2 workers. Native code is
built `RelWithDebInfo` even in the debug APK. `AS3D_ANDROID_ABIS=x86_64` builds only the
emulator ABI (faster); `AS3D_NATIVE_JOBS` sets the parallel compile jobs (default 4);
`AS3D_ANDROID_GAMES=as3d` makes the app of before, without the selector.
Output: `android/app/build/outputs/apk/debug/app-debug.apk`. Stop the Gradle daemon
afterwards on a shared machine: `(cd android && ./gradlew --stop)`.

Toolchain: Android SDK 36, NDK 28.2.13676358, CMake 3.31.6, JDK 17, Android Gradle Plugin
9.4.1 with Gradle 9.7.0, SDL 2.30.12 (SHA-256 pinned in `fetch_third_party.sh`).

### Launcher icon and name

The app is called "AirStrike" in the launcher (it holds more than one game; it was "AirStrike
3D" before the selector; the application id `org.as3dport.game` did not change, so installing
the new APK over the old one keeps the app and its saves). Its icon is an adaptive icon of our own (a
red helicopter from above under an orange rotor disc, on a dark background, with a monochrome
layer for themed icons), committed in `android/app/src/main/res/`. When the desktop viewer
(`build/apps/viewer/as3d_viewer`, or `AS3D_VIEWER`) and the game data are available,
`tools/android_build.sh` instead renders the default player helicopter from your own data as
the icon's foreground, into the gitignored `android/app/src/icon_from_data/`; set
`AS3D_ICON_FROM_DATA=0` to keep our drawing. That rendered icon comes from the copyrighted
data like the rest of the APK: it is never committed.

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
| (none) | the game selector (more than one playable game in the APK), then the chosen game's front end: intro pages, then the main menu |
| `--ez bot true` | no menus: the scripted test pilot plays (same as `as3d_game --bot`); touch still works; the first game offered unless `game` names one |
| `--ez bot true --ez menus true` | the selector and the menus as usual; the pilot plays the missions started from them (the smoke test) |
| `--ei level N` | no menus: start in mission N (1 up to the chosen game's mission count: 20 for `as3d`, 18 for `as2`) |
| `--es game KEY` | this game, no selector and no "Change game": `as3d`, `as2`, `gulf`; its files are read from `assets/<key>/` |
| `--ez allow_unfinished true` | offer and run a game that does not play yet (`gulf`); without it it is refused |
| `--ei frames N` | quit after N simulation frames |
| `--ei difficulty D` | 0 up to the game's difficulty count minus one (0..4 for `as3d`); a value outside is ignored |
| `--ez no_audio true` | no sound |
| `--ez rebuild_on_resume true` | test hook: rebuild every GL resource after each resume |

The activity is `singleInstance`: extras only apply when the app is not already running
(`adb shell am force-stop org.as3dport.game` first).

## Controls

Landscape only (either way round), full screen, the screen stays on.

**Menus**: tap. Spinners go back with a tap left of their value; the Start Game list scrolls
with its arrows; the name entry has its own keyboard; Configure keys has Clear and Cancel
(issue 090). The **Back** key is the original's Esc (back one screen; on the main menu it
asks whether to quit; it closes a hint box). Two players, mouse control and the video options
are not offered on Android.

- **Move: drag anywhere** outside the buttons. The movement is relative: the helicopter moves
  by the finger's displacement (times 1.875 by default, see Touch speed below), it does not jump to the finger, so your finger
  never has to cover it. Lift and put the finger down again to continue from wherever the
  helicopter is.
- **Fire: automatic** while any finger is on the screen (except on the pause button).
- **Buttons**: round, sized in millimetres from the display density (main buttons 12.5 mm,
  the others 9 mm), in a cluster under the right thumb: **missile** in the bottom corner with
  the selected missile type's icon and count, **power-up** above it with the selected
  power-up and its count (greyed at zero), each with a smaller **next** satellite (double
  chevron) on its inner side, and **next weapon** (the current weapon's icon) above
  power-up. They can be pressed while another finger drags. They are at full opacity while
  used and fade to a lighter look 2 s after the last button use.
- **Pause**: the small round pause button in the top corner on the other side (top centre
  on 4:3 tablets) or the Back key opens the in-game menu (Resume, Options, Quit); it is the
  only pause control. Without the menus (`level` / `bot` extras) it pauses and a tap anywhere
  continues.
- A tutorial hint box closes with its OK button (without the menus: a tap anywhere).

On screens of 16:9 and wider (most phones) the buttons sit beside the 4:3 play-field (an arc
on 20:9, a column on 16:9); on 16:10 and 4:3 tablets they are drawn translucent inside it,
clear of the HUD. They stay clear of display cutouts. The design and the reasons are in
`docs/spec/issues/100-touch-controls.md` and `140-android-polish.md`.

The three small "next" buttons show the item a press will select (icon, its count for missiles
and power-ups, and a small double-arrow badge); with nothing to switch to they are dimmed to the
arrow alone. The rule is `as3d/player_select.h`, shared with the simulation
(`docs/spec/issues/141-next-item-preview.md`).

**Settings of our own** in Options (in the original's style, stored in the profile):

- **Screen: Wide / 4:3**. Wide (default): the 3D world fills the screen. 4:3: the world and
  the menus are drawn only in the centred 4:3 area with black bars at the sides, as the
  original looked; the buttons then sit in the bars. Applied at once; the game itself plays
  the same either way. On desktop the row appears when the window is wider than 4:3, and
  `as3d_game --screen wide|4x3` overrides it for the session.
- **Controls: Right / Left**: the button cluster for the right or the left thumb (pause goes
  to the other top corner). Touch mode only.
- **Touch speed: Original, x1.25 (default), x1.5, x1.75, x2**: how far the helicopter goes for
  a given finger movement (the drag gain, and how far ahead of the helicopter a fast swipe
  may reach). Original is the feel of the first Android build. The helicopter's own top speed
  is the original game's and is not changed. Touch mode only.
- **Show FPS: Off / On**: a small frame counter in a free top corner (frames per second over
  the last second; below it the worst frame time and the simulation steps dropped in that
  second). Both targets; desktop `--fps` too.

On desktop, `as3d_game --touch` draws the same controls and makes the left mouse button one
finger (drag with the mouse; the window is resizable to try other aspect ratios). The
keyboard keeps working. The window is taken for a phone screen 68 mm tall when sizing the
buttons (`--dpi N` for another density); `--left-handed` mirrors them without the menus.

## App lifecycle

- Home, the app switcher or a call: during play the in-game menu opens (without the menus:
  the game pauses), fingers are released, sound stops, the profile is saved. Coming back shows
  the paused game under the in-game menu (without the menus: tap to continue).
- If the GL context is lost while in the background, every GL resource (renderer, HUD
  textures, shadow silhouettes) is rebuilt from the paks before the first frame.
- The simulation runs at a fixed 60 Hz, independent of the display rate; after a stall at
  most 5 steps are caught up, the rest is dropped.
- Level loads show the original's loading screen (a plain bar before the front end's pictures
  are loaded, and without the menus).

## Log markers and the smoke test

The app logs under the tag `AS3D` (`adb logcat -s AS3D:*`):

| Marker | When |
|---|---|
| `AS3D_ARGS` | at start: the arguments from the intent |
| `AS3D_GAMES present=N selector=0|1 game=KEY` | at start: the games in the APK, whether the selector opens, the first game |
| `AS3D_GAME_START size=WxH load_ms=... game=KEY` | a game is up (the first level, or the front end), after every choice on the selector |
| `AS3D_SCREEN name=main|start|ingame|...|playing|paused|intro|selector frame=N mission=M` | with the menus: the top screen changed (names of `Frontend::screenName`; `selector` between games) |
| `AS3D_SELECTOR cards=KEY@x,y,w,h;... play=... exit=... current=KEY` | the selector's layout (virtual 800x600), when it opens |
| `AS3D_GAME_CHOSEN game=KEY`, `AS3D_GAME_CHANGE game=KEY` | a card was played; "Change game" left that game |
| `AS3D_MENU name=... items=ID@x,y,w,h ...` | the top menu's items (virtual 800x600), "Change game" is item 60 |
| `AS3D_VIEW scale=S x=X y=Y` | virtual 800x600 to screen pixels (`px = v * S + X`), with the layout |
| `AS3D_LAYOUT size=... insets=... buttons=outside|inside screen=wide|4x3 hand=right|left px_per_mm=... missile=x,y,r ...` | button centres and radii in pixels, when the screen, the insets, Screen or Controls change |
| `AS3D_DPI ddpi=... hdpi=... vdpi=...` | the display density SDL reports (sizes the buttons) |
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
the bot pilot in mission 1 and `rebuild_on_resume`, waits for `AS3D_GAME_FRAME n=600`,
injects a drag, a missile and a next-weapon tap, the pause button and a tap, the Back key and
a tap, sends the app to the background and brings it back (checking `AS3D_GL_REBUILD`), taps
to continue, and plays on to frame 3600. Then it starts the first game forced (`game=as3d`,
as the app before the selector), opens and leaves Options so that `files/as3d/profile.bin`
exists, keeps a copy of it, and checks there is no `files/as2/profile.bin` yet. It restarts
the app without extras: the selector (`AS3D_GAMES present=2 selector=1`), the AirStrike 3D
card tapped (its rectangle from `AS3D_SELECTOR`), and taps through the first game's front end
by the `AS3D_SCREEN` markers: main menu, Start Game, Start, 600 frames of mission 1, the pause
button (in-game menu), Resume, pause again, Quit to the main menu, Back (exit confirmation),
No; screenshots `menu_*.png`; Options, Screen 4:3 and back. Then the pilot with the menus
(`bot`, `menus`): the selector with AirStrike 3D preselected (the last choice), AirStrike 3D's
Start Game, Start, mission 1 to 1800 frames under the pilot, pause, Quit, "Change game" (item
60 of `AS3D_MENU`) back to the selector, the AirStrike 2 card, its Start Game, Next, the
helicopter selection's Start, the start dialogue turned by taps, mission 1 to 1800 frames,
pause, Quit, "Change game" again; then the Gulf Thunder card (its own menus: Start Game, Next,
the helicopter selection's Start, no dialogue at operation 1), mission 1 to 1800 frames,
pause, Quit, "Change game". At the end, with `run-as`:
`files/as3d/profile.bin` is byte for byte what it was before AirStrike 2 and Gulf Thunder ran,
`files/as2/profile.bin` (key `as2`) is unchanged by Gulf Thunder, `files/gulf/profile.bin`
exists with the key `gulf`, and `files/launcher.bin` exists. The launcher
label must be "AirStrike". It fails on a `FATAL` marker, a Java exception or a native crash,
and on timeouts. Screenshots and the logcat capture go to
`$AS3D_DATA_ROOT/out/m8/` (gitignored; `AS3D_SMOKE_OUT` changes it). `adb shell input` has no
multi-touch, so multi-touch is covered by the unit tests only.

`AS3D_SMOKE_MIGRATION=1` adds a first stage that proves the save of the previous app build
survives the update: it installs the old APK (`AS3D_SMOKE_OLD_APK`, or one built from
`AS3D_SMOKE_OLD_COMMIT`, default `7753c66`, in a scratch git worktree under the output
directory), starts it on the front end, sets Screen to 4:3 in Options and leaves Options (the
profile is written), stops it, keeps a copy of `files/profile.bin`, installs the new APK over
it (`adb install -r`), starts it (on the selector: the AirStrike 3D card is tapped), and
checks with `run-as`: `files/profile.bin` is gone,
`files/profile.v1.bak` has the old file's bytes, `files/as3d/profile.bin` is a version 2 file
with the key `as3d`, the app logged no profile problem and comes up with Screen still 4:3
(`AS3D_LAYOUT ... screen=4x3`). The normal walk then goes on from the updated app.

## Known limits

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
- Without the menus a pause request (button, P, Back) is ignored while a tutorial hint box
  holds the pause, as in the original; with them the first tap on the pause button closes an
  open hint box. The smoke test retries either way.
- The on-screen keyboard of the name entry is the front end's own (issue 090), not Android's.
- Under heavy load the emulator's System UI may show "isn't responding"; the smoke test
  closes system dialogs before tapping.
- No app icon yet.
