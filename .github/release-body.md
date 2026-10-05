OpenAirStrike __VERSION__: a new engine that plays **AirStrike 3D**, **AirStrike 2** and
**AirStrike II: Gulf Thunder** (DivoGames) on Linux, Android and the web. No game data is
included: you need your own copy of each game (`pak0.apk`, `pak1.apk`, `pak2.apk`, Gulf
Thunder also `pak4.apk`, plus the game's `.exe` for the menu texts, and optionally
`Settings.xml` and `data/gfx/logo2s.tga`), or the original installer zip.

## Downloads

| File | What it is |
|---|---|
| `openairstrike-__VERSION__-android.apk` | Android 8.0 or newer, arm64 and x86_64, signed, no game data |
| `openairstrike-__VERSION__-linux-x86_64.tar.gz` | `as3d_game` and the headless `as3d_sim`, built on Ubuntu 22.04 |
| `openairstrike-__VERSION__-web.zip` | the web version, to host on any static web server |

## Install

**Android.** Install the APK (allow installs from your browser or file manager if asked).
On first start the app asks for your game files: copy the original installer zip or the
games' `data` folders and executables to the phone, tap "Choose files" and pick the zip or
one game's files. The app is `io.github.propaint.openairstrike`; it keeps its saves and the
imported files in its own storage. "Clear data" in the system settings removes them.

**Linux.** Needs the SDL2, OpenGL ES and EGL runtime libraries (Debian or Ubuntu:
`sudo apt install libsdl2-2.0-0 libgles2 libegl1`). Unpack the tarball, put your games where
`README.md` (Setup) says, then `./as3d_game --data /path/to/that/directory`.
`--list-games` shows what was found.

**Web.** Unzip and serve the folder with any static web server (`python3 -m http.server`
inside it is enough on your own machine), open it in Chromium, Chrome or Edge and pick your
game files on the start page; they stay in the browser's storage.

## Changes

__CHANGES__
