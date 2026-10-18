# 3DS UI rework: the screens today, a critique, one proposal (R4.3)

Analysis and design only (2026-10-11): no code changed. The owner's brief: "rework the UI for small and
dual screen and looks. Readability and looks." Every picture named here is under `out/ui3ds/` (local,
gitignored: game art, never committed). Captures: build `d63fbbf` (`out/3ds/drop-round3b-d63fbbf/`) on
Azahar, `run.ini` front-end runs with `dump_screens` / `dump_every` and the bot; desktop references from
`build/apps/game/as3d_game` (built 2026-10-11 14:56) at 800x600.

## Units

Both 3DS screens are about 132 dpi: **1 px = 0.192 mm** (top 400x240, 3.53" on the New 3DS, 3.88" on
the XL; bottom 320x240). A thumb wants **7 mm = 36 px**. The game's font (`gfx/ui/font.tga`, 256x256,
8x16 cells of 32x16 texels, 1-texel strokes) has a **cap height of 10 texels**; the 3DS draws the
desktop's 800x600 virtual screen at 0.4, so a scale-1 text has a 4-px cap (0.77 mm) and 0.4-px strokes.
Sizes below are measured on the dumps (cap height of the upper case).

| text as drawn today | px/texel | cap | mm |
|---|---|---|---|
| body text (Information, Top Scores table, Options labels, mission list, results, selector cards, the sequels' labels) | 0.4 | 4 px | 0.8 |
| HUD counts (AirStrike 3D), level name, hint box text | 0.6 | 6 px | 1.2 |
| AirStrike 3D menu items (pictures) and HUD score | 0.8 | 8-9 px | 1.6 |
| the sequels' HUD score and counts | 0.4-0.6 | 4-6 px | 0.8-1.2 |

## 1. Captures (out/ui3ds/current/)

Each capture exists as `<name>_top.png`, `_bottom.png` and `_both.png` (the two screens stacked as on the
console), each at 1x and `@2x` (nearest).

- `selector/sel_00_as3d`, `sel_01_as2`, `sel_02_gulf`: the game selector, each card focused (all three
  games are installed on the Azahar SD).
- `as3d/`: 01 main menu (attract + 3D banner), 02 options, 03 top scores, 04-07 information pages (story
  1 and 2, overview, weapons), 08 exit dialog, 09 start game, 10 play early, 11 level name typed, 12 the
  tutorial hint box, 13-15 mid-fight / explosion / bridge, 16 paused (in-game menu), 17 mission complete
  with results, 18 game over (first frame), 18b game over with its menu, 19 name entry (the touch keys).
- `as2/`: 01 the logo page, 02-04 the intro comic pages (on the bottom screen, top black), 05 main
  menu, 06 options, 07 top scores, 08 information, 09 start game, 10 helicopter selection (model on
  top), 11 the start dialogue with portrait, 12-14 play, 15 paused, 16 the hint panel, 17 the in-game
  menu that opened after the hint's OK was tapped (see problem 10).
- `gulf/`: 01 logo page, 02 main menu, 03 top scores, 04 options, 05 information, 06 credits, 07 start
  game, 08 helicopter selection, 09-10 the level name being typed ("O" alone, then "Operation 1: Recon"),
  11 mid-fight, 12 paused, 13 game over picture, 14 game over menu.
- `desktop/as3d/` (800x600, `--attract 1 --no-logo --bot`): main, options, scores, info, start, three
  play frames; `desktop/as2/`, `desktop/gulf/`: main menus. What the game looks like at its own size.

Not captured, and why:

- **The loading screen**: the loading frames are drawn inside the level start (`ctr_frame.cpp`
  `loadingFrame`), outside the per-frame dump hook; no run.ini key dumps them. Reconstructed in the
  mockup `as3d_loading` from the code (`menu\loading.tga` at 0.4 on both screens, a 3-px bar 8 px
  from the bottom edge). The gallery script needs a `dump_loading=1` key (implementation plan).
- **AirStrike 3D intro pages**: none exist: its `Settings.xml` has its intro image as `ImageTemp`
  (disabled). The sequels' intros were captured with a fresh profile (a profile saved by a `logo=0`
  run keeps `showLogo=0`, which is why `logo=1` showed nothing at first).
- **A weapon picker**: there is none on the bottom screen; the touch band has next-weapon / next-missile
  / next-power-up buttons (captured in every play frame).
- **The system keyboard** (name entry's applet): Azahar's keyboard window cannot be driven or read
  from this machine (P6's finding); the touch keys behind it were captured (`as3d/19`).
- The sequels' mission complete and name entry: the scripted taps on the sequels' end menus landed on
  Restart / the in-game menu (problem 10); their layouts are the shared `SequelScreens` ones seen in
  the other sequel captures. The desktop's `--dual-screen` preview was not captured: the Azahar dumps
  are the real console output.

## 2. Critique

### The ten worst readability problems

1. **Body text is 0.8 mm tall.** Every paragraph, table and secondary label is the desktop's scale-1
   text at 0.4: cap 4 px = 0.77 mm, strokes of 0.4 px blended to grey. Information pages
   (`as3d/04-07`: 80-character lines, 14 lines), the Top Scores table, the mission list, the mission
   results (`as3d/17`), the selector cards ("2 of 24 missions open", "Best score"), the sequels' menu
   labels. Unreadable at 30-40 cm; the owner's phrase "readability" is mostly this.
2. **Touch targets are a quarter of a thumb.** AirStrike 3D main menu items 128 x 14 px (2.7 mm tall);
   Options rows and the Information "page" control 16 virtual = 6.4 px (1.2 mm); name-entry keys 15 x
   9.6 px (2.9 x 1.8 mm); Start Game's helicopter icons about 21 x 21 px; the sequels' buttons 98 x 12 px
   (2.3 mm); BACK 51 x 26 px. The 7 mm minimum (36 px) is met by nothing in the menus.
3. **The in-play bottom screen is 70 % empty, and what is there is small and soft.** Health and score
   in the top 20 px, two life icons, one weapon box, then black down to the 36-px touch band. The HUD art
   (designed 1:1 at 800x600) is drawn at 0.6-0.8 with bilinear filtering: soft edges, uneven pips.
4. **The touch band does not say what it does.** Six circles 24 to 34 px (4.6 to 6.5 mm drawn; the hit
   areas are larger), three of them identical "»" (next weapon, next missile, next power-up), counts in
   4-px digits inside the rings. The same information (weapon, missiles, power-ups) is drawn a second
   time by the HUD boxes above, which are not touchable.
5. **The tutorial hint collides with the level name** (`as3d/12`): both on the bottom screen at the same
   height, "Mission 1 Tutorial" printed through the hint's second line; the hint text is 6 px cap and
   says "press the Ctrl key" (the desktop's keys). The touch band disappears while it is up.
6. **Things are on the wrong screen.** The level name is typed in the middle of the bottom screen at the
   start of a mission (cap 6 px; Gulf shows a lone "O" first, `gulf/09`), where nobody looks. The game
   over screen never shows the score. The sequels' logo pages and comic pages are 4:3 pictures squeezed
   on the bottom screen at 0.4 (speech bubbles unreadable) while the top screen is black (`as2/01-04`).
   Mission results are on the bottom.
7. **The top screen is underused in the menus**: the attract level and, for AirStrike 3D, its banner;
   for the sequels a line of title text, while their real logo sits blurred at 0.37 on the bottom
   screen's top 60 px (`as2/05`, `gulf/02`). Information text, briefings, results, the high score: all
   things to read, all on the smaller screen.
8. **Low contrast over the world.** PAUSED is about 9 px cap, orange, no outline, over a bright desert
   (`as3d/16`); the sequels' score is dark grey digits on a dark frame (cap about 5 px).
9. **Uneven scaling everywhere (looks cheap).** Pictures at 0.37 / 0.4 / 0.6 / 0.8 of their texels with
   bilinear filtering and no mip levels: the AIR STRIKE logo, the menu labels, the HUD frames and the
   loading picture are all slightly blurred and slightly aliased; the long rule of the letterbox
   (`corner.tga`, a 1-texel row) falls between sample rows and vanishes (M2.1). Text is pixel-snapped
   per quad, but the scale itself is fractional, so stems land on half pixels.
10. **A tap that closes a screen falls through.** Tapping the AirStrike 2 hint's OK returned to play and
    opened the in-game menu on the next frame (`as2/17`, log: hint -> playing 1300 -> ingame 1301);
    Gulf's game over Restart tap did the same (playing 950 -> ingame 951). The press ends the menu and
    the same touch reaches the play input as a pause. Interaction bug, to fix with the rework (a
    touch that started in a menu is consumed until released).

### Screen by screen

| screen | today | main issues |
|---|---|---|
| selector (`selector/`) | three 94 x 92 px cards, text 0.4, top screen: title text and keys | card text 0.8 mm; logos at odd scales; top screen holds only text |
| main menu (`as3d/01`, `as2/05`, `gulf/02`) | as3d: label pictures at 0.4-0.5 in the middle 40 % of the screen; sequels: 6 chrome buttons 98 x 12 px | items 2.3-2.7 mm tall, half the screen black; version / copyright 0.4 |
| options (`as3d/02`) | 5 rows of 16 virtual px, sliders 40 px long | rows 1.2 mm, labels 0.8 mm, the top half empty |
| top scores (`as3d/03`) | 15-row table at 0.4 | unreadable; would fit the top screen at S size |
| information (`as3d/04-07`) | 80-char lines at 0.4, "Page <1 of 10>" control 6 px tall | unreadable; page control untouchable |
| exit dialog (`as3d/08`) | question at 0.4, YES / NO pictures | question unreadable, targets 38 x 26 px |
| start game (`as3d/09`, `as2/09`, `gulf/07`) | mission list rows 6.4 px, helicopter grid 2x5 of 21 px | list and grid untouchable; the chosen helicopter is shown nowhere big (as3d) |
| helicopter select (sequels, `as2/10`) | model on the top screen at 0.8, panel on the bottom | good: the model on top works; the bottom text is 0.4 |
| start dialogue (`as2/11`) | portrait and text on the bottom at 0.4 | text unreadable; belongs on top |
| in play (`as3d/10-15`, `gulf/11`) | HUD top band + touch band | problems 3 and 4 |
| hint (`as3d/12`, `as2/16`) | bottom screen, 6-px cap (as3d) / 1.5 panel (sequels) | problem 5; keyboard key names |
| paused (`as3d/16`) | PAUSED on top, menu on the bottom (pictures 14 px tall) | PAUSED contrast; items small |
| mission complete (`as3d/17`) | title picture, results at 0.4, heli grid, 3 buttons 14 px tall | results unreadable; buttons small; the world behind on top unused |
| game over (`as3d/18`, `18b`, `gulf/13-14`) | as3d: GAME OVER text alone for seconds, then RESTART / QUIT; Gulf: its picture | no score; the menu appears late without a cue |
| name entry (`as3d/19`) | keyboard 10 x 4 keys of 15 x 10 px | keys 1.8 mm tall (the applet is the main path; this is its fallback) |
| loading (not dumped) | loading picture at 0.4, 3-px bar | soft picture, invisible bar, both screens the same |

Between screens and games: the menu labels are pictures in AirStrike 3D and font text in the sequels;
the BACK button is in a different place on each screen family (as3d 50,450; sequels 30-50,520); the
first game's HUD scales differ from the sequels' (2 / 1.5 vs 1.5 / 1.1). What already works: the
sequels' model view on the top screen, the attract level with the 3D banner, the HUD's own colours.

## 3. The proposal

### In one paragraph

Draw the 3DS screens in **their own pixels**, not as a 0.4 copy of 800x600: a 320 x 240 (bottom) and
400 x 240 (top) layout per screen, every picture at 1:1 or exactly 1/2 of its texels (the texture's mip
level 1) on whole pixels, and the game's **own font at three fixed sizes drawn 1 texel : 1 px**: S (0.8,
resampled once when the font loads; cap 8 px = 1.5 mm) for any running text, L (the native texels; cap
10 px = 1.9 mm) for items, labels and numbers, T (2x nearest; cap 20 px = 3.9 mm) for banners. **Text to
read goes on the top screen** (Information pages, results, high score, hints, dialogues, the level name,
PAUSED) over the dimmed world or attract level; **things to touch go on the bottom screen**, as
full-width bars and buttons of at least 36 px (7 mm), with the pad hints (A / B / L / R discs) on a
footer row. In play the bottom screen becomes the HUD drawn 1:1 whose **weapon, missile and power-up
boxes are the touch controls** (tap = select, tap again = use; 70 x 39 px each), replacing the band of
circles; the top screen carries only the level name (3 s), the hint box and PAUSED. Each game keeps its
art, colours and font: AirStrike 3D red/rust and its label pictures, AirStrike 2 grey chrome and green,
Gulf Thunder green scan lines and chrome; the sequels' logos move to the top screen.

### Typography (`mockups/typography_candidates`)

| size | source | px/texel | cap | mm | use |
|---|---|---|---|---|---|
| S | font.tga resampled at load (Lanczos 0.8 + contrast), its own 256x256 LA8 atlas, drawn 1:1 | 0.8 | 8 px | 1.54 | paragraphs, tables, footers, help lines |
| L | font.tga as is, drawn 1:1, nearest | 1.0 | 10 px | 1.92 | items, labels, values, HUD numbers |
| T | font.tga 2x nearest | 2.0 | 20 px | 3.85 | PAUSED, level name, selector title |

Line pitch S 13 px, L 16 px. On the top screen S gives about 48 characters a line (376 px), L about 36.
Over the world: a 1-px black outline (drawn as a pass before the glyphs) or a 60-80 % dark panel.
No size below S anywhere. The engine's additive text blend stays (it reads on black and on the dark
panels).

Why not the alternatives: rescaling the atlas on the GPU at 0.5-0.75 (`0.5 nearest` in the sheet) drops
or doubles strokes; a larger re-raster needs a vector source the game does not have (font.tga is a
bitmap with 1-texel strokes; a look-alike vector face such as an OFL "Microgramma" clone would not be
the game's own glyphs). **Alternative A (font, `mockups/alt_font_x11`)**: a public-domain bitmap font
(X11 misc-fixed 7x14 bold, cap 9 px, 2-px stems, a 4 KB atlas) for S only: sturdier and denser, but a
terminal look foreign to the game's extended square face. Recommended: the game's font; switch S to
the X11 face only if the console photo shows 0.8 too thin.

### Bottom screen in menus

- Items: full-width bars (248 to 311 px wide) **36 px tall at a 40-px pitch** (34/38 when six items are
  needed), label centred in L or the game's label picture at 1/2; focus = brighter bar + edge + markers.
- Lists (missions, scores when interactive): rows 30 px with a 22-px scroll bar whose arrows are
  targets; 4 rows visible.
- Spinners: value between two 36 x 32 px arrow targets; sliders 134 px long, draggable.
- Corners: BACK bottom-left (80 x 32), the screen's forward action bottom-right (START, PLAY, OK),
  under the right thumb; secondary corner buttons (GAMES) with hit areas extended to the edge.
- Footer: pad hints in S with button discs (A Select, B Back, L / R pages).
- Titles leave the bottom screen: the title picture sits on the top screen's letterbox band.

### Top screen in menus

The attract level stays (and AirStrike 3D's 3D banner on its band); the sequels' logo pictures move
there at 1/2 (`as2_main_menu`); a bottom band carries the version and copyright (S). When a screen has
something to read, the top shows it over the attract level dimmed to 20-45 %: the Information page
(`as3d_information`), the chosen mission and helicopter (`as3d_start_game`, the helicopter picture at
1:1), a help line for the focused option (`as3d_options`). **Alternative B (`alt_top_static`)**: a
static hero panel (banner, pictures, help line) on a dark gradient, no attract level: saves the attract
level's load at every return to the menus (5.6 s on Azahar) and its VRAM, but loses the live showcase.
Recommended: the attract level.

### In play

- Bottom (`as3d_in_play`, `gulf_in_play`): pause (top-left, hit 40 x 36), health frame and fill 1:1
  (180 x 21 px), score in L top-right, lives at 1/2; the weapon box (left; tap = next weapon); the
  power-up column (middle) and the missile column (right, under the right thumb when it leaves A/B):
  the HUD's own 70 x 39 boxes at 1:1 with their icons, empty kinds dimmed, counts in L on a black patch,
  the selected box double-framed. Tap a box = select that kind; tap the selected one = use it. A pad
  reminder in S fills the lower left. The circle band and its 8k vertices go. Sequels: the same grid
  with their frames at 3/4 (resampled at load) and only the owned power-up kinds (up to 10) in a
  scrolling column.
- Top: nothing during normal play. The level name for 3 s at the start (S + T, outlined,
  `as3d_level_start`); the hint box over the paused world dimmed 35 % (L title, S text, "A OK";
  `as3d_hint`) while the bottom shows one 160 x 56 px OK and the dimmed HUD; PAUSED in T outlined over
  the world dimmed 45 % (`as3d_paused`). All at screen depth (zero parallax), as today.

**Score width (owner's concern, 2026-10-11):** the in-play mockup sets the score right of the
health bar with no reserved width, so a growing score would run into the bar. Rule for the in-play
package: the score is right-aligned in a field reserved for the widest score the engine budgets
(10 digits: `hudElementRects` in engine/src/ui/hud_layouts.cpp counts a pen advance of 14 per digit
at the desktop scale; at the console's L size that is 10 digits x the L digit advance, about 80 px,
plus an 8 px gap), the field ends 8 px from the screen's right edge, and the health bar takes
whatever is left between the pause button and the field (about 140 px on a 320 px screen) instead
of a fixed 160 px. `hud_layout_test` checks that the 10-digit score rectangle and the bar rectangle
do not overlap at the console layout. The same rule for the sequels' score boxes.

### End screens, loading, name, selector

- Mission complete (`as3d_mission_complete`): title picture and results (L) on top over the dimmed
  world; the helicopter grid at 50 x 50 px and RESTART / QUIT / CONTINUE 100 x 40 on the bottom.
- Game over and name (`as3d_game_over_name`): GAME OVER picture 1:1, the score and rank on top; the
  system keyboard stays the main path; its fallback grid is 8 x 5 keys of 36 x 30 px, D-pad navigable.
- Loading (`as3d_loading`): the loading picture 1:1 and the mission name with a 6-px bar on top; the
  console's controls card in L on the bottom (the original's controls page names keyboard keys).
- Selector (`selector`): the focused game's logo, name (T), version, progress and best score on top;
  three 101 x 166 px cards and EXIT / PLAY 92 x 38 on the bottom.

### Per game

| | AirStrike 3D | AirStrike 2 | Gulf Thunder |
|---|---|---|---|
| ground | black | black | green scan lines (its own) |
| buttons | dark red bars, rust edge, label pictures at 1/2 | grey chrome bevel, green L text | grey chrome on green, white-green L text |
| top in menus | attract + rotating banner | attract + AIR STRIKE logo at 1/2 | attract + Gulf Thunder logo at 1/4-1/2 |
| HUD art | mainbar.tga 1:1 | mainbar2 / interface at 3/4 | mainbar2 / interface_gulf at 3/4 |
| intro / comics | none | logo and comic pages on the TOP screen (4:3 picture at 0.75-1:1, bubbles readable), Skip / Next on the bottom | logo page on top |
| fonts | each game's own font.tga (same metrics) | | |

Mockups (each `.png` 1x, `@2x.png`, and `_notes@2x.png` with the sizes in px and mm), in
`out/ui3ds/mockups/`: `typography_candidates`, `as3d_main_menu`, `as3d_options`, `as3d_information`,
`as3d_start_game`, `as3d_in_play`, `as3d_level_start`, `as3d_hint`, `as3d_paused`,
`as3d_mission_complete`, `as3d_game_over_name`, `as3d_loading`, `selector`, `as2_main_menu`,
`gulf_in_play`, and the two alternatives `alt_font_x11`, `alt_top_static`. They are PIL composites of the
real dumps (top screens) and the games' own art drawn with the rules above; the S size there is the same
offline resample the engine would do.

## Owner decisions (2026-10-11)

- **Font: the game's own font** at the three fixed sizes (alternative A, the X11 pixel font, is dropped).
- **Top screen in menus: the live attract level** (alternative B, the static panel, is dropped).
- **Controller-button sprites: candidate 4, "HUD plate"** (the owner's pick, 2026-10-11, from
  out/ui3ds/buttons/candidates_4x.png): bevelled squares with the bottom-right corner notched like
  the HUD's weapon and pause boxes, dark fill, orange frame, gold letter; START and SELECT as pills,
  the D-pad as a small cross. The glyph sheet and its per-key rectangles are out/ui3ds/buttons/set_4.png
  and set_4.json (15 px high, drawn by us, not game data: it may be committed into the app's own
  assets by the menus package). The letters use a 5x7 pixel font because the game's wide font does
  not fit a 15 px badge; if they read too thin on hardware, thicken the strokes by one pixel.
- **One size per group of labels** (the owner on the R4.5 gallery, 2026-10-11: "mission selection has
  inconsistent font size"; implemented by R4.7): the foundation's step-down shrank each line on its
  own, so "Mission 1: Tutorial" stayed at L while "Mission 2: Leaving the Home" dropped to S in the
  same list. Rule: the labels of a group share one size, the largest at which the group's longest
  label fits its room; a label that does not fit even at S is cut with "..." instead of shrinking
  further. Groups: the rows of one list (every entry, not only the visible rows, so scrolling never
  changes the size), the labelled boxes of the same kind and the same size on a screen (a column of
  bars, a row of buttons, the option rows), all the spinner values of a screen (every value of
  every spinner), the lines of an Information sheet and of the hint box (S throughout). The
  keyboard's keys are key caps, not a list: each is sized alone. Code: `consoleGroupSize`,
  `fitLabel` and `applySizeRule` in engine/src/ui/console_menu.cpp; test "console menus: one size
  per group of labels" (apps/tests/console_menu_test.cpp).

## 4. Implementation plan (next package)

Principle: the 3DS gets **console layouts** in pixel units; the desktop, Android and web paths and their
goldens do not change (the new layouts are 3DS targets; the desktop sees them only with
`--dual-screen`).

1. **Foundation (2D layer and font).** `engine/src/ui/pica/renderer2d_pica.cpp`: a pixel mapping
   (`begin(320, 240)` with scale 1, no 0.4 `computeMapping`), nearest filtering per texture class
   (Font, HUD atlases, menu pictures), sampling of mip level 1 for 1/2 pictures (or a half-size copy
   made at load). `engine/src/ui/font.cpp`, `as3d/ui.h` `FontMetrics`: the three sizes S/L/T with
   integer pen advance per size; the S atlas built from font.tga at load (`UiAssets::load`, 256x256 LA8,
   128 KB, VRAM). The outline pass in `drawText`. Desktop test: glyph advances and the S atlas
   checksum from the test font.
2. **Screen gallery** (before any screen changes, so every iteration shows the owner pictures):
   `tools/3ds_ui_gallery.sh` + `.py`: runs the run.ini scripts of this package (selector, the three
   front ends, missions with the bot, hint, pause, complete, game over, name) through the serialised
   Azahar recipe, converts the dumps, writes `out/ui3ds/gallery/<commit>/` with 1x / 2x and one contact
   sheet; a `dump_loading=1` key (`ctr_frame.cpp` `loadingFrame`) for the loading screen; a fresh-profile
   option for the intros. Taps in the scripts read `AS3D_MENU` positions, so they survive layout moves.
   The same screens on the desktop with `as3d_game --dual-screen --headless --ui-script` (fast check).
3. **Menus (AirStrike 3D first).** A console menu view (`engine/src/ui/console_menu.{h,cpp}`, used by
   `apps/3ds/source/ctr_frame.cpp` instead of `Frontend::drawUnder/drawOver` at 0.4): it draws the
   `Frontend`'s menu model (items, kinds, focus, values) with per-screen layouts (main, options,
   information, scores, start, ingame, exit, complete, gameover, name) and writes each item's hit
   rectangle back so the existing input (`CtrInput::uiInputs`, pad mode) is unchanged. The texts:
   short labels per key, re-flow of the Information pages, platform key names in hints ("Ctrl" -> A).
   Top-screen panels (`ctr_frame.cpp` top 2D list): titles, Information text, start-game context,
   results, high score.
4. **In play.** `engine/src/ui/hud_layouts.cpp`: `makeV170Bottom` / `makeSequelBottom` rewritten in
   pixel units with 1:1 art and the inventory columns; `hudElementRects` gives the touch rectangles.
   `engine/src/input/touch_mapper.cpp`: a `TouchLayoutOptions::hudTiles` layout (rectangles from the HUD
   instead of `bottomBandLayout`'s circles; tap = select, tap on the selected = use; pause corner);
   `touch_controls.cpp` no longer drawn on the 3DS. The hint (`drawHint`), the level name, PAUSED on the
   top list with the outline. The touch-through fix (problem 10): a touch that began on a menu is
   consumed until release (`ctr_input.cpp`).
5. **Sequels, selector, loading, name entry.** The same views with the sequels' skins (`as2_widgets`
   pieces at 3/4, logos on top, comics on top), the selector (`ui::GameSelector` console layout,
   `ctr_selector.cpp`), the loading frame (`ctr_frame.cpp loadingFrame`: picture 1:1 on top, controls
   card on the bottom), the name-entry fallback grid.
6. **One console photo session at the end** (the owner): main menu, Information, in play, hint, paused,
   name entry, the selector, at arm's length in mono and stereo; the questions: S size thick enough
   (else alternative A), tiles under the thumb, top-screen text comfortable in stereo.

What Azahar / the desktop verify: every layout, size and overlap (the gallery), pad and touch paths
(scripted taps on the `AS3D_MENU` rectangles), the menus' end states (`game_flow_test`), the HUD
rectangles (`hud_layout_test`), the bot's mission end states unchanged. What needs the console: the
perceived size and thinness of S and L, the stereo comfort of top-screen text, the thumb reach, the GPU
time of the bottom screen.

Risks:
- **The sequels' front ends** draw much of their own (`SequelScreens`, panels, dialogues with
  portraits, comics, the helicopter panel): the generic view covers the menus, the dialogue and comics
  need their own top-screen layouts; largest uncertainty of the plan.
- **The system keyboard** stays as it is (applet); only its fallback changes; still unverified on the
  console (P6).
- **Memory**: the S atlas 128 KB VRAM per game (VRAM is full from the terrain on: it would go to linear
  memory if loaded late; load it with the font, before the terrain); half-size copies of menu pictures
  only if mip level 1 cannot be sampled directly (about 0.2 MB in the menus, none in play).
- **GPU of the bottom screen** (1.4-2 ms today): the tiles are quads (a few hundred vertices) instead of
  the 8k-vertex circle band, so it should drop; top-screen text adds two flushes of a few hundred quads
  in stereo. Measured by the next soak's `AS3D_PASS_MS`.
- Scripts and docs that tap fixed bottom-screen pixels (`pad_script=...T160/107`) break; the soak uses
  the direct bot modes and is unaffected.

Size: steps 1-2 about 800 lines (with the gallery and tests), step 3 about 1500, step 4 about 1000,
step 5 about 1200: **about 4500 lines in four packages** (foundation + gallery; AirStrike 3D menus;
in play; sequels and the rest), each ending with a gallery for the owner, one console photo session at
the end.

## UI foundation and the screen gallery (round 4, R4.4)

Steps 1 and 2 of the plan (section 4). No layout moved; only the text sizes changed, and only on the
3DS. The desktop, Android and the web are untouched: everything below is off on the GL backend
(`UiAssets::consoleTextDefault()` is false there), and `tools/ci.sh` passes with the goldens unchanged.

### What the foundation does

- **Text in console pixels at three sizes** (`as3d/ui.h` `FontSize`, `engine/src/ui/font.cpp`). A
  console font (`Font::consoleSizes`, set by `UiAssets::uiFont()` when `UiAssets::consoleText` is on)
  draws the game's font one atlas texel per screen pixel: **S** (font.tga resampled once at load to
  exactly 0.8: each glyph's 30 x 15 texels area-averaged onto 24 x 12, contrast x 1.25 so a one-texel
  stroke keeps full intensity; its own 256 x 256 LA8 atlas, cap 8 px), **L** (font.tga as is, cap
  10 px), **T** (font.tga at 2x nearest, cap 20 px). Pen advances are whole pixels per size
  (`glyphAdvancePx`: L the table's, T twice it, S the table's x 0.8 rounded; the number routine 11 /
  14 / 28 px). `TextStyle::size` chooses one; `Auto` (every existing call) takes the size nearest the
  text's scale on screen (`consoleSizeFor`: below 0.7 px per texel S, below 1.1 L, else T): today's
  body text at 0.4 and the HUD counts at 0.6 become S, items at 0.8 L, PAUSED and the titles at 1.2 T.
  The text keeps its anchor (left, centre or right edge) and the vertical centre of its glyph box; an
  `Auto` line that would leave the screen steps down (T to L to S), and a line still too wide at S is
  cut at the screen edge. One LA8 atlas serves both kinds (RGB for the additive font, alpha for the
  alpha font and shadows). Run.ini `ui_text=0` gives back the scaled, bilinear text (comparisons).
- **Outline pass** (`TextStyle::effect`): `Outline` draws the glyphs of the alpha font in black at the
  eight one-pixel offsets under the text, `Shadow` once at (+1, +1). `Auto` outlines additive T text
  (the banners over the world: PAUSED, the titles) and nothing else.
- **Nearest filtering per texture class** (`UiAssets::load`): the font textures and the S atlas are
  nearest-filtered (they are drawn 1:1 or 2x); the HUD atlases and the menu pictures stay bilinear
  while they are drawn at fractional scales (0.4 to 0.8): the layout packages switch them as they draw
  them 1:1 or 1/2.
- **Console pixel mapping**: `Renderer2D::beginPixels(w, h)` (and `pixelMapping`) lays quads out in
  the target's own pixels, scale 1. The existing screens keep `begin()` and the 0.4 mapping of the
  800 x 600 virtual screen until their layouts are redone; the new console layouts start from
  `beginPixels(320, 240)` / `(400, 240)`.
- **Half-size pictures**: `UiAssets::textureHalf(path)` makes, on first use, a copy at exactly 1/2
  (2 x 2 blocks averaged with the colour weighted by alpha, so transparent texels do not darken the
  edges), padded with transparent texels to power-of-two sides and nearest-filtered;
  `drawHalfPicture` draws it 1:1. Chosen over sampling mip level 1: the PICA picks the level from
  screen derivatives, and at exactly 1/2 the LOD is exactly 1.0, where it can land on level 0 (no LOD
  clamp without changing the render module); a mip chain costs a third more for every picture; and the
  render module stretches a non-power-of-two picture to power-of-two sides (nearest) before mipping,
  which breaks 1:1. The copy is exact and costs memory only for the pictures a layout asks for
  (about 0.2 MB for the first game's menus, none in play).
- **Key hints** (`drawKeyHint`): a key from a swappable atlas (`KeyGlyphAtlas`: one small texture and
  a rectangle per `HintKey`; no art built in), or, without one, the key's name in the font inside a
  one-pixel frame, then the label. The owner's chosen set (candidate 4, "HUD plate") is wired by the
  menus package, the first one to draw hints.

Memory: the S atlas is LA8 256 x 256 = 128 KB, built in `UiAssets::load` right after font.tga, so
with the font and before the level's terrain. Logged on every load:

    AS3D_UI_FONT console=1 font=256x256_LA8_128KB_vram alpha=256x256_LA8_128KB_vram small=256x256_LA8_128KB_vram vram_used_kb=648

(all 12 loads of the gallery runs: the three font textures in VRAM, 647 to 968 KB of VRAM in use at
that point). It moves 128 KB of model skins of the level from VRAM to linear memory.

Desktop tests (`apps/tests/ui_console_text_test.cpp`, synthetic font only): the sizes and advances,
the S atlas from a synthetic font (no ink outside the glyph cells, RGB = alpha, a one-texel stroke at
full intensity, FNV-1a checksum), the quads on whole pixels at the 0.4 mapping and their sizes, the
step-down of a too-wide line, the scaled path for a desktop font, the number routine, the outline
rendered on a synthetic glyph (headless GL: the stroke white, the columns either side black, two
away the background), half-size pictures, key hints.

### The screen gallery

    tools/3ds_ui_gallery.sh [BINARY.3dsx] [LABEL] [--only run,...] [--tag NAME] [--convert-only]

Default binary `build-3ds/apps/3ds/openairstrike.3dsx`, default label the current commit. It runs
seven run.ini scripts on Azahar (`selector`; `as3d_menus`: main, options, scores, four Information
pages, exit, start, loading, the mission with the bot: level name, early, paused, hint, mid-fight,
mission complete; `as3d_gameover`: game over, its menu, name entry; `as2_intro` and `gulf_intro` on a
fresh profile; `as2_menus` and `gulf_menus` through helicopter selection, the start dialogue, play,
pause, hint, mission complete or game over), one at a time, each through the serialised recipe (waits
until no Azahar runs, checks twice; installs `<tag>.3dsx` and the run.ini; saves and restores the SD's
run.ini, unless another run replaced it meanwhile, and the profiles a fresh run deletes; kills only
its own launcher and emulator; a run whose log is someone else's is run again). It writes
`out/ui3ds/gallery/<label>/<game>/<NN>_<screen>_{top,bottom,both}.png` and `@2x` (nearest), and
`out/ui3ds/gallery/<label>/<game>_sheet.png` (+ `@2x`), the raw dumps and logs under `raw/`.
The whole set takes about 9 minutes (509 s for `52e1544`) and leaves no Azahar process.

New run.ini keys (apps/3ds/README.md): `dump_loading=1` (the loading frames, a few lines in
`ctr_frame.cpp` `loadingFrame`), `ui_taps=F:@SCREEN/ID` (a tap at the centre of a menu item wherever
the layout put it: the scripts survive layout moves), `hint_hold=N` (the bot leaves a hint up N steps
so it can be dumped), `ui_text=0|1`. A binary without `ui_taps` (the drops) gets pixel taps from the
item rectangles those builds log (`LEGACY_RECTS` in the script), and has no loading or hint pictures.

The desktop fast path was not added: `as3d_game --dual-screen --headless --ui-script` exists, but its
front end is not the console's (the 3D banner on the bottom screen, GL bilinear filtering, the scaled
text, one ui-script per screen to write); the Azahar set is the real output and fits in 9 minutes.

### Before and after

`out/ui3ds/gallery/before/` (the drop `d63fbbf`, unchanged) and `out/ui3ds/gallery/52e1544/` (this
package), one sheet per game (`selector`, `as3d`, `as2`, `gulf`).

| screen | before | after |
|---|---|---|
| selector, top | title 1.2 px/texel bilinear, version and key line 0.4-0.6 | title T outlined (Gulf Thunder's long title stepped down to L), version and key line S: crisp |
| selector, cards | card text 0.4 (cap 4 px) | S; the card text is now wider than the 94-px cards and overlaps the next card: the selector layout (menus package) |
| main menus | as3d: label pictures (unchanged), version line 0.4; sequels: button labels 0.4 | sequels' button labels S inside their 12-px buttons, readable; the version/copyright line S, full width |
| options | labels 0.4 | S, crisp; on the sequels the stacked rows (Sound / Music volume) touch each other |
| top scores | 15 rows at 0.4 | S: the columns overlap (name, score, rank were laid out for 0.4) and rows touch: the table needs its own layout |
| Information pages | 80-character lines at 0.4, 7.2-px pitch | titles and the page control crisp at S/L; the paragraphs overlap (S needs a 13-px pitch) and are cut at the right edge: the re-flow of the menus package |
| exit dialog | question 0.4 | S, readable |
| start game | mission list 0.4 | S; list rows overlap, "Difficulty" runs into the list (as3d) |
| loading | not dumped | dumped now; the picture unchanged; the sequels' "Loading" sits on its bar |
| in play, HUD | score L-ish 0.8 bilinear, counts 0.6, level name 0.6 | score L, counts and level name S, crisp; the touch-band counts S in the rings |
| PAUSED | 1.2 bilinear, no outline, over the world | T with the one-pixel outline: readable over the desert and the water |
| hint box | not dumped (bot) / 0.6 text | S; the first game's lines are cut at the box's edges (the box was sized for 0.6) |
| start dialogue (sequels) | portrait and text 0.4 | S, the line cut at the panel ("Listen t"): the dialogue's top-screen layout |
| mission complete | results 0.4 | S; label and value columns overlap ("Enemies destroyed" / count) |
| game over, name entry | 0.4 / key labels 0.4 | S; the touch keys' letters run together (keys 15 px wide) |
| credits (Gulf) | 0.4 | S, readable; headings and names touch |

In short: every glyph is now drawn at a whole size on whole pixels (no more grey half-pixel strokes),
headings, buttons, PAUSED and single lines read well; anything laid out as dense 0.4 text (paragraphs,
tables, lists, cards, results) overlaps or is cut, as expected without moving layouts.

### What the menus package starts from

- Lay the console screens out with `beginPixels` in pixels, text by `FontSize` (S body, line pitch
  13; L items and values, pitch 16; T banners) and `measureTextPx` for widths; the `Auto` step-down
  and edge cut then never trigger.
- Pictures at 1/2 through `textureHalf` + `drawHalfPicture`; switch a picture's class to nearest only
  where it is drawn 1:1 or 1/2.
- Key hints through `drawKeyHint` with the owner's set 4 loaded as a `KeyGlyphAtlas`.
- Text over the world: `TextEffect::Outline` (or `Auto` for T).
- Check every iteration with `tools/3ds_ui_gallery.sh` (scripts tap by item id, so they follow the
  new layouts; a new screen needs a `keep` entry in `RUNS`).
- Seen in the gallery and not addressed here: a tap on the in-game menu's Resume falls through to a
  new pause on the drop (problem 10; the gallery resumes with B); with `ui_text=1` the sequels' hint
  panel and Gulf's credits are readable as they are.

## AirStrike 3D menus on the console (round 4, R4.5)

Step 3 of the plan (section 4): the first game's menus in the 3DS screens' own pixels. The desktop,
Android and the web are untouched (the view is used only by apps/3ds; `tools/ci.sh` passes with the
goldens unchanged). Run.ini `ui_menus=0` gives back the 0.4 copy of the desktop's screens.

### The view

`engine/include/as3d/console_menu.h`, `engine/src/ui/console_menu.cpp` (`ui::ConsoleMenuView`). The
model stays the `Frontend`'s: its menus, items, focus, values, callbacks and the pad navigation of
M2.1 (`MenuSystem::padMode`). The view:

- **lays the top menu out** per screen (`ConsoleMenuView::layout` -> `ConsoleLayout`): boxes in
  bottom-screen pixels, each a `Bar` (an item: drawn, a tap clicks it) or a sub-target of its item
  (`SpinPrev` / `SpinNext`, `SliderTrack`, `ListRow`, `ListUp` / `ListDown`, `GridCell`) or a `Key`
  (a pad key without an item: Information's PREV / NEXT); a footer of pad hints (`ConsoleHint`:
  the owner's key pictures, S labels); a bottom title and sub-title. A screen's layout is a small
  function of the menu (`layoutMain`, `layoutOptions`, ...): which items go in the column, which in
  the corners, which hints. Layout constants: margin 4, bars 36 high, the bottom row
  (`kFooterTop` 204 .. 240) for BACK / corner buttons and the footer.
- **writes each item's hit rectangle** (`applyHits`): the item's bar in virtual units (2.5 x the
  pixels: the bottom screen's 0.4 mapping inverted), an empty rectangle for an item without a bar.
  So the menu system's own pointer path, the `AS3D_MENU` log and `ui_taps` follow the layout
  (Information's page spinner gets the NEXT button). Called by the menu loop before and after
  `GameFlow::uiFrame` and by the frame composer before drawing.
- **turns the touch into the menu's input** (`touchInput`, called by `CtrInput::uiInputs` when the
  view handles the screen): a bar = a pointer move to the item and Mouse1 (a click; spinners and the
  start screen's helicopter row: the next value; sliders, lists, grids: the focus only); an arrow =
  the pointer on the item + Left / Right (`padMode` turns them into the widget's step); the slider's
  track = as many Left / Right as the steps to the value under the finger, again on every move
  while the finger is down; a list row / a helicopter cell = the steps of `padKey`'s walk to it
  (locked ones: nothing); a `Key` box = that key. No widget's own pixel geometry (the desktop's
  20-px list rows, 72-px grid cells, 128-px slider) is involved: the 3DS layouts are free of it.
- **pages Information in sheets** (`filterKeys`): a page longer than the top screen is read in
  sheets; Left / Right / L / R / PREV / NEXT step through the sheets first, then the pages (a page
  entered backwards opens at its last sheet).
- **draws** the bottom screen (`drawBottom`) and the top screen's reading panel (`drawTop`), in
  pixels through the list's mapping (the 3DS lists keep `begin()` and its 0.4 mapping; the view
  converts its pixel rectangles, so the same code draws into `beginPixels` lists).

Input (`apps/3ds/source/ctr_input.cpp`): L / R are PageUp / PageDown in the menus (Information's
pages; the mission list's page). The tutorial hints' key names are the console's buttons
(`consoleKeyNames` through `Frontend::setHintFilter`): {Ctrl} A, {Shift} B, {Space} X, {Z} Y,
{X} R, "key" -> "button" after them (the default pad bindings).

The key pictures: the owner's set 4 ("HUD plate", original art of this project) is
`apps/3ds/assets/keys.png`, embedded as `apps/3ds/assets/keys.inc` (a 70-colour palette and an
index per pixel, `tools/3ds_key_glyphs.py`; the engine decodes no PNG and `*.tga` is never
committed), made into a 256 x 32 nearest texture by `apps/3ds/source/key_glyphs.cpp`; the
rectangles are `consoleKeyRect` (set_4.json). Without it the hints draw the keys' names in a frame.
Since 2026-10-17 (the owner: "controls icons for all the buttons") the sheet has a second row,
composed from the first row's plates in the same style: **ZL** and **ZR** on the shoulder plate, the
**circle pad** (a ring and its nub) and the **touch screen** (a stylus), `HintKey::CPad` and
`HintKey::Touch` added (names "PAD" and "TOUCH" without the pictures). The controls card and the
footers now draw every key as a picture (before: "Circle Pad", "ZR" and "touch" as words).

**The comic pages read at 1:1 (2026-10-18, the owner: "the comic text is not readable, too small").**
AirStrike 2's four intro pages were fitted whole to the top screen (1/2 to 0.4): the speech bubbles of
pages 3 and 4 came out at four pixels of lettering. `SequelScreens::comicWindow(page, t, w, h, x, y, zoom)`
(as2_comics.cpp) is a reading camera over the page's 800 x 600 space: keyframes of centre and zoom,
eased between them (the owner's second note the same day: "display the scaled down version before
zooming on text"). Pages 1 and 2 open on the whole panorama (1/2) and move in slowly to 0.8; page 3
opens on the whole control room (0.4, the page centred with black sides), zooms to 1:1 on the first
bubble as it appears (5.5 s), back out to the room at 10 s, 1:1 on the second bubble from 13 s, out at
the end; page 4 opens on the whole cockpit (0.47) and zooms to 1:1 on both bubbles from 4 s. At 1:1
the window lands on whole pixels so the lettering stays crisp. `drawIntroTop` maps the page with the
zoom as the scale and the window's offset (the logo pages stay at 1/2). The loading, game-over and
campaign-end comics carry no lettering and stay at 1/2. Test "console menus: the comic pages' reading
window stays on the page and reaches the bubbles".

### The tap-through fix (problem 10)

`CtrInput::uiInputs` makes a finger on the screen while a menu is up the menu's until it is
lifted: the touch mapper forgets it at once (`releaseAll` and a `takeFrame` that drops the latched
pause / confirm / buttons) and `readPad` keeps it from the touch buttons until the release. Before,
`readPad` fed the down edge to the touch mapper before the menu saw it; with the in-game menu up
(the world paused) a tap anywhere latched a pause, the menu closed on the same tap (Resume, a hint's
OK, Restart) and the first play step took the latched pause: the in-game menu again on the next
frame. With the console menus, run.ini `ui_taps` are real fingers (`CtrInput::queueTouch`, from the
next frame, held 6 frames), so the gallery taps go this path; the gallery logs a "tap-through"
check for every tap that returns to play (`tap_through` in tools/3ds_ui_gallery.py).

### Layouts (pixels; bottom 320 x 240, top 400 x 240)

| screen | bottom | top |
|---|---|---|
| main menu | five bars 240 x 36 at x 40, y 4 + 40 i (the game's mmenu_1 label pictures at 1/2, mmenu_2 glowing on the focused one, markers at the bar's ends); GAMES 84 x 36 at (4, 204) with the selector; footer A Select, D-pad Move | the attract level and the 3D banner (unchanged); a 16-px band at the bottom: version and copyright in S |
| options | the console's five rows 312 x 36 at y 4 + 38 i (Brightness, Sound, Music, Camera, Show FPS, short L labels), sliders' track x 168 .. 304 with a 9 x 22 knob (touch zone 160 .. 312), spinners' arrows 36 x 36 at the label column's end and at x 280, values in L (S when wider); BACK 80 x 36 at (4, 204); footer D-pad Change, B Back | the optionsh header at 1/2 on a 34-px black band; a help band at y 196: what the focused row does and how (S) |
| information | title L, the page's title S; PREV / NEXT 148 x 56 at y 48; ten page dots at y 118; BACK; footer L Prev, R Next, B Back | the page re-flowed: title L, counter "n/10 (sheet/sheets)" S, text S at a 13-px pitch from y 28 to 236 over the attract level dimmed to 28 %, paragraphs 5 px apart, item headings on their own line with their icon (33 x 18) in a 44-px indent, credits centred |
| top scores | the topscores picture at 1/2, the best pilot (L); BACK; footer B Back | the table: 15 rows at S, 13-px pitch, rank right-aligned at 34, name at 44, score right-aligned at 272, rank name at 284 |
| exit | EXIT (T), YES / NO 138 x 56 at y 84 (the yesno pictures at 1/2); footer A Select, B Back | the question in L on a dark band |
| start game | the mission list: three rows 272 x 36 (L, S when wider), the scroll arrows 36 x 36 and the position between them at x 280; Difficulty and Helicopter as spinner rows 312 x 36 at y 122, 160; BACK (4, 204) and START 100 x 36 at (216, 204) under the right thumb; footer A OK | the starth header; the chosen helicopter 1:1 (85 x 85) framed; mission name L; difficulty, helicopter n of N, helicopters open (S) |
| in-game menu | Resume / Options / Quit bars 240 x 36 at y 40 + 44 i; footer A Select, B Resume | PAUSED (the host's, T outlined, unchanged) |
| mission complete | the helicopter grid: ten cells 50 x 50 at a 54 pitch (icons at 1/2, locked dark, the chosen one framed); RESTART 108, QUIT 84, CONTINUE 112 x 44 at y 152 (pictures at 1/2, cropped to the bar); footer A Select, D-pad Move | MISSION COMPLETE (miscompl at 1/2, pulsing) over the world dimmed 35 %; the results in L: enemies destroyed, stars, rank, score (the score Continue banks) |
| game over | GAME OVER (T); after 2 s RESTART / QUIT 138 x 44 at y 128; footer A Select | the gameover picture 1:1 (256 x 64) pulsing; the score (the one Quit banks) in L and, when it enters the table, "NEW HIGH SCORE", its place and rank in S |
| name entry | title L; the field 248 x 32 and DEL 60 x 32 at y 22; the 40 keys 29 x 30 at a 31 x 32 pitch from y 58 (L letters, SP in S); OK 312 x 36 at y 188; footer A Type, Y Keyboard | WELL DONE, PILOT (T outlined), the score, place and rank, a help line |
| game complete | CONGRATULATIONS (L), CONTINUE 160 x 44 | the congratulation lines typed (S) and the results |

Not done by this view (legacy drawing at 0.4): the tutorial hint (the in-play package), the sequels
and the plain front end, Configure controls (not on the console), the intro pages.

### Tests

`apps/tests/console_menu_test.cpp` (desktop, the original's font metrics, synthetic texts): every
screen (main with and without GAMES, exit, start, scores, options, information on every page and
sheet, name entry, in-game, mission complete, game over before and after its menu, game complete):
every target at least 36 x 36 (the keyboard's keys 20 x 20), on the screen and on whole pixels, no
two targets of different items overlapping, sub-targets inside their item's bar, labels fitting
their box at their size (row labels before their arrows; every spinner value between its arrows at
S), the footer inside its room and clear of every box, titles fitting, every visible item with a
bar, hit rectangles equal to the bars (and their centre back in the bar through the 0.4 mapping);
the layouts' pixel positions; the touch path into the model (a bar opens Options, the Show FPS
arrow, the slider's ends and a drag, BACK, a mission row, a locked row, the difficulty arrow,
START starting the chosen mission); Information's sheets and pages by touch and by PageUp; the
re-flow (width, sheet height, paragraphs, icon headings, credits, balanced markup); the key names;
the key widths; drawing inside both screens. `game_flow_test` passes unchanged.

### Gallery

`out/ui3ds/gallery/9c0e504/as3d_sheet.png` (+ `@2x`) and the per-screen pictures under
`out/ui3ds/gallery/9c0e504/as3d/` (the selector and the sequels' sheets beside them are unchanged
in their layouts). The gallery's tap-through check, on fingers through the real touch path:

    as3d_menus: tap on ingame at 1300 -> playing at 1301, no menu within 30 frames: ok
    as3d_menus: tap on hint at 1500 -> playing at 1501, no menu within 30 frames: ok

The tutorial hint now reads "To launch missile press the {B} button" (key names). The sequels'
runs started mission 3 (their profiles on the Azahar SD keep earlier gallery runs' checkpoints), so
their early hint was not reached and not dumped: not a change of this package.

### Deviations from the mockups, and why

- **Options order**: Camera before Show FPS (the mockup has Show FPS first): the bars follow the
  menu's item order, which is the pad's focus order.
- **Options help band**: two S lines at y 196 instead of the mockup's larger text, so that the
  attract level keeps most of the screen.
- **Start Game**: three mission rows of 36 px (the mockup: four of 30) to keep every target at
  36 px; the helicopter row shows "2 OF 10" instead of a small icon (the icon at 1/2 is 42 px, taller
  than the bar; the top screen shows it 1:1). No "Best:" line on top (the profile keeps no per-mission
  best). Footer only "A OK" (no room beside BACK / START for more).
- **Information**: the page is read in sheets ("1/10 (1/2)") because a re-flowed page of the story
  is about 23 lines at S and the top screen holds 16; the mockup's text simply ran off the screen.
  The desktop's "(Page 1 of 2)" is dropped from the titles (the counter says it).
- **Top Scores**: the table is on the top screen at S (the mockup put nothing there); the bottom
  shows the title picture and the best pilot.
- **Mission complete**: the "Next helicopter: N more stars" line is not drawn (the front end does
  not compute it); buttons 108 / 84 / 112 x 44 instead of 100 x 40 (their pictures at 1/2 are wider).
- **Game over**: the desktop's gameover_0 layer (a picture without alpha, a black box over the world
  at 1:1) is not drawn; gameover_3 fades in and pulses over itself.
- **Name entry**: the desktop model's 4 x 10 keys (29 x 30 px) instead of the mockup's 8 x 5 grid
  of 36 x 30 (the model's keys and ids are kept, so the pad and the scripts are unchanged); the
  system keyboard stays the main path.
- **No focus is shown until the pad is used**: the touch mode starts menus without a focused item
  (the M2.1 rule); the gallery's screens therefore show no focused bar except where a tap left one.
- **Label pictures** of the small corner buttons (BACK, START, YES / NO) are at 1/2 as planned, which
  makes YES / NO small (about 7 px caps); L text would be larger (a choice for the owner).

### What the next packages start from

- **In play (step 4)**: the view's skin and drawing toolkit (`Px`, `drawBar`, the key hints, the
  half pictures) are in console_menu.cpp; the hint box (`Screen::Hint`) is still the legacy one and
  `ConsoleMenuView::handles` is the place to add it (its top-screen panel, a 160 x 56 OK on the
  bottom). PAUSED stays the host's. The tap-through fix already covers the hint's OK. The HUD
  tiles' touch goes through `TouchMapper`, which now never sees a finger that went down on a menu.
  `consoleKeyNames` already rewrites the hints' key names.
- **Sequels and the selector (step 5)**: `handles` returns false for `Frontend::sequel()` /
  `plain()`; their screens need a layout function each (the same boxes, hints and hit write-back:
  `applyHits` and `touchInput` are generic over the box kinds) and a skin (colours and label
  pictures are constants at the top of console_menu.cpp: make them a `ConsoleSkin` per game). The
  sequels' text buttons (`SequelButton`) are drawn as plain text labels by the generic Bar path.
  `GameSelector` has its own `MenuSystem`; the same `ConsoleLayout` / `touchInput` pattern applies.

## In play on the console (round 4, R4.7)

Step 4 of the plan (section 4), the first game. The bottom screen in play is the HUD drawn in the
screen's own pixels at the art's own size, and its tiles are the touch controls; the top screen
carries the level name, the hint box and PAUSED. The desktop, Android and the web are untouched
(`tools/ci.sh` passes with the goldens unchanged; the console layout is `HudTarget::Bottom320x240`,
which only the 3DS and the desktop's `--dual-screen` preview use). Also in this package: the
owner's size rule (one size per group of labels, "Owner decisions" above).

### The bottom screen

`engine/src/ui/hud_layouts.cpp` `makeV170Bottom`: the same `HudLayout` data as before, every value
a whole screen pixel times 2.5 (the 0.4 mapping of the 320 x 240 lists), so `drawHud` puts every
quad on a whole pixel. New layout fields (`as3d/hud_layout.h`, all off in the original layouts):
the health frame in three pieces (`barCapLeft` / `barCapRight`: its ends 1:1, the middle
stretched), `pauseTile`, `HudColumn::fixedSlots` (every kind in its own slot, empty ones dimmed by
`emptyDim`), `countPatch`, `selectOutline`, `scoreDigits`, `weaponTextX`, `keysArea`;
`levelNameY` / `messageY` < 0 leave those texts to the top screen. The icon atlases are
nearest-filtered for this layout (drawn 1:1); `mainbar.tga` stays bilinear (its fill is squeezed
into the bar; its frames and boxes sit on whole texels), `life.tga` is drawn at exactly 1/2.
`drawConsoleHudExtras` (`as3d/console_hud.h`) adds the pad reminder and the pressed look of the
tile under the finger.

| element | pixels (x, y, w, h) | notes |
|---|---|---|
| pause tile | touch (0, 0, 40, 36), drawn (4, 4, 32, 28) | dark plate, two bars |
| health bar | (44, 8, 120, 21) | frame ends 6 and 14 texels 1:1, middle stretched; the fill (172 texels) squeezed into 112 px |
| score | right-aligned at x 312, y 11; field (172, 11, 140, 16) | L digits (number routine, 14 px each); 10 digits reserved |
| lives | (6, 40), step 18, 16 x 16 | life.tga at 1/2, at most 5 |
| weapon box | (4, 60, 70, 38), icon 66 x 35 at (5, 62) | tap = next weapon |
| weapon level | "WEAPON" / "LEVEL n" at S from (80, 64), 13-px pitch | the first game has no level pips |
| power-up tiles | x 170, y 36 + 40 i, i = 0..3, 70 x 38 | the four kinds with icons |
| missile tiles | x 246, y 36 + 40 i, i = 0..4, 70 x 38 | under the right thumb |
| counts | right edge at the tile's x + 60, y tile + 21, L, on a dark patch | missiles from 1, power-ups from 2 (the game's rule) |
| selection | the box drawn twice (the game's own) and a one-pixel orange frame | |
| pad reminder | (4, 166, 162, 72): B Missile, X Power-up, Y Weapon, R Next missile | the owner's key pictures, S labels, 18-px rows |

**The score rule** (Owner decisions): the field is 10 digits x the L number advance = 140 px (the
plan's estimate of 80 px assumed an 8-px digit; the engine's number routine advances 14 px at L),
ending 8 px from the right edge; the health bar takes the 120 px left between the pause tile
(+4 px) and the field (-8 px). `hud_layout_test` "console hud: the layout in pixels, the score
rule, no overlaps" asserts the field width, its right edge, the gap to the bar and that the widest
score stays in its field; no two element rectangles overlap anywhere on the screen.

### Touch: the tiles

`TouchLayoutOptions::forHudTiles320x240(game)` / `hudTiles` (`as3d/input.h`,
`engine/src/input/touch_mapper.cpp`): the tiles are the layout's element rectangles named
`pause`, `weapon`, `missile0..4`, `powerup0..3` (`hudElementRects`), scaled to the framebuffer; no
round buttons, no band, no drag area, no auto-fire. A game whose bottom layout has no tiles (the
sequels) gets the band of before from the same call.

- **pause tile**: the pause edge (the in-game menu, as START).
- **weapon box**: one next-weapon press.
- **a missile / power-up tile of a kind not selected**: that kind is selected: "next" presses on
  alternate steps (press, release, press, ...) until the world's selection is the tapped kind,
  read from `TouchInventory` (`touchInventoryOf(world)`, set by the menu loop before each frame's
  steps), at most one cycle; nothing is fired. **A tap on the selected kind** uses it: missile /
  power-up held while the finger is down. **An empty slot**: nothing.
- Without an inventory (the direct modes of `ctr_loop.cpp`, which this package does not touch) a
  missile or power-up tile uses the selected kind, as the old buttons did.
- `log_input=1` names the tile: `AS3D_INPUT ... touch=missile3`.

The circle band is not built or drawn for the first game (`ctr_frame.cpp`: `prebuildTouch`, the
flow's and the direct path's touch lists). **Vertices**: before, the band alone was 8256 vertices
(`AS3D_TOUCH_PREBUILD ... vertices=8256`, gallery 9c0e504) plus the HUD list; now the whole bottom
screen in play is about 490 vertices per frame (`AS3D_HUD frames=300 bottom2d_vertices=505
hud=492 touch=0 menu=13 layout=tiles`, a new line every 300 frames beside `AS3D_UI_LISTS`; the
next soak's `AS3D_PASS_MS` will show the GPU side). The tap-through fix of R4.5 is unchanged: a
finger that went down on a menu (the hint's OK, Resume) never reaches the tiles.

Test "console hud: the tiles are the touch controls" (hud_layout_test): 11 tiles equal to the HUD
rectangles, each at least 36 px, the centre of each hits it, the bar and the reminder are no
tiles; pause, next weapon, an empty slot, a selection by two presses on alternate steps (no
missile fired), the use of the selected missile and power-up, a touch beside the tiles; the
sequels keep the band.

### The top screen in play

`drawConsolePlayTop` (`as3d/console_hud.h`), from `ctr_frame.cpp` `buildTopQuads`:

- **Level name** for the typewriter's time (types from 1 s, holds 3 s, fades over 1 s): "Mission 1:
  Tutorial" split at the colon, "MISSION 1" at L (y 14) over "TUTORIAL" at T (y 30), both outlined,
  each line growing rightwards from the left edge of its full text, centred; a name without a
  colon is all title; T steps down to L when wider than 392 px.
- **Cheat message** at S, outlined, centred at y 206.
- **PAUSED** at T, outlined, centred on y 116, over the world dimmed 45 % (the in-game menu).
- **The tutorial hint** is a console menu screen now (`ConsoleMenuView::handles` takes
  `Screen::Hint`): top, the world dimmed 35 % and a box at the bottom of the screen (x 14, 372 px
  wide, sized to its text, 8 px above the edge): TUTORIAL at L, the hint re-flowed at S to 356 px
  (the original's `^` breaks are its box's line ends: joined), A OK at the lower right; bottom,
  the HUD dimmed 60 % and one OK bar 160 x 56 at (80, 100), no footer (the reminder is there). A, B
  or a tap on OK closes it; the tap-through fix keeps the finger from the tiles. Test "console
  menus: the tutorial hint's OK".

### Gallery

`out/ui3ds/gallery/a75e679/as3d_sheet.png` (+ `@2x`, the first game's runs on the final build),
per screen under `out/ui3ds/gallery/a75e679/as3d/`; the selector and the sequels on the same code
less the hint footer fix: `out/ui3ds/gallery/16e2e4e/{selector,as2,gulf}_sheet.png`. New run
`as3d_play` (tools/3ds_ui_gallery.py): no bot, run.ini `hud_stock=5` (missiles 0, 1, 3 and
power-ups 0, 1), `pad_script` taps at the tiles' centres: `20_play_stocked`,
`21_tap_select_missile` (missile 3 selected by a tap: outlined), `22_tap_fire_missile` (a second
tap: the count 5 -> 4, the missile in the air), `23_tap_select_powerup`, `24_tap_next_weapon`,
`25_tap_pause` (the pause tile: PAUSED and the in-game menu). The tap-through check: `tap on
ingame at 1300 -> playing at 1301` and `tap on hint at 1500 -> playing at 1501`, no menu within 30
frames: ok. Start game, options and the Information pages show the size rule (the mission list
all at S because "Mission 2: Leaving the Home" does not fit at L).

### Deviations from the mockups, and why

- **Health bar 120 px** (mockup 173): the score rule's 140-px field (10 digits at the 14-px L
  number advance); the bar's ends stay 1:1, the middle and the fill are squeezed.
- **No frame around the score**: the field is plain text right-aligned; the mockup has none either
  at the console size, the original's second health-bar frame would not fit.
- **Tiles 70 x 38 at a 40-px pitch** (mockup 39 with a 4-px gap): the box art is 38 texel rows
  (1:1), and five missile rows must fit under the top row (36 + 5 x 40 = 236).
- **Weapon level as text only** ("WEAPON / LEVEL n" at S): the first game has no level-pip art; L
  would not fit beside the weapon box with the tile columns at x 170.
- **Lives at 1/2 under the bar** instead of beside it: the top row holds the pause tile, the bar
  and the score field.
- **Counts 10 px from the tile's right edge**: the box's notched corner cut the last digit at 4 px.
- **Pressed look** (a light frame and a faint glow on the tile under the finger): not in the
  mockups; the console's only feedback that a tap landed.
- **The hint's bottom screen has no footer** (the mockup's "A OK" footer would cover the pad
  reminder; the top box says A OK); a hint longer than the top screen's 14 lines is cut there.
- **PAUSED** is centred a little higher (y 96..136) than the mockup's to leave the helicopter clear.
- **The desktop `--dual-screen` preview** (apps/game, not this package's files) draws the new HUD
  with the old circle band over it and no level name (its top screen has no console overlay).

### What the sequels package starts from

- The sequels' bottom layouts (`makeSequelBottom`) are unchanged: the 0.4 copy above the band of
  round buttons, the band drawn and touched as before, PAUSED and the level name as before
  (`sequel` gallery runs of 16e2e4e). To move them: give `makeSequelBottom` pixel values (x 2.5)
  and the console fields (`pauseTile`, `fixedSlots` with up to 10 power-up slots or a scrolling
  column, `countPatch`, `scoreDigits`, `keysArea`, `levelNameY = -1`); their frames at 3/4 need a
  resampled copy (as `textureHalf`); `hudTilesLayout` switches the touch to tiles as soon as the
  layout has a `pauseTile` and fixed slots; `drawConsolePlayTop` takes over when `levelNameY < 0`.
- The sequels' hint is their `SequelPanel` (`ConsoleMenuView::handles` is false for sequels); the
  first game's `drawHintTop` / `layoutHint` are the pattern.
- The size rule (`applySizeRule`) applies to every layout the view makes, so the sequels' screens
  get it with their layout functions.
- Not done here: the loading screen, the name-entry fallback grid and the selector (next package).

## Sequels, selector, loading and name entry on the console (round 4, R4.8)

Step 5 of the plan (section 4). AirStrike 2 and Gulf Thunder go through the console menu view
with their own skins, the game selector and the loading screen get console layouts, and the
sequels' HUD is the console grid with their own art. The desktop, Android and the web are
untouched (`tools/ci.sh` passes with the goldens unchanged). Run.ini `ui_menus=0` still gives
back the 0.4 copy of every screen (README row).

### The sequels' screens

`engine/src/ui/console_menu.cpp`: the skin constants became a `ConsoleSkin` per game
(`kSkinAs3d`, `kSkinAs2`, `kSkinGulf`, chosen by `useSkin` from the front end): AirStrike 2 grey
bars with the top-right corner notched by 8 px (their HUD boxes' cut), green L labels (its ink,
0xFF00B000), orange titles (its accent); Gulf Thunder the same bars on its green ground with its
own scan lines (`lines_gulf.tga` repeated, one texel per pixel), white-green labels, red titles.
`ConsoleMenuView::handles` now takes the sequels' menus, their `HeliSelect`, `Credits` and
`Dialogue`, and their intro pages (`introShown`). The view reads the sequels' state through
read-only accessors of `SequelScreens` (`state`, `introRun`, `shownHeli`; as2_screens.h), so the
model, its callbacks and the pad navigation of M5.2 are unchanged. Their captions keep their own
case (the desktop's padding spaces trimmed).

| screen | bottom | top |
|---|---|---|
| intro (logo page, AirStrike 2's comic pages 1-4) | the game's name, FASTER (A: the page four times as fast) and SKIP (B or a tap: the comic's touch Skip) 148 x 56 at y 92 | the page fitted by its drawn part: pages 1 and 2 (the 800 x 320 panorama) at 1/2, page 3 at 0.4, page 4 (800 x 512) at 15/32, a logo page at 1/2 (`Renderer2D::beginMapped` / `appendMapped`) |
| main menu | six bars 154 x 56 in two columns (Start Game, Top Scores, Options / Information, Credits, Quit: the pad's Up / Down walk a column), GAMES and the footer on the bottom row | AirStrike 2: its logo with the clouds, the glow and the spinning "2" at exactly 1/2 on a 34-px band; Gulf Thunder: its emblem, tinted, with its clouds, at 1/2; the version / copyright band |
| start game | the first game's layout with their ids: three mission rows, Difficulty and Game mode spinners (labels at S when "SINGLE PLAYER" would not fit between the arrows at L), BACK, NEXT | the title band, the mission (L), difficulty and game mode (S) |
| helicopter selection | previous / next arrows 56 x 104 (their picture items), between them the name (L), "n / N", speed and armour bars (the desktop's scales), "not available" for a locked one; the player spinner with two players; BACK, Start / Continue / Accept | the model view (the frame's, unchanged: `drawModelViews` is now also called with the console view) over the dot grid and the static; a locked one: "not available" over the attract level |
| options, top scores, exit, name entry | the first game's layouts (the same item ids) in the skin | the first game's panels (the scores table, the question, WELL DONE) in the skin's colours |
| information | the first game's PREV / NEXT and dots (7 or 8 pages) | the page re-flowed at S from the sequels' texts pages (`layout().infoKey`: Gulf Thunder has no page 4) with their icons from the game's own HUD tables |
| credits | the title, BACK | the names centred at S, 13-px pitch, 5 px where the texts leave a line out; more than fit: sheets turning every 6 s |
| in-game menu | Resume, Options, Restart, Quit 240 x 40 | PAUSED (the host's) |
| hint | OK 160 x 56 over the HUD dimmed 60 % | the first game's box, titled "Tutorial Tip", the text re-flowed |
| dialogue | NEXT 160 x 56 (Enter: completes the page being typed, else turns it) over the dimmed HUD | a box at the bottom: the portrait 1:1 (portraits2.tga, 80 x 120), the page re-flowed at S beside it, typed at the desktop's pace |
| game over | GAME OVER (T), QUIT and RESTART 138 x 44 at once | the game's comic (gameover_k_1/2) and its GAME OVER picture at 1/2, fading in; the score panel (new high score, place, rank) |
| mission complete | MISSION COMPLETE, Choose Helicopter 312 x 44, Restart / Quit / Next 102 x 44 | the title (L: T is wider than the screen) outlined, the statistics in L as they appear, the new helicopter's line |
| game complete | CONGRATULATIONS, Continue from 4 s | the eleven lines typed at S from 4 s |

**Name entry (the fallback of the system keyboard).** The sequels' touch keys have the first
game's ids (200 + character, four rows of ten, DEL 60, OK 1), so they take the first game's
layout: keys 29 x 30 at a 31 x 32 pitch, L letters, the field, DEL, OK across the screen, footer
A Type, Y Keyboard; the M5.2 D-pad grid walks them unchanged.

### The sequels' HUD and its tiles

`engine/src/ui/hud_layouts.cpp` `makeSequelBottom`, in screen pixels x 2.5 as the first game's:
the same grid as `makeV170Bottom` (pause tile 40 x 36, health bar left of the score field, the
weapon box 70 x 38 at (4, 60), the power-up column at x 170 and the missile column at x 246 of
70 x 38 tiles at a 40-px pitch, the pad reminder at (4, 166)) with the sequels' art: the boxes of
mainbar2 in nine pieces (`HudLayout::boxSlice`, texels (13, 106) .. (87, 160), corners 8 / 7 /
18 / 19 texels at 1:1, the pin and most of the top strip left out), the bar and its segmented
fill at exactly 1/2 (116 x 17), the level pips 1:1 under the weapon box, the lives at 1/2 (at
most five), icons 1:1, counts at L on a dark patch, the selected tile double-framed and outlined
in the game's colour, every icon at full strength (the outline says which is selected), the
pause tile and the reminder in the game's colours. The **score rule** is the first game's: 10
digits at the L number advance (140 px) right-aligned at x 312, the bar between the pause tile and
the field. The level name, the cheat message and the hint are on the top screen
(`drawConsolePlayTop`, `levelNameY < 0`).

Ten power-up kinds do not fit a column: the column has five rows (`HudColumn::packedRows`)
showing the kinds owned in kind order; with more than five a window keeps the selected one in
the middle row where it can (`hudPackedFirst`, `hudPackedKind`, hud_layout.h). The rows are the
touch tiles `powerup0..4`; `TouchTile::packed` makes the touch mapper ask `hudPackedKind` which
kind a row shows at the moment of the tap (tap = select by "next" presses on alternate steps,
tap on the selected = use, an empty row = nothing), so the HUD and the touch never disagree. The
missiles are fixed slots as in the first game. `hudTilesLayout` switches both sequels to tiles
(12 of them); the circle band is no longer built for any game.

Tests (hud_layout_test): the console layout test now runs for the three games (pixels, no
overlaps, the score rule, tiles 70 x 38, the nine-piece box and the half-size bar for the
sequels, Main untouched); the packed rows (window, kinds, empty rows); a packed row selecting
then using its kind by touch; the fullest HUD of each game drawn inside its rectangles on whole
pixels (headless GL, game data). The sequels' old 0.4 tests (band, column of ten) are gone with
the layout.

### The game selector

`ui::consoleSelectorLayout`, `consoleSelectorApplyHits`, `drawConsoleSelectorBottom` /
`drawConsoleSelectorTop` (console_menu.h), used by apps/3ds/source/ctr_selector.cpp when
`ui_menus=1`. The model is `GameSelector`'s (cards, focus, Exit, Play); the hits are written
before each update so the bottom screen's 0.4 mapping takes taps to the drawn cards.

- Bottom: "CHOOSE A GAME" (L) on a 20-px band; three cards 101 x 166 at y 22 (the game's own
  marquee, AirStrike 3D's flaming banner mesh since R4.9, below; the card's
  name with "AirStrike II: " dropped, and "N missions", each group at one size, L or S;
  "TAP: PLAY" on the current card); EXIT and PLAY 92 x 38 in the bottom corners.
- Top: the current game's logo at exactly 1/2 (AirStrike 2's logo and "2", Gulf Thunder's
  emblem; the first game's banner mesh over the top 100 px since R4.9, below), its full title at T
  (L when wider), the version (S), the save
  lines (L) or its missing files (red), and the keys with the owner's pictures: A Play, B Exit,
  START SELECT Quit (`loadKeyGlyphs` is now also used by the selector).
- `GameCard::brief` and `GameCard::missing` are new fields the desktop ignores.

### The loading screen

`ui::drawConsoleLoading` (console_menu.h), from ctr_frame.cpp `loadingFrame` (the console branch
of its list building; its GPU part only skips the old 3-px bar). Top: the first game's
`menu\loading.tga` 1:1 (its middle 206 rows), AirStrike 2's comic of the campaign's third at
exactly 1/2 (400 x 165), Gulf Thunder's comic tiles at 1/2 (its first 400 x 206 px); under it the
mission's name (L, S when wider) and a 6-px bar of 200 px in the game's colour. Bottom: the
CONTROLS card: Circle Pad fly, A fire, B missile, X power-up, Y next weapon, R next missile, ZR
next power-up, START pause, touch tiles: select, use (key pictures where the set has them, the
labels one size). The attract level's loads stay black. `loadingInfo()` (frontend.h) gives the
look and the name; the first game's front end now leaves its mission's name there too.

### Gallery

`out/ui3ds/gallery/c708b4f/{selector,as2,gulf,as3d}_sheet.png` (+ `@2x`, per screen under the
game folders), all runs on this package's build. New runs in tools/3ds_ui_gallery.py:
`as2_gameover` and `gulf_gameover` (game over, then Quit by a tap: the name entry),
`as2_play` (the HUD tiles by taps, as `as3d_play`); `as2_menus` adds the second Information
page, credits, exit and the next helicopter; the sequels' runs use a fresh profile (the SD's
profiles start mission 3; restored after) so mission 1's start dialogue and hint are reached; a
`("frame", F)` selector and `("load", -1)` (the mission's last loading frame, with its name; the
first dump of a run is the boot's, without one).

### Deviations from the mockups, and why

- **Sequels' main menu in two columns** of 154 x 56 (the mockup: one column of six bars about 31
  px high): six bars of 36 px and the bottom row (GAMES, footer) do not fit 240 px; two columns
  keep every target at least 36 px and the footer row as the first game's. GAMES is bottom-left
  as in the first game (the mockup has it bottom-right).
- **Bars drawn, not the original's chrome pieces**: the sequels' button pieces are 30 (AirStrike
  2) or 37 texels high and would be stretched or squeezed to 36; the bars are the skin's flat
  fill, edge and notched corner, the captions in the game's font and colours.
- **Comic pages at 1/2, 0.4 and 15/32**, not 0.75-1:1: a 4:3 page fitted to the 400 x 240 top
  screen cannot be larger without cutting the speech bubbles; pages 1, 2 and 4 are about 20 %
  larger than before, page 3 (the whole page is drawn on) stays 0.4.
- **Gulf Thunder's logo at 1/2** (152 x 106 drawn), larger than the plan's 1/4-1/2: at 1/4 a
  bilinear picture drops texels.
- **The sequels' HUD boxes in nine pieces at 70 x 38** (the plan: their frames at 3/4 resampled):
  at 3/4 a box is 65 x 45 and five missile rows no longer fit under the top row; the nine pieces
  keep the corners and the notch at 1:1. The bar and fill at exactly 1/2 instead of 3/4 (a clean
  2 x 2 mean; 116 px fit the score rule's room). Lives at most five (the sequels allow ten).
- **Five power-up rows with a window** (the mockup shows four rows): five fit the 40-px pitch
  like the missiles; a window instead of a scrolling column, so a tap always names one kind.
- **Selection by the outline**, not the sequels' selection line and dimmed icons: the line is a
  1-px piece and the dimmed icons were hard to read at this size.
- **Mission complete title at L** (T is wider than 392 px).
- **Selector**: the cards' names without "AirStrike II: " (the full title is on the top screen),
  "TAP: PLAY" (the mockup's "TAP TO PLAY" is wider than a card at S); no "Best score" on a card
  (on the top screen); the first game's card and top had no logo in R4.8 (its banner is a 3D mesh;
  drawn since R4.9, below).
- **Loading**: the first game's picture cropped to its middle 206 rows; the bar 200 px (the
  mockup's 200 as well, 6 px high).
- **Name entry**: the model's 4 x 10 keys (as the first game's, R4.5), not the mockup's 8 x 5.
- **Not dumped**: Gulf Thunder's mission 1 hint did not come up in the scripted run; AirStrike 2's
  game over (`as2_gameover`) was tapped away before its settled dump (Gulf Thunder's is in the
  set; the earlier work-in-progress set `r48wip1` has AirStrike 2's comic pages at more moments).
- **Selector card names**: "Gulf Thunder" is wider than a card's 93 px even at S, so the size
  rule cuts it ("Gulf Thund..."); the full title is on the top screen.

### One photo session for the owner (five minutes, in this order)

At arm's length, slider off then halfway; a phone photo of each, both screens in frame.

1. **Selector** (hold START at boot if one game only): the three card names and "N missions"
   readable; the logo on top crisp; tap a card.
2. **AirStrike 2 intro** (delete `3ds/openairstrike/as2/profile.bin` first, or a fresh SD
   profile): page 2 and page 4 on the top screen; can the speech bubbles be read? Tap SKIP.
3. **AirStrike 2 main menu**: the logo band on top (clouds moving, the "2" spinning); the six
   buttons under the thumb; Information page 2 on top (S text thickness: the alternative A
   question of section 4).
4. **Start Game, helicopter selection**: the model on top in stereo; the arrows by thumb.
5. **Loading**: the comic on top, the name, the bar; the CONTROLS card legible.
6. **In play (AirStrike 2)**: the tiles' frames (nine pieces) and icons sharp; tap a missile
   tile, tap it again; the power-up rows; the score digits; the start dialogue's portrait and
   text on top.
7. **Gulf Thunder**: the main menu on the scan lines (do the lines beat or shimmer?), the
   emblem on top, PAUSED, game over's comic, the name-entry keys by thumb.
8. Note anything too thin (S), too dim (the sequels' green on black, Gulf's white-green on the
   scan lines) or out of thumb reach.

### What is left

- The desktop `--dual-screen` preview (apps/game, not this package's files) draws the sequels'
  new HUD with the old band over it and their old menus.
- On hardware: the console photo session above, the stereo comfort of the top-screen text and the
  model view, the GPU time of the nine-piece boxes (a few dozen more quads than one box).

## The first game's banner on the selector (round 4, R4.9)

The owner, 2026-10-12, on the R4.8 selector sheet: "In the selector airstrike 3d doesn't have its
proper logo". AirStrike 3D's logo is the flaming 3D title mesh (`objects\banner`): the desktop draws
it in the main menu over the attract level and, through the launcher's banner drawer, inside the
selector card's marquee box; on the console it was drawn over the attract level only (the main
menu, `ctr_frame.cpp`), and the selector card and top screen had the name in text.

- `apps/3ds/source/ctr_banner.h` (`ConsoleBanner`): the main menu's banner code moved out of the
  frame composer and shared. `load(vfs, defs)` (the object's mesh and material in a cache of its
  own; `AS3D_BANNER` logged once), `prepare(mt, slider, eyes, comfort, maxEyeOffset, minFocal)` (the
  pass and the stereo, as before), `drawEye(eye)` (the letterbox band and the mesh over the top
  400 x 100 px of the top screen, as before) and the new `drawBox(x, y, w, h, targetW, targetH,
  depth)`: the banner mono in a box of the current target, the viewport placed so that the part of
  the 4:1 picture the title covers (`ui::kBannerContent`, the desktop's rule) fills the box,
  scissored to it, shrunk to stay inside the target (the first card's box is 8 px from the screen's
  edge; its title is then 88 px wide instead of 93). The bottom target has no depth buffer: the
  mesh is drawn without the depth test in its pass order, which the banner's letters survive
  (gallery below).
- `ctr_selector.cpp`: when the first game's paks are present, its object definitions are loaded
  (`DefDatabase`, kept for the selector's stay with the paks mounted) and the banner with them
  (`selector: as3d: banner ready, defs+mesh 591 ms` on Azahar: the definitions' parse is most of
  it; the selector appears that much later at boot, once). Per frame the pass is prepared at the
  selector's clock, drawn after the bottom list in the card's marquee box (every frame: the card is
  always on screen) and, when that card is the current one, after the top list over the top 100 px
  with the band, in stereo per the slider as the main menu's (run.ini `slider=`, `eye_offset=`,
  `min_focal=`, `stereo_comfort=` apply).
- `console_menu.h`: `ConsoleSelectorLayout::marquee[3]` (the box 4 px inside each card, 56 px
  high; the app draws into the same box), `drawConsoleSelectorBottom(..., banner)` leaves the
  first game's name out of its marquee when the app draws the mesh, `drawConsoleSelectorTop(...,
  banner)` starts the text at y 104 under the mesh, as under the sequels' logos.
- Gallery: `tools/3ds_ui_gallery.sh BIN banner3 --only selector`, out/ui3ds/gallery/banner3/
  selector_sheet.png: the banner in the card (small, as on the desktop's card: the letters about
  11 px tall, the flames over the card's dark red) and over the top screen (the main menu's
  picture); the sequels' screens unchanged. The sway is the main menu's (the mesh's top edge
  leaves the band at some phases there too).
- Not changed: the title in text under the top-screen banner (as the full title under the
  sequels' logos).
- With it, the owner's second note on the same sheet ("You can write gulf thunder on two lines if
  no space"): a card's name wider than the card at L goes on two lines at its last space
  (`consoleCardTitleLines`), the group size then chosen for the parts: the group stays at L and
  all three names are two L lines ("AirStrike 3D" and "AirStrike 2" are wider than 93 px at L
  too), instead of three S lines with "Gulf Thund..."; a wrapped card's "N missions" line moves
  down 16 px (and its missing-file line with it), "TAP: PLAY" keeps its place. Gallery
  out/ui3ds/gallery/banner4/selector_sheet.png.

