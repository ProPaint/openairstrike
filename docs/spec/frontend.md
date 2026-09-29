# Front end: menus, HUD, game flow, save data

Spec version 1.0 (WP-26). Source: `AirStrike3D.exe` v1.70 (image base 0x400000), read through
the Ghidra export in `re/out/v170/` and, where the export has no function, a plain disassembly
of the executable. Function names are in `re/symbols_v170.csv`. Shipped data
(`assets_extracted/`, `Settings.xml`, `config.ini`) and the original HTML manual were used as
a cross-check and feature list.

This document details what `engine-behaviour.md` §1.3, §10 and §11 summarise. Where the two
differ, this document wins; the differences are listed in §9.

Tags: `VERIFIED-CODE` (address), `VERIFIED-DATA` (checked on shipped files),
`INFERRED-SEQUEL`, `GUESS`. "ftol" is truncation toward zero.

Conventions used throughout:

- **Coordinates** are virtual 2D pixels of the 800×600 screen, origin top-left, y down
  (render-pipeline.md §8.1). A rectangle is written (x, y, w, h).
- **UVs** are written (s0, t0, s1, t1) in the engine's 2D convention of render-pipeline.md
  §8.2: the top-left corner of the quad samples (s0, t1), the bottom-left (s0, t0). With
  bottom-up TGA storage, t = 1 is the top row of the image as seen in an image viewer.
- **Colours** are given as RGBA 0..1 or as the packed value the code uses, 0xAABBGGRR
  (render-pipeline.md §8.2). "Orange" below always means 0xFF00A0FF = (1, 0.627, 0, 1);
  "rust" means 0xFF0030C0 = (0.75, 0.19, 0, 1).
- **Blend** modes: 0 opaque (no blending), 1 ALPHA, 2 ADD, 3 FILTER (destination × source
  colour) (render-pipeline.md §8.2, engine-behaviour.md §13.2).
- **Menu time** `mt` is a clock that restarts at 0 whenever a menu is pushed or popped and
  advances by frametime while any menu is shown (0x1fd93b4; 0x4292f0, 0x429360, 0x4299f0).
  Most screen animations are driven by it. VERIFIED-CODE.
- **Pulse(f, φ)** means the grey level 255 × (0.5 + 0.5 sin(f·π·mt − φ)), applied as an
  RGB colour (R = G = B) with alpha 1.

---

## 1. Screens and state machine

### 1.1 Principle

There is no separate "menu mode". A level is always loaded: either an attract level
(`intro1`..`intro4`, an intermission level with a fixed swaying camera and no players) or a
mission. Menus are a stack (depth 16) drawn over that level; a menu shown during a mission
comes with the paused flag, so the mission is frozen but still rendered behind it.
VERIFIED-CODE (engine-behaviour.md §1.3; `UI_PushMenu` 0x4292f0, `UI_Frame` 0x4299f0).

The front end is driven by these flags (engine-behaviour.md §1.3): intro running, paused,
HUD-hidden, game over, intermission level, two players.

### 1.2 Screen list

| # | Screen | Built by | Shown over | Music |
|---|---|---|---|---|
| S0 | Intro logo pages | 0x408070, 0x408260 | nothing (2D only) | none started by the intro (GUESS; the intro only stops BASS at its end, 0x408260) |
| S1 | Main menu | 0x429f60 | attract level | attract level's `music` (all four: `music\track05.mo3`, VERIFIED-DATA) |
| S2 | Exit confirmation | 0x426c90 | attract level | same |
| S3 | Start Game (mission, difficulty, players, helicopters) | 0x42b710 | attract level | same |
| S4 | Top Scores | 0x42c660 | attract level (or wherever the high-score check ran) | same |
| S5 | Name entry | 0x42a580 | attract level | same |
| S6 | Options | 0x42aea0 | attract level, or the paused mission | unchanged |
| S7 | Configure controls | 0x423520 | as S6 | unchanged |
| S8 | Information (10 pages) | 0x428c00 | attract level | same |
| S9 | Loading screen | 0x404230 | replaces everything while a level loads | none |
| S10 | Playing (HUD) | 0x4041b0 | mission | mission's `music`, looped |
| S11 | Pause (P / Pause key) | none: no menu, no text | mission, frozen | keeps playing (GUESS: no pause call on the music was found) |
| S12 | Tutorial hint box | 0x42c000 | mission, frozen | keeps playing |
| S13 | In-game menu (Esc) | 0x428fa0 | mission, frozen, HUD hidden | keeps playing |
| S14 | Game over | 0x4278a0 | mission, frozen, HUD hidden | the current module jumps to pattern order 35 (0x408c80: same module, order 0x23) |
| S15 | Mission complete | 0x4267a0 | mission, frozen, HUD hidden | keeps playing |
| S16 | Game complete (after mission 20) | 0x4274f0 | mission 20, frozen, HUD hidden | keeps playing |

There is no mission briefing, no shop or upgrade screen, no separate difficulty screen, no
helicopter-selection screen other than the grid inside Start Game and Mission Complete, no
separate credits screen (credits are page 10 of Information, and the `givemecredit` cheat),
and no save/load slot screen. VERIFIED-CODE (every menu builder is listed above; the menu
builders were enumerated through `UI_AddItem` 0x429490's callers).

### 1.3 Transitions

```
boot ──(ShowLogo=1)──> S0 intro pages ──(all pages done)──> attract level + S1
boot ──(ShowLogo=0)──────────────────────────────────────> attract level + S1

S1 Main menu:  Start Game → S3 | Top Scores → S4 | Options → S6 | Information → S8
               Exit → S2 | Esc / right click: ignored
S2:  Yes → save game.bin, quit program | No / Esc → S1
S3:  Back / Esc → S1 | Start → S9 → S10 (new campaign at the chosen mission)
S4, S8:  Back / Esc → previous menu
S6:  → S7 (configure keys); Back/OK → previous menu (details §3.6)

S10 Playing:  Esc → S13 | P/Pause ↔ S11 | ShowTutorialHint → S12 → S10
              all lives lost → S14 | EndLevel: mission 1..19 → S15, mission 20 → S16
S13 In-game:  Resume / Esc / right click → S10 | Options → S6 (pushed over S13)
              Quit → S9 (attract level) → S1          (no banking, no high-score check)
S14 Game over (buttons appear after 2 s):
              Restart → S9 → S10 same mission, lives as at its start
              Quit → bank → S9 (attract) → S1, then high-score check → S5 → S4
S15 Mission complete:
              Continue → bank → S9 → S10 next mission
              Restart → S9 → S10 same mission (no banking)
              Quit → S9 (attract) → S1 (no banking, no high-score check)
S16 Game complete:
              Continue → bank → S9 (attract) → S1, then high-score check → S5 → S4
S5 Name entry: OK / Enter → insert entry → S4 (over S1)
```

All VERIFIED-CODE: 0x429b20, 0x426a20, 0x42b3b0, 0x428d40, 0x4275e0, 0x426270, 0x426df0,
0x42a300, 0x40bde0, 0x40be90, 0x408f10.

Returning to the main menu always rebuilds it and replaces the whole menu stack
(`M_ShowMainMenu` 0x42a260). Loading the attract level after Quit reuses the attract level
chosen at boot (0x1fdb35c), it is not re-drawn at random. Loading any level runs the loading
screen (S9) and restarts that level's music.

"Esc" below always means the Esc key (0x1B) and the right mouse button (code 201, 0xC9);
the generic menu handler treats both as "back" (pop the current menu) unless the screen's own
key handler swallows them (VERIFIED-CODE 0x429630).

### 1.4 What the world does behind each screen

- S0: no level is loaded yet; the intro frame draws only 2D (0x408260). The attract level is
  loaded (with its loading screen) when the last page ends.
- S1–S8 at boot or after quitting: the attract level runs normally (not paused): its scripts
  and particles animate, the camera sways (engine-behaviour.md §9.6), no players, no HUD.
  The cheat buffer is not fed on intermission levels.
- S6/S7 opened from the in-game menu, S11–S16: the mission is paused (time frozen, see
  engine-behaviour.md §1.2) and rendered every frame; for S13–S16 the HUD is hidden.
- While any menu is on the stack, every key and mouse button goes to the menu system first
  and gameplay never sees it (0x408f10 returns after `UI_KeyEvent` 0x429190 accepts it). The
  hotkeys F5–F9, P and Esc are therefore inactive while a menu is up. VERIFIED-CODE.

## 2. Menu system (widgets and input)

### 2.1 Menu stack (VERIFIED-CODE 0x4292f0, 0x429360, 0x42a260)

- A stack of up to 16 menus; the top one is "current" and is the only one drawn and the only
  one receiving input. Pushing or popping resets the menu time `mt` to 0 and clears the
  focused item of the newly current menu, then re-runs the hover test.
- Every menu is (re)built by its builder right before it is pushed, so all widget state is
  re-initialised on every opening unless the builder copies it from live settings.
- A menu has: up to 64 items; a focused item index (−1 none); a hovered item index and hover
  time; an optional custom draw function (otherwise the items are drawn in insertion order);
  an optional key function (otherwise the generic handler of §2.4).
- Menus are drawn after the 3D scene and after the HUD, and last of all comes the mouse
  cursor (§2.7).

### 2.2 Items

Each item has a type, an id (passed to the menu's callback), flags, a label, an optional
tooltip, a position (x, y), a hit rectangle and an optional callback receiving (item, event)
with event 1 = activate, 2 = gains focus, 3 = loses focus. VERIFIED-CODE (0x429490,
0x429200, 0x429630).

| Type | Widget | Hit rectangle (set when added, 0x429490) |
|---|---|---|
| 0 | text label | width 10 × character count, height 16, anchored by the align flags |
| 1 | image button | (x, y, w, h) of the picture, anchored by the align flags |
| 2 | edit field | (x, y, w, 16); the text buffer is cleared when the item is added |
| 4 | list | (x, y, w, h) |
| 5 | spinner | x − label width − 16 … x + 16 + widest value, y … y + 16 |
| 6 | slider | x − 10 × label length − 10 … x + 138, y … y + 16 |
| 7 | helicopter grid | (x, y, 352, 136) |

Flags: 1 focused; 2 disabled (grey, skipped by hit test and keyboard navigation); 4 not
drawn; 0x2000 right-align the label on x; 0x4000 centre it on x; 0x10000 brace markup in the
label; 0x20000 no hover sound. Flags 0x100 and 0x1000 are set by some builders but have no
effect. Buttons that are "hidden" use flags 2 | 4. VERIFIED-CODE.

### 2.3 Focus, hover and sounds

- Focus follows the mouse: each frame the item under the cursor (first in insertion order
  whose hit rectangle contains the cursor, disabled items excluded) becomes focused, and the
  previous one gets event 3, the new one event 2 (0x429200). Moving the cursor over empty
  space keeps the previous focus.
- When the hovered item changes and the new item does not have flag 0x20000,
  `sounds\menu2.wav` plays (2D). VERIFIED-CODE 0x429257.
- Activating an item with Enter or a left click plays `sounds\menu1.wav` (2D) and sends
  event 1. VERIFIED-CODE 0x4296f9, 0x429722.
- Hover time counts while the cursor does not move; after 1 s over an item with a tooltip,
  and only when `ShowHints` = 1, the tooltip is drawn (§2.6).

### 2.4 Generic keyboard and mouse handling (VERIFIED-CODE 0x429190, 0x429630)

Only key and button presses reach menus, except the release of the left button, which ends a
slider drag. Key codes are Windows virtual keys extended as in engine-behaviour.md §7.2
(200–202 mouse buttons, 239/240 wheel, 241–244 stick).

1. Esc (0x1B) or right button (201): pop the current menu. Checked before anything else.
2. If nothing is focused: Tab, Up, Down, stick up or stick down focus the first selectable
   item.
3. Otherwise, if the focused item is not disabled, its widget gets the key first (§2.5).
   A spinner, list, edit field or helicopter grid consumes the navigation keys too, so while
   one of them is focused Up/Down (and, except for the list, Tab) do not move the focus. A
   focused slider passes the keys on.
4. Tab, Down, stick down: focus the next non-disabled item (wrapping); Up, stick up: the
   previous one.
5. Enter: menu1.wav and event 1 on the focused item.
6. Left button (200) or joy button 1 (203): the same, but only if the cursor is over an item.

### 2.5 Widgets

All VERIFIED-CODE at the cited addresses; draws use the additive font (§2.8) unless stated.

**Text label (type 0, 0x423740).** Drawn at (x, y) with its align flags. Grey 0x808080;
white when focused; 0x404040 when disabled.

**Image button (type 1, draw inline in 0x425a10).** Normal texture drawn at (x, y, w, h) with
its UVs, white, blend ALPHA. When focused, the highlight texture is drawn over the same
rectangle with blend ADD, colour Pulse(2, 0) (1 Hz). Buttons have no text.

**Edit field (type 2, draw 0x426190, keys 0x425e80, characters 0x4260b0).**
- Holds up to 63 characters; accepts character codes 0x20–0x7F, only while the text width is
  below w − 20. Characters are inserted at the cursor, or replace the character under it in
  overwrite mode.
- Keys: Backspace deletes before the cursor, Delete at it; Home/End; Left/Right (and stick
  left/right) move the cursor; Insert toggles overwrite; Enter sends event 1.
- Draw: a filled box in 0x80000060 (dark red, alpha 0.5) behind (x, y, w, 16); the text in
  white at (x, y); when focused, a cursor at x + width of the text before it, blinking with
  a 0.5 s period (visible while (milliseconds / 250) is odd). The cursor glyph is drawn with
  the number routine; which character is not identified (GUESS: `_`).

**List (type 4, init 0x423c90, keys 0x423d30, draw 0x424030).**
- Entries are {text, enabled}. Visible rows = (h − 4) / 20. Selected index and first visible
  row start at 0.
- Draw: background fill (x, y, w, h) black alpha 0x50/255 (≈ 0.31), blend ALPHA. Row k at
  text position (x + 10, y + 4 + 20k). Selected row: bar (x + 4, row y, w − 30, 18) in
  (0.376, 0, 0, 0.5) blend ALPHA, text orange. Enabled rows rust; disabled rows grey 0x808080.
- Scroll bar at the right edge from `menu\scroller_1.tga` (32×128): up arrow (x + w − 22, y,
  32, 37) with t 0.711..1; thumb (x + w − 22, y + 28 + top × (h − 68) / (count − visible),
  32, 24) with t 0.523..0.711; down arrow (x + w − 22, y + h − 32, 32, 39) with t
  0.219..0.523 (all s 0..1). An arrow under the cursor is redrawn from `scroller_2.tga`
  with blend ADD in Pulse(2, 0).
- Keys: Up/Down (and stick) ±1; PgUp/PgDn one page; Home/End first/last; Tab next item. Left
  click on a row selects it if enabled; on an arrow zone (26 px) steps by one; on the track
  sets the first visible row. After any change, while the selected entry is disabled the
  selection moves back by one; the view scrolls to keep it visible. So disabled entries can
  never be selected.

**Spinner (type 5, init 0x4244d0, keys 0x424590, draw 0x4246e0).**
- A list of strings and an index (clamped when drawn).
- Draw: label right-aligned ending at x − 10, current value at x + 10. Rust; orange when
  focused, on a box (hit rectangle grown by 2 px left/right, 1 px up) in 0x80000060 blend
  ALPHA; 0x404040 when disabled.
- Next value (wrapping): Enter, Right, left click, joy 1, wheel up (240). Previous (wrapping):
  Left, wheel down (239), stick left. Every key sends event 1 afterwards. Note: while a
  spinner is focused, a left click anywhere advances it (the click is not hit-tested).

**Slider (type 6, init 0x423780, input 0x4237f0, draw 0x423a20).**
(`re/symbols_v170.csv` calls these three "key item"; they are the slider.)
- Integer range [min, max], float value, step (0 means 1).
- Draw: label right-aligned at x − 10 (rust, orange when focused); bar `menu\slider.tga` at
  (x + 8, y − 2, 128, 16); knob `menu\slidebutt_1.tga` (16×32) at (x + ftol((value − min) ×
  128 / (max − min)), y − 10); when focused, `slidebutt_2.tga` over it, blend ADD, Pulse(2, 0).
- Input: Enter, Right, stick right: value + step (clamped); Left, stick left: value − step.
  Left click: left of x + 8 → min; right of x + 136 → max; otherwise value = min + (mouse x −
  x − 8) / 128 × (max − min) (continuous). A click on the knob starts a drag: while the
  button is held and the slider stays hovered, the click is re-sent every frame; release
  ends it. Event 1 is sent after every change.

**Helicopter grid (type 7, init 0x4247c0, click 0x424900, draw 0x4249f0).**
- Ten cells, cell i at (x + 72·(i mod 5), y + 72·(i div 5)), 64×64 hit area each.
- State: player 1's choice and player 2's choice (copied from the player records when the
  item is added), a per-cell locked bit (from the helicopter table), and an alternator
  saying which player the next click is for.
- Input: only a left click selects (arrows and Enter do nothing; Tab moves on). Clicks on
  locked cells are ignored. One player: the click sets player 1's choice. Two players: the
  click goes to player 1 when the alternator is 0, to player 2 when 1, then the alternator
  flips (it is not reset when the menu is rebuilt). The choice is written to the player
  record (+0x88) immediately, so it survives Back.
- Draw, per cell, first pass: an outline rectangle (untextured, polygon lines) 64×64 in
  (0.392, 0, 0); `menu\icon_border_2.tga` (64×64 texture) at (cell − 4, cell − 4, 72, 72),
  blend ADD, white, on player 1's choice, and on player 2's choice in two-player mode when it
  differs; the same border in Pulse(2, 0) under the mouse if the helicopter is unlocked.
- Second pass, the icon, 85×85 at the cell position (shifted 21 px left for helicopters 0, 1,
  2, 5, 7, 8, a per-icon offset table at 0x45699c), white if unlocked, grey 0.251 if locked:
  - i < 9: 3×3 sheet, s0 = (i mod 3)·0.332, t0 = 0.668 − (i div 3)·0.332, size 0.332;
    `menu\icons_2.tga` blend ADD (glow; drawn twice for player 1's choice), then
    `menu\icons_1.tga` blend ALPHA.
  - i = 9: `menu\icons_3.tga` (256×128): glow at s 0.332..0.664, t 0.336..1 blend ADD
    (twice if chosen), body at s 0..0.332, t 0.336..1 blend ALPHA.
- Two players: "P1" at (cell x + 3, cell y + 48) and "P2" at (cell x + 35, cell y + 48) with
  the alpha font (§2.8), red 0xFF0000FF, on the respective choices.
- No names, no 3D preview, no statistics are shown for the helicopters.

### 2.6 Tooltip (VERIFIED-CODE 0x425a10)

Shown when `ShowHints` ≠ 0, the hovered item is not disabled, has tooltip text and has been
hovered for more than 1 s without the mouse moving. Box at (hover x + 4, hover y + 12), size
(text width + 8) × 20; if it would pass x = 800 it moves to x = 800 − text width − 8. Fill
(1, 1, 0.63) blend ALPHA, outline 0x404040, text black (alpha font) at +4, +2 inside; all
three with alpha min((hover time − 1) × 255, 255). Only the Resolution option has a tooltip
("Changes screen resolution.").

### 2.7 Mouse cursor

While a menu is current (VERIFIED-CODE 0x4299f0): unless `UseSystemMouse` = 1, the cursor is
drawn last as `menu\cursor_1.tga` (32×32) blend ALPHA then `menu\cursor_2.tga` blend ADD, both
at (mouse x − 4, mouse y − 2), white (render-pipeline.md §8.4). With `UseSystemMouse` = 1 the
OS cursor is shown while a menu is up and hidden otherwise.

During play with `MouseControl` = 1 and no menu, `gfx\mc_cur.tga` (16×16) is drawn at
(mouse x − 7, mouse y − 7), white, blend ADD, as part of the HUD (0x4041b0).

Mouse coordinates are the window coordinates × 800 / window width, for both axes
(0x4091c0); on non-4:3 windows y does not map to 0..600 (VERIFIED-CODE).

### 2.8 Text routines (VERIFIED-CODE)

Three routines draw text; all use the glyph cells and advance table of render-pipeline.md
§8.3.

| Routine | Texture, blend | Size | Colour | Flags |
|---|---|---|---|---|
| additive font 0x425290 | `gfx\ui\font.tga` (bytes ≥ 0x80: `font_rus.tga`, not shipped), ADD | glyph 30×15, pen advance from the table | call colour, alpha forced to 1 | 0x4000 centre, 0x2000 right-align, 0x10000 markup |
| alpha font 0x425750 | `gfx\ui\font_alpha.tga`, ALPHA | same | call colour including its alpha | 0x4000, 0x2000; no markup |
| number font 0x4258f0 | `font.tga` whole 32×16 cells, ADD | 32·s × 16·s, advance 14·s, x truncated after each glyph | current colour | none (left-aligned) |

- Markup: `{` switches to white (1, 1, 1), `}` back to the call colour; neither is drawn and
  neither advances the pen, but both are counted (6 px each, their table advance) when the
  width is measured for centring or right-alignment. A reimplementation that wants
  pixel-identical placement keeps this; otherwise measure without braces (§8).
- The additive font is used for nearly all text; black text and shadows need the alpha
  font, because additive black is invisible. Shadows are drawn first, black, 2 px right and
  2 px down.
- `re/symbols_v170.csv` calls 0x425750 "UI_DrawStringCentered"; it is the alpha-font routine.

## 3. Screens in detail

### 3.1 Shared frame elements

**Letterbox** (Start Game, Exit confirmation, Options, Controls, Top Scores, Name entry,
In-game menu, Information; VERIFIED-CODE 0x42b470, 0x426a70, 0x42abb0, 0x423230, 0x42c1d0,
0x42a360, 0x428e10, 0x4289b0): two opaque black fills (0, 0, 800, 100) and (0, 500, 800,
100), blend 0; then two 16-px rules from `menu\corner.tga` (128×16), white, blend ALPHA:
(0, 487, 800, 16) with UV (0.97, 0, 0.99, 0.97), and (0, 97, 800, 16) with UV (0.99, 0.97,
0.97, 0), i.e. the same thin slice of the texture stretched along the whole width, the top
one mirrored. The level shows through between y = 100 and 500.

The main menu uses the same bars with the full corner ornament (§3.3). Mission Complete, Game
Complete and Game Over have no letterbox.

**Three-layer header** (Start Game, Options, Controls, Top Scores): three textures drawn over
the same rectangle, UV (0, 0, 1, 1), white: `<name>_2.tga` blend ALPHA, `<name>_0.tga` blend
ADD, `<name>_1.tga` blend ALPHA. VERIFIED-CODE (see each screen for the rectangle).

**Panel**: a fill in 0x50000000 (black, alpha 0.31), blend ALPHA, behind a screen's widgets.

### 3.2 Intro logo pages (S0; VERIFIED-CODE 0x408070, 0x408260, 0x407b00..0x407d00)

- Built from Settings.xml `<Intros>` in document order: `<BuiltIn name="DivoGames"/>` makes
  the DivoGames page; `<Image name=… >` with a `<BackColor r g b/>` child makes an image page.
  Other tags are ignored; the shipped file has one `ImageTemp` (ignored) and the DivoGames
  page (VERIFIED-DATA). If `ShowLogo` = 0 or the list is empty, the intro is skipped.
- During the intro the brightness is forced to 0.5 and restored at the end. Each page has a
  clock starting at −0.5 s and advancing by frametime × speed; speed starts at 1 per page
  and every key press or mouse button press multiplies it by 4 (engine-behaviour.md §1.3).
- **Image page** (lasts until clock ≥ 6): clock ≤ 0: full-screen white. Otherwise:
  full-screen fill in the BackColor, opaque; the image at its natural size centred on
  (400, 300), blend ALPHA; while clock < 1 a white full-screen fill with alpha (1 − clock) ×
  255 over it (fade from white); after clock 5 a white fill with alpha (clock − 5) × 255
  (fade to white).
- **DivoGames page** (lasts until clock ≥ 8), all from `gfx\logo.tga` (256×256): full-screen
  white, opaque. For clock > 0, with f = min(clock / 2, 1): the emblem, UV (0.7, 0, 1, 1),
  at (400 − 52f, 166, 104f, 268) blend 0, white (it grows horizontally from the centre).
  From clock 2: two text pieces, blend ALPHA, colour white with alpha 255 × (clock − 2) / 2
  until clock 4, then opaque: (322, 180, 164, 52) with UV (0.01, 0.8, 0.64, 1.0) and (318,
  232, 164, 204) with UV (0.01, 0, 0.64, 0.8). After clock 7, a white full-screen fill with
  alpha (clock − 7) × 255.
- After the last page the attract level is started (`G_BeginLevel` on the attract level
  chosen at boot).

### 3.3 Main menu (S1; VERIFIED-CODE 0x429f60, 0x429c20, 0x429b20, 0x429bf0)

Buttons: five image buttons, 320 px wide at x = 240, normal `menu\mmenu_1.tga`, highlight
`menu\mmenu_2.tga` (both 256×256; the atlas holds seven 35-texel rows):

| id | Button | Rectangle | t0..t1 (s 0..1) |
|---|---|---|---|
| 1 | Start Game | (240, 250, 320, 33) | 0.8711..1.0 |
| 2 | Top Scores | (240, 283, 320, 35) | 0.7344..0.8711 |
| 3 | Options | (240, 318, 320, 35) | 0.5977..0.7344 |
| 4 | Information | (240, 353, 320, 35) | 0.4610..0.5977 |
| 5 | Exit | (240, 388, 320, 35) | 0.3242..0.4610 |

The two remaining atlas rows are the in-game menu's Resume (t 0.1875..0.3242) and Quit
(t 0.0506..0.1875) (§3.13). Button labels are inferred from order, the manual and the atlas
use (GUESS for the exact wording; the pictures are in the texture).

Drawing order:
1. Letterbox bars; corner ornament from `menu\corner.tga`: bottom-left corner (0, 487, 128,
   16) UV (0, 0, 0.99, 0.97); bottom rule (128, 487, 672, 16) s 0.97..0.99; top-right corner
   (672, 97, 128, 16) UV (0.99, 0.97, 0, 0) (the corner rotated 180°); top rule (0, 97, 672,
   16) UV (0.99, 0.97, 0.97, 0).
2. Settings.xml `<Info version>` at (20, 580) left-aligned and `copyright` at (780, 580)
   right-aligned, additive font, grey 0x808080, each only if non-empty. Shipped: "v 1.70" and
   the copyright line (VERIFIED-DATA).
3. Settings.xml `<Logotypes>`: each `<Image x y name invertAxisX invertAxisY>` at its
   natural size, blend ALPHA, white, at x (or 800 − x − width when invertAxisX = 1) and y (or
   600 − y − height when invertAxisY = 1). Shipped: `Gfx\logo2s.tga` (256×64, only in
   `data\gfx` of the install, not in a pak) at (534, 516) (VERIFIED-DATA).
4. The 2D queue so far is flushed with the 3D view (0x40e0d0).
5. The **3D banner** (0x401b30): object `banner` (`objects\banner.obj`: model
   `models\banner\banner.mdl`, skin `banner2.tga`) rendered alone in a viewport covering
   the top 200 virtual pixels (0, 0, 800, 200), FOV 60, no terrain, depth buffer cleared
   first. Entity at (−34, 7, −30), angles (95 + 7 sin mt, 3 sin(0.7 mt + 0.5), 0) degrees
   with mt in seconds used as radians inside sin. It is the game's title logo.
6. The buttons.

Actions: Start Game builds and pushes S3; Top Scores pushes S4; Options pushes S6 (after
refreshing the resolution list); Information pushes S8 (and registers the HUD atlases used
for the page icons); Exit pushes S2. Keys: Esc and right click are swallowed (the main menu
cannot be closed); everything else is generic.

Settings.xml `<Demo>` (Purchase button, nag screen) is parsed but never used in v1.70
(VERIFIED-CODE: no reader besides the parser). `<PostScores>` and `<CheckCD>` belong to the
dropped features.

### 3.4 Start Game (S3; VERIFIED-CODE 0x42b710, 0x42b470, 0x42b3b0, 0x408c40)

| Element | Type | Position | Content |
|---|---|---|---|
| header | 3-layer | (210, 63, 380, 64) | `menu\starth_*.tga` |
| "Choose mission:" | text | (300, 140), left | orange |
| mission list | list | (295, 160, 420, 106), 5 rows | all 20 missions in table order, labelled with the level's `name` from levels.txt ("Mission 1: Tutorial" …); locked missions disabled (grey) |
| Difficulty: | spinner | (160, 184), label right-aligned | Very Easy, Easy, Normal, Hard, Nightmare; reset to Normal on every opening |
| Game mode: | spinner | (160, 224) | "1 Player", "2 Players"; starts from the current two-player flag; changing it sets the flag immediately (so the grid shows P2 at once) |
| helicopter grid | grid | (224, 304) | §2.5 |
| Back | button id 1 | (50, 450, 128, 64) | `menu\back_1/2.tga`, UV 0..1 |
| Start | button id 2 | (605, 450, 150, 64) | `menu\start_1/2.tga` (128×64 stretched), UV 0..1 |

- The mission list selection starts at mission 1 each time (not the last unlocked one).
- Back and Esc pop the menu.
- Start: stores the difficulty index; frees the attract level; pops every menu; sets the
  two-player flag and the player count (1 or 2); mission index = list selection; runs New
  Campaign (§5.1), which loads the mission (loading screen) and starts play.

### 3.5 Exit confirmation (S2; VERIFIED-CODE 0x426c90, 0x426a70, 0x426a20)

Letterbox; panel (210, 220, 380, 160); "Are you sure you want to quit?" centred at (400,
240), orange. Buttons from `menu\yesno_1/2.tga` (128×64): Yes id 1 at (250, 320, 95, 64),
UV (0, 0, 0.5278, 1); No id 2 at (465, 320, 85, 64), UV (0.5278, 0, 1, 1). Yes: free the
level, save game.bin, close the window (the exit path then writes config.ini, §6). No and Esc
pop the menu.

### 3.6 Options (S6; VERIFIED-CODE 0x42aea0, 0x42abb0, 0x42a710, 0x42a8e0, 0x42aa60, 0x42a840)

Frame: letterbox; header `menu\optionsh_*.tga` at (272, 63, 256, 64); panel (210, 140, 380,
290). All labels right-aligned on x = 400 (spinner values at 410, slider bars at 408).

| y | Item | Type | Values | When it takes effect |
|---|---|---|---|---|
| 160 | Resolution: | spinner | 640 x 480, 800 x 600, 1024 x 768, 1152 x 864, 1280 x 960, 1280 x 1024, 1600 x 1200, 1920 x 1440, 2048 x 1536 (only the leading run of modes the display supports) | Apply |
| 180 | Refresh rate: | spinner | "default", then "N Hz" for each rate of the chosen mode; reset to default when the resolution changes | Apply |
| 200 | Color Depth: | spinner | Default, 16 bit, 32 bit; disabled and forced to Default while Fullscreen is Off | Apply |
| 220 | Fullscreen: | spinner | Off, On | Apply |
| 240 | Brightness: | slider | 2..10 step 1 (Brightness × 10) | immediately |
| 280 | Sound Volume: | slider | 0..10 (SfxVolume × 10) | immediately |
| 300 | Music Volume: | slider | 0..10 (MusicVolume × 10) | immediately |
| 320 | 3D Sound: | spinner | Off, On | Apply |
| 360 | Camera: | spinner | Low Pitch, Default, High Pitch, Top-Down (camera mode 0..3, engine-behaviour.md §9.3) | immediately |
| 400 | Mouse Control: | spinner | Off, On | immediately, with rebinding (below) |
| — | Configure keys | button id 2 (230, 430, 340, 32) | `menu\confkeys_1/2.tga` (256×32 stretched) | pushes S7 |
| — | Back | button id 1 (50, 450, 128, 64) | `menu\back_1/2.tga` | pops |
| — | Apply / OK | button (600, 450, 160, 64), UV (0, 0, 0.625, 1) of `menu\apply_ok_1/2.tga` | shown only while a pending video or 3D-sound value differs from the live one | video restart |

- Opened during a mission (not on an intermission level), Resolution, Refresh rate, Color
  Depth, Fullscreen and 3D Sound are disabled (grey).
- Back/Esc: pop; pending video and 3D-sound values are discarded (they are reloaded from the
  live settings at the next opening). Brightness, volumes, camera and mouse control were
  already applied.
- Apply: commits the pending values and performs a full restart (0x4206c0): if a level is
  loaded it is freed and game.bin is saved; sound, GL and window are shut down and
  re-created; then the game boots again without the logo pages (`G_Init(1)`): new random
  attract level, main menu, helicopter choices reset to 1 / 0. Apply is only reachable from
  the main menu path, because the video items are disabled in game.
- **Mouse Control rebinding** (0x42a9f0, run for fire, missile and power-up): On means the
  keys 200, 201, 202 (mouse 1–3) are bound to player 1's primary fire, missile and power-up;
  Off means 203, 204, 205 (joy 1–3). The key is put into player 1's slot for that action
  (into key 1 if empty, else replacing key 2), and removed from every other slot of both
  players. (Quirk: when removing from a slot whose key 1 matched, key 1 takes the key 2 value
  of player 1's slot at the same index, even in player 2's record.)
- Nothing is written to config.ini here; see §6.2.

### 3.7 Configure controls (S7; VERIFIED-CODE 0x423520, 0x423230, 0x423160, 0x422f10)

Frame: letterbox; header `menu\controlsh_*.tga` at (242, 63, 316, 64); panel (210, 165,
380, 280). "Controls Set:" spinner at (400, 145) with "Player 1" / "Player 2", always
starting at Player 1; changing it shows the other player's bindings. Back button (50, 450,
128, 64), `menu\back_1/2.tga`.

Rows (label right-aligned at x = 392, keys from x = 408; hit area x 220..580, y − 2..y + 18):

| y | Row | Action bit (engine-behaviour.md §7.2) |
|---|---|---|
| 180 | Primary Attack | 0x001 |
| 200 | Switch Weapon | 0x400 |
| 240 | Missile Attack | 0x002 |
| 260 | Switch Missiles | 0x100 |
| 300 | Use Item | 0x004 |
| 320 | Switch Item | 0x200 |
| 360 | Move Forward | 0x010 |
| 380 | Move Backward | 0x020 |
| 400 | Move Left | 0x040 |
| 420 | Move Right | 0x080 |

- Row text: "<key 1>" or "<key 1> or <key 2>"; "???" when key 1 is unbound (key 2 is then
  not shown). Rust; orange on a 0x80000060 box when focused; grey when disabled.
- Key names (0x422da0): digits and letters as themselves; NumPad0–9; F1–F24; Joy1–Joy32 for
  207–238; a table for the rest (BACKSPACE, TAB, CLEAR, ENTER, SHIFT, CTRL, ALT, CAPS LOCK,
  SPACE, PGUP, PGDOWN, End, Home, Left, Up, Right, Down, INS, DEL, NumLock, Scroll Lock,
  punctuation, Mouse 1–3 for 200–202, joy1–joy4 for 203–206, joyLeft/joyRight/joyUp/joyDown
  for 241–244); anything else as "0x%x" (so the wheel shows as 0xef / 0xf0).
- Capture: activating a row (Enter or click) starts capture; all other rows and Back are
  disabled; a blinking "=" (250 ms) is drawn at x − 5 of the rows. The next press of any key
  or button except Esc is bound: the code is removed from every displayed row, then on the
  chosen row key 2 = old key 1 and key 1 = the new code (the old key 2 is dropped), and the
  row is written to the player record. Esc cancels. Esc can never be bound.
- Backspace or Delete (not capturing) unbinds: key 1 = −1, key 2 = old key 1.
- Quirk: duplicates are removed only from the rows shown, not from the stored bindings
  of other rows; they reappear when the menu is rebuilt (VERIFIED-CODE). A reimplementation
  should remove the duplicate from the stored bindings too (§8).

### 3.8 Mission Complete (S15; VERIFIED-CODE 0x4267a0, 0x426360, 0x426270, 0x426330)

Over the frozen mission, HUD hidden, no letterbox.

- Title from `menu\miscompl_1.tga` (256×128, two words stacked in the texture), drawn as two
  pieces, blend ALPHA, white: (124, 64, 245, 64) with UV (0, 0.5, 0.765625, 1) and (369, 64,
  320, 64) with UV (0, 0, 1, 0.5). Then the same two pieces from `miscompl_2.tga` with blend
  ADD in Pulse(0.25, 0) (8 s period).
- One-player mode only: statistics box, fill (210, 156, 380, 120), black with alpha
  min(80·mt, 80)/255, blend ALPHA; then, additive font, orange:

| Appears when | Label at (240, y), left | Value at (560, y), right-aligned |
|---|---|---|
| mt > 1.0 | y 176: "Enemies destroyed:" | "N%" = kills × 100 / enemy total (integer division; omitted when the total is 0) |
| mt > 1.4 | y 206: "Stars collected:" | "a/b" = ftol(p_stars) / star total |
| mt > 1.8 | y 236: "Your current rank:" | rank name of v (§5.11) |

  All values are player 1's.
- Helicopter grid (type 7) at (224, 304): the player may pick the helicopter for the next
  mission (including the one just unlocked). Two-player alternation as in §2.5.
- Buttons (image, 64 high, y = 470): Restart id 2 at (64, 470, 210, 64) from
  `menu\restart_1/2.tga` UV (0, 0, 0.8203, 1); Quit id 1 at (332, 470, 128, 64) from
  `menu\quit_1/2.tga`; Continue id 3 at (482, 470, 256, 64) from `menu\continue_1/2.tga`.
- Actions: Continue: pop all menus, free the level, bank (§5.4), mission index + 1 (mod
  20), start that mission. Restart: free the level, pop, restart the same mission. Quit: free
  the level, load the attract level, pop, main menu (no banking).
- Esc and right click are swallowed (key handler 0x426330).

### 3.9 Game Complete (S16; VERIFIED-CODE 0x4274f0, 0x4270a0, 0x426e70, 0x426df0, 0x426e40)

After mission 20's `EndLevel`. Over the frozen mission, no letterbox.

- The same two-piece title as §3.8 (from `miscompl_1/2.tga`, same rectangles and pulse).
- Congratulation text, starting at mt ≥ 1: five lines ("Congratulations!", an empty line,
  and three lines of text, all in the executable at 0x449d6c..0x449df0), typed one line at a
  time at 8 characters per second (a line of n characters takes n/8 s), `sounds\type.wav`
  (2D) per new non-space character. Each line is centred on x = 400 using the full line's
  width (so the text types out from the left edge of its final position), at y = 160 + 18i,
  white additive font, with a black alpha-font shadow at +2, +2.
- One-player mode only: the statistics box as in §3.8 but at (210, 296, 380, 120), rows at
  y = 316, 346, 376, same timings (mt > 1.0, 1.4, 1.8).
- One button, Continue id 1, at (272, 470, 256, 64), `menu\continue_1/2.tga`: free the level,
  bank, load the attract level, pop, main menu, high-score check (§3.12).
- Esc and right click are swallowed.

### 3.10 Game Over (S14; VERIFIED-CODE 0x408c80, 0x4278a0, 0x427670, 0x4275e0)

Over the frozen mission, HUD hidden, no letterbox. The music jumps to pattern order 35 of the
current module.

- Red tint: a full-screen fill (0, 0, 800, 600) with blend FILTER (screen × colour). During
  the first 2 s the colour is (1, g, g) with g = (255 − 64·mt)/255, so the scene fades from
  normal to red-tinted; from then on (1, 0.5, 0.5).
- Title `menu\gameover_0.tga` (256×64) at (230, 200, 340, 64), UV 0..1, blend ALPHA, white
  with alpha 128·mt/255 during the first 2 s (reaching 1 at 2 s), then opaque; over it
  `menu\gameover_3.tga` same rectangle, blend ADD, Pulse(0.25, π/2) (8 s period, starting
  dark).
- Buttons are hidden and disabled for the first 2 s: Restart id 1 at (130, 450, 210, 64),
  `menu\restart_1/2.tga` UV (0, 0, 0.8203, 1); Quit id 2 at (542, 450, 128, 64),
  `menu\quit_1/2.tga`.
- Restart: free the level, pop, restart the same mission (§5.6). Quit: free the level, bank,
  load the attract level, pop, main menu, high-score check.
- Esc and right click are swallowed.

### 3.11 Top Scores (S4; VERIFIED-CODE 0x42c660, 0x42c1d0)

Letterbox; header `menu\topscores_*.tga` at (225, 63, 350, 64); panel (130, 140, 540, 298);
column bar (130, 140, 540, 20) in 0x80000060 blend ALPHA. Column titles at y = 142, orange:
"#" at x 138, "Name" at 168, "Score" at 393, "Rank" at 533. Rows i = 0..14 at y = 164 + 18i:
rank number i + 1 at 138 (orange), name at 168 (rust), score at 393 with the number font
scale 1 (current colour), rank name at 533 (rust). Back id 1 at (50, 450, 128, 64),
`menu\back_1/2.tga`, pops. (A "Post" button, `menu\post_1/2.tga` at (302, 458, 196, 32),
exists only when online posting is allowed; dropped.) The newly inserted entry is not
highlighted. Esc pops.

### 3.12 High-score check and name entry (S5; VERIFIED-CODE 0x40bde0, 0x42a580, 0x42a360, 0x42a300, 0x40be90)

- Check: only in one-player mode. The banked score of player 1 qualifies if it is ≥ the
  score of some table entry (15 entries sorted descending; the first entry it is ≥ to is its
  slot). If it qualifies, the name-entry menu is pushed with the edit field focused.
- Name entry: letterbox; panel (210, 220, 380, 160); "Please enter your name:" centred at
  (400, 240), orange; edit field id 2 at (275, 285), width 250, empty; OK button id 1 at
  (350, 320, 100, 64) from `menu\apply_ok_1/2.tga` UV (0.6094, 0, 1, 1). Enter in the field or
  OK: pop, insert the entry, then build and push Top Scores. Esc and right click are
  swallowed (there is no way to skip; an empty name is accepted).
- Insert: slot = first entry whose score ≤ the banked score; the entries from the slot down
  move one place (the last is dropped); the new entry is {name (up to 31 characters), banked
  score, rank index of accumulator × rank factor (0 if a cheat was used)}.

### 3.13 In-game menu (S13; VERIFIED-CODE 0x408f10, 0x428fa0, 0x428e10, 0x428d40, 0x428dd0)

Opened by Esc during play (sets HUD-hidden and paused). Letterbox (no panel, no header).
Buttons from the main-menu atlas `menu\mmenu_1/2.tga`, 320×35, x = 240:

| id | Button | y | t0..t1 |
|---|---|---|---|
| 1 | Resume | 270 | 0.1875..0.3242 |
| 2 | Options | 305 | 0.5977..0.7344 |
| 3 | Quit | 340 | 0.0506..0.1875 |

- Resume, Esc, right click: pop; clear paused and HUD-hidden; clear `p_action` of both
  players.
- Options: pushes S6 over this menu (video items disabled).
- Quit: free the level, load the attract level, pop, main menu. No banking, no high-score
  check; the campaign is lost.

### 3.14 Information (S8; VERIFIED-CODE 0x428d00, 0x428c00, 0x4289b0, 0x427a30, 0x427aa0..0x4288e0)

Letterbox. A "Page:" spinner at (400, 440) (label right-aligned) with values "1 of 10" ..
"10 of 10" selects the page; Back id 1 at (50, 450, 128, 64). Grey hints "PgUp - Previous
Page" at (570, 460) and "PgDown - Next Page" at (570, 480). Keys: PgUp or Left = previous
page, PgDn or Right = next page, both wrapping; other keys generic (Esc pops).

Page layout: title at (60, 80), white; body lines from y = 144 every 18 px, orange, with
markup, so `{…}` names are white. Pages with icons indent the text to x = 140 and draw one
66×35 icon per 4-line paragraph at (60, 154 + 72k), blend ADD, from the HUD atlases
(`gfx\ui\weapons.tga`, `missiles.tga`, `items.tga`, same UVs as the HUD, §4.4).

| Page | Title (as in the executable) | Body | Icons |
|---|---|---|---|
| 1 | THE STORY (Page 1 of 2) | 15 lines of story | none, text at x 60 |
| 2 | THE STORY (Page 2 of 2) | 10 lines | none |
| 3 | OVERVIEW | 10 lines, gameplay goals | none |
| 4 | PRIMARY WEAPONS (Page 1 of 3) | Machine Gun, Impulse Gun, Plasma Cannon, Laser Gun | weapons 0–3 |
| 5 | PRIMARY WEAPONS (Page 2 of 3) | Big Plasma Gun, G.O.R.O.X., Laser, Wave Gun | 4 weapon icons |
| 6 | PRIMARY WEAPONS (Page 3 of 3) | Meteorite Gun, Flamethrower | 2 weapon icons |
| 7 | MISSILES (Page 1 of 2) | Small, Big, Small Heat Seeking, Big Heat Seeking Missiles | missiles 0–3 |
| 8 | MISSILES (Page 2 of 2) | M.A.D. Missiles | missile 4 |
| 9 | ITEMS | Cluster Bomb, Nuclear Bomb, Rocket Strike, Lightning Bomb | power-up icons |
| 10 | CREDITS | four role/name pairs, centred on x = 400 from y = 144 | none; title centred at (400, 144) |

The page texts are compiled into the executable (string addresses 0x449ea4..0x44b07c), not
in the data files. Which icon UV each paragraph uses was not traced per paragraph (GUESS:
the HUD index of the item described).

### 3.15 Tutorial hint box (S12; VERIFIED-CODE 0x41c4c0, 0x42c000, 0x42beb0, 0x42bcc0, 0x42bd00, 0x42bd40)

`ShowTutorialHint(text)`:

- Sets paused. Does **not** set HUD-hidden: the frozen HUD stays visible behind the box.
  `ShowHints` is not consulted.
- Splits the text at every `^` into lines (at most 16; further text dropped; a line of 64
  or more characters overflows in the original: clamp at 63).
- Box: W = max(360, widest line + 40), H = max(160, 18 × lines + 80), left = ftol((800 −
  W) / 2), top = ftol((600 − H) / 2). No frame texture: a plain fill.
- Opening (box time u < 0.3 s, f = u / 0.3): only the fill, at x = ftol(400 − (400 − left)·f),
  width W·f, full height, black with alpha ftol(80f)/255: the box grows horizontally from the
  centre. Then: fill (left, top, W, H) in 0x50000000 blend ALPHA; each line i centred on x =
  400 at y = top + 20 + 18i, additive font, orange, markup on: `{…}` spans are white. The
  shipped hint texts put key and item names in braces, e.g. "…pressing^{X} key (by
  default)." (VERIFIED-DATA, `scripts\items\help\*.scr`).
- OK button (type 1) at (350, top + H − 60, 100, 64), `menu\apply_ok_1/2.tga` UV (0.6094,
  0, 1, 1); hidden until the opening animation ends.
- Close: Enter, Esc or Space (key handler), or activating OK. Closing clears paused, sets
  `p_action` = 0 for both players and pops the box. (Original quirk: the right mouse button
  reaches the generic handler, which pops the box without clearing paused, leaving the game
  paused until P is pressed. Treat it like Esc.)
- The line-justification factors the builder computes are never used; lines are centred
  (corrects engine-behaviour.md §11.3).

### 3.16 Loading screen (S9; VERIFIED-CODE 0x404230)

Drawn and presented immediately at each loading step of `G_StartLevel` (engine-behaviour.md
§10.2): full-screen opaque black; unless the level being loaded is an intermission level,
`menu\loading.tga` (256×256) at (272, 172), blend ALPHA; a progress line (10, 595, progress ×
780, 1) in (0.314, 0, 0), opaque. Progress runs from 0 to 1 in steps set by the loaders.

### 3.17 Pause (S11)

P or Pause toggles the paused flag (0x408b10) when no menu is open. Nothing is drawn: no
"paused" text; the HUD stays. Unpausing clears `p_action` of both players. VERIFIED-CODE.

## 4. HUD

### 4.1 When and in what order (VERIFIED-CODE 0x4041b0, 0x408df0)

The HUD is drawn when the level is not an intermission level and HUD-hidden is clear;
pause does not hide it. Order within a frame: entity pass → HUD (level-name typewriter; the
one- or two-player HUD; cheat message; mouse-control cursor if `MouseControl` = 1 and no menu
cursor is shown) → 3D view and the 2D queue → menus and menu cursor.

HUD textures (256×128 unless stated, VERIFIED-DATA): `gfx\ui\mainbar.tga` (frames),
`gfx\ui\life.tga` (32×32), `gfx\ui\weapons.tga`, `gfx\ui\missiles.tga`, `gfx\ui\items.tga`,
`gfx\ui\font.tga` (numbers), `gfx\mc_cur.tga` (16×16), and `sounds\type.wav`.

Frame pieces cut from `mainbar.tga` (UVs; pixels from the top-left of the image):

| Piece | UV (s0, t0, s1, t1) | Pixels |
|---|---|---|
| bar frame | (0, 0.8359, 0.7031, 1.0) | x 0–180, rows 0–21 |
| bar fill | (0.0078, 0.6719, fill, 0.8359) | x 2–174 at full, rows 21–42 |
| box frame | (0, 0.375, 0.2734, 0.6719) | x 0–70, rows 42–80 |

"Mirrored" below means s0 and s1 swapped (the piece is flipped horizontally).

Colours: frames grey 0.502 (0x80) blend ADD; lives grey 0.627 (0xA0) ADD; fill white ADD;
count numbers (0.816, 0.251, 0) for the selected missile or power-up type and (0.502,
0.031, 0) for the others, ADD; score number (0.753, 0.188, 0), ADD.

### 4.2 One-player HUD (VERIFIED-CODE 0x401ed0)

Values are player 1's record; f = player entity health / 400 (not clamped: above 400 the
fill overflows, below 0 it gets a negative width; clamp to [0, 1] in a reimplementation).

| Element | Rectangle | Content |
|---|---|---|
| Health frame | (10, 10, 180, 21) | bar frame |
| Health fill | (12, 10, 172·f, 21) | bar fill with s1 = f × 174/256 |
| Weapon box | (10, 32, 70, 39) | box frame |
| Weapon icon | (11, 35, 66, 35) | `weapons.tga`, UV from §4.4 by `p_weapon`, ADD; only if round(p_weapon) < 20 |
| Missile list | frame (10, 73 + 41n, 70, 39) per missile type k = 0..4 with count ≠ 0, in type order, packed (n counts only the shown ones); the selected type's frame is drawn twice (brighter) | icon (11, y + 3, 66, 35) from `missiles.tga`, blend **ALPHA**; count always drawn: number font scale 0.75 at x = ftol(76 − 10.5 × digits), y + 3 (right-aligned to x 76) |
| Score frame | (610, 10, 180, 21), mirrored | bar frame |
| Score | number font scale 1 at (630, 12), left-aligned | ftol(p_scores) + banked score |
| Power-up list | frame (720, 32 + 41n, 70, 39) mirrored, per slot k = 0..15 with count ≠ 0, packed; selected drawn twice | icon (721, 35 + 41n, 66, 35) from `items.tga` for kinds 0–3 (kind 0 blend ADD, kinds 1–3 ALPHA; slots 4–15 have no icon); count only if > 1, scale 0.75 at x = ftol(786 − 10.5 × digits), y = 35 + 41n |
| Lives | (15 + 32i, 555, 32, 32) for i < min(p_lives, 5) | `life.tga` whole |

There is no upgrade-level display, no star counter, no lives number, no low-health warning or
blinking, and no 2D boss bar (VERIFIED-CODE: every function that queues 2D quads was
enumerated). Enemy and boss health is shown only by the 3D sprite bar over the entity
(engine-behaviour.md §6.5; sprites `hbar_full` / `hbar_empty` from `objects\misc.obj`,
textures `gfx\hbar_1.tga` / `hbar_0.tga`, 32 units wide, drawn without depth test).

### 4.3 Two-player HUD (VERIFIED-CODE 0x402a90)

Same pieces, colours and 41-px list step as §4.2.

| Element | Player 1 | Player 2 |
|---|---|---|
| Lives | (15 + 32i, 555) | (753 − 32i, 555) |
| Health frame | (10, 10) | (610, 10) mirrored |
| Health fill | (12, 10, 172·f, 21) | (788 − 172·f, 10, 172·f, 21), UV mirrored (grows from the right) |
| Score frame | (10, 32) not mirrored | (610, 32) mirrored |
| Score | right-aligned: x = 170 − 14 × digits, y 34 | left-aligned at (630, 34) |
| Weapon box / icon | (10, 54) mirrored / (11, 57) | (720, 54) / (721, 57) |
| Missiles | frames (10, 95 + 41n) mirrored; icon (11, y + 3); count at ftol(76 − 10.5 × digits), y + 3 | frames (720, 95 + 41n); icon (721, y + 3); count at ftol(786 − 10.5 × digits) |
| Power-ups | frames (82, 54 + 41n) mirrored; icon (86, 57 + 41n); count at ftol(148 − 10.5 × digits) | frames (648, 54 + 41n) mirrored; icon (649, 57 + 41n); count at ftol(714 − 10.5 × digits) |

### 4.4 Icon atlases (VERIFIED-CODE, tables read from the executable)

Weapons (`weapons.tga`, table 0x457ae0, 20 entries; 10–19 are all zero, i.e. no picture):

| Weapon | s | t |
|---|---|---|
| 0 machine gun | 0–0.258 | 0.727–1.0 |
| 1 impulse | 0.258–0.516 | 0.727–1.0 |
| 2 plasma | 0.516–0.774 | 0.727–1.0 |
| 3 laser gun | 0.774–0.998 | 0.727–1.0 |
| 4 big laser | 0–0.258 | 0.18–0.453 |
| 5 BPG | 0.516–0.774 | 0.453–0.727 |
| 6 GOROX | 0.774–0.998 | 0.453–0.727 |
| 7 meteorite | 0–0.258 | 0.453–0.727 |
| 8 wave | 0.258–0.516 | 0.453–0.727 |
| 9 flamethrower | 0.258–0.516 | 0.18–0.453 |

(Weapon names from engine-behaviour.md §8.2.)

Missiles (`missiles.tga`, table 0x457c20): 0: s 0–0.258, t 0.727–1.0; 1: 0.516–0.774,
0.727–1.0; 2: 0.258–0.516, 0.727–1.0; 3: 0.774–0.998, 0.727–1.0; 4: 0–0.258, 0.501–0.727
(29 rows stretched to 35).

Power-ups (`items.tga`, constants in the draw code): 0 lightning: s 0–0.258, t
0.453–0.727; 1 nuclear bomb: 0.774–0.998, 0.727–1.0; 2 rocket strike: 0.258–0.516,
0.727–1.0; 3 cluster bomb: 0.516–0.774, 0.727–1.0.

### 4.5 Level-name typewriter (VERIFIED-CODE 0x401c60)

- τ = level clock − 1 (the level clock stops while paused). Nothing before τ = 0.
- Text = the level's `name`; D = character count / 8. Nothing once τ > D + 4.
- While τ < D, only the first ftol(8τ) + 1 characters are shown; whenever that index changes
  and the new character is not a space, `sounds\type.wav` plays (2D). The first character
  makes no sound.
- Position: x = ftol(400 − W/2) where W is the width of the **full** name (so the text grows
  rightwards from its final left edge), main text at y = 500 (additive font, white), black
  shadow with the alpha font at (x + 2, 502).
- Fade: for D + 3 < τ ≤ D + 4, a = 255 × (1 − (τ − D − 3)) is the shadow's alpha and the
  main text's grey level.

### 4.6 Messages (VERIFIED-CODE 0x401330, 0x4014c0)

Only the cheat handler posts messages ("God Mode: Enabled", "God Mode: Disabled", "All Lives:
Enabled", "All Weapons: Enabled", "All Missiles: Enabled", "All Power-Ups: Enabled"). A
message shows for 3 s (its timer runs even while paused): main text centred at y = 555
(additive font, white), black alpha-font shadow at (+2, +2); after 2 s it fades out over 1 s
like the typewriter. A new message replaces the old one. There is no other in-game message
system (mission objectives, "get ready" and the like do not exist).

### 4.7 Tutorial hints

See §3.15; the hint box is a menu, not part of the HUD.

### 4.8 Unused overlay

A full-screen fade overlay (0x404370) is called every frame but its inputs are never
written; it never shows (VERIFIED-CODE). Omit it.

### 4.9 Answers to issue 060 (HUD gaps), point by point

Issue 060 (`docs/spec/issues/060-hud-gaps.md` on main) lists the choices the HUD
implementation made. The original's behaviour, all VERIFIED-CODE at the cited addresses:

1. **Blend modes** (060 choice 1). Frames (mainbar pieces), health fill, lives and all numbers:
   ADD. Weapon icons: ADD. Missile icons: ALPHA. Power-up icons: kind 0 ADD, kinds 1–3 ALPHA.
   Mouse-control cursor: ADD. Colours in §4.1 (frames grey 0.502, lives grey 0.627, fill and
   icons white; count numbers (0.816, 0.251, 0) selected / (0.502, 0.031, 0) other; score
   (0.753, 0.188, 0)). 0x401ed0, 0x402a90, 0x4041b0.
2. **Cell sizes and the weapon table** (060 choice 2, "to establish" 1). Icon cells are 66×35
   texels (s step 0.258, t step 0.2734 of a 256×128 texture), drawn at 66×35. The weapon icon
   is **not** cell = index: it comes from the 20-entry UV table at 0x457ae0 (§4.4); weapon 4
   and 9 sit in the third row, 5–8 in the second. Missile table 0x457c20, item UVs
   hard-coded (§4.4). The tables hold UVs only; no frame colour depends on the weapon.
3. **Weapon box frame** (060 choice 3): mainbar texels x 0–70, rows 42–80 (UV (0, 0.375,
   0.2734, 0.6719)); confirmed.
4. **Slots** (060 choice 4). Packed in type order (missiles 0..4, power-up slots 0..15),
   skipping types with a zero count, not in order of acquisition and not at fixed positions
   per type. Power-up frames are drawn mirrored at x = 720 (one player). The selected entry's
   frame is drawn twice with ADD (so it looks brighter); its count uses the "selected" colour.
   Power-up counts are shown only when > 1, missile counts always.
5. **Count position** (060 choice 5): number font scale 0.75 (glyph 24×12, advance 10.5),
   top at frame y + 3, right-aligned so that its right end is at x = 76 (frame x + 66) for the
   left column, 786 for the right column (x = ftol(right − 10.5 × digits)).
6. **Typewriter and message** (060 choice 6). Typewriter: white (additive font), y = 500,
   black alpha-font shadow at +2, +2 (§4.5). Message line: white, centred at y = 555, shadow
   at +2, +2, 3 s with a 1 s fade (§4.6). Not y = 110, not yellow.
7. **Stars, upgrade level, boss bar** (060 choice 7): none of them exists on the original
   HUD. Stars are only reported on the Mission Complete tally (§3.8). The weapon upgrade level
   is not shown anywhere. Boss and big-enemy health use the 3D sprite bar above the entity
   (§4.2). A reimplementation that adds them deviates from the original.
8. **Two-player layout** (060 choice 8): §4.3. Both scores are shown (under each health
   bar), both lives rows, no stars. Player 1's score is right-aligned at x 170; weapon boxes
   at y = 54; power-up columns at x 82 (player 1) and 648 (player 2).
9. **Screen mapping** (060 choice 9): the original stretches the 800×600 space over the
   whole window, non-uniformly (render-pipeline.md §8.1); keeping 4:3 centred is our choice.
10. **Bytes 0x80–0xFF** (060 choice 10): the original draws a 30×15 quad per such byte with
    the handle of `font_rus.tga`; the failed registration returns handle 0 (0x418d60), and
    handle 0 is an untextured filled rectangle, so each such byte becomes a solid rectangle
    in the text colour, additive, and the pen advances by the table value (0 for most of
    0x80–0xBF, so those rectangles overlap). No shipped text (level names, hint scripts)
    contains such bytes (VERIFIED-DATA), so drawing nothing is a safe deviation.
11. **Tutorial hint** (060 choice 11): §3.15. The original cuts lines only at `^` (no width
    wrapping; the ten shipped hints have 2–4 lines of at most 50 characters, widest 506 px
    measured with braces, so the widest box is 546 × 160, VERIFIED-DATA), panel
    0x50000000 black at alpha 0.31 without border texture, orange text with white `{}`
    spans, OK button = right 100 texels of `menu\apply_ok_1.tga` (hover overlay
    `apply_ok_2.tga`, ADD, pulsing), placed at (350, top + H − 60, 100, 64).
12. **Mouse cursors**: in menus, `menu\cursor_1.tga` ALPHA then `menu\cursor_2.tga` ADD at
    (mouse − (4, 2)), unless `UseSystemMouse` (§2.7); during play with MouseControl,
    `gfx\mc_cur.tga` ADD at (mouse − (7, 7)); neither on a touch device needs to be drawn.

## 5. Mission flow and progression

### 5.1 What a campaign is

A campaign starts from the Start Game menu at any unlocked mission (§3.4). "New campaign"
(0x408c40) clears, for both player records, the banked score (+0x165) and the rank
accumulator (+0x169), and sets lives-at-level-start (+0x94) to 2, then runs the level start
`G_BeginLevel` (0x408b30). VERIFIED-CODE (engine-behaviour.md §1.3).

### 5.2 Level start: what is reset (VERIFIED-CODE 0x408b30)

At every level start (new campaign, Continue, Restart), for each of the `player count`
records:

| Field | Value at level start | Carries over between missions? |
|---|---|---|
| `p_scores` (+0x90) | 0 | no; the previous missions' score lives in the banked score |
| `p_lives` (+0x98) | lives-at-level-start (+0x94) | yes, through banking (§5.4) |
| `p_stars` (+0x9c) | 0 | no; folded into the rank accumulator |
| kills this level (+0x16d) | 0 | no |
| `p_counter3` (+0xa8) | 1.0 | no |
| `p_action` (+0x8c) | 0 (player 1 and player 2 always) | no |
| weapon upgrades (+0x115, 20 bytes) | all 0, then machine gun (slot 0) = 1 | **no** |
| `p_weapon` (+0xb0) | 0 (machine gun) | **no** |
| missiles and power-ups | cleared by `G_SpawnPlayer` (0x40b2f0) when the helicopter spawns | **no** |
| banked score (+0x165), rank accumulator (+0x169) | untouched | yes |
| helicopter index (+0x88) | untouched | yes (chosen in Start Game) |

Also cleared: pause, HUD-hidden, game-over flags; the level's enemy total (0x4d52f8) and
maximum score (0x4d52fc). The difficulty factors are re-applied from the stored difficulty
index (clamped to 4). VERIFIED-CODE 0x408b30.

Consequence (VERIFIED-CODE): every mission starts with the machine gun at level 1, no
missiles and no power-ups. Only lives, score (banked) and rank progress carry over. The
player scripts do not save anything across levels (they cannot: the script globals are
per-level). The manual's "you will be given upgrades" refers to pickups inside a mission.

### 5.3 End of mission (`EndLevel`, VERIFIED-CODE 0x407570 → 0x4269b0)

`EndLevel()` sets HUD-hidden and paused, then:

1. If the current level record's `enableHelic` (record +0x110) is in 0..9, the helicopter
   table entry is unlocked (0x457660 + 33·n, byte 0 = 1).
2. The mission table entry (current mission index + 1) mod 20 is unlocked (0x457838).
3. If the current mission index is 19 (the last), the Game Complete screen is built and
   pushed (§3.9); otherwise the Mission Complete screen (§3.8).

Nothing is banked at this point; banking happens when the player presses Continue. The unlock
flags change in memory immediately and are saved with the rest of game.bin at program exit
(§6.1). There is no separate end-of-level score bonus: the mission-complete tally only
displays ratios (VERIFIED-CODE 0x426360; GUESS that no other code adds score at the end).

Scripts reach `EndLevel` through `eol.scr` (autopilot fly-out, then EndLevel) and the three
boss scripts (rcsl-builtins-semantics.md). The cheat `messwiththebest` also calls it.

### 5.4 Banking (VERIFIED-CODE `G_BankScore` 0x40bd10)

For each of the `player count` records:

- banked score = ftol(banked score + p_scores);
- rank accumulator += p_stars / star total + 0.5 × p_scores / maximum level score;
- lives-at-level-start = p_lives.

Banking is called by: Mission Complete → Continue; Game Complete → Continue; Game Over →
Quit. It is **not** called by Mission Complete → Quit or Restart, Game Over → Restart, or the
in-game menu's Quit. So quitting from the mission-complete screen throws the mission's score
away (the unlocks are kept). VERIFIED-CODE 0x426270, 0x4275e0, 0x426df0, 0x428d40.

A level with no stars (star total 0) or no scoring objects (maximum 0) makes these ratios
infinite or NaN in the original. Guard both (treat a zero denominator as a ratio of 0).

### 5.5 Score, maximum score, stars, kills

- `p_scores` grows through `G_AwardScore` (enemy kills, score × difficulty score factor,
  engine-behaviour.md §6.4) and through the point items `i_pts5k` / `i_pts10k`, which add
  5000 / 10000 directly (not scaled by difficulty) (VERIFIED-DATA). Cap 10⁹.
- The maximum level score (0x4d52fc) and the enemy total (0x4d52f8) start at 0 at level
  start and grow while the level runs: each map object instantiated as the scroll advances
  adds its (difficulty-scaled) score value to the maximum and, when it is an enemy (class 2)
  without the non-target flag, 1 to the enemy total (VERIFIED-CODE 0x407590 at 0x407803..
  0x407855); `create` from a script also counts class-2 entities into the enemy total
  (VERIFIED-CODE 0x41a95e). So both are "what the player has had a chance to kill so far",
  which at mission end is the whole level. The star total (0x4d52d0) is counted once at map
  load from the `item_star` drops (VERIFIED-CODE 0x409493, 0x4095c7; engine-behaviour.md
  §10.2).
- `p_stars` += 1 per star picked up (`items\star.scr`, VERIFIED-DATA).
- Kills (+0x16d) count enemies killed by that player (engine-behaviour.md §6.1).

The HUD score is ftol(p_scores) + banked score (engine-behaviour.md §10.4).

### 5.6 Lives, extra lives, continues

- A campaign starts with lives-at-level-start = 2: the current helicopter plus two spares.
- On death the player script decrements `p_lives` and calls `RespawnPlayer`; the game is
  over when `p_lives` < 0 (one player), or when both players have `p_lives` < 0 (two
  players) (VERIFIED-CODE 0x40b440). In two-player mode, while player 1 still has lives ≥ 0
  the game goes on even if player 2 is out, and vice versa.
- The only source of extra lives is the 1-Up item (`items\i_life.scr`: `p_lives` += 1,
  VERIFIED-DATA). There is no score-based extra life and no upper limit on lives (the HUD
  draws at most 5 icons). The cheat `endisnear` sets 99.
- There are no continues in the arcade sense. Game Over offers Restart, which replays the
  same mission with the lives the mission began with, the same banked score and rank
  accumulator (the mission's own progress is discarded), as often as the player wants.
  VERIFIED-CODE 0x4275e0 → 0x408b30.

### 5.7 Unlocking

- Missions: the Start Game list shows only unlocked missions (§3.4). Missions 1 and 2 are
  unlocked in a fresh save. Completing mission i unlocks mission i + 1 (mission 20 unlocks
  mission 1, which is always unlocked).
- Helicopters: entries 0 (p_apache) and 1 (p_comanche) are unlocked in a fresh save. The
  others unlock on completing the mission whose `enableHelic` names them (VERIFIED-DATA,
  levels.txt): mission 3 → 7 (p_comanche_sand), 5 → 4 (p_comanche_white), 7 → 2
  (p_apache_white), 9 → 3 (p_apache_impala), 11 → 9 (p_comanche_green), 13 → 8
  (p_comanche_blue), 15 → 6 (p_apache_blue), 17 → 5 (p_comanche_lava).
- Unlocks are global to the save, not per player, and are never re-locked.

### 5.8 The ten helicopters

VERIFIED-DATA (`objects\player.obj`, `scripts\player\*.scr`): the helicopters differ only in
looks. All ten use player logic that is instruction-for-instruction identical; the three
scripts `player.scr`, `player_a.scr` and `player_c.scr` differ only in the model paths they
switch to when damaged. Health 400, speed, weapons and item handling are the same.

| Index | Object | Model | Skin | Script |
|---|---|---|---|---|
| 0 | p_apache | apache | apache_green | player_a |
| 1 | p_comanche | comanche_impala | comanche_impala | player |
| 2 | p_apache_white | apache | apache_white | player_a |
| 3 | p_apache_impala | apache | apache_impala | player_a |
| 4 | p_comanche_white | comanche | comanche_white | player_c |
| 5 | p_comanche_lava | comanche | comanche_lava | player_c |
| 6 | p_apache_blue | apache | apache_blue | player_a |
| 7 | p_comanche_sand | comanche | comanche_sand | player_c |
| 8 | p_comanche_blue | comanche | comanche_blue | player_c |
| 9 | p_comanche_green | comanche | comanche_green | player_c |

At boot (and after a video restart, §6.3) player 1's helicopter is 1 and player 2's is 0
(VERIFIED-CODE 0x408cc0); the choice is not saved.

### 5.9 Two-player rules (summary)

- Chosen in Start Game ("Game mode", §3.4). Each player has their own record, lives, score,
  bank, rank accumulator, bindings (`[Controls]` / `[Controls2]`) and helicopter.
- Game over only when both players are out (§5.6). Restart restarts both.
- Mission Complete and Game Complete do not draw the statistics box at all in two-player
  mode (VERIFIED-CODE 0x426360, 0x4270a0: the tally is inside a one-player test).
- Banking is done for both records.
- The high-score check is skipped in two-player mode (VERIFIED-CODE 0x40bde0), so two-player
  scores are never recorded.
- The two-player HUD is described in §4.

### 5.10 Difficulty

Chosen per campaign in Start Game (index 0..4: Very Easy, Easy, Normal, Hard, Nightmare;
default Normal). Factors (table 0x4577d0, 16 bytes per level) are in engine-behaviour.md §6.3.
The rank factor used for the mission-complete rank and for the high-score rank is the one of
the current difficulty; a campaign cannot change difficulty midway (Continue keeps it).
VERIFIED-CODE 0x408b30, 0x426360, 0x40be90.

### 5.11 Rank

Rank index from a value v (0x40bda0): 0 "Cheater" if any cheat was used this session (the
flag is never cleared); otherwise the first i in 1..5 with v < threshold[i] (3, 7.5, 14, 22,
30, table 0x457d48), else 6. Names (table 0x45650c): Cheater, Rookie, Junior Pilot, Pilot,
Master Pilot, Berserker, Elite. VERIFIED-CODE.

- Mission Complete / Game Complete display v = (p_stars / star total + 0.5 × p_scores /
  maximum score + accumulator) × rank factor, i.e. the accumulator including the mission just
  finished but before it is banked (player 1 only).
- The high-score entry stores the rank of v = accumulator × rank factor, taken after banking
  (0x40be90).

The accumulator grows by at most 1.5 per mission (all stars, maximum score), so over 20
missions the value can reach 30 × rank factor; "Elite" (≥ 30) needs near-perfect play on
Normal or above. (Arithmetic from the formulas above.)

## 6. Save file and settings

The original keeps progress in `game.bin` and settings in `config.ini`. Our engine writes its
own formats (spec README); this section says what must be persisted, when, and with which
defaults, and records the original layouts for an optional importer.

### 6.1 Progress (`game.bin`, VERIFIED-CODE 0x401050, 0x4011b0)

What is persisted: the 15-entry high-score table, the helicopter unlock flags (10), the
mission unlock flags (20). Nothing else: no campaign in progress, no current mission, no
score, no lives, no difficulty, no helicopter choice. Closing the program in the middle of a
campaign loses it; only unlocks and high scores survive.

When it is written: at program exit (Exit → Yes, window close), before the video restart of
Options → Apply (0x4206c0), and before the fatal-error message box (0x4204f0). It is read
once at start-up (`Sys_Init`). Unlocks and new high scores only live in memory until then
(a crash loses them). VERIFIED-CODE. Recommendation for our engine (not original
behaviour): write after every change (mission complete, high-score insert), which matters on
Android where the process can be killed.

Original layout (1858 bytes, engine-behaviour.md §10.5): float 1.0; 256-byte random XOR
key; u32 holding a CRC-16/CCITT of the encrypted payload; 0x63A-byte payload XORed with
key[i & 0xFF]. Payload: u32 (unused), 15 × {char name[32], i32 score, i32 rank index}, 10 ×
{u8 unlocked, char object name[32]}, 20 × {u8 unlocked, char level id[32]}. A file with a
wrong version or CRC is ignored (defaults kept, a log line written).

Fresh-install defaults (compiled in, VERIFIED-CODE, tables 0x4573e0, 0x457660, 0x457838):

| # | Name | Score | Rank |
|---|---|---|---|
| 1 | Divo Master | 1,000,000 | 6 Elite |
| 2 | Dennis | 900,000 | 5 Berserker |
| 3 | Terminator | 800,000 | 5 |
| 4 | Walter | 700,000 | 4 Master Pilot |
| 5 | Phil | 600,000 | 4 |
| 6 | James | 500,000 | 3 Pilot |
| 7 | Marianne | 400,000 | 3 |
| 8 | Chris | 300,000 | 3 |
| 9 | Smasher | 250,000 | 2 Junior Pilot |
| 10 | Greg Gizmo | 200,000 | 2 |
| 11 | Alex. B. Blom | 150,000 | 2 |
| 12 | Sam | 100,000 | 1 Rookie |
| 13 | Jennifer | 75,000 | 1 |
| 14 | Turner | 50,000 | 1 |
| 15 | Linda | 30,000 | 0 Cheater |

Helicopters 0–1 and missions 1–2 unlocked, all others locked.

### 6.2 Settings (`config.ini`, VERIFIED-CODE 0x41cef0, 0x41d810, 0x41dd60)

When: read at start-up (if missing, written with defaults and re-read); written at program
exit only (Options, Apply and the F-keys change memory only). The first run (`FirstRun` =
1, the default) and the `-setup` switch show a Windows setup dialog (video mode 16/32 bit,
fullscreen, vsync, system mouse, FPS display, texture filter); `-setup` exits after it. Our
engine drops the dialog.

| Section / key | Default if missing | Range and meaning | Changed in game by |
|---|---|---|---|
| System ShowHints | 0 | 0/1: menu tooltips | — (file only) |
| System FirstRun | 1 | 0/1: show setup dialog | setup dialog |
| System ShowLogo | 1 | 0/1: intro pages (read only, never written) | — |
| System UseSystemMouse | 0 | 0/1: OS cursor instead of the drawn one | — |
| System Camera | 1 | 0..3: Low Pitch, Default, High Pitch, Top-Down | Options, F9 |
| System MouseControl | 0 | 0/1 (§3.6) | Options |
| Display VideoMode | 1 | 0..8: 640×480, 800×600, 1024×768, 1152×864, 1280×960, 1280×1024, 1600×1200, 1920×1440, 2048×1536; other values become 1 | Options (Apply) |
| Display RefreshRate | 0 | 0 = default, else Hz | Options (Apply) |
| Display ColorDepth | 0 | 0 desktop, 16, 32 | Options (Apply) |
| Display Fullscreen | 1 | 0/1 | Options (Apply) |
| Display ForceFullscreen | 0 | 0/1 | — |
| Display WaitVSync | 0 | 0/1 | — |
| Display Brightness | 0.6 | 0.2..1.0 in the menu (full-screen modulate quad, engine-behaviour.md §13.2) | Options |
| Graphics TextureFilter | 0 | 0 bilinear, 1 trilinear | — |
| Graphics AllowSoftware, Disable* (5 keys) | 0 | renderer switches (read only) | — |
| Debug ShowFPS, ShowTris, ShowTexBinds, ShowCounters | 0 | debug counters (only ShowFPS written) | — |
| Sound SfxVolume | 0.5 | 0..1 (values ≥ 1 become 1) | Options, F5/F6 (±0.1) |
| Sound MusicVolume | 0.5 | 0..1 | Options, F7/F8 (±0.1) |
| Sound Sound3D | 0 | 0/1 | Options (Apply) |
| Controls, Controls2: 10 keys | see below | two key codes per action, "k1 k2"; 0 means unbound | Configure controls, Mouse Control |
| Joystick JoystickDisabled | 0 | read only | — |
| Joystick JoystickThresholdX/Y | 0.3 | stick dead zone (read only) | — |

Binding defaults when the keys are missing (both players alike): Move Forward 38 243 (Up,
stick up); Move Backward 40 244; Move Left 37 241; Move Right 39 242; Primary Attack 17 203
(Ctrl, joy1); Switch Weapon 50 206 ('2', joy4); Missile Attack 16 204 (Shift, joy2); Switch
Missiles 49 211 ('1', Joy5); Use Power-Up 32 205 (Space, joy3); Switch Power-Up 50 212 ('2',
Joy6). The shipped `config.ini` differs (mouse buttons for player 1's fire keys, '3'/'4' for
the power-up switch; VERIFIED-DATA), and the manual lists Z / X / C for the three switch keys
(VERIFIED-DATA, `manual\default-controls.html`): the three sources disagree.

The `F5`/`F6` keys lower/raise the effects volume and `F7`/`F8` the music volume by 0.1
(clamped to 0..1); the manual says F5 raises. F9 cycles the camera. VERIFIED-CODE 0x408f10.

### 6.3 Settings that exist only for the PC original

Resolution, refresh rate, colour depth, fullscreen, vsync, texture filter, renderer switches,
the setup dialog and `UseSystemMouse` have no meaning on Android and can be dropped or
replaced; the video restart of Apply (which also resets the attract level and helicopter
choices) need not be reproduced.

## 7. Touch adaptation notes

Facts only: which inputs of the original each screen depends on, and which of them a
landscape Android device without keyboard or mouse lacks. Pointer input is "left click" on
PC; a tap can deliver it, but there is no hover (focus-follows-mouse, hover sound, tooltip)
and no right button. Coordinates are in the virtual 800×600 space, so hit areas scale with
the screen.

| Screen | Inputs used by the original | Missing on touch-only |
|---|---|---|
| Intro pages | any key or click speeds up ×4 | none (tap) |
| Main menu | click or Enter on 5 buttons; hover focus with sound | hover highlight only appears on focus; the pulse needs a focused item |
| Exit confirmation | click Yes/No; Esc / right click = No | Esc and right click (back); Android has a system Back gesture |
| Start Game | list: click a row, arrows, PgUp/PgDn/Home/End, click scroll arrows and track; spinners: click cycles forward only, Left/wheel down cycles back; helicopter grid: click only (two players: alternating clicks); Back/Start buttons | reverse cycling of spinners (keyboard Left or wheel); list scrolling by keys; the list's 32-px scroll arrows are small (26-px zone); list rows are 20 px high |
| Options | spinners as above; sliders: click position, drag the knob, Left/Right steps | reverse spinner cycling; drag works only while the pointer stays over the slider row (hover-based) |
| Configure controls | click a row then press a key/button; Backspace/Delete unbinds; Esc cancels | the whole screen depends on a keyboard or gamepad; nothing to bind with touch |
| Top Scores | click Back; Esc | Esc |
| Name entry | type characters, Backspace/Delete/Home/End/arrows/Insert, Enter or click OK; Esc swallowed | a text input method is required (on-screen keyboard) |
| Information | click the page spinner (forward only); PgUp/PgDn/Left/Right; Back; Esc | going back a page (keys only) |
| In-game menu | opened by Esc; Resume/Options/Quit buttons; Esc/right click resume | a way to open it (no Esc) and to resume without the button |
| Pause | P / Pause key | a pause control |
| Playing | held bits: 4 directions, primary fire, missile, power-up; one-shot: next weapon, next missile, next power-up (engine-behaviour.md §7.2); optional MouseControl (helicopter flies toward the pointer, dead zone 20 px) | all ten actions need touch controls; MouseControl's "fly toward the pointer" maps onto a finger position but the three fire buttons (mouse 1–3) then need other controls |
| Hotkeys | F5–F8 volumes, F9 camera, F12 screenshot | no keys (the Options sliders and Camera spinner cover volumes and camera) |
| Cheats | typed words (WM_CHAR) outside intermission levels | a text input method |
| Tutorial hint | OK button, Enter, Esc, Space | none (tap OK) |
| Game over | Restart/Quit buttons after 2 s; Esc swallowed | none |
| Mission complete | helicopter grid (tap), Restart/Quit/Continue; Esc swallowed | none |
| Game complete | Continue; Esc swallowed | none |
| Two players | two keyboard binding sets on one keyboard (or joystick for player 2) | a second input device or a split touch layout |

Other facts relevant to touch:

- Every activation in the original happens on the button **press**, not the release
  (0x429190 passes presses only), and the item under the pointer at that moment is
  hit-tested.
- Focus is taken from the pointer position every frame; with touch there is no pointer
  between taps, so the "focused item" is simply the last one tapped.
- The original virtual screen is 4:3; phones in landscape are wider (§8, question 3).

## 8. Open questions

<!-- SECTION-8 -->

## 9. Corrections to other specs

<!-- SECTION-9 -->

## Changelog

- 1.0 (WP-26): first version.
