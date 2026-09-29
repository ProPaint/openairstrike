# 240: AirStrike 2 front end: quirks of the original and recommended choices

Status: open, recommendations made (package B7). Spec: [../frontend.md](../frontend.md). Each point
states what the original does (with the evidence in the spec) and what an implementation should
do. "Keep" means reproduce the original; "fix" means a deliberate, small deviation, to be listed
in the spec README's deviation table when implemented.

| # | Original behaviour (spec section) | Recommendation |
|---|---|---|
| 1 | A menu's closing flag (menu +0x118) is never set; its branch in `UI_Frame` would freeze the open value (2.1) | implement the opening only; no closing animation |
| 2 | The panel's static noise takes two `rand()` values per panel per frame (3.1) | use a front-end random source separate from the world's generator, so menus never change a game's random sequence (the world's RNG is ours anyway, spec README) |
| 3 | Text buttons slide in from the bottom edge in 0.25 s while their hit rectangles are already in place; hidden buttons are still drawn, below the screen (2.5) | keep the slide; accept activation during the slide (as the original); skip drawing a button whose slide value is 0 |
| 4 | Helicopter selection: leaving with Esc (or Accept is disabled but Esc still pops) keeps a locked helicopter in the player record, and the next mission would spawn it (3.18) | fix: on leaving, if the shown player's choice is locked, restore the choice the screen opened with |
| 5 | Helicopter selection: the shown player starts from the Player spinner's value of the previous opening, also in one-player mode (3.18) | fix: start on player 1 at every opening |
| 6 | Helicopter selection backdrop: zero UV divisors, so the tiles' UVs are undefined (3.18) | draw each 120-px tile with texels 0..120 of `grid.tga` |
| 7 | Game Complete: the Continue button's hit rectangle exists before the button is drawn at mt = 4 (3.9) | fix: ignore it until it is drawn |
| 8 | `game.bin` is not written when the window is closed, only on Exit → YES, before a video restart and before the fatal-error box (6.1) | already decided for our engine: write the profile after every change (issue 091 item 9, issue 130 §5), including the checkpoint at every `EndLevel` |
| 9 | Right click on the tutorial box pops it without unpausing (3.15) | already decided: closes and resumes (spec README deviation) |
| 10 | Portrait dialogue: every line after the first is passed to the text routine with its leading LF byte (3.19) | draw the lines without the LF |
| 11 | "New helicopter is available." shows whenever the level has an `enableHelic`, even when that helicopter was already unlocked by an earlier run (3.8) | keep |
| 12 | A Continue resumes lives, score and rank but uses the difficulty chosen now (5.10) | keep |
| 13 | After mission 18 the checkpoint is 18, never offered as Continue (5.4) | keep |
| 14 | Mission Complete → Quit loses the mission's banking but not its checkpoint, so Continue resumes with the finished mission's totals (5.4) | keep |
| 15 | Widgets drawn before the panel are multiplied by its noise (3.1) | keep (it is the look) |
| 16 | Mouse control defaults to on with relative mouse steering and fire on the mouse buttons (6.2, 8) | desktop: follow the original's defaults for AS2 profiles; touch: the touch controls of issue 100 replace it |
| 17 | The Information screen draws "PgUp - Previous Page" / "PgDown - Next Page" hints (3.14) | touch mode: hide them, as issue 090 does for the first game |
