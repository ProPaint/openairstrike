# RCSL builtins: behaviour of every builtin

Spec version 1.0 (WP-23). Companions: [rcsl-builtins-table.md](rcsl-builtins-table.md)
(signatures, table order, corpus counts), [rcsl-vm.md](rcsl-vm.md) (threads, dispatch,
return register, done flag), [engine-behaviour.md](engine-behaviour.md) (entities, damage,
player, camera), [render-pipeline.md](render-pipeline.md) (lights, lightning, screen
projection), [hmap.md](hmap.md) (waypoint paths), [obj.md](obj.md), [wpn.md](wpn.md).
Consistency check: `tools/ref/check_builtin_semantics.py` (prints one `OK` line).

Sources: the v1.70 executable (Ghidra export and disassembly, addresses cited), the 339
shipped scripts (call sites found by a scan of every CALL and LCALL, with the last write to
each argument slot in the same basic block), and the object, weapon and map definitions.
The sequel's decompilation was not needed; nothing below is tagged `INFERRED-SEQUEL`.

Every claim carries a tag from [README.md](README.md): `VERIFIED-CODE` (address given),
`VERIFIED-DATA` (checked on all shipped files), `GUESS`. Where a whole numbered step is
covered by one tag, the tag ends the step.

## 1. Conventions

**Script-level types.** As in rcsl-builtins-table.md: `float`; `int` (the slot as a float,
truncated toward zero); `vec` / `vec_out` (the slot holds the address of 3 floats); `entity`
(an entity reference: the address entity + 0x7B); `string` (the slot's raw bits are a byte
offset into the calling script's string table); `raw`. Units: world units (1 map cell =
40), seconds, degrees. `frametime` is the global at 0x1fb4bcc, seconds, at most 0.1.

**Names used below.**

- *self*: the entity whose script is running, i.e. the raw entity behind the `self`
  global. *self's player*: the player record selected by self's player index (+0x77);
  records are 0x171 bytes from 0x1ebe308 (engine-behaviour.md §7.1).
- *raw entity of a reference r*: the 4 bytes stored at address r (field 0).
- *Field k* of an entity: the float at entity + 0x7B + 4k (rcsl-vm.md, entity field map).
  Fields used here: 1 age, 2 class, 3 FL flags (a float holding bits), 4 dead, 5–7 origin,
  8–10 attach offset, 11–13 previous origin, 14–16 angles, 17–19 velocity, 23 path speed,
  24 turn rate, 25 bank factor, 32 scale, 34 health, 35 damage, 37 waypoint delay, 38
  render type, 41–43 base origin, 44–52 axis (three rows of three).
- Native offsets (engine-behaviour.md §3.2): +0x08 parent, +0x0C tag name, +0x10 `abs`
  flag, +0x11 counted-in-root flag, +0x12 attachment count, +0x16 name, +0x1A activation
  state, +0x1E runtime flags (0x01 removed, 0x04 script active, 0x08 on screen, 0x10
  frozen, 0x100 lock marker), +0x22 drop, +0x4B path, +0x4F path distance, +0x53 last path
  node, +0x57 thread, +0x5B touch filter, +0x6F time since damage, +0x73 looping channel,
  +0x77 player index, +0x153 model, +0x157 skin, +0x1BB emitter, +0x1DB/+0x1DF children.
- *The live list*: the pool entities, walked newest first from 0x458ccc (sentinel
  0x458cc8). Attach children created from a definition are not in it.
- *Return register* 0x1fa7dfc and *done flag* 0x1fa7e00 as in rcsl-vm.md. "Returns:
  none" means the register is not written, so the CALL destination receives a stale value.
- *ftol*: truncation toward zero (helper 0x440870). Two-player mode: flag 0x1fdb2c7.

## 2. Shared definitions

These procedures are used by several builtins; entries refer to them as D1..D9.

**D1. Resolving an entity argument.** A builtin that takes an entity reads the raw entity
through the reference (one indirection). The v1.70 code never tests the *reference* for
0: a reference of 0.0 makes it read address 0 and the game crashes (VERIFIED-CODE, e.g.
`remove` 0x41a985, `Damage` 0x41b57c, `activate` 0x41a9a0). Some builtins then test the
*raw* pointer for 0 (noted per entry). No shipped script passes 0.0 where an entity is
expected (VERIFIED-DATA: `remove` always gets `$self`; `Damage` gets `$other`, `$self` or
`$player`; the rest get `$player`, `$self` or a `create` result). An implementation should
treat a 0 reference as "no entity" and do nothing (engine decision, not original
behaviour).

**D2. Stale references.** A reference is a plain slot address with no generation count.
A removed entity is freed at the start of the next entity pass; the pool slot then keeps
its old contents (field 0 still points at itself, the removed bit stays set, the thread
pointer is 0) until the slot is reused, when it is zeroed and becomes a new entity at the
same address (VERIFIED-CODE 0x404590, 0x404530). A script that keeps a reference (for
example a homing missile's target, a boss's parts) therefore acts on whatever occupies the
slot. Attach children are heap blocks; a reference to a freed child is a dangling pointer
(GUESS: never happens in the shipped scripts). An implementation that wants to reproduce
the original exactly models references as pool slot indices; one that uses checked handles
must make a freed target look like "removed, dead, no thread".

**D3. Running a handler.** Running entry *e* (init 0, main 1, damage 2, touch 3, callback
4) of entity X synchronously: if X has a thread and the entry exists, save the globals
current thread, `self` and argument pointer, switch them to X, save X's pc and slots
t0..t15, run the interpreter from the entry, restore X's pc and t0..t15, restore the three
globals (rcsl-vm.md "Event dispatch"). The return register, the done flag, the latent
timeout, `other` and the `cb_*` globals are shared and not saved: a nested handler can
change the value that the calling CALL receives.

**D4. Attacker index** (for the kill counter and score of G_Damage). Single-player: 0.
Two-player: 0 if bit 0x2000 is set in ftol(self's field 3), otherwise 1 (VERIFIED-CODE
0x41b565, 0x41b6c1). Bit 0x2000 marks projectiles fired by player 1 (see `Shoot`), so in
two-player mode every other caller, player 2's projectiles and enemy scripts included, is
credited to player 2. Builtins that pass −1 award nothing.

**D5. G_Damage(T, amount, attacker)** (0x404ae0; engine-behaviour.md §6.1 in full):

1. Nothing happens if T's field 4 ≠ 0, if T's runtime bit 0x10 is set, or if T is the
   entity of a player record (index < player count) and either that record's freeze counter
   (+0xB4) ≠ 0 or god mode (0x1fdb2c4) is on.
2. T's time since damage (+0x6F) = 0; field 34 −= amount.
3. T's `damage` handler runs (D3). `other` is not set.
4. If field 34 ≤ 0 afterwards: if class = 2.0 and FL_NONTARGET (0x100) is clear in
   ftol(field 3), the attacker's kill counter (+0x16D) += 1 (attacker −1 writes 4 bytes
   before player 1's record; harmless); field 4 = 1.0; a drop (+0x22) is spawned at T and
   cleared; if attacker ≥ 0 the score is awarded (0x40bb20).

All VERIFIED-CODE 0x404ae0. G_Damage does not look at the class for the subtraction:
players, projectiles and scenery can all be damaged.

**D6. Direction to angles** (VectorToAngles 0x41e4f0). For a vector v: if v.x ≠ 0 or
v.y ≠ 0, yaw = atan2(v.y, v.x) and pitch = atan2(v.z, √(v.x² + v.y²)), both in degrees and
+360 when negative; otherwise yaw = 0 and pitch = 90 if v.z > 0, else 270. The result
written is (−pitch, 0, yaw − 90). VERIFIED-CODE.

**D7. Orientation convention.** The axis rows of an entity are, for angle fields x = 14,
y = 15, z = 16 (engine-behaviour.md §4.3): row 0 = (cy·cz, cy·sz, sy), row 1 =
(sx·sy·cz − cx·sz, sx·sy·sz + cx·cz, −sx·cy), row 2 = (−sx·sz − cx·sy·cz, sx·cz − cx·sy·sz,
cx·cy). Models face along **row 1** (+y at zero angles): the −90 in D6 exists so that
"yaw = D6 result" points row 1 along the vector. Consequently field 16 is the heading,
field 14 is the pitch (positive tilts the nose **down**: row 1 gets z = −sin x) and field
15 is the roll/bank about the nose. VERIFIED-CODE for the formulas (0x41e390) and for D6;
VERIFIED-DATA for the usage: the player script writes field 14 = 30 × vy / 150 (nose down
when flying forward) and field 15 from vx, `MoveToNextWP` writes its bank into field 15,
and the turret scripts clamp field 14 with `RotateToClamp` to [−60, 20] and similar
(guns elevate with negative values).

**D8. Matrix inverse defect** (0x41e730, used by `RotateTo`). The routine computes the
inverse of a 3×3 matrix m (row-major m0..m8) by cofactors divided by the determinant, but
element 7 (row 2, column 1) is computed as (m1·m6 − m0·m8) / det instead of
(m1·m6 − m0·m7) / det. All other elements are correct. VERIFIED-CODE (the product at
0x41e7b8 is reused at 0x41e7eb). Reproduce it where D8 is cited.

**D9. Screen segment test.** `TraceLine` and `TraceLineDamage` project both end points
with the camera matrices of the previous render (0x419540, render-pipeline.md §9.3) and
clip the 2D segment against each candidate's screen rectangle (0x40ca10,
engine-behaviour.md §5.1–5.2; a segment shorter than √0.5 pixel tests its end point).
Altitude plays no part.

## Summary table

Corpus = scripts / CALL sites / LCALL sites over the 339 shipped scripts (VERIFIED-DATA,
same numbers as rcsl-builtins-table.md). Priority: P0 = called by a script reachable from
mission 1 (the objects placed in `maps\level1.hsc` and their items, attachments, script
overrides, everything their scripts create or fire, and the player helicopters
`p_apache`/`p_comanche` with all their weapons), P1 = called elsewhere in the shipped
scripts, P2 = not called by any shipped script (VERIFIED-DATA, scratch reachability scan).
Confidence is that of the core behaviour; edge cases may carry their own tags.

| # | Builtin | Family | Corpus | Priority | Confidence |
|---|---|---|---|---|---|
| 3 | `random` | A math | 79 / 134 / 0 | P0 | VERIFIED-CODE |
| 4 | `crandom` | A math | 15 / 41 / 0 | P0 | VERIFIED-CODE |
| 5 | `sin` | A math | 66 / 98 / 0 | P0 | VERIFIED-CODE |
| 6 | `cos` | A math | 1 / 1 / 0 | P1 | VERIFIED-CODE |
| 7 | `tan` | A math | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 8 | `atan` | A math | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 9 | `abs` | A math | 20 / 20 / 0 | P0 | VERIFIED-CODE |
| 10 | `min` | A math | 13 / 16 / 0 | P0 | VERIFIED-CODE |
| 11 | `max` | A math | 3 / 6 / 0 | P0 | VERIFIED-CODE |
| 12 | `lerp` | A math | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 82 | `SetFlag` | A math | 1 / 4 / 0 | P0 | VERIFIED-CODE |
| 83 | `ClearFlag` | A math | 1 / 4 / 0 | P0 | VERIFIED-CODE |
| 13 | `vec_copy` | A math | 14 / 15 / 0 | P0 | VERIFIED-CODE |
| 14 | `vec_add` | A math | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 15 | `vec_sub` | A math | 57 / 64 / 0 | P0 | VERIFIED-CODE |
| 16 | `vec_ma` | A math | 3 / 6 / 0 | P0 | VERIFIED-CODE |
| 17 | `vec_length` | A math | 3 / 3 / 0 | P0 | VERIFIED-CODE |
| 18 | `vec_norm` | A math | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 19 | `vec_scale` | A math | 8 / 8 / 0 | P0 | VERIFIED-CODE |
| 20 | `vec_setlen` | A math | 3 / 3 / 0 | P0 | VERIFIED-CODE |
| 21 | `vec_toyaw` | A math | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 22 | `vec_toangles` | A math | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 23 | `ClearAxis` | A math | 3 / 3 / 0 | P0 | VERIFIED-CODE |
| 24 | `AnglesToAxis` | A math | 8 / 8 / 0 | P0 | VERIFIED-CODE |
| 25 | `create` | B entities | 176 / 461 / 0 | P0 | VERIFIED-CODE |
| 26 | `remove` | B entities | 228 / 269 / 0 | P0 | VERIFIED-CODE |
| 27 | `activate` | B entities | 1 / 1 / 0 | P1 | VERIFIED-CODE |
| 28 | `deactivate` | B entities | 4 / 5 / 0 | P1 | VERIFIED-CODE |
| 29 | `getentity` | B entities | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 30 | `setskin` | B entities | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 84 | `SetModel` | B entities | 3 / 9 / 0 | P0 | VERIFIED-CODE |
| 32 | `AttachEntity` | B entities | 8 / 11 / 0 | P0 | VERIFIED-CODE |
| 33 | `AttachActivate` | B entities | 59 / 86 / 0 | P0 | VERIFIED-CODE |
| 34 | `AttachDeactivate` | B entities | 63 / 221 / 0 | P0 | VERIFIED-CODE |
| 35 | `AttachCallback` | B entities | 21 / 34 / 0 | P1 | VERIFIED-CODE |
| 36 | `ParentCallback` | B entities | 8 / 8 / 0 | P1 | VERIFIED-CODE |
| 31 | `callback` | B entities | 12 / 12 / 0 | P0 | VERIFIED-CODE |
| 37 | `move` | C movement | 78 / 99 / 0 | P0 | VERIFIED-CODE |
| 38 | `movex` | C movement | 4 / 6 / 0 | P0 | VERIFIED-CODE |
| 39 | `movey` | C movement | 49 / 53 / 0 | P0 | VERIFIED-CODE |
| 40 | `movez` | C movement | 63 / 64 / 0 | P0 | VERIFIED-CODE |
| 41 | `rotate` | C movement | 4 / 4 / 0 | P0 | VERIFIED-CODE |
| 42 | `rotatex` | C movement | 19 / 20 / 0 | P0 | VERIFIED-CODE |
| 43 | `rotatey` | C movement | 20 / 31 / 0 | P0 | VERIFIED-CODE |
| 44 | `rotatez` | C movement | 96 / 108 / 0 | P0 | VERIFIED-CODE |
| 49 | `RotateTo` | C movement | 23 / 23 / 0 | P0 | VERIFIED-CODE |
| 50 | `lRotateTo` | C movement | 32 / 0 / 32 | P0 | VERIFIED-CODE |
| 51 | `RotateToClamp` | C movement | 19 / 20 / 0 | P1 | VERIFIED-CODE |
| 52 | `lRotateToClamp` | C movement | 18 / 0 / 18 | P1 | VERIFIED-CODE |
| 53 | `MoveToNextWP` | C movement | 20 / 20 / 0 | P0 | VERIFIED-CODE |
| 54 | `lMoveToNextWP` | C movement | 28 / 0 / 34 | P0 | VERIFIED-CODE |
| 55 | `RotateToNextWP` | C movement | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 56 | `lRotateToNextWP` | C movement | 6 / 0 / 6 | P1 | VERIFIED-CODE |
| 57 | `GetWaypointDelay` | C movement | 1 / 1 / 0 | P1 | VERIFIED-CODE |
| 58 | `TerrainHeight` | C movement | 35 / 44 / 0 | P0 | VERIFIED-CODE |
| 46 | `Shoot` | D weapons | 73 / 379 / 0 | P0 | VERIFIED-CODE |
| 47 | `Damage` | D weapons | 77 / 93 / 0 | P0 | VERIFIED-CODE |
| 48 | `RadialDamage` | D weapons | 4 / 4 / 0 | P0 | VERIFIED-CODE |
| 66 | `TraceLine` | D weapons | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 67 | `TraceLineDamage` | D weapons | 2 / 2 / 0 | P0 | VERIFIED-CODE |
| 68 | `Lightning` | D weapons | 1 / 1 / 0 | P0 | VERIFIED-CODE |
| 61 | `LockTarget` | D weapons | 3 / 3 / 0 | P0 | VERIFIED-CODE |
| 62 | `IsValidTarget` | D weapons | 3 / 4 / 0 | P0 | VERIFIED-CODE |
| 81 | `FreezeHealth` | D weapons | 2 / 6 / 0 | P1 | VERIFIED-CODE |
| 59 | `PlaceLight` | E effects | 12 / 23 / 0 | P0 | VERIFIED-CODE |
| 60 | `CameraQuake` | E effects | 11 / 12 / 0 | P0 | VERIFIED-CODE |
| 63 | `StartSound` | E effects | 58 / 116 / 0 | P0 | VERIFIED-CODE |
| 64 | `StartLoopingSound` | E effects | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 65 | `StopLoopingSound` | E effects | 3 / 3 / 0 | P0 | VERIFIED-CODE |
| 2 | `EndLevel` | F level/player | 4 / 4 / 0 | P0 | VERIFIED-CODE |
| 1 | `RespawnPlayer` | F level/player | 3 / 3 / 0 | P0 | VERIFIED-CODE |
| 78 | `ShowTutorialHint` | F level/player | 10 / 10 / 0 | P0 | VERIFIED-CODE |
| 69 | `PushPlayer` | F level/player | 17 / 17 / 0 | P0 | VERIFIED-CODE |
| 79 | `PlayerFreezeHealth` | F level/player | 2 / 4 / 0 | P0 | VERIFIED-CODE |
| 80 | `PlayerDisableAction` | F level/player | 2 / 3 / 0 | P0 | VERIFIED-CODE |
| 70 | `G_AddPowerUp` | F level/player | 4 / 4 / 0 | P0 | VERIFIED-CODE |
| 71 | `G_UsePowerUp` | F level/player | 3 / 12 / 0 | P0 | VERIFIED-CODE |
| 72 | `G_GetPowerUp` | F level/player | 3 / 3 / 0 | P0 | VERIFIED-CODE |
| 73 | `G_AddMissiles` | F level/player | 5 / 5 / 0 | P0 | VERIFIED-CODE |
| 74 | `G_UseMissile` | F level/player | 3 / 15 / 0 | P0 | VERIFIED-CODE |
| 75 | `G_GetMissiles` | F level/player | 3 / 3 / 0 | P0 | VERIFIED-CODE |
| 76 | `G_GetUpgrade` | F level/player | 13 / 16 / 0 | P0 | VERIFIED-CODE |
| 77 | `G_SetUpgrade` | F level/player | 13 / 13 / 0 | P0 | VERIFIED-CODE |
| 0 | `debug` | G other | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 45 | `sleep` | G other | 73 / 0 / 90 | P0 | VERIFIED-CODE |

Totals: 85 builtins; 62 P0, 10 P1, 13 P2.

## Builtins no shipped script calls

`debug`, `tan`, `atan`, `lerp`, `vec_add`, `vec_norm`, `vec_toyaw`, `vec_toangles`,
`getentity`, `setskin`, `RotateToNextWP` (its alias `lRotateToNextWP` is used),
`StartLoopingSound`, `TraceLine` (13; VERIFIED-DATA, same list as
rcsl-builtins-table.md). They are still resolvable by name, so the VM must accept them.

## Entry template

Each entry below has: signature (arguments with type and unit), returns, latent behaviour
(done flag), behaviour as numbered steps, edge cases, corpus with representative call
sites (`script` pc *n*), priority. The index and address are in the heading.

---

## A. Math, random, flags, vectors and axes

All of family A reads only its arguments and writes only its result or its `vec_out`
arguments; none touches entities, handlers or the done flag.

### 3. `random` — 0x41a360

- Signature: no arguments.
- Returns: float in [0, 1].
- Latent: no.
- Behaviour: 1. r = the C runtime `rand()` (state = state × 214013 + 2531011 mod 2³²,
  result = (state >> 16) & 0x7FFF, 0x42f593). 2. Return r × (1/32767) as a float, so 0 and 1
  are both reachable. VERIFIED-CODE. Our engine uses its own generator (README.md, engine
  decisions); only the range [0, 1] matters to the scripts.
- Edge cases: none.
- Corpus: 79 / 134 / 0; `barrel\barrel_benzin_dead.scr` pc 35, `effects\campfire.scr`
  (flicker factor 0.2 + 0.4 × random()).
- Priority: P0.

### 4. `crandom` — 0x41a380

- Signature: none. Returns: float in [−1, 1]. Latent: no.
- Behaviour: 1. u = rand() / 32767 rounded to float. 2. Return (u − 0.5) × 2.
  VERIFIED-CODE.
- Edge cases: none.
- Corpus: 15 / 41 / 0; `weapons\gorox\gorox_proj.scr` pc 4 (velocity jitter 60 × crandom),
  `barrel\barrel_benzin_dead.scr` pc 16.
- Priority: P0.

### 5. `sin` — 0x41a3b0

- Signature: `deg` float, degrees. Returns: float. Latent: no.
- Behaviour: return sin(deg × π/180). VERIFIED-CODE.
- Edge cases: NaN or infinity give NaN.
- Corpus: 66 / 98 / 0; `boss2\boss.scr` pc 91, `effects\expl1light.scr` (light pulse).
- Priority: P0.

### 6. `cos` — 0x41a3d0

- Signature: `deg` float, degrees. Returns: float. Latent: no.
- Behaviour: return cos(deg × π/180). VERIFIED-CODE.
- Edge cases: as `sin`.
- Corpus: 1 / 1 / 0; `boss2\boss.scr` pc 138.
- Priority: P1.

### 7. `tan` — 0x41a3f0

- Signature: `deg` float, degrees. Returns: float. Latent: no.
- Behaviour: return tan(deg × π/180). VERIFIED-CODE.
- Edge cases: ±90° give a large finite value (the argument is not exactly π/2).
- Corpus: none. Priority: P2.

### 8. `atan` — 0x41a410

- Signature: `x` float. Returns: float, **radians**. Latent: no.
- Behaviour: return atan(x × π/180). The input is scaled as if it were degrees and the
  output is not converted. VERIFIED-CODE (quirk; reproduce as is).
- Corpus: none. Priority: P2.

### 9. `abs` — 0x41a430

- Signature: `x` float. Returns: |x|. Latent: no. VERIFIED-CODE.
- Corpus: 20 / 20 / 0; `helics\helic1\bot1.scr` pc 201, player scripts (tilt limit).
- Priority: P0.

### 10. `min` — 0x41a460

- Signature: `a`, `b` float. Returns: float. Latent: no.
- Behaviour: return b if b ≤ a or the comparison is unordered (a NaN), else a.
  VERIFIED-CODE.
- Corpus: 13 / 16 / 0; `items\ammo\i_bpg.scr` pc 26 (`min(level + 1, 4)`: upgrade cap).
- Priority: P0.

### 11. `max` — 0x41a4a0

- Signature: `a`, `b` float. Returns: float. Latent: no.
- Behaviour: return b if b ≥ a or unordered, else a. VERIFIED-CODE.
- Corpus: 3 / 6 / 0; `player\player.scr` pc 1076 (`max(0, …)`).
- Priority: P0.

### 12. `lerp` — 0x41a4e0

- Signature: `t`, `a`, `b` float (t first). Returns: (b − a) × t + a. Latent: no.
  VERIFIED-CODE.
- Corpus: none. Priority: P2.

### 82. `SetFlag` — 0x41c5b0

- Signature: `flags` int, `bit` int (both floats truncated). Returns: float(int(flags) |
  int(bit)). Latent: no.
- Behaviour: pure; nothing is stored, the caller assigns the result. VERIFIED-CODE.
- Edge cases: NaN or out-of-range values convert to −2147483648 (rcsl-vm.md).
- Corpus: 1 / 4 / 0; `eol.scr` pc 26 (`p_action = SetFlag(p_action, 16)`: the
  end-of-level autopilot presses "forward").
- Priority: P0.

### 83. `ClearFlag` — 0x41c5e0

- Signature: `flags` int, `bit` int. Returns: float(int(flags) & ~int(bit)). Latent: no.
  VERIFIED-CODE.
- Corpus: 1 / 4 / 0; `eol.scr` pc 33.
- Priority: P0.

### 13. `vec_copy` — 0x41a500

- Signature: `src` vec, `dst` vec_out. Returns: none. Latent: no.
- Behaviour: dst.x = src.x, dst.y = src.y, dst.z = src.z, in that order. VERIFIED-CODE.
- Corpus: 14 / 15 / 0; `boss1\boss1.scr` pc 75 (`vec_copy(&self.origin, v)`).
- Priority: P0.

### 14. `vec_add` — 0x41a520

- Signature: `a`, `b` vec, `out` vec_out. Returns: none.
- Behaviour: out = a + b, component by component (x, then y, then z), so out may alias a
  or b. VERIFIED-CODE.
- Corpus: none. Priority: P2.

### 15. `vec_sub` — 0x41a550

- Signature: `a`, `b` vec, `out` vec_out. Returns: none.
- Behaviour: out = a − b, component by component; aliasing as `vec_add`. VERIFIED-CODE.
- Corpus: 57 / 64 / 0; `boss1\cannon_lighting.scr` pc 24 (`vec_sub(&player.origin,
  &self.origin, dir)`, the usual aim vector for `Shoot`).
- Priority: P0.

### 16. `vec_ma` — 0x41a580

- Signature: `a` vec, `s` float, `b` vec, `out` vec_out. Returns: none.
- Behaviour: out = a + b × s, each component rounded to float. VERIFIED-CODE.
- Corpus: 3 / 6 / 0; `player\player.scr` pc 1157 (`vec_ma(&self.origin, frametime ×
  p_speedfactor, &self.velocity, &self.origin)`, the player's integration step).
- Priority: P0.

### 17. `vec_length` — 0x41a5e0

- Signature: `v` vec. Returns: √(x² + y² + z²). VERIFIED-CODE.
- Corpus: 3 / 3 / 0; `player\player.scr` pc 1391 (speed test before `vec_setlen`).
- Priority: P0.

### 18. `vec_norm` — 0x41a630

- Signature: `v` vec_out (in place). Returns: float, the length before normalising.
- Behaviour: 1. L = |v|. 2. If L ≠ 0: v ×= 1/L. 3. Return L. VERIFIED-CODE.
- Corpus: none. Priority: P2.

### 19. `vec_scale` — 0x41a650

- Signature: `v` vec_out (in place), `s` float. Returns: none.
- Behaviour: v ×= s. VERIFIED-CODE.
- Corpus: 8 / 8 / 0; `boss1\boss1_rl.scr` pc 92 (`vec_scale(dir, 60)`).
- Priority: P0.

### 20. `vec_setlen` — 0x41a680

- Signature: `v` vec_out (in place), `len` float. Returns: none.
- Behaviour: if |v| ≠ 0, v ×= len / |v|; else v unchanged. VERIFIED-CODE (0x41e2a0).
- Corpus: 3 / 3 / 0; `player\player.scr` pc 1396 (`vec_setlen(&self.velocity, 100)` when
  `vec_length` of it exceeds 100).
- Priority: P0.

### 21. `vec_toyaw` — 0x41a6a0

- Signature: `v` vec. Returns: float, degrees.
- Behaviour: 1. If v.x = 0 exactly, return −90 (quirk: whatever v.y is). 2. Otherwise
  yaw = atan2(v.y, v.x) in degrees, +360 if negative. 3. Return yaw − 90 (D7).
  VERIFIED-CODE.
- Corpus: none. Priority: P2.

### 22. `vec_toangles` — 0x41a730

- Signature: `v` vec, `out` vec_out. Returns: none.
- Behaviour: out = D6(v) = (−pitch, 0, yaw − 90). VERIFIED-CODE.
- Corpus: none. Priority: P2.

### 23. `ClearAxis` — 0x41a750

- Signature: `axis` raw (address of 9 floats, normally a 3-slot-wide script array).
  Returns: none.
- Behaviour: write the identity: axis[0] = axis[4] = axis[8] = 1, the other six = 0.
  VERIFIED-CODE.
- Corpus: 3 / 3 / 0; `weapons\missile\p_heatmis.scr` pc 93.
- Priority: P0.

### 24. `AnglesToAxis` — 0x41a780

- Signature: `angles` vec (degrees, fields 14..16 order), `axis` raw (9 floats). Returns:
  none.
- Behaviour: write the three rows of D7 for x = angles[0], y = angles[1], z = angles[2]
  (0x41e390). The routine also leaves the six sines and cosines in engine scratch globals
  0x1fdb478..0x1fdb48c; nothing reads them afterwards that a script could see.
  VERIFIED-CODE.
- Corpus: 8 / 8 / 0; `boss1\boss1_rl.scr` pc 84 (`AnglesToAxis(&self.angles, m)`, then
  row 1 of m is used as the flight direction: the nose axis of D7).
- Priority: P0.
