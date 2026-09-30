# RCSL virtual machine and instruction set: Gulf Thunder delta (`gulf`)

Delta version 1.0 (package F1). Amends [../as2/rcsl-vm.delta.md](../as2/rcsl-vm.delta.md) and
[../as2/rcsl-opcodes.delta.md](../as2/rcsl-opcodes.delta.md) (which amend
[../rcsl-vm.md](../rcsl-vm.md) and [../rcsl-opcodes-v0.md](../rcsl-opcodes-v0.md)) for the game
`gulf` (AirStrike II: Gulf Thunder v2.71). The container delta of AirStrike 2
([../as2/rcsl-container.delta.md](../as2/rcsl-container.delta.md)) applies unchanged.

Tags: `VERIFIED-CODE` cites `gulf@0x…` and the AirStrike 2 counterpart `as2@0x…`; "same code"
means the `same` class of [symbol-map.md](symbol-map.md): identical instruction sequence with
addresses masked **and** the same read-only constants. `VERIFIED-DATA` holds for the 665
scripts of `assets_extracted_games/gulf/scripts` (49,658 instructions).

## Summary for implementers

**Nothing changes.** Run Gulf Thunder's scripts on the AirStrike 2 VM:

1. The interpreter, the script loader, thread creation, event dispatch, the entity pass and the
   script-global lookup are the same code as AirStrike 2's.
2. The script-global table has the same 28 names in the same order; the entity and
   player-record layouts are those of AirStrike 2.
3. The corpus uses three (opcode, mode) pairs that no earlier corpus uses (0x18/0x01,
   0x07/0x02, 0x07/0x00). The unchanged operand decoder handles them; a validator that lists
   allowed pairs must add them (or accept any combination of the mode bits, as the executable
   does).
4. One script has no CODE section (as in AirStrike 2's rule: valid, never runs).

## Interpreter and execution model

Same code, VERIFIED-CODE: interpreter gulf@0x41d540 (as2@0x41eb10), thread creation
gulf@0x4207f0 (as2@0x421db0), event dispatch gulf@0x41db60 (as2@0x41f130), stack error routine
gulf@0x41d4f0 (as2@0x41eac0), `SL_GetExternFunc` gulf@0x420670 (as2@0x421c30, string
`SL_GetExternFunc` gulf@0x0048afb0), `SL_FindGlobal` gulf@0x4206d0 (as2@0x421c90). The fatal
"Script stall detected." (gulf@0x0048a990) after 10,000 instructions per invocation, the
512-entry stack, the 31-entry jump table, the calling convention, latent calls, TMO and the POP
quirk are therefore those of the AirStrike 2 delta. Engine globals the interpreter uses:
current thread gulf@0x02104158, `self` gulf@0x02104168, argument pointer gulf@0x0210416c,
return register gulf@0x02104174, latent-done flag gulf@0x02104178, `frametime`
gulf@0x0049d760 (`re/symbols_gulf_data.csv`).

VERIFIED-DATA, the POP quirk: 1,332 of 1,332 POPs have B = 0 (every POP writes t0).

## Mode combinations

The Gulf Thunder corpus has 86 (opcode, mode) pairs (AirStrike 2: 82). Against AirStrike 2 it
adds five and lacks one (0x0E/0x10). Two of the five, 0x0C/0x22 and 0x10/0x02, occur in the
v1.70 corpus already (the AirStrike 2 delta lists them as "not in `as2`"). Three occur in no
earlier corpus:

| Opcode | Mode | Count | Meaning | Where |
|---|---|---|---|---|
| 0x18 `RET` | 0x01 | 3 | return with an immediate: bit 0x01 makes A the address of a copy of the immediate, RET copies [A] into the return register and ends the invocation with status 1 | `bosses\boss#1\pilesos.scr` 21, 22, 23: `RET #1.0`, `RET #0.0`, `RET #1.0` |
| 0x07 `LOR` | 0x02 | 1 | C = (int([A]) ≠ 0 or int(B) ≠ 0) | `mapobjects\house_cannon\house_cannon.scr` 25 |
| 0x07 `LOR` | 0x00 | 3 | C = (int(A) ≠ 0 or int(B) ≠ 0), both slots | `player\player2\player2.scr`, `player\player3\player3.scr`, `player\player7\player7.scr`, each at 2104 |

VERIFIED-CODE: the handlers of RET and LOR (same code as AirStrike 2, hence as v1.70) take
their operands from the common decoder, which applies bits 0x01/0x10 (immediate) and
0x02/0x20/0x80 (indirection) independently of the opcode. The player scripts' `LOR` is their death
handling ("no lives left, or the script's own flag v61 set": the wreck is deactivated instead
of respawned), so an implementation that rejects the pair breaks every helicopter's death.

## Globals

Same as AirStrike 2 (VERIFIED-CODE: the 28 records of the table gulf@0x0049b780 have the
names of as2@0x0049d908 in the same order, stride 12; the player-record globals point into
records of 0x164 bytes at gulf@0x020c3918 and gulf@0x020c3a7c).

Use by the scripts (VERIFIED-DATA, scripts declaring the global in DEFS; AirStrike 2 in
brackets): `self` 631 (593), `frametime` 236 (216), `player` 228 (231), `other` 129 (111),
`camera` 104 (96), `cb_msg` 74 (72), `g_map_pos` 55 (57), `l_waterlevel` 54 (53), `cb_parm1`
13 (8), `cameramode` **11** (1), `p_counter2` 5 (8), `p_speedfactor` 5 (8), `p_lives` 4 (7),
`p_action` 3, `p_weapon` 3, `p_counter1` 3, `cb_parm2` **2** (0), `l_water` 2, `p_scores` 2,
`p_stars` 1, **`player1` 1** (0), **`player2` 1** (0). `p_maxHealth` is declared by none.
`player1` and `player2` are used by one script, `bonus_level_start.scr` (it acts on the first
helicopter, and on the second one when `IsMultiplayer` returns 1); they hold the two players'
helicopter references (AirStrike 2 delta, "Globals").

## Entity references and fields

Same (every function that touches an entity is the same code). Script field indices 0..87 and
the engine-internal 88..91 of the AirStrike 2 delta hold.

## Other sections

Per-frame update, header field 1, random numbers, thread state, interpreter invocation, CALL
of a script subroutine, LCALL and TMO, values and arithmetic, quirks: same as AirStrike 2
(same code).

## Container

A script without a CODE section: `misc\svet_fonar_flicker.scr` (sections DATA only). As in
AirStrike 2 it is loaded and never runs. The other 664 files have the section sequences of
AirStrike 2 (`DEFS FUNC DATA STRG CODE` 306, `CASH DEFS FUNC DATA STRG CODE` 192, `DEFS FUNC
DATA CODE` 130, `FUNC DATA CODE` 31, `DEFS DATA CODE` 5); header field 1 is 16 in all.

## What an implementer must change

1. Accept the three pairs above (`tools/rcsl_disasm.py`'s list of seen modes, the strict
   parser, any validator in `engine/src/script`).
2. Nothing else.

## Checked sections

Of rcsl-vm.md (through the AirStrike 2 delta):

| Base section | Status for `gulf` |
|---|---|
| Summary for implementers | same (as AirStrike 2) |
| Per-frame update | same code (entity pass gulf@0x40cd50 = as2@0x40cdc0) |
| Event dispatch | same code |
| Globals | same (28 names, same order and layout); use counts above |
| Entity references and fields | same |
| Header field 1 | same (16 everywhere) |
| Random numbers | same code |
| Thread state | same code |
| Interpreter invocation | same code |
| CALL of a script subroutine | same code |
| LCALL and TMO | same code |
| Values and arithmetic | same code |
| Quirks an implementation must reproduce | same |
| Trace format | same (not engine behaviour) |
| Mock host | not checked |

Of rcsl-opcodes-v0.md (through the AirStrike 2 delta):

| Base section | Status for `gulf` |
|---|---|
| Status, Execution model, Calling convention, Latent calls | same code |
| The POP quirk | same (1,332 of 1,332) |
| Opcode table | same (the same 26 opcodes occur; handlers same code) |
| Mode combinations | changed: three pairs new to the family, table above |
| Entity fields | see "Entity references and fields" |
| Open questions | none |

## Changelog

- 1.0 (F1): first version.
