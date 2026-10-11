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
- **Better-looking controller-button sprites.** The owner keeps button glyphs in the key hints but
  does not like the round A/B/X/Y/R ones in the mockups. Candidates to draw and show as images
  (1x and 2x), the owner picks: (1) badges drawn in the game's own style and palette (the HUD's
  orange/gold frame colours, a bevel, the letter in the game's font); (2) a CC0 prompt set (e.g.
  Kenney's Input Prompts, pixel 16 px and 32 px variants) recoloured to the game's palette; (3) the
  letter in a thin HUD-style frame. No Nintendo glyph art (trademarked).

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
