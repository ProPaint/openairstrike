# 300: AirStrike 2's front end: implementation findings, corrections and choices (package E)

Status: implemented (`FrontendStyle::SequelMenus`; `engine/src/ui/screens_as2_*.cpp`,
`as2_*.cpp`; tests `apps/tests/as2_frontend_test.cpp`). Spec: [../frontend.md](../frontend.md),
quirks [issue 240](240-frontend-quirks-and-choices.md).

## Evidence

Every screen was rendered with `as3d_viewer --game as2 screen <name> --level <attract or
mission> --scroll S [--state ...]` (the level behind, the game's brightness pass) and compared
with the original's screenshots at 800x600: `out/reference/as2/` (see its index.md) and new
captures taken with `tools/run_original.sh as2` for this package (main menu over `intro2`, the
exit confirmation, Start Game scrolled to mission 18, the helicopter selection, the loading
comic of mission 18, mission 1's start dialogue and first tutorial tip, Game Complete after
`deadlineisnear` in mission 18). Pairs and crops are in `out/as2/frontend_pairs/` (not
committed). Positions of every frame, button, rivet, cable, title tab, text line, list row,
slider, statistics line, comic tile and portrait agree to the pixel as far as can be read; the
differences left are listed at the end.

## Corrections to the spec and to its text table

1. **`tools/exe_texts/as2.json`, `ctl.row.*`.** The row table as2@0x49d080 has thirteen
   20-byte records, not ten: three are `"-"` separators (records 2, 5 and 8, pointing at
   as2@0x48d6d8), which is why the rows of 3.7 have gaps. The list gives `ctl.row.2`, `.5`, `.8`
   the separator and misses Move Backward, Move Left and Move Right. The action names are at
   0x48d6ec, 0x48d6dc, 0x48d6c8, 0x48d6b8, 0x48d6ac, 0x48d6a0, 0x48d690, 0x48d680, 0x48d674,
   0x48d668 (read in the owner's executable: the first u32 of records 0, 1, 3, 4, 6, 7, 9..12).
   `tools/extract_exe_texts.py` and its JavaScript port apply these addresses over the list's
   (the list itself belongs to the spec package and was not edited).
2. **Helicopter selection, 3.18 step 4.** The Speed and Armor bars show only their fills; the
   outlines (300, 381/401, 280, 10) do not appear in the original's screen (`menu_helicopter_
   select.png`, `orig_heli18.png`: nothing at x 480..580 on those rows). Not drawn.
3. **Configure controls, 3.7.** The outline (200, 205, 400, 270) does not appear in the
   original's screen (`menu_configure_controls.png`). Not drawn.
4. **List, 2.5.** Under the pointer, the list's up and down boxes are drawn orange instead of
   green (`orig_start18.png`, the pointer on the down box). Added (never in touch mode). The
   thumb's travel (open question 6): at the last row it sits right above the down box, as the
   proportional guess puts it (same picture).
5. **Dialogue speakers.** The pages confirm the table: in mission 1 and 3 the pilot page
   addresses the officer as "Snake", and the officer's pages show the officer's portrait
   (`m3_portrait_dialogue.png`, `orig_m1_*`).

## Engine choices

- **Key presses in a dialogue** (3.19): a key or tap while a page types completes it; on a
  complete page it goes to the next page at once. A dialogue never closes on a key.
- **Intro comic in touch mode:** a tap speeds the page up four times (as a key or click in the
  original) and a small " Skip " text button at the bottom right (virtual (780 - w, 560),
  touch mode only) ends the whole intro at once. No long press.
- **Finger-sized hit rectangles in touch mode** for the exit confirmation's YES / NO labels
  (100 x 40) and the helicopter selection's arrows (44 px wider and 40 px taller); drawn as in
  the original.
- **Touch additions in the screens' style:** the MENU button during play (" MENU ", a text
  button at the top centre, when the host has no pause control of its own), the controls
  screen's " Clear " and " Cancel " (text buttons at y 520 while a row waits for a key), the
  name entry's keyboard (dark-green keys with a green outline under the panel, and "Del").
- **Main menu "Change game"** (issue 163): a text button of minimum width 200 centred at
  (400, 510), in the free band between Quit and the version line, only with several games.
- **Cooperative mode is not offered yet:** the game hosts pass `twoPlayerMode = false` for
  AirStrike 2 (its co-op rules are not done), so the Game mode spinner holds "Single Player"
  only; the two-player screens (Player spinner, taller panel, statistics hidden) are
  implemented and tested with a host that offers it.
- **Texts:** without `texts_as2.txt` the labels use built-in English defaults (the
  executable's captions with their padding spaces); dialogues are skipped (a mission starts at
  once), Information and Credits show a notice, helicopter names read "Helicopter N", Game
  Complete shows its first line and a notice.
- **Checkpoint** (5.1, 6.1): saved in the profile's optional chunk `CHKP` (profile.h), written
  at every EndLevel and whenever G_NewGame resets it; a session that used a cheat saves none.
- **Profile loading:** reading a save used to force helicopters 1 and 2 unlocked (the first
  game's fresh install) for every game; it now keeps the game's own fresh-install unlocks, so an
  AirStrike 2 save no longer gets Sky Keeper for free. An AirStrike 2 save written by the plain
  front end before this change may already hold that unlock; it is kept.
- **The start dialogue's hold:** the front end pauses the world right after the level start
  (`GameHost::setPaused`); `GameFlow` pauses a level loaded while the front end is paused (the
  web's deferred loads), so the scroll does not move under the dialogue.
- **3D preview:** `GameHost::drawModel(ModelView)`, drawn by `GameView::drawModel` with the
  object's attachment tree (`ObjectTree`, no script) and the default scene lighting; spin
  60 degrees per second, pitch 100 + 5 sin(2 mt).

## Differences left

- The preview helicopter has no blinking lights (they are particle or sprite attachments the
  script-less tree does not draw) and its lighting is our default scene light, not the
  original's.
- Options on the desktop viewer show the first game's video-mode names ("800 x 600"); the
  original enumerates "%dx%d". The port offers no video options.
- The panel noise is our own random sequence (issue 240 item 2), so its grain differs frame by
  frame.
