# 163: One app, a game selector

Status: done (packages D1 to D3). Affects `as3d/game_data.h` (`gameIsPlayable`),
`as3d/launcher.h` + `engine/src/game/launcher.cpp`, `as3d/frontend.h` (`GameSelector`,
`FrontendContent::changeGame`, `GameHost::changeGame`), `engine/src/ui/screens_launcher.cpp`,
`screens_main.cpp`, `screens_plain.cpp`, `apps/game/{game_stack,launcher_screen,game_loop,
game_flow,main,android_main}.*`, `apps/web/{web_main.cpp,site/*}`, `tools/android_build.sh`,
`tools/android_smoke.sh`, `tools/web_build.sh`, `tools/web_known_files.py`, tests
`launcher_test.cpp` and the play tests.

The owner decided: one app holds every playable game, on desktop, Android and the web, with a
game selector. AirStrike 3D (`as3d`) and AirStrike 2 (`as2`) are playable; Gulf Thunder
(`gulf`) is not offered yet.

## Playable, decided in one place

`gameIsPlayable(const GameProfile&)` in `engine/src/game/game_data.cpp`, from the list
`kPlayableGames = {"as3d", "as2"}`. Everything else reads it:

* `tools/web_known_files.py` parses that line and writes `playable` into `known_files.json`,
  which the web page reads;
* the tests' `testdata::playable()` calls the function; `testdata/golden/<key>/expected.json`
  keeps its `"playable"` as the tests' statement of which test groups apply, and
  `expected_test.cpp` checks that it agrees with the code;
* the selector lists playable games only; Android and the web refuse `gulf` without the
  development override (`allow_unfinished`, `?unfinished=1`), and the desktop selector lists it
  only with `--allow-unfinished`.

Proposed for the orchestrator-owned `GameProfile` (`game_profile.h`): a field
`bool playable = false;` set in `game_profiles.cpp` (`true` for `as3d` and `as2`), and
`gameIsPlayable` returning it; `tools/games.json` would then carry `"playable"` and
`web_known_files.py` read it there, `game_profile_test.cpp` checking both agree. Likewise the
selector's picture could become `const char* logo` (`gfx\logo\logo.tga` for `as2`,
`gfx\logo\logo_gulf.tga` for `gulf`, none for `as3d`), today `gameLogoPath` in
`apps/game/launcher_screen.cpp`.

Play tests for `as2`: the bot regression of mission 1 (world-aware pilot, god mode, seed 1,
Normal: end frame 13448, score 15390, lives 2, as in `docs/missions-status-as2.md`), the
integration tests on the game under test's data and rules, the shipped sounds count. Tests that
assume the first game's menus or content skip loudly per test (`AS3D_REQUIRE_FIRST_GAME(why)`,
the flow tests of `game_flow_test.cpp`, `all_missions_test.cpp`, whose sequel counterpart is
`as2_missions_test.cpp`).

## When the selector shows

`planLaunch(present, forced, lastChoice, allowUnfinished)`:

* a forced game (`--game`, `$AS3D_GAME`, `--paks` on the desktop; the Android extra `game`;
  `?game=` on the web) starts directly, no selector and no "Change game";
* else the playable games present, in GameId order: more than one, the selector; exactly one,
  that game starts as before; none, nothing to start (the caller reports it).
* The last choice only preselects its card; it never starts a game by itself.
* Headless runs and `--level` / `--bot` without the menus never show it (the regression script
  runs `as3d_game` and `as3d_sim` without `--game`: they still take `as3d`). On Android,
  `--bot` / `--level` without `menus` start the first game offered.

## The remembered choice: `<user data dir>/launcher.bin`

Ours, little-endian: `"AS3DLNCH"`, u32 version 1, u8 key length (1..15), the key (`a-z 0-9 _`,
naming a known game), u32 CRC-32 of the key: 13 + n + 4 bytes. Anything else (length, magic,
version, CRC, characters, an unknown key, a file over 64 bytes) is ignored. Written through
`<path>.tmp` and a rename when a card is played. On the web the page keeps the choice in
`localStorage` (`as3d-last-game`), since it chooses before the engine runs.

## The screen

`ui::GameSelector`, in the plain front end's style (issue as2/260): black bars with two rules,
"Choose a game" in the top bar, one card per game between the bars (at most 340 virtual pixels
wide, side by side, centred), Exit (left) and Play (right) in the bottom bar, a key hint on the
desktop. A card: the game's own title picture from its data when it has an obvious one
(AirStrike 2 `gfx\logo\logo.tga`, Gulf Thunder `gfx\logo\logo_gulf.tga`, fitted into the top of
the card), else the title in large text (AirStrike 3D has no title picture: its menus use
pictures of words); the title in text under a picture; "Version x"; the save summary; "Tap to
play" / "Click to play". The current card has a dark red fill and an orange frame, the focused
one a pulsing outline.

The save summary is read by `readSaveSummary` through `loadProfileFile` (never written; the
first game's save from before issue 160 is read where it is, not migrated): "N of M missions
open" and "Best score X" (the highest entry that is not one of the fresh table's), "No save
yet", or "Save cannot be read".

It uses no picture of any game but those logos: the font (`gfx\ui\font.tga`, `font_alpha.tga`,
shipped by every game of the family) comes from the first listed game's data, and nothing asks
the asset cache for a texture. Input goes through the menu system: a click or tap on a card
plays it, Play plays the current card, Left / Right / Tab move, Enter plays, Esc / Back /
Exit leaves the app. Cards and buttons stay inside the part of the screen clear of display
cutouts (`setSafeArea`, from the window's safe insets).

## "Change game"

`FrontendContent::changeGame` (the host sets it when the selector offers more than one game):

* the first game's main menu gets a text button (`kChangeGameRect` = 310, 440, 180 x 32) in the
  free band between the Exit picture (ends at y 423) and the corner rule (y 487); without it the
  menu is exactly the original (the test compares the pixels of both renders: they differ only
  inside that rectangle);
* the plain front end's main menu gets it in the bottom bar's left slot.

Choosing it saves the profile and calls `GameHost::changeGame`; the window leaves the game.

## Switching without restarting

`GameStack` (`apps/game/game_stack.*`) builds session (VFS, definitions, world), renderer,
audio and front end as one unit and tears them down in the reverse order (profile saved, front
end, audio device closed, every GL object of the renderer and the HUD, session). The window keeps
the GL context, the 2D overlay and the input mappers; between games it shows the
`LauncherScreen` (the selector with its font and logos, destroyed when a game starts). A lost
GL context (Android's context reset, the web's context loss, the resume test hook) rebuilds the
selector when it is up, the game's renderer otherwise.

`launcher_test.cpp` switches as3d -> as2 five times each way through `GameStack` and the
selector, draws each, rebuilds the renderer once in the middle as the window does after a lost
context, and counts the live GL objects (textures, buffers, vertex arrays, programs,
framebuffers, by probing names) after each teardown: they are the same after every switch
(2 textures, 1 buffer, 1 vertex array, 2 programs, 2 framebuffers: the test's own target and
overlay). Run it under AddressSanitizer with `AS3D_SANITIZE=ON`: no leak, no error.

## Per platform

* Desktop: `as3d_game` with the menus and no forced game opens on the selector when the data of
  two playable games is under the data root; `--allow-unfinished` lists Gulf Thunder too.
  `as3d_game --headless --selector-shot FILE.png [--selector-games k1,k2] [--size WxH]
  [--touch] [--insets l,t,r,b] [--selector-focus N]` draws it once.
* Android: `AS3D_ANDROID_GAMES` defaults to `as3d,as2`; the app finds the games in its assets
  (`<key>/pak0.apk`) and opens on the selector. The app's name is "AirStrike" (the application
  id `org.as3dport.game` and the icon are unchanged, so an installed app and its saves carry
  over). Extra `menus` with `bot`: the selector and menus as usual, the pilot plays.
* Web: the page's start screen chooses, not the in-engine selector. The engine needs one game's
  files before it starts, the files are 25 MB (AirStrike 3D) and 48 MB (AirStrike 2), and only
  the chosen game's should be downloaded; the page knows what it holds before anything is
  downloaded (`data/games.txt` of the bundled build, the stored files of the byo build), the
  engine would have to be started with every game's data to draw its cards. A card on the start
  screen is its game's Play button; `?game=` forces one; with more than one game offered the
  engine gets `--change-game`, and "Change game" saves, lets the page sync browser storage and
  reloads the start screen. Saves stay per game under `/persist/<key>/`.

## Not done

The sequel's own front end (its real menus) and a save summary on the web page's cards (the
page would have to read IndexedDB's copy of `/persist` itself).
