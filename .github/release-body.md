OpenAirStrike __VERSION__: a new engine that plays **AirStrike 3D**, **AirStrike 2** and
**AirStrike II: Gulf Thunder** (DivoGames) on Linux, Android, the web and the Nintendo 3DS. No game data is
included: you need your own copy of each game (`pak0.apk`, `pak1.apk`, `pak2.apk`, Gulf
Thunder also `pak4.apk`, plus the game's `.exe` for the menu texts, and optionally
`Settings.xml` and `data/gfx/logo2s.tga`), or the archive or installer you downloaded.

## Downloads

| File | What it is |
|---|---|
| `openairstrike-__VERSION__-android.apk` | Android 8.0 or newer, arm64 and x86_64, signed, no game data |
| `openairstrike-__VERSION__-linux-x86_64.tar.gz` | `as3d_game` and the headless `as3d_sim`, built on Ubuntu 22.04 |
| `openairstrike-__VERSION__-x86_64.AppImage` | Steam Deck and other x86_64 Linux: one file, imports your game files by itself |
| `openairstrike-__VERSION__-linux-aarch64.tar.gz` | `as3d_game` and `as3d_sim` for 64-bit ARM Linux (glibc 2.31 or newer, OpenGL ES 3) |
| `openairstrike-__VERSION__-portmaster.zip` | PortMaster package for Linux handhelds |
| `openairstrike-__VERSION__-web.zip` | the web version, to host on any static web server |
| `openairstrike-__VERSION__-3ds.zip` | Nintendo 3DS: the Homebrew Launcher app, the CIA, `run.ini` and `INSTALL.txt` |

## Install

**Android.** Install the APK (allow installs from your browser or file manager if asked).
On first start the app asks for your game files: copy the archive or installer you downloaded, or the
games' `data` folders and executables to the phone, tap "Choose files" and pick the archive or
one game's files. The app is `io.github.propaint.openairstrike`; it keeps its saves and the
imported files in its own storage. "Clear data" in the system settings removes them.

**Steam Deck.** In Desktop Mode, make the AppImage executable (Properties > Permissions) and
add it to Steam as a non-Steam game. Its first start creates a folder `OpenAirStrike-games`
next to it: put your game files there (the game folder, its installer or the archive) and
start it again from Game Mode.

**Linux handhelds (PortMaster).** Unzip the package into your PortMaster ports folder, put
your game files in `openairstrike/gamefiles/` and start it from the Ports menu. If it does
not work, please open an issue with the `log.txt` it writes (TESTING.md in the package).

**Linux.** Needs the SDL2, OpenGL ES and EGL runtime libraries (Debian or Ubuntu:
`sudo apt install libsdl2-2.0-0 libgles2 libegl1`). Unpack the tarball, put your games where
`README.md` (Setup) says, then `./as3d_game --data /path/to/that/directory`.
`--list-games` shows what was found.

**Web.** Unzip and serve the folder with any static web server (`python3 -m http.server`
inside it is enough on your own machine), open it in Chromium, Chrome or Edge and pick your
game files on the start page; they stay in the browser's storage.

**Nintendo 3DS.** Needs custom firmware (Luma3DS) with the Homebrew Launcher or FBI.
`INSTALL.txt` in the zip is a step by step guide: the web page's "Files for the 3DS"
(https://propaint.github.io/openairstrike/) makes the SD card files from your games, then
both zips are unzipped at the root of the SD card.

## Changes

__CHANGES__
