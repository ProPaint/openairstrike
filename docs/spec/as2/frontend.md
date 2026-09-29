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
