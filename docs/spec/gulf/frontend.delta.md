# Front end of Gulf Thunder: delta against AirStrike 2 (`gulf`)

Delta version 1.0 (package F1). Amends [../as2/frontend.md](../as2/frontend.md) 1.0 (a full
document for AirStrike 2) for the game `gulf` (AirStrike II: Gulf Thunder v2.71). Section
numbers mirror that document. Game rules behind the screens:
[engine-behaviour.delta.md](engine-behaviour.delta.md). Functions and their comparison class:
[symbol-map.md](symbol-map.md). Text addresses: `tools/exe_texts/gulf.json` (addresses only,
section 7).

Sources and tags as in the AirStrike 2 document. "(emu)" marks a layout read by running Gulf
Thunder's own drawing code in the 2D emulator (`re/tools/emu2d.py`, `gulf` entry;
`re/tools/emu_screens.py --game gulf <screen>` prints every quad and text call of a screen; the
same command with `--game as2` gives the AirStrike 2 screen for comparison). "Same code" is the
`same` class of the symbol map (instructions and read-only constants equal). Checked against the
running original: `out/reference/gulf/main_menu.png`, `menu_start_game.png`,
`menu_helicopter_select.png`, `loading.png`, `menu_ingame.png` (docs/running-originals.md).

## Summary for implementers

The Gulf Thunder front end is the AirStrike 2 front end (same screens, builders, actions, menu
stack, widgets' logic, HUD code, save handling) with a new skin:

1. **Colours**: text light grey 0xFFBFBFBF (AirStrike 2 green 0xFF00B000); focus and titles
   **red** 0xFF0000FF (orange 0xFF00A0FF). Packed 0xAABBGGRR as in the AirStrike 2 document.
2. **Title bar** instead of the AirStrike 2 title logo: black letterbox bars at the top and
   bottom with grey rules, the `logo_gulf.tga` emblem (512×256) at (144, 0), and on the main and
   in-game menus a green scan-line texture over the middle band (2.9, 3.1).
3. **Panel** from `interface_gulf.tga`: other bar pieces and offsets, side rails instead of
   chains, a red title on a riveted tab (3.1).
4. **Text button** 37 high from one bevelled box, no rivets, no 46-pixel margin (2.5).
5. **Intro**: logo pages only, no comic (3.2). **Loading screen**: one comic for every
   operation, tinted, under the title bar, with the operation's name (3.16).
6. **Information**: 7 pages (3.14). **Helicopter selection**: 3 helicopters, new arrow pieces
   (3.18). **Dialogues**: 3 in the whole game (3.19).
7. **HUD**: same code; the weapon icon table and level caps are Gulf Thunder's (4.4).

## 1. Screens and state machine

### 1.1 Principle: same

### 1.2 Screen list: same, with these differences

| # | Screen | Gulf Thunder |
|---|---|---|
| S0 | Logo pages | same code for the pages; the page list has no comic (3.2) |
| S0b | Intro comic | **absent** |
| S1..S8b, S12..S16 | menus | same builders and actions; drawn in the new skin (2, 3) |
| S9 | Loading screen | changed (3.16) |
| S9b, S13b | Portrait dialogues | same code; only operations 10 (start, end) and 24 (start) have one |
| S10 | Playing (HUD) | same code, other icon table (4.4) |

### 1.3 Transitions: same

With 24 operations: "Next" after operation 24 does not exist (game complete follows it).

### 1.4 What the world does behind each screen: same

No comic, so the brightness forced to 0.5 during the logo pages is restored after them.

## 2. Menu system (widgets and input)

### 2.1 Menu stack: same code

### 2.2 Items: changed (text button rectangle)

Same types and events. The **text button's hit rectangle** is (left, y, W, **37**) with
W = max(caption width, minimum width at item +0x5C), left = x, x − W/2 (flag 0x4000) or x − W
(flag 0x2000) (`UI_LayoutTextButton` gulf@0x4228a0; as2: W + 46 wide, 30 high).

### 2.3 Focus, hover and sounds: same code

### 2.4 Generic keyboard and mouse handling: same code

### 2.5 Widgets: changed (look)

Colours: see the summary. Every widget reads `gfx\ui\interface_gulf.tga` (256×256, registered
by `UI_LoadAssets` gulf@0x4296d0) in place of `interface.tga`.

**Text label** (same code, other colours): light grey; red when focused; grey 0x404040 when
disabled.

**Picture**: same code (tint light grey, red when focused).

**Edit field**: same code.

**List** (`UI_DrawList` gulf@0x423430; (emu), Start Game list (190, 200, 420, 144)): outline
(x, y, w, h) in the same green outline colour 0x5000FF00 as AirStrike 2, blend ALPHA. Rows at
(x + 10, y + 4 + 20k): the selected row **red** on a bar (x + 4, row y − 1, w − 25, 18) of
colour (0.44, 0.44, 0.44, 0.44), blend ALPHA; enabled rows light grey; disabled rows **dark
grey 0x404040** (as2: 0x808080). Scroll bar at x + w − 18, 15 wide, tinted (0.75, 0.75, 0.75),
blend ALPHA: up box texel (163, 148, 15, 15) at y + 3; track texel (163, 173, 15, 15) repeated
from y + 21 over h − 42; thumb texel (163, 165, 15, 6); down box texel (163, 189, 15, 15) at
y + h − 19. The up and down boxes are drawn in another colour while the mouse is over them
(GUESS for the colour: white). The thumb's travel was not traced (as for AirStrike 2).

**Spinner**: same code, other colours.

**Slider** (`UI_DrawSlider` gulf@0x422e80): label right-aligned at x − 10, light grey (red when
focused); bar texel (117, 124, 128, 11) at (x + 8, y + 3, 128, 11); knob texel (249, 122, 6, 15)
at (x + 5 + position, y + 1, 6, 15) in the label colour, blend ALPHA. Same position formula.

**Helicopter grid**: unused, as in AirStrike 2.

**Text button** (`UI_DrawTextButton` gulf@0x422950; (emu), checked on `main_menu.png`):

- Slide-in: same (value a rises by 4 × frametime; y_d = 600 − (600 − y)·a).
- Frame, white, blend ALPHA, from the bevelled box at texel rows 216..253 of the atlas: left
  piece texel (9, 216, 17, 37) at (left, y_d); body texel (25, 216, ≤77, 37) repeated from
  left + 17 over W − 68 in pieces of at most 77 (the last cut); right piece texel
  (102, 216, 51, 37) at left + W − 51. Total width W (AirStrike 2: W + 46).
- Caption centred on left + W/2 at y_d + 10: light grey; red when focused; grey 0x404040 when
  disabled; 0x000000FF when flag 0x10000 is set.
- **No rivets.** No highlight picture.

Example (emu): the main menu's "Start Game" (minimum width 180, centred at x 400, y 250):
frame 310..490, caption at (400, 260).

### 2.6 Tooltip: same code

### 2.7 Mouse cursor: same code

### 2.8 Text routines: same code

### 2.9 Shared screen decoration: changed

The two drawers keep their names and roles: `UI_DrawPanel` gulf@0x4252f0 and the **title bar**
`UI_DrawMenuHeader` gulf@0x429b00, which now takes two arguments (colour, scan lines). Both in
3.1.

## 3. Screens in detail

### 3.1 Shared frame elements: changed

**Atlas `gfx\ui\interface_gulf.tga`** (256×256 RGBA, checked on the image). Pieces used:

| Piece | Texel (x, y, w, h) | Used by |
|---|---|---|
| `title_left` | (4, 4, 45, 40) | panel title tab, left end |
| `title_body` | (49, 4, 45, 40) | panel title tab, repeated |
| `title_right` | (94, 4, 37, 40) | panel title tab, right end |
| `bar_top_left` | (4, 44, 67, 31) | panel top bar, left end |
| `bar_top_body` | (72, 44, 132, 31) | panel top bar, repeated |
| `bar_top_right` | (204, 44, 37, 31) | panel top bar, right end |
| `bar_bottom_left` | (4, 78, 67, 31) | panel bottom bar, left end |
| `bar_bottom_body` | (72, 78, 132, 31) | panel bottom bar, repeated |
| `bar_bottom_right` | (204, 78, 37, 31) | panel bottom bar, right end |
| `rail` | (8, 116, 3, 50) | the panel's left and right edges |
| `slider_bar` | (117, 124, 128, 11) | slider |
| `slider_knob` | (249, 122, 6, 15) | slider |
| `arrow_left` | (120, 148, 16, 40) | helicopter selection |
| `arrow_right` | (140, 148, 16, 40) | helicopter selection |
| `scroll_up` | (163, 148, 15, 15) | list |
| `scroll_thumb` | (163, 165, 15, 6) | list |
| `scroll_track` | (163, 173, 15, 15) | list |
| `scroll_down` | (163, 189, 15, 15) | list |
| `button_left` | (9, 216, 17, 37) | text button |
| `button_body` | (25, 216, 77, 37) | text button |
| `button_right` | (102, 216, 51, 37) | text button |

The pieces of the AirStrike 2 table (cables, rivets, titled/untitled top bars) do not exist in
this atlas.

**Panel `UI_DrawPanel(x, y, w, h, f, title)`** (gulf@0x4252f0; the title is now the sixth
argument, or null; VERIFIED-CODE, geometry (emu) at f = 0.16 .. 1 on the exit confirmation).
With Y = y − (1 − f)·y and yb = (y + h − 6) + (1 − f)·(600 − y − h), both truncated to integers:

1. **Static fill**: as AirStrike 2 (`snow.tga`, FILTER, the same overshooting height
   H = f²·h + 600·f·(1 − f) centred on y + h/2, random UV origin every frame).
2. **Top bar** at y = Y − 25 (as2: Y − 15), white, ALPHA: `bar_top_left` at x − 3, the body
   from x + 64 over w − 98 in pieces of at most 132 (the last cut), `bar_top_right` at
   x + w − 34. The same with or without a title.
3. **Bottom bar** at y = yb: `bar_bottom_left` at x − 3, the body from x + 64 over w − 98,
   `bar_bottom_right` at x + w − 34.
4. **Rails** (new; no cables): `rail` at x − 1 and at x + w − 1, from Y + 6 over
   (yb − Y − 5) in pieces of at most 50 taken from the top of the piece, white, ALPHA.
5. **Title tab** (titled panels only) at y = Y − 57: `title_left` at x + 17, `title_body`
   repeated from x + 62 over the title width Wt (pieces of at most 45), `title_right` after it;
   white, ALPHA. The title text centred on x + 62 + Wt/2 at y = Y − 45, **red**, additive font.

Order: items, panel, title bar, as in AirStrike 2.

**Title bar `UI_DrawMenuHeader(colour, scanLines)`** (gulf@0x429b00, VERIFIED-CODE, (emu),
checked on `main_menu.png`), replacing the AirStrike 2 title logo (no glow, no rotating emblem):

1. Black opaque fill (0, 0, 800, 97); grey opaque rule (0, 97, 800, 3), colour
   (0.373, 0.373, 0.373).
2. With `scanLines`: `gfx\logo\lines_gulf.tga` (4×4: one black row of alpha 0.58 and three
   dark-green rows (1, 51, 0) of alpha 0.38) over (0, 100, 800, 440) with UV (0, 0, 200, 110),
   i.e. one texel per screen pixel, repeated; blend ALPHA, white. The attract level in the
   middle band appears dark green with a dark line every 4 pixels.
3. Grey rule (0, 540, 800, 3); black fill (0, 543, 800, 97) (it runs past the bottom).
4. `gfx\logo\logo_gulf.tga` (512×256, the "Gulf Thunder" emblem with the lightning ring) at
   (144, 0, 512, 256), UV (0, 0, 1, 1), blend ALPHA, with the second texture
   `gfx\logo\clouds.tga` at UV (0.1T, 0, 0.1T + 2, 1) combined by ADD (as AirStrike 2;
   T += 0.5 × frametime per draw). Its colour is a tint that follows the `colour` argument
   (packed 0xAABBGGRR): each of the four channels moves towards the argument's channel by
   3 × frametime per draw (it starts at white, table gulf@0x0049c4d8, and then oscillates within
   one step of the target). Screens pass:

| Caller | colour | scan lines |
|---|---|---|
| main menu gulf@0x42a160, in-game menu gulf@0x428ec0 | 0xFFFFFFFF (white) | yes |
| Start Game, Options, Controls, Mission Complete, Exit, Helicopter selection, Information, Top Scores | 0xB0808080 (grey 0.5, alpha 0.69) | yes |
| Credits gulf@0x4226e0 | 0x60808080 (grey 0.5, alpha 0.38) | yes |
| loading screen gulf@0x40acb0 | 0xFFFFFFFF | no |

So the emblem fades to a dimmed grey on every sub-screen in about 0.17 s and back to white on
the main menu.

### 3.2 Logo pages and intro comic: changed

`G_StartIntros` gulf@0x40f0b0: the pages of Settings.xml `<Intros>` only (DivoGames built-in
page, image pages), **no comic appended**; the comic functions of AirStrike 2 are absent
(symbol-map.md). The shipped Settings.xml has the DivoGames page (VERIFIED-DATA:
`data/Settings.xml` of the install). Frame loop, speed-up and brightness: same code.

### 3.3 Main menu: changed (positions, captions)

`M_BuildMainMenu` gulf@0x42a200 (same opcodes): six text buttons, id and order as AirStrike 2,
centred on x 400 at y **250, 295, 340, 385, 430, 475** (as2: 230 .. 455, 45 apart), minimum
width **180** (200), captions without padding spaces (keys `button.start_game` ... in
`tools/exe_texts/gulf.json`). Draw gulf@0x42a160: the title bar in white with scan lines, then
the items; the version and copyright lines as AirStrike 2 (same code).

### 3.4 Start Game: changed (look only)

Same builder logic (gulf@0x42c1d0; its buttons' captions carry other padding, which changes the
button widths). List, spinners and buttons in the new skin; title bar dimmed.

### 3.5 Exit confirmation: changed (look only)

(emu) panel (210, 230, 380, 160) titled; YES and NO labels at (300, 330) and (500, 330).

### 3.6 Options, 3.7 Configure controls: changed (look only)

Same builders; captions with other padding (" Configure Controls " → wider); key-binding rows
(gulf@0x422110) in the new colours.

### 3.8 Mission complete: changed (colours, bound)

Same builder and action (24 operations, engine-behaviour.delta.md 10.3). Draw gulf@0x426180:
the statistic lines in **red** (as2 orange); the "New helicopter is available." line only when
`enableHelic` is 0..**2** (gulf@0x426361). Buttons " Choose Helicopter " at (400, 440),
Restart, Quit, Next at y 520 (emu).

### 3.9 Game complete, 3.10 Game over: changed (look only)

Same code apart from captions and colours; the game-over comic tiles are the same files and
positions (emu); title "Game Over" red.

### 3.11 Top Scores: changed (colours)

Same layout (emu): header bar (130, 180, 540, 20) in the same dark green 0x80006000, column
headings and rank numbers red, names and ranks light grey.

### 3.12 High-score check and name entry: same (one comparison)

`G_InsertHighScore` gulf@0x4130f0: the inserted entry offers "Post Scores" when its field at
+0x24 is non-zero (as2: greater than zero). Online scores are dropped anyway.

### 3.13 In-game menu: changed (positions, captions)

Buttons Resume, Options, Restart, Quit centred on x 400 at y **300, 345, 390, 435** (as2: 250,
295, 340, 385), captions without padding; title bar white with scan lines (checked on
`menu_ingame.png`).

### 3.14 Information: changed (7 pages)

The page spinner has **7** pages: overview, primary weapons 1 and 2 ("Page 1 of 2", "Page 2 of
2"), missiles 1 and 2, items 1 and 2; the third weapons page of AirStrike 2 is gone. Draw
callback gulf@0x428bb0 (new function; AirStrike 2's was code inside another function). Page
text colours (emu): headings white, body **red**, page hints grey 0x808080 at (570, 520) and
(570, 540). Texts: keys `info.N.*` of `tools/exe_texts/gulf.json`; AirStrike 2's page 4 keys
are listed under "removed", the later pages keep AirStrike 2's key numbers.

### 3.15 Tutorial hint box: changed (look only)

(emu) panel (220, 220, 360, 160) titled, text red with markup, Ok button at (400, 520).
No Gulf Thunder script reachable from operation 1 shows a hint.

### 3.16 Loading screen: changed

`SCR_SelectLoadingComic` gulf@0x40ac20 registers **one** comic for every operation:
`gfx\ui\comix\loading1_0_0` .. `loading1_0_3` and `loading1_1_0` .. `loading1_1_3` (no mission
test). `SCR_DrawLoading` gulf@0x40acb0, each step ((emu), checked on `loading.png`):

1. Black opaque full-screen fill.
2. Unless intermission: the comic, two rows of four tiles, columns at x 0, 256, 512, 768
   (widths 256, 256, 256, 64), rows at y 65 and 321 (256 high): one 832×512 picture from y 65,
   blend **ALPHA** with colour (1, 1, 1, **0.812**) (207/255), so it is slightly darkened by the
   black under it; UVs inset half a texel.
3. The title bar, white, without scan lines (it covers y 0..100 and 540..600 of the picture).
4. The operation's name centred on (400, 550), orange 0x00A0FF (additive).
5. Progress bar: outline (340, **575**, 200, 10) and fill (340, 575, progress × 200, 10) in
   light grey (as2: orange at y 500); "Loading" at (260, **571**), light grey.

### 3.17 Pause: same

### 3.18 Helicopter selection: changed (3 helicopters, arrows, colours)

Same builder, draw and action logic (gulf@0x427b70, gulf@0x427820, gulf@0x4275e0): index
modulo **3**; arrows `arrow_right` texel (140, 148, 16, 40) at (600, 278, 16, 40) and
`arrow_left` texel (120, 148, 16, 40) stretched to (180, 278, 20, 44); names from the table of
three names (keys `heli.name.0` .. `heli.name.2`), "Speed:"/"Armor:" labels light grey, the
same bars (speed × 280 / 1.5, health × 280 / 800); captions " Continue ", "  Start  ",
"  Accept  "; title bar dimmed. Checked on `menu_helicopter_select.png`.

### 3.19 Portrait dialogues: same code, three dialogues

Dialogue table gulf@0x0049b378: operation 10 start (3 pages) and end (2 pages), operation 24
start (2 pages) (keys `dialog.10.start.*`, `dialog.10.end.*`, `dialog.24.start.*`). Portraits
from `portraits2.tga` as AirStrike 2 (VERIFIED-DATA: same file name). The panel is the new one.

### 3.20 Credits: changed

Same layout (21 lines from y 120, 18 apart, centred on 400) with other names (keys
`credits.*`), role lines red; title bar dimmed to 0x60808080. The "Back" button at (40, 520).

### 3.21 Demo nag screen, purchase button, play-time limit: same (dropped)

The nag texts differ (unreferenced data).

## 4. HUD

Same code (`HUD_Frame` gulf@0x40abe0, `HUD_Draw1P` gulf@0x407c80, `HUD_Draw2P` gulf@0x408a60):
4.1, 4.2, 4.3, 4.5 to 4.9 of the AirStrike 2 document apply with Gulf Thunder's art files
(`mainbar2.tga`, `life.tga`, `items.tga`, `missiles.tga` of the Gulf data).

### 4.2 One-player HUD: same code, other level caps

The weapon level bar reads the cap table gulf@0x0049c09c = {4, 5, 7, 6, 5, 5, 4, 5, 3}: the
empty bar is 7 × cap wide and the filled part 7 × level wide, so where the level exceeds the
cap (laser, slot 4: loadout level 7 against cap 5; engine-behaviour.delta.md 8.2) the filled
part runs past the empty bar. Reproduce it.

### 4.4 Icon atlases: changed (weapons)

Weapons (`weapons.tga`, 256×128, UV table gulf@0x0049c0c0, 9 entries, ADD; slots as in
engine-behaviour.delta.md 8.2):

| Slot | Weapon | UV (s0, t0, s1, t1) | Texel |
|---|---|---|---|
| 0 | machine gun | (0, 0.727, 0.258, 1) | (0, 0, 66, 35) |
| 1 | impulse gun | (0.774, 0.453, 1, 0.727) | (198, 35, 58, 35) |
| 2 | plasma gun | (0, 0.453, 0.258, 0.727) | (0, 35, 66, 35) |
| 3 | photon gun | (0.258, 0.18, 0.516, 0.453) | (66, 70, 66, 35) |
| 4 | laser | (0.516, 0.727, 0.774, 1) | (132, 0, 66, 35) |
| 5 | big laser | (0.258, 0.453, 0.516, 0.727) | (66, 35, 66, 35) |
| 6 | plasma laser | (0.516, 0.18, 0.774, 0.453) | (132, 70, 66, 35) |
| 7 | lightning gun | (0.516, 0.453, 0.774, 0.727) | (132, 35, 66, 35) |
| 8 | wave gun | (0.258, 0.727, 0.516, 1) | (66, 0, 66, 35) |

Gulf Thunder's `weapons.tga` has two more cells than AirStrike 2's ((66, 70) and (132, 70),
used by the photon gun and the plasma laser). Missiles and power-ups: same tables and switch
(same code; the missile UV table gulf@0x0049c150 has the values of as2@0x0049e278).

## 5. Mission flow and progression

Same code; counts and tables in engine-behaviour.delta.md (24 operations, 3 helicopters,
loadout table, checkpoint, dialogues). 5.8 "The six helicopters" becomes three:
`player_1` (unlocked), `player_2` (operation 4), `player_3` (operations 9 and 10). 5.12: bonus
operations 11 and 18, bosses 10 and 24.

## 6. Save file and settings

### 6.1 Progress (`game.bin`): changed

Same scheme, payload 0x5d8 (engine-behaviour.delta.md 10.5): 3 helicopters, 24 missions. The
owner's `game.bin` of the install is 1792 bytes (docs/running-originals.md).

### 6.2 to 6.4 Settings: same code

`CFG_Read`/`CFG_Write` gulf@0x401b80/0x402500 are `same`; Settings.xml tags as AirStrike 2.

## 7. Texts compiled into the executable

`tools/exe_texts/gulf.json` (written by `re/tools/gen_exe_texts_gulf.py`): the AirStrike 2 key
scheme with Gulf Thunder's addresses, 230 entries (176 placed through the data map, 26 through
the pointer that holds them, 14 by a unique identical string), 7 dialogue pages from the Gulf
Thunder dialogue table, and a `removed` list of the 5 AirStrike 2 keys that have no Gulf Thunder
text (the third weapons page). Differences of content an implementer will notice: button
captions carry other padding (it sets button widths), the main and in-game menu captions have
none, three helicopter names, other credits, the Information overview mentions the game by
another name. `tools/extract_exe_texts.py` knows the Gulf Thunder executable by its hash but has
no table for it yet ("table": None); an implementer points it at `tools/exe_texts/gulf.json`.

## 8. Touch adaptation notes: same

## 9. Reuse assessment

Everything of the AirStrike 2 front end is reusable with per-game data: colours, the atlas
table, the title bar (new drawer), the panel's offsets and rails, the text button's pieces, the
loading layout, the information page count, the heli count.

## 10. Open questions

1. The colour of the list's scroll boxes while hovered (GUESS white).
2. The slider bar's colour (the colour state left by the label; GUESS white).
3. The loading comic was checked on one operation's screenshot only.

## Checked sections

| Section of ../as2/frontend.md | Status for `gulf` |
|---|---|
| 1.1 Principle | same |
| 1.2 Screen list | changed (no comic, loading, dialogues) |
| 1.3 Transitions | same |
| 1.4 World behind screens | same |
| 2.1 Menu stack | same code |
| 2.2 Items | changed (text button rectangle) |
| 2.3 Focus, hover, sounds | same code |
| 2.4 Keyboard and mouse | same code |
| 2.5 Widgets | changed (colours, list, slider, text button) |
| 2.6 Tooltip | same code |
| 2.7 Mouse cursor | same code |
| 2.8 Text routines | same code |
| 2.9 Shared decoration | changed (title bar) |
| 3.1 Shared frame elements | changed (atlas, panel, title bar) |
| 3.2 Logo pages and intro comic | changed (no comic) |
| 3.3 Main menu | changed (positions, width, captions) |
| 3.4 Start Game | changed (look) |
| 3.5 Exit confirmation | changed (look) |
| 3.6 Options | changed (look) |
| 3.7 Configure controls | changed (look) |
| 3.8 Mission complete | changed (colours, bound 3) |
| 3.9 Game complete | changed (look) |
| 3.10 Game over | changed (look; comic same) |
| 3.11 Top Scores | changed (colours) |
| 3.12 High-score check and name entry | same (one comparison) |
| 3.13 In-game menu | changed (positions, captions) |
| 3.14 Information | changed (7 pages, colours) |
| 3.15 Tutorial hint box | changed (look) |
| 3.16 Loading screen | changed |
| 3.17 Pause | same |
| 3.18 Helicopter selection | changed (3, arrows, colours) |
| 3.19 Portrait dialogues | same code; 3 dialogues |
| 3.20 Credits | changed (names, colours) |
| 3.21 Demo features | same (dropped) |
| 4.1 HUD order | same code |
| 4.2 One-player HUD | same code; caps table changed |
| 4.3 Two-player HUD | same code |
| 4.4 Icon atlases | changed (weapons) |
| 4.5 to 4.9 | same code |
| 5 Mission flow | same code; counts in engine-behaviour.delta.md |
| 6.1 Progress | changed (payload) |
| 6.2 to 6.4 Settings | same code |
| 7 Texts | changed (`tools/exe_texts/gulf.json`) |
| 8 Touch notes | same |
| 9 Reuse | see above |
| 10 Open questions | new list above |

## Changelog

- 1.0 (F1): first version.
