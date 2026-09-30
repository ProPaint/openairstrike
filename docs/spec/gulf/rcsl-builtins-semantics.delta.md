# RCSL builtin semantics: Gulf Thunder delta (`gulf`)

Delta version 1.0 (package F1). Amends
[../as2/rcsl-builtins-semantics.delta.md](../as2/rcsl-builtins-semantics.delta.md) (which
amends [../rcsl-builtins-semantics.md](../rcsl-builtins-semantics.md)) for the game `gulf`.
Table: [rcsl-builtins-table.delta.md](rcsl-builtins-table.delta.md),
`testdata/golden/gulf/rcsl_builtins.json`. Checker:
`tools/ref/check_builtin_semantics.py --game gulf`.

## Result

**Every builtin behaves as in AirStrike 2.** All 97 functions of the table gulf@0x0049b448
are the same code as their AirStrike 2 counterparts, read-only constants included
([symbol-map.md](symbol-map.md), class `same`), and so are the native functions they call
(`G_Damage` gulf@0x40b970 = as2@0x40b9e0, `G_InitObject` gulf@0x4108c0 = as2@0x411e90,
`Shoot` gulf@0x41ebd0 = as2@0x420190, the terrain, water, collision, camera and player
helpers). The AirStrike 2 entries apply word for word, with Gulf Thunder's addresses from the
table delta.

Two builtins read data that differs between the games, which is not a difference of the
builtin:

- `G_GetUpgrade` / `G_SetUpgrade` (gulf@0x420200 / gulf@0x420270): 9 slots as in AirStrike 2.
  Gulf Thunder's data uses the slots for other weapons (engine-behaviour.delta.md 8.2), and
  two of its pick-up scripts address slots 9 and 10 (missile gun, flamethrower), which the
  builtins ignore (read 0, write nothing); no map or script places those two pick-ups
  (VERIFIED-DATA).
- `EndLevel` (gulf@0x40e6c0): the same steps; the end dialogue table has three entries
  (engine-behaviour.delta.md 10.3).

## Definitions

D1 to D9 of the AirStrike 2 delta: same (same code).

## Summary table

Columns: `#` = table index; Status relative to **AirStrike 2** (`same` = the AirStrike 2
entry applies; no builtin is `changed` or `new`); Corpus = Gulf Thunder scripts / CALL sites /
LCALL sites (VERIFIED-DATA); Priority: see "Priorities".

| # | Builtin | gulf address | as2 address | Status | Corpus | Priority | Confidence |
|---|---|---|---|---|---|---|---|
| 0 | `debug` | 0x41dcc0 | 0x401040 | same | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 1 | `RespawnPlayer` | 0x420490 | 0x421a50 | same | 3 / 3 / 0 | P0 | VERIFIED-CODE |
| 2 | `EndLevel` | 0x40e6c0 | 0x40e730 | same | 4 / 4 / 0 | P0 | VERIFIED-CODE |
| 3 | `GameOver` | 0x40f8a0 | 0x410e70 | same | 1 / 1 / 0 | P1 | VERIFIED-CODE |
| 4 | `random` | 0x41dcd0 | 0x41f290 | same | 163 / 303 / 0 | P0 | VERIFIED-CODE |
| 5 | `crandom` | 0x41dcf0 | 0x41f2b0 | same | 34 / 111 / 0 | P0 | VERIFIED-CODE |
| 6 | `sin` | 0x41dd20 | 0x41f2e0 | same | 106 / 176 / 0 | P0 | VERIFIED-CODE |
| 7 | `cos` | 0x41dd40 | 0x41f300 | same | 7 / 10 / 0 | P0 | VERIFIED-CODE |
| 8 | `tan` | 0x41dd60 | 0x41f320 | same | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 9 | `atan` | 0x41dd80 | 0x41f340 | same | 1 / 1 / 0 | P1 | VERIFIED-CODE |
| 10 | `atan2` | 0x41ddb0 | 0x41f370 | same | 2 / 2 / 0 | P1 | VERIFIED-CODE |
| 11 | `abs` | 0x41ddf0 | 0x41f3b0 | same | 45 / 47 / 0 | P0 | VERIFIED-CODE |
| 12 | `min` | 0x41de20 | 0x41f3e0 | same | 17 / 22 / 0 | P0 | VERIFIED-CODE |
| 13 | `max` | 0x41de60 | 0x41f420 | same | 4 / 7 / 0 | P0 | VERIFIED-CODE |
| 14 | `lerp` | 0x41dea0 | 0x41f460 | same | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 15 | `copysign` | 0x41dec0 | 0x41f480 | same | 4 / 4 / 0 | P1 | VERIFIED-CODE |
| 16 | `floor` | 0x41def0 | 0x41f4b0 | same | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 17 | `floor2` | 0x41df20 | 0x41f4e0 | same | 2 / 3 / 0 | P1 | VERIFIED-CODE |
| 18 | `fmod` | 0x41df50 | 0x41f510 | same | 2 / 2 / 0 | P1 | VERIFIED-CODE |
| 19 | `vec_copy` | 0x41df70 | 0x41f530 | same | 38 / 41 / 0 | P0 | VERIFIED-CODE |
| 20 | `vec_add` | 0x41df90 | 0x41f550 | same | 1 / 1 / 0 | P1 | VERIFIED-CODE |
| 21 | `vec_sub` | 0x41dfc0 | 0x41f580 | same | 118 / 133 / 0 | P0 | VERIFIED-CODE |
| 22 | `vec_ma` | 0x41dff0 | 0x41f5b0 | same | 3 / 6 / 0 | P0 | VERIFIED-CODE |
| 23 | `vec_length` | 0x41e050 | 0x41f610 | same | 6 / 9 / 0 | P0 | VERIFIED-CODE |
| 24 | `vec_norm` | 0x41e0a0 | 0x41f660 | same | 1 / 2 / 0 | P1 | VERIFIED-CODE |
| 25 | `vec_scale` | 0x41e0c0 | 0x41f680 | same | 9 / 11 / 0 | P0 | VERIFIED-CODE |
| 26 | `vec_setlen` | 0x41e0f0 | 0x41f6b0 | same | 4 / 4 / 0 | P0 | VERIFIED-CODE |
| 27 | `vec_toyaw` | 0x41e110 | 0x41f6d0 | same | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 28 | `vec_toangles` | 0x41e1a0 | 0x41f760 | same | 1 / 1 / 0 | P1 | VERIFIED-CODE |
| 29 | `ClearAxis` | 0x41e1c0 | 0x41f780 | same | 5 / 5 / 0 | P0 | VERIFIED-CODE |
| 30 | `AnglesToAxis` | 0x41e1f0 | 0x41f7b0 | same | 14 / 19 / 0 | P0 | VERIFIED-CODE |
| 31 | `create` | 0x41e210 | 0x41f7d0 | same | 346 / 1258 / 0 | P0 | VERIFIED-CODE |
| 32 | `remove` | 0x41e400 | 0x41f9c0 | same | 418 / 504 / 0 | P0 | VERIFIED-CODE |
| 33 | `activate` | 0x41e420 | 0x41f9e0 | same | 3 / 3 / 0 | P1 | VERIFIED-CODE |
| 34 | `deactivate` | 0x41e440 | 0x41fa00 | same | 8 / 9 / 0 | P0 | VERIFIED-CODE |
| 35 | `getentity` | 0x41e460 | 0x41fa20 | same | 2 / 2 / 0 | P1 | VERIFIED-CODE |
| 36 | `setskin` | 0x41e480 | 0x41fa40 | same | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 37 | `callback` | 0x41e980 | 0x41ff40 | same | 20 / 32 / 0 | P0 | VERIFIED-CODE |
| 38 | `AttachEntity` | 0x41e4b0 | 0x41fa70 | same | 10 / 18 / 0 | P0 | VERIFIED-CODE |
| 39 | `DetachEntity` | 0x41e520 | 0x41fae0 | same | 2 / 9 / 0 | P1 | VERIFIED-CODE |
| 40 | `AttachActivate` | 0x41e590 | 0x41fb50 | same | 147 / 213 / 0 | P0 | VERIFIED-CODE |
| 41 | `AttachDeactivate` | 0x41e650 | 0x41fc10 | same | 154 / 505 / 0 | P0 | VERIFIED-CODE |
| 42 | `AttachCallback` | 0x41e710 | 0x41fcd0 | same | 56 / 69 / 0 | P0 | VERIFIED-CODE |
| 43 | `ParentCallback` | 0x41e8a0 | 0x41fe60 | same | 7 / 12 / 0 | P1 | VERIFIED-CODE |
| 44 | `move` | 0x41ea60 | 0x420020 | same | 122 / 170 / 0 | P0 | VERIFIED-CODE |
| 45 | `movex` | 0x41eab0 | 0x420070 | same | 14 / 14 / 0 | P0 | VERIFIED-CODE |
| 46 | `movey` | 0x41ead0 | 0x420090 | same | 86 / 91 / 0 | P0 | VERIFIED-CODE |
| 47 | `movez` | 0x41eaf0 | 0x4200b0 | same | 87 / 90 / 0 | P0 | VERIFIED-CODE |
| 48 | `rotate` | 0x41eb10 | 0x4200d0 | same | 6 / 6 / 0 | P0 | VERIFIED-CODE |
| 49 | `rotatex` | 0x41eb60 | 0x420120 | same | 52 / 53 / 0 | P0 | VERIFIED-CODE |
| 50 | `rotatey` | 0x41eb80 | 0x420140 | same | 28 / 31 / 0 | P0 | VERIFIED-CODE |
| 51 | `rotatez` | 0x41eba0 | 0x420160 | same | 138 / 158 / 0 | P0 | VERIFIED-CODE |
| 52 | `sleep` | 0x41ebc0 | 0x420180 | same | 165 / 0 / 196 | P0 | VERIFIED-CODE |
| 53 | `Shoot` | 0x41ebd0 | 0x420190 | same | 143 / 742 / 0 | P0 | VERIFIED-CODE |
| 54 | `Damage` | 0x41f070 | 0x420630 | same | 131 / 173 / 0 | P0 | VERIFIED-CODE |
| 55 | `RadialDamage` | 0x41f0f0 | 0x4206b0 | same | 6 / 6 / 0 | P0 | VERIFIED-CODE |
| 56 | `RadialDamagePlayer` | 0x41f280 | 0x420840 | same | 3 / 3 / 0 | P1 | VERIFIED-CODE |
| 57 | `RotateTo` | 0x41f3f0 | 0x4209b0 | same | 31 / 31 / 0 | P0 | VERIFIED-CODE |
| 58 | `lRotateTo` | 0x41f3f0 | 0x4209b0 | same | 68 / 0 / 68 | P0 | VERIFIED-CODE |
| 59 | `RotateToClamp` | 0x41f660 | 0x420c20 | same | 59 / 60 / 0 | P0 | VERIFIED-CODE |
| 60 | `lRotateToClamp` | 0x41f660 | 0x420c20 | same | 57 / 0 / 57 | P0 | VERIFIED-CODE |
| 61 | `MoveToNextWP` | 0x41f700 | 0x420cc0 | same | 61 / 61 / 0 | P0 | VERIFIED-CODE |
| 62 | `lMoveToNextWP` | 0x41f700 | 0x420cc0 | same | 88 / 0 / 100 | P0 | VERIFIED-CODE |
| 63 | `RotateToNextWP` | 0x41f730 | 0x420cf0 | same | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 64 | `lRotateToNextWP` | 0x41f730 | 0x420cf0 | same | 21 / 0 / 21 | P0 | VERIFIED-CODE |
| 65 | `GetWaypointDelay` | 0x41f750 | 0x420d10 | same | 1 / 1 / 0 | P1 | VERIFIED-CODE |
| 66 | `TerrainHeight` | 0x41f790 | 0x420d50 | same | 110 / 135 / 0 | P0 | VERIFIED-CODE |
| 67 | `WaterHeight` | 0x41f7c0 | 0x420d80 | same | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 68 | `PlaceLight` | 0x41f7f0 | 0x420db0 | same | 12 / 30 / 0 | P0 | VERIFIED-CODE |
| 69 | `CameraQuake` | 0x41f860 | 0x420e20 | same | 46 / 51 / 0 | P0 | VERIFIED-CODE |
| 70 | `LockTarget` | 0x41f810 | 0x420dd0 | same | 5 / 5 / 0 | P0 | VERIFIED-CODE |
| 71 | `IsValidTarget` | 0x41f830 | 0x420df0 | same | 5 / 8 / 0 | P0 | VERIFIED-CODE |
| 72 | `StartSound` | 0x41f920 | 0x420ee0 | same | 96 / 217 / 0 | P0 | VERIFIED-CODE |
| 73 | `StartLoopingSound` | 0x41f9a0 | 0x420f60 | same | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 74 | `StopLoopingSound` | 0x41f970 | 0x420f30 | same | 3 / 3 / 0 | P0 | VERIFIED-CODE |
| 75 | `TraceLine` | 0x41fa10 | 0x420fd0 | same | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 76 | `TraceLineDamage` | 0x41fbc0 | 0x421180 | same | 2 / 2 / 0 | P0 | VERIFIED-CODE |
| 77 | `Lightning` | 0x41fd50 | 0x421310 | same | 3 / 3 / 0 | P0 | VERIFIED-CODE |
| 78 | `PushPlayer` | 0x41fee0 | 0x4214a0 | same | 42 / 42 / 0 | P0 | VERIFIED-CODE |
| 79 | `G_AddPowerUp` | 0x41ff90 | 0x421550 | same | 11 / 11 / 0 | P0 | VERIFIED-CODE |
| 80 | `G_UsePowerUp` | 0x420060 | 0x421620 | same | 3 / 21 / 0 | P0 | VERIFIED-CODE |
| 81 | `G_GetPowerUp` | 0x420040 | 0x421600 | same | 3 / 3 / 0 | P0 | VERIFIED-CODE |
| 82 | `G_SetPowerUpCount` | 0x420090 | 0x421650 | same | 3 / 3 / 0 | P0 | VERIFIED-CODE |
| 83 | `G_AddMissiles` | 0x420100 | 0x4216c0 | same | 5 / 5 / 0 | P0 | VERIFIED-CODE |
| 84 | `G_UseMissile` | 0x4201d0 | 0x421790 | same | 3 / 15 / 0 | P0 | VERIFIED-CODE |
| 85 | `G_GetMissiles` | 0x4201b0 | 0x421770 | same | 3 / 3 / 0 | P0 | VERIFIED-CODE |
| 86 | `G_GetUpgrade` | 0x420200 | 0x4217c0 | same | 15 / 15 / 0 | P0 | VERIFIED-CODE |
| 87 | `G_SetUpgrade` | 0x420270 | 0x421830 | same | 12 / 12 / 0 | P0 | VERIFIED-CODE |
| 88 | `ShowTutorialHint` | 0x4202f0 | 0x4218b0 | same | 10 / 10 / 0 | P1 | VERIFIED-CODE |
| 89 | `PlayerFreezeHealth` | 0x420310 | 0x4218d0 | same | 2 / 4 / 0 | P0 | VERIFIED-CODE |
| 90 | `PlayerDisableAction` | 0x4203a0 | 0x421960 | same | 2 / 3 / 0 | P0 | VERIFIED-CODE |
| 91 | `FreezeHealth` | 0x420370 | 0x421930 | same | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 92 | `SetFlag` | 0x420400 | 0x4219c0 | same | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 93 | `ClearFlag` | 0x420430 | 0x4219f0 | same | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 94 | `SetModel` | 0x420460 | 0x421a20 | same | 3 / 9 / 0 | P0 | VERIFIED-CODE |
| 95 | `TerraMorph` | 0x4204d0 | 0x421a90 | same | 8 / 10 / 0 | P0 | VERIFIED-CODE |
| 96 | `IsMultiplayer` | 0x4204f0 | 0x421ab0 | same | 1 / 2 / 0 | P1 | VERIFIED-CODE |
| 97 | `IsPlayerInGame` | 0x420520 | 0x421ae0 | same | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 98 | `GetMapPosOfs` | 0x420560 | 0x421b20 | same | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 99 | `GetPlayerAccel` | 0x420580 | 0x421b40 | same | 3 / 3 / 0 | P0 | VERIFIED-CODE |
| 100 | `GetPlayersDistance` | 0x4205e0 | 0x421ba0 | same | 3 / 3 / 0 | P0 | VERIFIED-CODE |

## Priorities

P0 = called by a script reachable from operation 1 of Gulf Thunder; P1 = called by another
Gulf Thunder script; P2 = no Gulf Thunder script calls it (VERIFIED-DATA,
`re/tools/builtin_reach.py --game gulf --mission 1`, the rule of the AirStrike 2 delta).
Operation 1 is the first block of `maps/levels.txt` ("Operation 1: Recon", `maps\level01.hsc`).
Roots: its placed objects, items and script overrides, the three helicopters, `score_num`
and `wavegun_hit`. Result: 365 objects, 179 scripts, 50 weapons, no missing script.

**P0 (69)**: `AnglesToAxis`, `AttachActivate`, `AttachCallback`, `AttachDeactivate`, `AttachEntity`, `CameraQuake`, `ClearAxis`, `Damage`, `EndLevel`, `G_AddMissiles`, `G_AddPowerUp`, `G_GetMissiles`, `G_GetPowerUp`, `G_GetUpgrade`, `G_SetPowerUpCount`, `G_SetUpgrade`, `G_UseMissile`, `G_UsePowerUp`, `GetPlayerAccel`, `GetPlayersDistance`, `IsValidTarget`, `Lightning`, `LockTarget`, `MoveToNextWP`, `PlaceLight`, `PlayerDisableAction`, `PlayerFreezeHealth`, `PushPlayer`, `RadialDamage`, `RespawnPlayer`, `RotateTo`, `RotateToClamp`, `SetModel`, `Shoot`, `StartSound`, `StopLoopingSound`, `TerraMorph`, `TerrainHeight`, `TraceLineDamage`, `abs`, `callback`, `cos`, `crandom`, `create`, `deactivate`, `lMoveToNextWP`, `lRotateTo`, `lRotateToClamp`, `lRotateToNextWP`, `max`, `min`, `move`, `movex`, `movey`, `movez`, `random`, `remove`, `rotate`, `rotatex`, `rotatey`, `rotatez`, `sin`, `sleep`, `vec_copy`, `vec_length`, `vec_ma`, `vec_scale`, `vec_setlen`, `vec_sub`.

**P1 (17)**: `GameOver`, `atan`, `atan2`, `copysign`, `floor2`, `fmod`, `vec_add`,
`vec_norm`, `vec_toangles`, `activate`, `getentity`, `DetachEntity`, `ParentCallback`,
`RadialDamagePlayer`, `GetWaypointDelay`, `ShowTutorialHint`, `IsMultiplayer`.

**P2 (15)**: `debug`, `tan`, `lerp`, `floor`, `vec_toyaw`, `setskin`, `RotateToNextWP`,
`WaterHeight`, `StartLoopingSound`, `TraceLine`, `FreezeHealth`, `SetFlag`, `ClearFlag`,
`IsPlayerInGame`, `GetMapPosOfs`.

Against AirStrike 2's P0 (66): Gulf Thunder's operation 1 adds `GetPlayersDistance`, `cos`,
`lRotateToNextWP`, `movex` and does not need `ShowTutorialHint` (no tutorial). An engine that
implements AirStrike 2's builtins needs nothing new; `GameOver`, `atan2`, `copysign`,
`floor2`, `fmod`, `IsMultiplayer`, `GetPlayersDistance`, `vec_toangles` and `getentity` are
called by Gulf Thunder only, so they are the ones to test first against their AirStrike 2
entries.

## Checked sections

| Base section | Status for `gulf` |
|---|---|
| Definitions D1..D9 | same (same code) |
| Summary table | same for all 101 (table above) |
| Every builtin entry | same (same code); no entry of this delta |
| Priorities | changed (operation 1 of Gulf Thunder) |
| Corrections to other specs (AirStrike 2 delta) | apply unchanged |

## Changelog

- 1.0 (F1): first version.
