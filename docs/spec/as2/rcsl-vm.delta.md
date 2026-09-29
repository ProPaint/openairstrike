# RCSL virtual machine: AirStrike 2 delta (`as2`)

Delta version 1.0. Amends [../rcsl-vm.md](../rcsl-vm.md) 1.1 for the game `as2` (AirStrike 2
v2.51, `AirStrike3D II.exe`). Companions: [rcsl-container.delta.md](rcsl-container.delta.md),
[rcsl-opcodes.delta.md](rcsl-opcodes.delta.md),
[rcsl-builtins-table.delta.md](rcsl-builtins-table.delta.md). Symbols of the functions named
here: `re/symbols_as2_vm.csv`.

The base spec has no section numbers; this delta mirrors its headings. VERIFIED-CODE cites
`as2@` and, for a comparison, `v170@` addresses. VERIFIED-DATA holds for the 631 files of
`assets_extracted_games/as2/scripts`.

Method: each VM function and each dispatcher of AS2 was paired with its v1.70 counterpart and
the two disassemblies compared instruction by instruction, with absolute addresses masked and
(where said) with the v1.70 structure offsets replaced by their AS2 values. Pairs: interpreter
as2@0x41eb10 / v170@0x419be0, event runner as2@0x41f130 / v170@0x41a200, thread creation
as2@0x421db0 / v170@0x41c7d0, init dispatcher as2@0x40bee0 / v170@0x404fa0, damage routine
as2@0x40b9e0 / v170@0x404ae0, touch dispatcher as2@0x40c120 / v170@0x4051d0, entity update
as2@0x40cb30 / v170@0x405a60, callback builtins as2@0x41ff40, 0x41fcd0, 0x41fe60 /
v170@0x41ae80, 0x41ac10, 0x41ada0, `create` as2@0x41f7d0 / v170@0x41a7a0.

## Summary for implementers

The VM is the v1.70 VM. What differs for `as2`:

- the 28-entry global table (4 new names, new indices, a new player-record layout);
- the entity structure grew, but every script-visible field index keeps its meaning (the
  engine moved the reference point from entity + 0x7B to entity + 0x84 together with the
  fields); two new engine-internal fields sit at indices 88 and 89 and the children list moved
  from 88/89 to 90/91;
- `create` no longer lets the new entity's `init` overwrite the value it returns (quirk 9 is
  gone);
- the touch pass reads the collision mode as a bit set and skips dead entities.

## Per-frame update

Not re-checked as a whole (the frame function and the spawner belong to the engine-behaviour
delta). Checked parts:

- **Entity update** as2@0x40cb30 (v170@0x405a60): same structure. Skipped when removed; if
  not paused, field 1 (age) and the internal time since the last damage (entity + 0x74,
  v170 + 0x6F) += frametime; **new:** an internal timer at entity + 0x78 is increased by
  frametime while it is below 5.0 (as2@0x40cb82..0x40cb9b, inserted after what is
  v170@0x405aab; `Lightning` resets it, see the builtins delta); then `main` is dispatched under the same conditions as in v1.70 (active bit
  +0x20 & 0x4, visibility state +0x1C ≠ 1, parent allows scripts, script has a `main`).
  VERIFIED-CODE. Placement of attached children changed in details (as2@0x40cc36) that do not
  concern the VM.
- **`create`** as2@0x41f7d0 still builds the entity and its thread, runs its `init` (through
  as2@0x40bee0) and one entity update with its `main` (as2@0x41f9a0) synchronously inside the
  caller, and propagates the caller's player index (entity + 0x80, as2@0x41f8dc). **Changed:**
  the new reference is kept in a local and written to the return register only after the
  nested `init` and update have finished (as2@0x41f9a5..0x41f9ac); v1.70 wrote it before
  (v170@0x41a888). The failure path still returns 0.0 (as2@0x41f893). VERIFIED-CODE.

## Event dispatch

Same mechanism, VERIFIED-CODE: every dispatcher saves current thread (as2@0x2106310), `self`
(as2@0x2106320) and argument pointer (as2@0x2106324), sets them to the target's thread
(entity + 0x5C, v170 + 0x57), entity + 0x84 (v170 + 0x7B) and its frame; for handlers other
than `main` it saves the pc and t0..t15 (16 dwords copied), sets pc = entry, runs the
interpreter, restores pc and t0..t15, then restores the three globals. An entry of 0xFFFFFFFF
is not run. The event runner as2@0x41f130 is the same code as v170@0x41a200 apart from those
two offsets and the addresses.

| Entry | Status | as2 details |
|---|---|---|
| `init` (0) | same | as2@0x40bee0 (same code as v170@0x404fa0 apart from offsets): sets the time since damage to 2.0, runs `init`, recurses into attached children (children list at entity + 0x1EC/0x1F0, v170 + 0x1DB/0x1DF) after copying the parent's player index (+0x80). Callers: as2@0x40e7e0 (GUESS: the map spawner, v170@0x407590), player spawn as2@0x413b20, `create` as2@0x41f7d0; the kill reward as2@0x414350 and `Shoot` as2@0x420190 run `init` of what they spawn. |
| `main` (1) | same | see "Per-frame update" |
| `damage` (2) | same | damage routine as2@0x40b9e0: same skip conditions (field 4 ≠ 0; frozen flag entity + 0x20 & 0x10; invulnerable player: the freeze counter at player record + 0xB8 ≠ 0), same order (time since damage = 0, field 34 −= amount, handler, then dead flag, drop and score when health ≤ 0). `other` is not set. The only other change is in the kill accounting (a per-player counter is no longer increased past a level value, as2@0x40bb46), which is not VM behaviour. Called by `Damage`, `RadialDamage`, `RadialDamagePlayer`, `TraceLineDamage`, `Lightning` and the projectile pass. |
| `touch` (3) | **changed** | as2@0x40c120, see below |
| `callback` (4) | same | `callback` as2@0x41ff40, `AttachCallback` as2@0x41fcd0, `ParentCallback` as2@0x41fe60: same code as v1.70 apart from offsets; they set `cb_msg` (as2@0x210631c), `cb_parm1` (as2@0x2106318), `cb_parm2` (as2@0x2106328) from the caller's t1..t3 and do not restore them. |

**Touch dispatch, changed** (VERIFIED-CODE as2@0x40c120 against v170@0x4051d0). The dispatch
itself is unchanged: `other` = the toucher's reference, handler run with the save/restore
above, `other` restored afterwards (as2@0x40c21b..0x40c2ec); a player touch also sets the
entity's player index (+0x80) and ends the entity's pass. What changed is which pairs are
tested. The collision mode (entity + 0x60, v170 + 0x5B, from the object definition) is now read
as a bit set:

| Test | v1.70 | as2 |
|---|---|---|
| test the live players | mode 2 or 3 | bit 0x2 set (as2@0x40c140) |
| test other entities at all | mode ≠ 2 | mode ≠ 2 (as2@0x40c1fa) |
| class filter on the other entity | modes 1 and 3: class (field 2) = 2.0 only; any other mode: any class | accepted when (bit 0x1 and class = 2.0) or (bit 0x4 and class = 5.0); otherwise skipped (as2@0x40c314..0x40c346) |
| other entity's dead flag (field 4) | not tested | must be 0 (as2@0x40c34c) |
| other entity in view, not removed, not frozen | tested | tested (as2@0x40c35d..0x40c36a) |

For modes 1, 2 and 3 the only difference is the new dead-flag test. Which modes the `as2`
object definitions use is not checked here (object-definition delta).

## Globals

Table at as2@0x49d908, 28 records of 12 bytes `{name, address for player index 0, address for
player index 1}`, terminated by a NULL name (VERIFIED-CODE: the table read from the executable;
lookup as2@0x421c90). The interpreter picks the address with the player index at entity +
**0x80** of the current `self` (as2@0x41eb24; v1.70: + 0x77 at v170@0x419bf4). Scripts resolve globals by
name, so the new order does not affect them; it matters only for tools that number globals
(such as the reference VM's mock address space).

Player records: base as2@0x20c5ad0 (v170@0x1ebe308), stride **0x164** (v1.70: 0x171; e.g. the
damage routine's player loop as2@0x40ba36 against v170@0x404b36); +0 is the
player's raw entity pointer. The record fields the globals point at moved: `p_maxHealth` was
inserted after `p_action`, and every later field is 4 bytes further than in v1.70.

The VM has no read-only globals: any global can be written by a MOV. The "Engine writes"
column lists the engine code that overwrites the variable (so a script's write does not last);
the other variables keep what scripts write. Counts are `as2` scripts declaring the name
(VERIFIED-DATA).

| # | v170 # | Name | Type | Scripts | Address (player index 0 / 1) | Engine writes, meaning | Status |
|---|---|---|---|---|---|---|---|
| 0 | 0 | `self` | entity reference | 594 | 0x2106320 | every dispatch | same |
| 1 | 1 | `other` | entity reference | 111 | 0x2106314 | touch dispatch as2@0x40c228, restored after | same |
| 2 | 2 | `cb_msg` | float | 72 | 0x210631c | callback builtins (as2@0x41ff58, 0x41fe7e, 0x41fd86) | same |
| 3 | 3 | `cb_parm1` | float | 8 | 0x2106318 | callback builtins | same |
| 4 | 4 | `cb_parm2` | float | 0 | 0x2106328 | callback builtins | same |
| 5 | 5 | `player` | entity reference | 231 | 0x2106308 / 0x210630c | the player entity of self's index; written at player spawn as2@0x413beb (entity + 0x84) | same meaning |
| 6 | – | `player1` | entity reference | 0 | 0x2106308 / 0x2106308 | **new**: player 1's entity whatever self's index | new |
| 7 | – | `player2` | entity reference | 0 | 0x210630c / 0x2106308 | **new**: the *other* player's entity: player 2 for an entity of index 0, player 1 for an entity of index 1 (0.0 while that player has no entity) | new |
| 8 | 6 | `p_action` | float (bit mask) | 6 | 0x20c5b5c / 0x20c5cc0 (record + 0x8C) | input/action bits; cleared at level start (as2@0x410d43) | same field |
| 9 | – | `p_maxHealth` | float | 0 | 0x20c5b60 / 0x20c5cc4 (record + 0x90) | **new**. No instruction contains either address (a byte scan of the whole executable finds them only in the global table), and no access through a player-record pointer + 0x90 was found. Meaning from the name only: GUESS, a script-owned maximum health | new |
| 10 | 7 | `p_scores` | float | 2 | 0x20c5b64 / 0x20c5cc8 (record + 0x94, v170 + 0x90) | score; 0 at level start | moved in record |
| 11 | 8 | `p_lives` | float | 7 | 0x20c5b6c / 0x20c5cd0 (+ 0x9C, v170 + 0x98) | lives; set at level start from record + 0x98 | moved in record |
| 12 | 9 | `p_stars` | float | 1 | 0x20c5b70 / 0x20c5cd4 (+ 0xA0, v170 + 0x9C) | 0 at level start | moved in record |
| 13 | 10 | `p_speedfactor` | float | 8 | 0x20c5b80 / 0x20c5ce4 (+ 0xB0, v170 + 0xAC) | meaning GUESS as in the base | moved in record |
| 14 | 11 | `p_counter1` | float | 6 | 0x20c5b74 / 0x20c5cd8 (+ 0xA4, v170 + 0xA0) | script-owned | moved in record |
| 15 | 12 | `p_counter2` | float | 8 | 0x20c5b78 / 0x20c5cdc (+ 0xA8, v170 + 0xA4) | script-owned | moved in record |
| 16 | 13 | `p_counter3` | float | 0 | 0x20c5b7c / 0x20c5ce0 (+ 0xAC, v170 + 0xA8) | 1.0 at level start (as2@0x410d56) | moved in record |
| 17 | 14 | `p_weapon` | float | 6 | 0x20c5b84 / 0x20c5ce8 (+ 0xB4, v170 + 0xB0) | weapon index | moved in record |
| 18 | 15 | `l_night` | float 0/1 | 0 | 0x4c207c | level load as2@0x40e52f | same |
| 19 | 16 | `l_water` | float 0/1 | 2 | 0x54327c | level load as2@0x40e548 | same |
| 20 | 17 | `l_waterlevel` | float | 53 | 0x543284 | level load as2@0x40e554 | same |
| 21 | 18 | `frametime` | float, seconds | 216 | 0x49f920 | once per frame, as2@0x405ea4 (the timing formula belongs to the engine-behaviour delta) | same role |
| 22 | 19 | `time` | float, seconds | 0 | 0x49f924 | += frametime per frame (as2@0x405eb8) | same role |
| 23 | 20 | `camera` | pointer (not an entity) | 96 | 0x49d900 | constant 0x20df0a8 (initialised data), the camera structure | same |
| 24 | – | `cameramode` | float | 1 | 0x49d904 | **new**. Initial value 1.0 (initialised data). No engine code refers to the address (only the two global-table records do); the engine's own camera mode is another variable (as2@0x49db68, an integer). The only script use is `eol.scr` writing 0.0 at the end of a level, which therefore has no effect in `as2` | new |
| 25 | 21 | `g_map_pos` | float | 57 | 0x54329c | camera update as2@0x415034 (+= camera[7] × frametime) | same |
| 26 | 22 | `g_damage_factor` | float | 0 | 0x49ded8 | level start as2@0x410cff from the difficulty table | same values |
| 27 | 23 | `g_health_factor` | float | 0 | 0x49dedc | level start as2@0x410d11 | same values |

Difficulty table at as2@0x49dee8 (rows of 4 floats, first two are health and damage factor):
health 0.3, 0.5, 0.75, 1.5, 2.0 and damage 0.5, 0.7, 0.8, 1.25, 1.4, as in v1.70; difficulty
index at as2@0x49ded4 (default 2, clamped to 4), number of players at as2@0x49ded0 (default 1).
VERIFIED-CODE (as2@0x410cd0) and the initialised data.

## Entity references and fields

**Representation, changed offsets, same contract** (VERIFIED-CODE). An entity is **0x1F4**
bytes (allocations `PUSH 0x1f4` in the object builder as2@0x411ed3; v1.70: 0x1E3). A
reference is entity + **0x84** (v1.70: + 0x7B): every dispatcher sets `self` to it
(as2@0x41f167, 0x40ba6d, 0x40bf17), `create` returns it (as2@0x41f8a6), the player globals
hold it (as2@0x413be5), and field 0 (the 4 bytes at the reference) holds the entity's own
address (object builder as2@0x411f13). Builtins still dereference the reference once to get
the raw entity. Engine-internal offsets before the reference moved too (thread + 0x5C, player
index + 0x80, flags + 0x20, collision mode + 0x60), which scripts cannot see.

**Field access**: same (`LEA t = &base[k]` is `base + 4k`). Fields 0..91 lie inside the
entity (v1.70: 0..89).

**Field map.** Method: in 347 function pairs matched through the builtin table and the call
graph, every structure displacement of the aligned v1.70 instructions was compared with the
AS2 one. For every field offset of v1.70 from entity + 0x7B to + 0x1D7 that the engine uses,
the AS2 displacement is exactly 9 larger, i.e. the same distance from the reference; so field
k keeps its index for k = 0..87. The children count and array (v1.70 fields 88 and 89, entity
+ 0x1DB/0x1DF) moved to entity + 0x1EC/0x1F0 (fields 90 and 91; 21 and 17 aligned uses), and
two new engine-internal dwords occupy entity + 0x1E4 and + 0x1E8 (fields 88 and 89). Script
use (VERIFIED-DATA, LEA through a global): through `self` the indices 1, 4, 5, 8, 14, 17,
20, 23, 24, 25, 28, 32, 33, 34, 35, 37 (the same set as v1.70); through `player` 4, 5, 14,
17, 34 (17 is new); through `camera` 0, 6, 9 (0 is new). No script uses an index above 37.

Every row of the base table, with the aligned-use count that confirms it ("uses" = aligned
instruction pairs with v1.70 offset → AS2 offset; one example pair):

| k | v1.70 offset | as2 offset | Name (base) | Evidence (uses; example v170 → as2) | Status |
|---|---|---|---|---|---|
| 0 | 0x7B | 0x84 | self_ptr | 12; v170@0x41a237 → as2@0x41f167 | same |
| 1 | 0x7F | 0x88 | age | 2; v170@0x405a9c → as2@0x40cb6a | same |
| 2 | 0x83 | 0x8C | class | 11; v170@0x405bf1 → as2@0x40cce7 | same |
| 3 | 0x87 | 0x90 | flags | 16; v170@0x405d66 → as2@0x40ce8b | same |
| 4 | 0x8B | 0x94 | dead | 8; v170@0x405c0c → as2@0x40cd02 | same |
| 5–7 | 0x8F | 0x98 | origin | 27/18/18; v170@0x405d2c → as2@0x40ce51 | same |
| 8–10 | 0x9B | 0xA4 | attach_offset | 2/1/1; v170@0x4048aa → as2@0x40b7af | same |
| 11–13 | 0xA7 | 0xB0 | prev_origin | 2/2/2; v170@0x40588e → as2@0x40c87b | same |
| 14–16 | 0xB3 | 0xBC | angles | 3/2/13; v170@0x405859 → as2@0x40c846; `DetachEntity` copies the parent's 14..16 (as2@0x41faf6) | same |
| 17–19 | 0xBF | 0xC8 | velocity | 1/1/1; v170@0x41b16a → as2@0x42023d | same |
| 20–22 | 0xCB | 0xD4 | field20 | no engine use in either executable | same (script-owned) |
| 23 | 0xD7 | 0xE0 | wp_speed | 1; v170@0x406002 → as2@0x40d152. **New initial value:** the object builder now stores a float from the object definition into field 23 at spawn (as2@0x411f51..0x411f57); the v1.70 builder (the same sequence at v170@0x409c4f..0x409c6a) does not initialise it. Belongs to the object-definition delta. | same index, changed initialisation |
| 24 | 0xDB | 0xE4 | wp_turn_rate | 1; v170@0x4061d4 → as2@0x40d324; `RotateTo` reads self + 0x60 (as2@0x4209ff) | same |
| 25 | 0xDF | 0xE8 | wp_bank | 2; v170@0x4060e2 → as2@0x40d232 | same |
| 26–27 | 0xE3 | 0xEC | (not in the base table) | no engine use found | not listed in base |
| 28–31 | 0xEB | 0xF4 | color | 2 each; v170@0x4058ac → as2@0x40c899; initial 1.0 (as2@0x411f75..0x411f89) | same |
| 32 | 0xFB | 0x104 | scale | 1; v170@0x4058e8 → as2@0x40c998 | same |
| 33 | 0xFF | 0x108 | frame | 2; v170@0x4058f4 → as2@0x40c9a4 | same |
| 34 | 0x103 | 0x10C | health | 9; v170@0x404ef1 → as2@0x40be2f; damage routine as2@0x40ba46 | same |
| 35 | 0x107 | 0x110 | damage | 5; v170@0x409c6a → as2@0x411f6f; `Lightning` reads self + 0x8C (as2@0x421429) | same |
| 36 | 0x10B | 0x114 | score | 1; v170@0x404c7f → as2@0x40bb8d | same |
| 37 | 0x10F | 0x118 | wp_wait | 3; v170@0x40605b → as2@0x40d1ab | same |
| 38 | 0x113 | 0x11C | render_type | 13; v170@0x405b80 → as2@0x40cc76 | same |
| 44–52 | 0x12B | 0x134 | axis | 6 (k = 44), 2 each for 45..52; v170@0x405b4e → as2@0x40cc44; `RotateTo` reads entity + 0x134 (as2@0x420a02) | same |
| 88 | 0x1DB | 0x1E4 | (children count in v1.70) | v1.70's children count moved to 0x1EC (21 uses, e.g. v170@0x40506b → as2@0x40bfae in the init dispatcher); 0x1E4 is a new engine-internal dword | changed, engine-internal |
| 89 | 0x1DF | 0x1E8 | (children array in v1.70) | moved to 0x1F0 (17 uses, e.g. v170@0x405073 → as2@0x40bfc0); 0x1E8 is a new engine-internal dword | changed, engine-internal |
| 90–91 | – | 0x1EC, 0x1F0 | – | children count and array | new, engine-internal |

The engine-internal fields 39..43 and 53..87 not listed above also keep their offsets relative
to the reference (one to fourteen aligned uses each, except 66, 67, 71..73 and 78, which no
paired function touches). Scripts do not use them.

**Camera structure**: same layout (VERIFIED-CODE, camera update as2@0x414e90): [0..2] position
(as2@0x414f1a), [3..5] angles, [6] lateral velocity (as2@0x41503a), [7] scroll speed
recomputed as [9] × 42 every frame (as2@0x41500e..0x41501a, constant 42.0 at as2@0x48f430), [9]
scroll factor; `g_map_pos` += [7] × frametime (as2@0x415028..0x415034).

## Header field 1

Same: not read after loading (see rcsl-container.delta.md, "Header").

## Random numbers

Same: `random` and `crandom` are the same code as in v1.70 and call the C runtime `rand`
(as2@0x43adaa). VERIFIED-CODE.

## Thread state

Same. Thread creation as2@0x421db0 is the same code as v170@0x41c7d0 (0x824-byte thread, same
field offsets, frame zeroed then DATA applied, pc at `main`, empty stack, timeout 0). The
entity keeps its thread at entity + **0x5C** (v1.70: + 0x57). Engine globals shared by all
threads: current thread as2@0x2106310, `self` as2@0x2106320, argument pointer as2@0x2106324,
return register as2@0x210632c (one float, **shared** by all threads as in v1.70), latent-done
flag as2@0x2106330. Lifetime (freeing of removed entities with their threads) not re-checked.

## Interpreter invocation

Same (VERIFIED-CODE, interpreter code identical): stops at END, RET, completed or waiting
LCALL; budget of 10,000 instructions per invocation (as2@0x41eb39), exhaustion calls the fatal
routine as2@0x405750 with "Script stall detected." (as2@0x48c09c); stack of 512 with fatal
overflow/underflow through as2@0x41eac0. The fatal routine differs from v170@0x4204f0 only in
its shutdown calls.

## CALL of a script subroutine

Same code. Nested latent case: still unreachable in the data: no subroutine reachable from any
of the 1,806 script-subroutine call sites (878 CALL, 928 LCALL) contains an LCALL
(VERIFIED-DATA, static reachability).

## LCALL and TMO

Same code (as2@0x41efec..0x41f0ab). Data (VERIFIED-DATA): the `l`-named builtins are only
called with LCALL and their plain twins only with CALL; 197 of the 200 `LCALL sleep` directly
follow a TMO. The three others are a second `LCALL sleep` right after a completed `TMO; LCALL
sleep` (`tanks\m113\jeep.scr` 130, `helics\heli_chinook\jeep.scr` 111,
`transport\tyagach\tank.scr` 98). Completing the first wait resets the timeout to 0, so the
second `sleep` waits forever: the thread stays on it for the rest of the entity's life and its
`main` never restarts. An implementation reproduces this by following the base rules.

## Values and arithmetic

Same (handlers identical; float-to-int as2@0x47cbb0 is the same kind of truncating
conversion as v170@0x440870, VERIFIED-CODE for the call sites, the routine itself is the C
runtime's).

## Quirks an implementation must reproduce

| # | Status for `as2` |
|---|---|
| 1 POP writes B | same (1,338 of 1,338 POPs write t0) |
| 2 completed LCALL ends the invocation | same |
| 3 waiting LCALL in a handler ends it | same. The affected `as2` script is `bonuses\satellite_strike\satellite_strike.scr`: its `init` runs `TMO #0.3; LCALL sleep` at instruction 47 (the only LCALL reachable from a handler entry, VERIFIED-DATA); everything after it in `init` (a `TerraMorph`, a `CameraQuake`, more `create`s and further waits) never runs in the original, and the thread keeps a timeout of 0.3 − frametime |
| 4 timeout shared by main and handlers | same |
| 5 stale return register | same |
| 6 END does not advance the pc | same |
| 7 unused operands still decoded | same |
| 8 unknown DEFS names resolve to NULL | same (none occur) |
| 9 `create` returns what the new entity's `init` left | **changed: gone.** `create` writes the new reference to the return register after the nested `init` and update (as2@0x41f9ac; v170@0x41a888 wrote it before). The caller always receives the new entity (or 0.0 on failure) |
| 10 `other`, `cb_*` never cleared | same |
| 11 damage handler before the dead flag | same |
| 12 a player touch changes the player index | same (entity + 0x80) |
| 13 main restart decided by the opcode under the pc | same (event runner code identical) |

## Trace format

Not changed by this delta (a property of our reference VM). For `as2` traces the `B` line
uses the argument counts of `testdata/golden/as2/rcsl_builtins.json`.

## Mock host

Not changed by this delta. A mock host for `as2` needs: the 28-name global table with the
indices above (the mock address of a global is derived from its index); fields 0..91 per mock
entity; the `as2` builtin table (101 entries, `Lightning` with one argument).

## What an implementer must change

Against the VM as the base spec describes it:

1. Use the `as2` global table: 28 names in the order above. `player1` = player 1's entity
   reference; `player2` = the other player's reference (player 2 for an entity of player index
   0, player 1 for index 1); `p_maxHealth` = a new per-player float slot; `cameramode` = a
   global float with initial value 1.0 that nothing in the engine reads.
2. Keep the per-player globals per player as before; `p_maxHealth` is per player like the other
   `p_*`.
3. Keep every entity field index as in v1.70 (0..87 unchanged). Give entities 92 fields; fields
   88 and 89 are engine-internal and unused by scripts.
4. In `create`, return the new entity reference even when its `init` or first update executes
   RET or a value-returning builtin (drop quirk 9 for `as2`).
5. Touch pass: read the collision mode as a bit set (0x2 players; 0x1 enemies of class 2.0;
   0x4 entities of class 5.0; mode exactly 2 means players only) and skip other entities
   whose field 4 (dead) is non-zero.
6. Entity update: keep an internal per-entity timer that grows by frametime up to 5.0 (used by
   `Lightning`; semantics in the builtins semantics delta).
7. Field 23 gets its initial value from the object definition at spawn (object-definition
   delta).
8. Nothing else: interpreter, handlers, dispatch save/restore, LCALL/TMO, stall detector,
   shared return register and the remaining quirks are as in the base.

## Checked sections

| Base section | Status |
|---|---|
| Summary for implementers | changed (see Summary above) |
| Per-frame update | partly: entity update and `create` checked (changed as described); frame order, spawner, entity pass, projectile pass not checked |
| Event dispatch | changed (`touch` pair tests); `init`, `main`, `damage`, `callback` same |
| Globals | changed (28 names, new order, new player-record layout, 4 new globals) |
| Entity references and fields | changed offsets of the reference and engine-internal fields; script field indices 0..87 same; fields 88..91 changed (engine-internal); field 23 initialisation changed |
| Header field 1 | same |
| Random numbers | same |
| Thread state | same (entity keeps the thread at + 0x5C); lifetime not checked |
| Interpreter invocation | same |
| CALL of a script subroutine | same |
| LCALL and TMO | same |
| Values and arithmetic | same |
| Quirks an implementation must reproduce | changed (quirk 9 gone; quirk 3 affects another script) |
| Trace format | same (not engine behaviour) |
| Mock host | not checked (reference-tool contract; needs the `as2` tables) |

### Checked entity fields

One row per field index of the `as2` entity. "Aligned uses" counts the paired v1.70/as2
instructions that access the v1.70 offset and the as2 offset at the same place (method in
"Entity references and fields"). "Used by scripts" marks the first-level indices that `as2`
scripts address through a global (a vector's components are then reached by a second LEA).

| k | v1.70 offset | as2 offset | Name (base) | Aligned uses | Status |
|---|---|---|---|---|---|
| 0 | 0x7B | 0x84 | self_ptr | 12 | same |
| 1 | 0x7F | 0x88 | age | 2 | same; used by scripts |
| 2 | 0x83 | 0x8C | class | 11 | same |
| 3 | 0x87 | 0x90 | flags | 16 | same |
| 4 | 0x8B | 0x94 | dead | 8 | same; used by scripts |
| 5 | 0x8F | 0x98 | origin.x | 27 | same; used by scripts |
| 6 | 0x93 | 0x9C | origin.y | 18 | same |
| 7 | 0x97 | 0xA0 | origin.z | 18 | same |
| 8 | 0x9B | 0xA4 | attach_offset.x | 2 | same; used by scripts |
| 9 | 0x9F | 0xA8 | attach_offset.y | 1 | same |
| 10 | 0xA3 | 0xAC | attach_offset.z | 1 | same |
| 11 | 0xA7 | 0xB0 | prev_origin.x | 2 | same |
| 12 | 0xAB | 0xB4 | prev_origin.y | 2 | same |
| 13 | 0xAF | 0xB8 | prev_origin.z | 2 | same |
| 14 | 0xB3 | 0xBC | angles.x | 3 | same; used by scripts |
| 15 | 0xB7 | 0xC0 | angles.y | 2 | same |
| 16 | 0xBB | 0xC4 | angles.z (yaw) | 13 | same |
| 17 | 0xBF | 0xC8 | velocity.x | 1 | same; used by scripts |
| 18 | 0xC3 | 0xCC | velocity.y | 1 | same |
| 19 | 0xC7 | 0xD0 | velocity.z | 1 | same |
| 20 | 0xCB | 0xD4 | field20.x | none | same (no engine use in either game; script-owned); used by scripts |
| 21 | 0xCF | 0xD8 | field20.y | none | same (no engine use in either game; script-owned) |
| 22 | 0xD3 | 0xDC | field20.z | none | same (no engine use in either game; script-owned) |
| 23 | 0xD7 | 0xE0 | wp_speed | 1 | same index; initial value now from the object definition (as2@0x411f57); used by scripts |
| 24 | 0xDB | 0xE4 | wp_turn_rate | 1 | same; used by scripts |
| 25 | 0xDF | 0xE8 | wp_bank | 2 | same; used by scripts |
| 26 | 0xE3 | 0xEC | engine-internal | none | not checked: no engine use in the paired code, not used by scripts |
| 27 | 0xE7 | 0xF0 | engine-internal | none | not checked: no engine use in the paired code, not used by scripts |
| 28 | 0xEB | 0xF4 | color.r | 2 | same; used by scripts |
| 29 | 0xEF | 0xF8 | color.g | 2 | same |
| 30 | 0xF3 | 0xFC | color.b | 2 | same |
| 31 | 0xF7 | 0x100 | color.a | 2 | same |
| 32 | 0xFB | 0x104 | scale | 1 | same; used by scripts |
| 33 | 0xFF | 0x108 | frame | 2 | same; used by scripts |
| 34 | 0x103 | 0x10C | health | 9 | same; used by scripts |
| 35 | 0x107 | 0x110 | damage | 5 | same; used by scripts |
| 36 | 0x10B | 0x114 | score | 1 | same |
| 37 | 0x10F | 0x118 | wp_wait | 3 | same; used by scripts |
| 38 | 0x113 | 0x11C | render_type | 13 | same |
| 39 | 0x117 | 0x120 | engine-internal | 1 | same |
| 40 | 0x11B | 0x124 | engine-internal | 1 | same |
| 41 | 0x11F | 0x128 | engine-internal | 14 | same |
| 42 | 0x123 | 0x12C | engine-internal | 11 | same |
| 43 | 0x127 | 0x130 | engine-internal | 11 | same |
| 44 | 0x12B | 0x134 | axis[0] | 6 | same |
| 45 | 0x12F | 0x138 | axis[1] | 2 | same |
| 46 | 0x133 | 0x13C | axis[2] | 2 | same |
| 47 | 0x137 | 0x140 | axis[3] | 2 | same |
| 48 | 0x13B | 0x144 | axis[4] | 2 | same |
| 49 | 0x13F | 0x148 | axis[5] | 2 | same |
| 50 | 0x143 | 0x14C | axis[6] | 2 | same |
| 51 | 0x147 | 0x150 | axis[7] | 2 | same |
| 52 | 0x14B | 0x154 | axis[8] | 2 | same |
| 53 | 0x14F | 0x158 | engine-internal | 2 | same |
| 54 | 0x153 | 0x15C | engine-internal (model) | 9 | same |
| 55 | 0x157 | 0x160 | engine-internal (skin) | 3 | same |
| 56 | 0x15B | 0x164 | engine-internal | 1 | same |
| 57 | 0x15F | 0x168 | engine-internal | 1 | same |
| 58 | 0x163 | 0x16C | engine-internal | 1 | same |
| 59 | 0x167 | 0x170 | engine-internal | 1 | same |
| 60 | 0x16B | 0x174 | engine-internal | 1 | same |
| 61 | 0x16F | 0x178 | engine-internal | 1 | same |
| 62 | 0x173 | 0x17C | engine-internal | 1 | same |
| 63 | 0x177 | 0x180 | engine-internal | 1 | same |
| 64 | 0x17B | 0x184 | engine-internal | 1 | same |
| 65 | 0x17F | 0x188 | engine-internal | 1 | same |
| 66 | 0x183 | 0x18C | engine-internal | none | not checked: no engine use in the paired code, not used by scripts |
| 67 | 0x187 | 0x190 | engine-internal | none | not checked: no engine use in the paired code, not used by scripts |
| 68 | 0x18B | 0x194 | engine-internal | 2 | same |
| 69 | 0x18F | 0x198 | engine-internal | 1 | same |
| 70 | 0x193 | 0x19C | engine-internal | 1 | same |
| 71 | 0x197 | 0x1A0 | engine-internal | none | not checked: no engine use in the paired code, not used by scripts |
| 72 | 0x19B | 0x1A4 | engine-internal | none | not checked: no engine use in the paired code, not used by scripts |
| 73 | 0x19F | 0x1A8 | engine-internal | none | not checked: no engine use in the paired code, not used by scripts |
| 74 | 0x1A3 | 0x1AC | engine-internal | 1 | same |
| 75 | 0x1A7 | 0x1B0 | engine-internal | 1 | same |
| 76 | 0x1AB | 0x1B4 | engine-internal | 1 | same |
| 77 | 0x1AF | 0x1B8 | engine-internal | 3 | same |
| 78 | 0x1B3 | 0x1BC | engine-internal | none | not checked: no engine use in the paired code, not used by scripts |
| 79 | 0x1B7 | 0x1C0 | engine-internal | 1 | same |
| 80 | 0x1BB | 0x1C4 | engine-internal | 5 | same |
| 81 | 0x1BF | 0x1C8 | engine-internal | 5 | same |
| 82 | 0x1C3 | 0x1CC | engine-internal (collision rectangle) | 12 | same |
| 83 | 0x1C7 | 0x1D0 | engine-internal (collision rectangle) | 5 | same |
| 84 | 0x1CB | 0x1D4 | engine-internal | 1 | same |
| 85 | 0x1CF | 0x1D8 | engine-internal (collision rectangle) | 11 | same |
| 86 | 0x1D3 | 0x1DC | engine-internal (collision rectangle) | 4 | same |
| 87 | 0x1D7 | 0x1E0 | engine-internal | 1 | same |
| 88 | 0x1DB | 0x1E4 | (children count in v1.70) | 21 (v1.70 0x1DB → as2 0x1EC) | changed: the children count moved to k = 90; this dword is new, engine-internal |
| 89 | 0x1DF | 0x1E8 | (children array in v1.70) | 17 (v1.70 0x1DF → as2 0x1F0) | changed: the children array moved to k = 91; this dword is new, engine-internal |
| 90 | - | 0x1EC | - | see k = 88 | new: children count (engine-internal) |
| 91 | - | 0x1F0 | - | see k = 89 | new: children array (engine-internal) |

## Changelog

- 1.0 (B2): first version.
