# Testing the PortMaster package

The package has not run on a handheld yet: the maintainer has no PortMaster device. Thank you
for trying it. Nothing here needs a terminal; everything you need to send back is in one log
file.

## What you need

- A handheld with PortMaster and a 64-bit (aarch64) firmware: ArkOS / dArkOS, ROCKNIX,
  AmberELEC, muOS, Knulli, ... The GPU must do OpenGL ES 3.0 (Mali-G31 in the RK3326 and
  H700 devices, Mali-G52 in the RK3566 ones: yes; Mali-400 / Mali-450 devices: no).
- Your own copy of at least one of the games: AirStrike 3D 1.70, AirStrike 2 2.51 or
  AirStrike II: Gulf Thunder 2.71 (Windows): its install folder, or the zip / installer.

## Install

1. Unzip `openairstrike-<version>-portmaster.zip`. Copy `OpenAirStrike.sh` and the
   `openairstrike/` folder into your ports folder (for example `roms/ports/` or
   `roms2/ports/`), next to the other ports.
2. Copy your game files into `ports/openairstrike/gamefiles/` (see the `README.txt` there).
3. Refresh the game list of your front end if the port does not show up.

## What to try

Each line is worth a word in your report, even "works".

1. **No game files.** Start the port with `gamefiles/` empty. Expected: a text screen "No game
   files yet" naming the folder; any button closes it. Is the text readable on your screen?
2. **Import.** Put a game in `gamefiles/` and start again. Expected: "Importing ..." then
   "Import done", any button opens the game. How long did the import take? (An installer or a
   RAR takes longest.)
3. **Start-up.** The intro pages, then the main menu over a flying helicopter. With more than
   one game imported, a game selector comes first.
4. **Menus with the pad.** D-pad / stick move, A confirms, B goes back. Can you reach Start
   Game, Options and the mission list? Is the text readable at your screen size?
5. **Play a mission.** Fly (D-pad / stick), fire (A), missile (B). Does the frame rate feel
   smooth? Is the HUD (score, health, weapons) readable? Is there sound and music?
6. **In-game menu and quit.** Select or Start opens the in-game menu; quitting from it returns to
   the front end of your firmware. Select + Start should also close the game at any time.
7. **Saves.** Finish or quit a mission, start the port again: is your progress kept?
8. **Optional: frame rate.** Put `--fps --perf` on one line in
   `ports/openairstrike/args.txt`, play a minute, quit. The log then has frame times every
   5 seconds. Remove the file afterwards.

## What to send back

- `ports/openairstrike/log.txt` from the run you report about (it is overwritten at each start:
  copy it after the run). Its header names your device, firmware, screen size, glibc and the
  libraries the game found; the game's own lines name the SDL video driver, the drawn screen
  size and the GPU driver (`sdl-window: driver=... size=... vendor=... renderer=...
  version=...`). It holds no personal data beyond your firmware's folder names; look it over
  before posting.
- Your device and firmware with its version, and the answers to the list above.
- A photo of the screen where something looks wrong (text cut off, too small, misplaced).

Known limits, no need to report them: entering a name for the high scores needs a keyboard
(the pad cannot type; A accepts an empty name); online co-op needs the codes pasted, which a
handheld without a keyboard cannot do.
