# AirStrike 2 v2.51 (`as2`): symbol map against v1.70

Package B1. This document explains how the AS2 executable (`AirStrike3D II.exe`) was mapped
onto the v1.70 executable, gives the statistics, and is the first overview of what changed in
the engine between the two games. It holds no decompiled code and no disassembly: functions are
described in words, by address, name, strings, constants and call relations.

Files:

| File | Content |
|---|---|
| `re/symbols_as2.csv` | one row per AS2 function (1919): `address,name,subsystem,description,confidence,evidence,v170_address` |
| `re/symbols_as2_data.csv` | 96 globals and tables: `address,name,kind,description,confidence,evidence,v170_address` |
| `re/tools/match_symbols.py` | the matcher, the CSV writer, the report tables of this document and `--check` |
| `re/tools/match_data.py` | the data-address mapping |
| `re/tools/re_export.py` | loader of the Ghidra exports (functions, imports, disassembly features) and a small PE reader |
| `re/tools/d3d8_vtable.py` | method order of the Direct3D 8 interfaces, to label indirect calls |
| `re/tools/overrides_v170_as2.csv` | the manual decisions (pairs, removals, code addresses), each with its evidence |
| `re/tools/names_as2.csv` | names of AS2 functions without a v1.70 counterpart |
| `re/tools/globals_v170.csv` | the v1.70 globals to map, named after the v1.70 specs, plus AS2-only globals |

Names in the `name` column are the v1.70 names wherever the function is the counterpart of a
named v1.70 function (`re/symbols_v170_render.csv` wins over `re/symbols_v170.csv`, except
where the render file marks its name GUESS; the other name is kept in the description as
"v1.70 alias"). Functions without a counterpart carry the name chosen in
`re/tools/names_as2.csv`, a builtin name `PF_<builtin>` from the builtin table, a Ghidra
function-ID name for runtime code, or `FUN_<address>` when nothing better is known.

Confidence: `high` = unique shared string, table entry, identical instruction shape unique in
both executables, or Ghidra function-ID name; `medium` = call-graph propagation with agreeing
features, or a manual decision backed by strings, call relations and constants; `low` = shape,
position or address layout only.

## Reproduce

```
export AS3D_DATA_ROOT=/path/to/airstrike3d      # holds re/out/ and third_party_local/
python3 re/tools/match_symbols.py --from v170 --to as2 --out re/symbols_as2.csv \
    --pairs /tmp/pairs.json --report /tmp/report.md
python3 re/tools/match_data.py --from v170 --to as2 --pairs /tmp/pairs.json \
    --out re/symbols_as2_data.csv
python3 re/tools/match_symbols.py --check re/symbols_as2.csv
```

The run takes about a minute. `--check` verifies that every address exists in
`re/out/as2/functions.json`, that no address and no name is used twice, that every function of
the AS2 builtin table has a row, and prints the statistics below. For Gulf Thunder the same
tools run with `--to gulf` once its tables and address layout are added to `re_export.TABLES`
and `match_symbols.LAYOUT` and its override and names files exist.

## Method

Both executables were built by the same compiler (Visual C++ 2005, linker 8.0, same C runtime
with `EncodePointer`/`FlsAlloc`), so unchanged functions compile to the same instruction
sequence; only absolute addresses differ. Every function of both exports is reduced to features:
an address-free normalised instruction sequence (addresses of code and data replaced by
placeholders, small constants and structure offsets kept), a mnemonic histogram, the ordered
list of internal calls, the imports it calls, its strings, the floating-point constants it loads
(read from the executable), the structure offsets it uses, the code addresses it takes as values
(callbacks) and its indirect calls through `[register + offset]` (COM method calls).

Matching runs in this order; a pair keeps the evidence of the step that produced it, and every
function takes part in one pair at most:

1. Manual decisions (`overrides_v170_as2.csv`), applied first. A v1.70 function marked removed
   and an AS2 function listed in `names_as2.csv` are never paired automatically afterwards.
2. Anchors (high): the builtin tables of both executables read by name (v1.70 0x00456f70, 85
   records; AS2 0x0049d5d0, 101 records); strings referenced by exactly one function in each
   executable; imports with a single caller; thunk and function-ID names present once in each;
   an identical normalised instruction sequence of at least 10 instructions that is unique in
   both executables.
3. Call-graph propagation (medium): for every matched pair, its unmatched callees, callers and
   callbacks are scored against each other (instruction histogram and length, shared strings,
   imports, constants, floating-point constants, structure offsets, agreement of already-matched
   callers and callees, a bonus for the same position in the call order); a pair is accepted
   when it is the best candidate of both sides with a margin, above 0.62. Repeated until nothing
   changes.
4. Global shape search (low): unmatched functions of at least 8 instructions against all
   unmatched functions of similar size, mutual best above 0.8.
5. Graph-led and neighbour passes (low): pairs whose already-matched callers and callees agree
   (at least 60 %) above a score of 0.5; the only unmatched function with exactly the same set of
   matched callers (or callees) on both sides.
6. Manual review of every named v1.70 function the automatic steps left open, and of every
   automatic pair of low score among the named ones, using strings, constants, call relations,
   callback positions of the menu builders and the Direct3D method slots called. The decisions
   and their evidence are the rows of `overrides_v170_as2.csv`; the manual review also found and
   corrected four wrong automatic pairs (see "Matches that were corrected").
7. Runtime and libraries: functions that Ghidra's function ID named (`_malloc`, `__output_l`,
   ...) keep that name with subsystem `crt`; the unnamed functions between 0x0044ef00 and
   0x00478d40 are the statically linked Direct3DX 8 library (its strings: `d3d8d.dll`,
   `DisableMMX`, libpng 1.0.5 chunk errors, libjpeg messages, zlib inflate errors), subsystem
   `lib`; their internals are not named.
8. New code: AS2 functions without a counterpart are named from their strings, callers, callees,
   constants and Direct3D calls (`names_as2.csv`); the rest get a subsystem from the named
   functions around them (the linker keeps each source file's functions together).

Data (`match_data.py`): for every matched function pair the two instruction sequences are
aligned (longest matching blocks of normalised instructions); aligned instructions with the same
opcode vote for "v1.70 data address to AS2 data address" for each absolute data address they
use. 2167 of the 2305 v1.70 data addresses seen are mapped. The script-global table of AS2
gives the script-visible globals directly. The rows of `symbols_as2_data.csv` are the globals
named in the v1.70 specs, the two tables, the level and object definition tables, and four
AS2-only Direct3D globals.

Cross-check with Gulf Thunder: 132 AS2 functions have an identical, unique instruction shape in
the Gulf Thunder export and a name in the third-party toolkit's Gulf decompilation (placed by
the order of its function definitions). The toolkit author's names agree with the map in most
cases (for example the frame begin/present pair, the 3D scene render, the camera update, the
player input frame, the Direct3D resource, reset and state functions, the texture loader). One
disagreement was a real error of the map (0x0042f3d0, see below); the other differences are
positional offsets of the order-based placement or a different wording. Toolkit names were used
as a naming aid only and are marked as such where quoted.

## Statistics

Named v1.70 functions (unique addresses of `re/symbols_v170.csv` and
`re/symbols_v170_render.csv`): 529.

| Outcome | Count |
|---|---|
| mapped to an AS2 function, high | 265 |
| mapped to an AS2 function, medium | 181 |
| mapped to an AS2 function, low | 34 |
| mapped to an AS2 code address that the AS2 export does not define as a function (menu callbacks), medium / low | 9 / 8 |
| removed (no counterpart; dropped or inlined, with evidence) | 25 |
| undecided | 7 |
| **mapped or removed** | **522 of 529 (98.7 %)** |

Of all 1061 v1.70 functions, 999 have an AS2 counterpart function; 10 more named v1.70
addresses that are not function entries in the v1.70 export map to AS2 functions.

AS2 functions (1919):

| Kind | Count | Bytes |
|---|---|---|
| counterpart of a v1.70 function (`v170_address` set) | 1009 | 250,594 |
| no counterpart, named: game code | 99 | 36,102 |
| no counterpart, named: runtime and library (function-ID names, exception funclets) | 154 | 5,725 |
| unnamed (`FUN_`): game code | 11 | 1,627 |
| unnamed (`FUN_`): runtime and Direct3DX library | 646 | 160,570 |

Every function of the AS2 builtin table has a row (101 records, 97 distinct functions: four
pairs of names share a function as in v1.70, `RotateTo`/`lRotateTo` and so on). 15 builtins are
new functions; the 16th new builtin, `GameOver`, points to the existing `G_GameOver`.

Confidence of the 1919 rows: high 879, medium 295, low 745 (most low rows are unnamed runtime or
library functions labelled by their address range).

## Subsystems

Counts per subsystem of the v1.70 names. "Same instruction shape" means the normalised
instruction sequence is identical (the function is unchanged apart from addresses); "same
opcodes, other operands" means the same instructions with different registers, offsets or
constants. "AS2 functions" counts the rows of `re/symbols_as2.csv` with that subsystem.

| subsystem | v1.70 named | matched | same instruction shape | same opcodes, other operands | removed | undecided | AS2 functions | AS2 without counterpart |
|---|---|---|---|---|---|---|---|---|
| script-builtin | 81 | 81 | 46 | 19 | 0 | 0 | 96 | 15 |
| menu | 67 | 60 | 2 | 1 | 3 | 4 | 62 | 19 |
| game | 46 | 42 | 6 | 18 | 3 | 1 | 46 | 4 |
| ui | 37 | 35 | 13 | 4 | 0 | 2 | 41 | 6 |
| settings | 31 | 26 | 13 | 1 | 5 | 0 | 27 | 1 |
| crt | 30 | 30 | 30 | 0 | 0 | 0 | 821 | 290 |
| intro | 17 | 17 | 9 | 0 | 0 | 0 | 27 | 10 |
| 2d | 14 | 14 | 3 | 0 | 0 | 0 | 18 | 4 |
| sound | 13 | 13 | 10 | 0 | 0 | 0 | 41 | 0 |
| fs | 11 | 11 | 10 | 0 | 0 | 0 | 11 | 0 |
| math | 11 | 11 | 10 | 1 | 0 | 0 | 11 | 0 |
| particle | 11 | 11 | 5 | 0 | 0 | 0 | 11 | 0 |
| terrain | 11 | 10 | 2 | 0 | 1 | 0 | 26 | 16 |
| model | 10 | 10 | 8 | 0 | 0 | 0 | 11 | 1 |
| script | 10 | 10 | 5 | 2 | 0 | 0 | 10 | 0 |
| window | 10 | 8 | 3 | 0 | 2 | 0 | 9 | 1 |
| input | 9 | 8 | 4 | 0 | 1 | 0 | 8 | 0 |
| shadow | 9 | 9 | 2 | 0 | 0 | 0 | 10 | 1 |
| sys | 9 | 8 | 3 | 0 | 1 | 0 | 10 | 2 |
| texture | 9 | 7 | 1 | 1 | 2 | 0 | 8 | 1 |
| level | 8 | 8 | 1 | 2 | 0 | 0 | 9 | 1 |
| cull | 7 | 7 | 4 | 1 | 0 | 0 | 8 | 1 |
| gl | 7 | 3 | 0 | 0 | 4 | 0 | 3 | 0 |
| hud | 7 | 7 | 0 | 0 | 0 | 0 | 10 | 3 |
| player | 7 | 7 | 0 | 0 | 0 | 0 | 9 | 2 |
| config | 6 | 5 | 0 | 0 | 1 | 0 | 5 | 0 |
| frame | 6 | 6 | 1 | 0 | 0 | 0 | 7 | 1 |
| camera | 4 | 4 | 2 | 1 | 0 | 0 | 4 | 0 |
| save | 4 | 3 | 1 | 0 | 1 | 0 | 3 | 0 |
| weapons | 4 | 4 | 3 | 1 | 0 | 0 | 4 | 0 |
| decal | 3 | 3 | 0 | 0 | 0 | 0 | 5 | 2 |
| light | 3 | 3 | 2 | 0 | 0 | 0 | 3 | 0 |
| parse | 3 | 3 | 3 | 0 | 0 | 0 | 3 | 0 |
| effect | 2 | 2 | 0 | 0 | 0 | 0 | 2 | 0 |
| material | 2 | 2 | 0 | 0 | 0 | 0 | 2 | 0 |
| net | 2 | 2 | 2 | 0 | 0 | 0 | 2 | 0 |
| render | 2 | 1 | 1 | 0 | 1 | 0 | 20 | 19 |
| sprite | 2 | 2 | 0 | 0 | 0 | 0 | 2 | 0 |
| cheat | 1 | 1 | 0 | 0 | 0 | 0 | 1 | 0 |
| entity | 1 | 1 | 0 | 0 | 0 | 0 | 1 | 0 |
| particles | 1 | 1 | 0 | 0 | 0 | 0 | 1 | 0 |
| util | 1 | 1 | 1 | 0 | 0 | 0 | 1 | 0 |
| lib | 0 | 0 | 0 | 0 | 0 | 0 | 510 | 510 |

Subsystems without a v1.70 name that appear only in AS2: `lib` (Direct3DX 8, 510 functions)
and most of `render` (the Direct3D 8 back end, 19 new functions).

## What changed between the two engines

The verdicts below come from the table above: the share of matched functions with an identical
instruction shape, the size changes of the key functions, the strings present or absent, and the
new functions around them.

### Unchanged (identical code apart from addresses)

- **C runtime**: same compiler and runtime; all 30 named runtime helpers identical.
- **File system** (`fs`, pak reading and decryption): 10 of 11 identical.
- **Math** (vectors, axes, sine table): 10 of 11 identical.
- **Sound** (BASS wrapper): 10 of 13 identical, same 28 BASS imports.
- **Text parsing** (`parse`), **score posting** (`net`), **weapon files** (`weapons`, 3 of 4):
  identical.
- **Model loader** (`model`): 8 of 10 identical; the model file format is read by the same code.
- **Script VM**: `SL_Execute` 1434 against 1437 bytes (near-identical), `SL_CreateThread`
  identical, `SL_LoadScript` near-identical; the script container and opcodes can be assumed
  unchanged until the VM package says otherwise.
- **Script builtins**: 46 of the 81 shared builtin functions identical and 19 more with the same
  instructions and other operands.
- **Camera**: `CL_CameraQuake` identical; `V_UpdateCamera` has the same size (1015 bytes) but not
  the same instructions. `V_ResetCamera` changed (new `-campos` command-line option).

### Changed (same function, different code)

- **Entity frame loop** (`game`): only 6 of 42 matched functions are identical. `G_RunEntities`
  (0x0040cdc0) absorbed the v1.70 collision pass `G_RunCollisions` and is also called from level
  start; `G_FreeEntity` absorbed the tree walk; `G_ThinkEntity` grew from 522 to 562 bytes,
  `G_TouchEntity` from 1011 to 1068, `G_InitObject` from 1240 to 1578, `G_SyncRenderRecord`
  from 363 to 675 (it now also aligns entities on water). The globals around the entity pool moved
  apart: the pool starts 0x1f8 bytes after the live-list head instead of 0x1e8 (the record size
  itself is for the game-rules package to measure).
- **Player** (`player`, 0 of 7 identical): `G_PlayerFrame` 669 against 692 bytes; the player
  record gained a field (script global `p_maxHealth` at +0x90 pushes `p_scores` from +0x90 to
  +0x94); new per-player start/update functions and two-player builtins.
- **HUD** (0 of 7 identical): one- and two-player HUDs redrawn; the statistics overlay moved out
  of `R_RenderView` into its own function.
- **Level start** (`G_StartLevel`, `G_BeginLevel`): shows a comic panel while loading, can show a
  portrait dialogue, runs one entity pass before the first frame; the level list and object
  parsers are nearly unchanged (`level` 8 of 8 matched).
- **Terrain** (`terrain`, 2 of 10 identical, 16 new functions): the loader is split into five
  steps, the renderer draws patches through Direct3D, and there are morph maps (`TerraMorph`
  builtin, `morphmaps` data directory) and a water surface with its own height function
  (`WaterHeight` builtin).
- **Particles** (`particle`): update and spawn logic identical or nearly so (`PS_Update` score
  0.999); only the drawing changed with the renderer.
- **Settings** (`settings`): the XML reader is unchanged, the unused XML writer helpers are gone.
- **Window and video modes** (`window`, `config`): the OpenGL-era mode enumeration and switching
  are replaced by a new display-mode selector (0x004012b0, with C++ containers) and Direct3D
  device modes.
- **Menus** (`menu`, 2 of 60 identical; `ui` 13 of 35): every screen was redrawn with new art;
  menu builders pass their callbacks in a different order (action, draw, key instead of draw,
  key, action); new screens for helicopter and player selection, a second items page, portrait
  dialogues, an options screen with a resolution list; the text story pages are gone.
- **Intro** (`intro`): the logo and image pages are unchanged (9 identical); 10 new functions
  play the intro comic.
- **Save file** (`save`): same `game.bin` handling; the XOR helper is inlined.

### New

- **Direct3D 8 renderer** (`render` and the rewritten `gl`, `frame`, `2d`, `sprite`, `shadow`,
  `decal`, `texture`, `terrain` drawers): device creation and reset, dynamic vertex and index
  buffers, render-state and texture-stage switches, format checks, fade overlay, statistics
  overlay. The v1.70 drawers keep their place in the frame (`R_RenderView` calls the same passes
  in the same order, plus one new pass) but none of their code is shared.
- **Direct3DX 8 static library** (`lib`, 510 functions): texture loading from files and memory
  with libpng, libjpeg and zlib inside.
- **Comic screens** (intro comic, loading comics), **helicopter selection**, **portrait
  dialogues**, **statistics overlay**.
- **Water and terrain morphing** (see Terrain).
- **Two-player extensions**: builtins `IsMultiplayer`, `IsPlayerInGame`, `GetPlayersDistance`,
  `GetPlayerAccel`, `RadialDamagePlayer`; script globals `player1`, `player2`, `p_maxHealth`,
  `cameramode`.
- **Math builtins** `atan2`, `copysign`, `floor`, `floor2`, `fmod`; `DetachEntity`,
  `G_SetPowerUpCount`, `GetMapPosOfs`.
- **Start-up**: a registry write under a CLSID key and a drive-type check with a message box.

### Removed

The OpenGL back end (context, pixel format, extensions, texture upload), the OpenGL-era video
mode switching and restart, the screenshot writer, the text story pages and the congratulations
text, the unused XML writer, and several small helpers that the AS2 build inlines. Details in
the next table.

## Removed v1.70 functions

| v1.70 address | v1.70 name | status | confidence | evidence |
|---|---|---|---|---|
| `0x00401000` | G_XorSavePayload | removed | medium | inlined into G_LoadBin 0x004069e0 and G_SaveBin 0x00406c70 (neither calls a separate XOR routine) |
| `0x004044b0` | G_FreeEntityTree | removed | medium | merged into G_FreeEntity 0x0040b300 (which now frees itself recursively) |
| `0x004055d0` | G_RunCollisions | removed | medium | inlined into G_RunEntities 0x0040cdc0 (its 2000/-2000 constants and G_TouchEntity and VectorNormalize calls appear there) |
| `0x004099a0` | G_AllocEmitterEntity | removed | medium | inlined into G_InitObject 0x00411e90 (which calls _malloc and PS_CreateInstance directly) |
| `0x00411fc0` | GL_IsHardwareRenderer | removed | high | OpenGL renderer-name check: its 'gdi generic' string is absent in AS2 |
| `0x00412050` | GL_InitExtensions | removed | high | OpenGL extension probing: none of its extension-name strings exist in AS2 |
| `0x00412530` | R_ScreenShot | removed | high | screenshot writer: its 'screenshots' strings are absent in AS2 |
| `0x00412720` | GL_EnableDetailTexture | removed | low | detail-texture stage set-up inlined into R_DrawTerrainPass 0x00437f80 (no separate function among its callees) |
| `0x00412cc0` | GL_DisableBlend | removed | low | no counterpart found: blend and fog disabling is done by the Direct3D render-state switch 0x0042fb50 |
| `0x004184e0` | R_Downsample2x | removed | low | no function with its shape or constants in AS2; the one the automatic propagation proposed (0x0042f3d0) is a Direct3D format check (IDirect3D8 CheckDeviceFormat); mip levels are made by D3DX |
| `0x00418a20` | GL_UploadTexture | removed | medium | OpenGL texture upload (glTexImage2D and gluBuild2DMipmaps) replaced by D3DX texture creation in 0x004380e0 and 0x00438310; its 'Too many registered textures' message is absent |
| `0x0041cdc0` | CFG_SetResolution | removed | medium | its 'Choosen resolution mode' message is absent; video modes are handled by the Direct3D mode code (0x004012b0) |
| `0x004206c0` | Sys_RestartVideo | removed | medium | video restart (called when the OpenGL mode changed): in AS2 only Sys_Init 0x00405a80 calls MW_Init and S_Init |
| `0x00421500` | MW_EnumDisplayModes | removed | medium | its 'detected display refresh rates' messages are absent; replaced by the Direct3D mode enumeration (0x004012b0) |
| `0x004216c0` | MW_SetDisplayMode | removed | medium | its 'Choosen refresh rate' messages and GetDeviceCaps calls are absent; replaced by the Direct3D mode code |
| `0x004218b0` | GL_ChoosePixelFormat | removed | high | OpenGL pixel-format selection: its ChoosePixelFormat/SetPixelFormat strings are absent and the only GDI32 import of AS2 is PatBlt |
| `0x004229c0` | Joy_Axes | removed | low | no separate function; inlined into Joy_Frame 0x00416a80 (457 + 526 bytes in v1.70; 964 bytes in AS2); the screenshot call is gone |
| `0x00426e70` | M_DrawCongrats | removed | medium | the v1.70 congratulations text is absent in AS2 ('Congratulations!' alone remains as unreferenced data) |
| `0x00427aa0` | M_PageStory1 | removed | high | story page: its text strings are absent in AS2 (AS2 tells its story with comic screens) |
| `0x00427b70` | M_PageStory2 | removed | high | story page: its text strings are absent in AS2 |
| `0x0042ce30` | XML_AddAttribute | removed | medium | XML writer helper with no callers in v1.70 (dead code); no counterpart in AS2 |
| `0x0042cf80` | XML_SetAttribute | removed | medium | XML writer helper with no callers in v1.70 (dead code); no counterpart in AS2 |
| `0x0042d030` | XML_SetAttributeInt | removed | medium | XML writer helper with no callers in v1.70 (dead code); no counterpart in AS2 |
| `0x0042d080` | XML_SetAttributeFloat | removed | medium | XML writer helper with no callers in v1.70 (dead code); no counterpart in AS2 |
| `0x0042d130` | XML_IsNameChar | removed | medium | only used by the two dead XML writer helpers; no counterpart in AS2 |

## Counterparts that are not function entries in the AS2 export

Named v1.70 callbacks whose AS2 counterpart is code that the AS2 export does not define as a
function either (it lies inside the preceding function). Status `code`.

| v1.70 address | v1.70 name | AS2 code address | status | confidence | evidence |
|---|---|---|---|---|---|
| `0x00422ef0` | M_ControlsRowAction | `0x004236b0` | code | medium | fourth callback of M_ControlsMenu 0x00423ac0; the callback order is unchanged |
| `0x00423160` | M_ControlsKey | `0x00423930` | code | medium | first callback of M_ControlsMenu 0x00423ac0; the callback order is unchanged (draw and key-binding drawer at the same positions) |
| `0x00426330` | M_MissionCompleteKey | `0x00427b30` | code | medium | key callback of M_MissionCompleteMenu 0x00427db0 (its action and draw callbacks are matched) |
| `0x00426a20` | M_ExitConfirmAction | `0x00427fb0` | code | medium | action callback of the quit confirmation 0x00428090 (its draw callback 0x00428000 is matched) |
| `0x004270a0` | M_DrawGameComplete | `0x00428430` | code | medium | draw callback of M_GameCompleteMenu 0x004289c0 (its key and action callbacks M_GameCompleteKey and M_GameCompleteAction are matched) |
| `0x00427670` | M_DrawGameOver | `0x00428b90` | code | medium | draw callback of M_GameOverMenu 0x00428db0 (its action and key callbacks are matched) |
| `0x00428dd0` | M_InGameMenuKey | `0x0042a9a0` | code | medium | key callback of M_InGameMenu 0x0042aa20 (its action and draw callbacks are matched) |
| `0x0042a300` | M_NameEntryAction | `0x0042bc10` | code | low | M_NameEntryMenu 0x0042bcd0 callbacks: first position (action in the AS2 order) and lowest address as in v1.70 |
| `0x0042a330` | M_NameEntryKey | `0x0042bc40` | code | low | M_NameEntryMenu 0x0042bcd0 callbacks: third position (key in the AS2 order) and middle address as in v1.70 |
| `0x0042a360` | M_DrawNameEntry | `0x0042bc70` | code | low | M_NameEntryMenu 0x0042bcd0 callbacks: second position (draw in the AS2 order) and highest address as in v1.70 |
| `0x0042a840` | M_OptionsApplyAction | `0x0042c4a0` | code | low | M_OptionsMenu 0x0042c8d0 callbacks other than the draw callback: the lower address as in v1.70 |
| `0x0042aa60` | M_OptionsAction | `0x0042c700` | code | low | M_OptionsMenu 0x0042c8d0 callbacks other than the draw callback: the higher address as in v1.70 (M_OptionsApplyAction below M_OptionsAction) |
| `0x0042b3b0` | M_StartGameCallback | `0x0042d7b0` | code | medium | the other callback of M_StartGameMenu 0x0042d8b0 (its draw callback 0x0042d840 is matched) |
| `0x0042bcc0` | M_MessageBoxAction | `0x0042daf0` | code | low | message-box builder 0x0042dc10 callbacks keep the v1.70 order (draw; key; action) and the v1.70 address order |
| `0x0042bd00` | M_MessageBoxKey | `0x0042db30` | code | low | message-box builder 0x0042dc10 callbacks keep the v1.70 order (draw; key; action) and the v1.70 address order |
| `0x0042bd40` | M_DrawMessageBox | `0x0042db70` | code | low | message-box builder 0x0042dc10 callbacks keep the v1.70 order (draw; key; action) and the v1.70 address order |
| `0x0042c180` | M_TopScoresAction | `0x0042de60` | code | medium | action callback of M_TopScoresMenu 0x0042e0d0 (its draw callback M_DrawTopScores is matched) |

## Named v1.70 functions without a decision

Menu and widget callbacks whose builders pass no callback list that could be aligned, and
`G_NewCampaign`, a code address inside `G_BeginLevel` in v1.70.

| v1.70 address | v1.70 name | subsystem |
|---|---|---|
| `0x00408c40` | G_NewCampaign | game |
| `0x00424590` | UI_SpinnerKey | ui |
| `0x00425e80` | UI_EditKey | ui |
| `0x00427a10` | M_InfoMenuAction | menu |
| `0x00427a30` | M_InfoMenuKey | menu |
| `0x004289b0` | M_DrawInfoMenu | menu |
| `0x0042a9f0` | M_SetBinding | menu |

## New functions (no v1.70 counterpart), by subsystem

Game code only. Runtime and library functions without a counterpart (154 named by Ghidra
function ID, 646 unnamed, mostly Direct3DX) are not listed. Names are this map's own unless
the evidence column of `re/symbols_as2.csv` says otherwise. `FUN_` rows have no name yet; their
subsystem is guessed from the neighbouring functions.

### 2d (4)

| AS2 address | name | description | confidence |
|---|---|---|---|
| `0x0040b260` | FUN_0040b260 | subsystem guessed from the neighbouring functions (2d / game) | low |
| `0x00417a90` | R_Add2DGradient | 2D helper of the menu header drawer | low |
| `0x00417b60` | R_Add2DFrame | 2D helper of list and button drawing | low |
| `0x00435bf0` | R_Setup2DStages | texture-stage set-up for 2D drawing | low |

### cull (1)

| AS2 address | name | description | confidence |
|---|---|---|---|
| `0x00431170` | R_GetViewMatrices | reads back the view/projection transforms and the viewport | medium |

### decal (2)

| AS2 address | name | description | confidence |
|---|---|---|---|
| `0x004300e0` | R_DrawMark | draws one mark (decal) | low |
| `0x004311e0` | R_AllocDecal | allocates a decal record | low |

### frame (1)

| AS2 address | name | description | confidence |
|---|---|---|---|
| `0x00407850` | R_AddMenuModelTree | adds a menu preview model and its attached children to the scene (recursive) | low |

### game (4)

| AS2 address | name | description | confidence |
|---|---|---|---|
| `0x0040b510` | G_FreeAllEntities | frees every entity at level end | low |
| `0x0040cef0` | G_FloorHelper | small rounding helper of the water-height lookup | low |
| `0x00410dc0` | G_NewGame | starts a new game from the player-selection menu | low |
| `0x00414800` | G_LevelEndHelper | level-end helper | low |

### hud (3)

| AS2 address | name | description | confidence |
|---|---|---|---|
| `0x0040a9e0` | R_UpdateFrameRate | frame-rate counter for the statistics overlay | low |
| `0x0040aa40` | R_DrawStats | statistics overlay (FPS; triangles; texture binds; models; sprites; marks; entities) split out of R_RenderView | high |
| `0x0040acc0` | SCR_SelectLoadingComic | picks the comic panel shown on the loading screen | medium |

### intro (10)

| AS2 address | name | description | confidence |
|---|---|---|---|
| `0x0040f000` | IntroComic_DoneA | duration callback of an intro comic page (7 s) | low |
| `0x0040f020` | IntroComic_DrawA | draw callback of an intro comic page | low |
| `0x0040f1e0` | IntroComic_Begin1 | starts the intro comic: switches the music and registers the frames of page 1 | medium |
| `0x0040f280` | IntroComic_Begin2 | registers the frames of intro comic page 2 | medium |
| `0x0040f310` | IntroComic_Begin3 | registers the frames of intro comic page 3 | medium |
| `0x0040f3f0` | IntroComic_DoneB | duration callback of an intro comic page (20 s) | low |
| `0x0040f410` | IntroComic_DrawB | draw callback of the multi-panel intro comic pages | low |
| `0x004100b0` | IntroComic_Begin4 | registers the frames of intro comic page 4 | medium |
| `0x00410140` | IntroComic_DoneC | duration callback of an intro comic page (11 s) | low |
| `0x00410160` | IntroComic_DrawC | draw callback of an intro comic page | low |

### level (1)

| AS2 address | name | description | confidence |
|---|---|---|---|
| `0x004119f0` | G_WriteObjectsTxt | writes an objects.txt file with a generated header | low |

### menu (19)

| AS2 address | name | description | confidence |
|---|---|---|---|
| `0x004234b0` | M_ShowPortraitDialog | shows a dialogue with a portrait at level start or end | low |
| `0x00423d90` | M_CreditsMenu | menu with a Back button opened from the main menu | low |
| `0x00428f10` | M_SpawnPreviewHeli | spawns the 'player_1' object for the helicopter preview | medium |
| `0x00428fb0` | M_HeliSelectAction | action callback of the helicopter-selection menu (starts 'mission1') | medium |
| `0x004290c0` | M_DrawHeliStats | draws the speed/armour bars of the selected helicopter | low |
| `0x004291f0` | M_DrawHeliSelect | draw callback of the helicopter-selection menu | high |
| `0x00429530` | M_HeliSelectMenu | builds the helicopter/player selection menu | medium |
| `0x004297d0` | M_ShowHeliSelect | opens the helicopter-selection menu | medium |
| `0x0042a550` | M_PageItems2 | second page of the items help (Satellite Strike; Air Support) | high |
| `0x0042bda0` | M_BuildResolutionList | fills the resolution list of the options menu | low |
| `0x0042c270` | FUN_0042c270 | subsystem guessed from the neighbouring functions (menu / menu) | low |
| `0x0042c350` | FUN_0042c350 | subsystem guessed from the neighbouring functions (menu / menu) | low |
| `0x0042c690` | FUN_0042c690 | subsystem guessed from the neighbouring functions (menu / menu) | low |
| `0x0042cd30` | M_ShowOptions | opens the options menu | medium |
| `0x0042cd60` | FUN_0042cd60 | subsystem guessed from the neighbouring functions (menu / crt) | low |
| `0x0042ce90` | FUN_0042ce90 | subsystem guessed from the neighbouring functions (menu / crt) | low |
| `0x0042ceb0` | FUN_0042ceb0 | subsystem guessed from the neighbouring functions (menu / crt) | low |
| `0x0042cf80` | FUN_0042cf80 | subsystem guessed from the neighbouring functions (menu / crt) | low |
| `0x0042cfe0` | FUN_0042cfe0 | subsystem guessed from the neighbouring functions (menu / crt) | low |

### model (1)

| AS2 address | name | description | confidence |
|---|---|---|---|
| `0x00433e60` | R_SetupModelLight | model-pass helper | low |

### player (2)

| AS2 address | name | description | confidence |
|---|---|---|---|
| `0x004138f0` | G_ResetPlayers | resets the per-player state when a game or level starts | low |
| `0x00414850` | G_UpdatePlayers | per-frame update that uses terrain heights and distances | low |

### render (19)

| AS2 address | name | description | confidence |
|---|---|---|---|
| `0x00417e30` | R_Shutdown | frees every renderer resource and the Direct3D buffers | medium |
| `0x00417e90` | R_FreeLevelResources | frees the per-level renderer resources | medium |
| `0x0042eed0` | D3D_CreateBuffers | creates the vertex and index buffers and the shadow render target | high |
| `0x0042efe0` | D3D_ReleaseBuffers | releases the Direct3D buffers | medium |
| `0x0042f070` | R_LockVertexBuffer | reserves room in the dynamic vertex buffer (32-byte vertices; discard when full) | medium |
| `0x0042f0f0` | R_FlushVertices | draws the vertices written since the last lock | medium |
| `0x0042f200` | R_LockParticleVertices | vertex reservation used by the particle drawer | low |
| `0x0042f290` | R_DrawIndexed | draws indexed triangles from the dynamic buffers | medium |
| `0x0042f3d0` | D3D_FindTextureFormat | checks which texture format the device supports | medium |
| `0x0042f4b0` | D3D_FindAdapterFormat | checks which back-buffer format the adapter supports for the chosen mode | medium |
| `0x0042f570` | D3D_FindDepthFormat | picks a depth-buffer format compatible with the display format | medium |
| `0x0042f620` | D3D_SetDefaultStates | sets the default render and texture-stage states after device creation or reset | medium |
| `0x0042f750` | D3D_ResetDevice | resets the Direct3D device (lost device) and recreates the buffers | high |
| `0x0042f7c0` | D3D_ApplyMode | applies a window/fullscreen mode change | low |
| `0x0042ff70` | R_DrawGroundPass | a draw pass of R_RenderView between the frustum set-up and the marks (new in AS2) | low |
| `0x00430e00` | R_ClearScreen | clears the back buffer | medium |
| `0x00430eb0` | R_DrawFadeOverlay | draws the full-screen fade at the end of a frame | medium |
| `0x004331f0` | R_DrawTriangleList | copies a prepared triangle list into the vertex buffer and draws it | medium |
| `0x00435ad0` | R_SetTextureStage | binds a texture and sets the colour/alpha operations of a texture stage | medium |

### script-builtin (15)

| AS2 address | name | description | confidence |
|---|---|---|---|
| `0x0041f370` | PF_atan2 | builtin 'atan2' (new in as2) | high |
| `0x0041f480` | PF_copysign | builtin 'copysign' (new in as2) | high |
| `0x0041f4b0` | PF_floor | builtin 'floor' (new in as2) | high |
| `0x0041f4e0` | PF_floor2 | builtin 'floor2' (new in as2) | high |
| `0x0041f510` | PF_fmod | builtin 'fmod' (new in as2) | high |
| `0x0041fae0` | PF_DetachEntity | builtin 'DetachEntity' (new in as2) | high |
| `0x00420840` | PF_RadialDamagePlayer | builtin 'RadialDamagePlayer' (new in as2) | high |
| `0x00420d80` | PF_WaterHeight | builtin 'WaterHeight' (new in as2) | high |
| `0x00421650` | PF_G_SetPowerUpCount | builtin 'G_SetPowerUpCount' (new in as2) | high |
| `0x00421a90` | PF_TerraMorph | builtin 'TerraMorph' (new in as2) | high |
| `0x00421ab0` | PF_IsMultiplayer | builtin 'IsMultiplayer' (new in as2) | high |
| `0x00421ae0` | PF_IsPlayerInGame | builtin 'IsPlayerInGame' (new in as2) | high |
| `0x00421b20` | PF_GetMapPosOfs | builtin 'GetMapPosOfs' (new in as2) | high |
| `0x00421b40` | PF_GetPlayerAccel | builtin 'GetPlayerAccel' (new in as2) | high |
| `0x00421ba0` | PF_GetPlayersDistance | builtin 'GetPlayersDistance' (new in as2) | high |

### settings (1)

| AS2 address | name | description | confidence |
|---|---|---|---|
| `0x00438e40` | FUN_00438e40 | subsystem guessed from the neighbouring functions (settings / settings) | low |

### shadow (1)

| AS2 address | name | description | confidence |
|---|---|---|---|
| `0x00438260` | R_ShadowMapHelper | shadow-map helper | low |

### sys (2)

| AS2 address | name | description | confidence |
|---|---|---|---|
| `0x00405880` | Sys_WriteRegistryKey | writes a value under a CLSID key of the registry at start-up | medium |
| `0x00405930` | Sys_CheckDrive | checks the type of the drive the game runs from and shows a message box before quitting | low |

### terrain (16)

| AS2 address | name | description | confidence |
|---|---|---|---|
| `0x0041a560` | R_LoadMorphMap | loads a terrain morph image (TGA) | medium |
| `0x0041a670` | R_TerrainMorph | applies a morph map to the terrain height field | medium |
| `0x0041a7d0` | R_TerrainVertexLight | computes terrain vertex lighting/colour for the drawer | low |
| `0x0041b2e0` | R_LoadTerrain_Heights | terrain loading step (height field and normals) | low |
| `0x0041b580` | R_LoadTerrain_Colors | terrain loading step | low |
| `0x0041b780` | R_LoadTerrain_Tiles | terrain loading step that loads the tiles | low |
| `0x0041ba70` | R_LoadTerrain_Textures | terrain loading step that registers textures | low |
| `0x0041bb60` | R_LoadTerrain_Copy | terrain loading step | low |
| `0x0041c1c0` | R_FreeTerrainNode | frees a terrain quadtree node recursively | medium |
| `0x0041d530` | G_WaterHeight | height of the water surface at a point | medium |
| `0x0041d6d0` | G_AlignToWater | orients an entity on the water surface (bobbing) | low |
| `0x0041d8d0` | R_WaterWaveVertices | animates water vertices | low |
| `0x004362a0` | R_SetupTerrainStages | texture-stage set-up for the terrain (base and detail textures) | low |
| `0x00436440` | R_SetupWaterStages | texture-stage and texture-transform set-up for the water (wave animation) | low |
| `0x004377d0` | R_DrawTerrainPatch | draws one terrain patch | low |
| `0x00438650` | R_DrawWaterSurface | draws the water geometry | low |

### texture (1)

| AS2 address | name | description | confidence |
|---|---|---|---|
| `0x00438310` | R_CreateTexture | creates a Direct3D texture from a generated image with mip levels | medium |

### ui (6)

| AS2 address | name | description | confidence |
|---|---|---|---|
| `0x00422f70` | FUN_00422f70 | subsystem guessed from the neighbouring functions (ui / ui) | low |
| `0x00423e40` | UI_LayoutItem | sizes a new menu item | low |
| `0x00423f00` | UI_DrawButton | draws a text button of a menu | low |
| `0x00426510` | UI_DrawHeliGridCell | draws one cell of the helicopter grid | low |
| `0x00426ae0` | UI_DrawPanel | draws the decorated background panel of a menu screen | low |
| `0x0042b610` | UI_DrawMenuHeader | draws the animated title bar of a menu screen | low |

### window (1)

| AS2 address | name | description | confidence |
|---|---|---|---|
| `0x004012b0` | VID_SelectDisplayMode | enumerates the display modes and picks the one to use; logs the list and falls back or quits when none fits | medium |

## Data map highlights

From `re/symbols_as2_data.csv` (96 rows: high 60, medium 25, low 10, none 1):

| Global | v1.70 | AS2 | Confidence |
|---|---|---|---|
| builtin table / script-global table | 0x00456f70 / 0x00457220 | 0x0049d5d0 / 0x0049d908 | high |
| entity pool / live list / free list | 0x00458eb0 / 0x00458cc8 / 0x00458cc4 | 0x004c2278 / 0x004c2080 / 0x004c2078 | medium / high / medium |
| player records | 0x01ebe308 | 0x020c5ad0 | high |
| camera record | 0x01ebe5f4 | 0x020df0a8 | medium |
| script `self`, argument pointer, return register, latent-done flag | 0x01fa7df0, 0x01fa7df4, 0x01fa7dfc, 0x01fa7e00 | 0x02106320, 0x02106324, 0x0210632c, 0x02106330 | high |
| frame time / game time | 0x01fb4bcc / 0x01fb4bd0 | 0x0049f920 / 0x0049f924 | high |
| difficulty, number of players, damage factor | 0x004577bc, 0x004577b8, 0x004577c0 | 0x0049ded4, 0x0049ded0, 0x0049ded8 | medium, high, high |
| level records / count | 0x004d1ab8 / 0x004d52bc | 0x0053f278 / 0x00543280 | high / medium |
| object definition names / count | 0x005545e1 / 0x00551304 | 0x005c68d4 / 0x005c32cc | low / high |
| Direct3D device / vertex buffer (AS2 only) | none | 0x0221918c / 0x022191a8 | high / medium |

The alignment cannot map data used only by rewritten functions: the v1.70 blend-mode cache was
first paired with the Direct3D device pointer and is set by hand (low), and the missile icon
table has no mapping.

## Matches to verify first

The pairs that later packages will lean on most, with how they were found. Authors of the game
rules, renderer and builtin packages should confirm these before building on them.

| Role | v1.70 | AS2 | Confidence | Why it may be wrong |
|---|---|---|---|---|
| entity think `G_ThinkEntity` | 0x00405a60 | 0x0040cb30 | medium | propagation from `PF_create` (score 0.75); 40 bytes larger |
| entity frame loop `G_RunEntities` | 0x00405cc0 | 0x0040cdc0 | medium, manual | three times larger: the v1.70 collision pass is inlined; also called at level start |
| collision `G_RunCollisions` | 0x004055d0 | inlined into 0x0040cdc0 | medium, manual | no separate function; identified by its constants and calls only |
| touch dispatch `G_TouchEntity` | 0x004051d0 | 0x0040c120 | medium | propagation (score 0.73); 57 bytes larger |
| damage `G_Damage` | 0x00404ae0 | 0x0040b9e0 | medium | propagation from `PF_Damage` (score 0.83) |
| particle damage `G_ParticleDamage` | 0x00404cb0 | 0x0040bbc0 | medium | propagation from `PS_Update` (score 0.74) |
| player frame `G_PlayerFrame` | 0x0040b440 | 0x00413c50 | **low** | graph-led pass (score 0.58); the player record layout changed |
| per-player update (new) `G_UpdatePlayers` | none | 0x00414850 | low | name is a guess from its calls (terrain height, square roots) |
| camera `V_UpdateCamera` | 0x0040c260 | 0x00414e90 | medium | caller of `CL_CameraQuake`; same size, different instructions |
| camera reset `V_ResetCamera` | 0x0040c660 | 0x00415290 | medium, manual | shape changed; matched by constants 42/640/32 and call position |
| spawner `G_ActivateMapObjects` | 0x00407590 | 0x0040e7e0 | medium | propagation (score 0.80) |
| spawner `G_InitObject` | 0x00409ba0 | 0x00411e90 | high | unique string; grew by 338 bytes (the emitter allocation is inlined) |
| player spawn `G_SpawnPlayer` | 0x0040b2f0 | 0x00413b20 | medium | propagation from `PF_RespawnPlayer` (score 0.70); smaller in AS2 |
| render record `G_SyncRenderRecord` | 0x004057c0 | 0x0040c750 | medium | nearly twice the size (water alignment added) |
| script dispatch `SL_Execute` | 0x00419be0 | 0x0041eb10 | high | unique strings; near-identical |
| script events `SL_RunEvent` | 0x0041a200 | 0x0041f130 | medium | propagation from `PF_Shoot` (score 0.92) |
| init handler `G_RunInitHandler` | 0x00404fa0 | 0x0040bee0 | medium | propagation from `PF_create` (score 0.85) |
| frame `R_RenderView` | 0x0040e0d0 | 0x00430a20 | medium, manual | Direct3D rewrite; matched by callers, pass order and constants |
| model pass `R_DrawModel` | 0x00410ac0 | 0x00433f40 | medium, manual | Direct3D rewrite; matched by its callees |

## Matches that were corrected during the review

Automatic pairs that the manual review found wrong (the overrides file now fixes them):

- `R_RenderView` had been paired by its statistics strings with 0x0040aa40, which in AS2 only
  draws the statistics overlay (`R_DrawStats`); the real frame function is 0x00430a20. This also
  moved `R_DrawSprites` from 0x0040a9e0 (the frame-rate counter) to 0x0042fe80.
- `M_ShowMainMenu` had been paired with 0x0042b340, which is `UI_PopMenu` (the v1.70
  `M_ShowMainMenu` inlines a copy of `UI_PopMenu`, hence the shared string); the counterpart is
  0x0042bbc0.
- `R_BeginFrame` and `R_EndFrame` had been swapped: the Direct3D BeginScene/Clear calls are in
  0x00430e50, EndScene in 0x00431110.
- `R_Downsample2x` had been paired with 0x0042f3d0, which calls `IDirect3D8::CheckDeviceFormat`
  (now `D3D_FindTextureFormat`); v1.70 `R_Downsample2x` is marked removed.
- `XML_SetAttributeFloat` (dead code in v1.70) had been paired through two runtime callees with
  the registry writer 0x00405880; the neighbour pass now needs a higher score when all shared
  neighbours are runtime functions.

## Corrections to the v1.70 symbol files

Reported here; the v1.70 files were not edited.

1. 31 addresses of `re/symbols_v170.csv` are not function entries in the v1.70 export: menu
   callbacks and the window procedure that Ghidra left inside the preceding function (for
   example `MW_WndProc` 0x00421ee0 lies inside 0x00421ea0, `M_DrawStartGame` 0x0042b470 inside
   0x0042aea0). They are correct code addresses but cannot be matched by function; this map
   pairs them by hand through the callback lists of the menu builders.
2. Seven addresses carry two names in `re/symbols_v170.csv` (0x00411d70 `R_ModelRadius` twice,
   0x004198d0, 0x00419920, 0x00423780, 0x004237f0, 0x00423a20, 0x00425750).
3. `G_BeginLevel` names two functions: `re/symbols_v170_render.csv` gives it to 0x00407080
   (confidence GUESS), while `re/symbols_v170.csv` and the specs use `G_BeginLevel` for
   0x00408b30 and `G_StartLevel` for 0x00407080. This map follows the latter.
4. 96 of the 96 addresses of `re/symbols_v170_render.csv` are also in `re/symbols_v170.csv`,
   often under another name (for example 0x00418a20 `RegisterTexture` / `GL_UploadTexture`,
   0x0040d920 `R_DrawPlanarShadow` / `R_DrawBlobShadow`, 0x0040fde0 `R_DrawDynamicShadow` /
   `R_DrawPlanarShadow`). The two files should agree on one name per address.
5. `UI_ListClick_case4` (0x00422292) is a case of the window procedure's message switch that
   calls `UI_ListClick`, not a fragment of the list widget.

