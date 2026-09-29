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

---

## B. Entity creation, removal and hierarchy

Two kinds of parent–child link exist and the builtins below treat them differently
(VERIFIED-CODE 0x409ba0, 0x41aa30, 0x41b0d0, 0x404410):

- **Definition children**: the `attach` lines of an object definition. They are heap
  entities listed in the parent's child array (+0x1DB/+0x1DF), named by their attach `id`,
  thought, drawn, removed and freed with the parent. `AttachActivate`, `AttachDeactivate`,
  `AttachCallback`, `activate`, `deactivate` and `remove` recurse into them.
- **Linked pool entities**: made by `AttachEntity` and by `Shoot`'s muzzle flash. They stay
  in the live list with +0x08 pointing at the parent; they are not in the parent's child
  array, so the recursive builtins above do not reach them, and removing the parent does
  not remove them. Each one holds a count on the root of its parent chain (+0x12), which
  keeps that root from being freed until the linked entity is removed. The entity pass
  thinks the parent before the linked entity (0x405d8a).

### 25. `create` — 0x41a7a0

- Signature: `name` string (an object definition name), `pos` vec (world position).
- Returns: the new entity's reference, or 0.0 when nothing was created; see step 4 for
  the value the caller actually receives.
- Latent: no; the done flag is not written.
- Behaviour:
  1. Look `name` up in the calling script's CASH entries of kind 0 (exact, case-sensitive,
     first entry with that name). If found with a cached definition index, use it; if found
     uncached, or not found, look the name up in the object definitions (0x409860: first
     definition with that name; an unknown name logs `ERROR: Unknown object '%s'.` and
     gives 0). VERIFIED-CODE 0x41a7c0..0x41a844.
  2. Spawn (0x40a080): a pool entity from the definition at *pos*, with its definition
     children; if the FL flags have FL_ONGROUND (0x1) z = TerrainHeight(x, y), if FL_ONWATER
     (0x4) z = water level; a TYPE_MARK builds its decal. Health, maximum health and score
     come from the definition **unscaled** by difficulty. If the definition index is 0 or
     out of range nothing is created: write 0 to the return register and stop.
     VERIFIED-CODE 0x40a080, 0x409ba0.
  3. Copy self's fields 14, 15, 16 (angles) and self's player index (+0x77) into the new
     entity. VERIFIED-CODE 0x41a88e..0x41a8a8.
  4. Write the new reference (entity + 0x7B) to the return register **before** anything
     below runs. Nothing saves the register afterwards, so a RET or a value-returning
     builtin executed by the new entity's handlers in steps 5 and 8 replaces the value the
     caller's CALL receives (rcsl-vm.md quirk 9). VERIFIED-CODE 0x41a888.
  5. Run the `init` handler of the new entity and then, recursively, of its definition
     children, each child first taking its parent's player index (0x404fa0, which also
     sets the time since damage to 2.0). VERIFIED-CODE.
  6. If the entity has a shadow handle and is a model: register its shadow again for the
     current model and skin (init may have changed them) and, for projected shadow kinds
     1–3, bake it at the current origin. VERIFIED-CODE 0x41a8b8..0x41a90c.
  7. If class (field 2) = 2.0 and FL_NONTARGET (0x100) is clear in ftol(field 3): the
     level's enemy total 0x4d52f8 += 1 (used for "Enemies destroyed %"). The maximum level
     score is not changed. VERIFIED-CODE 0x41a912..0x41a962.
  8. One full think of the new entity (0x405a60 with scripts allowed): age += frametime,
     `main` runs if the active bit is set (it is, from the builder) and the game is not
     paused, then attachment, transform, screen rectangle, render queue, children. The
     entity was inserted at the newest end of the live list, so the running entity pass
     does not think it again this frame. VERIFIED-CODE 0x41a968.
- Edge cases: the returned reference may be stale data from step 4 (see there). Pool
  exhaustion (1024 entities) crashes the original (engine-behaviour.md §3.3); an
  implementation should create nothing and return 0.0. A `name` offset that is not a string
  start still works (it reads the bytes from there to the next NUL).
- Corpus: 176 / 461 / 0. Names: 377 sites pass a string, 84 pass string offset 0 (the
  first string of the script, disassembled as `#0.0`). Positions: `&self.origin` at 430
  sites, `&player.origin` at 16, script vectors elsewhere (VERIFIED-DATA). Examples
  `weapons\bpg\bpg_proj.scr` pc 6 (`create("bpg_hit", &self.origin)` in the touch
  handler), `barrel\balon_gaz.scr` pc 14 (`create("balon_gaz_dead", &self.origin)`).
- Priority: P0.

### 26. `remove` — 0x41a980

- Signature: `ent` entity. Returns: none. Latent: no.
- Behaviour (0x404410 on the raw entity E; nothing happens if E is 0):
  1. Set the removed bit (+0x1E |= 0x01).
  2. If E carries a particle emitter: stop it (emitter bytes +0x0C = 1 and +0x0E = 1) and
     clear E's emitter pointer.
  3. If E has a shadow handle and a per-frame shadow display list, delete the list.
  4. Apply steps 1–5 to every definition child, recursively.
  5. If E holds a count on a root (+0x11 ≠ 0): decrement the count (+0x12) of the root of
     E's parent chain and clear +0x11.
  All VERIFIED-CODE. The memory is released at the start of the next entity pass, once the
  entity's own count (+0x12) is below 1 (0x405cc0). From the moment the bit is set the
  entity is skipped by every later think, touch test, trace and damage loop that tests
  the bit (`RadialDamage`, `TraceLine`, `TraceLineDamage`, `LockTarget` do; `Lightning`
  does not, see there). The calling script keeps running: `remove(self)` does not stop
  the current invocation, and code after it executes normally (VERIFIED-CODE: no VM state
  is touched).
- Edge cases: D1 (a 0 reference crashes the original). Linked pool entities (AttachEntity,
  muzzle flashes) are not removed with their parent. Removing twice is harmless.
- Corpus: 228 / 269 / 0; every site passes `$self` (VERIFIED-DATA). Example
  `weapons\bpg\bpg_proj.scr` pc 8.
- Priority: P0.

### 27. `activate` — 0x41a9a0

- Signature: `ent` entity. Returns: none. Latent: no.
- Behaviour: on the raw entity E and recursively on its definition children (0x404a40):
  set the script-active bit (+0x1E |= 0x04); if the entity carries a particle emitter,
  re-enable it (emitter byte +0x0C = 0, emitter timer +0x10 = 0). VERIFIED-CODE.
- Edge cases: D1; no test for a removed entity.
- Corpus: 1 / 1 / 0; `boss3\boss3.scr` pc 53 (re-activates a stored part).
- Priority: P1.

### 28. `deactivate` — 0x41a9c0

- Signature: `ent` entity. Returns: none. Latent: no.
- Behaviour: on E and recursively on its definition children (0x404a90): clear the
  script-active bit; if E carries an emitter, disable it (emitter bytes +0x0C = 1,
  +0x0D = 0). VERIFIED-CODE. A deactivated entity still thinks, moves with its parent,
  renders, is touched and takes damage; only its `main` stops (rcsl-vm.md).
- Edge cases: D1. `deactivate(self)` inside `main` does not stop the current invocation.
- Corpus: 4 / 5 / 0; `boss1\boss1_cover.scr` pc 20 (`deactivate($self)`).
- Priority: P1.

### 29. `getentity` — 0x41a9e0

- Signature: `ptr` raw (a raw entity address). Returns: entity reference = ptr + 0x7B
  (integer addition on the slot bits). Latent: no. VERIFIED-CODE.
- Edge cases: 0 gives 0x7B, an invalid reference.
- Corpus: none. Priority: P2.

### 30. `setskin` — 0x41aa00

- Signature: `ent` entity, `name` string (texture path). Returns: none. Latent: no.
- Behaviour: E.skin (+0x157) = texture handle of `name`: an already registered texture of
  that name, else the image is loaded and registered (0x418cf0). VERIFIED-CODE.
- Edge cases: D1. For an empty name or a missing file the handle written is whatever the
  helper leaves in its result register (GUESS: 0, i.e. the model's default skin).
- Corpus: none. Priority: P2.

### 84. `SetModel` — 0x41c610

- Signature: `name` string, a model **file path** such as
  `models/apache/apache_2.mdl` (VERIFIED-DATA: all 9 sites). Returns: none. Latent: no.
- Behaviour: self's model handle (+0x153) = the registered model of that path, loading and
  registering it on first use (0x411e70). Only +0x153 changes: the bounding radius
  (+0x1BF), the shadow and the definition are kept. Collision rectangles and tags come
  from the model handle every frame, so they follow the new model. VERIFIED-CODE.
- Edge cases: a model that fails to load gives handle 0 (nothing drawn, no tags); the
  registry slot is still consumed; more than 1024 models logs `R_RegisterModel: Too many
  registered models.` and gives 0. VERIFIED-CODE.
- Corpus: 3 / 9 / 0; `player\player.scr` pc 898, 912, 923 (damaged helicopter models
  below 200 and 100 health, and back).
- Priority: P0.

### 32. `AttachEntity` — 0x41aa30

- Signature: `child` entity, `parent` entity, `tag` string (tag name in the parent's
  model), `flag` int (the `abs` mode). Returns: none. Latent: no.
- Behaviour (C = raw child; nothing happens if C is 0):
  1. C.parent (+0x08) = raw parent (read through the reference without a test).
  2. C.tag (+0x0C) = address of the `tag` string in the calling script (kept by pointer).
  3. C.runtime flags |= 0x24 (0x04 active, 0x20 "linked entity with its own screen
     rectangle").
  4. C.abs (+0x10) = (ftol(flag) ≠ 0).
  5. The root of C's new parent chain gets +0x12 += 1; C.+0x11 = 1.
  All VERIFIED-CODE 0x41aa30. From the next think on, C's origin is overwritten with the
  tag position of the parent plus C's fields 8–10 (engine-behaviour.md §4.4), so the
  script moves C only through fields 8–10. C inherits the parent's on-screen bit.
- Edge cases: D1 for `parent`. Attaching an entity that is already attached increments a
  root count again without releasing the old one, so the old root is never freed (GUESS:
  never happens; all sites attach a fresh entity or `$self` once). A tag the model lacks is
  logged and the child is detached at its first think (engine-behaviour.md §4.4).
- Corpus: 8 / 11 / 0. Two patterns (VERIFIED-DATA): a boss attaches parts it created,
  `AttachEntity(part, $self, "tag_cannon_lightning", 1)` (`boss1\boss1.scr` pc 26); a
  shield or effect attaches itself to the player, `AttachEntity($self, $player, …, 1)`
  (`player\p_shield.scr`, `player\p_rshield.scr`).
- Priority: P0.

### 33. `AttachActivate` — 0x41aa90

- Signature: `name` string (an attach `id`). Returns: none. Latent: no.
- Behaviour: walk self's definition children in order; for the **first** child whose name
  (+0x16, the attach `id`) equals `name` exactly and which is not removed: if it is
  already active, stop; otherwise activate it (as `activate`: recursive, emitter
  re-enabled) and stop. Removed children with that name are skipped. VERIFIED-CODE.
- Edge cases: no match: nothing. Linked pool entities are not searched.
- Corpus: 59 / 86 / 0; `barrel\balon_gaz.scr` pc 7 (`AttachActivate("GAZ")`), 41 sites
  turn on a `CAMPFIRE_ALMOSTDEAD` smoke emitter when a vehicle is badly damaged.
- Priority: P0.

### 34. `AttachDeactivate` — 0x41ab50

- Signature: `name` string. Returns: none. Latent: no.
- Behaviour: as `AttachActivate`, but the first non-removed match is deactivated (as
  `deactivate`) if it is active, and left alone if it is already inactive. VERIFIED-CODE.
- Corpus: 63 / 221 / 0; `boss1\boss1.scr` pc 28 (`AttachDeactivate("PROGRESS_FLARE1_R")`);
  `player\player.scr` pc 892–921 switches the `CAMPFIRE_ALMOSTDEAD` / `…2` smoke emitters
  on and off together with the damaged models (`SetModel`).
- Priority: P0.

### 35. `AttachCallback` — 0x41ac10

- Signature: `name` string, `msg`, `parm1`, `parm2` float. Returns: none (but see D3).
  Latent: no.
- Behaviour:
  1. Find the first definition child of self whose name equals `name` (removed children
     are **not** skipped here). No match: stop.
  2. If that child is removed or inactive: stop.
  3. `cb_msg` = msg, `cb_parm1` = parm1, `cb_parm2` = parm2 (raw copies).
  4. If the child has a thread and a `callback` entry, run it (D3).
  VERIFIED-CODE. The `cb_*` globals are not restored.
- Edge cases: a removed first match hides a later live child with the same name.
- Corpus: 21 / 34 / 0; `boss1\boss1.scr` pc 54 (`AttachCallback("COVER", 1, 0, 0)`).
  Messages used: 1 and 2 (VERIFIED-DATA).
- Priority: P1.

### 36. `ParentCallback` — 0x41ada0

- Signature: `msg`, `parm1`, `parm2` float. Returns: none (see D3). Latent: no.
- Behaviour: if self has a parent (+0x08, definition or linked): set `cb_msg`,
  `cb_parm1`, `cb_parm2`; if the parent has a thread and a `callback` entry, run it (D3).
  No test for a removed or inactive parent. VERIFIED-CODE.
- Corpus: 8 / 8 / 0; `boss1\boss1_sphere.scr` pc 7 (`ParentCallback(3, 0, 0)`: a boss
  part reports its destruction). Messages 1, 2, 3 (VERIFIED-DATA).
- Priority: P1.

### 31. `callback` — 0x41ae80

- Signature: `ent` entity, `msg`, `parm1`, `parm2` float. Returns: none (see D3). Latent:
  no.
- Behaviour: E = raw entity (D1). If E ≠ 0: set `cb_msg`, `cb_parm1`, `cb_parm2` (even when
  E has no thread); if E has a thread and a `callback` entry, run it (D3). No test for
  removed or inactive. VERIFIED-CODE.
- Corpus: 12 / 12 / 0, always to `$player` (VERIFIED-DATA). The ten weapon pick-ups send
  `callback($player, 2, weapon_id, 0)` when the weapon was not owned
  (`items\ammo\i_bpg.scr` pc 19); the player's handler sets `p_weapon = cb_parm1`. Message
  4 (`items\i_shield.scr`) makes the player create `p_shield`, message 5
  (`weapons_enemy\fball\bfg\lightingbomb_proj.scr`) creates `p_moln`
  (`player\player.scr` pc 925–950). The player's handler ends with `RET t0`, so the CALL
  in the sender receives that value (D3).
- Priority: P0.

---

## C. Movement, orientation, waypoints and terrain

The eight simple movers change self's fields only: no collision, no clamping, no angle
wrapping, no velocity integration (VERIFIED-CODE 0x41af60..0x41b0a0). They work on the
origin fields even for attached entities, whose origin is overwritten at the next think
(engine-behaviour.md §4.4).

### 37. `move` — 0x41af60

- Signature: `vel` vec (units per second). Returns: none. Latent: no.
- Behaviour: fields 5, 6, 7 += vel × frametime (each component rounded to float).
  VERIFIED-CODE.
- Corpus: 78 / 99 / 0; the projectile pattern `move(&self.velocity)` in `main`
  (`weapons\bpg\bpg_proj.scr` pc 11): the velocity set by `Shoot` is integrated here.
- Priority: P0.

### 38. `movex` — 0x41afb0

- Signature: `speed` float, units per second. Returns: none.
- Behaviour: field 5 += speed × frametime. VERIFIED-CODE.
- Corpus: 4 / 6 / 0; `futuristic\rotatingbomb.scr` pc 33 (`movex(20)`).
- Priority: P0.

### 39. `movey` — 0x41afd0

- Signature: `speed` float, units per second. Returns: none.
- Behaviour: field 6 += speed × frametime. VERIFIED-CODE.
- Corpus: 49 / 53 / 0; `helics\helic1\bot3.scr` pc 226 (`movey(-50)`), many scripts add
  the scroll speed (`camera[7]`).
- Priority: P0.

### 40. `movez` — 0x41aff0

- Signature: `speed` float, units per second. Returns: none.
- Behaviour: field 7 += speed × frametime. VERIFIED-CODE.
- Corpus: 63 / 64 / 0; `effects\scorenum.scr` pc 14 (`movez(32)`: score digits rise).
- Priority: P0.

### 41. `rotate` — 0x41b010

- Signature: `rate` vec, degrees per second for fields 14, 15, 16. Returns: none.
- Behaviour: fields 14, 15, 16 += rate × frametime. VERIFIED-CODE.
- Corpus: 4 / 4 / 0; `barrel\barrel_benzin_dead.scr` pc 70 (`rotate(&self[20])`: field
  20–22 used as an angular velocity).
- Priority: P0.

### 42. `rotatex` — 0x41b060

- Signature: `rate` float, degrees per second. Returns: none.
- Behaviour: field 14 (pitch, D7) += rate × frametime. VERIFIED-CODE.
- Corpus: 19 / 20 / 0; `bonuses\abomb\abomb_proj.scr` pc 36 (`rotatex(300)`).
- Priority: P0.

### 43. `rotatey` — 0x41b080

- Signature: `rate` float, degrees per second. Returns: none.
- Behaviour: field 15 (roll, D7) += rate × frametime. VERIFIED-CODE.
- Corpus: 20 / 31 / 0; `bonuses\clusterbomb\clusterbomb_small.scr` pc 73
  (`rotatey(480)`).
- Priority: P0.

### 44. `rotatez` — 0x41b0a0

- Signature: `rate` float, degrees per second. Returns: none.
- Behaviour: field 16 (heading) += rate × frametime. VERIFIED-CODE.
- Corpus: 96 / 108 / 0; `boss1\boss1_rl.scr` pc 40 (`rotatez(50)`).
- Priority: P0.

### 49. `RotateTo` — 0x41b750

- Signature: `target` entity, `axes` int (bit 0x1 heading, bit 0x2 pitch). Returns:
  none. Latent: computed done flag (used through the alias `lRotateTo`; plain CALL
  ignores it).
- Behaviour, one step per call:
  1. mask = ftol(axes); step = self's field 24 (turn rate, degrees per second) ×
     frametime.
  2. d = target's origin (fields 5–7, read through the reference) − self's origin.
  3. Local direction: Minv = inverse of self's axis (fields 44–52, the axis of self's
     **last think**, not recomputed from the current angles) computed with the D8 defect;
     local[j] = Minv[0][j]·d.x + Minv[1][j]·d.y + Minv[2][j]·d.z for j = 0, 1, 2. For an
     orthonormal axis without the defect this is (d·row0, d·row1, d·row2).
  4. Normalise local (unchanged if zero) and convert with D6: a = (−pitch, 0, yaw − 90).
     a[2] is the heading error (0 when the target is straight along the nose, row 1),
     a[0] the pitch error.
  5. arrived = 0. If mask & 1: e = a[2]; if e > 180, e −= 360; if e < −180, e += 360. If
     |e| < 0.1: field 16 += e and arrived |= 1. Otherwise, if |e| < step: field 16 += e
     (snap, **not** counted as arrived); else field 16 += step if e ≥ 0, −= step if e < 0.
  6. If mask & 2: the same with e = a[0] on field 14, arrived |= 2 when |e| < 0.1.
  7. done flag = (arrived == mask).
  All VERIFIED-CODE 0x41b750..0x41b9b7. The return register is not written.
- Consequences: an axis that snaps in this call reports arrival on the next call, so an
  LCALL takes at least one extra update; mask = 0 is done at once and turns nothing; a
  mask with other bits (4 and up) is never done. The D8 defect adds −cos(heading)·d.z (for
  zero pitch and roll) to local[1], which biases the aim when target and self are at
  different heights (for example a ground turret aiming at the player at z ≈ 100);
  reproduce it.
- Edge cases: D1 for `target` (it reads the target's fields directly). Target at self's
  exact position: local = 0, D6 gives (−270, 0, −90), i.e. heading error −90 and, after
  wrapping, pitch error +90.
- Corpus: 23 / 23 / 0; `boss1\cannon_lighting.scr` pc 36 (`RotateTo($player, 1)`),
  `boss2\pcanproj.scr` pc 28 (`RotateTo($player, 3)`). Targets are `$player` at 20 sites
  and a script variable at 3 (VERIFIED-DATA).
- Priority: P0.

### 50. `lRotateTo` — 0x41b750

- Same implementation as `RotateTo` (table record alias, VERIFIED-CODE). Used only with
  LCALL (VERIFIED-DATA): the step runs on every update of the thread until the done flag
  is set (all requested axes within 0.1°) or the latent timeout, if any, runs out. None of
  the 32 shipped sites has a TMO in the same basic block (VERIFIED-DATA); a completed
  LCALL leaves the timeout at 0, so they wait until aligned unless a handler of the thread
  left a timeout behind (rcsl-vm.md quirks 3–4).
- Corpus: 32 / 0 / 32; `boss1\cannon_plasma.scr` pc 26 (`lRotateTo($player, 1)`).
- Priority: P0.

### 51. `RotateToClamp` — 0x41b9c0

- Signature: `target` entity, `axes` int, `max` float (degrees), `min` float (degrees).
  Returns: none. Latent: computed (alias `lRotateToClamp`).
- Behaviour: 1. mask = ftol(axes); read max and min. 2. Do one `RotateTo` step (it sets
  the done flag). 3. If mask & 1: if field 16 > max, field 16 = max; then if field 16 <
  min, field 16 = min. 4. If mask & 2: the same on field 14. The done flag is left as
  `RotateTo` set it. VERIFIED-CODE.
- Consequences: the clamp works on the raw angle value (no wrapping), and when the target
  lies outside [min, max] the entity never arrives, so an LCALL without a timeout waits
  until the target comes back into range.
- Corpus: 19 / 20 / 0. Every site is `RotateToClamp($player, 2, 20, min)` with min −60
  (13), −80 (5), −70, −40 (VERIFIED-DATA): gun barrels elevate (negative field 14, D7) up
  to 60–80° and depress at most 20°. Example `btrs\btr\btr_guns.scr` pc 11.
- Priority: P1.

### 52. `lRotateToClamp` — 0x41b9c0

- Same implementation as `RotateToClamp`, used only with LCALL (VERIFIED-DATA, 18 sites,
  none preceded by a TMO in its block). Done when the unclamped step arrived (see above).
- Corpus: 18 / 0 / 18; `btrs\btr\btr_guns.scr` pc 51.
- Priority: P1.

### 53. `MoveToNextWP` — 0x41ba60

- Signature: `align` int (non-zero: the heading follows the path). Returns: none.
  Latent: computed (alias `lMoveToNextWP`).
- Behaviour (0x405fe0 on self; the path structure and its sample tables are defined in
  hmap.md "Waypoint paths"):
  1. No path (+0x4B = 0): done = 1, nothing else.
  2. Path already finished (its finished byte is set): deactivate self (as `deactivate`,
     recursive) and done = 1.
  3. s = path distance (+0x4F) + field 23 (speed, units per second) × frametime, stored
     (rounded to float). n = s × 0.125 (8 units per sample).
  4. If n > N − 1 (N = number of samples): path distance = 0 and last node (+0x53) = 0.
     For an open path: set the finished byte, field 37 = delay of the **last** waypoint,
     done = 1, stop (the position is not updated this call). For a looping path continue
     with s = 0 (the overshoot is dropped).
  5. Evaluate the path at s (0x405dd0): x and y of the origin are set to the curve point
     (z is untouched; ground or water snapping happens at the think); it also yields the
     interpolated heading h and bank value b, and the index i of the waypoint that starts
     the current segment.
  6. If i equals the last node (+0x53, compared as floats): field 37 = 0; if ftol(align) ≠ 0,
     field 16 = h; if field 25 ≠ 0, field 15 = b × field 25 / 4.8. done = 0.
  7. Otherwise (a waypoint was reached): last node = i; field 37 = delay of waypoint i;
     done = 1. Heading and bank are not updated in this call.
  All VERIFIED-CODE 0x405fe0..0x40610a, constants 0.125 and 4.8 read from the binary.
- Consequences: an LCALL returns once per waypoint (step 7) with field 37 = that
  waypoint's delay. Scripts that honour delays test field 37 > 0 and then wait with
  `TMO self[37]; LCALL sleep` (VERIFIED-DATA, e.g. `helics\helic1\bot4.scr` pc 83–90);
  others (`btrs\btr\btr.scr`) simply call again. At the wrap of a looping path no waypoint is
  reported (the last node is reset to 0 in step 4 and the segment index at s = 0 is 0).
  Field 37 is 0 on every other call.
- Edge cases: the bank value is only written by 0x405dd0 when the heading change between
  the two samples is non-zero; otherwise the original uses an uninitialised stack value
  (GUESS: in practice the value of the previous call). An implementation should use the
  interpolated bank table value always; it only matters when field 25 ≠ 0 (7 scripts).
- Corpus: 20 / 20 / 0 with align 1 (15) or 0 (5); `btrs\btr\btr.scr` pc 56.
- Priority: P0.

### 54. `lMoveToNextWP` — 0x41ba60

- Same implementation as `MoveToNextWP`, used only with LCALL: the thread advances along
  the path on every update and continues after the LCALL when a waypoint or the end is
  reached (or when a timeout set with TMO runs out: 10 of the 34 sites set one, 0.13 to
  2 s, VERIFIED-DATA).
- Corpus: 28 / 0 / 34; `btrs\btr\btr.scr` pc 68, `jeeps\jeep\jeep.scr` pc 68.
- Priority: P0.

### 55. `RotateToNextWP` — 0x41ba90

- Signature: none. Returns: none. Latent: computed.
- Behaviour (0x406110 on self):
  1. No path: done = 1.
  2. n = path distance × 0.125; k = floor(n); p = T[k] + (n − k)·(T[k+1] − T[k]) with T
     the sample-to-curve-parameter table; seg = floor(p).
  3. W = waypoint (seg + 1) mod K (K = number of waypoints): the **end of the current
     segment**, not the path tangent. dir = (W.x − origin.x, W.y − origin.y, 0), normalised
     (unchanged if zero); target heading t = D6(dir)[2] = yaw − 90.
  4. e = AngleMod360(t − field 16) (brought into [0, 360] by adding or subtracting 360
     repeatedly, 0x41e320); if e > 180, e −= 360. step = field 24 × frametime.
  5. If |e| < 0.1: field 16 = t, done = 1. Else if |e| < step: field 16 = t, done = 0.
     Else field 16 += step if e > 0 (or e = 0), −= step if e < 0; done = 0.
  All VERIFIED-CODE 0x406110..0x4062b6.
- Edge cases: the path position itself is not advanced. Self exactly at W: dir = 0, t =
  −90.
- Corpus: none under this name. Priority: P2 (the implementation is P1 through
  `lRotateToNextWP`).

### 56. `lRotateToNextWP` — 0x41ba90

- Same implementation as `RotateToNextWP`, used only with LCALL (6 sites, none with a TMO in
  its basic block): tanks turn on the spot toward the next waypoint before driving on
  (`tanks\tank.scr` pc 35).
- Corpus: 6 / 0 / 6. Priority: P1.

### 57. `GetWaypointDelay` — 0x41bab0

- Signature: `index` int. Returns: float. Latent: no.
- Behaviour: 1. Return register = 0. 2. If self has a path: i = ftol(index);
  return register = delay (+0x1C) of waypoint (i mod K), with the C remainder (a negative
  i gives a negative remainder and reads before the array). VERIFIED-CODE.
- Edge cases: negative index: undefined read in the original; an implementation should
  wrap it into [0, K).
- Corpus: 1 / 1 / 0; `nature\storm_spice.scr` pc 15.
- Priority: P1.

### 58. `TerrainHeight` — 0x41baf0

- Signature: `x`, `y` float, world units. Returns: float, terrain height (world z).
  Latent: no.
- Behaviour: return the bilinear height of the terrain vertex grid at (x, y) (0x416470),
  exactly as hmap.md "TerrainHeight" specifies, including 0 for x < 0 or y < 0 and the
  one-cell-too-generous upper bound of the original (our engine clamps; README.md).
  VERIFIED-CODE.
- Corpus: 35 / 44 / 0; every site passes components of a vector, typically
  `TerrainHeight(self.origin.x, self.origin.y)` to land debris or bombs
  (`bonuses\abomb\abomb_proj.scr` pc 21).
- Priority: P0.

---

## D. Weapons, targeting and damage

Target classes: native code recognises enemies by class (field 2) = 2.0 exactly; the
`enemy` keyword of a definition sets it (engine-behaviour.md §3.1). Players are found
through the two player records, not by class.

### 46. `Shoot` — 0x41b0d0

- Signature: `weapon` string (a weapon block name from `weapons\*.wpn`), `point` string (a
  tag name in self's model, or `origin`), `dir` vec (direction, any length). Returns:
  **none** (the return register is not written; see D3 for what the CALL receives).
  Latent: no.
- Behaviour (S = self's entity):
  1. If S's on-screen bit (+0x1E & 0x08) is clear, or S's activation state (+0x1A) is 2
     (leaving), return: off-screen shooters cannot fire. VERIFIED-CODE 0x41b0db..0x41b0e9.
  2. Muzzle M (0x4046a0): look `point` up in S's model (0x410380): the name `origin` gives
     (0, 0, 0); a found tag gives its position T; a tag the model lacks gives (0, 0, 0) and
     logs `Tag '%s' not found in model '%s'`. If field 32 (scale) > 0.01, T ×= scale. M =
     S's base origin (fields 41–43) + T.x·row0 + T.y·row1 + T.z·row2 of S's axis (fields
     44–52). Base origin and axis are those of S's **last think**, so movement done by the
     current `main` before `Shoot` is not reflected (one frame of lag). VERIFIED-CODE.
  3. W = the first weapon block with this exact name (0x40c990). Unknown: log `ERROR:
     Unknown weapon '%s'.` and return (no projectile, no flash). VERIFIED-CODE.
  4. P = a new pool entity from W's `missile` object definition, built at (0, 0, 0)
     (0x409ba0: no ground snapping, no difficulty scaling, active, player index 0). If
     W has no valid missile definition, skip to step 11. VERIFIED-CODE.
  5. P's class (field 2) = 4.0; P's origin and previous origin (fields 5–7 and 11–13) =
     M; P's velocity (fields 17–19) = the vector `dir` points to. VERIFIED-CODE.
  6. Normalise P's velocity (left as is if zero) and set P's angles (fields 14–16) =
     D6(velocity) = (−pitch, 0, yaw − 90), so P's nose (row 1, D7) points along `dir`.
     VERIFIED-CODE 0x41b1c1, 0x41b1ce.
  7. If P is a model (field 38 = 0): with P's model box minimum mn (0x411e20), P's origin
     += velocity × (−mn.y): the model is pushed forward so that its rear end sits at the
     muzzle. VERIFIED-CODE 0x41b1d3..0x41b243.
  8. P's velocity ×= W's `speed` (units per second; a weapon without `speed` has 0, so its
     projectile starts at rest and its script supplies the motion). VERIFIED-CODE.
  9. Run P's `init` handler (0x41a200 kind 1), then the `init` handler of each **direct**
     definition child of P that has a thread (not recursive, and the children do not take
     P's player index here), then one think of P (as `create` step 8; P's `main` runs).
     VERIFIED-CODE 0x41b273..0x41b36a.
  10. Difficulty: single-player: if self is not player 1's entity (`self` ≠ the `player`
      global of index 0), P's field 35 (damage) ×= g_damage_factor. Two-player: scaled
      unless self is either player's entity. The scaling happens **after** P's `init` and
      first `main` (step 9). VERIFIED-CODE 0x41b374..0x41b3a3.
  11. Two-player mode only: if S is player 1's entity: P's player index = 0 and P's field 3
      = float(ftol(field 3) | 0x2000); if S is player 2's: index 1 and | 0x4000; otherwise P's
      player index = S's. In single-player mode P keeps player index 0. VERIFIED-CODE
      0x41b3a5..0x41b449. (Consequence: during P's `init` and first `main` in step 9, `player`
      and the `p_*` globals always refer to player 1.)
  12. F = a new pool entity from W's `flash` definition at (0, 0, 0); if W has one: F's
      parent (+0x08) = S, F's tag (+0x0C) = the `point` string, F's `abs` flag = 1, F's
      active bit set, F.+0x11 = 1, and the root of S's parent chain (S itself for a root)
      gets +0x12 += 1 (a linked pool entity, see family B). Run F's `init` handler. F is
      not thought now; its position comes from the tag at its first think.
      VERIFIED-CODE 0x41b449..0x41b547.
- Consequences: the projectile's script moves it with `move(&self.velocity)` and deals
  damage in its `touch` handler with `Damage(other, self.damage)`; that damage is scaled by
  g_damage_factor again inside `Damage`, so enemy fire scales with the factor squared and
  player fire with the factor once (engine-behaviour.md §6.2). The flash is not removed
  with S; its own script removes it, and until then S cannot be freed.
- Edge cases: two-player mode with a weapon whose missile definition is missing: the
  original writes through a null pointer in step 11 (crash); guard it (no shipped weapon
  lacks a missile, wpn.md). A shooter without a model (handle 0; model table entry 0 is
  never filled, 0x411e70): for any tag other than `origin` the lookup leaves T unset, so
  the original uses stack garbage (VERIFIED-CODE 0x410380); use T = (0, 0, 0). `dir` of
  zero length: velocity 0,
  angles (−270, 0, −90) by D6 (pitch 270 for z = 0).
- Corpus: 73 / 379 / 0. Weapon names are string literals at every site (29 use string
  offset 0), tags come from string literals or string variables, `dir` is a script
  vector, usually `player.origin − self.origin` plus a lead such as (0, 32, 32)
  (VERIFIED-DATA). Examples `boss2\boss.scr` pc 57 (`Shoot("b2_wpn_missile",
  "tag_left_cannon", dir)`), `btrs\btr\btr_guns.scr` (aim at the player with 32 units of
  lead in y and z).
- Priority: P0.

### 47. `Damage` — 0x41b550

- Signature: `target` entity, `amount` float (hit points). Returns: none (D3). Latent: no.
- Behaviour: 1. T = raw target (D1). 2. a = amount × g_damage_factor (rounded to float).
  3. attacker = D4. 4. G_Damage(T, a, attacker) (D5), which runs T's `damage` handler
  synchronously. VERIFIED-CODE 0x41b550.
- Edge cases: D1 (0 crashes the original). Damage to a dead (field 4 ≠ 0) or frozen
  target does nothing, not even the handler. A negative amount heals and still runs the
  handler. Damage to self from inside self's `damage` handler recurses (the scripts do not
  do this).
- Corpus: 77 / 93 / 0. Targets: `$other` 76 (touch handlers), `$self` 16 (a ramming
  helicopter damages itself by 100–300 in its `touch` handler, `helics\helic1\bot1.scr`
  pc 28), `$player` 1 (`boss2\boss.scr` pc 50, `Damage($player, 200 ×
  frametime)`: a per-second beam). Amounts: `self.damage` (field 35) at 60 sites, fractions
  of it (/ 7, / 10, / 3) or literals 100–400 (VERIFIED-DATA). Example
  `weapons\bpg\bpg_proj.scr` pc 3.
- Priority: P0.

### 48. `RadialDamage` — 0x41b5d0

- Signature: `center` vec (world position), `radius` float (world units), `damage` float
  (hit points **per second** at the rim). Returns: none. Latent: no.
- Behaviour: for each entity E of the live list (newest first; the next pointer is read
  after E is processed): skip E if removed, if class ≠ 2.0 or if health (field 34) ≤ 0.
  d = |E.origin − center| (3D, rounded to float). If d ≤ radius (or the comparison is
  unordered): G_Damage(E, frametime × damage × (d / radius) × g_damage_factor, attacker D4).
  VERIFIED-CODE 0x41b5d0..0x41b744 (the division at 0x41b6d5/0x41b701, opcode DE F9,
  divides the distance by the radius).
- Consequences: damage is 0 at the centre and maximal at the rim: the shipped callers are
  expanding shock waves whose radius grows each frame (`effects\wave.scr`: radius =
  24 × scale, scale growing over 0.6 s), so the ring front does the damage. Players are
  never hit. No on-screen test, no FL_NONTARGET test; frozen entities are protected by
  G_Damage.
- Edge cases: radius 0 with an entity exactly at the centre gives 0/0 = NaN damage
  (health becomes NaN); guard it. Negative radius: nothing is in range.
- Corpus: 4 / 4 / 0; `effects\wave.scr` pc 14 (`RadialDamage(&self.origin, r, 100)`),
  `effects\p_wave.scr` (4000), `effects\wave_big.scr` (1000), one at 40.
- Priority: P0.

### 66. `TraceLine` — 0x41bd40

- Signature: `from`, `to` vec (world positions), `mask` int (0x2 players, 0x1 enemies).
  Returns: entity reference or 0.0. Latent: no.
- Behaviour:
  1. Project `from` and `to` to screen space (D9).
  2. If ftol(mask) & 2: for each player record i < player count, in order: if the player
     entity's field 4 = 0 and the segment crosses its screen rectangle (D9), return that
     player's reference. No on-screen or health test.
  3. If ftol(mask) & 1: for each live-list entity, newest first: not removed, on-screen bit
     set, class 2.0, health > 0 and the segment crosses its rectangle: return its reference.
  4. Return 0.0.
  VERIFIED-CODE 0x41bd40..0x41be96.
- Edge cases: the player entity pointer of a record is read without a test (GUESS: always
  set during play).
- Corpus: none. Priority: P2.

### 67. `TraceLineDamage` — 0x41bea0

- Signature: `from`, `to` vec (world positions), `damage` float (hit points per call).
  Returns: none (D3). Latent: no.
- Behaviour:
  1. Project both points (D9).
  2. If self's touch filter (+0x5B) is exactly 2 (TOUCH_PLAYER): for each player record in
     order whose entity has field 4 = 0 and whose rectangle the segment crosses:
     G_Damage(player, damage, −1).
  3. If the touch filter is exactly 1 (TOUCH_ENEMIES): for each live-list entity, newest
     first, that is not removed, on screen, class 2.0, a model (field 38 = 0), without
     FL_POINT_COLLISION (0x1000 in ftol(field 3)), with field 4 = 0, and crossed by the
     segment: G_Damage(E, damage, −1).
  4. Any other filter (0 or 3): nothing.
  VERIFIED-CODE 0x41bea0..0x41bff4. The damage is not scaled by frametime or by
  g_damage_factor and gives no score (attacker −1).
- Corpus: 2 / 2 / 0: the big laser beams, `weapons\laser\biglaser_proj.scr` pc 27
  (`TraceLineDamage(&self.origin, (player.x, player.y + 500, player.z), self.damage)`, once
  per projectile, then a 0.6 s fade) and its enemy twin in `weapons_enemy\laser\`.
- Priority: P0.

### 68. `Lightning` — 0x41c000

- Signature: none (the one caller also writes t0 and t1; they are ignored,
  rcsl-builtins-table.md). Returns: none (D3). Latent: no.
- Behaviour: for each live-list entity E (next pointer read before E is processed):
  1. Skip unless E's field 4 = 0, E's on-screen bit is set and class = 2.0. The removed bit
     is **not** tested.
  2. d = |E.origin − self.origin| (3D, float); skip unless d ≤ 500.
  3. Queue a lightning render record from self's origin to E's origin (render flags
     0x1000, drawn as in render-pipeline.md §7.1).
  4. G_Damage(E, self's field 35 × frametime × g_damage_factor, −1).
  VERIFIED-CODE 0x41c000..0x41c15f (500.0 read from 0x44bc68).
- Consequences: called every frame from `main`, so field 35 is damage per second. A
  removed enemy that has not been freed yet (same frame) can still be struck and drawn.
- Corpus: 1 / 1 / 0; `bonuses\lightingbomb\lightingbomb_proj.scr` pc 6 (each frame, while
  the bomb falls).
- Priority: P0.

### 61. `LockTarget` — 0x41bb40

- Signature: none. Returns: entity reference or 0.0. Latent: no.
- Behaviour (0x40c020):
  1. best = 9999, choice = none.
  2. For each live-list entity E, newest first: skip unless not removed, class = 2.0,
     FL_NONTARGET (0x100) clear in ftol(field 3), on-screen bit set, field 4 = 0 and health
     > 0.
  3. P = the entity of the player record selected by **E's** player index (+0x77), not
     self's. v = E.origin − P.origin; L = |v|; normalise v.
  4. If v.y ≥ 0.3 (E lies ahead of the player, within about 72° of the scroll direction)
     and L < best: best = L, choice = E.
  5. If a choice was made: set its runtime bit 0x100 and return its reference; else 0.0.
  VERIFIED-CODE (constants 9999, 0.3 read from the binary).
- Consequences: runtime bit 0x100 is written but never read anywhere in the executable
  (VERIFIED-CODE, scan of the export), so several missiles can lock the same target. Map
  objects in two-player mode have a random player index, so the "ahead" test uses that
  player.
- Edge cases: the player entity pointer is read without a test.
- Corpus: 3 / 3 / 0; the heat-seeking missiles (`weapons\missile\p_heatmis.scr` pc 95,
  `p_heatmis_big.scr`, `p_missile_mad.scr`), which then steer with `RotateTo(target, 3)`
  each frame while `IsValidTarget(target)` holds.
- Priority: P0.

### 62. `IsValidTarget` — 0x41bb60

- Signature: `ent` entity. Returns: 1.0 or 0.0. Latent: no.
- Behaviour: return 1.0 if the reference is non-zero, the entity's field 4 = 0 and its
  field 34 > 0, else 0.0. The fields are read through the reference itself (no
  dereference), and the removed bit is not tested. VERIFIED-CODE.
- Edge cases: 0.0 gives 0.0 safely. A stale reference (D2) reads whatever the slot holds:
  a freed enemy keeps field 4 = 1.0 or health ≤ 0 in the common case (killed), but an
  enemy removed alive (left the screen) stays "valid" until the slot is reused.
- Corpus: 3 / 4 / 0; `weapons\missile\p_heatmis.scr` pc 13, 46.
- Priority: P0.

### 81. `FreezeHealth` — 0x41c530

- Signature: `ent` entity, `on` float. Returns: none. Latent: no.
- Behaviour: on ≠ 0: set runtime bit 0x10 of the raw entity; on = 0 (or −0): clear it.
  VERIFIED-CODE. While set, G_Damage ignores the entity (D5) and the touch pass does not
  offer it as an enemy candidate (engine-behaviour.md §5.2). Not recursive.
- Edge cases: D1. NaN counts as non-zero (sets the bit).
- Corpus: 2 / 6 / 0; `boss1\boss1.scr` pc 14 / 57 (boss parts invulnerable until their
  phase), `boss3\boss3.scr`.
- Priority: P1.

---

## E. Effects, lights, camera and sound

No builtin creates particles directly: particle systems are attached through object
definitions (`attach` of a `.ps` name) and switched with `AttachActivate` /
`AttachDeactivate`, or come with objects made by `create`.

### 59. `PlaceLight` — 0x41bb20

- Signature: `pos` vec (world position), `color` vec (RGB multipliers, values above 1
  allowed), `radius` float (world units). Returns: none. Latent: no.
- Behaviour: if fewer than 32 dynamic lights are queued this frame, append one (0x40dd00)
  with the three values copied; otherwise ignore the call. The list is cleared at the
  start of each frame (0x40e690) and used when models are lit (render-pipeline.md §2.4:
  contribution (0.7·n·l + 0.3) × (radius − d) / radius × color for d ≤ radius).
  VERIFIED-CODE.
- Consequences: a light lasts one frame, so scripts call it from `main` every frame; while
  the game is paused `main` does not run and script lights disappear (VERIFIED-CODE,
  0x405a60).
- Corpus: 12 / 23 / 0; `effects\campfire.scr` pc 3 (`PlaceLight(&self.origin, c, 200)` with
  c = (2.5, 2, 0) × a random flicker), explosion lights with radius 150 + t, the player's
  weapons (`player\player.scr`), radii up to 6000 (VERIFIED-DATA).
- Priority: P0.

### 60. `CameraQuake` — 0x41bb90

- Signature: `amount` float (amplitude A). Returns: none. Latent: no.
- Behaviour: quake amplitude (0x1ebe5f0) = A; quake time left (0x1fdb2d4) = 3.0 s; quake
  time (0x1ebe654) = 0. A later call restarts the quake with the new amplitude.
  VERIFIED-CODE. The camera applies it for 3 s of unpaused time (engine-behaviour.md §9.4,
  0x40c150: yaw 2A·e^(−2t)·sin(24t), roll and field of view similar).
- Corpus: 11 / 12 / 0; amplitudes 0.3, 0.5, 0.6, 1.0, 1.4 (VERIFIED-DATA); `boss1\boss1.scr`
  pc 201 (1.4), explosion scripts (`effects\expl1.scr`).
- Priority: P0.

### 63. `StartSound` — 0x41bc50

- Signature: `file` string (a sample path, e.g. `sounds/expl3.wav`). Returns: none.
  Latent: no.
- Behaviour:
  1. Look the name up among the calling script's CASH entries of kind 3 (0x41bbc0); take
     the cached sample handle of the first entry with that name.
  2. If none (or 0): register the sample by name, loading it (0x41ff90, at most 511
     samples).
  3. Play it once, attached to self (0x41fdf0, not looping): if sound is off or the handle
     is 0 nothing plays; a sample whose registry record has its flag byte (+0x44) set plays
     as a plain unpositioned channel (GUESS: the flag marks samples registered as UI
     sounds); otherwise it takes a free one of the 32 entity channels (none free: not
     played) and is positioned at the entity every frame (engine-behaviour.md §12).
  VERIFIED-CODE.
- Corpus: 58 / 116 / 0; `boss2\boss.scr` pc 53 (`StartSound("sounds/missile.wav")`),
  weapon fire in `player\player.scr`, explosions. 53 sites pass string offset 0.
- Priority: P0.

### 64. `StartLoopingSound` — 0x41bcd0

- Signature: `file` string. Returns: none. Latent: no.
- Behaviour: 1. If self's looping channel (+0x73) is set: stop it (when sound is on) and
  clear it. 2. Resolve the sample as `StartSound` steps 1–2. 3. Play it looping attached
  to self and store the channel in +0x73 (0 if it could not play). VERIFIED-CODE.
- Edge cases: the channel is not stopped when the entity is removed (GUESS: the sound
  module stops channels of freed entities; not traced).
- Corpus: none. Priority: P2.

### 65. `StopLoopingSound` — 0x41bca0

- Signature: none. Returns: none. Latent: no.
- Behaviour: if self's +0x73 is set: stop that channel (only when sound is enabled) and
  clear +0x73. VERIFIED-CODE.
- Corpus: 3 / 3 / 0; the player scripts at death (`player\player.scr` pc 1412). Since no
  shipped script starts a looping sound, this is a no-op in practice (VERIFIED-DATA).
- Priority: P0.

---

## F. Level flow, player, HUD and items

"Self's player" is the record selected by self's +0x77 (0 or 1). The counts below are
32-bit integers in the record (engine-behaviour.md §7.1): power-up counts at +0xB9 (16),
selected power-up +0xF9 (−1 none), missile counts +0xFD (5), selected missile +0x111 (−1
none), upgrade levels +0x115 (20).

**Integer arguments of the G_ builtins** are converted with the rounding control forced to
truncation (FNSTCW, OR 0x0C00, FLDCW around FISTP), i.e. truncation toward zero, and then
compared **unsigned**, so negative values fail the range test (VERIFIED-CODE 0x41c219,
0x41c2f0, 0x41c3d0, 0x41c440).

### 2. `EndLevel` — 0x407570

- Signature: none. Returns: none. Latent: no.
- Behaviour: HUD-hidden flag 0x4d52d6 = 1, paused flag 0x458eab = 1, then the mission
  complete sequence (0x4269b0; engine-behaviour.md §1.3 state 9, §10.3–10.4).
  VERIFIED-CODE. From the next frame on no `main` runs and the world is frozen behind the
  mission-complete screen; the calling script finishes its current invocation.
- Corpus: 4 / 4 / 0; `eol.scr` (the end-of-level trigger, after its autopilot) and the three
  bosses (`boss1\boss1.scr` pc 273).
- Priority: P0.

### 1. `RespawnPlayer` — 0x41c640

- Signature: none. Returns: none. Latent: no.
- Behaviour: if self is player 1's entity, run G_SpawnPlayer(0); then, if self is player
  2's entity, G_SpawnPlayer(1) (0x40b2f0). Otherwise nothing. G_SpawnPlayer(i):
  1. Remove the record's current entity (as `remove`).
  2. If p_lives < 0: set that entity's field 4 = 1.0 and stop (game over is detected by the
     player logic).
  3. Clear the power-up and missile counts and selections of **both** records.
  4. Create the helicopter object of the record's helicopter index (a pool entity at
     (0, 0, 0), no ground snap), set its player index = i, store it in the record and in the
     `player` global of index i, reset the record's freeze counter (+0xB4) and
     action-disabled flag (+0xB8) to 0, and run its `init` handler (recursively).
  5. Two-player mode: x −= 100 for player 1, += 100 for player 2 (after `init`).
  All VERIFIED-CODE 0x40b2f0. The new entity gets no immediate think.
- Consequences: the new player's `init` runs nested inside the dying player's script;
  after `RespawnPlayer` returns, the old script continues with `self` = the removed
  entity, and `player` already names the new helicopter.
- Corpus: 3 / 3 / 0; `player\player.scr` pc 1414 (after the crash sequence and
  `p_lives -= 1`).
- Priority: P0.

### 78. `ShowTutorialHint` — 0x41c4c0

- Signature: `text` string. Returns: none. Latent: no.
- Behaviour: open the message box with this text (0x42c000): the world is paused (paused
  flag = 1), the text is split into lines at `^` (at most 16 lines of 64 characters), and
  the OK button resumes (engine-behaviour.md §11.3). Braces `{…}` in the shipped texts
  mark highlighted words (GUESS: drawn in another colour; the formatter was not traced).
  VERIFIED-CODE for the pause and the line split.
- Corpus: 10 / 10 / 0, one per `items\help\i_help_*.scr` pick-up (mission 1 is the
  tutorial), e.g. `items\help\i_help_ammobox.scr` pc 11.
- Priority: P0.

### 69. `PushPlayer` — 0x41c170

- Signature: none. Returns: none. Latent: no.
- Behaviour: P = the entity of the `player` global for self's player index. n = (P.x −
  self.x, P.y − self.y, 0), normalised (unchanged if zero). P's velocity (fields 17–19)
  += n × 4000 × frametime. VERIFIED-CODE (4000.0 read from the binary).
- Consequences: a per-frame shove of 4000 units/s² away from self in the ground plane; the
  player script integrates and clamps the velocity (±150 per axis).
- Corpus: 17 / 17 / 0; enemy helicopters and `boss2\boss.scr` pc 47 call it from their
  `touch` handler when they ram the player (`helics\helic1\bot1.scr` pc 29).
- Priority: P0.

### 79. `PlayerFreezeHealth` — 0x41c4e0

- Signature: `on` float. Returns: none (the helper leaves a pointer in EAX; the return
  register is not written). Latent: no.
- Behaviour: on ≠ 0: self's player's freeze counter (+0xB4) += 1; on = 0: −= 1. While the
  counter is non-zero (negative included) G_Damage ignores that player's entity (D5).
  VERIFIED-CODE. G_SpawnPlayer resets the counter to 0.
- Edge cases: unbalanced calls leave the player invulnerable (negative counter) until the
  next respawn.
- Corpus: 2 / 4 / 0; `player\p_rshield.scr` pc 1 (on) and pc 30 (off after 7 s),
  `player\p_shield.scr`.
- Priority: P0.

### 80. `PlayerDisableAction` — 0x41c560

- Signature: `on` float. Returns: none. Latent: no.
- Behaviour: self's player's action-disabled byte (+0xB8) = (on ≠ 0); if it is now set,
  that player's `p_action` = 0. While set, key events are not applied to that player
  (engine-behaviour.md §7.2), but scripts can still write `p_action`. VERIFIED-CODE.
- Corpus: 2 / 3 / 0; `eol.scr` pc 134 (the autopilot then drives the helicopter with
  `SetFlag`/`ClearFlag` on `p_action`), `player\p_moln.scr` pc 1 and 23 (stunned).
- Priority: P0.

### 70. `G_AddPowerUp` — 0x41c210

- Signature: `type` int (0–15), `count` int. Returns: none. Latent: no.
- Behaviour: t = trunc(type), c = trunc(count). If t < 16 (unsigned): n = counts[t] + c;
  if n > 98 as an unsigned number, n = 99; counts[t] = n; if no power-up is selected (< 0),
  select t. VERIFIED-CODE.
- Edge cases: a negative sum is a huge unsigned value and becomes 99. Out-of-range type:
  nothing.
- Corpus: 4 / 4 / 0; `items\bombs\i_abomb.scr` pc 10 (`G_AddPowerUp(1, 1)`), types 0–3
  with counts 1, 2, 4 (VERIFIED-DATA; kinds in engine-behaviour.md §8.3).
- Priority: P0.

### 71. `G_UsePowerUp` — 0x41c2d0

- Signature: none. Returns: 1.0 if a power-up was consumed, else 0.0. Latent: no.
- Behaviour (0x40b150 for self's player): if no power-up is selected, return 0. If the
  selected count < 1, return 0 (no change). Otherwise decrement it; if it is now < 1, set it
  to 0 and select the next kind with a non-zero count (0x40b0d0: kinds sel+1, sel+2, … mod
  16; −1 if none has any); return 1. VERIFIED-CODE.
- Corpus: 3 / 12 / 0; `player\player.scr` pc 718 (fires the selected bomb when the
  power-up key bit 0x4 is held and the use interval allows).
- Priority: P0.

### 72. `G_GetPowerUp` — 0x41c2b0

- Signature: none. Returns: the selected power-up kind as a float, −1.0 if none.
  VERIFIED-CODE.
- Corpus: 3 / 3 / 0; `player\player.scr` pc 870 (selects the subroutine that fires the
  matching bonus weapon, e.g. `Shoot("p_bonus_lightingb", "tag_gun1", …)`).
- Priority: P0.

### 73. `G_AddMissiles` — 0x41c2f0

- Signature: `type` int (0–4), `count` int. Returns: none. Latent: no.
- Behaviour: as `G_AddPowerUp` with 5 missile slots: type < 5 (unsigned), count capped at
  99, selects the type if none is selected. VERIFIED-CODE.
- Corpus: 5 / 5 / 0; `items\missiles\i_mis.scr` pc 10 (`G_AddMissiles(0, 20)`); counts 20,
  12, 15 (VERIFIED-DATA).
- Priority: P0.

### 74. `G_UseMissile` — 0x41c3b0

- Signature: none. Returns: 1.0 if a missile was consumed, else 0.0. Latent: no.
- Behaviour (0x40b220): as `G_UsePowerUp` over the 5 missile slots; when the selected type
  runs out, the next type with missiles is selected (mod 5; −1 if none). VERIFIED-CODE.
- Corpus: 3 / 15 / 0; `player\player.scr` pc 582 (one site per missile type: the script
  asks `G_GetMissiles`, then `G_UseMissile`, and fires only when it returned 1).
- Priority: P0.

### 75. `G_GetMissiles` — 0x41c390

- Signature: none. Returns: the selected missile type as a float, −1.0 if none.
  VERIFIED-CODE.
- Corpus: 3 / 3 / 0; `player\player.scr` pc 851.
- Priority: P0.

### 76. `G_GetUpgrade` — 0x41c3d0

- Signature: `index` int (weapon id 0–19). Returns: float. Latent: no.
- Behaviour: i = trunc(index); if i > 19 as an unsigned number return 0.0, else the upgrade
  level of weapon i of self's player (0 = not owned). VERIFIED-CODE.
- Corpus: 13 / 16 / 0; the weapon pick-ups (`items\ammo\i_bpg.scr` pc 10:
  `G_GetUpgrade(5)`), the player script with `p_weapon`. Weapon ids 0–9 (VERIFIED-DATA;
  names in engine-behaviour.md §8.2).
- Priority: P0.

### 77. `G_SetUpgrade` — 0x41c440

- Signature: `index` int (0–19), `value` int. Returns: none. Latent: no.
- Behaviour: i = trunc(index); if i < 20 (unsigned): upgrade level of weapon i of self's
  player = trunc(value). VERIFIED-CODE.
- Corpus: 13 / 13 / 0; `items\ammo\i_bpg.scr` pc 31 (`G_SetUpgrade(5, min(level + 1,
  4))`), the player script at death (every weapon above level 1 loses a level).
- Priority: P0.

---

## G. Other

### 0. `debug` — 0x41a350

- Signature: none. Returns: none. Latent: no. Behaviour: returns immediately.
  VERIFIED-CODE.
- Corpus: none. Priority: P2.

### 45. `sleep` — 0x41b0c0

- Signature: none (the value some scripts leave in t0 is ignored). Returns: none. Latent:
  done flag = 0.
- Behaviour: store 0 in the done flag. Under `LCALL` the call therefore never completes by
  itself: it waits until the thread's latent timeout (set by the preceding `TMO`) runs
  out, decremented by frametime on each update (rcsl-vm.md "LCALL and TMO"). With a
  timeout of exactly 0 it waits forever. Under a plain CALL it does nothing.
  VERIFIED-CODE.
- Corpus: 73 / 0 / 90; all 90 sites are LCALLs preceded by a TMO (VERIFIED-DATA): literal
  durations 0.1–7 s, or a script value such as `self[37]` (the waypoint delay, 26 sites
  take the timeout from memory). Example `boss1\boss1.scr` pc 268.
- Priority: P0.

---

## Notes for implementers (most important first)

1. **Nested handlers share VM state.** `create`, `Shoot`, `Damage`, `RadialDamage`,
   `TraceLineDamage`, `Lightning`, `callback`, `AttachCallback`, `ParentCallback` and
   `RespawnPlayer` run other entities' handlers synchronously (D3). The return register,
   the latent timeout, `other` and `cb_*` are shared, so the value a CALL receives from
   these builtins can come from the nested script (for `create` this is documented
   behaviour the shipped scripts live with).
2. **`Shoot` returns nothing**, fires only when the shooter is on screen and not leaving,
   takes the muzzle from the shooter's last-think transform, and scales enemy projectile
   damage by g_damage_factor after the projectile's `init` and first `main` (§D).
3. **Orientation**: models face row 1 of the axis (+y at zero angles); field 16 is the
   heading, field 14 the pitch (positive = nose down), field 15 the roll (D7). All
   direction-to-angle conversions subtract 90° from the heading (D6).
4. **`RotateTo` uses a defective matrix inverse** (D8) and reports arrival only when the
   error was already below 0.1° at the start of the step. Homing missiles and turrets
   depend on both.
5. **Integer arguments of the G_ builtins are truncated** and range-tested unsigned.
6. **Enemy targeting keys on class 2.0**; `RadialDamage` damage grows with distance from
   the centre; `TraceLineDamage` picks its victims by the caller's touch filter; the lock
   marker of `LockTarget` is never read.
7. **Entity references are raw slot addresses** (D1, D2); decide on a policy for 0 and
   stale references and keep the original's behaviour where the shipped scripts can
   observe it (a stale homing target, a player reference after the final death).
8. **Linked pool entities** (`AttachEntity`, muzzle flashes) are not reached by the
   recursive child builtins and keep their parent's root alive until they are removed.

## Open questions

1. `MoveToNextWP` bank value when the heading does not change between two path samples:
   the original leaves an uninitialised stack value (it only matters for the 7 scripts that
   set field 25). Proposed: use the interpolated bank table value. Low impact.
2. The per-sample flag byte (+0x44 of a sound registry record) that makes
   `StartSound` play unpositioned: which samples have it (GUESS: UI samples). Sound
   package.
3. Whether looping channels (`StartLoopingSound`, +0x73) are stopped when their entity is
   freed. Irrelevant for the shipped scripts (never started).
4. `setskin` result for an empty or missing texture name (unused builtin).
5. The definition `item_help` (objects/items.obj) names `scripts\items\i_help.scr`,
   which is not shipped; all ten placements (all in mission 1) override the script
   (VERIFIED-DATA). The object precache and the object builder create a thread from the
   definition script first; what the v1.70 loader does with the missing file was not
   traced (script loader package). An implementation must tolerate it, as it is P0.
6. Policy for 0 and stale entity references (D1, D2): an engine decision; the original
   crashes on 0 and acts on the slot's current occupant when stale.
7. `ShowTutorialHint` text formatting of `{…}` spans (UI package).

## Corrections to other specs

Listed here, not applied (WP-23 does not edit other specs).

| Spec, place | Says | Correct | Evidence |
|---|---|---|---|
| engine-behaviour.md §8.1 step 8 | `Shoot` "returns the projectile handle" | `Shoot` never writes the return register; its CALL receives a stale value or whatever a nested handler left | no store to 0x1fa7dfc in 0x41b0d0..0x41b54f |
| engine-behaviour.md §8.1 steps 4–6 | init, then damage scaling, then two-player bits | Order is: projectile `init`, direct children's `init` (not recursive), one think, damage scaling, two-player index and bits, flash; during the projectile's `init` and first `main` its player index is still 0 | 0x41b273, 0x41b36a, 0x41b374, 0x41b3a5 |
| engine-behaviour.md §8.2 | `G_GetUpgrade`/`G_SetUpgrade` "round their float arguments to nearest" | They truncate: the control word is set to round-toward-zero (OR 0x0C00) around each FISTP; the same holds for `G_AddPowerUp` and `G_AddMissiles`. rcsl-builtins-table.md (`int` = truncated) is right | 0x41c3d0, 0x41c440, 0x41c210, 0x41c2f0 |
| engine-behaviour.md §8.2 | "the native code sends `callback(player, 2, id, 0)`" on first pick-up | The weapon pick-up scripts send it; no native code dispatches a `callback` entry (the entry is read only by the three callback builtins and the unused kind-4 path of 0x41a200) | `items\ammo\i_*.scr` pc 19 (VERIFIED-DATA); scan of reads of script +0x74 |
| engine-behaviour.md §8.3 | `G_UseMissile`: "otherwise selects the next type that has missiles and returns 0" | With an empty selected count it returns 0 and changes nothing; the next type is selected when a use empties the count (that call returns 1). Same for power-ups | 0x40b220, 0x40b150 |
| engine-behaviour.md §8.4 and rcsl-builtins-table.md | `LockTarget` skips targets "already locked (runtime bit 0x100)"; uses "self's player" | It tests FL_NONTARGET (0x100 in field 3) instead; runtime bit 0x100 is set but never read; the "ahead" test uses the player of the **candidate's** player index | 0x40c06a..0x40c07a, 0x40c0a7 |
| engine-behaviour.md §4.3 | field 15 is pitch (nose up positive), field 14 is roll | With models facing row 1 (+y), field 14 is pitch (positive = nose down) and field 15 is roll/bank; field 16 heading is right (D7) | D6 subtracts 90°; player.scr field 14 = 30·vy/150; `MoveToNextWP` bank into field 15 |
| rcsl-builtins-table.md `RotateTo` | done when "all requested axes arrived within this step" | An axis counts as arrived only if its error was below 0.1° at the start of the call; a snap within one step is reported on the next call. The local frame uses a defective inverse (D8) | 0x41b88f..0x41b8a3, 0x41e7e5 |
| rcsl-builtins-table.md and hmap.md `RotateToNextWP` | turns toward "the path direction" / "the path heading" | Turns toward the position of the next waypoint (end of the current Bézier segment) in 2D; a snap within one step returns not-done | 0x406194..0x4061cf |
| rcsl-builtins-table.md `RadialDamage` | which operand divides which "is not settled" | distance / radius (DE F9 = FDIVP ST(1),ST(0), ST(1) holds the distance); engine-behaviour.md §6.2 is right | 0x41b6d5, 0x41b701 |
| rcsl-builtins-table.md `MoveToNextWP` | "when a new waypoint is reached, stores its delay into field 37" | Also: field 37 is set to 0 on every call that does not reach a waypoint, heading and bank are not updated on the call that reaches one, and a looping path's wrap reports no waypoint | 0x4060a8..0x4060d0, 0x406037 |
| rcsl-vm.md "Event dispatch", `init` row | projectiles created by `Shoot` go through 0x404fa0 | `Shoot` runs the projectile's `init` through 0x41a200 (kind 1) and its direct children's with an inline loop: the time since damage is not set to 2.0 and the children do not take the parent's player index | 0x41b273..0x41b36a |
| wpn.md `speed` | meaning GUESS | Initial projectile speed: `Shoot` sets velocity = normalised direction × speed; 0 leaves the projectile at rest for its script to move | 0x41b249..0x41b270 |
| engine-behaviour.md §6.2 `Lightning` | "every enemy with class 2 and bit 0x08" | Also requires field 4 = 0; does not test the removed bit | 0x41c060..0x41c091 |

## Symbols added to `re/symbols_v170.csv`

C runtime helpers identified while reading the builtins, named like the existing `sin`,
`cos`, `sqrt` rows: 0x440e40 `floor`, 0x440b1a `atan2`, 0x440c9c `exp`, 0x440d30 `acos`
(subsystem `crt`, confidence medium: identified by use; see the CSV evidence column). All builtins and engine helpers used here already had names.

## Changelog

- 1.0 (WP-23): first version. All 85 builtins specified; shared definitions D1–D9;
  priorities from a mission-1 reachability scan; corrections to engine-behaviour.md,
  rcsl-builtins-table.md, rcsl-vm.md, hmap.md and wpn.md listed, not applied.
