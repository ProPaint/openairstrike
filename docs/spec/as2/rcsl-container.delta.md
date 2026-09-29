# RCSL compiled script: AirStrike 2 delta (`as2`)

Delta version 1.0. Amends [../rcsl-container.md](../rcsl-container.md) 0.2 for the game `as2`
(AirStrike 2 v2.51, `AirStrike3D II.exe`). Companions: [rcsl-opcodes.delta.md](rcsl-opcodes.delta.md),
[rcsl-vm.delta.md](rcsl-vm.delta.md), [rcsl-builtins-table.delta.md](rcsl-builtins-table.delta.md).
Checker: `tools/ref/check_builtin_calls.py --game as2`.

The base spec has no section numbers; this delta mirrors its section headings one to one.
`VERIFIED-CODE` cites `as2@0x...` (and `v170@0x...` for a comparison). `VERIFIED-DATA` holds
for all 631 files of `assets_extracted_games/as2/scripts` (630 `.scr` and one `.scr` copy named
`.sc`, see "Overview"; 51,617 instructions).

How the comparison was made: every VM function of AS2 was paired with its v1.70 counterpart
(through the builtin table, the interpreter's strings and the call graph) and the two
disassemblies were compared instruction by instruction, with absolute addresses masked. "Same
code" below means the instruction sequences are identical apart from absolute addresses (and,
where stated, the entity offsets listed in rcsl-vm.delta.md).

## Overview

Same format. VERIFIED-DATA: the sections tile every file from 0x38 to its end, each tag
appears at most once, and the same five orders as in v1.70 occur, no other:

| Count | Section order |
|---|---|
| 302 | DEFS FUNC DATA STRG CODE |
| 174 | CASH DEFS FUNC DATA STRG CODE |
| 115 | DEFS FUNC DATA CODE |
| 36 | FUNC DATA CODE |
| 4 | DEFS DATA CODE |

The presence rules of the base (CASH, DEFS, FUNC, STRG present exactly when their count is
non-zero; DATA and CODE always present) hold (VERIFIED-DATA: `tools/rcsl_disasm.py`'s strict
parser accepts all 631 files).

One file has another extension: `scripts\transport\truck_medium_kuzov\truck_medium_kuzov_wheel.sc`
is byte-identical to `truck_medium_kuzov_wh.scr` next to it and no object definition names it
(the definitions in `objects\transport.obj` name the `.scr`). It is a leftover; an
implementation needs no special case for it. VERIFIED-DATA.

## Loader behaviour

Changed in two details that have no effect on the shipped data. The loader is
as2@0x421f60 (v170@0x41c980); VERIFIED-CODE unless marked.

- Same as v1.70: the 0x38-byte header is read into a zeroed 0x90-byte script object
  (allocation as2@0x421fea, zeroing as2@0x422011), only the magic is checked
  (as2@0x422049); a bad magic logs `'%s': File header corrupted.` (as2@0x48ada4, same text as
  v170@0x447474) and the load fails with -1. Header field 1 and all counts are not
  validated; there is **no version check** for scripts.
- Same: the section loop dispatches on the tag, skips unknown tags by their length, reads
  known sections by the header counts (CODE = count × 14 bytes), not by the section length.
- Same: FUNC names are resolved by SL_GetExternFunc as2@0x421c30 (v170@0x41c680) against the
  table at as2@0x49d5d0; an unknown name logs `SL_GetExternFunc: Built-in function '%s' not
  supported.` and resolves to NULL. DEFS names are resolved by as2@0x421c90 (v170@0x41c6e0)
  against the table at as2@0x49d908; an unknown name resolves to NULL silently.
- Changed: a DATA section with count 0 and a STRG section with size 0 allocate nothing; the
  table pointer stays NULL from the zeroed object (as2@0x4221e0, as2@0x42223f). v1.70
  allocated a zero-size block (v170@0x41cc07, v170@0x41cc58). No observable difference: an
  empty table is never read.
- A missing section is never an error. A script without a CODE section keeps a NULL code
  pointer and an instruction count of 0. Such a file does not occur in `as2` (VERIFIED-DATA);
  it occurs once in the third game (see the `gulf` delta), with all five entry points
  0xFFFFFFFF, so nothing is ever executed from it.
- The message `G_LoadBin: Illegal '%s' version.` (as2@0x489fbc) belongs to the loader of the
  save file `game.bin` (as2@0x4069e0), not to scripts.
- Script objects are appended to the array at as2@0x2106340, count at as2@0x2106338
  (v170: 0x1fa7e08, 0x1fa7e04). Object layout unchanged: header at +0x40, table pointers at
  +0x78 CASH, +0x7C DEFS, +0x80 FUNC, +0x84 DATA, +0x88 STRG, +0x8C CODE (as2@0x422305,
  as2@0x422298, as2@0x42217c, as2@0x4221f7, as2@0x42224d, as2@0x4220f6).
- The unload routine as2@0x421ce0 (v170@0x41c730) now also frees the DATA table and the
  script object itself. Not observable.

Loading on demand, by name from the object definitions, is outside this spec.

## Header

Same fields and meanings. VERIFIED-DATA for `as2`:

| Field | as2 corpus |
|---|---|
| 1 | always 16 (631 of 631); still never read (a scan of every access through the script array as2@0x2106340 finds no read of +0x44) |
| 6 frame size | 16..296 (v1.70: 16..81) |
| 9..13 entry points | every present value below the instruction count |

## Section payloads

### CASH: precache list

Same. Kinds 0 (531 entries) and 3 (39 entries), no other (VERIFIED-DATA). The precache
routine is as2@0x421ed0 (v170@0x41c8f0); record size 0x48 as before.

### DEFS: globals used

Same encoding and resolution. The engine table has **28** names (was 24); the table, the
addresses and the four new names are in rcsl-vm.delta.md, "Globals". 19 names are used by the
`as2` scripts (VERIFIED-DATA, scripts that declare the name): self 594, player 231,
frametime 216, other 111, camera 96, cb_msg 72, g_map_pos 57, l_waterlevel 53, cb_parm1 8,
p_speedfactor 8, p_counter2 8, p_lives 7, p_action 6, p_weapon 6, p_counter1 6, l_water 2,
p_scores 2, cameramode 1, p_stars 1. All resolve. The new names `player1`, `player2` and
`p_maxHealth` are not used by any `as2` script.

### FUNC: builtins used

Same encoding and resolution. The engine registers **101** builtins (was 85) in a different
order; see rcsl-builtins-table.delta.md. 75 distinct names are used by the `as2` scripts, 5 of
them new (`TerraMorph`, `G_SetPowerUpCount`, `GetPlayerAccel`, `RadialDamagePlayer`,
`DetachEntity`); all resolve (VERIFIED-DATA, `check_builtin_calls.py` reports no unknown
name). The four `l`-prefixed aliases still share the implementation of their plain twins
(table records as2@0x49d798..0x49d7d0).

### DATA: slot initialisers

Same. Kinds 2 (110 entries) and 3 (344 entries), no other; every kind-3 entry has
`value = slot + 1` (VERIFIED-DATA). Applied by the thread creation as2@0x421db0, whose code is
the same as v170@0x41c7d0.

### STRG: string literals

Same (VERIFIED-DATA: every file's STRG ends with NUL and matches header field 7).

### CODE: instructions

Same: 14-byte instructions (the interpreter advances by 0xE, same code as v1.70).

## Operand encoding

Same. The operand decoder as2@0x41eb4b..0x41ec1c is the same code as v170@0x419c18..0x419ce9,
including the exceptions for opcodes 0x15, 0x1B, 0x1C, 0x1D (A raw), 0x19, 0x1A (B raw) and
0x12 (B raw). The only difference in the interpreter's prologue is the offset of the player
index used to pick a global's address: entity + 0x80 instead of entity + 0x77 (as2@0x41eb24,
v170@0x419bf4); see rcsl-vm.delta.md.

Mode bytes seen in `as2`: 00 01 02 10 12 20 21 22 80 82 92 A2, the same set as v1.70; mode
bits 0x04, 0x08, 0x40 never set (VERIFIED-DATA). Seven (opcode, mode) pairs are new; they are
decoded in rcsl-opcodes.delta.md.

**Globals** in operands: same rule; which engine variable each of the 28 names maps to is in
rcsl-vm.delta.md. `self` holds entity + 0x84 in `as2` (entity + 0x7B in v1.70); scripts do
not see the difference, field k is still at `self + 4k`.

**String literals**: same rule (MOV immediate B whose raw value is a STRG offset).

## Entry points and events

Same five slots and the same dispatch rules; details and the changes to the triggers are in
rcsl-vm.delta.md, "Event dispatch". The dispatchers read the entry point from the same
script-object offsets (+0x64 init .. +0x74 callback; e.g. touch as2@0x40c276).

Scripts with each entry (VERIFIED-DATA): init 422, main 593, damage 175, touch 163,
callback 72. Populated combinations (init, main, damage, touch, callback): init+main 152; main
only 128; init+main+damage 80; init+main+callback 68; init+main+touch 61;
init+main+damage+touch 55; main+touch 43; damage only 33; init+main+touch+callback 3;
main+damage 3; init+damage 2; damage+touch 1; damage+callback 1; init only 1.

## Reference parser checks

`tools/rcsl_disasm.py` (strict parser) reads all 631 `as2` files. Its opcode and mode tables
describe v1.70 only: the seven new (opcode, mode) pairs are not in its `SEEN_MODES`, and its
builtin annotations come from the v1.70 table (it prints the new builtins without a
signature and `Lightning` without its argument). Parameterising the tool by game is left to
the tooling package; `tools/ref/check_builtin_calls.py` parses the files itself.

## What an implementer must change

Against the loader as the base spec describes it:

1. Nothing in the file format. Load `as2` scripts with the v1.70 loader.
2. Resolve DEFS names against the 28-name `as2` table and FUNC names against the 101-name
   `as2` table (rcsl-vm.delta.md, rcsl-builtins-table.delta.md), selected by game.
3. Accept frame sizes up to at least 296 slots (the format allows any u32).
4. Optional: an empty DATA or STRG table may be represented as absent.

## Checked sections

| Base section | Status |
|---|---|
| Overview | same (VERIFIED-DATA over 631 files) |
| Loader behaviour | changed (empty DATA/STRG not allocated; no observable effect) |
| Header | same |
| Section payloads: CASH | same |
| Section payloads: DEFS | changed (table of 28 names, see rcsl-vm.delta.md) |
| Section payloads: FUNC | changed (table of 101 builtins, see rcsl-builtins-table.delta.md) |
| Section payloads: DATA | same |
| Section payloads: STRG | same |
| Section payloads: CODE | same |
| Operand encoding | same (decoder code identical; player-index offset see rcsl-vm.delta.md) |
| Entry points and events | same slots; trigger changes in rcsl-vm.delta.md |
| Reference parser checks | not checked for `as2` by `test_rcsl.py` (v1.70 only); see above |

## Changelog

- 1.0 (B2): first version.
