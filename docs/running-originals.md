# Running the original games under Wine

The three original executables (AirStrike 3D v1.70 `as3d`, AirStrike 2 v2.51 `as2`, Gulf
Thunder v2.71 `gulf`) run on this machine under a user-level Wine, in a private virtual X
display, for reference screenshots. Nothing is installed system-wide and nothing of it lives in
the repository except `tools/run_original.sh` and this page. Screenshots go to
`out/reference/<key>/` (gitignored; screenshots of the games are never committed), with an
`index.md` per game describing each picture.

## What is installed where

Everything is under `~/tools/wine` (override with `WINE_HOME`); see `~/tools/wine/README.md`
for versions, download URL and SHA-256.

- Wine 11.0 stable, Kron4ek "amd64-wow64" build (32-bit Windows programs without 32-bit host
  libraries): `~/tools/wine/wine-11.0-amd64-wow64/`.
- Xvfb and xdotool extracted from the Ubuntu .deb files (no root): `~/tools/wine/x11/`.
- The prefix `~/tools/wine/prefix-airstrike` (desktop integration, menu builder, Mono and Gecko
  off; Wine audio off; WININET proxy pointed at a closed local port).
- Private copies of the installs: `~/tools/wine/games/<key>/`, made by the script from
  `third_party_local/` (never run inside the repository). In the copies: `PostScores
  allow="0"` in `data/Settings.xml`, `config.ini` set to windowed (`Fullscreen=0`), 800x600
  (`VideoMode=1` for as3d, `-1` plus `ForceStdModes=1` for the sequels: under Xvfb Wine reports
  only the screen's own mode), sound volumes 0.
- Helpers in `~/tools/wine/bin/`: `xwd2png.py`, `unlock_gamebin.py` (unlock every mission and
  helicopter in a `game.bin`; `--check` prints the flags), `sheet.py` (contact sheet).

## Using the script

```
tools/run_original.sh <key> [sub-commands] [-- game args]   # start as3d | as2 | gulf
tools/run_original.sh [sub-commands]                         # drive the running instance
```

Sub-commands run in order: `--shot f.png` (the 800x600 game client area; `--shot-full` for the
whole 1024x768 display), `--key Return` (xdotool key names: `Escape`, `space`, `Up`, `p`, `F9`,
`3`...), `--hold Up 2` (hold a key for 2 s), `--type invulnerability` (cheat words), `--click x
y` and `--move x y` (coordinates inside the 800x600 client), `--wait s`, `--status`, `--stop`,
`--reset` (with a key: recreate the private copy).

Examples:

```
tools/run_original.sh as2 --wait 5 --key Escape --wait 14 --shot out/reference/as2/menu.png -- -god
tools/run_original.sh --click 400 244 --wait 2 --shot out/reference/as2/start_game.png
tools/run_original.sh --stop
```

The start refuses when less than 3 GB of memory is available (`RUN_ORIGINAL_MIN_AVAIL_MB`), runs
the game under `systemd-run --user --scope -p MemoryMax=3G` (`RUN_ORIGINAL_MEM`), `nice` and
`timeout 600` (`RUN_ORIGINAL_TIMEOUT`), on display `:77` (`RUN_ORIGINAL_DISPLAY`). One instance at
a time. Always finish with `--stop` (kills wineserver and Xvfb). Logs: `~/tools/wine/run/` and
the game's own `game.log` in its private copy.

To unlock everything: `python3 ~/tools/wine/bin/unlock_gamebin.py ~/tools/wine/games/<key>/game.bin`
while the game is not running (the game rewrites game.bin when it exits through its menu). The
as2 and as3d copies are already unlocked; gulf is not (the owner's save: operations 1 to 3). The
gulf layout (3 helicopters, 24 missions, file size 1792) was inferred from the file, not from a
spec.

## Driving the games: what works

- Mouse: the games read relative mouse motion, so a click must be preceded by a move; the
  script's `--click` hovers, nudges one pixel, then presses and releases. Clicks at client
  coordinates work in every menu tried.
- Keys: `Escape` skips the logo and intro comic (twice: logo, then comic) and opens the in-game
  menu; `space` also turns intro pages. `p` pauses. **Typing a cheat word that contains a `p`
  toggles the pause** (`showmetheweapons`, `moremoreweapons`, `glitteringprizes` each contain one):
  after an odd number of `p`s, press `p` again.
- `--hold Up 2` moves the helicopter (the games poll key state); `space` uses the selected
  power-up; the power-up switch key defaults to `2` (same as weapons) in as2, so the as2 copy's
  player-1 binding was changed to `3` (`KeySwitchPowerUp=51 212`).
- The sequels accept the `-god` switch (`-- -god`; the health bar stayed full under fire in
  missions 15 and 18, not checked more rigorously). Cheat words from the specs work in all three
  games (as2: `invulnerability`, `showmetheweapons`, `moremoreweapons`, `glitteringprizes`,
  `deadlineisnear` ends the level, `diediediemydarling` game over; as3d: `iwannabe`,
  `armorychamber`, `launchmenow`, `iamstronger`).
- Mission selection: Start Game (as2 at client (400, 244)), click a mission row, scroll with the
  list's down arrow (as2 (599, 332), as3d (703, 248)), Next (700, 534), helicopter screen Start
  (700, 534). A level loads in about 20 s with software rendering.

## Rendering

Xvfb has no GPU: OpenGL is Mesa llvmpipe (as3d's game.log: "llvmpipe (LLVM 20.1.2)"), and the
sequels' Direct3D 8 goes through wined3d onto the same GL (their log names a fake "NVIDIA GeForce
GTX 470"). A game uses about 1.2 CPU cores and 300 MB; frames are fine for screenshots (in play
the frame rate is low but the game runs in real time). Headless GPU access was not attempted
further (the X server would have to be the NVIDIA one).

## What does not work / limits

- No sound: Wine's audio driver is disabled on purpose (the owner's sound server is not
  touched); BASS reports `BASS_Init: Failed.` and the games run silently.
- No network namespace: `unshare -r -n` and `bwrap --unshare-net` are refused on this machine
  (`kernel.apparmor_restrict_unprivileged_userns = 1`). Score posting is off in Settings.xml and
  WININET goes to a dead proxy instead.
- Wine's window frame is drawn around the game window; `--shot` crops to the client area using
  the window geometry from xdotool (the window class is the exe name).
- The display is 1024x768 so that the whole 800x600 client plus frame fits.

## Aligned comparisons with our renderer

To compare a place of the original with ours by numbers (as in
`docs/spec/as2/render-corrections.md`):

1. Start the mission in the original and leave the player alone (no arrow keys, and park the
   mouse: with `MouseControl=1` the helicopter follows the pointer, so after the last menu
   click the player drifts towards it; in the sequels the Start/Continue button is at the
   bottom right, and the drift is small). Take `--shot`s every 3 s; `p` pauses for an exact
   frame. The level holds its scroll while the start dialogue is shown.
2. Render the same mission with our game headless, one frame every 10 ticks (7 map units):
   `as3d_game --game as2 --headless --level N --god --frames 2400 --screenshot-every 10
   --out-dir DIR --size 800x600 --no-audio`, inside `ulimit -v 4000000` and `timeout`.
   Or sweep the viewer: `as3d_viewer --game as2 level N --scroll S --camx X --time T` (the
   `level` command applies the game's brightness overlay, 0.6 by default).
3. `python3 tools/compare_reference.py ORIGINAL.png --region x0,y0,x1,y1 --shift 40 --best-of
   DIR/frame_*.png` picks the best frame (correlation of blurred luma over static scenery, with
   a vertical shift search); 0.9 and above is a clean match.
4. `python3 tools/compare_reference.py ORIGINAL.png OURS.png --region ...,name --side pair.png
   --diff diff.png [--hist]` prints mean colour, deviation, luma and ratios per region.

Menu details found on the way (as2): the mission list's down arrow (599, 332) moves the
selection by one mission (the list scrolls with it), the up arrow is at (599, 211); the list
opens on mission 4 after a fresh start; on the helicopter screen the right arrow (605, 297)
selects the second helicopter (Sky Keeper, the one our game uses by default) and the button
then reads Continue (680, 534). A third `Escape` at start skips the intro comic reliably.
