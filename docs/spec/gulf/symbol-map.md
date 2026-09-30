# Gulf Thunder v2.71 (`gulf`): symbol map against AirStrike 2

Package F1. How the Gulf Thunder executable (`AirStrike3D II - Gulf.exe`) was mapped onto the
AirStrike 2 executable (`AirStrike3D II.exe`, [../as2/symbol-map.md](../as2/symbol-map.md)),
the statistics, and the list of every function whose code differs. No decompiled code and no
disassembly: functions are described in words, by address, name, strings and constants.

The two executables were linked five minutes apart (PE timestamps 0x4649BA68 and 0x4649BB88)
from one code base. The result of this package in one sentence: **everything that runs the
game world is the same code as AirStrike 2** (script interpreter, all 97 builtin functions,
entities, collision, player, weapons, camera, HUD, terrain, water, particles, renderer, sound,
save logic); what differs is a handful of **tables** (24 missions, 3 helicopters, a new
mission loadout table, portrait dialogues, HUD weapon icons) and the **look of the front end**
(title bar, panel, text button, list, colours, loading screen, no intro comic).

## Files

| File | Content |
|---|---|
| `re/symbols_gulf.csv` | one row per Gulf Thunder function (1910): `address,name,subsystem,description,confidence,evidence,as2_address,vs_as2,v170_address` |
| `re/symbols_gulf_data.csv` | 106 globals and tables: `address,name,kind,description,confidence,evidence,as2_address,v170_address` (descriptions are AirStrike 2's; Gulf Thunder's counts are in the delta specs) |
| `re/tools/overrides_as2_gulf.csv` | the manual decisions: 4 pairs of interchangeable XML stubs, 11 AirStrike 2 functions marked removed |
| `re/tools/names_gulf.csv` | the one new game function |
| `re/tools/globals_as2_gulf.csv` | AirStrike 2 data named by this package, mapped to Gulf Thunder |
| `re/tools/sequel_diff.py` | the comparison of two builds: function classes, constant differences, data sections |
| `re/tools/emu_screens.py` | draws a front-end screen of either sequel in the 2D emulator (`emu2d.py`, which now has a `gulf` entry) |
| `re/tools/builtin_reach.py` | the builtins reachable from one mission (priorities of the builtins delta) |

`vs_as2` is the comparison with the AirStrike 2 counterpart:

| Value | Meaning |
|---|---|
| `same` | identical normalised instruction sequence (addresses masked) **and** every read-only constant it loads (floats, doubles, strings) has the same content |
| `same-shape-other-constants` | identical instruction sequence, but a read-only constant or an immediate that is not an address differs (a colour, a string, a count) |
| `same-opcodes` | same mnemonics, other operands (a bound, an offset) |
| `changed` | other instructions |
| `new` | no AirStrike 2 counterpart |

The mask of the AirStrike 2 map hid the content of read-only constants; `sequel_diff.py
--consts` closes that gap by comparing, for every aligned operand of two same-shape functions,
the bytes it points to. Initial values of writable tables are compared separately
(`sequel_diff.py --data`, section "Data" below).

## Reproduce

```
export AS3D_DATA_ROOT=/path/to/airstrike3d
python3 re/tools/match_symbols.py --from as2 --to gulf --out re/symbols_gulf.csv --pairs /tmp/pairs.json
python3 re/tools/match_symbols.py --from as2 --to gulf --check re/symbols_gulf.csv
python3 re/tools/match_data.py --from as2 --to gulf --pairs /tmp/pairs.json \
    --out re/symbols_gulf_data.csv --dump /tmp/datamap.json
python3 re/tools/sequel_diff.py --pairs /tmp/pairs.json --functions --consts --data \
    --names re/symbols_as2.csv
```

About two minutes in all. `--check` verifies addresses, unique names, a row for every builtin
table entry, and prints the statistics below.

## Method

The AirStrike 2 matcher with three additions (`match_symbols.py`, enabled whenever `--from`
is not `v170`):

1. Names: `re/symbols_as2.csv`, then the corrections of the later AirStrike 2 packages
   (`symbols_as2_render.csv`, `_builtins.csv`, `_frontend.csv`, `_game.csv`; the last wins,
   the replaced name is kept as an alias in the description).
2. A position pass: between two matched neighbours, an unmatched run of the same length on
   both sides is paired in order when every pair has the same mnemonic sequence (the linker
   kept the function order). It pairs the small functions whose shape is not unique.
3. The comparison class of every pair (`vs_as2`).

Steps and their yield: builtin table 97 pairs, unique strings 135, single-caller imports 35,
function-ID names 373, unique identical shapes 846, propagation 220, shape and graph passes
11, position pass 183, manual 4.

## Statistics

Rows: 1910 Gulf Thunder functions; 1904 have an AirStrike 2 counterpart; 1904 of the 1919
AirStrike 2 functions are mapped, 11 are removed (below), 4 unnamed runtime helpers at the end
of the runtime range have no counterpart.

| Kind | same | same-shape-other-constants | same-opcodes | changed | new |
|---|---|---|---|---|---|
| all (1910) | 1835 | 23 | 22 | 24 | 6 |
| game code (574) | 523 | 23 | 8 | 24 | 1 |
| runtime and library (1336) | 1312 | 0 | 14 | 0 | 5 |

By bytes of code: same 402,134; other constants 19,616; same opcodes 13,902; changed 13,020;
new 387. **96.3 % of the code is byte-for-byte the same logic.** The 14 `same-opcodes`
library functions are Direct3DX functions whose jump tables moved; the 5 new runtime rows are
compiler funclets.

Per subsystem (game code only where it matters):

| Subsystem | Functions | same | other constants | same opcodes | changed | new |
|---|---|---|---|---|---|---|
| script-builtin | 96 | 96 | 0 | 0 | 0 | 0 |
| script (VM, loader) | 10 | 10 | 0 | 0 | 0 | 0 |
| game | 53 | 50 | 1 | 1 | 1 | 0 |
| player | 4 | 3 | 0 | 1 | 0 | 0 |
| hud | 10 | 8 | 0 | 0 | 2 | 0 |
| terrain, particle, model, shadow, render, cull, texture, frame, light, sprite, decal, effect, material, gl | 110 | 109 | 1 | 0 | 0 | 0 |
| sound, fs, math, parse, net, input, camera, weapons, level, cheat, util | 101 | 101 | 0 | 0 | 0 | 0 |
| save | 4 | 2 | 0 | 0 | 2 | 0 |
| settings, config, window, sys | 52 | 46 | 6 | 0 | 0 | 0 |
| intro | 17 | 15 | 1 | 0 | 1 | 0 |
| ui | 41 | 30 | 4 | 0 | 7 | 0 |
| menu | 61 | 26 | 18 | 6 | 10 | 1 |
| 2d | 18 | 17 | 0 | 0 | 1 | 0 |

The builtin table (gulf@0x0049b448, 101 records) has the same names in the same order as
AirStrike 2's and points to 97 distinct functions, all `same`. The script-global table
(gulf@0x0049b780, 28 records) has the same names in the same order.

## What differs, by impact on play

Every function that is not `same`, grouped. Behaviour is specified in the deltas named in the
last column.

### Game rules (small)

| Gulf | AirStrike 2 | Function | Class | Difference | Spec |
|---|---|---|---|---|---|
| `0x00412320` | `0x004138f0` | `G_SetMissionLoadout` | same-opcodes | row index clamped to 0..23 (AirStrike 2: 0..17); table gulf@0x00489bc0, 24 rows | engine-behaviour.delta.md 8.2 |
| `0x00426560` | `0x00427f40` | `G_MissionComplete` | same-opcodes | helicopter unlock bound 3 (6); mission unlock and game-complete test modulo 24 (18) | engine-behaviour.delta.md 10.3 |
| `0x00426040` | `0x00427a20` | `M_MissionCompleteAction` | same-opcodes | "Next" wraps modulo 24 | engine-behaviour.delta.md 10.3 |
| `0x004275e0` | `0x00428fb0` | `M_HeliSelectAction` | same-opcodes | helicopter index cycles modulo 3 | engine-behaviour.delta.md 7.6 |
| `0x00406940`, `0x00406bd0` | `0x004069e0`, `0x00406c70` | `G_LoadBin`, `G_SaveBin` | changed | payload 0x5d8 bytes: 3 helicopters and 24 missions | engine-behaviour.delta.md 10.5 |
| `0x004130f0` | `0x004146c0` | `G_InsertHighScore` | changed | one comparison: the new entry's "may post" test is "not zero" instead of "greater than zero" | frontend.delta.md 3.12 |

Plus tables read by `same` code: helicopter table gulf@0x0049bc70 (3 records), mission table
gulf@0x0049bd78 (24 records), portrait dialogue table gulf@0x0049b378 (48 pointers, 3 used),
HUD weapon icon table gulf@0x0049c0c0 and the weapon level-cap table gulf@0x0049c09c (section
"Data").

### Front end (the look)

| Gulf | AirStrike 2 | Function | Class | Difference |
|---|---|---|---|---|
| `0x00429b00` | `0x0042b610` | `UI_DrawMenuHeader` | changed | the title logo becomes a letterboxed title bar: black bars, grey rules, scan-line texture, `logo_gulf.tga` tinted towards a colour argument; takes (colour, scan lines) |
| `0x004252f0` | `0x00426ae0` | `UI_DrawPanel` | changed | new atlas pieces, other bar offsets, side rails instead of chains, title as an argument, red title |
| `0x004228a0`, `0x00422950` | `0x00423e40`, `0x00423f00` | `UI_LayoutTextButton`, `UI_DrawTextButton` | changed | button 37 high, no 46-pixel margin, three pieces, no rivets, grey/red captions |
| `0x00423430` | `0x00424bb0` | `UI_DrawList` | changed | grey selection bar, new scroll-bar pieces, grey tint |
| `0x00422e80` | `0x004245a0` | `UI_DrawSlider` | changed | new atlas pieces (bar texel (117, 124, 128, 11), knob (249, 122, 6, 15)), grey/red |
| `0x00423b30` | `0x004252d0` | `UI_InitHeliGrid` | changed | unused widget (never added) |
| `0x004296d0` | `0x0042b1f0` | `UI_LoadAssets` | changed | registers `logo_gulf.tga`, `interface_gulf.tga` and the new `lines_gulf.tga` in place of `logo.tga` and `interface.tga`; `glow.tga` and `two3.tga` are still registered but nothing draws them |
| `0x0040ac20`, `0x0040acb0` | `0x0040acc0`, `0x0040adf0` | `SCR_SelectLoadingComic`, `SCR_DrawLoading` | changed | one comic set for every mission, faded tiles, title bar, bar at y 575 |
| `0x0040f0b0` | `0x00410530` | `G_StartIntros` | changed | no intro comic appended after the logo pages |
| 10 draw callbacks (`M_DrawMainMenu` 0x0042a160, `M_DrawStartGame` 0x0042c160, `M_DrawOptions` 0x0042b0f0, `M_DrawControls` 0x00422440, `M_DrawCredits` 0x004226e0, `M_DrawMissionComplete` 0x00426180, `M_DrawExitConfirm` 0x00426620, `M_DrawHeliSelect` 0x00427820, `M_DrawInGameMenu` 0x00428ec0, `M_DrawTopScores` 0x0042c7d0) | | | changed | call the title bar with its two arguments; credits list other names; mission complete's helicopter line bound 3 |
| 18 builders and page drawers | | | same-shape-other-constants / same-opcodes | colours (focus red 0xFF0000FF, text light grey 0xFFBFBFBF), caption padding, main and in-game menu positions, helicopter preview rectangles |
| `0x00428bb0` | (as2 code 0x0042a6e0) | `M_DrawInfoMenu` | new | 7 Information pages (AirStrike 2: 8; its third weapons page is gone) |

### Other constants

`MW_CreateWindow` (window title "Airstrike II: Gulf - Divo Games"), `Sys_WriteRegistryKey`
(another CLSID), and the XML helpers (a moved pointer only).

## Removed AirStrike 2 functions

| AirStrike 2 | Name | Evidence |
|---|---|---|
| `0x0040f000`..`0x00410160` (10 functions) | the intro comic's pages (begin, draw, end tests) | `G_StartIntros` gulf@0x0040f0b0 appends no comic page; no function of their size or strings exists |
| `0x00429df0` | `M_PageWeapons3` | the weapons pages say "Page 1 of 2" / "Page 2 of 2"; the information draw callback switches over 7 pages |

## Data

`sequel_diff.py --data` over the read-only (.rdata, 24,784 / 23,576 words) and initialised
(.data, 7,168 words) sections, pointers masked. Every changed region was attributed to its
referencing functions; the ones that matter:

| Gulf | AirStrike 2 | Table | Difference |
|---|---|---|---|
| `0x00489bc0` | `0x0048b3a8` | mission loadout, rows of 9 ints | 24 rows, other values (engine-behaviour.delta.md 8.2) |
| `0x0049bc70` | `0x0049ddf8` | helicopters {u8 unlocked, name[32]} | 3 records `player_1`, `player_2`, `player_3` |
| `0x0049bd78` | `0x0049df60` | missions {u8 unlocked, name[32]} | 24 records `mission1`..`mission24`, 1 and 2 unlocked |
| `0x0049b378` | `0x0049d530` | portrait dialogues, mission × 2 + end | 48 pointers; only operation 10 (start, end) and operation 24 (start) have one |
| `0x0049c0c0` | `0x0049e1e8` | HUD weapon icon UVs | other cells (the weapon order changed) |
| `0x0049c09c` | `0x0049e1c4` | HUD weapon level caps | slot 3: 6 (8) |
| `0x0049c4d8` | – | title-bar tint, 4 floats, initially 1 | new |
| `0x00489220`.. | `0x0048a240`.. | loading-comic tile names | one set `loading1_0_0`..`loading1_1_3` |
| `0x0048b0e8`..`0x0048b47c` | `0x0048c7f4`..`0x0048d460` | dialogue pages | three dialogues |
| `0x0048b70c`.. | `0x0048d794`.. | button captions | other padding (" Back " → "   Back   ") |
| `0x0048cbc4`.. | `0x0048ece8`.. | main and in-game menu captions | no padding ("Start Game") |

Unchanged tables (read and compared): difficulty (gulf@0x0049bd00), camera modes
(gulf@0x0049b9a0), rank thresholds (gulf@0x0049c488) and names (gulf@0x0049aba4), default
high scores (gulf@0x0049b9f0, "Divo Master" first), attract level names (gulf@0x0049bd50,
`intro1`..`intro4`, the first two used), font advance table.

## Changelog

- 1.0 (F1): first version.
