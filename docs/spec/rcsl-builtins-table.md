# RCSL builtin functions

Spec version 1.0. Machine-readable table: `testdata/golden/rcsl_builtins.json` (schema
below). Related: [rcsl-container.md](rcsl-container.md) (FUNC section, name resolution),
[rcsl-opcodes-v0.md](rcsl-opcodes-v0.md) (CALL, LCALL, TMO), [rcsl-vm.md](rcsl-vm.md)
(runtime).

Sources: the v1.70 executable (`objdump` of each implementation; Ghidra export for a few
engine helpers). Every arity and argument type below is **VERIFIED-CODE**: read from the
implementation's accesses through the argument pointer. Corpus counts and the call-site
cross-check are **VERIFIED-DATA** over the 339 shipped scripts. Descriptions of what
gameplay helpers do inside the engine are summaries; where a name or meaning is inferred it
says GUESS.

## Calling convention recap (VERIFIED-CODE)

- The engine table at 0x456f70 has 85 records `{char *name; void (*fn)(void)}`, terminated
  by a NULL name. The table index is the `index` column; scripts do not use it (they
  resolve by name through FUNC), but it is a stable identifier.
- A builtin takes no C arguments. It reads its script arguments through the argument
  pointer 0x1fa7df4, which points at frame slot t0 of the calling thread: argument *k* is
  the 4 bytes at `argptr + 4k`, i.e. slot t*k*.
- A result is returned by storing to the return register 0x1fa7dfc. Builtins with
  `returns: none` leave it untouched, so the CALL/LCALL destination receives whatever value
  the register held (stale).
- A latent-capable builtin writes the done flag 0x1fa7e00 (see LCALL in the opcodes spec).
- `self` below means the value of the `self` global (entity + 0x7B). A script field index
  *k* means the float at `self + 4k`. An "entity" value is such a reference (entity +
  0x7B); the 4 bytes at the reference (field 0) hold the raw entity pointer, which is how
  `remove`, `activate`, `Damage` etc. find the entity (VERIFIED-CODE, e.g. 0x41a985).

### Argument type vocabulary

| Type | Meaning |
|---|---|
| `float` | slot read as a float |
| `int` | slot read as a float and truncated toward zero (`_ftol2` or `fistp` with truncation) |
| `vec` | slot holds a pointer to 3 floats, read |
| `vec_out` | slot holds a pointer to 3 floats, written (also read where the row says "in place") |
| `entity` | slot holds an entity reference (as `self`, `other`, `create`'s result) |
| `string` | slot holds a STRG byte offset in its raw bits |
| `raw` | other raw 32-bit use (pointer to 9 floats for the axis functions; raw entity pointer for `getentity`) |

Return vocabulary: `none` (register untouched), `float`, `int_as_float` (an integer
converted to float, e.g. 0.0/1.0), `entity` (entity reference bits, or 0.0 when nothing).

## Table

Columns: corpus = scripts using it / CALL sites / LCALL sites.

| # | Name | Address | Same implementation as | Arity | Arguments | Returns | Done flag | Corpus | Confidence | Summary |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | `debug` | 0x41a350 | - | 0 | - | none | none | 0 / 0 / 0 | VERIFIED-CODE | Does nothing (empty function). |
| 1 | `RespawnPlayer` | 0x41c640 | - | 0 | - | none | none | 3 / 3 / 0 | VERIFIED-CODE | If self is player 1 or player 2 entity, respawns that player (0x40b2f0). |
| 2 | `EndLevel` | 0x407570 | - | 0 | - | none | none | 4 / 4 / 0 | VERIFIED-CODE | Sets the level-finished flags 0x4d52d6 and 0x458eab and calls 0x4269b0. |
| 3 | `random` | 0x41a360 | - | 0 | - | float | none | 79 / 134 / 0 | VERIFIED-CODE | Uniform value rand()/32767 in [0,1] from the C runtime LCG. |
| 4 | `crandom` | 0x41a380 | - | 0 | - | float | none | 15 / 41 / 0 | VERIFIED-CODE | (rand()/32767 - 0.5) * 2, in [-1,1]. |
| 5 | `sin` | 0x41a3b0 | - | 1 | deg:float | float | none | 66 / 98 / 0 | VERIFIED-CODE | sin of an angle in degrees. |
| 6 | `cos` | 0x41a3d0 | - | 1 | deg:float | float | none | 1 / 1 / 0 | VERIFIED-CODE | cos of an angle in degrees. |
| 7 | `tan` | 0x41a3f0 | - | 1 | deg:float | float | none | 0 / 0 / 0 | VERIFIED-CODE | tan of an angle in degrees. |
| 8 | `atan` | 0x41a410 | - | 1 | x:float | float | none | 0 / 0 / 0 | VERIFIED-CODE | QUIRK: returns atan(x * pi/180) in radians (input is converted as if it were degrees). |
| 9 | `abs` | 0x41a430 | - | 1 | x:float | float | none | 20 / 20 / 0 | VERIFIED-CODE | Absolute value. |
| 10 | `min` | 0x41a460 | - | 2 | a:float, b:float | float | none | 13 / 16 / 0 | VERIFIED-CODE | b if b <= a (or unordered) else a. |
| 11 | `max` | 0x41a4a0 | - | 2 | a:float, b:float | float | none | 3 / 6 / 0 | VERIFIED-CODE | b if b >= a (or unordered) else a. |
| 12 | `lerp` | 0x41a4e0 | - | 3 | t:float, a:float, b:float | float | none | 0 / 0 / 0 | VERIFIED-CODE | a + (b - a) * t. |
| 13 | `vec_copy` | 0x41a500 | - | 2 | src:vec, dst:vec_out | none | none | 14 / 15 / 0 | VERIFIED-CODE | dst = src. |
| 14 | `vec_add` | 0x41a520 | - | 3 | a:vec, b:vec, out:vec_out | none | none | 0 / 0 / 0 | VERIFIED-CODE | out = a + b. |
| 15 | `vec_sub` | 0x41a550 | - | 3 | a:vec, b:vec, out:vec_out | none | none | 57 / 64 / 0 | VERIFIED-CODE | out = a - b. |
| 16 | `vec_ma` | 0x41a580 | - | 4 | a:vec, s:float, b:vec, out:vec_out | none | none | 3 / 6 / 0 | VERIFIED-CODE | out = a + b * s. |
| 17 | `vec_length` | 0x41a5e0 | - | 1 | v:vec | float | none | 3 / 3 / 0 | VERIFIED-CODE | Euclidean length. |
| 18 | `vec_norm` | 0x41a630 | - | 1 | v:vec_out | float | none | 0 / 0 / 0 | VERIFIED-CODE | Normalises v in place (unchanged if zero); returns the length before normalisation. |
| 19 | `vec_scale` | 0x41a650 | - | 2 | v:vec_out, s:float | none | none | 8 / 8 / 0 | VERIFIED-CODE | v *= s in place. |
| 20 | `vec_setlen` | 0x41a680 | - | 2 | v:vec_out, len:float | none | none | 3 / 3 / 0 | VERIFIED-CODE | Rescales v in place to the given length (unchanged if zero). |
| 21 | `vec_toyaw` | 0x41a6a0 | - | 1 | v:vec | float | none | 0 / 0 / 0 | VERIFIED-CODE | Yaw of v in degrees: atan2(y,x) in [0,360) minus 90; QUIRK: -90 whenever x == 0. |
| 22 | `vec_toangles` | 0x41a730 | - | 2 | v:vec, out:vec_out | none | none | 0 / 0 / 0 | VERIFIED-CODE | out = (-pitch, 0, yaw - 90) in degrees. |
| 23 | `ClearAxis` | 0x41a750 | - | 1 | axis:raw | none | none | 3 / 3 / 0 | VERIFIED-CODE | Writes the 3x3 identity into 9 floats at the pointer. |
| 24 | `AnglesToAxis` | 0x41a780 | - | 2 | angles:vec, axis:raw | none | none | 8 / 8 / 0 | VERIFIED-CODE | Writes the rotation matrix (9 floats) for angles in degrees. |
| 25 | `create` | 0x41a7a0 | - | 2 | name:string, pos:vec | entity | none | 176 / 461 / 0 | VERIFIED-CODE | Spawns object definition `name` at pos with self's angles; runs its init handler; returns the new entity or 0.0. |
| 26 | `remove` | 0x41a980 | - | 1 | ent:entity | none | none | 228 / 269 / 0 | VERIFIED-CODE | Marks the entity and its attached children removed (flag 0x1) and detaches it. |
| 27 | `activate` | 0x41a9a0 | - | 1 | ent:entity | none | none | 1 / 1 / 0 | VERIFIED-CODE | Sets the active flag (0x4) on the entity and all its children. |
| 28 | `deactivate` | 0x41a9c0 | - | 1 | ent:entity | none | none | 4 / 5 / 0 | VERIFIED-CODE | Clears the active flag (0x4) on the entity and all its children. |
| 29 | `getentity` | 0x41a9e0 | - | 1 | ptr:raw | entity | none | 0 / 0 / 0 | VERIFIED-CODE | Converts a raw entity pointer to an entity reference (ptr + 0x7B). |
| 30 | `setskin` | 0x41aa00 | - | 2 | ent:entity, name:string | none | none | 0 / 0 / 0 | VERIFIED-CODE | Sets the entity skin (entity+0x157) from a skin name. |
| 31 | `callback` | 0x41ae80 | - | 4 | ent:entity, msg:float, parm1:float, parm2:float | none | none | 12 / 12 / 0 | VERIFIED-CODE | Sets cb_msg/cb_parm1/cb_parm2 and runs ent's callback handler synchronously. |
| 32 | `AttachEntity` | 0x41aa30 | - | 4 | child:entity, parent:entity, tag:string, flag:int | none | none | 8 / 11 / 0 | VERIFIED-CODE | Attaches child to parent at a named tag; flag stored as a boolean. |
| 33 | `AttachActivate` | 0x41aa90 | - | 1 | name:string | none | none | 59 / 86 / 0 | VERIFIED-CODE | Activates the first attached child of self with this name (if not removed). |
| 34 | `AttachDeactivate` | 0x41ab50 | - | 1 | name:string | none | none | 63 / 221 / 0 | VERIFIED-CODE | Deactivates the first attached child of self with this name (if not removed). |
| 35 | `AttachCallback` | 0x41ac10 | - | 4 | name:string, msg:float, parm1:float, parm2:float | none | none | 21 / 34 / 0 | VERIFIED-CODE | Runs the callback handler of the first attached child with this name, if active. |
| 36 | `ParentCallback` | 0x41ada0 | - | 3 | msg:float, parm1:float, parm2:float | none | none | 8 / 8 / 0 | VERIFIED-CODE | Runs the callback handler of self's parent entity. |
| 37 | `move` | 0x41af60 | - | 1 | vel:vec | none | none | 78 / 99 / 0 | VERIFIED-CODE | origin (fields 5..7) += vel * frametime. |
| 38 | `movex` | 0x41afb0 | - | 1 | speed:float | none | none | 4 / 6 / 0 | VERIFIED-CODE | field 5 (origin.x) += speed * frametime. |
| 39 | `movey` | 0x41afd0 | - | 1 | speed:float | none | none | 49 / 53 / 0 | VERIFIED-CODE | field 6 (origin.y) += speed * frametime. |
| 40 | `movez` | 0x41aff0 | - | 1 | speed:float | none | none | 63 / 64 / 0 | VERIFIED-CODE | field 7 (origin.z) += speed * frametime. |
| 41 | `rotate` | 0x41b010 | - | 1 | rate:vec | none | none | 4 / 4 / 0 | VERIFIED-CODE | angles (fields 14..16) += rate * frametime (degrees per second). |
| 42 | `rotatex` | 0x41b060 | - | 1 | rate:float | none | none | 19 / 20 / 0 | VERIFIED-CODE | field 14 += rate * frametime. |
| 43 | `rotatey` | 0x41b080 | - | 1 | rate:float | none | none | 20 / 31 / 0 | VERIFIED-CODE | field 15 += rate * frametime. |
| 44 | `rotatez` | 0x41b0a0 | - | 1 | rate:float | none | none | 96 / 108 / 0 | VERIFIED-CODE | field 16 += rate * frametime. |
| 45 | `sleep` | 0x41b0c0 | - | 0 | - | none | zero | 73 / 0 / 90 | VERIFIED-CODE | Clears the done flag; used with TMO + LCALL, the wait is the timeout. |
| 46 | `Shoot` | 0x41b0d0 | - | 3 | weapon:string, point:string, dir:vec | none | none | 73 / 379 / 0 | VERIFIED-CODE | Fires weapon definition `weapon` from the named point of self along dir; spawns projectile(s) and runs their init. |
| 47 | `Damage` | 0x41b550 | - | 2 | target:entity, amount:float | none | none | 77 / 93 / 0 | VERIFIED-CODE | Applies amount * g_damage_factor to target via 0x404ae0 (runs its damage handler). |
| 48 | `RadialDamage` | 0x41b5d0 | - | 3 | center:vec, radius:float, damage:float | none | none | 4 / 4 / 0 | VERIFIED-CODE | Damages entities within radius; amount scaled by distance, frametime and g_damage_factor. |
| 49 | `RotateTo` | 0x41b750 | lRotateTo | 2 | target:entity, axes:int | none | computed | 23 / 23 / 0 | VERIFIED-CODE | Turns self toward the target entity origin (axes bit 0x1 yaw, 0x2 pitch) at field-24 rate; done when all requested axes arrived. |
| 50 | `lRotateTo` | 0x41b750 | RotateTo | 2 | target:entity, axes:int | none | computed | 32 / 0 / 32 | VERIFIED-CODE | Same implementation as RotateTo. |
| 51 | `RotateToClamp` | 0x41b9c0 | lRotateToClamp | 4 | target:entity, axes:int, max:float, min:float | none | computed | 19 / 20 / 0 | VERIFIED-CODE | RotateTo, then clamps the turned angles into [min, max]. |
| 52 | `lRotateToClamp` | 0x41b9c0 | RotateToClamp | 4 | target:entity, axes:int, max:float, min:float | none | computed | 18 / 0 / 18 | VERIFIED-CODE | Same implementation as RotateToClamp. |
| 53 | `MoveToNextWP` | 0x41ba60 | lMoveToNextWP | 1 | align:int | none | computed | 20 / 20 / 0 | VERIFIED-CODE | Advances self along its waypoint path at field-23 speed; done on reaching a waypoint or path end. |
| 54 | `lMoveToNextWP` | 0x41ba60 | MoveToNextWP | 1 | align:int | none | computed | 28 / 0 / 34 | VERIFIED-CODE | Same implementation as MoveToNextWP. |
| 55 | `RotateToNextWP` | 0x41ba90 | lRotateToNextWP | 0 | - | none | computed | 0 / 0 / 0 | VERIFIED-CODE | Turns self yaw toward the path direction at field-24 rate; done when aligned. |
| 56 | `lRotateToNextWP` | 0x41ba90 | RotateToNextWP | 0 | - | none | computed | 6 / 0 / 6 | VERIFIED-CODE | Same implementation as RotateToNextWP. |
| 57 | `GetWaypointDelay` | 0x41bab0 | - | 1 | index:int | float | none | 1 / 1 / 0 | VERIFIED-CODE | Delay value of waypoint index (mod count) of self's path; 0.0 without a path. |
| 58 | `TerrainHeight` | 0x41baf0 | - | 2 | x:float, y:float | float | none | 35 / 44 / 0 | VERIFIED-CODE | Terrain height at (x, y) (0x416470). |
| 59 | `PlaceLight` | 0x41bb20 | - | 3 | pos:vec, color:vec, radius:float | none | none | 12 / 23 / 0 | VERIFIED-CODE | Adds a dynamic light for this frame (max 32); names of 2nd/3rd args GUESS. |
| 60 | `CameraQuake` | 0x41bb90 | - | 1 | amount:float | none | none | 11 / 12 / 0 | VERIFIED-CODE | Starts a camera shake of the given amount. |
| 61 | `LockTarget` | 0x41bb40 | - | 0 | - | entity | none | 3 / 3 / 0 | VERIFIED-CODE | Picks the nearest lockable enemy for self's player and marks it locked; entity or 0.0. |
| 62 | `IsValidTarget` | 0x41bb60 | - | 1 | ent:entity | int_as_float | none | 3 / 4 / 0 | VERIFIED-CODE | 1.0 if ent != 0, its field 4 == 0 and its health (field 34) > 0, else 0.0. |
| 63 | `StartSound` | 0x41bc50 | - | 1 | file:string | none | none | 58 / 116 / 0 | VERIFIED-CODE | Plays a sound (CASH kind 3 lookup) at self. |
| 64 | `StartLoopingSound` | 0x41bcd0 | - | 1 | file:string | none | none | 0 / 0 / 0 | VERIFIED-CODE | Stops self's looping sound and starts this one looping. |
| 65 | `StopLoopingSound` | 0x41bca0 | - | 0 | - | none | none | 3 / 3 / 0 | VERIFIED-CODE | Stops self's looping sound. |
| 66 | `TraceLine` | 0x41bd40 | - | 3 | from:vec, to:vec, mask:int | entity | none | 0 / 0 / 0 | VERIFIED-CODE | Segment test against players (mask bit 2) and entities (bit 1); first hit entity or 0.0. |
| 67 | `TraceLineDamage` | 0x41bea0 | - | 3 | from:vec, to:vec, damage:float | none | none | 2 / 2 / 0 | VERIFIED-CODE | Damages every player or entity (by self's side) crossed by the segment. |
| 68 | `Lightning` | 0x41c000 | - | 0 | - | none | none | 1 / 1 / 0 | VERIFIED-CODE | Damages and draws lightning to targetable entities within 500 units of self. |
| 69 | `PushPlayer` | 0x41c170 | - | 0 | - | none | none | 17 / 17 / 0 | VERIFIED-CODE | Pushes self's player horizontally away from self (4000 * frametime). |
| 70 | `G_AddPowerUp` | 0x41c210 | - | 2 | type:int, count:int | none | none | 4 / 4 / 0 | VERIFIED-CODE | Adds power-ups of a type (<16) to self's player, capped at 99. |
| 71 | `G_UsePowerUp` | 0x41c2d0 | - | 0 | - | int_as_float | none | 3 / 12 / 0 | VERIFIED-CODE | Uses the current power-up of self's player (0x40b150), returns its integer result. |
| 72 | `G_GetPowerUp` | 0x41c2b0 | - | 0 | - | int_as_float | none | 3 / 3 / 0 | VERIFIED-CODE | Current power-up type of self's player. |
| 73 | `G_AddMissiles` | 0x41c2f0 | - | 2 | type:int, count:int | none | none | 5 / 5 / 0 | VERIFIED-CODE | Adds missiles of a type (<5) to self's player, capped at 99. |
| 74 | `G_UseMissile` | 0x41c3b0 | - | 0 | - | int_as_float | none | 3 / 15 / 0 | VERIFIED-CODE | Uses a missile of self's player (0x40b220), returns its integer result. |
| 75 | `G_GetMissiles` | 0x41c390 | - | 0 | - | int_as_float | none | 3 / 3 / 0 | VERIFIED-CODE | Current missile type of self's player. |
| 76 | `G_GetUpgrade` | 0x41c3d0 | - | 1 | index:int | int_as_float | none | 13 / 16 / 0 | VERIFIED-CODE | Upgrade slot value (index < 20) of self's player, else 0.0. |
| 77 | `G_SetUpgrade` | 0x41c440 | - | 2 | index:int, value:int | none | none | 13 / 13 / 0 | VERIFIED-CODE | Sets upgrade slot (index < 20) of self's player. |
| 78 | `ShowTutorialHint` | 0x41c4c0 | - | 1 | text:string | none | none | 10 / 10 / 0 | VERIFIED-CODE | Shows a tutorial hint (0x42c000). |
| 79 | `PlayerFreezeHealth` | 0x41c4e0 | - | 1 | on:float | none | none | 2 / 4 / 0 | VERIFIED-CODE | Increments (on != 0) or decrements (on == 0) the health-freeze counter of self's player. |
| 80 | `PlayerDisableAction` | 0x41c560 | - | 1 | on:float | none | none | 2 / 3 / 0 | VERIFIED-CODE | Sets the action-disabled flag of self's player; when set, clears p_action. |
| 81 | `FreezeHealth` | 0x41c530 | - | 2 | ent:entity, on:float | none | none | 2 / 6 / 0 | VERIFIED-CODE | Sets (on != 0) or clears the invulnerability flag 0x10 of ent. |
| 82 | `SetFlag` | 0x41c5b0 | - | 2 | flags:int, bit:int | int_as_float | none | 1 / 4 / 0 | VERIFIED-CODE | Returns int(flags) \| int(bit) (pure). |
| 83 | `ClearFlag` | 0x41c5e0 | - | 2 | flags:int, bit:int | int_as_float | none | 1 / 4 / 0 | VERIFIED-CODE | Returns int(flags) & ~int(bit) (pure). |
| 84 | `SetModel` | 0x41c610 | - | 1 | name:string | none | none | 3 / 9 / 0 | VERIFIED-CODE | Sets self's model (entity+0x153) by name. |

72 of the 85 builtins are used by the shipped scripts (VERIFIED-DATA). The 13 unused ones
are `debug`, `tan`, `atan`, `lerp`, `vec_add`, `vec_norm`, `vec_toyaw`, `vec_toangles`,
`getentity`, `setskin`, `RotateToNextWP` (its twin `lRotateToNextWP` is used),
`StartLoopingSound` and `TraceLine`.

## Aliases: `RotateTo` versus `lRotateTo`

Four pairs share one implementation (VERIFIED-CODE, table records 0x4570f8..0x457130):
`RotateTo`/`lRotateTo` (0x41b750), `RotateToClamp`/`lRotateToClamp` (0x41b9c0),
`MoveToNextWP`/`lMoveToNextWP` (0x41ba60), `RotateToNextWP`/`lRotateToNextWP` (0x41ba90).
The function does **one step** of the motion per invocation (one frame's worth, scaled by
`frametime`) and reports in the done flag whether the motion has arrived. The difference
between the names is only how scripts call them (VERIFIED-DATA): the `l` names appear only
in LCALL (90 sites), which repeats the step every update until the flag says done; the plain
names appear only in CALL (63 sites), where one step is taken, the done flag is ignored,
and execution continues. A VM must therefore not treat the `l` prefix specially; the
behaviour follows from the opcode.

## Simple builtins: complete semantics (all VERIFIED-CODE)

Units: angles are **degrees** everywhere in the script interface (conversion factor
π/180 = 0x44b6f8, or π and 180 at 0x44b660/0x44b658). "× frametime" means multiplied by
the global `frametime` (0x1fb4bcc) at the time of the call, so rates are per unit of
frametime (per second if frametime is in seconds; see rcsl-vm.md). Nothing below is
additionally scaled.

### Math

- `random()` → `rand() * (1/32767)` as float, in [0, 1] inclusive (0x44b650 =
  1/32767). `rand` is the MSVC C runtime generator (0x42f593): a per-thread 32-bit linear
  congruential state updated as state × 214013 + 2531011, returning bits 16..30 of the new
  state (0..32767).
- `crandom()` → `(rand()/32767 − 0.5) × 2`, in [−1, 1]. The intermediate is rounded to
  float before the subtraction.
- `sin(d)`, `cos(d)`, `tan(d)` → the function of `d × π/180`.
- `atan(x)` → `atan(x × π/180)`, **in radians**. The input is converted as if it were an
  angle, the output is not converted. Unused by the shipped scripts; reproduce as is.
- `abs(x)` → |x|.
- `min(a, b)` → `b` if `b ≤ a` or the comparison is unordered, else `a`.
  `max(a, b)` → `b` if `b ≥ a` or unordered, else `a`.
- `lerp(t, a, b)` → `(b − a) × t + a` (argument order: t first).
- `SetFlag(f, b)` → `int(f) | int(b)`; `ClearFlag(f, b)` → `int(f) & ~int(b)`. Pure
  functions: nothing is stored, the caller assigns the result.

### Vectors (arguments are pointers to 3 floats; pointers may alias)

- `vec_copy(src, dst)`: dst = src, component by component in order x, y, z.
- `vec_add(a, b, out)`: out = a + b. `vec_sub(a, b, out)`: out = a − b. Computed and
  stored component by component, so `out` may alias `a` or `b`.
- `vec_ma(a, s, b, out)`: out = a + b × s (each component rounded to float).
- `vec_length(v)` → √(x² + y² + z²).
- `vec_norm(v)`: divides v by its length unless the length is 0; returns the length
  measured before normalising (the value left on the FPU by helper 0x41e220). Unused.
- `vec_scale(v, s)`: v ×= s. `vec_setlen(v, len)`: v ×= len / |v| unless |v| = 0.
  Neither writes the return register.
- `vec_toyaw(v)` → `yaw − 90` where yaw = atan2(y, x) in degrees, plus 360 if negative.
  QUIRK: if x = 0 exactly the result is −90 whatever y is. Unused.
- `vec_toangles(v, out)`: if x ≠ 0 or y ≠ 0: yaw = atan2(y, x)°, pitch =
  atan2(z, √(x²+y²))°, each +360 if negative; else yaw = 0 and pitch = 90 if z > 0,
  otherwise 270. Writes out = (−pitch, 0, yaw − 90). Unused.
- `ClearAxis(m)`: writes the 3×3 identity into the 9 floats at m (row-major; m[0], m[4],
  m[8] = 1).
- `AnglesToAxis(a, m)`: with sa, ca = sin, cos of a[0]; sb, cb of a[1]; sc, cc of a[2]
  (degrees): m = [cb·cc, cb·sc, sb, sa·sb·cc − ca·sc, sa·sb·sc + ca·cc, −sa·cb,
  −sa·sc − ca·sb·cc, sa·cc − ca·sb·sc, ca·cb]. Also stores the six sines and cosines in
  engine globals 0x1fdb478..0x1fdb48c (no script-visible effect).

### Movement of `self`

- `move(v)`: fields 5, 6, 7 (origin) += v × frametime.
- `movex(s)`, `movey(s)`, `movez(s)`: field 5, 6 or 7 += s × frametime.
- `rotate(v)`: fields 14, 15, 16 (angles, degrees) += v × frametime.
- `rotatex(r)`, `rotatey(r)`, `rotatez(r)`: field 14, 15 or 16 += r × frametime.

These change the fields only; they do no collision and no clamping or wrapping of angles.

### Entities

- `create(name, pos)`: looks `name` (STRG offset) up among the script's CASH entries of kind
  0 and uses the cached definition stored there; if absent or not cached, loads the
  definition by name (0x409860). Spawns it at the position `pos` points to (0x40a080).
  On failure returns 0.0 and does nothing else. On success: copies self's fields 14..16
  (angles) into the new entity, copies self's side/player index (entity+0x77) into it,
  writes the new entity reference to the return register, **runs the new entity's `init`
  handler and those of its attached children synchronously** (0x404fa0), sets up its
  model-dependent state, may increment a level counter (0x4d52f8) depending on its fields 2
  and 3, and registers it (0x405a60). QUIRK (VERIFIED-CODE): the reference is stored in
  the return register at 0x41a888, *before* `init` runs, and nothing saves the register
  around the nested handlers. If the new entity's `init` (or a child's) executes RET or
  calls any builtin that returns a value, the CALL that invoked `create` receives that
  value instead of the entity. A VM must reproduce this by sharing one return register.
- `remove(e)`: sets the removed flag (entity+0x1E bit 0) on e and recursively on its
  attached children, frees its particle emitter link, and if e was attached, decrements the
  attachment count of the root of its parent chain. The entity is not freed immediately
  (GUESS: freed later by the engine loop).
- `activate(e)` / `deactivate(e)`: set / clear the active flag (bit 2, value 0x4, of
  entity+0x1E) on e and recursively on all attached children, and reset e's emitter state.
- `getentity(p)` → `p + 0x7B`: turns a raw entity pointer into an entity reference.
  Unused.
- `setskin(e, name)`: e's skin (entity+0x157) = skin lookup of `name` (0x418cf0). Unused.
- `SetModel(name)`: self's model (entity+0x153) = model lookup of `name` (0x411e70).
- `AttachEntity(child, parent, tag, flag)`: if child's raw pointer is non-null: child
  parent link (entity+0x08) = parent's raw entity, child tag name (entity+0x0C) = the
  STRG string, child+0x10 = (int(flag) ≠ 0), child flags |= 0x24 (attached and active),
  child+0x11 = 1, and the root of the parent chain gets its attachment count (+0x12)
  incremented.
- `AttachActivate(name)` / `AttachDeactivate(name)`: walks self's children list
  (entity+0x1DB count, +0x1DF array) for the **first** child whose name (+0x16) equals
  `name` and that is not removed; activates it if inactive (resp. deactivates it if
  active), then stops. Removed children with the name are skipped.
- `AttachCallback(name, msg, p1, p2)`: finds the first child with that name (no removed
  check during the search). If that child is removed or inactive nothing happens. Else sets
  the globals `cb_msg` = msg, `cb_parm1` = p1, `cb_parm2` = p2 (as floats) and, if the child
  has a script thread, runs the child's `callback` entry synchronously with `self` switched
  to the child, saving and restoring the child's pc and t0..t15, then restores `self`,
  the current thread and the argument pointer. `cb_*` are **not** restored.
- `ParentCallback(msg, p1, p2)`: if self has a parent (entity+0x08): sets `cb_*` and runs the
  parent's `callback` entry the same way.
- `callback(e, msg, p1, p2)`: if e's raw pointer is non-null: sets `cb_*` (even if e has no
  thread) and runs e's `callback` entry the same way.
- `IsValidTarget(e)` → 1.0 if the reference e is non-zero, e's field 4 is 0 and e's
  field 34 (health) is > 0; else 0.0. Note it reads the fields directly through the
  reference, not through the raw pointer.

### Sound, terrain, waiting

- `StartSound(file)`: finds `file` among the CASH entries of kind 3 and plays the cached
  sound, or loads it by name (0x41ff90) if not cached; played at self (0x41fdf0, not
  looping).
- `StartLoopingSound(file)`: stops self's current looping sound (entity+0x73) if any, then
  plays `file` looping and remembers the handle in entity+0x73.
  `StopLoopingSound()`: stops it and clears the handle.
- `TerrainHeight(x, y)` → height of the terrain at (x, y) (0x416470).
- `sleep()`: stores 0 in the done flag and nothing else. `sleep` has no argument; the
  scripts always set the duration with TMO before `LCALL sleep` (VERIFIED-DATA, 90/90), so
  the wait length is the latent timeout. The value some scripts leave in t0 is ignored.
- `debug()`: returns immediately; no effect.

## Latent / waypoint builtins (signature and summary)

- `RotateTo(target, axes)` (0x41b750): target is an **entity**; the code reads its fields
  5..7 (origin). Transforms target.origin − self.origin into self's local frame (axis at
  entity+0x12B), converts to angles, and turns self by at most `field 24 × frametime`
  degrees per call: bit 0x1 of `int(axes)` turns field 16 (yaw), bit 0x2 turns field 14
  (pitch); differences are wrapped into (−180, 180]. Done flag = (set of axes that arrived
  within this step) == `int(axes)`. Does not write the return register.
- `RotateToClamp(target, axes, max, min)` (0x41b9c0): calls RotateTo, then for each
  selected axis clamps the angle: if angle > max then max, then if angle < min then min.
  Done flag as set by RotateTo.
- `MoveToNextWP(align)` (0x41ba60 → 0x405fe0): advances self along its waypoint path
  (entity+0x4B) by `field 23 × frametime`; when a new waypoint is reached, stores its delay
  into field 37 and signals done; at the end of a non-looping path, signals done and marks
  the path finished; if `int(align)` ≠ 0 sets field 16 to the path heading; may set field 15
  from field 25 (banking). Signals done immediately when there is no path.
- `RotateToNextWP()` (0x41ba90 → 0x406110): turns field 16 toward the path direction at
  `field 24 × frametime`; done when within 0.1 degree or when there is no path.
- `GetWaypointDelay(index)` (0x41bab0): 0.0 without a path, else the delay stored in path
  entry `int(index) mod count`.

## Complex gameplay builtins (signature, summary, touched state)

Semantics of these belong to later packages. Arity and argument types are VERIFIED-CODE.

| Builtin | Summary | Engine functions and globals touched |
|---|---|---|
| `Shoot(weapon, point, dir)` 0x41b0d0 | If self is a shooter (flag 0x8 in entity+0x1E, state ≠ 2): gets the world position of the named model point of self (0x4046a0), looks up the weapon definition (0x40c990), spawns its projectile (0x409ba0) with origin at the point and velocity = normalised dir × weapon speed, angles from dir, runs the projectile's `init` (0x41a200 kind 1) and its children's `init`; optionally spawns a second (muzzle) object attached to self. Applies g_damage_factor to projectiles of non-player shooters. | 0x4046a0, 0x40c990, 0x409ba0, 0x41e220, 0x41e4f0, 0x411e20, 0x41a200, 0x419be0, 0x405a60; globals 0x1fdb2c7, 0x1fa7dd8/0x1fa7ddc (players), 0x4577c0, 0x1ebe308, 0x1ebe479 |
| `Damage(target, amount)` 0x41b550 | Calls the damage routine 0x404ae0 on target with amount × g_damage_factor; the routine subtracts from health and runs target's `damage` handler. | 0x404ae0, 0x4577c0, 0x1fdb2c7 |
| `RadialDamage(center, radius, damage)` 0x41b5d0 | For every entity in the live list 0x458cc8 that is not removed, has field 2 = 2.0 (the value that marks enemy targets, also tested by `LockTarget`, `TraceLine`, `Lightning` and `create`'s counter) and health > 0, and lies within radius of center: applies damage × frametime × g_damage_factor scaled by the distance/radius ratio (which operand divides which is not settled here: GNU objdump's `fdivp` naming is ambiguous) via 0x404ae0. | 0x404ae0, 0x458cc8, 0x1fb4bcc, 0x4577c0 |
| `TraceLine(from, to, mask)` 0x41bd40 | Tests the segment against players (bit 0x2 of int(mask)) and against targetable entities (bit 0x1); returns the first hit as an entity, else 0.0. Unused. | 0x419540, 0x40ca10, 0x1ebe308 (players, stride 0x171, count 0x4577b8), 0x458cc8 |
| `TraceLineDamage(from, to, damage)` 0x41bea0 | Depending on self's side (entity+0x5B = 2: players, = 1: entities), applies `damage` (unscaled) via 0x404ae0 to everything the segment crosses. | 0x419540, 0x40ca10, 0x404ae0 |
| `LockTarget()` 0x41bb40 | Nearest lockable enemy to self's player (0x40c020), marked locked (flag 0x100); entity or 0.0. | 0x40c020, 0x458cc8, 0x1ebe308 |
| `PushPlayer()` 0x41c170 | Adds `4000 × frametime` × the horizontal unit vector from self to self's player to that player's fields 17..19 (velocity). | 0x1fa7dd8 (player globals), 0x41e220 |
| `Lightning()` 0x41c000 | No arguments. For every live entity with flag 0x8, field 4 = 0 and field 2 = 2.0 within 500 units of self's origin: draws a bolt (0x40de50) and applies self's field 35 × frametime × g_damage_factor via 0x404ae0. | 0x458cc8, 0x40de50, 0x404ae0 |
| `PlaceLight(pos, color, radius)` 0x41bb20 | Appends a dynamic light (max 32) with two vectors and a float to the light list 0x1f286a8 (0x40dd00). Argument names GUESS. | 0x40dd00, 0x1efe6a4 |
| `CameraQuake(amount)` 0x41bb90 | 0x1ebe5f0 = amount, 0x1fdb2d4 = 3.0, 0x1ebe654 = 0. | those globals |
| `EndLevel()` 0x407570 | Sets 0x4d52d6 and 0x458eab to 1 and calls 0x4269b0. 0x458eab also suppresses the per-frame `main` update (0x405a88). | 0x4269b0 |
| `RespawnPlayer()` 0x41c640 | If self is player 1's (resp. player 2's) entity, calls 0x40b2f0 with index 0 (resp. 1). | 0x1ebe308, 0x1ebe479, 0x40b2f0 |
| `ShowTutorialHint(text)` 0x41c4c0 | Tail-calls 0x42c000 with the string. | 0x42c000 |
| `PlayerFreezeHealth(on)` 0x41c4e0 | on ≠ 0: increments, on = 0: decrements a per-player counter (record + 0x1ebe3bc). | player record of self's index |
| `PlayerDisableAction(on)` 0x41c560 | Per-player byte (0x1ebe3c0) = (on ≠ 0); when set, the player's `p_action` = 0.0. | player record |
| `FreezeHealth(e, on)` 0x41c530 | Sets (on ≠ 0) or clears flag 0x10 of e (the damage routine skips entities with it). | entity+0x1E |
| `G_AddPowerUp(type, count)` 0x41c210 | type < 16: adds count to the player's power-up counter, capped at 99; selects it if none selected. | player record 0x1ebe3c1.. |
| `G_UsePowerUp()` 0x41c2d0, `G_GetPowerUp()` 0x41c2b0 | Use (0x40b150) / read the selected power-up of self's player; integer result as float. | player record |
| `G_AddMissiles(type, count)` 0x41c2f0 | type < 5: adds, capped at 99; selects if none selected. | player record 0x1ebe405.. |
| `G_UseMissile()` 0x41c3b0, `G_GetMissiles()` 0x41c390 | Use (0x40b220) / read the selected missile type. | player record |
| `G_GetUpgrade(i)` 0x41c3d0, `G_SetUpgrade(i, v)` 0x41c440 | Read / write upgrade slot i < 20 of self's player (integers); out of range reads 0.0 and writes nothing. | player record 0x1ebe41d.. |

"Self's player" is the player record selected by self's side index (entity+0x77, 0 or 1,
stride 0x171 from 0x1ebe308); see rcsl-vm.md for the selector.

## Builtins that re-enter the VM or touch VM state (VERIFIED-CODE)

- **Run other scripts' handlers synchronously**, switching the current-thread global
  0x1fa7de0, `self` 0x1fa7df0 and the argument pointer 0x1fa7df4 and restoring them
  afterwards: `create` (new entity's `init` and its children's), `Shoot` (projectile
  `init`, children, second object), `callback`, `AttachCallback`, `ParentCallback`
  (`callback` entry), and every builtin that reaches the damage routine 0x404ae0
  (`Damage`, `RadialDamage`, `TraceLineDamage`, `Lightning`: the target's `damage`
  handler). The target thread's pc and t0..t15 are saved and restored around its handler;
  the caller's frame is not touched.
- **Write frame slots**: only through pointer arguments (`vec_out`, the axis functions),
  which may point into the caller's frame (e.g. a vector variable). No builtin writes
  t0..t15 directly.
- **Write the return register**: exactly those with `returns` ≠ `none`.
- **Write the done flag**: `sleep` (0), the `RotateTo`, `RotateToClamp`, `MoveToNextWP`,
  `RotateToNextWP` families (computed). All others leave it as LCALL cleared it (0), so an
  LCALL of any other builtin only ends by timeout.
- **Set callback globals** `cb_msg`, `cb_parm1`, `cb_parm2`: the three callback builtins,
  without restoring them.

## Call-site cross-check (VERIFIED-DATA)

Method (`scratch` script, not shipped): for each script, a forward pass over each basic
block (blocks start at entry points, jump and call targets and after END/RET/jumps)
records for t0..t15 what kind of value was last written: string immediate, float
immediate, raw 0 (ambiguous: 0.0 or string offset 0), pointer (LEA result or a DATA
kind-3 vector variable), entity (`$self`, `$other`, `$player`, `$camera`, or the result of
a builtin returning an entity), float (arithmetic, comparisons, float globals, float
results), or unknown (loaded through a pointer or from a script variable). PUSH/POP are
simulated with a kind stack; a CALL to a script subroutine resets all kinds. At every
builtin call site each argument's kind is compared with the documented type
(float/int accept float or 0; string accepts string or 0; vec/vec_out/raw accept pointer;
entity accepts entity or 0; unknown accepts anything). A second pass flags temporaries at
or above the arity that are written right before the call and never read ("extra
arguments").

| | Count |
|---|---|
| builtin call sites | 2982 (2802 CALL, 180 LCALL) |
| agree | 2981 |
| disagree | 1 |
| argument slots checked | 4655 |
| of which positively typed | 4332 |
| of which unknown kind (accepted) | 323 |
| argument slots not written in the block | 0 |

Evidence per documented type (documented type, observed kind, count): entity/entity 484,
entity/unknown 21; float/float 657, float/zero 81, float/unknown 201; int/float 198,
int/zero 17, int/unknown 20; string/string 1377, string/zero 248, string/unknown 81;
vec/pointer 1163; vec_out/pointer 96; raw/pointer 11.

Disagreements:

| Builtin | Script | Instruction | Finding |
|---|---|---|---|
| `Lightning` | `scripts\bonuses\lightingbomb\lightingbomb_proj.scr` | 6 | The script passes two arguments (t0 = pointer to self.origin, t1 = raw 0, i.e. 0.0 or the string at offset 0 "PS_LBOMB_HIT"); the v1.70 implementation reads none and always uses self's origin. Harmless: the VM must simply not read them. |

The cross-check also corrected a first reading: `RotateTo`/`RotateToClamp` take an
**entity** as first argument, not a vector pointer (all 93 sites pass an entity value such
as `$player`, or a value of unknown kind; none passes a pointer; the code reads the
argument's fields 5..7). The table records the corrected,
code-verified type.

## JSON schema (`testdata/golden/rcsl_builtins.json`)

```
{"version": 1,
 "builtins": [ { "index": 0..84, "name": str, "address": "0x0041a360",
                 "same_impl_as": [names], "arity": int,
                 "args": [{"name": str, "type": float|int|vec|vec_out|entity|string|raw}],
                 "returns": none|float|int_as_float|entity|pointer,
                 "done_flag": none|zero|computed,
                 "corpus": {"scripts": int, "call": int, "lcall": int},
                 "callsites": {"total": int, "agree": int, "disagree": int},
                 "confidence": str, "summary": str } ... ] }
```

Ordered by engine table index; all 85 entries. `arity` is the number of argument slots the
implementation reads (fixed for every builtin; none reads a variable number).

## Changelog

- 1.0 (WP-21/22): first version; all 85 implementations read, corpus cross-checked.
