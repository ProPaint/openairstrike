# RCSL builtin functions: AirStrike 2 delta (`as2`)

Delta version 1.0. Amends [../rcsl-builtins-table.md](../rcsl-builtins-table.md) 1.0 for the
game `as2` (AirStrike 2 v2.51). Machine-readable table:
`testdata/golden/as2/rcsl_builtins.json` (schema of the base, see "JSON schema"). Checker:
`tools/ref/check_builtin_calls.py --game as2`. Related: [rcsl-vm.delta.md](rcsl-vm.delta.md).

Scope: signatures only (arity, argument kinds, return kind, done flag), for all 101
builtins. Behaviour is not described beyond the signature; where the code of an old builtin
is the same as in v1.70 this is stated as a fact of the comparison, not as a semantics
review. A later package writes the semantics delta.

The base spec has no section numbers; this delta mirrors its headings. VERIFIED-CODE cites
`as2@` and `v170@` addresses; VERIFIED-DATA holds for the 631 files of
`assets_extracted_games/as2/scripts`.

## Calling convention recap

Same (VERIFIED-CODE): builtins take no C arguments, read script argument k from the argument
pointer (as2@0x2106324) + 4k, i.e. slot t*k*, return through the single return register
(as2@0x210632c; `none` leaves it untouched), and the latent-capable ones write the done flag
(as2@0x2106330). An entity argument is a reference (entity + 0x84 in `as2`) whose first 4
bytes hold the raw entity pointer; builtins dereference it once, as in v1.70.

**Table**: as2@0x49d5d0, **101** records `{char *name; void (*fn)(void)}` of 8 bytes,
terminated by a NULL name at as2@0x49d8f8 (v1.70: 85 records at v170@0x456f70). The order
differs from v1.70: the 16 new builtins are inserted among the old ones, so the table index of
an old builtin changed for most of them. Scripts resolve builtins by name (FUNC section), so
the index is only an identifier; the `v1.70 #` column gives the old index.

### Argument type vocabulary

Same vocabulary and return vocabulary as the base.

## Table

How each signature was established (VERIFIED-CODE):

- Old builtins: the implementations of both executables were compared instruction by
  instruction with absolute addresses masked, then with the v1.70 structure offsets replaced
  by their AS2 values (rcsl-vm.delta.md). In addition the argument slots each implementation
  reads through the argument pointer, whether it writes the return register and whether it
  writes the done flag were extracted from both and compared. They agree for all 85 except
  `Lightning`.
- New builtins: read from their code (argument reads, conversions applied to each slot, what
  is stored in the return register, done flag).

Columns: `v1.70 # / address` is the base table's row; "Corpus" = `as2` scripts using it / CALL
sites / LCALL sites (VERIFIED-DATA; the `.sc` duplicate of a script counts as a script);
"Signature" compares arity, argument kinds, return kind and done flag with v1.70; "Code
against v1.70": `same` = identical instruction sequences apart from absolute addresses and
the moved entity and player-record offsets; `same logic` = small differences named in the
cell; `differs` = the logic differs (behaviour belongs to the semantics delta).

| # | Name | as2 address | v1.70 # / address | Same implementation as | Arity | Arguments | Returns | Done flag | Corpus | Signature | Code against v1.70 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | `debug` | 0x401040 | 0 / 0x41a350 | - | 0 | - | none | none | 0 / 0 / 0 | same | same logic (empty function) |
| 1 | `RespawnPlayer` | 0x421a50 | 1 / 0x41c640 | - | 0 | - | none | none | 6 / 6 / 0 | same | same |
| 2 | `EndLevel` | 0x40e730 | 2 / 0x407570 | - | 0 | - | none | none | 4 / 4 / 0 | same | differs |
| 3 | `GameOver` | 0x410e70 | new | - | 0 | - | none | none | 0 / 0 / 0 | new | - |
| 4 | `random` | 0x41f290 | 3 / 0x41a360 | - | 0 | - | float | none | 153 / 286 / 0 | same | same |
| 5 | `crandom` | 0x41f2b0 | 4 / 0x41a380 | - | 0 | - | float | none | 35 / 114 / 0 | same | same |
| 6 | `sin` | 0x41f2e0 | 5 / 0x41a3b0 | - | 1 | deg:float | float | none | 102 / 176 / 0 | same | same |
| 7 | `cos` | 0x41f300 | 6 / 0x41a3d0 | - | 1 | deg:float | float | none | 4 / 7 / 0 | same | same |
| 8 | `tan` | 0x41f320 | 7 / 0x41a3f0 | - | 1 | deg:float | float | none | 0 / 0 / 0 | same | same |
| 9 | `atan` | 0x41f340 | 8 / 0x41a410 | - | 1 | x:float | float | none | 0 / 0 / 0 | same | same logic (calls another C runtime arctangent entry) |
| 10 | `atan2` | 0x41f370 | new | - | 2 | y:float, x:float | float | none | 0 / 0 / 0 | new | - |
| 11 | `abs` | 0x41f3b0 | 9 / 0x41a430 | - | 1 | x:float | float | none | 44 / 44 / 0 | same | same |
| 12 | `min` | 0x41f3e0 | 10 / 0x41a460 | - | 2 | a:float, b:float | float | none | 16 / 22 / 0 | same | same |
| 13 | `max` | 0x41f420 | 11 / 0x41a4a0 | - | 2 | a:float, b:float | float | none | 6 / 12 / 0 | same | same |
| 14 | `lerp` | 0x41f460 | 12 / 0x41a4e0 | - | 3 | t:float, a:float, b:float | float | none | 0 / 0 / 0 | same | same |
| 15 | `copysign` | 0x41f480 | new | - | 2 | x:float, sign:float | float | none | 0 / 0 / 0 | new | - |
| 16 | `floor` | 0x41f4b0 | new | - | 1 | x:float | float | none | 0 / 0 / 0 | new | - |
| 17 | `floor2` | 0x41f4e0 | new | - | 2 | x:float, step:float | float | none | 0 / 0 / 0 | new | - |
| 18 | `fmod` | 0x41f510 | new | - | 2 | x:float, y:float | float | none | 0 / 0 / 0 | new | - |
| 19 | `vec_copy` | 0x41f530 | 13 / 0x41a500 | - | 2 | src:vec, dst:vec_out | none | none | 32 / 37 / 0 | same | same |
| 20 | `vec_add` | 0x41f550 | 14 / 0x41a520 | - | 3 | a:vec, b:vec, out:vec_out | none | none | 0 / 0 / 0 | same | same |
| 21 | `vec_sub` | 0x41f580 | 15 / 0x41a550 | - | 3 | a:vec, b:vec, out:vec_out | none | none | 120 / 137 / 0 | same | same |
| 22 | `vec_ma` | 0x41f5b0 | 16 / 0x41a580 | - | 4 | a:vec, s:float, b:vec, out:vec_out | none | none | 6 / 12 / 0 | same | same |
| 23 | `vec_length` | 0x41f610 | 17 / 0x41a5e0 | - | 1 | v:vec | float | none | 6 / 12 / 0 | same | same |
| 24 | `vec_norm` | 0x41f660 | 18 / 0x41a630 | - | 1 | v:vec_out | float | none | 1 / 2 / 0 | same | same |
| 25 | `vec_scale` | 0x41f680 | 19 / 0x41a650 | - | 2 | v:vec_out, s:float | none | none | 9 / 11 / 0 | same | same |
| 26 | `vec_setlen` | 0x41f6b0 | 20 / 0x41a680 | - | 2 | v:vec_out, len:float | none | none | 6 / 6 / 0 | same | same |
| 27 | `vec_toyaw` | 0x41f6d0 | 21 / 0x41a6a0 | - | 1 | v:vec | float | none | 0 / 0 / 0 | same | same |
| 28 | `vec_toangles` | 0x41f760 | 22 / 0x41a730 | - | 2 | v:vec, out:vec_out | none | none | 0 / 0 / 0 | same | same |
| 29 | `ClearAxis` | 0x41f780 | 23 / 0x41a750 | - | 1 | axis:raw | none | none | 5 / 5 / 0 | same | same |
| 30 | `AnglesToAxis` | 0x41f7b0 | 24 / 0x41a780 | - | 2 | angles:vec, axis:raw | none | none | 14 / 19 / 0 | same | same |
| 31 | `create` | 0x41f7d0 | 25 / 0x41a7a0 | - | 2 | name:string, pos:vec | entity | none | 320 / 1124 / 0 | same | differs: writes the return register after the init of the new entity and update instead of before (see rcsl-vm.delta.md, quirk 9) |
| 32 | `remove` | 0x41f9c0 | 26 / 0x41a980 | - | 1 | ent:entity | none | none | 371 / 450 / 0 | same | same |
| 33 | `activate` | 0x41f9e0 | 27 / 0x41a9a0 | - | 1 | ent:entity | none | none | 5 / 8 / 0 | same | same |
| 34 | `deactivate` | 0x41fa00 | 28 / 0x41a9c0 | - | 1 | ent:entity | none | none | 22 / 30 / 0 | same | same |
| 35 | `getentity` | 0x41fa20 | 29 / 0x41a9e0 | - | 1 | ptr:raw | entity | none | 0 / 0 / 0 | same | same |
| 36 | `setskin` | 0x41fa40 | 30 / 0x41aa00 | - | 2 | ent:entity, name:string | none | none | 0 / 0 / 0 | same | same logic (register allocation and the skin offset moved) |
| 37 | `callback` | 0x41ff40 | 31 / 0x41ae80 | - | 4 | ent:entity, msg:float, parm1:float, parm2:float | none | none | 13 / 16 / 0 | same | same |
| 38 | `AttachEntity` | 0x41fa70 | 32 / 0x41aa30 | - | 4 | child:entity, parent:entity, tag:string, flag:int | none | none | 10 / 17 / 0 | same | differs |
| 39 | `DetachEntity` | 0x41fae0 | new | - | 1 | ent:entity | none | none | 1 / 3 / 0 | new | - |
| 40 | `AttachActivate` | 0x41fb50 | 33 / 0x41aa90 | - | 1 | name:string | none | none | 144 / 189 / 0 | same | same |
| 41 | `AttachDeactivate` | 0x41fc10 | 34 / 0x41ab50 | - | 1 | name:string | none | none | 154 / 487 / 0 | same | same |
| 42 | `AttachCallback` | 0x41fcd0 | 35 / 0x41ac10 | - | 4 | name:string, msg:float, parm1:float, parm2:float | none | none | 55 / 56 / 0 | same | same |
| 43 | `ParentCallback` | 0x41fe60 | 36 / 0x41ada0 | - | 3 | msg:float, parm1:float, parm2:float | none | none | 7 / 7 / 0 | same | same |
| 44 | `move` | 0x420020 | 37 / 0x41af60 | - | 1 | vel:vec | none | none | 116 / 162 / 0 | same | same |
| 45 | `movex` | 0x420070 | 38 / 0x41afb0 | - | 1 | speed:float | none | none | 2 / 2 / 0 | same | same |
| 46 | `movey` | 0x420090 | 39 / 0x41afd0 | - | 1 | speed:float | none | none | 73 / 80 / 0 | same | same |
| 47 | `movez` | 0x4200b0 | 40 / 0x41aff0 | - | 1 | speed:float | none | none | 84 / 87 / 0 | same | same |
| 48 | `rotate` | 0x4200d0 | 41 / 0x41b010 | - | 1 | rate:vec | none | none | 6 / 6 / 0 | same | same |
| 49 | `rotatex` | 0x420120 | 42 / 0x41b060 | - | 1 | rate:float | none | none | 52 / 53 / 0 | same | same |
| 50 | `rotatey` | 0x420140 | 43 / 0x41b080 | - | 1 | rate:float | none | none | 25 / 28 / 0 | same | same |
| 51 | `rotatez` | 0x420160 | 44 / 0x41b0a0 | - | 1 | rate:float | none | none | 140 / 161 / 0 | same | same |
| 52 | `sleep` | 0x420180 | 45 / 0x41b0c0 | - | 0 | - | none | zero | 165 / 0 / 200 | same | same |
| 53 | `Shoot` | 0x420190 | 46 / 0x41b0d0 | - | 3 | weapon:string, point:string, dir:vec | none | none | 146 / 1092 / 0 | same | differs |
| 54 | `Damage` | 0x420630 | 47 / 0x41b550 | - | 2 | target:entity, amount:float | none | none | 117 / 158 / 0 | same | same |
| 55 | `RadialDamage` | 0x4206b0 | 48 / 0x41b5d0 | - | 3 | center:vec, radius:float, damage:float | none | none | 5 / 5 / 0 | same | differs |
| 56 | `RadialDamagePlayer` | 0x420840 | new | - | 3 | center:vec, radius:float, damage:float | none | none | 1 / 1 / 0 | new | - |
| 57 | `RotateTo` | 0x4209b0 | 49 / 0x41b750 | lRotateTo | 2 | target:entity, axes:int | none | computed | 31 / 31 / 0 | same | differs: returns at once (done flag 0) when the target or self is null |
| 58 | `lRotateTo` | 0x4209b0 | 50 / 0x41b750 | RotateTo | 2 | target:entity, axes:int | none | computed | 70 / 0 / 70 | same | differs: returns at once (done flag 0) when the target or self is null |
| 59 | `RotateToClamp` | 0x420c20 | 51 / 0x41b9c0 | lRotateToClamp | 4 | target:entity, axes:int, max:float, min:float | none | computed | 59 / 60 / 0 | same | same |
| 60 | `lRotateToClamp` | 0x420c20 | 52 / 0x41b9c0 | RotateToClamp | 4 | target:entity, axes:int, max:float, min:float | none | computed | 58 / 0 / 58 | same | same |
| 61 | `MoveToNextWP` | 0x420cc0 | 53 / 0x41ba60 | lMoveToNextWP | 1 | align:int | none | computed | 58 / 59 / 0 | same | same |
| 62 | `lMoveToNextWP` | 0x420cc0 | 54 / 0x41ba60 | MoveToNextWP | 1 | align:int | none | computed | 80 / 0 / 92 | same | same |
| 63 | `RotateToNextWP` | 0x420cf0 | 55 / 0x41ba90 | lRotateToNextWP | 0 | - | none | computed | 0 / 0 / 0 | same | same |
| 64 | `lRotateToNextWP` | 0x420cf0 | 56 / 0x41ba90 | RotateToNextWP | 0 | - | none | computed | 18 / 0 / 18 | same | same |
| 65 | `GetWaypointDelay` | 0x420d10 | 57 / 0x41bab0 | - | 1 | index:int | float | none | 1 / 1 / 0 | same | same logic (waypoint path record offsets moved) |
| 66 | `TerrainHeight` | 0x420d50 | 58 / 0x41baf0 | - | 2 | x:float, y:float | float | none | 114 / 141 / 0 | same | same |
| 67 | `WaterHeight` | 0x420d80 | new | - | 2 | x:float, y:float | float | none | 0 / 0 / 0 | new | - |
| 68 | `PlaceLight` | 0x420db0 | 59 / 0x41bb20 | - | 3 | pos:vec, color:vec, radius:float | none | none | 14 / 41 / 0 | same | same |
| 69 | `CameraQuake` | 0x420e20 | 60 / 0x41bb90 | - | 1 | amount:float | none | none | 43 / 49 / 0 | same | same |
| 70 | `LockTarget` | 0x420dd0 | 61 / 0x41bb40 | - | 0 | - | entity | none | 5 / 5 / 0 | same | same |
| 71 | `IsValidTarget` | 0x420df0 | 62 / 0x41bb60 | - | 1 | ent:entity | int_as_float | none | 5 / 8 / 0 | same | same |
| 72 | `StartSound` | 0x420ee0 | 63 / 0x41bc50 | - | 1 | file:string | none | none | 91 / 265 / 0 | same | same |
| 73 | `StartLoopingSound` | 0x420f60 | 64 / 0x41bcd0 | - | 1 | file:string | none | none | 0 / 0 / 0 | same | same |
| 74 | `StopLoopingSound` | 0x420f30 | 65 / 0x41bca0 | - | 0 | - | none | none | 6 / 6 / 0 | same | same |
| 75 | `TraceLine` | 0x420fd0 | 66 / 0x41bd40 | - | 3 | from:vec, to:vec, mask:int | entity | none | 0 / 0 / 0 | same | differs |
| 76 | `TraceLineDamage` | 0x421180 | 67 / 0x41bea0 | - | 3 | from:vec, to:vec, damage:float | none | none | 2 / 2 / 0 | same | differs |
| 77 | `Lightning` | 0x421310 | 68 / 0x41c000 | - | 1 | radius:float | none | none | 2 / 2 / 0 | CHANGED | differs |
| 78 | `PushPlayer` | 0x4214a0 | 69 / 0x41c170 | - | 0 | - | none | none | 45 / 45 / 0 | same | same |
| 79 | `G_AddPowerUp` | 0x421550 | 70 / 0x41c210 | - | 2 | type:int, count:int | none | none | 11 / 11 / 0 | same | differs |
| 80 | `G_UsePowerUp` | 0x421620 | 71 / 0x41c2d0 | - | 0 | - | int_as_float | none | 6 / 42 / 0 | same | same logic (register allocation only) |
| 81 | `G_GetPowerUp` | 0x421600 | 72 / 0x41c2b0 | - | 0 | - | int_as_float | none | 6 / 6 / 0 | same | same |
| 82 | `G_SetPowerUpCount` | 0x421650 | new | - | 2 | type:int, count:int | none | none | 3 / 3 / 0 | new | - |
| 83 | `G_AddMissiles` | 0x4216c0 | 73 / 0x41c2f0 | - | 2 | type:int, count:int | none | none | 5 / 5 / 0 | same | differs |
| 84 | `G_UseMissile` | 0x421790 | 74 / 0x41c3b0 | - | 0 | - | int_as_float | none | 6 / 30 / 0 | same | same |
| 85 | `G_GetMissiles` | 0x421770 | 75 / 0x41c390 | - | 0 | - | int_as_float | none | 6 / 6 / 0 | same | same |
| 86 | `G_GetUpgrade` | 0x4217c0 | 76 / 0x41c3d0 | - | 1 | index:int | int_as_float | none | 16 / 16 / 0 | same | differs: upgrade index bound is 9 instead of 20 |
| 87 | `G_SetUpgrade` | 0x421830 | 77 / 0x41c440 | - | 2 | index:int, value:int | none | none | 10 / 10 / 0 | same | differs: upgrade index bound is 9 instead of 20 |
| 88 | `ShowTutorialHint` | 0x4218b0 | 78 / 0x41c4c0 | - | 1 | text:string | none | none | 10 / 10 / 0 | same | same |
| 89 | `PlayerFreezeHealth` | 0x4218d0 | 79 / 0x41c4e0 | - | 1 | on:float | none | none | 2 / 4 / 0 | same | same |
| 90 | `PlayerDisableAction` | 0x421960 | 80 / 0x41c560 | - | 1 | on:float | none | none | 2 / 3 / 0 | same | same |
| 91 | `FreezeHealth` | 0x421930 | 81 / 0x41c530 | - | 2 | ent:entity, on:float | none | none | 0 / 0 / 0 | same | same |
| 92 | `SetFlag` | 0x4219c0 | 82 / 0x41c5b0 | - | 2 | flags:int, bit:int | int_as_float | none | 0 / 0 / 0 | same | same |
| 93 | `ClearFlag` | 0x4219f0 | 83 / 0x41c5e0 | - | 2 | flags:int, bit:int | int_as_float | none | 0 / 0 / 0 | same | same |
| 94 | `SetModel` | 0x421a20 | 84 / 0x41c610 | - | 1 | name:string | none | none | 6 / 18 / 0 | same | same |
| 95 | `TerraMorph` | 0x421a90 | new | - | 2 | pos:vec, name:string | none | none | 7 / 8 / 0 | new | - |
| 96 | `IsMultiplayer` | 0x421ab0 | new | - | 0 | - | int_as_float | none | 0 / 0 / 0 | new | - |
| 97 | `IsPlayerInGame` | 0x421ae0 | new | - | 1 | index:int | int_as_float | none | 0 / 0 / 0 | new | - |
| 98 | `GetMapPosOfs` | 0x421b20 | new | - | 0 | - | float | none | 0 / 0 / 0 | new | - |
| 99 | `GetPlayerAccel` | 0x421b40 | new | - | 1 | out:vec_out | none | none | 6 / 6 / 0 | new | - |
| 100 | `GetPlayersDistance` | 0x421ba0 | new | - | 0 | - | float | none | 0 / 0 / 0 | new | - |

Summary (VERIFIED-CODE unless marked):

- **Same signature**: 84 of the 85 v1.70 builtins. 66 of them have the same code as v1.70
  (in the sense above), 5 the same logic, 14 a different implementation (`EndLevel`,
  `create`, `AttachEntity`, `Shoot`, `RadialDamage`, `RotateTo`/`lRotateTo`, `TraceLine`,
  `TraceLineDamage`, `Lightning`, `G_AddPowerUp`, `G_AddMissiles`, `G_GetUpgrade`,
  `G_SetUpgrade`).
- **Changed signature**: `Lightning` (index 77, as2@0x421310; v170@0x41c000) now reads t0 as a
  float and compares it with the horizontal distance to each candidate (as2@0x4213f5..0x421404);
  v1.70 read no argument and used a fixed distance. Arity 0 → 1. Both `as2` call sites pass
  it (`MOV t0 = #500.0` in `bonuses\lightingbomb\lightingbomb_proj.scr`, `#200.0` in
  `weapons\lightinggun\lightinggun_proj.scr`). The v1.70 corpus's call already put a value in
  t0 (base table, "Disagreements").
- **New builtins (16)**, signatures read from the code:

| Name | as2 address | Signature | Evidence |
|---|---|---|---|
| `GameOver` | 0x410e70 | no argument, returns none | no access to the argument pointer or the return register |
| `atan2` | 0x41f370 | (y:float, x:float) → float | loads t0 then t1 onto the FPU and calls the C runtime two-argument arctangent (as2@0x41f383), scales and stores the result (as2@0x41f39c) |
| `copysign` | 0x41f480 | (x:float, sign:float) → float | t0 and t1 passed as doubles to the C runtime `_copysign` (as2@0x41f494) |
| `floor` | 0x41f4b0 | (x:float) → float | t0 passed to the C runtime `floor` (as2@0x41f4c6) |
| `floor2` | 0x41f4e0 | (x:float, step:float) → float | reads t0 (twice) and t1, calls the C runtime `fmod` and subtracts (as2@0x41f4f7..0x41f506) |
| `fmod` | 0x41f510 | (x:float, y:float) → float | t0, t1 to the C runtime `fmod` (as2@0x41f51b) |
| `DetachEntity` | 0x41fae0 | (ent:entity) → none | t0 dereferenced to the raw entity (as2@0x41fae7); nothing for a null entity |
| `RadialDamagePlayer` | 0x420840 | (center:vec, radius:float, damage:float) → none | same argument reads as `RadialDamage` (as2@0x4206b0), which it copies except that it loops over the player records instead of the entity list |
| `WaterHeight` | 0x420d80 | (x:float, y:float) → float | t0, t1 passed to as2@0x41d530, result stored (as2@0x420d99); same shape as `TerrainHeight` |
| `G_SetPowerUpCount` | 0x421650 | (type:int, count:int) → none | t1 truncated (as2@0x42165d; negative becomes 0), t0 truncated (as2@0x421698), stored into self's player record |
| `TerraMorph` | 0x421a90 | (pos:vec, name:string) → none | t1 added to the thread's STRG pointer (as2@0x421aa1); t0 passed on as a pointer whose floats as2@0x41a670 reads |
| `IsMultiplayer` | 0x421ab0 | no argument → int_as_float | 1.0 or 0.0 from a byte flag (as2@0x421ab1) |
| `IsPlayerInGame` | 0x421ae0 | (index:int) → int_as_float | t0 truncated (as2@0x421ae8) and used as a player index; 1.0 or 0.0 |
| `GetMapPosOfs` | 0x421b20 | no argument → float | float read from a table indexed by the camera mode (as2@0x421b28) |
| `GetPlayerAccel` | 0x421b40 | (out:vec_out) → none | t0 is a pointer; 3 floats written through it (as2@0x421b5d..0x421b6e, 0x421b89..0x421b9a) only when self is one of the two player entities; the return register is not written |
| `GetPlayersDistance` | 0x421ba0 | no argument → float | always writes the return register (as2@0x421bf8, 0x421c16, 0x421c23) |

None of the new builtins writes the done flag (their code never stores to as2@0x2106330), so
an LCALL of one of them ends only by its timeout, as for most old ones.

`as2` scripts use 75 distinct builtins: 70 old ones (the 72 of the v1.70 scripts minus
`FreezeHealth`, `SetFlag`, `ClearFlag`, plus `vec_norm`) and 5 new ones. Unused new builtins: `GameOver`, `atan2`, `copysign`, `floor`,
`floor2`, `fmod`, `WaterHeight`, `IsMultiplayer`, `IsPlayerInGame`, `GetMapPosOfs`,
`GetPlayersDistance` (VERIFIED-DATA).

## Aliases: `RotateTo` versus `lRotateTo`

Same four pairs sharing one implementation (table records as2@0x49d798..0x49d7d0:
`RotateTo`/`lRotateTo` as2@0x4209b0, `RotateToClamp`/`lRotateToClamp` as2@0x420c20,
`MoveToNextWP`/`lMoveToNextWP` as2@0x420cc0, `RotateToNextWP`/`lRotateToNextWP`
as2@0x420cf0). Same use in the data: the `l` names only in LCALL (70 + 58 + 92 + 18 sites),
the plain names only in CALL (31 + 60 + 59 + 0 sites) (VERIFIED-DATA).

## Simple builtins: complete semantics

Not checked as semantics. For the builtins whose code is the same as v1.70 (column "Code
against v1.70" = `same`) the base description applies unchanged, because the code is the
same. The math, vector and movement builtins are all in that group; `atan` calls another C
runtime entry for the arctangent.

## Latent / waypoint builtins (signature and summary)

Signatures same. `MoveToNextWP`, `RotateToClamp`, `RotateToNextWP`, `GetWaypointDelay` (path
record offsets aside): same code. `RotateTo` differs (it now returns at once with the done
flag 0 when t0 is 0 or self has no raw entity, as2@0x4209e3..0x4209f9, and stores the done
flag directly); not checked further.

## Complex gameplay builtins (signature, summary, touched state)

Signatures same (VERIFIED-CODE, see above); summaries not checked. Implementations that differ:
see the table.

## Builtins that re-enter the VM or touch VM state

- Run other scripts' handlers synchronously: the v1.70 list, plus `RadialDamagePlayer`
  (through the damage routine as2@0x40b9e0) (VERIFIED-CODE: its only callee besides the C
  runtime is that routine).
- Write frame slots: through pointer arguments only; new: `GetPlayerAccel` (`vec_out`).
  No builtin writes t0..t15 directly (VERIFIED-CODE for the new ones: they store only
  through t0's pointer or to engine data).
- Write the return register: exactly those with `returns` ≠ `none` (both tables). `create`
  now writes it last (rcsl-vm.delta.md, quirk 9).
- Write the done flag: `sleep` (0) and the four `RotateTo`/`RotateToClamp`/`MoveToNextWP`/
  `RotateToNextWP` families, as in v1.70.
- Set callback globals: the three callback builtins, as in v1.70.

## Call-site cross-check

Replaced for `as2` by an arity check (the base's kind check was a scratch script that was not
kept). `tools/ref/check_builtin_calls.py` computes, for every CALL and LCALL of a builtin, how
many argument slots t0, t1, ... are certainly written since the previous call on every path
to the call (a must-analysis over the script's control flow; see the tool's header), and
fails if a call passes fewer than the table's arity. More is allowed (stale slots written for
another purpose count as written, so this is a lower-bound check).

| Game | Scripts | Builtin call sites | Distinct builtins | Sites with fewer slots than the arity | Result |
|---|---|---|---|---|---|
| `as3d` (base table) | 339 | 2,982 (the base's count) | 72 | 0 | OK |
| `as2` (this table) | 631 | 6,465 | 75 | 0 | OK |
| `gulf` (this table, as a cross-check) | 665 | 6,322 | 86 | 0 | OK |

Sensitivity: raising the arity of `Shoot`, `Lightning` and `TerraMorph` by one in a copy of the
table makes the `as2` run fail on exactly their 1,092 + 2 + 8 = 1,102 sites.

In the JSON, `callsites` now holds this arity check: `total` = sites, `agree` = sites that pass
at least `arity` slots, `disagree` = sites that pass fewer (0 everywhere).

Kind spot checks of the new and changed builtins at their call sites (VERIFIED-DATA, by
reading the sites): `TerraMorph` gets `t0 = &self[5]` and a string in t1; `G_SetPowerUpCount`
gets an immediate type in t0 and a computed float in t1; `GetPlayerAccel` gets a vector
variable's pointer; `RadialDamagePlayer` gets `&self[5]`, a float and `#2000.0`;
`DetachEntity` gets an entity held in a script variable; `Lightning` gets a float immediate.

## JSON schema

Same schema as the base (`testdata/golden/rcsl_builtins.json`), 101 entries in engine table
order, `index` 0..100. Additions, all optional for readers of the base schema:

```
{"version": 1, "game": "as2", "table_address": "0x0049d5d0",
 "builtins": [ { ...base fields...,
                 "v170": null | {"index": int, "address": "0x0041a360",
                                 "signature": "same"|"changed",
                                 "code_identical": bool} } ... ] }
```

`v170` is null for the 16 new builtins. `code_identical` is true for the `same` and
`same logic` rows of the table above. `summary` states the signature status and the code
comparison only.

## What an implementer must change

Against the builtin table as the base spec describes it:

1. Register the 101 `as2` builtins by name (resolution is by name; keep the index only as an
   identifier).
2. `Lightning` reads one float argument (t0) in `as2`.
3. Add the 16 new builtins with the signatures above; semantics come from the semantics
   delta. Until then, an implementation may stub the unused ones (11 of the 16 are not
   called by `as2` scripts).
4. `create` must return the new entity even if its `init` writes the return register
   (rcsl-vm.delta.md).
5. Nothing else changes in the calling convention, aliases or done-flag behaviour.

## Checked sections

| Base section | Status |
|---|---|
| Calling convention recap | same (table address and size changed: 101 records at as2@0x49d5d0) |
| Argument type vocabulary | same |
| Table | changed (101 rows, new order, 16 new, `Lightning` arity 0 → 1) |
| Aliases: `RotateTo` versus `lRotateTo` | same |
| Simple builtins: complete semantics | not checked as semantics (same code for the rows marked `same`) |
| Latent / waypoint builtins | signatures same; semantics not checked |
| Complex gameplay builtins | signatures same; semantics not checked |
| Builtins that re-enter the VM or touch VM state | changed (additions above) |
| Call-site cross-check | changed (arity check by `check_builtin_calls.py`) |
| JSON schema | changed (optional fields `game`, `table_address`, `v170`) |

## Changelog

- 1.0 (B2): first version.
