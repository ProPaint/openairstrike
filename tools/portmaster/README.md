## Notes

OpenAirStrike is an open-source (MIT) reimplementation of the engine of DivoGames' helicopter
shooters AirStrike 3D (1.70), AirStrike 2 (2.51) and AirStrike II: Gulf Thunder (2.71). No game
data is included: you need your own copy of each game you want to play (the Windows version).

Thanks to DivoGames for the original games. Engine source: https://github.com/ProPaint/openairstrike

## Installing the game files

Copy into `ports/openairstrike/gamefiles/`, for each game you own, one of:

- the game's install folder from a Windows PC (the folder holding the `.exe` and the `data`
  folder with `pak0.apk`, `pak1.apk`, ...), copied whole;
- or the game as you downloaded it: a zip, RAR or 7z archive, or its setup `.exe` (Inno Setup;
  NSIS and Gentee installers are refused: install those on a PC and copy the folder).

At the next start the game installs them into `ports/openairstrike/data/` (an archive can take
a minute) and says what it found. With nothing usable it shows where the files go and what was
wrong, then closes on any button. After a successful import the files in `gamefiles/` can be
deleted to save space.

Profiles and saves are in `ports/openairstrike/conf/`. Everything the game prints is in
`ports/openairstrike/log.txt`.

## Controls

| Button | In flight | In the menus |
|--|--|--|
| D-pad / left stick | Fly | Move the selection; Left / Right change a value |
| A | Fire | Confirm |
| B | Missile | Back |
| X | Use the power-up | |
| Y | Switch weapon | |
| L1 / R1 | Switch missile | |
| L2 / R2 | Switch power-up | |
| Start | In-game menu | Back |
| Select | In-game menu | Back |
| Select + Start | Quit (gptokeyb) | Quit |

Two players on one device: a second controller flies player 2 (the game's Players screen
says who flies with what).

## Compile

```shell
# Ubuntu 20.04 arm64 (glibc 2.31) with CMake 3.22 or newer, see the linux-arm64 job of
# .github/workflows/release.yml in the OpenAirStrike repository
apt install build-essential pkg-config curl git python3 libsdl2-dev libgles-dev libegl-dev zlib1g-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DAS3D_BUILD_TESTS=OFF \
  -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"
cmake --build build --target as3d_game_app
tools/portmaster/package.sh build/apps/game/as3d_game VERSION
```
