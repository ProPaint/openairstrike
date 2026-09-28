# RCSL instruction set, version 0

Spec version 0.1. Container format: [rcsl-container.md](rcsl-container.md).
Reference: `tools/rcsl_disasm.py` (`OPCODES`, `SEEN_MODES`), test `tools/ref/test_rcsl.py`
(which reads the two tables below and fails if they disagree with the corpus).

Status. The operand decoder and every opcode handler of the v1.70 interpreter were read
from the executable (0x419be0..0x41a178), so the semantics below are tagged VERIFIED-CODE
with the handler address. The corpus was then checked against that reading
(VERIFIED-DATA: bounds, operand shapes, and the behaviour of small scripts). What is not
known yet is listed at the end; WP-21 should re-confirm the handlers with Ghidra and fill
in the builtins.

## Execution model (VERIFIED-CODE)

- **Thread.** Each script instance is a 0x824-byte thread created at 0x41c7d0:
  +0x00 script index, +0x04 DEFS resolution array, +0x08 FUNC resolution array,
  +0x0C pc (pointer to the current instruction), +0x10 stack depth, +0x14 stack of 512
  floats, +0x814 STRG pointer, +0x818 frame (header[6] slots, zeroed, then DATA applied),
  +0x81C CODE pointer, +0x820 latent timeout. The pc starts at the `main` entry.
- **Interpreter** 0x419be0. Engine globals it uses: current thread 0x1fa7de0, `self`
  0x1fa7df0, argument pointer 0x1fa7df4 (= the frame, i.e. slot 0), return register
  0x1fa7dfc (float), latent-done flag 0x1fa7e00.
- **Loop.** For each instruction: decode the three operand addresses (see container spec,
  "Operand encoding"), then `jmp [0x41a17c + 4*opcode]` for opcodes 0..0x1E; opcodes above
  0x1E do nothing and fall through to the next instruction. After a normal handler,
  pc += 14. Jumps and calls set the pc themselves.
- **Stall guard.** At most 10,000 instructions per interpreter invocation (counter 0x2710
  at 0x419c06); exceeding it calls 0x4204f0 with "Script stall detected.". 0x4204f0 is a
  fatal-error routine (it calls a series of shutdown functions; details not analysed).
- **Stack.** PUSH/POP use the per-thread stack of 512 entries. Overflow ("Script '%s':
  Thread stack overflow.", 0x419f70) and underflow (0x41a098) call 0x419b90, which logs and
  exits the process.
- **Return status.** The interpreter returns 0 from END and from a pending latent call,
  1 from RET. RET also copies its operand into the return register. Neither END nor RET
  advances the pc.
- **Integers.** Integer conversions truncate toward zero (0x440870). Comparison results
  and logical results are the floats 0.0 and 1.0.

### Calling convention (VERIFIED-CODE, confirmed by the data)

- Builtins read their arguments from frame slots t0, t1, t2, ... through the argument
  pointer (e.g. `TerrainHeight` 0x41baf0 reads t0 and t1; `lerp` 0x41a4e0 reads t0..t2) and
  write their result to the return register 0x1fa7dfc. Vector arguments are passed as
  pointers stored in a slot (`move` 0x41af60 reads `*(float**)t0`).
- CALL/LCALL copy the return register into operand B. Builtins that return nothing leave
  the register unchanged, so B then receives a stale value.
- String arguments are STRG byte offsets in the raw bits of the slot (`create`,
  `StartSound`, `AttachActivate`, ... add them to the thread's STRG pointer).
- Script subroutines (CALL with A ≥ 0) share the caller's frame. The compiler saves live
  temporaries with PUSH before a call and restores them with POP afterwards (see the POP
  quirk below).
- Immediately before every builtin CALL (not LCALL) the handler does:
  `if t0 == t1 and t1 == 1.0 then t0 = t1` (0x41a00b..0x41a02e), which has no effect.
  Recorded only so nobody wonders about it.

### Latent calls (VERIFIED-CODE, 0x41a0b9..0x41a178)

LCALL (0x1D) implements waiting. On each execution it clears the done flag and runs the
callee once:

- builtin callee: the builtin may set the done flag. Only the `RotateTo`,
  `RotateToClamp`, `MoveToNextWP`, `RotateToNextWP` implementations write it (from a
  helper's return value; GUESS: non-zero when the motion is finished), and `sleep`
  (0x41b0c0) sets it to 0, so `sleep` only ends through the timeout;
- script callee: the subroutine runs from its start in a nested interpreter; the done flag
  is its return status (1 if it ended with RET, 0 if with END).

If the flag is set, the call is complete: pc advances, the timeout is reset to 0, B gets
the return register, and the interpreter returns 0 (the thread yields for this update).
Otherwise, if a timeout was set by TMO (0x1E), it is decreased by `frametime`; when it
reaches ≤ 0 the call completes the same way. Otherwise the interpreter returns 0 with the
pc still on the LCALL, so the callee is run again at the next update.

Consequences, all consistent with the corpus (VERIFIED-DATA): `sleep` is always preceded
by TMO (90 of 90 LCALLs of `sleep`); the `l*` builtins are only ever called with LCALL
and their plain twins only with CALL; a script subroutine called with LCALL acts as a
per-frame "think" function until it RETs (167 cases without timeout) or for the given
duration (277 cases after TMO).

Inside an event handler (see container spec) a pending LCALL makes the interpreter
return; the dispatcher then restores the saved pc, so the rest of the handler is
abandoned. GUESS: the compiler or script authors avoid this; not checked.

### The POP quirk (VERIFIED-CODE 0x419f7a, VERIFIED-DATA)

POP writes the popped value to the location of **operand B**. The compiler, however, puts
the slot it means to restore in operands A and C and leaves B = 0 (620 of 620 POPs).
So every POP in the shipped scripts writes t0. Because each restore sequence ends with the
POP matching the first PUSH, which is always the push of t0 (336 of 336 sequences), t0 ends
up correct; the other restores are lost. Builtins never write frame slots, so after a
builtin CALL the lost restores are harmless. A reimplementation must reproduce the
executable, not the compiler's intent.

## Opcode table

Operands: A = op1, B = op2, C = op3, meaning the decoded location (after immediate and
indirection flags) unless marked raw. `Count` is the number of instructions in the
v1.70 corpus. Handler addresses are the jump-table targets.

| Opcode | Mnemonic | Count | Semantics | Handler | Confidence |
|---|---|---|---|---|---|
| 0x00 | `END` | 1208 | stop; interpreter returns 0; pc is not advanced | 0x41a08c | VERIFIED-CODE |
| 0x01 | `MUL` | 690 | C = A × B | 0x419cfc | VERIFIED-CODE |
| 0x02 | `DIV` | 171 | C = A / B | 0x419d0a | VERIFIED-CODE |
| 0x03 | `ADD` | 595 | C = A + B | 0x419d18 | VERIFIED-CODE |
| 0x04 | `SUB` | 292 | C = A − B | 0x419d26 | VERIFIED-CODE |
| 0x05 | `BAND` | 96 | C = int(A) & int(B) | 0x419d34 | VERIFIED-CODE |
| 0x06 | `BOR` | 0 | C = int(A) \| int(B) | 0x419d5c | VERIFIED-CODE |
| 0x07 | `LOR` | 3 | C = (int(A) ≠ 0 or int(B) ≠ 0) | 0x419d84 | VERIFIED-CODE |
| 0x08 | `LAND` | 14 | C = (int(A) ≠ 0 and int(B) ≠ 0) | 0x419dbc | VERIFIED-CODE |
| 0x09 | `EQ` | 152 | C = (int(A) = int(B)), integer compare | 0x419df4 | VERIFIED-CODE |
| 0x0A | `NE` | 6 | C = (int(A) ≠ int(B)), integer compare | 0x419e21 | VERIFIED-CODE |
| 0x0B | `GT` | 255 | C = (A > B), float compare, false if unordered | 0x419e4e | VERIFIED-CODE |
| 0x0C | `LT` | 345 | C = (A < B) | 0x419e79 | VERIFIED-CODE |
| 0x0D | `GE` | 270 | C = (A ≥ B) | 0x419ea4 | VERIFIED-CODE |
| 0x0E | `LE` | 36 | C = (A ≤ B) | 0x419ecc | VERIFIED-CODE |
| 0x0F | `NOT` | 128 | C = (int(A) = 0) | 0x419ef4 | VERIFIED-CODE |
| 0x10 | `NEG` | 125 | C = −A | 0x419f15 | VERIFIED-CODE |
| 0x11 | `MOV` | 5947 | A = B (4 bytes copied through the FPU) | 0x419f20 | VERIFIED-CODE |
| 0x12 | `LEA` | 5521 | C = (pointer stored at A) + 4 × B, with B raw | 0x419f28 | VERIFIED-CODE |
| 0x13 | `PUSH` | 620 | push A | 0x419f58 | VERIFIED-CODE |
| 0x14 | `POP` | 620 | B = pop (see quirk) | 0x419f7a | VERIFIED-CODE |
| 0x15 | `ALLOC` | 0 | C = heap allocation of A bytes, A raw | 0x419f91 | VERIFIED-CODE |
| 0x16 | `NOP16` | 0 | nothing | 0x419f33 | VERIFIED-CODE |
| 0x17 | `NOP17` | 0 | nothing | 0x419f33 | VERIFIED-CODE |
| 0x18 | `RET` | 502 | return register = A; interpreter returns 1; pc not advanced | 0x41a0a2 | VERIFIED-CODE |
| 0x19 | `JNZ` | 0 | if A ≠ 0 (or NaN): pc = this + B (B raw) | 0x419fac | VERIFIED-CODE |
| 0x1A | `JZ` | 1496 | if A = 0: pc = this + B (B raw) | 0x419fcd | VERIFIED-CODE |
| 0x1B | `JMP` | 391 | pc = this + A (A raw) | 0x419fee | VERIFIED-CODE |
| 0x1C | `CALL` | 3189 | A raw < 0: builtin FUNC[−A−1]; A ≥ 0: script subroutine at instruction A in a nested interpreter; then B = return register | 0x41a002 | VERIFIED-CODE |
| 0x1D | `LCALL` | 624 | latent call, see above; B = return register on completion | 0x41a0b9 | VERIFIED-CODE |
| 0x1E | `TMO` | 379 | latent timeout (thread +0x820) = A | 0x41a07d | VERIFIED-CODE |

Jump offsets are counted in instructions from the jump itself (`this`), not from the next
instruction. VERIFIED-DATA: every JZ/JMP target lies inside the script, every CALL/LCALL
subroutine target is a valid instruction index, every builtin index is inside FUNC.

26 opcodes occur; 0x06, 0x15, 0x16, 0x17 and 0x19 are implemented but unused.

Operands a handler does not use are still decoded, including their indirection (which
reads memory). They are zero in the corpus except in these cases: POP carries the
compiler's register number in A and C; the compiler sets mode bit 0x80 on PUSH/POP and on
MOV with an indirect target, where C is not used (GUESS: the compiler marks "destination is
through a pointer" without regard to which operand the handler writes).

### Evidence from the data, per group

- **Arithmetic and moves.** `helics\vint270.scr` is `MOV t0 = #370.0; CALL rotatez; END`,
  its siblings differ only in the immediate (vint180: 250.0, vint270i: −370.0,
  vint270small: 270.0 with `rotatex`, `radar.scr`: 60.0, `misc\windmill_lopasti.scr`:
  100.0 with `rotatey`). `rotatez` (0x41b0a0) adds `t0 × frametime` to the angle z field,
  so the immediate is a rotation speed in degrees per second.
- **LEA and fields.** `LEA t0 = &$self[5]` followed by `LEA t0 = &t0[2]` addresses the z
  component of the origin (fields 5, 6, 7), confirmed by `move` (0x41af60) adding to
  entity+0x7B+0x14/0x18/0x1C.
- **Comparisons and JZ.** In `tanks\tank.scr` the damage handler is
  `LEA t0 = &$self[34]; LT t0 = [t0] < #50.0; JZ t0, +3; ...` (field 34 is health: the
  damage routine 0x404ae0 subtracts from entity+0x103 = 0x7B + 4 × 34 before running the
  handler).
- **Calls.** `TerrainHeight(x, y)` in `barrel\barrel_benzin_dead.scr`: t0 = origin.x,
  t1 = origin.y, `CALL t1 = TerrainHeight`, then `MOV [t0] = t1` after restoring t0 (the
  pushed origin.z pointer).
- **Latent.** `weapons\hit.scr`: `TMO #2.0; LCALL sleep; MOV t0 = $self; CALL remove`.
  `boss1\block_left.scr` main: `EQ t0 = v16 == 1; JZ ...; MOV v16 = 0; LCALL sub_0002`
  where sub_0002 moves the block a little each frame and RETs when it reaches its limit;
  its `callback` handler is `MOV v16 = $cb_msg; END`.

## Mode combinations

Every (opcode, mode) pair in the v1.70 corpus, with its count and one example as printed
by the disassembler (`tN` temporary slot, `vN` script variable, `$name` DEFS global,
`#x` float immediate, `[...]` indirection).

| Opcode | Mode | Count | Example |
|---|---|---|---|
| 0x00 | 0x00 | 1208 | `END` |
| 0x01 | 0x00 | 41 | `MUL t1 = v16 * v31` |
| 0x01 | 0x01 | 400 | `MUL t1 = #17.0 * t2` |
| 0x01 | 0x02 | 6 | `MUL t1 = [t1] * v20` |
| 0x01 | 0x10 | 123 | `MUL t1 = v16 * #4.0` |
| 0x01 | 0x12 | 24 | `MUL t3 = [t3] * #180.0` |
| 0x01 | 0x20 | 1 | `MUL t2 = t2 * [t3]` |
| 0x01 | 0x21 | 93 | `MUL t2 = #200.0 * [t3]` |
| 0x01 | 0x92 | 2 | `MUL [t0] = [t0] * #-0.2` |
| 0x02 | 0x00 | 20 | `DIV t1 = t1 / v16` |
| 0x02 | 0x10 | 48 | `DIV t1 = v18 / #0.6` |
| 0x02 | 0x12 | 103 | `DIV t2 = [t2] / #2.0` |
| 0x03 | 0x00 | 94 | `ADD v16 = v16 + t1` |
| 0x03 | 0x01 | 95 | `ADD t1 = #0.5 + t2` |
| 0x03 | 0x02 | 32 | `ADD t1 = [t1] + t2` |
| 0x03 | 0x10 | 98 | `ADD t1 = $l_waterlevel + #2.0` |
| 0x03 | 0x12 | 15 | `ADD t1 = [t1] + #15.0` |
| 0x03 | 0x82 | 122 | `ADD [t0] = [t0] + t1` |
| 0x03 | 0x92 | 136 | `ADD [t0] = [t0] + #30.0` |
| 0x03 | 0xA2 | 3 | `ADD [t0] = [t0] + [t1]` |
| 0x04 | 0x00 | 1 | `SUB t1 = v16 - v17` |
| 0x04 | 0x01 | 26 | `SUB t1 = #1.0 - t2` |
| 0x04 | 0x02 | 24 | `SUB t0 = [t0] - $g_map_pos` |
| 0x04 | 0x10 | 41 | `SUB v16 = v16 - #0.2` |
| 0x04 | 0x12 | 33 | `SUB t1 = [t1] - #25.0` |
| 0x04 | 0x20 | 1 | `SUB t0 = $g_map_pos - [t1]` |
| 0x04 | 0x21 | 31 | `SUB t1 = #0.6 - [t2]` |
| 0x04 | 0x22 | 34 | `SUB t0 = [t0] - [t1]` |
| 0x04 | 0x82 | 98 | `SUB [t0] = [t0] - t1` |
| 0x04 | 0x92 | 3 | `SUB [t0] = [t0] - #30.0` |
| 0x05 | 0x10 | 96 | `BAND t0 = int($p_action) & int(#1.0)` |
| 0x07 | 0x20 | 3 | `LOR t0 = int(t0) or int([t1])` |
| 0x08 | 0x00 | 14 | `LAND t0 = int(t0) and int(t1)` |
| 0x09 | 0x10 | 112 | `EQ t0 = int(v16) == int(#1.0)` |
| 0x09 | 0x12 | 40 | `EQ t0 = int([t0]) == int(#0.0)` |
| 0x0A | 0x10 | 6 | `NE t0 = int($p_weapon) != int(#9.0)` |
| 0x0B | 0x00 | 4 | `GT t0 = v19 > v16` |
| 0x0B | 0x02 | 28 | `GT t0 = [t0] > t1` |
| 0x0B | 0x10 | 54 | `GT t0 = v16 > #1.0` |
| 0x0B | 0x12 | 165 | `GT t0 = [t0] > #0.0` |
| 0x0B | 0x20 | 2 | `GT t0 = t0 > [t1]` |
| 0x0B | 0x22 | 2 | `GT t0 = [t0] > [t1]` |
| 0x0C | 0x00 | 19 | `LT t0 = v17 < v16` |
| 0x0C | 0x02 | 47 | `LT t0 = [t0] < v16` |
| 0x0C | 0x10 | 154 | `LT t0 = v16 < #12.0` |
| 0x0C | 0x12 | 121 | `LT t0 = [t0] < #50.0` |
| 0x0C | 0x20 | 2 | `LT t0 = t0 < [t1]` |
| 0x0C | 0x22 | 2 | `LT t0 = [t0] < [t1]` |
| 0x0D | 0x02 | 3 | `GE t0 = [t0] >= t1` |
| 0x0D | 0x10 | 148 | `GE t0 = t0 >= #0.5` |
| 0x0D | 0x12 | 102 | `GE t0 = [t0] >= #70.0` |
| 0x0D | 0x22 | 17 | `GE t0 = [t0] >= [t1]` |
| 0x0E | 0x00 | 24 | `LE t0 = t0 <= t1` |
| 0x0E | 0x12 | 11 | `LE t0 = [t0] <= #-20.0` |
| 0x0E | 0x22 | 1 | `LE t0 = [t0] <= [t1]` |
| 0x0F | 0x00 | 110 | `NOT t0 = !int(v16)` |
| 0x0F | 0x02 | 18 | `NOT t0 = !int([t0])` |
| 0x10 | 0x01 | 123 | `NEG t1 = -#500.0` |
| 0x10 | 0x02 | 2 | `NEG t0 = -[t0]` |
| 0x11 | 0x00 | 1619 | `MOV t0 = $self` |
| 0x11 | 0x10 | 2909 | `MOV t0 = #360.0` or `MOV t0 = str@40"expl1"` |
| 0x11 | 0x20 | 193 | `MOV t1 = [t1]` |
| 0x11 | 0x82 | 469 | `MOV [t0] = t1` |
| 0x11 | 0x92 | 740 | `MOV [t0] = #50.0` |
| 0x11 | 0xA2 | 17 | `MOV [t0] = [t1]` |
| 0x12 | 0x00 | 5521 | `LEA t0 = &$self[17]` |
| 0x13 | 0x00 | 450 | `PUSH t1` |
| 0x13 | 0x80 | 170 | `PUSH t0` |
| 0x14 | 0x00 | 424 | `POP t0` (A = C = 1) |
| 0x14 | 0x80 | 196 | `POP t0` |
| 0x18 | 0x00 | 502 | `RET t0` |
| 0x1A | 0x00 | 1317 | `JZ t0, L0004` |
| 0x1A | 0x02 | 179 | `JZ [t0], L0022` |
| 0x1B | 0x00 | 391 | `JMP L0001` |
| 0x1C | 0x00 | 3189 | `CALL t0 = rotatey` |
| 0x1D | 0x00 | 624 | `LCALL t0 = sleep` |
| 0x1E | 0x00 | 12 | `TMO v16` |
| 0x1E | 0x01 | 341 | `TMO #2.0` |
| 0x1E | 0x02 | 26 | `TMO [t0]` |

Operand shapes (VERIFIED-DATA): immediates in A occur only for MUL, ADD, SUB, NEG, TMO
(float constants); immediates in B for arithmetic, comparisons and MOV. Raw immediate
values 1..0x7FFFFF (string offsets) occur only as B of `MOV` mode 0x10 and all land on STRG
string starts. Slot operands are always below the frame size, global operands always
inside DEFS.

## Entity fields

LEA through `self` (and `other`, `player`) addresses 4-byte fields of the entity, counted
from entity + 0x7B. Field indices used through `self` in the corpus: 1, 4, 5, 8, 9, 14,
17, 20, 23, 24, 25, 28, 32, 33, 34, 35, 37. Named so far:

| Field | Meaning | Evidence |
|---|---|---|
| 5..7 | origin x, y, z | VERIFIED-CODE: `move`/`movex`/`movey`/`movez` 0x41af60..0x41b009 |
| 14..16 | angles x, y, z (degrees) | VERIFIED-CODE: `rotate`/`rotatex`/`rotatey`/`rotatez` 0x41b010..0x41b0b9 |
| 34 | health | VERIFIED-CODE: damage routine 0x404ae0 subtracts from entity+0x103 |

GUESS, to be checked in the entity spec: 17..19 velocity (set from `crandom` in the
barrel debris script and integrated with `move`), 35 damage dealt on touch (passed to
`Damage` in touch handlers), 37 a start delay (the tank's main loop sleeps for it before
following waypoints), 23 and 24 health-related values set in `init`.

## Open questions (prioritised)

1. Builtin argument lists and return values (72 used, 85 registered). Needed before any
   script runs; only a handful were read here (`random`, `crandom`, `sin`..`atan`, `abs`,
   `min`, `max`, `lerp`, `move*`, `rotate*`, `sleep`, `TerrainHeight`, `create` lookup,
   `StartSound` lookup).
2. The entity field map behind LEA indices (all 17 used indices through `self`, plus
   those used through `player`, `other`, `camera` and through pointers returned by
   builtins).
3. Exact trigger conditions of `init` and `touch` (who calls 0x404fa0, 0x40544b,
   0x405562 and when), and the order of events within an update.
4. The meaning of the two addresses per DEFS record and the selector `[[self]+0x77]`.
5. Header field 1 (always 16): unused by the loader; possibly read elsewhere.
6. What happens when a nested subroutine (CALL with A ≥ 0) hits a pending LCALL, and
   whether any shipped script does that.
7. Whether PUSH/POP stack contents persist across updates in practice (the stack is not
   reset by the dispatcher).

## Changelog

- 0.1 (WP-16): first version; all 31 handlers read from the executable, corpus checked.
