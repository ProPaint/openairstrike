# Front end of AirStrike 2: menus, comic screens, HUD, mission flow, save

Spec version 1.0 (package B7). Game `as2`: AirStrike 2 v2.51, `AirStrike3D II.exe` (image base
0x400000; section table in [README.md](README.md)). This is a full document, not a delta: the
screen set of the sequel is new. Its sections mirror [../frontend.md](../frontend.md) (the
first game's front end, "the base" below) section by section, so that an implementer can diff
the two; every section starts with one line saying how it relates to the base section: **same**,
**changed** or **new**.

Sources: the Ghidra export `re/out/as2/` (functions, decompilation, disassembly), plain
`objdump` of the executable for callbacks that the export leaves inside other functions, the
v1.70 export `re/out/v170/` for comparisons, the Gulf Thunder export `re/out/gulf/` for the
Gulf column of the screen table, and the AS2 data (`assets_extracted_games/as2/`, the install's
`data/Settings.xml`, `config.ini`, `game.bin`). Every atlas rectangle given here was checked by
looking at the image.

**The emulator.** Most layouts in this document were read by running the game's own drawing
code in a CPU emulator, `re/tools/emu2d.py` (Unicorn engine; the PE sections are mapped, imports
return 0, texture and sound registration, `rand`, `sprintf` and the 2D-list flush are replaced
by Python). It records every quad the code queues for the 2D layer (position, size, UVs, texture,
colour, blend, rotation, second texture) and every call of the text routines (string, position,
colour, flags). A claim marked "(emu)" comes from such a run of the cited function; it is still
`VERIFIED-CODE`, because it is the original code that computed it. Screen code that loads
levels or talks to Direct3D was not emulated; those parts were read.

Tags as in [../README.md](../README.md): `VERIFIED-CODE` cites `as2@0x…` (and `v170@0x…` when a
comparison is made), `VERIFIED-DATA` holds for the shipped AS2 files, `GUESS` is unproven.
Symbols named by this package: `re/symbols_as2_frontend.csv`. Addresses of texts compiled into
the executable: `tools/exe_texts/as2.json` (section 7). Quirks and the choices this spec
recommends: [issue 240](issues/240-frontend-quirks-and-choices.md).

Conventions (as in the base unless stated):

- **Coordinates** are virtual pixels of the 800×600 2D layer, origin top-left, y down. A
  rectangle is (x, y, w, h).
- **Texel rectangle** "texel (x, y, w, h)" is a rectangle of an image in pixels, from the
  top-left corner of the image as seen in an image viewer. The functions
  `R_Add2DFrame` as2@0x417b60 (a texel rectangle drawn at texel size) and `R_Add2DPic`
  as2@0x417c60 (a whole picture at its natural size) work in these units; UVs follow from them.
  New in AS2: `R_Add2DPic` insets the UVs by half a texel on every side (as2@0x417c60, (emu):
  a 32-texel cursor gets s 0.0156..0.9844). VERIFIED-CODE.
- **UVs** (s0, t0, s1, t1) with t = 1 at the top row of the image, as in the base.
- **Colours** are packed 0xAABBGGRR as in the code. Names used below:

| Name | Packed | RGBA |
|---|---|---|
| green (the AS2 text colour) | 0xFF00B000 | (0, 0.69, 0, 1) |
| orange (focus, titles) | 0xFF00A0FF | (1, 0.627, 0, 1) |
| grey (disabled) | 0x00404040 | (0.25, 0.25, 0.25), alpha forced to 1 by the additive font |
| dark green box | 0x80006000 | (0, 0.376, 0, 0.5) |
| green outline | 0x5000FF00 | (0, 1, 0, 0.31) |

- **Blend** 0 opaque, 1 ALPHA, 2 ADD, 3 FILTER (destination × source colour), as in the base.
- **Menu time `mt`** (as2@0x2111ef4): reset to 0 on every push and pop, advanced by frametime
  while a menu is shown (as2@0x42b2c0, 0x42b340, 0x42b3c0). Same as the base.
- **Open value `f`** of a menu (menu record +0x11C, new): see 2.1.
- **Shape comparisons.** "Same code" between two executables means the same instruction
  sequence with absolute addresses masked (`re/tools/compare_shapes.py`). The mask also hides
  constants that are addresses in the data sections, among them floating-point constants and
  colours stored there; so "same code" says the logic is the same, not that every value is.
  Values in this document were read in the AS2 executable itself.

---

## 1. Screens and state machine

Relation to the base §1: **changed**. Same principle (menus are a stack drawn over a level that
is always loaded); the screen set is new: a comic intro, text buttons, helicopter selection,
portrait dialogues, comic loading, game over and game complete screens, credits.

### 1.1 Principle

Same as the base (VERIFIED-CODE `UI_PushMenu` as2@0x42b2c0, `UI_Frame` as2@0x42b3c0; flags in
[engine-behaviour.delta.md](engine-behaviour.delta.md) 1.3): an attract level (`intro1` or
`intro2`, chosen by `rand` at boot, as2@0x410eb0) or a mission is always loaded, and menus are a
stack of at most 16 drawn over it. A menu shown during a mission comes with the paused flag.

### 1.2 Screen list

"Gulf" says whether Gulf Thunder v2.71 runs the same code for the screen (same instruction shape
of the builder and draw functions, `re/tools/compare_shapes.py --from as2 --to gulf`; data values
may still differ, see the conventions). It is information for a later package, not a spec of
Gulf Thunder.

| # | Screen | Built by | Shown over | Music | vs base | Gulf |
|---|---|---|---|---|---|---|
| S0 | Logo pages | `G_StartIntros` as2@0x410530, `G_IntroFrame` as2@0x410850 | nothing (2D only) | none (GUESS) | same | frame loop same, page list builder changed |
| S0b | Intro comic, 4 pages | comic pages as2@0x40f1e0.. (3.2) | nothing (2D only) | `music\track02.mo3` from order 0, started by page 1 | new | changed |
| S1 | Main menu | `M_BuildMainMenu` as2@0x42b950 | attract level | attract level's `music` (both: `music\track03.mo3`, VERIFIED-DATA) | changed | builder same opcodes, draw changed |
| S2 | Exit confirmation | as2@0x428090 | attract level | same | changed | same |
| S3 | Start Game (mission, difficulty, game mode) | `M_StartGameMenu` as2@0x42d8b0 | attract level | same | changed | builder same opcodes, draw changed |
| S3b | Helicopter selection | `M_ShowHeliSelect` as2@0x4297d0 | attract level (from S3) or the finished mission (from S15) | unchanged | new | builder same, draw changed |
| S4 | Top Scores | `M_TopScoresMenu` as2@0x42e0d0 | attract level | same | changed | builder same, draw changed |
| S5 | Name entry | `M_NameEntryMenu` as2@0x42bcd0 | attract level | same | changed | same |
| S6 | Options | `M_ShowOptions` as2@0x42cd30 | attract level or the paused mission | unchanged | changed | builder same, draw changed |
| S7 | Configure controls | `M_ControlsMenu` as2@0x423ac0 | as S6 | unchanged | changed | builder same, draw changed |
| S8 | Information (8 pages) | `M_InfoMenu` as2@0x42a880 | attract level | same | changed | same |
| S8b | Credits | `M_CreditsMenu` as2@0x423d90 | attract level | same | new (was page 10 of S8) | builder same, draw changed |
| S9 | Loading screen with comic | `SCR_SelectLoadingComic` as2@0x40acc0, `SCR_DrawLoading` as2@0x40adf0 | replaces everything | none | changed | changed |
| S9b | Portrait dialogue (mission start) | `M_ShowPortraitDialog` as2@0x4234b0 | mission, paused, HUD visible | mission's music | new | same |
| S10 | Playing (HUD) | `HUD_Frame` as2@0x40ac80 | mission | mission's `music`, looped | changed | same code |
| S11 | Pause (P / Pause) | none | mission, frozen | keeps playing (GUESS, as the base) | same | same |
| S12 | Tutorial hint box | `UI_MessageBox` as2@0x42dd20 | mission, frozen | keeps playing | changed | same |
| S13 | In-game menu (Esc) | `M_InGameMenu` as2@0x42aa20 | mission, frozen, HUD hidden | keeps playing | changed (Restart added) | builder same opcodes |
| S13b | Portrait dialogue (mission end) | `M_ShowPortraitDialog` as2@0x4234b0 | mission, frozen, HUD hidden | keeps playing | new | same |
| S14 | Game over | `M_GameOverMenu` as2@0x428db0 | mission, frozen, HUD hidden | module jumps to order 35 (same as the base) | changed | builder and actions same |
| S15 | Mission complete | `M_MissionCompleteMenu` as2@0x427db0 | mission, frozen, HUD hidden | keeps playing | changed | builder same, draw changed |
| S16 | Game complete (after mission 18) | `M_GameCompleteMenu` as2@0x4289c0 | black over the frozen mission 18 | keeps playing (GUESS) | changed | same |
| — | Statistics overlay (debug) | `R_DrawStats` as2@0x40aa40 | anything | — | moved out of the renderer | same |

There is no mission briefing screen (the portrait dialogues take that role), no shop, no save
slot screen, no nag screen and no purchase button in this build (3.21). VERIFIED-CODE: the menu
builders were enumerated through the callers of `UI_AddItem` as2@0x42adb0 and `UI_PushMenu`
as2@0x42b2c0.

### 1.3 Transitions

```
boot ──(ShowLogo=1)──> S0 logo pages ──> S0b comic pages 1..4 ──> attract level + S1
boot ──(ShowLogo=0)──────────────────────────────────────────────> attract level + S1

S1 Main menu:  Start Game → S3 | Top Scores → S4 | Options → S6 | Information → S8
               Credits → S8b | Quit → S2 | Esc / right click: ignored
S2:  YES → save game.bin, quit | NO / Esc → S1
S3:  Back / Esc → S1 | Next → S3b (start mode)
S3b (start mode):  Back / Esc → S3 | Start or Continue → S9 → S10 (G_NewGame, 5.1)
S3b (accept mode, from S15):  Accept / Esc → S15
S4, S8, S8b:  Back / Esc → previous menu
S6:  → S7; Back / Esc → previous menu; Apply → video restart (6.3)

S9 → S10, and on missions with a start dialogue S9b over S10 (paused) → S10
S10 Playing:  Esc → S13 | P/Pause ↔ S11 | ShowTutorialHint → S12 → S10
              all lives lost → S14
              EndLevel → S13b end dialogue (if the mission has one) → S15, or S16 after mission 18
S13 In-game:  Resume / Esc / right click → S10 | Options → S6 (over S13)
              Restart → S9 → S10 same mission (loadout re-applied)
              Quit → S9 (attract level) → S1   (no banking, no high-score check)
S14 Game over (buttons usable at once):
              Restart → S9 → S10 same mission (loadout re-applied)
              Quit → bank → S9 (attract) → S1, then high-score check → S5 → S4
S15 Mission complete:
              Next → bank → S9 → S10 next mission (upgrades kept)
              Restart → S9 → S10 same mission (loadout re-applied)
              Choose Helicopter → S3b (accept mode) → S15
              Quit → S9 (attract) → S1         (no banking, no high-score check)
S16 Game complete:
              Continue → bank → S9 (attract) → S1, then high-score check → S5 → S4
S5 Name entry: Ok / Enter → insert entry → S4 (over S1)
```

VERIFIED-CODE: as2@0x42b7c0, 0x427fb0, 0x42d7b0, 0x428fb0, 0x42c700, 0x42a8c0, 0x428ac0,
0x427a20, 0x428140, 0x42bc10, 0x4234b0, 0x4231b0, 0x410850. "Esc" means the Esc key (0x1B) and
the right mouse button (code 201), as in the base: the generic handler pops the menu unless the
screen's key callback swallows them.

Returning to the main menu rebuilds it and replaces the whole stack (`M_ShowMainMenu`
as2@0x42bbc0, same as the base). Quit reloads the attract level chosen at boot (name at
as2@0x22191e8, looked up with as2@0x40d410).

### 1.4 What the world does behind each screen

Same as the base §1.4, with these differences (VERIFIED-CODE):

- S0 and S0b draw only 2D; brightness is forced to 0.5 for the whole sequence, comic included
  (as2@0x410531..0x41054d), and restored before the attract level starts.
- S3b draws a 3D preview of a helicopter over the menu (3.18); the level behind keeps running.
- S9b (start dialogue): the mission is paused and the HUD stays visible; closing the dialogue
  unpauses and clears `p_action` of both players (as2@0x4231b0).
- S13b and S15, S16: the HUD is hidden (`EndLevel` as2@0x40e730 sets it before the dialogue).
- While any menu is on the stack, keys and buttons go to the menu system first; F5–F9, P and Esc
  are inactive then (`G_KeyEvent` as2@0x4110f0, same rule as the base).

## 2. Menu system (widgets and input)

Relation to the base §2: **changed in its art and in two mechanisms, same in its logic**. The
stack, focus rules, generic keys, spinner, slider, list, edit field and text routines work as in
the base; the item types are renumbered, two item types are new (a picture and a text button),
every widget is drawn in the new green style from `gfx\ui\interface.tga`, a menu has an opening
animation, and most widgets no longer play the hover sound. The first game's implementation can
be reused with per-game tables (section 9).

### 2.1 Menu stack (VERIFIED-CODE as2@0x42b2c0, 0x42b340, 0x42b3c0, 0x42bbc0)

Same as the base: 16 menus at most (depth as2@0x2219180, array as2@0x2112698, current menu
as2@0x2111e5c); only the top menu is drawn and receives input; push and pop reset `mt` and the
focus of the newly current menu and re-run the hover test; builders rebuild every item on each
opening.

Menu record (static globals, one per screen): +0x00 item count, +0x04 64 item pointers, +0x104
focused index, +0x108 hovered index, +0x10C hover time, +0x110/+0x114 cursor position when the
hover started, **+0x118 closing flag (new)**, **+0x11C open value f (new)**, +0x120 draw
callback, +0x124 key callback. Builders now pass the callbacks in the order action (per item,
item +0x44), draw, key.

**Open value f (new).** Push and pop set f of the new top menu (and of the menu it covers) to 0
(as2@0x42b2c0, 0x42b340). `UI_Frame` then raises it by 4 × frametime twice per frame while the
closing flag is clear, so a menu opens in **0.125 s**; it is clamped at 1 (as2@0x42b3fe..0x42b4e8;
(emu): 0.16, 0.32, … 0.96, 1.0 at frametime 0.02). With the closing flag set the two steps cancel
out and f stays where it is; no code of the export sets that flag (no byte write to +0x118 of a
menu record exists), so the branch is dead. f is used for:

- the panel's opening animation (3.1), on the screens that pass f to `UI_DrawPanel`;
- widgets of types 0, 3, 5, 6, 7 and 8 are drawn only once f ≥ 1 (as2@0x426570); pictures
  (types 1, 2) and text buttons (type 9) are drawn from the first frame, text buttons with their
  own slide-in (2.5).

Drawing: the menu's draw callback if it has one, else `UI_DrawMenu` as2@0x426570 (items in
insertion order, then the tooltip); then the cursor (2.7); then the 2D list is flushed and the
per-frame render lists are cleared (as2@0x42b5d9..0x42b605).

### 2.2 Items

Same record and events as the base (type, id, flags, label, tooltip, x, y, hit rectangle, action
callback with event 1 activate, 2 focus gained, 3 focus lost). New: item +0x48 is an optional
per-item draw callback that replaces the type's drawer (used by the controls rows, 3.7). The
**type numbers changed** (`UI_AddItem` as2@0x42adb0, jump table as2@0x42ae64; VERIFIED-CODE):

| AS2 type | Base type | Widget | Hit rectangle when added |
|---|---|---|---|
| 0 | 0 | text label | 10 × character count wide, 16 high, anchored by the align flags (same) |
| 1 | 1 | image button | (x, y, w, h), w and h at item +0x54/+0x58 (same) |
| 2 | – | **picture** (new): an image button without highlight | (x, y, w, h) |
| 3 | 2 | edit field | (x, y, w, 16) (same) |
| 5 | 4 | list | (x, y, w, h) (same) |
| 6 | 5 | spinner | as the base |
| 7 | 6 | slider | as the base |
| 8 | 7 | helicopter grid | as the base; **never added by any AS2 builder** (Start Game fills one in at (324, 304) and does not add it, as2@0x42d8b0) |
| 9 | – | **text button** (new) | (left, y, W + 46, 30), W = max(caption width, minimum width at item +0x5C), left from the align flags (2.5) |

Types 4 and above 9 add nothing. Flags: as the base (1 focused, 2 disabled, 4 not drawn, 0x2000
right-align on x, 0x4000 centre on x, 0x10000 brace markup, 0x20000 no hover sound); 0x100 and
0x1000 appear in builders without effect. Item +0x24 holds 0xFFFFFF on every text button; no
reader was found (GUESS: an unused colour field).

### 2.3 Focus, hover and sounds

Same rules as the base (VERIFIED-CODE `UI_UpdateHover` as2@0x42ac00, same logic as
v170@0x429200; `UI_MenuKey` as2@0x42af60): focus follows the mouse; `sounds\menu2.wav` plays when
the hovered item changes and the new item lacks flag 0x20000; activation plays
`sounds\menu1.wav`. **Changed in effect:** the list (`UI_InitListItem` as2@0x424800), spinner
(`UI_InitSpinner` as2@0x425020) and text-button (as2@0x423e40) initialisers now set 0x20000 and
the builders give it to sliders and controls rows, so in practice only plain text labels (the
exit confirmation's YES and NO) and possibly the pictures play the hover sound ((emu): hovering a
spinner or a text button queues no sound). Tooltips: same code as the base; no AS2 item has one
(the resolution tooltip text is gone).

### 2.4 Generic keyboard and mouse handling (VERIFIED-CODE as2@0x42ab90, 0x42af60)

Same as the base §2.4 with the renumbered types: Esc or right button pops; with nothing focused,
Tab/Up/Down/stick focus the first selectable item; a focused edit field (3), list (5), spinner
(6), slider (7) or grid (8) gets the key first; Tab/Down/stick down and Up/stick up move the focus
over non-disabled items, wrapping; Enter activates; left button or joy 1 activates the item under
the cursor.

### 2.5 Widgets

Additive font unless stated. VERIFIED-CODE at the cited addresses; layouts (emu) where marked.

**Text label (type 0, as2@0x423e00).** Same shape as the base with new colours: green; orange when
focused; grey 0x404040 when disabled (the base: grey, white, dark grey).

**Image button (type 1, inline in as2@0x426570).** Same as the base: normal texture ALPHA, white
(0x808080 when disabled); when focused the highlight texture ADD in Pulse(2, 0) =
255 × (0.5 + 0.5 sin(2π·mt)). No AS2 screen uses it.

**Picture (type 2, new, inline in as2@0x426570).** The texture (item +0x4C) with the item's UVs
(+0x5C..+0x68) over (x, y, w, h), blend ALPHA; green, orange when focused ((emu), the helicopter
selection arrows). No highlight texture.

**Edit field (type 3, as2@0x427940).** Same code as the base (63 characters, cursor, overwrite,
Backspace/Delete/Home/End/Left/Right/Insert, Enter sends event 1). Its only AS2 use is the name
entry: box (x − 2, y, w + 4, 16) in dark green, blend ALPHA; text white at (x, y) ((emu),
as2@0x42bcd0).

**List (type 5, keys as2@0x4248a0, draw as2@0x424bb0).** Logic the same as the base: visible rows
= (h − 4) / 20; entries {text, enabled}; disabled entries can never be selected; same keys and
clicks. Drawing changed ((emu), Start Game list at (190, 200, 420, 144)):

- outline (x, y, w, h) in the green outline colour, blend ALPHA (the base: a black fill);
- row k text at (x + 10, y + 4 + 20k): the selected row orange on a dark-green bar
  (x + 4, row y − 1, w − 25, 18), blend ALPHA; enabled rows green; disabled rows grey 0x808080;
- scroll bar at x + w − 18 from `interface.tga`, 15 wide, tinted green, blend ALPHA: up box
  texel (153, 113, 15, 15) at y + 3; track texel (153, 138, 15, 15) repeated from y + 21 to
  y + h − 21 (the last piece cut); thumb texel (153, 130, 15, 6) on the track, at y + 21 for the
  first row (its travel formula was not traced; GUESS: proportional to the first visible row as in
  the base); down box texel (153, 154, 15, 15) at y + h − 19. The base used
  `menu\scroller_1/2.tga`.

**Spinner (type 6, as2@0x425250).** Same logic as the base (next: Enter, Right, left click, joy 1,
wheel up; previous: Left, wheel down, stick left; event 1 after every key). Drawing changed: label
right-aligned ending at x − 10, value at x + 10, green; orange when focused, **without** the base's
box; grey when disabled ((emu), Start Game and Options).

**Slider (type 7; init as2@0x424300 and input as2@0x424370, both the same code as the base; draw
as2@0x4245a0).** Logic the same as the base. Drawing changed ((emu), Options): label right-aligned
at x − 10, green (orange focused); bar `interface.tga` texel (107, 89, 128, 11) at
(x + 8, y + 3, 128, 11); knob texel (239, 87, 6, 15) at
(x + 8 + ftol((value − min) × 128 / (max − min)) − 3, y + 1, 6, 15); both in the label colour,
blend ALPHA. `re/symbols_as2.csv` calls these three functions "key item"; they are the slider.

**Helicopter grid (type 8).** Code present (as2@0x4252d0, 0x4253c0, 0x4254b0), never shown. Omit.

**Text button (type 9, new; layout as2@0x423e40, draw as2@0x423f00).** Every AS2 button is one.

- Width W = max(caption width, minimum width at item +0x5C); the caption width skips `{` and `}`
  and includes the padding spaces of the caption string. Total size (W + 46) × 30. left = x, or
  x − (W + 46)/2 with flag 0x4000, or x − (W + 46) with flag 0x2000.
- **Slide-in:** a slide value a (item +0x58) rises by 4 × frametime to 1 while the button is not
  hidden (flag 4) and falls at the same rate to 0 while it is; the button is drawn at
  y_d = 600 − (600 − y)·a, so it slides up from the bottom edge in 0.25 s when a screen opens and
  back down when it is hidden ((emu): the tutorial box's Ok at y 584, 552, 520 on successive
  0.05 s frames). The hit rectangle does not move; hidden buttons are still drawn, below the
  screen.
- **Frame**, `interface.tga`, white, blend ALPHA, at (left, y_d): left cap texel (18, 79, 23, 30);
  body texel (41, 79, ≤40, 30) repeated over W in pieces of at most 40 (the last one cut); right
  cap texel (80, 79, 23, 30) at left + 23 + W.
- **Caption:** centred on left + 23 + W/2 at y_d + 7: green; orange when focused; grey 0x404040
  when disabled; red 0x000000FF when flag 0x10000 is set (no AS2 button has it).
- **Rivets**, white, ALPHA: texel (0, 230, 26, 15) at (left + 25, y_d + 21) and
  (left + W − 6, y_d + 21); texel (0, 215, 26, 15) at (left + 25, y_d − 5) and
  (left + W − 6, y_d − 5).
- No highlight picture: focus only changes the caption colour.

Checked on the atlas: the three frame pieces cut the first bevelled bar at texel y 79..109 into
its left end, body and right end; the two rivet pictures are the small bolts at texel (0, 215)
and (0, 230). (emu) for the main menu gives, for " Start Game " (minimum width 200) centred at
x 400, y 230: frame 277..523, caption centred on 400 at y 237, rivets at x 302 and 471.

### 2.6 Tooltip (VERIFIED-CODE as2@0x426570)

Same code and look as the base §2.6. Unused: no AS2 item has tooltip text.

### 2.7 Mouse cursor

Same as the base (VERIFIED-CODE as2@0x42b5a6..0x42b5d4, (emu)): unless `UseSystemMouse` = 1,
`menu\cursor_1.tga` ALPHA then `menu\cursor_2.tga` ADD at (mouse x − 4, mouse y − 2), white;
both files are shipped (VERIFIED-DATA). **Changed:** nothing is drawn during play any more, also
with mouse control on (`HUD_Frame` as2@0x40ac80; `gfx\mc_cur.tga` is still registered by
as2@0x4077b0 but never drawn). Mouse coordinates: the base's mapping (as2@0x411390, same code as
v170@0x4091c0).

### 2.8 Text routines

Same three routines as the base:

| Routine | Texture, blend | As the base? |
|---|---|---|
| additive font as2@0x425d50 | `gfx\ui\font.tga`, ADD | same glyph cells (30×15 from 32×16), advance table as2@0x49cc50 (identical to v1.70) and markup (`{` white, `}` back) |
| alpha font as2@0x426230 | `gfx\ui\font_alpha.tga`, ALPHA | same |
| number font as2@0x4263e0 | `font.tga` whole 32×16 cells, ADD, advance 14·scale | same code as v170@0x4258f0 |

**Changed:** when a string is centred or right-aligned, `{` and `}` are no longer counted in its
width (as2@0x425d66..0x425da4; the base counted 6 px each). The same rule is used by the text
button, the tutorial box, the panel title and the typewriters. For AS2 this settles the base's
open question 4 in the direction issue 091 item 2 already took. VERIFIED-CODE.

Calling convention of the additive routine (for reading the code): string in EAX, flags in ECX,
then x, y and colour on the stack.

### 2.9 Shared screen decoration (new)

Two drawers replace the base's letterbox, three-layer headers and panel fills: the **panel**
`UI_DrawPanel` as2@0x426ae0 and the **title logo** `UI_DrawMenuHeader` as2@0x42b610. They are
specified in 3.1, with the `interface.tga` atlas table.

## 3. Screens in detail

Relation to the base §3: **changed**; every screen is redrawn. Subsections keep the base's
numbers; 3.18 to 3.21 are new screens.

### 3.1 Shared frame elements

Relation: **changed**. The base's letterbox bars, corner rules, three-layer headers and plain
panel fills are gone (no AS2 code draws them; their `menu\*.tga` files are not shipped,
VERIFIED-DATA: `menu/` holds only the two cursors). Two drawers replace them.

**Atlas `gfx\ui\interface.tga`** (256×256 RGBA, registered by `UI_LoadAssets` as2@0x42b1f0).
Every piece the front end uses, checked on the image:

| Piece | Texel (x, y, w, h) | Used by |
|---|---|---|
| `bar_top_left_titled` | (9, 9, 67, 28) | panel top bar, left end, when the panel has a title |
| `bar_top_left` | (9, 9, 20, 28) | panel top bar, left end, without title |
| `bar_top_body` | (76, 9, 111, 28) | panel top bar, repeated |
| `bar_top_right` | (187, 9, 61, 28) | panel top bar, right end, titled |
| `bar_top_right_short` | (228, 9, 20, 28) | panel top bar, right end, without title |
| `bar_bottom_left` | (9, 42, 67, 31) | panel bottom bar, left end |
| `bar_bottom_body` | (76, 42, 111, 31) | panel bottom bar, repeated |
| `bar_bottom_right` | (187, 42, 61, 31) | panel bottom bar, right end |
| `cable` | (0, 82, 9, 90) | the vertical chains above and below a panel (cut to length from its top) |
| `title_left` | (105, 180, 65, 39) | panel title tab, left end |
| `title_body` | (170, 180, 64, 39) | panel title tab, repeated |
| `title_right` | (234, 180, 15, 39) | panel title tab, right end |
| `button_left` | (18, 79, 23, 30) | text button (2.5) |
| `button_body` | (41, 79, 40, 30) | text button, repeated |
| `button_right` | (80, 79, 23, 30) | text button |
| `rivet_low` | (0, 230, 26, 15) | text button, below |
| `rivet_high` | (0, 215, 26, 15) | text button, above |
| `slider_bar` | (107, 89, 128, 11) | slider (2.5) |
| `slider_knob` | (239, 87, 6, 15) | slider |
| `arrow_left` | (110, 113, 16, 40) | helicopter selection |
| `arrow_right` | (130, 113, 16, 40) | helicopter selection |
| `scroll_up` | (153, 113, 15, 15) | list |
| `scroll_thumb` | (153, 130, 15, 6) | list |
| `scroll_track` | (153, 138, 15, 15) | list |
| `scroll_down` | (153, 154, 15, 15) | list |

The atlas also holds three larger bevelled boxes (texel y 115..220) and a second bar with bolts
(texel (105, 180) row, used by the title tab) that nothing else draws.

**Panel `UI_DrawPanel(x, y, w, h, f)`** (as2@0x426ae0; title string in EDI, or none;
VERIFIED-CODE, geometry (emu) at several sizes and f values). f is the opening value, clamped to
0..1; screens pass their menu's open value (2.1), the portrait dialogue its own fade value, the
game-over screen `mt`. With yt = (y − 15) − (1 − f)·y and yb = (y + h − 13) + (1 − f)·(600 − y − h)
(the bars slide in from the screen edges):

1. **Static fill:** `gfx\ui\snow.tga` (256×256 greyscale noise) over (x, cy − H/2, w, H) with
   cy = y + h/2 and H = f²·h + 600·f·(1 − f) (the fill overshoots while opening, then settles on
   the panel), blend **FILTER** (the scene is multiplied by the noise), white. UVs (r1, r2,
   r1 + w/256, r2 + H/256) with r1 and r2 = rand()/32767 drawn anew **every frame**, so the noise
   flickers.
2. **Top bar** at y = yt, white, ALPHA: with a title `bar_top_left_titled` at x − 4, the body
   from x + 63 over w − 120 in pieces of at most 111 (the last one cut), `bar_top_right` at
   x + w − 57; without a title `bar_top_left` at x − 4, the body from x + 16 over w − 32,
   `bar_top_right_short` at x + w − 16.
3. **Bottom bar** at y = yb: `bar_bottom_left` at x − 4, body from x + 63 over w − 120,
   `bar_bottom_right` at x + w − 57.
4. **Cables** (`cable`, white, ALPHA), drawn in pieces of at most 90 from their top end, the last
   piece cut: with a title, two cables above the panel, at x + 46 from y = −40 down to yt − 25 and
   at x + w − 50 from y = −15 down to yt; always, two cables below it at x + 46 and x + w − 50
   from yb + 31 down to 600.
5. **Title tab** (titled panels only) at (x − 4, yt − 32): `title_left`, then `title_body`
   repeated from x + 61 over the title width Wt (pieces of at most 64), then `title_right` at
   x + 61 + Wt; white, ALPHA. The title text is drawn centred on x + 33 + Wt/2 (so it starts at
   x + 33) at y = yt − 20, orange, additive font.

Most screens draw their items first and the panel after them ((emu): items, panel, title logo
for Start Game, Options, Controls, Mission Complete, the helicopter selection, the exit
confirmation and the tutorial box), so the static fill also multiplies the widgets' own pixels
by the noise. Keep that order to look like the original.

**Title logo `UI_DrawMenuHeader`** (as2@0x42b610, VERIFIED-CODE, (emu)); drawn at the top of the
main menu and of most screens. With T the logo clock (as2@0x2219184, advanced by 0.5 × frametime
each time the logo is drawn):

1. `gfx\logo\glow.tga` (512×128) at its natural size at (124, 0), blend ADD, white.
2. `gfx\logo\two3.tga` (128×128, the crosshair "2" emblem) as a rotated quad: with
   g = 15 + 15·sin(T + 0.2), rectangle (580 − g, −g, 128 + 2g, 128 + 2g), UV (0, 0, 1, 1), blend
   ALPHA, white, rotated by 20 + 15·sin(2T) degrees about its centre. It pulses between 128 and
   188 px around the point (644, 64).
3. `gfx\logo\logo.tga` (512×128, the word "AIRSTRIKE") at (124, 0, 512, 128), UV (0, 0, 1, 1),
   blend ALPHA, white, with the second texture `gfx\logo\clouds.tga` at UV
   (0.1T, 0, 0.1T + 2, 1) combined by ADD (render-pipeline.delta.md 8.2, combine mode 2): the
   letters show clouds scrolling to the left at 0.05 texture widths per second.

The attract level shows through everywhere else; there is no letterbox.

### 3.2 Logo pages and intro comic (S0, S0b)

Relation: logo pages **same**; comic **new**. VERIFIED-CODE `G_StartIntros` as2@0x410530,
`G_IntroFrame` as2@0x410850, comic functions below; layouts (emu).

- Pages come from Settings.xml `<Intros>` as in the base (`BuiltIn` DivoGames, `Image` with
  `BackColor`; the new tag `Video` is parsed and ignored, VERIFIED-CODE as2@0x41e130). After
  them, **four comic pages are always appended** (as2@0x4106ad..0x4107d0). With `ShowLogo` = 0
  neither the logo pages nor the comic run.
- Shipped Settings.xml: an ignored `ImageTemp` and the DivoGames page (VERIFIED-DATA). So the
  sequence is DivoGames (8.5 s), then comic pages 1 to 4 (7 + 7 + 20 + 11 s): 53.5 s at normal
  speed.
- Logo pages: the base §3.2 geometry, fades and durations (VERIFIED-CODE, same constants).
- Frame loop, speed and skipping: as the base. Each page's clock advances by speed × frametime;
  every key press or mouse button press multiplies the speed by 4 (as2@0x41111c, 0x416055);
  speed is reset to 1 at every new page (as2@0x410984). There is no instant skip. After the last
  page: the pages are freed, sound is stopped, the brightness restored, and the attract level
  starts.

**Comic pages** (2D only; every draw starts with an opaque black full-screen fill; tiles are
drawn with `R_Add2DPic` at their natural size, opaque unless stated; the page clock t starts at
0). Tile files are `gfx\ui\comix\intro\Frame#<name>.tga` (named with a capital F in the code,
lower-case on disk; the file system is case-insensitive). Captions and speech bubbles are
painted into the tiles: the comic has no text in the executable.

| Page | Begin, Draw, Done | Tiles and positions | Animation | Ends |
|---|---|---|---|---|
| 1 | as2@0x40f1e0, 0x40f020, 0x40f000 | `1_0_0..1_0_3` at y 150 and `1_1_0..1_1_3` at y 406, columns at x 0, 256, 512, 768 (widths 256, 256, 256, 64; rows 256 and 64 high): one 832×320 picture at (0, 150), its right 32 px off-screen | t < 2: white full-screen fill with alpha (2 − t)/2 over it (fade from white); t > 5: white fill with alpha (t − 5)/2 | t ≥ 7 |
| 2 | as2@0x40f280, same Draw and Done | `2_*`, same layout | fade from white (t < 2), fade to **black** (t > 5) | t ≥ 7 |
| 3 | as2@0x40f310, 0x40f410, 0x40f3f0 | panel A `3_1_0_0..3` at (0, 0), (256, 0), (512, 0), (768, 0) (832×256, opaque); B `3_2_0_0`, `3_2_0_1`, `3_2_1_0`, `3_2_1_1` at (0, 162), (256, 162), (0, 418), (256, 418); C `3_3_0_0`, `3_3_0_1` at (190, 147), (446, 147); D `3_4_0_0`, `3_4_0_1`, `3_4_1_0`, `3_4_1_1` at (472, 100), (728, 100), (472, 356), (728, 356); E `3_5_0_0`, `3_5_0_1` at (264, 384), (520, 384); B to E blend ALPHA (their tiles carry shaped alpha) | A slides up: drawn at y = ftol((1 − t/2)·200) while t < 2; B, C, D, E fade in (white, alpha t − start) over t 3..4, 5..6, 10..11, 12..13; black fill with alpha (2 − t)/2 while t < 2 and (t − 18)/2 for 18 ≤ t < 20 | t ≥ 20 |
| 4 | as2@0x4100b0, 0x410160, 0x410140 | `4_1_0_0..3` at y 90 and `4_1_1_0..3` at y 346 (x 0, 256, 512, 768; 832×512); overlay `4_2_0_0` (256 wide) at (39, 120) and `4_2_0_1` (128 wide) at (295, 120), ALPHA | overlay fades in over t 3..4 (alpha t − 3); black fade from (t < 2) and to (9 ≤ t < 11) | t ≥ 11 |

Page 1's Begin starts the sound system and `music\track02.mo3` from order 0 (as2@0x40f1ee;
string as2@0x48a590). The Frame#1 and Frame#2 pages are panoramas whose tiles join seamlessly
(the 64-px column holds black filler), and the pieces of page 3 overlap panel A (VERIFIED-DATA:
composites of the tiles were looked at). The 42 intro files are exactly these tiles; 42 of the
AS2 TGAs have no TGA footer, all of them intro tiles (render-pipeline.delta.md 9.1).

### 3.3 Main menu (S1; VERIFIED-CODE as2@0x42b950, 0x42b8c0, 0x42b7c0, 0x42b890; (emu))

Relation: **changed** (text buttons, title logo, Credits added, no 3D banner).

Six text buttons, centred on x = 400 (flag 0x4000), minimum width 200, so all are 246 wide:

| id | Caption (address) | y | Action |
|---|---|---|---|
| 1 | " Start Game " (as2@0x48ece8) | 230 | builds and pushes Start Game (3.4) |
| 2 | " Top Scores " (as2@0x48ecf8) | 275 | builds and pushes Top Scores (3.11); the builder's Post button is disabled at once (dropped feature) |
| 3 | " Options " (as2@0x48ebc4) | 320 | `M_ShowOptions` (3.6) |
| 4 | " Information " (as2@0x48ed08) | 365 | `M_InfoMenu` (3.14) |
| 7 | " Credits " (as2@0x48ed18) | 410 | builds and pushes Credits (3.20) |
| 5 | " Quit " (as2@0x48ebd0) | 455 | pushes the exit confirmation (3.5) |

Action id 6 (the dormant purchase command, 3.21) exists in the callback but no item has that id.
Keys: Esc and right click swallowed (as2@0x42b890), everything else generic.

Drawing order (as2@0x42b8c0): the Settings.xml `<Logotypes>` pictures (`UI_DrawSkinImages`
as2@0x422eb0, same rules as the base; the shipped file lists only an ignored `ImageTemp`,
VERIFIED-DATA); the title logo (3.1); the buttons; the Settings.xml `<Info>` version at (20, 580)
left-aligned and copyright at (780, 580) right-aligned, grey 0x808080, each only if non-empty
(shipped: "v 2.51" and a DivoGames copyright line, VERIFIED-DATA). There is no 3D banner object
(the base's `objects\banner.obj` pass is gone) and no letterbox.

### 3.4 Start Game (S3; VERIFIED-CODE as2@0x42d8b0, draw 0x42d840, callback 0x42d7b0; (emu))

Relation: **changed** (no helicopter grid; "Next" leads to the helicopter selection).

| Element | Type, id | Position | Content |
|---|---|---|---|
| mission list | list | (190, 200, 420, 144), 7 rows | one entry per mission table record (as2@0x49df60, 18 × 33 bytes) whose level exists, labelled with the level's `name` from `levels.txt` ("Mission 1: Tutorial" …); locked missions disabled (grey) |
| Difficulty: | spinner, id 3 | (400, 375) | Very Easy, Easy, Normal, Hard, Nightmare (pointer table as2@0x49e6d8); set to Normal at every opening |
| Game mode: | spinner, id 4 | (400, 405) | "Single Player", "Cooperative" (table as2@0x49e6f0); starts from the two-player flag; changing it sets the flag at once |
| " Back " | text button, id 1 | left 40, y 520 | pops |
| " Next " | text button, id 2 | left 645, y 520 | see below |

- Initial list selection: the checkpoint mission (as2@0x49ddf4) when it is 1..17, otherwise the
  item keeps its last value (GUESS: 0 on the first opening).
- Draw: items, then `UI_DrawPanel(150, 180, 500, 270, f)` titled "Start Game" (as2@0x48ee84),
  then the title logo.
- Next: difficulty index (as2@0x49ded4) = spinner; mission index (as2@0x2219140) = list
  selection; two-player flag and player count (1 or 2) from the Game mode spinner; then the
  helicopter selection in start mode (3.18). Nothing is loaded yet.
- Keys: generic (Esc pops).

### 3.5 Exit confirmation (S2; VERIFIED-CODE as2@0x428090, draw 0x428000, action 0x427fb0; (emu))

Relation: **changed** (panel, text labels).

`UI_DrawPanel(210, 230, 380, 160, f)` titled "Confirm Exit" (as2@0x48d9a0); "Are you sure you want
to quit?" (as2@0x48d980) centred at (400, 250), green; text labels (type 0, centred) " YES "
(as2@0x48d9b0) id 1 at (300, 330) and " NO " id 2 at (500, 330); title logo. YES: when the game is
ready, frees the level and saves `game.bin`, then posts WM_CLOSE (the exit path writes
`config.ini`, 6.2). NO and Esc pop.

### 3.6 Options (S6; VERIFIED-CODE `M_OptionsMenu` as2@0x42c8d0, `M_ShowOptions` as2@0x42cd30, draw as2@0x42c860, update as2@0x42c560, action as2@0x42c700, Apply as2@0x42c4a0; (emu))

Relation: **changed** layout, **same** logic as the base §3.6.

`M_ShowOptions` builds the resolution strings once per run (as2@0x42bda0), the refresh list for the
current mode (as2@0x42bfa0), then builds and pushes the menu. Draw: per-frame update, items,
`UI_DrawPanel(210, 180, 380, 290, f)` titled " Options " (as2@0x48ebc4), title logo. Labels
right-aligned on x = 400 (drawn to x 390), values at 410. No key callback (Esc pops).

| y | Item | Type, id | Values or range | Takes effect |
|---|---|---|---|---|
| 200 | Resolution: | spinner, 0 | every enumerated mode as "%dx%d" (as2@0x489960), 6.2 | Apply |
| 220 | Refresh rate: | spinner, 0 | "default", then "%d Hz" per rate of the chosen mode; back to "default" when the resolution changes | Apply |
| 240 | Color Depth: | spinner, 0 | Default, 16 bit, 32 bit; disabled and forced to Default while Fullscreen is Off | Apply |
| 260 | Fullscreen: | spinner, 0 | Off, On | Apply |
| 280 | Brightness: | slider, 3 | 2..10 (Brightness × 10) | immediately |
| 320 | Sound Volume: | slider, 4 | 0..10 | immediately |
| 340 | Music Volume: | slider, 5 | 0..10 | immediately |
| 360 | 3D Sound: | spinner, 0 | Off, On | Apply |
| 400 | Camera: | spinner, 6 | Low Pitch, Default, High Pitch, Top-Down | immediately |
| 440 | Mouse Control: | spinner, 8 | Off, On | immediately, with the base's rebinding rule and quirk (mouse 1–3 on, joy 1–3 off; `M_SetBinding` as2@0x42c690) |
| — | " Configure Controls " | text button, 2, centred at (400, 520) | | pushes S7 |
| — | "  Back  " | text button, 1, left 50, y 520 | | pops; pending values dropped |
| — | " Apply " | text button, 0, left 620, y 520 | shown only while a pending resolution, refresh, fullscreen, 3D sound or depth value differs from the live one (hidden: flags 2 and 4, so it slides out below the screen) | video restart (6.3) |

During a mission (intermission flag as2@0x54329a clear) Resolution, Refresh rate, Color Depth,
Fullscreen and 3D Sound are disabled (grey), as in the base. The rows are the base's rows moved
down 40 px; the base's Resolution tooltip is gone.

### 3.7 Configure controls (S7; VERIFIED-CODE `M_ControlsMenu` as2@0x423ac0, draw 0x423a00, row draw 0x4236d0, row action 0x4236b0, key 0x423930, action 0x4238c0; (emu))

Relation: **changed** look, **same** logic as the base §3.7 (capture, cancel, unbind, the
display-only duplicate removal quirk, key names: `In_KeyName` as2@0x423560 and its 50-entry table
as2@0x49d198 are identical to v1.70).

- Draw: items; `UI_DrawPanel(190, 165, 420, 320, f)` titled "Configure Controls"
  (as2@0x48d728); an outline (200, 205, 400, 270) in the green outline colour, blend ALPHA; title
  logo. The builder still registers `menu\controlsh_0/1/2.tga`, which are neither drawn nor
  shipped.
- "Controls Set:" spinner (id 2) at (400, 180), "Player 1" / "Player 2", always opening on
  Player 1. " Back " text button id 1, left 50, y 520.
- Rows (table as2@0x49d080, 20-byte records), label right-aligned at x 392, keys from x 408, hit
  area x 220..580, y − 2..y + 18: Primary Attack 210, Switch Weapon 230, Missile Attack 270, Switch
  Missiles 290, Use Item 330, Switch Item 350, Move Forward 390, Move Backward 410, Move Left 430,
  Move Right 450 (the base's y + 30); same action bits.
- Row colours: green; orange when focused, on a dark-green box (the base: dark red); grey when
  disabled; the blinking "=" while capturing, as the base. " or %s" (as2@0x48d720) joins the two
  key names.

### 3.8 Mission complete (S15; VERIFIED-CODE `G_MissionComplete` as2@0x427f40, builder 0x427db0, draw 0x427b60, action 0x427a20, key 0x427b30; (emu))

Relation: **changed** (panel with the statistics inside, new buttons, no title picture, no grid).

Pushed by `G_MissionComplete` after the end dialogue (5.3). Over the frozen mission, HUD hidden.

- **Statistics**, one-player mode only, orange, label left-aligned at x 240, value right-aligned
  at x 560; rows appear with `mt` as in the base:

| Appears when | y | Label (address) | Value |
|---|---|---|---|
| mt > 1.0 | 220 | "Enemies destroyed:" (as2@0x48d8c8) | "N%" = kills × 100 / enemy total, integer division; the value is omitted when the total is 0 |
| mt > 1.4 | 250 | "Stars collected:" (as2@0x48d8e4) | "a/b" = ftol(p_stars) / star total |
| mt > 1.8 | 280 | "Your current rank:" (as2@0x48d900) | rank name of the value of 5.8 |

  All player 1's. Kills are capped at the enemy total (engine-behaviour.delta.md 6.1), so the
  percentage never exceeds 100.
- **"New helicopter is available."** (as2@0x48d914) centred at (400, 400), white, in both modes,
  whenever the finished level's `enableHelic` is 0..5, even if that helicopter was already
  unlocked.
- Panel `UI_DrawPanel(180, 160, 440, 200, f)` titled "Mission Complete" (as2@0x48d934); title logo.
- Buttons (text buttons, centred):

| id | Caption (address) | Centre x, y | Action |
|---|---|---|---|
| 4 | " Choose Helicopter " (as2@0x48d960) | 400, 440 | opens the helicopter selection in accept mode over this screen (3.18) |
| 2 | " Restart " (as2@0x48d948) | 120, 520 | free the level, pop, apply the mission's weapon loadout, restart the same mission |
| 1 | "  Quit  " (as2@0x48d954) | 400, 520 | free the level, load the attract level, pop, main menu; no banking, no high-score check |
| 3 | "  Next  " (as2@0x48d974) | 680, 520 | pop all, free the level, bank (5.4), mission index + 1 (mod 18), start it; upgrades kept |

- Keys: Esc and right click swallowed.

### 3.9 Game complete (S16; VERIFIED-CODE builder as2@0x4289c0, draw 0x428430, typewriter 0x4281c0, action 0x428140, key 0x428190; (emu))

Relation: **changed** (comic panorama, 11 typed lines, no statistics box, no title).

After mission 18's end dialogue, `G_MissionComplete` pushes this screen instead of S15.

| Phase | Drawn |
|---|---|
| mt < 2 | only a black full-screen fill with alpha mt/2, blend ALPHA: the frozen mission fades to black |
| 2 ≤ mt < 4 | opaque black fill; the six `gfx\ui\comix\gamedone_*` tiles fade in (ALPHA, white, alpha (mt − 2)/2) |
| mt ≥ 4 | opaque black fill; opaque tiles; the Continue button; the typewriter |

Tiles, stretched from 256 to 267 wide, UVs inset half a texel: `gamedone_1_1` (0, 80, 267, 267),
`gamedone_1_2` (0, 347, 267, 133), `gamedone_2_1` (267, 80, 267, 267), `gamedone_2_2`
(267, 347, 267, 133), `gamedone_3_1` (534, 80, 267, 267), `gamedone_3_2` (534, 347, 267, 133):
one panorama over (0, 80, 801, 400) (VERIFIED-DATA, looked at).

Typewriter (from mt > 4): 11 lines (keys `congrats.0` … `congrats.10`, section 7; lines 1, 5 and 9
are a single space), line i centred on x 400 at y = 160 + 18i with the full line's width (braces
skipped), so each line types out from its final left edge; 8 characters per second; the current
line starts when the previous one is complete; `sounds\type.wav` for each newly shown non-space
character except a line's first; white additive text over a black alpha-font shadow at (+2, +2).
This is the base's rule with 11 lines and a start at 4 s.

Button: " Continue " (as2@0x48dd28), id 1, centred at (400, 520), drawn from mt = 4 (its hit
rectangle exists from the start; GUESS that it can be clicked earlier). Action: free the level,
bank, load the attract level, pop, main menu, high-score check. Esc and right click swallowed.

### 3.10 Game over (S14; VERIFIED-CODE `G_GameOver` as2@0x410e70, builder 0x428db0, draw 0x428b90, action 0x428ac0, key 0x428b60; (emu))

Relation: **changed** (comic art; no red tint; buttons usable at once).

`G_GameOver` sets the game-over, HUD-hidden and paused flags, makes the current module jump to
pattern order 35 (as the base), builds and pushes this screen.

- Comic: three panels, each `gfx\ui\comix\gameover_k_1.tga` (256×256) over `gameover_k_2.tga`
  (256×128): column k at x = 16 + 256(k − 1), tiles at y 108 and 364; together (16, 108, 768,
  384). While mt < 1 they are blend ALPHA, white with alpha mt (a 1 s fade-in); then opaque.
- `gfx\ui\comix\gamov.tga` (512×64, the "GAME OVER" lettering) at its natural size at
  (144, 260), blend ADD, grey level mt until mt = 1, then white.
- `UI_DrawPanel(16, 108, 768, 384, mt)` titled "Game Over" (as2@0x48dd34): the frame slides in
  with `mt` over the first second.
- Buttons (text buttons, centred, y 520): " Restart " (as2@0x48d948) id 1 at x 550; "  Quit  "
  (as2@0x48d954) id 2 at x 250. Usable from the first frame (the base hid them for 2 s; the AS2
  draw still clears their hide bits after 2 s, without effect).
- Restart: free the level, pop, apply the mission's weapon loadout, restart the mission with the
  lives it began with. Quit: free the level, bank, load the attract level, main menu, high-score
  check (5.4).
- Esc and right click swallowed. No red FILTER tint (the base had one), no title picture of the
  base.

### 3.11 Top Scores (S4; VERIFIED-CODE `M_TopScoresMenu` as2@0x42e0d0, draw 0x42deb0; (emu))

Relation: **changed** (panel, colours), same table.

`UI_DrawPanel(120, 170, 560, 318, f)` titled "Top Scores" (as2@0x48eedc); a column bar
(130, 180, 540, 20) in dark green, blend ALPHA; column titles at y 182, orange: "#" at x 138,
"Name" 168, "Score" 393 (as2@0x48eecc), "Rank" 533. Rows i = 0..14 at y = 204 + 18i: position
i + 1 at 138 (orange), name at 168 (green), score with the number font scale 1 at 393 (green),
rank name at 533 (green). " Back " text button id 1, left 30, y 520, pops. Title logo. The
" Post Scores " button (as2@0x48eee8) belongs to the dropped online scores. The new entry is not
highlighted. Esc pops.

### 3.12 High-score check and name entry (S5; VERIFIED-CODE `G_CheckHighScore` as2@0x414610, `M_NameEntryMenu` as2@0x42bcd0, callbacks 0x42bc10, 0x42bc40, 0x42bc70; (emu))

Relation: **changed** look, **same** logic as the base §3.12 (one-player only; slot = first entry
the banked score is ≥ to; insert moves the rest down; name up to 31 characters; rank index of
accumulator × rank factor, 0 if a cheat was used).

`UI_DrawPanel(210, 220, 380, 160, f)` titled "Enter Your Name" (as2@0x48ed24) (the base's "Please
enter your name:" line is gone); edit field id 2 at (275, 285), width 250, focused by the check;
"  Ok  " (as2@0x48ed34) text button id 1 centred at (400, 520). Enter in the field or Ok: pop,
insert the entry (as2@0x4146c0), which shows Top Scores. Esc and right click swallowed. No title
logo.

### 3.13 In-game menu (S13; VERIFIED-CODE `M_InGameMenu` as2@0x42aa20, action 0x42a8c0, draw 0x42a9e0, key 0x42a9a0)

Relation: **changed** (text buttons, new Restart).

Opened by Esc during play (HUD hidden, paused). Text buttons centred on x 400, minimum width 160;
draw: items and title logo, no panel.

| id | Caption (address) | y | Action |
|---|---|---|---|
| 1 | " Resume " (as2@0x48ebb8) | 250 | pop; `p_action` = 0 for both players; paused and HUD-hidden cleared |
| 2 | " Options " (as2@0x48ebc4) | 295 | `M_ShowOptions` over this menu (video items disabled) |
| 4 | " Restart " (as2@0x48d948) | 340 | free the level, pop, apply the mission's loadout, restart the mission |
| 3 | " Quit " (as2@0x48ebd0) | 385 | free the level, load the attract level, main menu replaces the stack; no banking, no high-score check |

Esc and right click do what Resume does (the key callback clears the flags, the generic handler
pops).

### 3.14 Information (S8; VERIFIED-CODE `M_InfoMenu` as2@0x42a880, builder 0x42a7b0, draw 0x42a6e0, key 0x429820, action 0x429800; page builders 0x429890..0x42a550; (emu))

Relation: **changed**: 8 pages (the story pages are gone, the credits have their own screen, a
second items page is new), new positions, explicit icons.

"Page:" spinner at (400, 520) with "1 of 8" … "8 of 8" (table as2@0x49e684); "  Back  " text
button id 1, left 50, y 520; grey 0x808080 hints "PgUp - Previous Page" (as2@0x48eb84) at
(570, 520) and "PgDown - Next Page" (as2@0x48eb9c) at (570, 540). Keys: PgUp or Left previous
page, PgDn or Right next page, both wrapping (as2@0x429820, the base's table); others generic.
No panel, no title logo: the attract level shows behind the text.

Page layout: title at (60, 120), white (additive); body lines from y = 184 every 18 px, orange;
pages with icons put the text at x = 140 with markup (so `{…}` names are white) and draw one icon
per paragraph at (60, y_icon, 66, 35) from the HUD atlases with the HUD's UVs and blend (4.4).

| Page | Title (address) | Body lines | Icons: atlas entry (4.4) at y_icon |
|---|---|---|---|
| 1 | OVERVIEW (as2@0x48e150) | 10 lines and 2 blank slots, text at x 60, no markup | none |
| 2 | PRIMARY WEAPONS (Page 1 of 3) (as2@0x48e340) | Machine Gun, Impulse Gun, Plasma Cannon, Quantum Gun paragraphs | weapon 0 at 194, 1 at 286, 2 at 358, 3 at 430 (ADD) |
| 3 | PRIMARY WEAPONS (Page 2 of 3) (as2@0x48e50c) | Big Laser, Lightning Gun, Wave Gun | weapon 4 at 194, 5 at 295, 6 at 365 (ADD) |
| 4 | PRIMARY WEAPONS (Page 3 of 3) (as2@0x48e5d0) | Missile Gun, Flamethrower | weapon 7 at 194, 8 at 260 (ADD) |
| 5 | MISSILES (Page 1 of 2) (as2@0x48e828) | Small, Big, Small Heat Seeking, Big Heat Seeking Missiles | missile 0 at 194, 1 at 260, 2 at 328, 3 at 418 (ALPHA) |
| 6 | MISSILES (Page 2 of 2) (as2@0x48e8c0) | M.A.D. Missiles | missile 4 at 194 (ALPHA) |
| 7 | ITEMS (Page 1 of 2) (as2@0x48ea84) | Annihilator, Nuclear Bomb, Rocket Strike, Lightning Bomb | power-up 4 at 194, 1 at 256, 2 at 310 (ALPHA), 0 at 378 (ADD) |
| 8 | ITEMS (Page 2 of 2) (as2@0x48eb70) | Satellite Strike, Air Support | power-up 5 at 194, 8 at 256 (ALPHA) |

Because every paragraph heading sits next to the icon the page draws for it, these pages name
the HUD's icon cells: they confirm the weapon order of 4.4 (VERIFIED-CODE). Blank lines are a
one-space string (as2@0x48b728) drawn in their slot. The line slot of every string is in section 7.

### 3.15 Tutorial hint box (S12; VERIFIED-CODE `ShowTutorialHint` as2@0x4218b0 → `UI_MessageBox` as2@0x42dd20, callbacks 0x42daf0, 0x42db30, 0x42db70; (emu))

Relation: **changed** look, **same** rules as the base §3.15 (pauses without hiding the HUD; lines
cut at `^`, at most 16 lines of 63 characters; closing clears paused and `p_action` of both
players; the right button pops without unpausing, the base's quirk).

- Box: W = max(360, widest line + 40), H = max(160, 18 × lines + **60**), left = (800 − W)/2,
  top = (600 − H)/2 (widths without braces). Drawn as `UI_DrawPanel(left, top, W, H, f)` titled
  "Tutorial Tip" (as2@0x48eeb0); the base's growing black box is replaced by the panel's opening.
- Lines, only once f ≥ 1: centred on x 400 at y = top + (H − 18n)/2 + 18i, orange, markup on.
- "  Ok  " text button id 1 centred at (400, 520), whatever the box size (it slides up).
- Close: Enter, Esc or Space (key callback) or Ok.

### 3.16 Loading screen (S9; VERIFIED-CODE `SCR_SelectLoadingComic` as2@0x40acc0, `SCR_DrawLoading` as2@0x40adf0; (emu))

Relation: **changed** (comic, orange bar, "Loading" label).

- At the start of `G_StartLevel` the comic set is chosen by the mission index (0-based,
  as2@0x2219140): 0..6 (missions 1–7) `loading1`, 7..12 (missions 8–13) `loading2`, 13..17
  (missions 14–18) `loading3`; no random choice. Its six tiles `gfx\ui\comix\loadingN_k_1` and
  `loadingN_k_2` (k = 1..3; named without extension, they resolve to `.tga`) are registered.
  Progress is reset to 0.
- Each loading step: full-screen black, opaque. Unless the level is an intermission level, the
  comic: column k at x = 0, 267, 534, width 267: tile `_k_1` at (x, 135, 267, 264) and `_k_2` at
  (x, 399, 267, 66), opaque, white, UVs inset half a texel (one continuous picture of about
  801×330 over y 135..465, VERIFIED-DATA). Then a progress bar: orange outline (340, 500, 200, 10)
  and orange fill (340, 500, progress × 200, 10), opaque; "Loading" (as2@0x48a424) at (260, 496),
  orange, left-aligned.
- Intermission levels: black, bar and label only.

### 3.17 Pause (S11)

Relation: **same** (P or Pause toggles the paused flag when no menu is open; nothing drawn;
unpausing clears `p_action`; VERIFIED-CODE `G_SetPause` as2@0x410cb0).

### 3.18 Helicopter selection (S3b, new; VERIFIED-CODE `M_ShowHeliSelect` as2@0x4297d0, `M_HeliSelectMenu` 0x429530, draw 0x4291f0, backdrop 0x4290c0, action 0x428fb0, preview 0x428f10, view 0x4079c0 and 0x407850; (emu))

Opened in **start mode** from Start Game (Next) or in **accept mode** from mission complete
(Choose Helicopter); the mode flag is as2@0x20fedb7. `gfx\ui\grid.tga` is registered.

Items:

| Item | Type, id | Position | Details |
|---|---|---|---|
| next arrow | picture, 6 | (600, 278, 16, 40) | `arrow_right` |
| previous arrow | picture, 7 | (180, 278, 20, 44) | `arrow_left`, stretched |
| start button | text button, 1 (8 in accept mode), right-aligned on x 760, y 520 | | " Accept " (as2@0x48deb4) in accept mode; else " Continue " (as2@0x48dd28) when the mission index is > 0 and equals the checkpoint mission (5.1); else " Start " (as2@0x48dec0) |
| " Back " | text button, 2, left 40, y 520 | | start mode only |
| Player: | spinner, 5, (400, 435) | | "Player 1", "Player 2" (table as2@0x49e678); two-player mode only; selects whose helicopter the screen shows |

Draw (items other than the buttons and arrows only once f ≥ 1):

1. Backdrop: `grid.tga` (128×128, a dot grid) tiled in 120-px squares over (210, 190, 381, 231),
   last row and column cut, blend ADD, colour (0, 0.376, 0). The original passes zero UV divisors
   to the tiling helper (as2@0x4290c0), so its UVs are degenerate; GUESS for the intended look:
   each 120-px tile shows texels 0..120.
2. The 2D list is flushed and the render lists cleared, then:
   - **unlocked** helicopter: a 3D view (as2@0x4079c0): viewport (170, 160, 460, 270) in virtual
     pixels scaled to the window, FOV 60, cleared depth; the preview entity at origin (0, 0, −100)
     with angles (100 + 5·sin(2·mt), spin − 120, 0) degrees, spin = +60°/s (as2@0x221917c, never
     reset); attached children drawn with it (as2@0x407850). The preview entity is the selected
     helicopter's object (helicopter table as2@0x49ddf9) built by `G_InitObject` without its
     script (as2@0x428f10), rebuilt at every change;
   - **locked** helicopter: `gfx\ui\helicna.tga` (256×128, a dark helicopter silhouette) at its
     natural size at (272, 241), ALPHA, tinted (0, 0.376, 0); "NOT AVAILABLE" (as2@0x48de78)
     centred at (400, 300), red 0x000000FF.
3. Name at (220, 200), green, from table as2@0x49cbec by helicopter index (6 names, section 7).
4. "Speed:" (as2@0x48de88) at (220, 378) and "Armor:" (as2@0x48de90) at (220, 398), green. Bars
   in 0x6000FF00 (green, alpha 0.376), blend ALPHA: outlines (300, 381, 280, 10) and
   (300, 401, 280, 10); fills from the same left edge, width speed × 280 / 1.5 and maximum health
   × 280 / 800 of the preview entity (the definition's `speed` field and maximum health). Shown for
   locked helicopters too.
5. The start/Accept button is disabled (grey) while player 1's selection is locked, or in
   two-player mode while either player's is.
6. Items; `UI_DrawPanel(160, 160, 480, 290, f)` (height 310 in two-player mode) titled "Choose
   Helicopter" (as2@0x48de98); title logo.

Actions: the arrows set the shown player's helicopter index to (index ± 1) mod 6, **write it to the
player record at once** and rebuild the preview (locked helicopters can be browsed); the Player
spinner switches the shown player; Back and Accept pop; Start/Continue frees the level, pops every
menu, loads the selected mission and runs `G_NewGame` (5.1). No key callback: Esc pops. Quirks
(issue 240): leaving with Esc keeps a locked choice in the record; the shown player starts from
the spinner's stale value of the previous opening.

### 3.19 Portrait dialogues (S9b, S13b, new; VERIFIED-CODE `M_ShowPortraitDialog` as2@0x4234b0, draw 0x4231b0, key 0x423050; (emu))

`M_ShowPortraitDialog(mission, isEnd)` is called by level start (start dialogue) and `EndLevel`
(end dialogue). Dialogue table as2@0x49d530: 36 pointers indexed mission × 2 + isEnd; each points
to {speaker, text} pairs (8 bytes), ended by a pair with a null text. Speaker 0 is the officer,
1 the pilot. Start dialogues: missions 1, 2, 3, 5, 6, 8, 11, 12, 14, 15, 17, 18; end dialogues:
1, 2, 3, 4, 5, 6, 9, 10, 11, 12, 15, 16; 40 pages in all (section 7).

- No dialogue: a start slot does nothing (the mission runs unpaused); an end slot pauses and calls
  `G_MissionComplete` at once.
- Otherwise: paused; `gfx\ui\portraits2.tga` registered; an item-less menu is pushed.
- Opening: the fade value rises by 4 × frametime (0.25 s); only the panel is drawn meanwhile.
- Open: portrait at (120, 420, 80, 120), ALPHA, white: officer texel (0, 8, 80, 120), pilot texel
  (80, 8, 80, 120) (checked on the atlas). Text typed at one character per 0.05 s (at most one per
  frame), no sound; lines split at '\n', left-aligned at x 220 from y 430, 20 px apart, white,
  additive, no markup. A complete page stays 3 s, then the next page starts; after the last one
  the dialogue closes.
- Every frame: `UI_DrawPanel(100, 400, 600, 160, fade)` without title, then the (empty) items.
- Keys: Enter, Esc, Space, left and right button complete a page that is still typing, or skip to
  the next page (the timer is set to 4 s); they never close the dialogue at once and are ignored
  while it closes. Other keys go to the generic handler.
- Closing: the fade value falls at the same rate; at 0 the menu pops; an end dialogue then calls
  `G_MissionComplete` (the game stays paused); a start dialogue clears paused and `p_action` of
  both players.
- HUD: visible behind a start dialogue, hidden behind an end dialogue.

### 3.20 Credits (S8b, new screen; VERIFIED-CODE `M_CreditsMenu` as2@0x423d90, draw `M_PageCredits` as2@0x423ca0; (emu))

Relation: replaces page 10 of the base's Information screen. Title logo; 21 lines centred on x 400
from y 120, 18 px apart, orange with markup (the names are in braces, so white): seven role/name
groups separated by one-space lines (keys `credits.0` … `credits.20`, section 7). "  Back  " text
button id 1, left 50, y 520, pops. No panel. Esc pops.

### 3.21 Demo nag screen, purchase button, play-time limit (dropped)

Relation: **new in the data, dead in the code**. VERIFIED-CODE unless stated.

- Settings.xml `<Demo><Purchase showButtonInMainMenu shellCommand>` and `<NagScreen show>` are
  parsed (as2@0x41e130; flags as2@0x22192a0, 0x22192a1; command as2@0x2219284). The two flags are
  never read (a raw scan of the executable for their addresses finds only the parser and the
  free routine). The command is read only by main-menu action id 6 (stop sound, shell-execute the
  command, save `game.bin`, close), and no builder adds an item with id 6. The shipped file sets
  both flags to 1 with the command `Register.url` and says in a comment that only the demo build
  uses the tag (VERIFIED-DATA).
- The nag texts (a heading and eight feature lines, pointer table as2@0x49cc10) and three
  end-of-demo lines (table as2@0x49df48, after the attract-level names) are unreferenced data.
- The demo play-time counter (1 h, written to the registry every 60 s) never runs: its gate byte
  as2@0x2219138 is never set (engine-behaviour.delta.md 1.2).
- New tag `<AtExit shellCommand>`: shell-executes a command after `config.ini` is written at exit
  (as2@0x405bf0); absent from the shipped file.

What we do: drop all of them (the purchase command, nag screen, demo lines, play-time counter and
AtExit launch), as we dropped online scores and the CD check. Nothing visible is lost with the
full version's data.

## 4. HUD

Relation to the base §4: **changed**: new art (`gfx\ui\mainbar2.tga`), segmented health bar
scaled by the helicopter's maximum health, weapon level pips, up to 10 lives, timer power-ups,
dimmed unselected icons; the level-name typewriter, messages and number routine are the same.
Every layout below was confirmed by running `HUD_Draw1P` and `HUD_Draw2P` in the emulator with
chosen player records ((emu), VERIFIED-CODE at those addresses); every atlas rectangle was checked
on the image.

### 4.1 When and in what order (VERIFIED-CODE `HUD_Frame` as2@0x40ac80, `G_Frame` as2@0x410fd0)

Same rule as the base: nothing on an intermission level (as2@0x54329a) or while the HUD is hidden
(as2@0x54329b); pause does not hide it. `HUD_Frame` draws the level-name typewriter
(as2@0x407ab0), then `HUD_Draw2P` (as2@0x408b00) when the two-player flag (as2@0x2219145) is set,
else `HUD_Draw1P` (as2@0x407d20), then the cheat message (as2@0x406f80). **No mouse-control
cursor any more.** Nothing is drawn for portrait dialogues. `G_Frame` then calls the (dead) fade
overlay and the debug statistics overlay (4.8), then renders.

HUD textures (`CL_LoadHudAssets` as2@0x4077b0): `gfx\ui\mainbar2.tga` (256×256 RGBA, new),
`gfx\ui\life.tga` (32×32, paletted, black background), `gfx\ui\weapons.tga`, `missiles.tga`,
`items.tga` (256×128), `gfx\ui\font.tga` numbers, `sounds\type.wav`; `gfx\mc_cur.tga` and
`gfx\lightning2.tga` are registered but not drawn by the HUD.

**Atlas `gfx\ui\mainbar2.tga`** (VERIFIED-DATA, each rectangle seen on the image):

| Piece | Texel (x, y, w, h) | Content |
|---|---|---|
| `bar_fill` | (0, 0, 5n + 17, 34) | full width 227: the bolt cap (17 px) and 42 segments of 5 px, red to green |
| `bar_frame` | (0, 34, 232, 34) | the same bar, empty |
| `score_frame` | (0, 68, 232, 34) | a plain frame |
| `box` | (0, 102, 87, 60) | the weapon, missile and power-up box |
| `level_on` | (16, 164, 7k, 6) | bright red pips, 7 px apart |
| `level_max` | (16, 176, 7k, 6) | dim pips |
| `sel_line` | (21, 190, 53, 1) | the green selection line |

`level_on`, `level_max` and `sel_line` have alpha 0 everywhere and are drawn with ADD, so they
show only if the ADD blend ignores the texture's alpha (source × 1 + destination). A green eagle
emblem near texel (98, 108) is not drawn by any code.

Colours (VERIFIED-CODE): frames, fill and boxes white, ALPHA (the base: grey, ADD); lives grey
0.627, ADD; counts (0.816, 0.251, 0) for the selected type, (0.502, 0.031, 0) for the others, ADD;
score (0.753, 0.188, 0), ADD (these three as the base).

### 4.2 One-player HUD (VERIFIED-CODE as2@0x407d20, (emu))

Values are player 1's record (as2@0x20c5ad0; offsets engine-behaviour.delta.md 7.1). In drawing
order:

| Element | Rectangle | Content |
|---|---|---|
| Lives | (15 + 32i, 555, 32, 32) for i < min(`p_lives`, **10**) | `life.tga` whole, ADD, grey 0.627 |
| Health frame | (0, 6, 232, 34) | `bar_frame`, ALPHA |
| (clamp) | | if the player entity's health exceeds its maximum health (entity +0x70), health = maximum (engine-behaviour.delta.md 7.4) |
| Health fill | (0, 6, 5n + 17, 34) with n = ftol(health / maximum × 42) | `bar_fill` of the same width, ALPHA; the original lets n go negative for a negative health: clamp n to 0..42 |
| Weapon box | (0, 40, 87, 60) | `box`, ALPHA |
| Weapon level (new) | from (18, 51): `level_max` 7 × maxLevel[w] wide, then `level_on` 7 × level wide, both 6 high, ADD | w = round(`p_weapon`); maxLevel from table as2@0x49e1c4 = {4, 5, 7, 8, 5, 5, 4, 5, 3}; level = the player's upgrade level of slot w. Drawn even when the weapon has no icon |
| Weapon icon | (15, 57, 66, 35) | `weapons.tga` UVs of slot w (4.4), ADD, white; only if w < 9 |
| Missile column | per missile type 0..4 with a count ≠ 0, packed in type order; frame top F = 100, 160, 220 … (step 60) | `box` at (0, F); icon (15, F + 17, 66, 35) ALPHA, white with alpha 0.251 unless it is the selected type (then alpha 1); if selected, `sel_line` at (22, F + 5, 53, 1) ADD; count always, number font scale 0.75 at x = ftol(80 − 10.5 × digits), y = F + 13 |
| Score frame | covering (568, 6, 232, 34), mirrored | `score_frame` |
| Score | number font scale 1 at (600, 17), left-aligned | ftol(`p_scores`) + banked score |
| Power-up column | per power-up slot 0..15 with a count ≠ 0, packed; frame top F = 44, 104, … (step 60) | `box` mirrored covering (713, F, 87, 60); when the count is > 0: if selected, `sel_line` mirrored covering (725, F + 5, 53, 1), ADD, then the count at x = ftol(782 − 10.5 × digits), y = F + 13, scale 0.75, colours as the missiles; then the icon at (721, F + 17, 66, 35) (4.4) |

"Mirrored" means the original draws the quad from the right edge with a negative width, which
flips the picture horizontally. Counts show from 1 upwards (the base: power-up counts only above
1). There is no boss bar, no star counter and no lives number (boss health stays the 3D sprite
bar, base §4.2).

### 4.3 Two-player HUD (VERIFIED-CODE as2@0x408b00, (emu))

Player 2's record is as2@0x20c5c34. Both players' health is clamped to their maximum. Same pieces
and colours as 4.2.

| Element | Player 1 | Player 2 |
|---|---|---|
| Lives (max 10) | (15 + 32i, 555) | (753 − 32i, 555) |
| Health frame | (0, 6) | mirrored, covering (568, 6, 232, 34) |
| Health fill | (0, 6, w, 34) | mirrored, from x = 800 leftwards, same UVs |
| Score frame | (0, 40), not mirrored | mirrored, covering (568, 40, 232, 34) |
| Score | right-aligned: x = 200 − 14 × digits, y 51 | (600, 51), left-aligned |
| Weapon box | (0, 74) | mirrored, covering (713, 74, 87, 60) |
| Level pips | from (18, 85) | mirrored, right end at x 782, y 85 |
| Weapon icon | (15, 91) | (719, 91) |
| Missile frames, top F | from 134, step 60 | same F, frames mirrored at 713 |
| Missile icon | (15, F + 13) | (719, F + 13) |
| Missile selection line | (22, F + 5) | mirrored, x 725..778, F + 5 |
| Missile count, y = F + 13 | right end at x 80 | right end at x 780 |
| Power-ups | below the missiles in the same column: frame (0, F′), icon (15, F′ + 17), line at F′ + 5, count right end at 80, y F′ + 13 | frames mirrored at 713, icon (721, F′ + 17), line x 725..778, count right end at 782 |

There are no "P1"/"P2" labels.

### 4.4 Icon atlases (VERIFIED-CODE, tables read from the executable; cell names confirmed by the Information pages, 3.14)

All cells are 66×35, drawn at 66×35.

Weapons (`weapons.tga`, UV table as2@0x49e1e8, 9 entries, ADD):

| Slot | Weapon (Information page heading) | UV (s0, t0, s1, t1) | Texel |
|---|---|---|---|
| 0 | Machine Gun | (0, 0.727, 0.258, 1) | (0, 0, 66, 35) |
| 1 | Impulse Gun | (0.516, 0.727, 0.774, 1) | (132, 0, 66, 35) |
| 2 | Plasma Cannon | (0.774, 0.453, 1, 0.727) | (198, 35, 58, 35) |
| 3 | Quantum Gun (laser) | (0, 0.453, 0.258, 0.727) | (0, 35, 66, 35) |
| 4 | Big Laser | (0.258, 0.453, 0.516, 0.727) | (66, 35, 66, 35) |
| 5 | Lightning Gun | (0.516, 0.453, 0.774, 0.727) | (132, 35, 66, 35) |
| 6 | Wave Gun | (0.258, 0.727, 0.516, 1) | (66, 0, 66, 35) |
| 7 | Missile Gun | (0.774, 0.727, 1, 1) | (198, 0, 58, 35) |
| 8 | Flamethrower | (0, 0.18, 0.258, 0.453) | (0, 70, 66, 35) |

(The slot numbers are the weapon ids of engine-behaviour.delta.md 8.2; cells narrower than 66
texels are stretched.)

Missiles (`missiles.tga`, table as2@0x49e278, same values as the base; ALPHA): 0 Small
(0, 0, 66, 35); 1 Big (132, 0, 66, 35); 2 Small Heat Seeking (66, 0, 66, 35); 3 Big Heat Seeking
(198, 0, 57, 35); 4 M.A.D. (0, 35, 66, 29) stretched to 35 high.

Power-ups (`items.tga`, a switch at as2@0x408838 over slots 0 to 9; slots 10 to 15 have no icon):

| Slot | Power-up | Texel | Blend and colour |
|---|---|---|---|
| 0 | lightning bomb | (0, 35, 66, 35) | ADD; the cell's alpha is 0; grey 0.251 when not selected, white when selected |
| 1 | nuclear bomb | (198, 0, 57, 35) | ALPHA |
| 2 | rocket strike | (66, 0, 66, 35) | ALPHA |
| 3 | cluster bomb | (132, 0, 66, 35) | ALPHA |
| 4 | annihilator | (66, 35, 66, 35) | ALPHA |
| 5 | satellite strike | (132, 35, 66, 35) | ALPHA |
| 6 | speed-up (timer) | (0, 0, 66, 35) | ALPHA, always opaque white |
| 7 | slow-down (timer) | (0, 70, 66, 35) | ALPHA, always opaque white |
| 8 | air support | (198, 35, 57, 35) | ALPHA |
| 9 | shield (timer) | (66, 70, 66, 35) | ALPHA, always opaque white |

The other ALPHA icons are white with alpha 0.251 unless selected (then 1).

**Timer power-ups (slots 6, 7, 9).** They are the ones power-up cycling skips
(engine-behaviour.delta.md 8.3). The item scripts set their count every frame to the seconds left
through `G_SetPowerUpCount` (speed-up 15 s, slow-down 10 s, shield 20 s, VERIFIED-DATA), so the HUD
shows them in the power-up column with a count-down number and an always-opaque icon; they are
never "selected".

### 4.5 Level-name typewriter (VERIFIED-CODE as2@0x407ab0)

Same as the base §4.5 (same constants and positions) except that the centring width skips `{` and
`}` (2.8). Level names come from `levels.txt`.

### 4.6 Messages (VERIFIED-CODE as2@0x406f80)

Same code as the base §4.6 (3 s, fade in the last second, white at y 555 with a black shadow).
Only the cheat handler posts messages; the six texts are compiled in (section 7, `cheat.*`).

### 4.7 Tutorial hints

A menu, not part of the HUD: 3.15.

### 4.8 Overlays

- The full-screen fade overlay (as2@0x40b1c0) has the base's shape and is still dead: its inputs
  are never written (VERIFIED-CODE). Omit it.
- **Statistics overlay** (debug, as2@0x40aa40, moved out of the renderer): additive font, white;
  labels at x 20, values at x 150, from y 200. `[Debug] ShowFPS` draws "FPS: %3i" (frames counted
  over 0.5 s × 2, as2@0x40a9e0); `ShowTris` and `ShowTexBinds` add a line each and advance 25;
  `ShowCounters` draws Models, Sprites, Marks and entities at y, +20, +40, +60 (the entity label
  is misspelt in the executable). Our engine may keep its own FPS counter instead (issue 140).

### 4.9 Issue 060 points for AS2

1. Blend modes: frames, fill and boxes ALPHA white; lives, pips, selection line, numbers ADD;
   weapon icons ADD; missile icons ALPHA; power-up icons ALPHA except slot 0 (ADD).
2. Cell size 66×35; weapon UVs from the table of 4.4 (9 entries).
3. Box: 87×60 from `mainbar2.tga` (0, 102).
4. Slots packed in type order; the selected entry is marked by the green line and full alpha
   (no longer by drawing its frame twice).
5. Counts: scale 0.75, top at F + 13, right end at x 80 / 782 (one player).
6. Typewriter and messages: as the base.
7. Weapon level: shown (new pips); stars and boss bar: not shown.
8. Two-player layout: 4.3.

## 5. Mission flow and progression

Relation to the base §5: **changed**: a campaign checkpoint written at every mission end and
resumed from the menu, a weapon loadout per mission, portrait dialogues before the
mission-complete screen, a helicopter selection screen, 18 missions and 6 helicopters. The game
rules are in [engine-behaviour.delta.md](engine-behaviour.delta.md) 7, 8.2 and 10; this section
says what the front end does to them, and lists where the two documents disagree (5.12).

### 5.1 Starting a game (`G_NewGame` as2@0x410dc0, VERIFIED-CODE)

Only the helicopter selection's Start/Continue button (id 1) starts a game: it frees the level,
pops every menu, looks up the selected mission's level record and runs `G_NewGame`:

1. If the selected mission index is 0, or differs from the **checkpoint mission**
   (as2@0x49ddf4), the checkpoint is reset: mission −1 and, in both player records, checkpoint
   lives 2, score 0, rank 0.
2. For both records: lives at level start = checkpoint lives, banked score = checkpoint score,
   rank accumulator = checkpoint rank.
3. With `-god` on the command line, god mode.
4. The mission's weapon loadout (engine-behaviour.delta.md 8.2) is applied to both players.
5. `G_BeginLevel` (level start, 5.2).

So picking the mission that follows the last completed one resumes the campaign ("Continue" on the
button) with its lives, score and rank; any other choice starts fresh with three helicopters.
The check is repeatable: the checkpoint is kept, so Continue can be chosen again after a Quit.
Mission index 0 (the tutorial) can never be a Continue.

### 5.2 Level start

As engine-behaviour.delta.md 7.4 and 10.2: per player, `p_lives` = lives at level start,
`p_scores`, `p_stars`, kills = 0, `p_counter3` = 1, `p_action` = 0; weapon upgrades untouched (set
by the loadout on a new game and on both restarts, carried over by Next); missiles and power-ups
cleared at every spawn. The loading comic (3.16) is shown while the level loads; on missions with
a start dialogue the dialogue opens right after (3.19), with the game paused.

### 5.3 End of mission (`EndLevel` as2@0x40e730 → `M_ShowPortraitDialog` → `G_MissionComplete` as2@0x427f40)

1. `EndLevel` writes the checkpoint for the next mission (mission index + 1; for both records:
   lives = ftol(`p_lives`), score = ftol(banked + `p_scores`), rank = accumulator + stars ratio +
   0.5 × score ratio), hides the HUD, shows the end dialogue if the mission has one (3.19), and
   pauses.
2. When the dialogue has faded out (or at once, without a dialogue), `G_MissionComplete`: unlocks
   the helicopter named by the level's `enableHelic` when 0..5, unlocks mission (index + 1) mod 18,
   and pushes Game Complete after mission 18, otherwise Mission Complete.

So the unlocks happen after the end dialogue, and the checkpoint before it. Nothing is banked at
this point (the checkpoint already holds what banking would give).

### 5.4 Banking (`G_BankScore` as2@0x414540, same code as the base)

Banked score += `p_scores`; rank accumulator += stars ratio + 0.5 × score ratio; lives at level
start = `p_lives`, for each of the player count's records. Called by Mission Complete → Next,
Game Complete → Continue, Game Over → Quit. Not by Mission Complete → Quit or Restart, Game Over →
Restart, the in-game menu's Restart or Quit.

What each button does (VERIFIED-CODE as2@0x427a20, 0x428ac0, 0x428140, 0x42a8c0, 0x428fb0):

| Button | Lives | Score | Rank accumulator | Weapon upgrades | Checkpoint | High-score check |
|---|---|---|---|---|---|---|
| Mission complete: Next | lives at start = `p_lives` | banked | banked | kept | unchanged | no |
| Mission complete: Restart | as the mission began | mission score discarded | unchanged | mission loadout | unchanged | no |
| Mission complete: Choose Helicopter, then Accept or Esc | – | – | – | – | – | – (only the helicopter index changes, at each arrow click) |
| Mission complete: Quit | – | not banked | not banked | – | still the finished mission's | no |
| Game over: Restart | as the mission began | mission score discarded | unchanged | mission loadout | unchanged | no |
| Game over: Quit | banked | banked | banked | – | unchanged | yes |
| In-game menu: Restart | as the mission began | discarded | unchanged | mission loadout | unchanged | no |
| In-game menu: Quit | – | lost | lost | – | unchanged | no |
| Game complete: Continue | banked | banked | banked | – | mission 18 (never offered) | yes |

Consequence: Mission Complete → Quit loses nothing of the finished mission, because Start Game
then preselects the next mission and its button reads Continue (VERIFIED-CODE chain
as2@0x40e730, 0x42d8b0, 0x429530, 0x410dc0). After mission 18 the checkpoint is 18, outside the
preselection range 1..17, so a finished campaign is never offered as Continue.

### 5.5 Score, maximum score, stars, kills

As the base §5.5 and engine-behaviour.delta.md 6.1, 10.2, 10.4 (kills capped at the enemy total;
the objects spawned while loading are not counted in the totals, issue 211). The meaning of the
fields is confirmed by the screens that read them (answer to rcsl-builtins-semantics.delta.md
open question 1): record +0x140 banked score (compared by the high-score check, restored by
`G_NewGame`), +0x144 rank accumulator (the rank line), +0x148 kills (the "Enemies destroyed" line),
+0x158/+0x15C/+0x160 checkpoint lives/score/rank (restored by `G_NewGame`); as2@0x5432c4 maximum
level score and as2@0x543294 star total (the denominators of the rank line and of "Stars
collected"), as2@0x5432c0 enemy total, as2@0x49ddf4 the checkpoint mission (not "the highest level
unlocked"). VERIFIED-CODE as2@0x427b60, 0x410dc0, 0x414610.

### 5.6 Lives and continues

As the base §5.6 with engine-behaviour.delta.md 7.4: a fresh game gives two spare helicopters;
extra lives from the 1-Up item; up to 10 icons on the HUD; game over when `p_lives` < 0 (one
player) or both < 0 (two players); Restart replays the mission with the lives it began with, any
number of times. The checkpoint (5.1) is the new form of "continue" across sessions: it is saved
in `game.bin` (6.1) unless a cheat was used.

### 5.7 Unlocking

- Missions: the Start Game list shows all 18; locked ones are disabled. Missions 1 and 2 are
  unlocked in a fresh save; completing mission i unlocks mission i + 1 (mission 18 unlocks mission
  1, which is always unlocked). VERIFIED-CODE as2@0x427f40.
- Helicopters: entry 0 is unlocked in a fresh save; missions 4, 7, 10, 13 and 16 unlock entries 1
  to 5 through `enableHelic` (VERIFIED-DATA, `levels.txt`). Mission Complete then shows "New
  helicopter is available." The selection screen shows every helicopter; a locked one is drawn as
  the grey silhouette with "NOT AVAILABLE" and cannot be started with (3.18).
- Unlocks are global to the save, never re-locked.

### 5.8 The six helicopters

Table as2@0x49ddf8 order (engine-behaviour.delta.md 7.6), display names (table as2@0x49cbec,
texts in section 7), and the two bars of the selection screen, from the definitions in
`objects\player.obj` (VERIFIED-DATA):

| Index | Object | Display name | Health (Armor bar = health / 800) | `speed` (Speed bar = speed / 1.5) | Unlocked by |
|---|---|---|---|---|---|
| 0 | `player_1` | Green Viper | 500 | 1.0 | – |
| 1 | `player_2` | Sky Keeper | 400 | 1.25 | mission 4 |
| 2 | `player_4` | Steel Falcon | 600 | 0.85 | mission 7 |
| 3 | `player_6` | Venomous Thorn | 600 | 1.2 | mission 10 |
| 4 | `player_5` | DG 17-F | 800 | 0.7 | mission 13 |
| 5 | `player_3` | Lava Hammer | 300 | 1.4 | mission 16 |

Unlike the first game, the helicopters differ in play: health and the `speed` factor
(engine-behaviour.delta.md 7.3, 7.4). At boot both players' index is 0; the choice is not saved.

### 5.9 Two-player rules in the menus

- Chosen with Start Game's "Game mode" spinner ("Single Player" / "Cooperative"), which sets the
  flag at once; Next sets the player count.
- The selection screen gets a "Player:" spinner; each player picks with the arrows; Start needs
  both choices unlocked; the panel is 20 px taller.
- Mission Complete hides the statistics (VERIFIED-CODE as2@0x427b60 tests the flag) but still
  shows the new-helicopter line; there is no high-score check; banking covers both records; the
  checkpoint and `G_NewGame` always treat both records.
- Game over only when both are out (engine-behaviour.delta.md 7.5).

### 5.10 Difficulty

Chosen per game in Start Game (Very Easy … Nightmare, reset to Normal at each opening); the factor
table and its use are unchanged (engine-behaviour.delta.md 6.3). A Continue uses the difficulty
chosen now, not the one of the checkpoint (the checkpoint does not store it; VERIFIED-CODE
as2@0x406c70, 0x410dc0). The rank factor applied by Mission Complete is read from as2@0x49dee4
(GUESS: the fourth factor of the current difficulty, copied there at level start next to the
damage factor as2@0x49ded8).

### 5.11 Rank

Same as the base §5.11 (thresholds 3, 7.5, 14, 22, 30 at as2@0x49e654; names at as2@0x49cba4;
Cheater when a cheat was used; `G_RankIndex` as2@0x4145d0 identical). Mission Complete shows the
rank of (stars ratio + 0.5 × score ratio + accumulator) × rank factor; the high-score entry stores
the rank of the accumulator × rank factor after banking.

### 5.12 Special missions and disagreements with engine-behaviour.delta.md

- Tutorial (mission 1), bonus missions (7, 13), boss missions (6, 12, 18): no front-end code treats
  them specially (VERIFIED-CODE, the screens of this section read); their dialogues are ordinary
  table entries. Missions 7 and 13 have neither a start nor an end dialogue.

Disagreements (this document wins for the front end; the delta is not edited):

| Delta | Says | This spec | Evidence |
|---|---|---|---|
| 1.3 state 4 | the selection menu "replaces the Start Game grid" | the start is two screens: Start Game (mission, difficulty, game mode, Next) then the helicopter selection | as2@0x42d7b0 |
| 1.3 state 4, open question 7 | " Accept " when opened from mission complete; case 1 of the action starts a game | " Accept " has id 8 and only pops back to Mission Complete; the helicopter is written by the arrows; case 1 is Start/Continue only | as2@0x428fb0, 0x4295e8 |
| 1.3 state 7 | in-game menu "same as v1.70" | text buttons and a fourth button, Restart, which re-applies the loadout | as2@0x42aa20, 0x42a8c0 |
| 8.2 | the loadout is applied by `G_NewGame` and the two restarts | also by the in-game menu's Restart | as2@0x42a8c0 |
| 10.3 step 2 | `G_MissionComplete` unlocks | yes, after the end dialogue has faded out, not at `EndLevel` | as2@0x4231b0 |
| open question 4 | how the end dialogue hands over | answered: 3.19 and 5.3 | as2@0x4231b0, 0x4234b0 |

## 6. Save file and settings

Relation to the base §6: **changed**: smaller tables, a checkpoint block in `game.bin`, new video
keys and new defaults in `config.ini`. As for the first game, our engine writes its own formats
(spec README); this section says what must be persisted, when, with which defaults, and records
the original layouts for an optional importer.

### 6.1 Progress (`game.bin`; VERIFIED-CODE `G_LoadBin` as2@0x4069e0, `G_SaveBin` as2@0x406c70)

**What is persisted:** the 15-entry high-score table, the 6 helicopter unlock flags, the 18 mission
unlock flags, and **the campaign checkpoint** (mission, and per player lives, banked score and
rank accumulator; 5.1). Not persisted: difficulty, player count, helicopter choice, weapon
upgrades, a mission in progress. When a cheat was used in the session, the checkpoint is saved as
"none" (mission −1, zeros).

**When it is written:** on Exit → YES (when the game is ready: the level is freed first), before
the Options → Apply video restart (6.3), before the fatal-error message box (as2@0x405750), and by
the dormant purchase action (3.21). Every call site was found (as2@0x405777, 0x405b53, 0x410fc3,
0x427fe4). **Closing the window does not save it**: the WM_CLOSE path (`Sys_Quit` as2@0x405bf0,
reached from as2@0x415d8c) writes `config.ini` only. The same holds for the first game (the
callers of v170@0x4011b0 are the exit confirmation, the video restart, the fatal-error box and the
dormant purchase action), which corrects the base §6.1 (section 10). Read once at start-up
(`Sys_Init`). Our engine should write after every change (issue 091 item 9 and issue 130 §5 already
do so), which also covers the checkpoint written at every `EndLevel`.

**Original layout** (1692 bytes; the base's scheme with smaller tables and a second block):

| Offset | Size | Content |
|---|---|---|
| 0 | 4 | float version 1.0; any other value: the file is ignored ("Illegal version") |
| 4 | 256 | XOR key, each byte round(rand() / 32767 × 255), new at every save |
| 260 | 4 | u32 CRC-16/CCITT (table as2@0x49e2d8, start 0xFFFF, no final XOR) of the encrypted payload |
| 264 | 0x574 | payload, byte i XORed with key[i & 0xFF] |
| 1660 | 0x1C | checkpoint block, byte i XORed with key[i & 0xFF] (the key index restarts at 0) |
| 1688 | 4 | u32 CRC of the encrypted checkpoint block |

Payload: +0 u32 (always 0; the global it comes from is cleared after loading, unused); +4 15 ×
{char name[32], i32 score, i32 rank index}; +604 6 × {u8 unlocked, char object name[32]}; +802
18 × {u8 unlocked, char level id[32]}. The load copies these blocks over the tables at
as2@0x49db78, 0x49ddf8 and 0x49df60, names included. Checkpoint block: i32 mission (−1 none); then
for player 1 and player 2: i32 lives, i32 score, f32 rank accumulator. A bad payload CRC keeps all
defaults ("corrupted"); a bad checkpoint CRC keeps the payload and resets the checkpoint.

**Fresh-install defaults** (compiled in, VERIFIED-CODE): the high-score table is the base's (same
15 names, scores and ranks, as2@0x49db78); helicopter 0 unlocked, 1–5 locked; missions 1 and 2
unlocked, 3–18 locked; checkpoint −1.

The shipped `game.bin` of the install is a played save (VERIFIED-DATA: both CRCs valid; default
high scores; helicopter 0 and missions 1–4 unlocked; a checkpoint for mission 4). Our engine starts
from the compiled-in defaults, not from that file.

### 6.2 Settings (`config.ini`; VERIFIED-CODE `CFG_Read` as2@0x401b90, `CFG_Write` as2@0x402510, `CFG_Init` as2@0x402ab0)

Read at start-up (missing file: written with defaults and read again; `-setup` and `FirstRun` show
the setup dialog, dropped as in the base), written at program exit only (Options, Apply and the F
keys change memory only). Path: the current directory. Keys as in the base §6.2, with these
differences:

| Section / key | Default if missing | Meaning, range | vs base |
|---|---|---|---|
| System MouseControl | **1** | 0/1; relative mouse steering (engine-behaviour.delta.md 7.2) | default changed (was 0) |
| Display VideoMode | **−1** | **index into the list of display modes enumerated at start-up** (not a fixed table); −1 or out of range picks the last mode before the first one wider than 800 or 800 wide and taller than 600, normally 800×600 | meaning changed |
| Display RefreshRate | 0 | Hz; snapped at start and on Apply (as2@0x4011e0): kept if the mode lists it, else 85 if listed, else the rate just below the first rate above 85 | snapping new |
| Display **EnableNonStdModes** | 0 | 1: list every 16- and 32-bit mode; 0: only 4:3 modes (width / 4 = height / 3) | new |
| Display **ForceStdModes** | 0 | 1: add 640×480, 800×600, 1024×768 and 1280×960 at 60, 75 and 85 Hz when Windows does not report them | new |
| Controls, Controls2: Primary Attack, Missile Attack, Use Power-Up | 17 **200**, 16 **201**, 32 **202** (Ctrl/Shift/Space and the three mouse buttons) | "k1 k2", 0 read as unbound | defaults changed (the base: 203, 204, 205) |

Other keys and defaults are the base's (ShowHints 0, FirstRun 1, ShowLogo 1, UseSystemMouse 0,
Camera 1, RefreshRate 0, ColorDepth 0, Fullscreen 1, ForceFullscreen 0, WaitVSync 0, Brightness
0.6, TextureFilter 0, the Graphics switches, Debug counters, SfxVolume 0.5, MusicVolume 0.5,
Sound3D 0, the other binding defaults, Joystick keys). The writer spells `KeyUsePowerup`, the
reader `KeyUsePowerUp` (INI lookups ignore case). When no display mode qualifies, the start-up
retries with EnableNonStdModes, then ForceStdModes, then quits; the flags it turned on are saved.
Shipped `config.ini` (VERIFIED-DATA): VideoMode 2, RefreshRate 60, ColorDepth 32, both new
Display keys 0, MouseControl 1, FirstRun 0, both players' bindings equal to the defaults.

For our engine: EnableNonStdModes, ForceStdModes, VideoMode, RefreshRate and ColorDepth have no
meaning (window size from the command line, issue 130); keep them only for an importer.
**MouseControl defaults to 1** for AS2 profiles, and the default fire keys include the mouse
buttons.

### 6.3 Settings that exist only for the PC original

As the base §6.3. The Options → Apply video restart exists in AS2 (the symbol map's "removed" is
wrong, section 10): code at as2@0x405b40, reached from the Apply action, frees the level and saves
`game.bin` when the game is ready, shuts down sound and the renderer, re-creates window, sound and
Direct3D, and runs `G_Init(1)` (no logo pages, a new random attract level, main menu). Our engine
does not reproduce it.

### 6.4 Settings.xml

Tags read (as2@0x41e130): `CheckCD`, `PostScores` (both dropped features), `Demo/Purchase`,
`Demo/NagScreen` (dead, 3.21), `Info version copyright` (main menu), **`AtExit shellCommand`**
(new, dropped), `Intros` with `BuiltIn`, `Image` + `BackColor`, **`Video`** (new, parsed and
ignored), `Logotypes/Image`. The engine's Settings.xml reader (`parseSettingsXml`) already covers
what we use. The shipped file has no re-release branding (copyright "… DivoGames"); the branding
filter of the first game is harmless.
