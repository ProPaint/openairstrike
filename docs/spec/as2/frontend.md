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
