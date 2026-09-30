# RCSL builtin functions: Gulf Thunder delta (`gulf`)

Delta version 1.0 (package F1). Amends [../as2/rcsl-builtins-table.delta.md](../as2/rcsl-builtins-table.delta.md)
(which amends [../rcsl-builtins-table.md](../rcsl-builtins-table.md)) for the game `gulf`
(AirStrike II: Gulf Thunder v2.71). Machine-readable table:
`testdata/golden/gulf/rcsl_builtins.json` (the AirStrike 2 schema plus an `as2` object per
builtin: `index`, `address`, `signature`, `code_identical`). Checker:
`tools/ref/check_builtin_calls.py --game gulf` (it now reads Gulf Thunder's own table).
Behaviour: [rcsl-builtins-semantics.delta.md](rcsl-builtins-semantics.delta.md).

VERIFIED-CODE cites `gulf@` and `as2@`; VERIFIED-DATA holds for the 665 scripts of
`assets_extracted_games/gulf/scripts`.

## Calling convention recap

Same code as AirStrike 2 (argument pointer gulf@0x0210416c, return register gulf@0x02104174,
done flag gulf@0x02104178; [rcsl-vm.delta.md](rcsl-vm.delta.md)).

**Table**: gulf@0x0049b448, **101** records of 8 bytes `{char *name; void (*fn)(void)}`, the
same names in the same order as as2@0x0049d5d0, 97 distinct functions (the same four pairs of
names share a function). VERIFIED-CODE: read from the executable (`re/tools/re_export.py`
`TABLES`).

## Table

Every one of the 97 functions is the **same code** as its AirStrike 2 counterpart (class
`same` of [symbol-map.md](symbol-map.md): identical instruction sequence with addresses
masked and the same read-only constants). Signatures (arity, argument kinds, return kind, done
flag) are therefore those of the AirStrike 2 delta, and they agree with every call site of the
Gulf Thunder corpus (the checker: 6,322 call sites, none passes fewer argument slots than the
arity).

Columns: Corpus = scripts / CALL sites / LCALL sites, Gulf Thunder then AirStrike 2
(VERIFIED-DATA); "Same implementation as" = other names of the same function.

| # | Name | gulf address | as2 address | Same implementation as | Corpus (gulf) | Corpus (as2) | Signature | Code against as2 |
|---|---|---|---|---|---|---|---|---|
| 0 | `debug` | 0x41dcc0 | 0x401040 | - | 0 / 0 / 0 | 0 / 0 / 0 | same | same |
| 1 | `RespawnPlayer` | 0x420490 | 0x421a50 | - | 3 / 3 / 0 | 6 / 6 / 0 | same | same |
| 2 | `EndLevel` | 0x40e6c0 | 0x40e730 | - | 4 / 4 / 0 | 4 / 4 / 0 | same | same |
| 3 | `GameOver` | 0x40f8a0 | 0x410e70 | - | 1 / 1 / 0 | 0 / 0 / 0 | same | same |
| 4 | `random` | 0x41dcd0 | 0x41f290 | - | 163 / 303 / 0 | 153 / 286 / 0 | same | same |
| 5 | `crandom` | 0x41dcf0 | 0x41f2b0 | - | 34 / 111 / 0 | 35 / 114 / 0 | same | same |
| 6 | `sin` | 0x41dd20 | 0x41f2e0 | - | 106 / 176 / 0 | 102 / 176 / 0 | same | same |
| 7 | `cos` | 0x41dd40 | 0x41f300 | - | 7 / 10 / 0 | 4 / 7 / 0 | same | same |
| 8 | `tan` | 0x41dd60 | 0x41f320 | - | 0 / 0 / 0 | 0 / 0 / 0 | same | same |
| 9 | `atan` | 0x41dd80 | 0x41f340 | - | 1 / 1 / 0 | 0 / 0 / 0 | same | same |
| 10 | `atan2` | 0x41ddb0 | 0x41f370 | - | 2 / 2 / 0 | 0 / 0 / 0 | same | same |
| 11 | `abs` | 0x41ddf0 | 0x41f3b0 | - | 45 / 47 / 0 | 44 / 44 / 0 | same | same |
| 12 | `min` | 0x41de20 | 0x41f3e0 | - | 17 / 22 / 0 | 16 / 22 / 0 | same | same |
| 13 | `max` | 0x41de60 | 0x41f420 | - | 4 / 7 / 0 | 6 / 12 / 0 | same | same |
| 14 | `lerp` | 0x41dea0 | 0x41f460 | - | 0 / 0 / 0 | 0 / 0 / 0 | same | same |
| 15 | `copysign` | 0x41dec0 | 0x41f480 | - | 4 / 4 / 0 | 0 / 0 / 0 | same | same |
| 16 | `floor` | 0x41def0 | 0x41f4b0 | - | 0 / 0 / 0 | 0 / 0 / 0 | same | same |
| 17 | `floor2` | 0x41df20 | 0x41f4e0 | - | 2 / 3 / 0 | 0 / 0 / 0 | same | same |
| 18 | `fmod` | 0x41df50 | 0x41f510 | - | 2 / 2 / 0 | 0 / 0 / 0 | same | same |
| 19 | `vec_copy` | 0x41df70 | 0x41f530 | - | 38 / 41 / 0 | 32 / 37 / 0 | same | same |
| 20 | `vec_add` | 0x41df90 | 0x41f550 | - | 1 / 1 / 0 | 0 / 0 / 0 | same | same |
| 21 | `vec_sub` | 0x41dfc0 | 0x41f580 | - | 118 / 133 / 0 | 120 / 137 / 0 | same | same |
| 22 | `vec_ma` | 0x41dff0 | 0x41f5b0 | - | 3 / 6 / 0 | 6 / 12 / 0 | same | same |
| 23 | `vec_length` | 0x41e050 | 0x41f610 | - | 6 / 9 / 0 | 6 / 12 / 0 | same | same |
| 24 | `vec_norm` | 0x41e0a0 | 0x41f660 | - | 1 / 2 / 0 | 1 / 2 / 0 | same | same |
| 25 | `vec_scale` | 0x41e0c0 | 0x41f680 | - | 9 / 11 / 0 | 9 / 11 / 0 | same | same |
| 26 | `vec_setlen` | 0x41e0f0 | 0x41f6b0 | - | 4 / 4 / 0 | 6 / 6 / 0 | same | same |
| 27 | `vec_toyaw` | 0x41e110 | 0x41f6d0 | - | 0 / 0 / 0 | 0 / 0 / 0 | same | same |
| 28 | `vec_toangles` | 0x41e1a0 | 0x41f760 | - | 1 / 1 / 0 | 0 / 0 / 0 | same | same |
| 29 | `ClearAxis` | 0x41e1c0 | 0x41f780 | - | 5 / 5 / 0 | 5 / 5 / 0 | same | same |
| 30 | `AnglesToAxis` | 0x41e1f0 | 0x41f7b0 | - | 14 / 19 / 0 | 14 / 19 / 0 | same | same |
| 31 | `create` | 0x41e210 | 0x41f7d0 | - | 346 / 1258 / 0 | 320 / 1124 / 0 | same | same |
| 32 | `remove` | 0x41e400 | 0x41f9c0 | - | 418 / 504 / 0 | 371 / 450 / 0 | same | same |
| 33 | `activate` | 0x41e420 | 0x41f9e0 | - | 3 / 3 / 0 | 5 / 8 / 0 | same | same |
| 34 | `deactivate` | 0x41e440 | 0x41fa00 | - | 8 / 9 / 0 | 22 / 30 / 0 | same | same |
| 35 | `getentity` | 0x41e460 | 0x41fa20 | - | 2 / 2 / 0 | 0 / 0 / 0 | same | same |
| 36 | `setskin` | 0x41e480 | 0x41fa40 | - | 0 / 0 / 0 | 0 / 0 / 0 | same | same |
| 37 | `callback` | 0x41e980 | 0x41ff40 | - | 20 / 32 / 0 | 13 / 16 / 0 | same | same |
| 38 | `AttachEntity` | 0x41e4b0 | 0x41fa70 | - | 10 / 18 / 0 | 10 / 17 / 0 | same | same |
| 39 | `DetachEntity` | 0x41e520 | 0x41fae0 | - | 2 / 9 / 0 | 1 / 3 / 0 | same | same |
| 40 | `AttachActivate` | 0x41e590 | 0x41fb50 | - | 147 / 213 / 0 | 144 / 189 / 0 | same | same |
| 41 | `AttachDeactivate` | 0x41e650 | 0x41fc10 | - | 154 / 505 / 0 | 154 / 487 / 0 | same | same |
| 42 | `AttachCallback` | 0x41e710 | 0x41fcd0 | - | 56 / 69 / 0 | 55 / 56 / 0 | same | same |
| 43 | `ParentCallback` | 0x41e8a0 | 0x41fe60 | - | 7 / 12 / 0 | 7 / 7 / 0 | same | same |
| 44 | `move` | 0x41ea60 | 0x420020 | - | 122 / 170 / 0 | 116 / 162 / 0 | same | same |
| 45 | `movex` | 0x41eab0 | 0x420070 | - | 14 / 14 / 0 | 2 / 2 / 0 | same | same |
| 46 | `movey` | 0x41ead0 | 0x420090 | - | 86 / 91 / 0 | 73 / 80 / 0 | same | same |
| 47 | `movez` | 0x41eaf0 | 0x4200b0 | - | 87 / 90 / 0 | 84 / 87 / 0 | same | same |
| 48 | `rotate` | 0x41eb10 | 0x4200d0 | - | 6 / 6 / 0 | 6 / 6 / 0 | same | same |
| 49 | `rotatex` | 0x41eb60 | 0x420120 | - | 52 / 53 / 0 | 52 / 53 / 0 | same | same |
| 50 | `rotatey` | 0x41eb80 | 0x420140 | - | 28 / 31 / 0 | 25 / 28 / 0 | same | same |
| 51 | `rotatez` | 0x41eba0 | 0x420160 | - | 138 / 158 / 0 | 140 / 161 / 0 | same | same |
| 52 | `sleep` | 0x41ebc0 | 0x420180 | - | 165 / 0 / 196 | 165 / 0 / 200 | same | same |
| 53 | `Shoot` | 0x41ebd0 | 0x420190 | - | 143 / 742 / 0 | 146 / 1092 / 0 | same | same |
| 54 | `Damage` | 0x41f070 | 0x420630 | - | 131 / 173 / 0 | 117 / 158 / 0 | same | same |
| 55 | `RadialDamage` | 0x41f0f0 | 0x4206b0 | - | 6 / 6 / 0 | 5 / 5 / 0 | same | same |
| 56 | `RadialDamagePlayer` | 0x41f280 | 0x420840 | - | 3 / 3 / 0 | 1 / 1 / 0 | same | same |
| 57 | `RotateTo` | 0x41f3f0 | 0x4209b0 | `lRotateTo` | 31 / 31 / 0 | 31 / 31 / 0 | same | same |
| 58 | `lRotateTo` | 0x41f3f0 | 0x4209b0 | `RotateTo` | 68 / 0 / 68 | 70 / 0 / 70 | same | same |
| 59 | `RotateToClamp` | 0x41f660 | 0x420c20 | `lRotateToClamp` | 59 / 60 / 0 | 59 / 60 / 0 | same | same |
| 60 | `lRotateToClamp` | 0x41f660 | 0x420c20 | `RotateToClamp` | 57 / 0 / 57 | 58 / 0 / 58 | same | same |
| 61 | `MoveToNextWP` | 0x41f700 | 0x420cc0 | `lMoveToNextWP` | 61 / 61 / 0 | 58 / 59 / 0 | same | same |
| 62 | `lMoveToNextWP` | 0x41f700 | 0x420cc0 | `MoveToNextWP` | 88 / 0 / 100 | 80 / 0 / 92 | same | same |
| 63 | `RotateToNextWP` | 0x41f730 | 0x420cf0 | `lRotateToNextWP` | 0 / 0 / 0 | 0 / 0 / 0 | same | same |
| 64 | `lRotateToNextWP` | 0x41f730 | 0x420cf0 | `RotateToNextWP` | 21 / 0 / 21 | 18 / 0 / 18 | same | same |
| 65 | `GetWaypointDelay` | 0x41f750 | 0x420d10 | - | 1 / 1 / 0 | 1 / 1 / 0 | same | same |
| 66 | `TerrainHeight` | 0x41f790 | 0x420d50 | - | 110 / 135 / 0 | 114 / 141 / 0 | same | same |
| 67 | `WaterHeight` | 0x41f7c0 | 0x420d80 | - | 0 / 0 / 0 | 0 / 0 / 0 | same | same |
| 68 | `PlaceLight` | 0x41f7f0 | 0x420db0 | - | 12 / 30 / 0 | 14 / 41 / 0 | same | same |
| 69 | `CameraQuake` | 0x41f860 | 0x420e20 | - | 46 / 51 / 0 | 43 / 49 / 0 | same | same |
| 70 | `LockTarget` | 0x41f810 | 0x420dd0 | - | 5 / 5 / 0 | 5 / 5 / 0 | same | same |
| 71 | `IsValidTarget` | 0x41f830 | 0x420df0 | - | 5 / 8 / 0 | 5 / 8 / 0 | same | same |
| 72 | `StartSound` | 0x41f920 | 0x420ee0 | - | 96 / 217 / 0 | 91 / 265 / 0 | same | same |
| 73 | `StartLoopingSound` | 0x41f9a0 | 0x420f60 | - | 0 / 0 / 0 | 0 / 0 / 0 | same | same |
| 74 | `StopLoopingSound` | 0x41f970 | 0x420f30 | - | 3 / 3 / 0 | 6 / 6 / 0 | same | same |
| 75 | `TraceLine` | 0x41fa10 | 0x420fd0 | - | 0 / 0 / 0 | 0 / 0 / 0 | same | same |
| 76 | `TraceLineDamage` | 0x41fbc0 | 0x421180 | - | 2 / 2 / 0 | 2 / 2 / 0 | same | same |
| 77 | `Lightning` | 0x41fd50 | 0x421310 | - | 3 / 3 / 0 | 2 / 2 / 0 | same | same |
| 78 | `PushPlayer` | 0x41fee0 | 0x4214a0 | - | 42 / 42 / 0 | 45 / 45 / 0 | same | same |
| 79 | `G_AddPowerUp` | 0x41ff90 | 0x421550 | - | 11 / 11 / 0 | 11 / 11 / 0 | same | same |
| 80 | `G_UsePowerUp` | 0x420060 | 0x421620 | - | 3 / 21 / 0 | 6 / 42 / 0 | same | same |
| 81 | `G_GetPowerUp` | 0x420040 | 0x421600 | - | 3 / 3 / 0 | 6 / 6 / 0 | same | same |
| 82 | `G_SetPowerUpCount` | 0x420090 | 0x421650 | - | 3 / 3 / 0 | 3 / 3 / 0 | same | same |
| 83 | `G_AddMissiles` | 0x420100 | 0x4216c0 | - | 5 / 5 / 0 | 5 / 5 / 0 | same | same |
| 84 | `G_UseMissile` | 0x4201d0 | 0x421790 | - | 3 / 15 / 0 | 6 / 30 / 0 | same | same |
| 85 | `G_GetMissiles` | 0x4201b0 | 0x421770 | - | 3 / 3 / 0 | 6 / 6 / 0 | same | same |
| 86 | `G_GetUpgrade` | 0x420200 | 0x4217c0 | - | 15 / 15 / 0 | 16 / 16 / 0 | same | same |
| 87 | `G_SetUpgrade` | 0x420270 | 0x421830 | - | 12 / 12 / 0 | 10 / 10 / 0 | same | same |
| 88 | `ShowTutorialHint` | 0x4202f0 | 0x4218b0 | - | 10 / 10 / 0 | 10 / 10 / 0 | same | same |
| 89 | `PlayerFreezeHealth` | 0x420310 | 0x4218d0 | - | 2 / 4 / 0 | 2 / 4 / 0 | same | same |
| 90 | `PlayerDisableAction` | 0x4203a0 | 0x421960 | - | 2 / 3 / 0 | 2 / 3 / 0 | same | same |
| 91 | `FreezeHealth` | 0x420370 | 0x421930 | - | 0 / 0 / 0 | 0 / 0 / 0 | same | same |
| 92 | `SetFlag` | 0x420400 | 0x4219c0 | - | 0 / 0 / 0 | 0 / 0 / 0 | same | same |
| 93 | `ClearFlag` | 0x420430 | 0x4219f0 | - | 0 / 0 / 0 | 0 / 0 / 0 | same | same |
| 94 | `SetModel` | 0x420460 | 0x421a20 | - | 3 / 9 / 0 | 6 / 18 / 0 | same | same |
| 95 | `TerraMorph` | 0x4204d0 | 0x421a90 | - | 8 / 10 / 0 | 7 / 8 / 0 | same | same |
| 96 | `IsMultiplayer` | 0x4204f0 | 0x421ab0 | - | 1 / 2 / 0 | 0 / 0 / 0 | same | same |
| 97 | `IsPlayerInGame` | 0x420520 | 0x421ae0 | - | 0 / 0 / 0 | 0 / 0 / 0 | same | same |
| 98 | `GetMapPosOfs` | 0x420560 | 0x421b20 | - | 0 / 0 / 0 | 0 / 0 / 0 | same | same |
| 99 | `GetPlayerAccel` | 0x420580 | 0x421b40 | - | 3 / 3 / 0 | 6 / 6 / 0 | same | same |
| 100 | `GetPlayersDistance` | 0x4205e0 | 0x421ba0 | - | 3 / 3 / 0 | 0 / 0 / 0 | same | same |

## Use by the corpus

86 builtins are called (AirStrike 2: 75). Of the 16 builtins new in AirStrike 2, Gulf
Thunder's scripts call 12 (AirStrike 2's own scripts call 5): `GameOver`, `atan2`,
`copysign`, `floor2`, `fmod`, `DetachEntity`, `RadialDamagePlayer`, `G_SetPowerUpCount`,
`TerraMorph`, `IsMultiplayer`, `GetPlayerAccel`, `GetPlayersDistance`. Not called: `floor`,
`WaterHeight`, `IsPlayerInGame`, `GetMapPosOfs`. Old builtins called by Gulf Thunder and by no
AirStrike 2 script: `atan`, `vec_add`, `vec_toangles`, `getentity` (their AirStrike 2
signatures, and for `atan` the corrected semantics of the AirStrike 2 semantics delta, apply).

## JSON schema

The AirStrike 2 schema; `game` is `gulf`, `table_address` `0x0049b448`, `corpus` and
`callsites` are Gulf Thunder's, `summary` states the comparison, and `as2` gives the
AirStrike 2 row (`code_identical` true for all 101). `v170` is kept from the AirStrike 2 table.

## Checked sections

| Base section | Status for `gulf` |
|---|---|
| Calling convention recap | same code |
| Argument type vocabulary | same |
| Table | same: 101 names, same order, same signatures, all functions same code |
| Use by the corpus (new) | counts above |
| JSON schema | changed: added `as2` object |

## Changelog

- 1.0 (F1): first version.
