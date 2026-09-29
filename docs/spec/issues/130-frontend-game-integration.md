# 130: the front end in the game: choices where the specs are silent

Status: open, choices made. Raised by WP-50 (the front end wired into `as3d_game` and the
Android app). Affects `apps/game/game_flow.*`, `apps/game/game_loop.cpp`, `apps/game/main.cpp`,
`engine/src/input/`. `frontend.md` stays the authority for screens and flow; issue 090 lists the
touch additions of the menus themselves, 100 the touch controls.

## 1. Options the game does not offer

* **Video options** (Resolution, Refresh rate, Color Depth, Fullscreen) are hidden on the
  desktop too, not only on Android (`FrontendContent::videoOptions = false`). The window size
  comes from the command line (`--size`, `--fullscreen`, resizable with `--touch`) and the 4:3
  field is scaled to any window (issue 091), so a mode list would only restate that; the
  original's Apply also restarts the whole game. The profile keeps the values.
* **3D Sound** is hidden with them: the audio bridge only plays 2D sound (issue 050 section 7).
* **Mouse Control** is implemented on the desktop (`engine-behaviour.md` 7.3: player 1's four
  direction bits follow the pointer with a 20-pixel dead zone, `gfx\mc_cur.tga` is drawn during
  play) and hidden in touch mode, where there is no pointer between taps.
* **Two players** are offered on the desktop and hidden on Android (one touch layout).

## 2. Key bindings

* The profile's bindings (the original's codes) drive the key mapper; they are applied at
  start and after every change in Options or Configure keys.
* Joystick codes (203 to 238, 241 to 244) and the wheel are kept in the profile but bind
  nothing: there is no joystick support.
* `frontend.md` 6.2 gives both players the same built-in keys (the original expected a joystick
  for player 2), so with a keyboard both helicopters would answer the same keys. A **fresh
  profile on the desktop gives player 2** W / S / A / D to move, F fire, G missile, H power-up,
  E next weapon, Q next missile, R next power-up (the joystick codes stay in the second slot).
  An existing profile is used as it is.
* Player 1's built-in defaults bind '2' to both Switch Weapon and Switch Power-Up, as the spec
  says; kept.
* P, Pause, Esc, F5 to F9 and Enter are the front end's (pause, in-game menu, volumes, camera,
  hint box); the mapper's own pause and hint-confirm keys are cleared behind the front end.
* While the UI takes the input of a frame (a menu is up, or Esc / P was pressed) key and button
  presses do not reach the mapper; releases always do, so nothing sticks.

## 3. Pause, background, Back

* P pauses without drawing anything (`frontend.md` 3.17). The world is frozen through
  `World::setPaused`; closing the hint box calls `World::dismissHint`.
* Music and sounds go on under the menus and during the pause (`frontend.md` 1.2, question 6);
  audio is paused only while the app is in the background.
* Going to the background during play opens the in-game menu, so the game comes back paused
  under a menu, and saves the profile (the process may be killed).
* Android Back is the front end's "back" (Esc). On the main menu, where Esc does nothing, it
  opens the exit confirmation; during the intro pages it speeds them up.
* Touch mode: the touch overlay's pause button is the only pause control during play; it acts
  as Esc (opens the in-game menu). The front end's own MENU button (issue 090) is not drawn.
  Fingers go to the menus whenever a menu (or the intro) is up and to the touch controls
  otherwise.

## 4. Level flow

* The attract level is drawn at random from intro1 to intro4 once at boot (wall clock, it is
  not part of any recorded game) and reused after every Quit (`frontend.md` 1.3). Headless runs
  use intro1 unless `--attract N`.
* Every level start re-initialises the world with the campaign's difficulty, player count and
  helicopters, then sets lives-at-start and banked score from the campaign before loading
  (`frontend.md` 5.2). The world's random generator restarts from the configured seed at every
  level start, so a mission started from the menus plays like `--level N` with the same input:
  same entities, positions, scripts and scores. Only the entity slots' generation counters (and
  so the values of entity references) differ, because the attract level used the slots first;
  `apps/tests/game_flow_test.cpp` checks this.
* The level-name typewriter uses `World::time()` as the level clock (it restarts at level load
  and stops while paused).
* The mission report takes `p_scores`, `p_lives`, `p_stars` and kills from the player records
  and the star total, maximum score and enemy total from the world.
* Cheat codes (`engine-behaviour.md` 14) are not implemented: the game side has none yet, so the
  "cheat used" flag is always false and the rank is never "Cheater".
* The bot's (or an input script's) hint confirm closes the front end's hint box, as Enter does.

## 5. Saving

On top of the points of issue 091 item 9 (after EndLevel, a high-score insert, leaving Options
or Configure keys, Exit -> Yes), the profile is written when the window is closed and when the
app goes to the background. Desktop: `$XDG_DATA_HOME/airstrike3d/profile.bin` (default
`~/.local/share/airstrike3d/`), or `--profile FILE`; headless runs write nothing unless
`--profile` is given. Android: `profile.bin` in the app's internal files directory.

## 6. Time

The menus run on wall-clock time (frame time capped at 0.1 s), the world on its fixed 60 Hz
step; headless, one loop frame is 1/60 s of UI time and one world step.
