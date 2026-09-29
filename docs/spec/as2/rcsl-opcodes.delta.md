# RCSL instruction set: AirStrike 2 delta (`as2`)

Delta version 1.0. Amends [../rcsl-opcodes-v0.md](../rcsl-opcodes-v0.md) 0.2 for the game
`as2` (AirStrike 2 v2.51). Container delta: [rcsl-container.delta.md](rcsl-container.delta.md);
runtime delta: [rcsl-vm.delta.md](rcsl-vm.delta.md).

The base spec has no section numbers; this delta mirrors its headings. Corpus figures are
VERIFIED-DATA over the 631 files of `assets_extracted_games/as2/scripts` (51,617
instructions); code claims are VERIFIED-CODE with `as2@` and `v170@` addresses.

## Status

The `as2` interpreter is as2@0x41eb10 (1,437 bytes; v170@0x419be0, 1,434 bytes). The two were
compared instruction by instruction with absolute addresses masked: they are identical
except for

- the addresses of the engine globals (current thread as2@0x2106310, `self` as2@0x2106320,
  argument pointer as2@0x2106324, return register as2@0x210632c, latent-done flag
  as2@0x2106330, `frametime` as2@0x49f920), of the C runtime helpers (float-to-int
  truncation as2@0x47cbb0, allocation as2@0x43a07e), of the fatal-error routine
  (as2@0x405750, called with `Script stall detected.` as2@0x48c09c) and of the stack error
  routine (as2@0x41eac0, same code as v170@0x419b90);
- the offset of the player index in the entity, read in the prologue to select a global's
  address: entity + 0x80 (as2@0x41eb24) instead of entity + 0x77 (v170@0x419bf4). This
  instruction is 3 bytes longer, which shifts every later address of the function by 3.

The jump table (as2@0x41f0b0, 31 entries) sends every opcode to the handler at the same
instruction position as in v1.70 (checked entry by entry). So **every one of the 31 handlers
is unchanged**; the per-opcode semantics of the base apply to `as2` as written.

## Execution model

Same, VERIFIED-CODE (as2@0x41eb10): thread layout (0x824 bytes, same offsets; thread creation
as2@0x421db0 is the same code as v170@0x41c7d0), decode then `jmp [0x41f0b0 + 4 × opcode]`
for opcodes 0..0x1E, opcodes above 0x1E fall through as no-ops, pc += 14 after normal
handlers, a budget of 10,000 instructions per invocation (0x2710 at as2@0x41eb39) with the
fatal "Script stall detected." on exhaustion, stack of 512 entries with fatal overflow and
underflow, return status 0 from END and from a pending or completed LCALL, 1 from RET,
truncating integer conversion, 0.0/1.0 comparison results.

### Calling convention

Same. Builtins read t0, t1, ... through the argument pointer as2@0x2106324 and write the
single return register as2@0x210632c; CALL/LCALL copy the return register into B; the no-op
test before a builtin CALL is still there (as2@0x41ef3e). String arguments are STRG offsets
added to the thread's STRG pointer (+0x814), as in `setskin` as2@0x41fa50 and `TerraMorph`
as2@0x421a9b.

### Latent calls

Same code (as2@0x41efec..0x41f0ab against v170@0x41a0b9..0x41a178). The builtins that write
the done flag are listed in rcsl-builtins-table.delta.md (the same families as v1.70).

### The POP quirk

Same handler, and the compiler still emits POP with B = 0 and the restored slot in A and C:
1,338 of 1,338 POPs (VERIFIED-DATA). So every POP of the `as2` scripts writes t0.

## Opcode table

Handler addresses in `as2`; every handler is the same code as its v1.70 counterpart. "Count"
is the number of instructions in the `as2` corpus (v1.70 count in brackets).

| Opcode | Mnemonic | Count (v1.70) | as2 handler | v170 handler | Status |
|---|---|---|---|---|---|
| 0x00 | `END` | 2330 (1208) | 0x41efbf | 0x41a08c | same |
| 0x01 | `MUL` | 1594 (690) | 0x41ec2f | 0x419cfc | same |
| 0x02 | `DIV` | 350 (171) | 0x41ec3d | 0x419d0a | same |
| 0x03 | `ADD` | 1383 (595) | 0x41ec4b | 0x419d18 | same |
| 0x04 | `SUB` | 681 (292) | 0x41ec59 | 0x419d26 | same |
| 0x05 | `BAND` | 180 (96) | 0x41ec67 | 0x419d34 | same |
| 0x06 | `BOR` | 0 (0) | 0x41ec8f | 0x419d5c | same |
| 0x07 | `LOR` | 6 (3) | 0x41ecb7 | 0x419d84 | same |
| 0x08 | `LAND` | 18 (14) | 0x41ecef | 0x419dbc | same |
| 0x09 | `EQ` | 258 (152) | 0x41ed27 | 0x419df4 | same |
| 0x0A | `NE` | 12 (6) | 0x41ed54 | 0x419e21 | same |
| 0x0B | `GT` | 656 (255) | 0x41ed81 | 0x419e4e | same |
| 0x0C | `LT` | 763 (345) | 0x41edac | 0x419e79 | same |
| 0x0D | `GE` | 596 (270) | 0x41edd7 | 0x419ea4 | same |
| 0x0E | `LE` | 71 (36) | 0x41edff | 0x419ecc | same |
| 0x0F | `NOT` | 287 (128) | 0x41ee27 | 0x419ef4 | same |
| 0x10 | `NEG` | 262 (125) | 0x41ee48 | 0x419f15 | same |
| 0x11 | `MOV` | 13118 (5947) | 0x41ee53 | 0x419f20 | same |
| 0x12 | `LEA` | 12062 (5521) | 0x41ee5b | 0x419f28 | same |
| 0x13 | `PUSH` | 1338 (620) | 0x41ee8b | 0x419f58 | same |
| 0x14 | `POP` | 1338 (620) | 0x41eead | 0x419f7a | same |
| 0x15 | `ALLOC` | 0 (0) | 0x41eec4 | 0x419f91 | same |
| 0x16 | `NOP16` | 0 (0) | 0x41ee66 | 0x419f33 | same |
| 0x17 | `NOP17` | 0 (0) | 0x41ee66 | 0x419f33 | same |
| 0x18 | `RET` | 1095 (502) | 0x41efd5 | 0x41a0a2 | same |
| 0x19 | `JNZ` | 0 (0) | 0x41eedf | 0x419fac | same |
| 0x1A | `JZ` | 3288 (1496) | 0x41ef00 | 0x419fcd | same |
| 0x1B | `JMP` | 905 (391) | 0x41ef21 | 0x419fee | same |
| 0x1C | `CALL` | 6905 (3189) | 0x41ef35 | 0x41a002 | same |
| 0x1D | `LCALL` | 1366 (624) | 0x41efec | 0x41a0b9 | same |
| 0x1E | `TMO` | 755 (379) | 0x41efb0 | 0x41a07d | same |

VERIFIED-DATA: the same 26 opcodes occur as in v1.70 and no other; 0x06, 0x15, 0x16, 0x17 and
0x19 are still unused. Every jump target lies inside its script and every call target is a
valid instruction or FUNC index (the strict parser and `check_builtin_calls.py` accept all
files).

### Evidence from the data, per group

The base's examples come from v1.70 files. `as2` examples of the same patterns: field access
through `self` (`LEA t0 = &$self[5]` then a component), `TMO #0.3; LCALL sleep` in
`bonuses\satellite_strike\satellite_strike.scr`, and the new builtins used with the argument
shapes given in rcsl-builtins-table.delta.md (for example `MOV t0 = #500.0; CALL Lightning` in
`bonuses\lightingbomb\lightingbomb_proj.scr`).

## Mode combinations

Every (opcode, mode) pair of the base table is decoded by the unchanged operand decoder
(rcsl-container.md, "Operand encoding"). The `as2` corpus has 82 pairs. Four pairs of the
v1.70 corpus do not occur in `as2` (0x0B/0x22, 0x0C/0x22, 0x0E/0x22, 0x10/0x02); their
meaning is unchanged. Seven pairs are new. They need no new code: the mode bits select
immediate (0x01 for A, 0x10 for B) and indirection (0x02 for A, 0x20 for B, 0x80 for C) exactly
as for the pairs the base lists, and the handler is the same. Meaning, count and an example
(disassembler notation of the base):

| Opcode | Mode | Count | Meaning | Example (file, instruction) and context |
|---|---|---|---|---|
| 0x01 `MUL` | 0x22 | 4 | C = [A] × [B], both operands read through the pointers held in A and B | `MUL t1 = [t1] * [t2]` (`ships\avianos\plane.scr` 50): velocity component times a component of a vector variable, then `MUL t1 = t1 * $frametime` and `ADD [t0] = [t0] + t1` into the origin |
| 0x02 `DIV` | 0x01 | 6 | C = imm(A) / B | `DIV t1 = #1.0 / v87` (`player\player1\player1.scr` 1407, same in player2, player3 and others): reciprocal of the velocity length, guarded by `JZ` on v87 just before |
| 0x03 `ADD` | 0x21 | 3 | C = imm(A) + [B] | `ADD t1 = #50.0 + [t2]` (`experemental\terra_blast.scr` 40): 50 plus self.origin.z |
| 0x07 `LOR` | 0x22 | 1 | C = (int([A]) ≠ 0 or int([B]) ≠ 0) | `LOR t0 = int([t0]) \|\| int([t1])` (`eol.scr` 62): the two components of vector variable v16, then `NOT` and `JZ` |
| 0x0E `LE` | 0x02 | 8 | C = ([A] ≤ B) | `LE t0 = [t0] <= t1` (`bosses\boss_2\boss_2.scr` 199): origin.z ≤ `TerrainHeight(x, y)` + 60, then `JZ` / `RET` |
| 0x0E `LE` | 0x10 | 1 | C = (A ≤ imm(B)) | `LE t0 = $cb_parm1 <= #0.0` (`bosses\boss_1\boss_1_bashnya.scr` 29, callback handler) |
| 0x10 `NEG` | 0x00 | 2 | C = −A, A a slot | `NEG t1 = -t1` (`_new\meteorite_falling.scr` 56 and `_new\meteorite_medium_falling.scr` 56): negates `5 × random()` before storing it through `[t0]` |

For the record (belongs to the `gulf` delta): the third game adds 0x18/0x01 (`RET #1.0`, the
decoder makes A the address of a copy of the immediate and RET copies it into the return
register), 0x07/0x02 and 0x07/0x00, all decoded by the same rules.

## Entity fields

Superseded, as in the base, by the entity field map in rcsl-vm.md; the `as2` changes (none in
the script-visible indices) are in rcsl-vm.delta.md, "Entity references and fields".

## Open questions (prioritised)

None for the instruction set: all handlers are shown unchanged.

## What an implementer must change

Against the instruction set as the base spec describes it:

1. Nothing. Run `as2` scripts on the v1.70 interpreter.
2. If an implementation validates (opcode, mode) pairs against the v1.70 list (as
   `tools/rcsl_disasm.py`'s `SEEN_MODES` does), add the seven pairs above; better, accept any
   combination of the mode bits 0x01, 0x02, 0x10, 0x20, 0x80 as the executable does.

## Checked sections

| Base section | Status |
|---|---|
| Status | same (interpreter compared instruction by instruction) |
| Execution model | same |
| Calling convention | same |
| Latent calls | same |
| The POP quirk | same (VERIFIED-DATA: 1,338 of 1,338 POPs write t0) |
| Opcode table | same (all 31 handlers; one row per opcode above) |
| Evidence from the data, per group | same |
| Mode combinations | changed (7 new pairs in the corpus, decoded above; no code change) |
| Entity fields | see rcsl-vm.delta.md |
| Open questions | same (none open) |

## Changelog

- 1.0 (B2): first version.
