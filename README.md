# AirStrike 3D engine reimplementation

A new, from-scratch C++17 engine that loads the data of the helicopter shooters made by
DivoGames and plays them: **AirStrike 3D** (v1.70, 2002), **AirStrike 2** (v2.51) and
**AirStrike II: Gulf Thunder** (v2.71). It runs on Linux desktop (SDL2 and OpenGL ES),
Android and the web (Emscripten and WebGL 2).

The original games were created by DivoGames. This project is not affiliated with them.

## Legal position

The engine is released under the MIT licence (`LICENSE`).

The engine in this repository is original code, written from reverse-engineering
specifications (`docs/spec/`). The game data (pak archives, art, sound, music, texts) is
copyrighted, is not included here and must come from your own copy of each game; builds that
bundle that data (the Android APK, the bundled web site) are for personal use only and must
never be shared or published.

## Status

- AirStrike 3D: all shipped files load, all 20 missions can be played to the end, with the
  original front end (intro, menus, high scores, options, two players, saved profile).
  See `docs/missions-status.md`.
- AirStrike 2: all 18 missions play to the end, with its own HUD and front end.
  Co-op is not done. See `docs/missions-status-as2.md`.
- Gulf Thunder: all 24 operations play to the end, with its own loadouts, HUD and front end.
  Co-op is not done. See `docs/missions-status-gulf.md`.
- Desktop and Android are the main targets. The web version plays one player at a time and
  was tested in Chromium and on Android Chrome and Edge; Safari, iPhone and Firefox are
  untested.

The detailed table is "Implementation status" in `docs/spec/README.md`.

## Setup

### Game data you need

You need your own installation of each game you want to play. For every game the engine reads:

| Game | Key | Executable | Pak files in `data/` |
|---|---|---|---|
| AirStrike 3D 1.70 | `as3d` | `AirStrike3D.exe` | `pak0.apk`, `pak1.apk`, `pak2.apk` |
| AirStrike 2 2.51 | `as2` | `AirStrike3D II.exe` | `pak0.apk`, `pak1.apk`, `pak2.apk` |
| AirStrike II: Gulf Thunder 2.71 | `gulf` | `AirStrike3D II - Gulf.exe` | `pak0.apk`, `pak1.apk`, `pak2.apk`, `pak4.apk` |

Also used when present: `data/Settings.xml` and `data/gfx/logo2s.tga` (intro pages, version
line and menu logo). The executable is needed once, to extract the front-end texts that the
original keeps inside it (`tools/extract_exe_texts.py`). A game is recognised by its pak
files, which are checked against `tools/games.json`; a different version of a game is not
accepted.

Everything goes into gitignored directories:

| | AirStrike 3D | AirStrike 2 and Gulf Thunder |
|---|---|---|
| Game install directory | `third_party_local/original/` | `third_party_local/games/<key>/` |
| Extracted files | `assets_extracted/` | `assets_extracted_games/<key>/` |

Set `AS3D_DATA_ROOT` to use another directory instead of the repository root (for example,
from a git worktree).

If you have the archive `binaries/AirStrike.zip` laid out as `tools/setup_data.sh` expects (see
`tools/games.json`, `zip_member`), the script unpacks a game for you. Otherwise copy the game's
install directory to the location above by hand.

```
# AirStrike 3D (needs unzip; or copy the install directory into third_party_local/original/)
tools/setup_data.sh as3d path/to/AirStrike.zip
P=third_party_local/original/data
python3 tools/paktool.py extract assets_extracted $P/pak0.apk $P/pak1.apk $P/pak2.apk
python3 tools/extract_exe_texts.py --game as3d

# AirStrike 2 and Gulf Thunder (setup_data.sh also extracts the paks)
tools/setup_data.sh as2 path/to/AirStrike.zip
python3 tools/extract_exe_texts.py --game as2
tools/setup_data.sh gulf path/to/AirStrike.zip
python3 tools/extract_exe_texts.py --game gulf
```

### Build and run on desktop

Needs a C++17 compiler, CMake 3.22 or newer, and the SDL2, OpenGL ES 2 and EGL development
packages. On Debian or Ubuntu: `sudo apt install build-essential cmake pkg-config curl python3
libsdl2-dev libgles-dev libegl-dev`. The first configure downloads libopenmpt 0.8.9 into
`third_party_local/` (`tools/fetch_third_party.sh`, checksum pinned).

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
build/apps/game/as3d_game --data .
```

With the data of more than one game the window opens on a game selector. Useful flags (the
full list is at the top of `apps/game/main.cpp`):

- `--game as3d|as2|gulf` picks the game; `--list-games` shows the games found.
- `--data ROOT` is where the extracted data is (default `$AS3D_DATA_ROOT`); `--paks DIR`
  reads the paks straight from a game's `data/` directory.
- `--level N` starts directly in a mission, `--difficulty 0..4`, `--seed S`.
- `--size WxH`, `--fullscreen`, `--touch` (touch controls, mouse as finger), `--fps`.
- `--headless` with `--frames N`, `--screenshot-every K` and `--out-dir DIR` runs without a
  window, for tests and screenshots.

Without `--level`, the game opens on the original front end; Esc opens the in-game menu.

### Android

Needs the Android SDK 36, NDK 28.2.13676358, CMake 3.31.6 and JDK 17 (see `docs/android.md`).

```
tools/android_build.sh
```

This builds `android/app/build/outputs/apk/debug/app-debug.apk` and bundles the paks from
`third_party_local/` into it, so the APK contains copyrighted data: keep it to yourself.
`AS3D_ANDROID_GAMES` chooses the bundled games and `AS3D_ANDROID_ABIS` the ABIs.

### Web

Needs Emscripten (`docs/web.md` names the version and how to install it).

```
tools/web_build.sh byo      # engine only, into out/web/site-byo/
AS3D_WEB_SITE=$PWD/out/web/site-byo tools/web_serve.sh   # http://127.0.0.1:8080/
```

The bring-your-own site contains no game data: on first start the page asks you to pick the
files of your own copy and keeps them in the browser's storage. `tools/web_build.sh bundled`
copies your data into the site instead; never host that build publicly. Details in
`docs/web.md`.

## Releases

A version tag (`v1.2.3`) builds a draft GitHub release (`.github/workflows/release.yml`) with
three data-free artifacts and notes made by `tools/release_notes.sh` from `CHANGELOG.md`:

- `openairstrike-<version>-linux-x86_64.tar.gz`: `as3d_game` and `as3d_sim` built on Ubuntu
  22.04 (needs `libsdl2-2.0-0`, `libgles2` and `libegl1`); point them at your data with
  `--data`.
- `openairstrike-<version>-web.zip`: the bring-your-own-data site, to serve from any static
  web server; the page asks for your game files on first start.
- `openairstrike-<version>-android.apk`: the signed APK without any game data; the app asks
  for your game files on first start (`docs/android.md`). Its application id is
  `io.github.propaint.openairstrike`, so it installs beside a personal build with bundled data
  (`org.as3dport.game`).

## Tests

```
tools/ci.sh
```

builds the desktop tree, runs the C++ tests (`as3d_tests`) under a memory cap and the Python
reference tests in `tools/ref/`, once per game whose data is present. Without data the tests
that compare against the game files are skipped and say so. `AS3D_CI_GAMES` selects games and
`AS3D_TEST_MEM_KB=0` removes the cap (needed for sanitizer builds).

## Documentation

- `docs/spec/`: the reverse-engineering specifications (file formats, script VM, simulation,
  rendering, front end), the design reference for the engine; per-game differences are in
  `docs/spec/as2/` and `docs/spec/gulf/`. Start with `docs/spec/README.md`.
- `docs/android.md`, `docs/web.md`, `docs/graphics.md`, `docs/running-originals.md`.
- Layout: `engine/include/as3d/` public headers, `engine/src/<module>/` the engine, `apps/`
  the game, viewer and tools, `android/`, `tools/`, `testdata/golden/` committed test goldens.
