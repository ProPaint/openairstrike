# RCSL compiled script (`scripts\*.scr`)

Spec version 0.2. Reference implementation: `tools/rcsl_disasm.py`, test `tools/ref/test_rcsl.py`.
Companion: [rcsl-opcodes-v0.md](rcsl-opcodes-v0.md) (instruction set, execution model).

Claims tagged VERIFIED-DATA hold for all 339 `.scr` files of v1.70 (23,675 instructions).
Claims tagged VERIFIED-CODE were read from `AirStrike3D.exe` v1.70 (image base 0x400000);
addresses are virtual addresses.

All integers are little-endian. Strings are cp1251 (in practice ASCII).

## Overview

| Part | Content |
|---|---|
| header | 0x38 bytes, 14 × u32 |
| sections | repeated `{char tag[4]; u32 length; u8 payload[length]}` until end of file |

VERIFIED-DATA: the sections tile the file exactly from 0x38 to end of file, each tag
appears at most once, and only the five sequences below occur.

| Count | Section order |
|---|---|
| 171 | DEFS FUNC DATA STRG CODE |
| 78 | CASH DEFS FUNC DATA STRG CODE |
| 63 | DEFS FUNC DATA CODE |
| 19 | FUNC DATA CODE |
| 8 | DEFS DATA CODE |

CASH, DEFS, FUNC and STRG are present exactly when their count (size for STRG) is
non-zero; DATA and CODE are always present, DATA possibly with length 0 (VERIFIED-DATA).

## Loader behaviour (VERIFIED-CODE, loader at 0x41c9f4..0x41cdb3)

- Reads the 0x38-byte header into the script object and checks only the magic
  (`cmp [edi], 0x4C534352` at 0x41ca69). Field 1 is not validated.
- Then loops until end of file: reads tag and length, dispatches on the tag.
  Unknown tags are skipped by `length` (0x41cc88). Section order does not matter.
- Known sections are read by **header counts, not by the section length**: e.g. CODE
  reads `count × 14` bytes. In all shipped files length and counts agree (VERIFIED-DATA),
  so a reimplementation may validate either; our reference parser checks both.
- The script object is 0x90 bytes; the header lives at +0x40, table pointers at
  +0x78 CASH, +0x7C DEFS, +0x80 FUNC, +0x84 DATA, +0x88 STRG, +0x8C CODE.
  Loaded scripts are appended to a global array at 0x1fa7e08 (count at 0x1fa7e04).

## Header

| Offset | Field | Meaning | Tag |
|---|---|---|---|
| 0x00 | 0 | magic `RCSL` (0x4C534352) | VERIFIED-CODE |
| 0x04 | 1 | always 16. Not read by the loader nor anywhere else in the executable (see rcsl-vm.md, "Header field 1") | VERIFIED-DATA (value), VERIFIED-CODE (unused) |
| 0x08 | 2 | CASH entry count | VERIFIED-CODE 0x41cd03 |
| 0x0C | 3 | DEFS entry count | VERIFIED-CODE 0x41cc98 |
| 0x10 | 4 | FUNC entry count | VERIFIED-CODE 0x41cb8c |
| 0x14 | 5 | DATA entry count (8 bytes each) | VERIFIED-CODE 0x41cbfd |
| 0x18 | 6 | frame size in 4-byte slots (≥ 16) | VERIFIED-CODE 0x41c827 |
| 0x1C | 7 | STRG size in bytes | VERIFIED-CODE 0x41cc54 |
| 0x20 | 8 | instruction count | VERIFIED-CODE 0x41cb00 |
| 0x24 | 9 | entry point `init` | see below |
| 0x28 | 10 | entry point `main` | see below |
| 0x2C | 11 | entry point `damage` | see below |
| 0x30 | 12 | entry point `touch` | see below |
| 0x34 | 13 | entry point `callback` | see below |

Entry points hold an instruction index, or 0xFFFFFFFF when absent (VERIFIED-DATA: every
present value is below the instruction count).

Field 6 (frame size) is 16 in scripts without script variables and 16 + (variables and
vector storage) otherwise; range 16..81 in the corpus. Every non-negative slot operand in
the code is below it (VERIFIED-DATA).

## Section payloads

### CASH: precache list

`count` entries, each:

| Size | Field |
|---|---|
| u8 | kind |
| u8 | n = name length including the NUL |
| n | name, NUL-terminated |

VERIFIED-CODE (0x41cd03): the loader stores each entry in a 0x48-byte record, kind as u32
at +0, name at +4 (no length check in the original; the longest shipped name has 20
characters). VERIFIED-DATA: n ≥ 1, the last byte is NUL and no other byte is.

| Kind | Meaning | Count | Evidence |
|---|---|---|---|
| 0 | object definition name (e.g. `tank_dead`) | 230 | VERIFIED-CODE: `create` (0x41a7a0) searches CASH entries of kind 0 for its name argument |
| 3 | sound file (e.g. `sounds/expl1.wav`) | 16 | VERIFIED-CODE: `StartSound` helper 0x41bbc0 searches kind 3 |

No other kinds occur. What the engine does with the list at load time (precaching) is not
analysed here (GUESS from the name).

### DEFS: globals used

`count` entries of `{u8 n; char name[n]}` with n including the NUL (same rules as CASH,
no kind byte). VERIFIED-CODE 0x41cc98: each name is resolved with 0x41c6e0 against the
engine's global-variable table at 0x457220 (records of 12 bytes: name pointer and two
variable addresses); an unknown name resolves to NULL silently.

Operands refer to DEFS entry `i` as the negative number `-(i+1)`.

The engine table has 24 names: `self other cb_msg cb_parm1 cb_parm2 player p_action
p_scores p_lives p_stars p_speedfactor p_counter1 p_counter2 p_counter3 p_weapon l_night
l_water l_waterlevel frametime time camera g_map_pos g_damage_factor g_health_factor`.
18 of them are used by the shipped scripts (VERIFIED-DATA): self 318 scripts, player 117,
frametime 87, other 76, camera 59, cb_msg 34, g_map_pos 33, l_waterlevel 20,
p_speedfactor 5, p_counter2 5, p_action 4, p_lives 4, p_weapon 3, cb_parm1 3,
p_counter1 3, p_scores 2, l_water 1, p_stars 1.

The two addresses per record differ only for `player` and the `p_*` globals (two player
records, stride 0x171). VERIFIED-CODE 0x419bf4/0x419c59: the interpreter picks address 0
or 1 with the player index stored at entity + 0x77 of the current `self` entity (reached
as `[[self]+0x77]`, since the 4 bytes at `self` hold the entity address). Correction in 0.2:
0.1 called this a field of the entity's definition. The meaning (the entity's associated
player in two-player mode) is in rcsl-vm.md, "Globals".

### FUNC: builtins used

Same encoding as DEFS. VERIFIED-CODE 0x41cb8c: each name is resolved with 0x41c680 against
the builtin table at 0x456f70 (records of 8 bytes: name pointer, function pointer,
terminated by a NULL name). An unknown name logs
"SL_GetExternFunc: Built-in function '%s' not supported." and resolves to NULL.
Call instructions refer to FUNC entry `i` as `-(i+1)`.

The engine registers 85 builtins; 72 distinct names are used by the shipped scripts
(VERIFIED-DATA). The `l`-prefixed names (`lRotateTo`, `lRotateToClamp`, `lMoveToNextWP`,
`lRotateToNextWP`) are registered with the same function pointer as their plain versions
(VERIFIED-CODE, table at 0x4570f8..0x457130); the difference is only how they are called
(latent call, opcode 0x1D). The full list with addresses belongs to the builtins spec
(future package); this package only needs the names.

### DATA: slot initialisers

`count` entries of 8 bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | u16 | kind |
| 2 | s16 | slot index |
| 4 | u32 | value |

VERIFIED-CODE (thread creation 0x41c7d0, loop at 0x41c862): after the frame is allocated and
zeroed, each entry is applied in order:

| Kind | Effect | Count |
|---|---|---|
| 2 | `frame[slot] = value` (raw 32 bits, normally a float; may be a STRG offset) | 73 |
| 3 | `frame[slot] = address of frame[value]` | 166 |
| other | ignored | 0 |

VERIFIED-DATA: every slot is in 16..frame size−1; every kind-3 entry has
`value = slot + 1` (a pointer slot followed by its storage: this is how vector variables
are declared; GUESS: the storage is 3 slots, the gap to the next kind-3 slot is 4 in 102
of 166 cases and larger otherwise); kind-2 values that have the
shape of a string offset land on STRG string starts.

### STRG: string literals

`size` bytes (header field 7): NUL-terminated strings concatenated, no padding, the last
byte is NUL (VERIFIED-DATA). Code refers to a string by its **byte offset** from the start
of STRG, carried as the raw 32-bit pattern of an immediate (VERIFIED-CODE: `create`
computes `STRG + (int)arg` at 0x41a7da; the thread keeps the STRG pointer at +0x814).
The same text can appear several times (the compiler does not merge literals).

### CODE: instructions

`count × 14` bytes (VERIFIED-DATA for all scripts; VERIFIED-CODE: the interpreter advances
by 0xE at 0x419f37). One instruction:

| Offset | Size | Field |
|---|---|---|
| 0 | u8 | opcode |
| 1 | u8 | mode flags |
| 2 | i32 | operand 1 (A) |
| 6 | i32 | operand 2 (B) |
| 10 | i32 | operand 3 (C) |

## Operand encoding (VERIFIED-CODE, decoder at 0x419c18..0x419ce9)

Before dispatching, the interpreter turns each operand into an address:

| Operand | Immediate bit | Otherwise | Indirection bit |
|---|---|---|---|
| A (op1) | 0x01: A is the address of a copy of the 4 raw bytes | v < 0: address of global DEFS[−v−1]; v ≥ 0: address of frame slot v | 0x02: address = the pointer stored at that address |
| B (op2) | 0x10: same, for B | same | 0x20 |
| C (op3) | never immediate | same | 0x80 |

Exceptions: for opcodes 0x15, 0x1B, 0x1C, 0x1D a negative A is not treated as a global
(the handler reads A raw); for opcodes 0x19, 0x1A a negative B is not treated as a global.
Opcode 0x12 reads B raw. Details per opcode are in the opcodes spec.

Mode bits 0x04, 0x08 and 0x40 are never set in the corpus and not tested by the decoder
(VERIFIED-DATA, VERIFIED-CODE). The mode bytes seen are 00 01 02 10 12 20 21 22 80 82 92 A2.

All values are 32-bit IEEE floats. There is no integer type: immediates are read with
`fld` and stored back with `fstp`, which copies the bit pattern (so STRG offsets, which are
tiny denormals when viewed as floats, survive). Integer operations convert with truncation
(`_ftol2`/`cvttsd2si` at 0x440870). Pointers (entity pointers, slot addresses, results of
LEA) are stored in slots as raw 32-bit addresses.

**Frame slots.** Each script instance (thread, 0x824 bytes, created at 0x41c7d0) owns a
frame of `header[6]` slots. Slots 0..15 are temporaries: builtin arguments are read from
slot 0 upwards, results are written where the call instruction says, and they are saved
and restored around event handlers. Slots 16 and up are script variables that persist
for the life of the instance. The disassembler names them `t0..t15` and `v16..`.

**Globals.** A negative operand −(i+1) is the address of the engine variable named by DEFS
entry i. `self` holds a pointer into the current entity (entity + 0x7B, VERIFIED-CODE
0x404b6e), so field access is done with LEA (opcode 0x12) through that pointer. `other`,
`player` hold entity references of the same form; `camera` holds the address of the
camera structure, not an entity (rcsl-vm.md, "Entity references and fields");
`frametime`, `cb_msg`, ... hold floats.

**String literals** appear as MOV immediates (opcode 0x11, mode 0x10, operand B) whose raw
value is a STRG offset (1,619 cases, every one landing on a string start, VERIFIED-DATA).
Offset 0 is indistinguishable from 0.0 in the encoding; the disassembler prints both
readings. Immediate raw values between 1 and 0x7FFFFF occur nowhere else.

## Entry points and events

The five entry-point fields select what the engine runs for each event. VERIFIED-CODE for
who runs them:

| Slot | Field | Name here | Run by | Scripts |
|---|---|---|---|---|
| 0 | 9 | `init` | 0x404fa0 (runs it for an entity, then recursively for its attached children; called from entity creation paths 0x4077a4, 0x40b3f1), and projectile setup in `Shoot` (0x41b273, 0x41b315, 0x41b513), 0x40bc7e | 219 |
| 1 | 10 | `main` | the per-instance thread: the pc is initialised to it at thread creation (0x41c8c1) and it is resumed each update (0x41a200 with kind 0, called from 0x405abf) | 324 |
| 2 | 11 | `damage` | 0x404ae0 after subtracting the damage from the health field (entity +0x103, script field 34); called by builtin `Damage` (0x41b592) and two other sites (0x404d31, 0x404e12) | 78 |
| 3 | 12 | `touch` | 0x40544b and 0x405562, which set global `other` just before | 117 |
| 4 | 13 | `callback` | builtins `AttachCallback` (0x41ad59), `ParentCallback` (0x41ae40), `callback` (0x41af1e), which set `cb_msg`, `cb_parm1`, `cb_parm2` just before | 36 |

The names `init` and `touch` are ours; the exact trigger conditions are now specified in
rcsl-vm.md, "Event dispatch" (VERIFIED-CODE): `init` runs when an object is spawned or
created, `touch` from the collision pass. `damage` and `callback` follow directly from the
code. Data consistency (VERIFIED-DATA): `other` is used by 76 scripts, `cb_msg` by 34;
the tank's `init` sets fields and deactivates an attachment, its `damage` handler tests
health (field 34) and on ≤ 0 spawns explosions and removes itself.

How the handlers run (VERIFIED-CODE, 0x41a200 and the inlined copies at 0x404b5d,
0x40500a, 0x4053bf, 0x4054dd, 0x40bc5a, 0x41acc6, ...):

- Event handlers (slots 0, 2, 3, 4) run synchronously to completion: the dispatcher saves
  the thread's pc and frame slots 0..15, sets pc to the entry point, runs the interpreter
  until it returns, then restores pc and slots 0..15. Script variables (slots ≥ 16)
  changed by the handler keep their new values. The PUSH/POP stack is not saved.
- `main` resumes where it last stopped. When the interpreter returns and the current
  instruction is END (0x00) or RET (0x18), the pc is reset to the `main` entry, so `main`
  is a loop that restarts on the next update.
- A handler whose entry is 0xFFFFFFFF is not run.

Populated combinations (init, main, damage, touch, callback): main only 56; init+main 80;
init+main+touch 50; main+touch 39; init+main+damage 32; init+main+damage+touch 28;
init+main+callback 26; damage only 11; main+callback 9; main+damage 4; init+damage 2;
damage+callback 1; init only 1 (VERIFIED-DATA).

## Reference parser checks

`tools/ref/test_rcsl.py` verifies, for all 339 scripts: magic and header field 1; that
the sections tile the file; that the counts in the header match the decoded tables; the
name encodings; DATA length = 8 × count; STRG size; CODE length = 14 × count; entry points;
byte-identical re-serialisation; that every opcode and (opcode, mode) pair is in the
opcodes spec; and that every jump target, call target, DEFS index, FUNC index, slot index,
STRG reference and DATA slot is within bounds. It also compares
`testdata/golden/as3d/rcsl_summary.json`.

## Changelog

- 0.1 (WP-16): first version, container fully decoded, loader read from the executable.
- 0.2 (WP-21/22): header field 1 shown unused; the player-index selector is entity + 0x77
  (not the definition); `camera` is not an entity; entry-point triggers point to
  rcsl-vm.md.
