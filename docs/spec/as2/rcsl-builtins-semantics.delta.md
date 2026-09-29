# RCSL builtins, behaviour: AirStrike 2 delta (`as2`)

Delta version 1.0 (B3). Amends [../rcsl-builtins-semantics.md](../rcsl-builtins-semantics.md)
1.0 (with [../issues/120-player-fire-at-screen-edge.md](../issues/120-player-fire-at-screen-edge.md))
for the game `as2` (AirStrike 2 v2.51, `AirStrike3D II.exe`). Companions:
[rcsl-builtins-table.delta.md](rcsl-builtins-table.delta.md) (signatures of the 101 builtins),
[rcsl-vm.delta.md](rcsl-vm.delta.md) (entity layout, globals, touch pass, `create`).
Consistency check: `tools/ref/check_builtin_semantics.py --game as2` (prints one `OK` line).
Symbols named here: `re/symbols_as2_builtins.csv`.

The base spec has numbered sections 1 and 2 and lettered families A to G; this delta mirrors
them. Tags as in [../README.md](../README.md): `VERIFIED-CODE` cites `as2@` (and for a
comparison `v170@`) addresses; `VERIFIED-DATA` holds for the 631 files of
`assets_extracted_games/as2/scripts` and the `as2` object, weapon and map files;
`INFERRED-SEQUEL` is not used (the third-party toolkit was not needed); `GUESS` is marked.

## Method

- **Old builtins.** Every one of the 85 v1.70 builtins was compared with its `as2`
  implementation instruction by instruction, absolute addresses masked and every v1.70
  structure offset replaced by its `as2` value (entity offsets from rcsl-vm.delta.md, player
  record stride 0x171 → 0x164). Unlike the table delta, the comparison also covers the
  engine routines each builtin calls, paired by call order, to a depth of two. Where the
  normalised code differs, both decompilations and disassemblies were read. The functions
  with a different implementation in the table delta (14) and those with the "same logic"
  (5) were all read in full.
- **New builtins.** Read from the `as2` code and cross-checked against Gulf Thunder
  (`re/out/gulf`): all 16 new builtins, the `TerraMorph` stamp routine and the changed
  `Shoot`, `RadialDamage`, `TraceLineDamage`, `Lightning` and `create` are instruction for
  instruction identical there (addresses masked; gulf@0x4204d0 `TerraMorph`, gulf@0x4190a0
  its stamp routine).
- **Call sites and priorities.** Scripts disassembled with `tools/rcsl_disasm.py`; counts from
  `testdata/golden/as2/rcsl_builtins.json` (same numbers as
  `tools/ref/check_builtin_calls.py --game as2`). Reachability: see "Priorities".

## 1. Conventions

Changed only in addresses and offsets (VERIFIED-CODE, rcsl-vm.delta.md "Entity references and
fields"; confirmed for every builtin by the offset-mapped comparison):

- An entity reference is entity + **0x84** (v1.70 + 0x7B); field k is the float at entity +
  0x84 + 4k, with the same indices 0..87 as in v1.70.
- Native offsets used below, `as2` (v1.70): parent +0x08 (+0x08), tag +0x0C, `abs` flag +0x10,
  counted-in-root flag +0x11, attachment count **+0x14** (+0x12), name **+0x18** (+0x16),
  activation state **+0x1C** (+0x1A), runtime flags **+0x20** (+0x1E, same bits), drop
  **+0x24** (+0x22), path **+0x50** (+0x4B), path distance **+0x54** (+0x4F), last node
  **+0x58** (+0x53), thread **+0x5C** (+0x57), touch mode **+0x60** (+0x5B), time since damage
  **+0x74** (+0x6F), new timer **+0x78**, looping channel **+0x7C** (+0x73), player index
  **+0x80** (+0x77), model +0x15C, skin +0x160, emitter +0x1C4, children +0x1EC/+0x1F0.
- Globals: argument pointer as2@0x2106324, `self` as2@0x2106320, current thread
  as2@0x2106310, return register as2@0x210632c, done flag as2@0x2106330, frametime
  as2@0x49f920, `g_damage_factor` as2@0x49ded8, number of players as2@0x49ded0 (v1.70: a
  count at 0x4577b8), two-player flag as2@0x2219145 (v1.70 0x1fdb2c7), live list head
  as2@0x4c2084 with sentinel as2@0x4c2080 (v1.70 0x458ccc / 0x458cc8), ftol as2@0x47cbb0.
- **Player record** (base: engine-behaviour.md §7.1): records at as2@0x20c5ad0, stride
  **0x164**. Fields used by builtins, `as2` (v1.70): +0x00 entity pointer (+0x00), +0x88
  helicopter index (v1.70 offset not compared), +0x8C `p_action`, +0x94 `p_scores`, +0x9C
  `p_lives`, +0xA0 `p_stars`, +0xB8 freeze counter (+0xB4), +0xBC action-disabled byte
  (+0xB8), +0xC0 power-up counts ×16 (+0xB9), +0x100 selected power-up (+0xF9), +0x104 missile
  counts ×5 (+0xFD), +0x118 selected missile (+0x111), +0x11C upgrade levels, **9** of them
  (+0x115, 20), +0x140 int and +0x144 float (level-end statistics, see `EndLevel`), +0x148 kill
  counter (+0x16D), **+0x14C..+0x154 acceleration vector (new)**, +0x158, +0x15C, +0x160
  level-end results (new). VERIFIED-CODE: the builtins' addressing (e.g. as2@0x4215bd,
  as2@0x4216a2, as2@0x421b5d), G_SpawnPlayer as2@0x413b20.
- **Entity classes**: the object keyword `civilian` gives class **5.0** (as2@0x4125a0,
  the object parser, next to `enemy` = 2.0); `touch` takes a set of `TOUCH_ENEMIES` (0x1),
  `TOUCH_PLAYER` (0x2), `TOUCH_CIVILIAN` (0x4) or `TOUCH_ALL` (0xF) (same routine). VERIFIED-CODE.
- Same everything else: script-level types, units, ftol, "Returns: none".

## 2. Shared definitions

| Definition | Status | `as2` detail |
|---|---|---|
| D1 Resolving an entity argument | same | One indirection through the reference. New null tests in `AttachEntity` (both raw pointers), `RotateTo` (reference and self), `DetachEntity` (raw); the other builtins still read through without a test (e.g. `Damage`, `remove`, `activate` code identical to v1.70). VERIFIED-CODE |
| D2 Stale references | not checked | The free path of removed entities was not re-read; the builtins that act on references behave as in v1.70 |
| D3 Running a handler | same | rcsl-vm.delta.md "Event dispatch" (as2@0x41f130 identical to v170@0x41a200 apart from offsets) |
| D4 Attacker index | same | Two-player flag as2@0x2219145; bit 0x2000 of ftol(self's field 3), e.g. as2@0x4207bc..0x4207c6 |
| D5 G_Damage | **changed** | as2@0x40b9e0. Step 1 same (field 4, frozen bit 0x10 of +0x20, player freeze counter at record + 0xB8, god mode). Step 4: the attacker's kill counter (record + 0x148) is increased **only while it is below the level's enemy total** (as2@0x5432c0, the counter `create` and the map spawner increase), as2@0x40bb46..0x40bb5d; v1.70 increased it always (v170@0x404c44). Everything else, including the order handler → dead flag → drop → score, same. VERIFIED-CODE |
| D6 Direction to angles | same | as2@0x417010 identical to v170@0x41e4f0 |
| D7 Orientation convention | same | axis routine as2@0x416eb0 identical to v170@0x41e390 |
| D8 Matrix inverse defect | same | as2@0x417250 identical to v170@0x41e730, defect included |
| D9 Screen segment test | same rule | Segment-rectangle test as2@0x417650 identical to v170@0x40ca10. The projection as2@0x41cee0 computes the same products with the previous render's **Direct3D** matrices stored as floats (v1.70: OpenGL matrices as doubles, v170@0x419540); the rule (previous frame's camera, fixed viewport in our engine) is unchanged |

## Summary table

Columns: `#` = `as2` table index; `v1.70 #` = base index (`-` for new); Status: `same` = the
base entry applies with the offsets and addresses of section 1 (the code is the same apart
from addresses, offsets and the note in the Confidence cell); `changed` = see the entry
below; `new` = full entry below. Corpus = `as2` scripts / CALL sites / LCALL sites
(VERIFIED-DATA). Priority: see "Priorities". Confidence is that of the core behaviour.

| # | Builtin | as2 address | v1.70 # | Status | Corpus | Priority | Confidence |
|---|---|---|---|---|---|---|---|
| 0 | `debug` | 0x401040 | 0 | same | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 1 | `RespawnPlayer` | 0x421a50 | 1 | changed | 6 / 6 / 0 | P0 | VERIFIED-CODE; no lives test: always spawns |
| 2 | `EndLevel` | 0x40e730 | 2 | changed | 4 / 4 / 0 | P0 | VERIFIED-CODE; statistics, then the front end's level-end sequence |
| 3 | `GameOver` | 0x410e70 | - | new | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 4 | `random` | 0x41f290 | 3 | same | 153 / 286 / 0 | P0 | VERIFIED-CODE |
| 5 | `crandom` | 0x41f2b0 | 4 | same | 35 / 114 / 0 | P0 | VERIFIED-CODE |
| 6 | `sin` | 0x41f2e0 | 5 | same | 102 / 176 / 0 | P0 | VERIFIED-CODE |
| 7 | `cos` | 0x41f300 | 6 | same | 4 / 7 / 0 | P1 | VERIFIED-CODE |
| 8 | `tan` | 0x41f320 | 7 | same | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 9 | `atan` | 0x41f340 | 8 | changed | 0 / 0 / 0 | P2 | VERIFIED-CODE; returns degrees of atan(x) |
| 10 | `atan2` | 0x41f370 | - | new | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 11 | `abs` | 0x41f3b0 | 9 | same | 44 / 44 / 0 | P0 | VERIFIED-CODE |
| 12 | `min` | 0x41f3e0 | 10 | same | 16 / 22 / 0 | P0 | VERIFIED-CODE |
| 13 | `max` | 0x41f420 | 11 | same | 6 / 12 / 0 | P0 | VERIFIED-CODE |
| 14 | `lerp` | 0x41f460 | 12 | same | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 15 | `copysign` | 0x41f480 | - | new | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 16 | `floor` | 0x41f4b0 | - | new | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 17 | `floor2` | 0x41f4e0 | - | new | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 18 | `fmod` | 0x41f510 | - | new | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 19 | `vec_copy` | 0x41f530 | 13 | same | 32 / 37 / 0 | P0 | VERIFIED-CODE |
| 20 | `vec_add` | 0x41f550 | 14 | same | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 21 | `vec_sub` | 0x41f580 | 15 | same | 120 / 137 / 0 | P0 | VERIFIED-CODE |
| 22 | `vec_ma` | 0x41f5b0 | 16 | same | 6 / 12 / 0 | P0 | VERIFIED-CODE |
| 23 | `vec_length` | 0x41f610 | 17 | same | 6 / 12 / 0 | P0 | VERIFIED-CODE |
| 24 | `vec_norm` | 0x41f660 | 18 | same | 1 / 2 / 0 | P1 | VERIFIED-CODE |
| 25 | `vec_scale` | 0x41f680 | 19 | same | 9 / 11 / 0 | P0 | VERIFIED-CODE |
| 26 | `vec_setlen` | 0x41f6b0 | 20 | same | 6 / 6 / 0 | P0 | VERIFIED-CODE |
| 27 | `vec_toyaw` | 0x41f6d0 | 21 | same | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 28 | `vec_toangles` | 0x41f760 | 22 | same | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 29 | `ClearAxis` | 0x41f780 | 23 | same | 5 / 5 / 0 | P0 | VERIFIED-CODE |
| 30 | `AnglesToAxis` | 0x41f7b0 | 24 | same | 14 / 19 / 0 | P0 | VERIFIED-CODE |
| 31 | `create` | 0x41f7d0 | 25 | changed | 320 / 1124 / 0 | P0 | VERIFIED-CODE; return register written last (quirk 9 gone) |
| 32 | `remove` | 0x41f9c0 | 26 | same | 371 / 450 / 0 | P0 | VERIFIED-CODE; render step 3 gone |
| 33 | `activate` | 0x41f9e0 | 27 | same | 5 / 8 / 0 | P1 | VERIFIED-CODE |
| 34 | `deactivate` | 0x41fa00 | 28 | same | 22 / 30 / 0 | P0 | VERIFIED-CODE |
| 35 | `getentity` | 0x41fa20 | 29 | same | 0 / 0 / 0 | P2 | VERIFIED-CODE; adds 0x84 |
| 36 | `setskin` | 0x41fa40 | 30 | same | 0 / 0 / 0 | P2 | VERIFIED-CODE; texture registry |
| 37 | `callback` | 0x41ff40 | 31 | same | 13 / 16 / 0 | P0 | VERIFIED-CODE |
| 38 | `AttachEntity` | 0x41fa70 | 32 | changed | 10 / 17 / 0 | P0 | VERIFIED-CODE; null parent ignored |
| 39 | `DetachEntity` | 0x41fae0 | - | new | 1 / 3 / 0 | P1 | VERIFIED-CODE |
| 40 | `AttachActivate` | 0x41fb50 | 33 | same | 144 / 189 / 0 | P0 | VERIFIED-CODE |
| 41 | `AttachDeactivate` | 0x41fc10 | 34 | same | 154 / 487 / 0 | P0 | VERIFIED-CODE |
| 42 | `AttachCallback` | 0x41fcd0 | 35 | same | 55 / 56 / 0 | P0 | VERIFIED-CODE |
| 43 | `ParentCallback` | 0x41fe60 | 36 | same | 7 / 7 / 0 | P1 | VERIFIED-CODE |
| 44 | `move` | 0x420020 | 37 | same | 116 / 162 / 0 | P0 | VERIFIED-CODE |
| 45 | `movex` | 0x420070 | 38 | same | 2 / 2 / 0 | P1 | VERIFIED-CODE |
| 46 | `movey` | 0x420090 | 39 | same | 73 / 80 / 0 | P0 | VERIFIED-CODE |
| 47 | `movez` | 0x4200b0 | 40 | same | 84 / 87 / 0 | P0 | VERIFIED-CODE |
| 48 | `rotate` | 0x4200d0 | 41 | same | 6 / 6 / 0 | P0 | VERIFIED-CODE |
| 49 | `rotatex` | 0x420120 | 42 | same | 52 / 53 / 0 | P0 | VERIFIED-CODE |
| 50 | `rotatey` | 0x420140 | 43 | same | 25 / 28 / 0 | P0 | VERIFIED-CODE |
| 51 | `rotatez` | 0x420160 | 44 | same | 140 / 161 / 0 | P0 | VERIFIED-CODE |
| 52 | `sleep` | 0x420180 | 45 | same | 165 / 0 / 200 | P0 | VERIFIED-CODE |
| 53 | `Shoot` | 0x420190 | 46 | changed | 146 / 1092 / 0 | P0 | VERIFIED-CODE; a dead shooter (field 4 ≠ 0) does not fire |
| 54 | `Damage` | 0x420630 | 47 | same | 117 / 158 / 0 | P0 | VERIFIED-CODE; D5 changed |
| 55 | `RadialDamage` | 0x4206b0 | 48 | changed | 5 / 5 / 0 | P0 | VERIFIED-CODE; civilians (class 5) are hit too |
| 56 | `RadialDamagePlayer` | 0x420840 | - | new | 1 / 1 / 0 | P1 | VERIFIED-CODE |
| 57 | `RotateTo` | 0x4209b0 | 49 | changed | 31 / 31 / 0 | P0 | VERIFIED-CODE; null target or self: done 0, nothing turns |
| 58 | `lRotateTo` | 0x4209b0 | 50 | changed | 70 / 0 / 70 | P0 | VERIFIED-CODE; as `RotateTo` |
| 59 | `RotateToClamp` | 0x420c20 | 51 | same | 59 / 60 / 0 | P0 | VERIFIED-CODE; its `RotateTo` step has the new null test |
| 60 | `lRotateToClamp` | 0x420c20 | 52 | same | 58 / 0 / 58 | P0 | VERIFIED-CODE; as `RotateToClamp` |
| 61 | `MoveToNextWP` | 0x420cc0 | 53 | same | 58 / 59 / 0 | P0 | VERIFIED-CODE; path layout |
| 62 | `lMoveToNextWP` | 0x420cc0 | 54 | same | 80 / 0 / 92 | P0 | VERIFIED-CODE; path layout |
| 63 | `RotateToNextWP` | 0x420cf0 | 55 | same | 0 / 0 / 0 | P2 | VERIFIED-CODE; path layout |
| 64 | `lRotateToNextWP` | 0x420cf0 | 56 | same | 18 / 0 / 18 | P1 | VERIFIED-CODE; path layout |
| 65 | `GetWaypointDelay` | 0x420d10 | 57 | same | 1 / 1 / 0 | P1 | VERIFIED-CODE; path layout |
| 66 | `TerrainHeight` | 0x420d50 | 58 | same | 114 / 141 / 0 | P0 | VERIFIED-CODE; terrain mutable, see `TerraMorph` |
| 67 | `WaterHeight` | 0x420d80 | - | new | 0 / 0 / 0 | P2 | VERIFIED-CODE; wave term not given |
| 68 | `PlaceLight` | 0x420db0 | 59 | same | 14 / 41 / 0 | P0 | VERIFIED-CODE |
| 69 | `CameraQuake` | 0x420e20 | 60 | same | 43 / 49 / 0 | P0 | VERIFIED-CODE |
| 70 | `LockTarget` | 0x420dd0 | 61 | same | 5 / 5 / 0 | P0 | VERIFIED-CODE; class 2.0 only (no civilians) |
| 71 | `IsValidTarget` | 0x420df0 | 62 | same | 5 / 8 / 0 | P0 | VERIFIED-CODE |
| 72 | `StartSound` | 0x420ee0 | 63 | same | 91 / 265 / 0 | P0 | VERIFIED-CODE |
| 73 | `StartLoopingSound` | 0x420f60 | 64 | same | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 74 | `StopLoopingSound` | 0x420f30 | 65 | same | 6 / 6 / 0 | P0 | VERIFIED-CODE |
| 75 | `TraceLine` | 0x420fd0 | 66 | changed | 0 / 0 / 0 | P2 | VERIFIED-CODE; mask bit 0x4 civilians; mask 2 players only |
| 76 | `TraceLineDamage` | 0x421180 | 67 | changed | 2 / 2 / 0 | P0 | VERIFIED-CODE; touch mode as bit set; civilians |
| 77 | `Lightning` | 0x421310 | 68 | changed | 2 / 2 / 0 | P0 | VERIFIED-CODE; radius argument, horizontal distance, hit effect |
| 78 | `PushPlayer` | 0x4214a0 | 69 | same | 45 / 45 / 0 | P0 | VERIFIED-CODE |
| 79 | `G_AddPowerUp` | 0x421550 | 70 | same | 11 / 11 / 0 | P0 | VERIFIED-CODE; record layout |
| 80 | `G_UsePowerUp` | 0x421620 | 71 | changed | 6 / 42 / 0 | P0 | VERIFIED-CODE; auto-selection skips kinds 6, 7, 9 |
| 81 | `G_GetPowerUp` | 0x421600 | 72 | same | 6 / 6 / 0 | P0 | VERIFIED-CODE |
| 82 | `G_SetPowerUpCount` | 0x421650 | - | new | 3 / 3 / 0 | P0 | VERIFIED-CODE |
| 83 | `G_AddMissiles` | 0x4216c0 | 73 | same | 5 / 5 / 0 | P0 | VERIFIED-CODE; record layout |
| 84 | `G_UseMissile` | 0x421790 | 74 | same | 6 / 30 / 0 | P0 | VERIFIED-CODE; record layout |
| 85 | `G_GetMissiles` | 0x421770 | 75 | same | 6 / 6 / 0 | P0 | VERIFIED-CODE |
| 86 | `G_GetUpgrade` | 0x4217c0 | 76 | changed | 16 / 16 / 0 | P0 | VERIFIED-CODE; 9 upgrade slots |
| 87 | `G_SetUpgrade` | 0x421830 | 77 | changed | 10 / 10 / 0 | P0 | VERIFIED-CODE; 9 upgrade slots |
| 88 | `ShowTutorialHint` | 0x4218b0 | 78 | same | 10 / 10 / 0 | P0 | VERIFIED-CODE |
| 89 | `PlayerFreezeHealth` | 0x4218d0 | 79 | same | 2 / 4 / 0 | P0 | VERIFIED-CODE; counter at record + 0xB8 |
| 90 | `PlayerDisableAction` | 0x421960 | 80 | same | 2 / 3 / 0 | P0 | VERIFIED-CODE; byte at record + 0xBC |
| 91 | `FreezeHealth` | 0x421930 | 81 | same | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 92 | `SetFlag` | 0x4219c0 | 82 | same | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 93 | `ClearFlag` | 0x4219f0 | 83 | same | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 94 | `SetModel` | 0x421a20 | 84 | same | 6 / 18 / 0 | P0 | VERIFIED-CODE |
| 95 | `TerraMorph` | 0x421a90 | - | new | 7 / 8 / 0 | P0 | VERIFIED-CODE |
| 96 | `IsMultiplayer` | 0x421ab0 | - | new | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 97 | `IsPlayerInGame` | 0x421ae0 | - | new | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 98 | `GetMapPosOfs` | 0x421b20 | - | new | 0 / 0 / 0 | P2 | VERIFIED-CODE |
| 99 | `GetPlayerAccel` | 0x421b40 | - | new | 6 / 6 / 0 | P0 | VERIFIED-CODE |
| 100 | `GetPlayersDistance` | 0x421ba0 | - | new | 0 / 0 / 0 | P2 | VERIFIED-CODE |

Totals: 101 builtins: 70 same, 15 changed, 16 new; 66 P0, 9 P1, 26 P2.

### Spot checks of the table delta's "same code" rows

All 66 were checked with the offset-mapped comparison, callees included (the task asked for
at least 10). 60 are identical with their callees: `random`, `crandom`, `sin`, `cos`, `tan`,
`abs`, `min`, `max`, `lerp`, `vec_copy`, `vec_add`, `vec_sub`, `vec_ma`, `vec_length`,
`vec_norm`, `vec_scale`, `vec_setlen`, `vec_toyaw`, `vec_toangles`, `ClearAxis`,
`AnglesToAxis`, `activate`, `deactivate`, `getentity`, `callback`, `AttachActivate`,
`AttachDeactivate`, `AttachCallback`, `ParentCallback`, `move`, `movex`, `movey`, `movez`,
`rotate`, `rotatex`, `rotatey`, `rotatez`, `sleep`, `Damage` (wrapper; D5 changed),
`RotateToClamp`, `lRotateToClamp`, `MoveToNextWP`, `lMoveToNextWP`, `RotateToNextWP`,
`lRotateToNextWP`, `TerrainHeight`, `PlaceLight`, `CameraQuake`, `LockTarget`,
`IsValidTarget`, `StartSound`, `StartLoopingSound`, `StopLoopingSound`, `PushPlayer`,
`G_GetPowerUp`, `G_UseMissile`, `G_GetMissiles`, `ShowTutorialHint`, `PlayerFreezeHealth`,
`PlayerDisableAction`, `FreezeHealth`, `SetFlag`, `ClearFlag`, `SetModel` (the path and
record-layout differences of the waypoint, sound-registry and player-record helpers are
offsets only). Two differ in a callee: **`RespawnPlayer`** (its G_SpawnPlayer lost the lives
test: `changed`, see F) and `remove` (its removal routine as2@0x40b280 no longer frees a
per-frame shadow display list, base step 3, because the Direct3D renderer has none: not
script-visible, kept `same`). Of the 5 "same logic" rows, `debug`, `setskin` and
`GetWaypointDelay` are the same; `atan` and `G_UsePowerUp` changed (see A and F).

## Priorities

P0 = called by a script reachable from mission 1 of `as2`; P1 = called by some other `as2`
script; P2 = no `as2` script calls it (VERIFIED-DATA, scratch scan, not kept). Mission 1 is
the first block of `maps/levels.txt` (`"Mission 1: Tutorial"`, map `maps\level1_tutor.hsc`,
786 placements). Roots: the placed objects, their items and script overrides; the six player
helicopters `player_1` .. `player_6`; the engine-created `score_num` and `wavegun_hit`.
Closure: an object's script, its `attach` targets and every other object named in its
definition; a script's CASH objects, and every string of its STRG that names an object, a
weapon (then its `missile` and `flash` objects) or a script. The string rule
over-approximates (a string naming an object counts even if never passed to `create`), so P0
may be slightly too large, never too small. Result: 379 objects, 171 scripts, 35 weapons, no
missing script.

**P0 (66)**: `AnglesToAxis`, `AttachActivate`, `AttachCallback`, `AttachDeactivate`,
`AttachEntity`, `CameraQuake`, `ClearAxis`, `Damage`, `EndLevel`, `G_AddMissiles`,
`G_AddPowerUp`, `G_GetMissiles`, `G_GetPowerUp`, `G_GetUpgrade`, `G_SetPowerUpCount`,
`G_SetUpgrade`, `G_UseMissile`, `G_UsePowerUp`, `GetPlayerAccel`, `IsValidTarget`,
`Lightning`, `LockTarget`, `MoveToNextWP`, `PlaceLight`, `PlayerDisableAction`,
`PlayerFreezeHealth`, `PushPlayer`, `RadialDamage`, `RespawnPlayer`, `RotateTo`,
`RotateToClamp`, `SetModel`, `Shoot`, `ShowTutorialHint`, `StartSound`, `StopLoopingSound`,
`TerraMorph`, `TerrainHeight`, `TraceLineDamage`, `abs`, `callback`, `crandom`, `create`,
`deactivate`, `lMoveToNextWP`, `lRotateTo`, `lRotateToClamp`, `max`, `min`, `move`, `movey`,
`movez`, `random`, `remove`, `rotate`, `rotatex`, `rotatey`, `rotatez`, `sin`, `sleep`,
`vec_copy`, `vec_length`, `vec_ma`, `vec_scale`, `vec_setlen`, `vec_sub`.

**P1 (9)**: `DetachEntity`, `GetWaypointDelay`, `ParentCallback`, `RadialDamagePlayer`,
`activate`, `cos`, `lRotateToNextWP`, `movex`, `vec_norm`.

## Builtins no `as2` script calls

26 (VERIFIED-DATA): `debug`, `GameOver`, `tan`, `atan`, `atan2`, `lerp`, `copysign`, `floor`,
`floor2`, `fmod`, `vec_add`, `vec_toyaw`, `vec_toangles`, `getentity`, `setskin`,
`RotateToNextWP` (its alias is used), `WaterHeight`, `StartLoopingSound`, `TraceLine`,
`FreezeHealth`, `SetFlag`, `ClearFlag`, `IsMultiplayer`, `IsPlayerInGame`, `GetMapPosOfs`,
`GetPlayersDistance`. Seven of the new ones are called by Gulf Thunder scripts (`GameOver`,
`GetPlayersDistance`, `IsMultiplayer`, `atan2`, `copysign`, `floor2`, `fmod`; table delta).

## Entry template

Same as the base. Only `changed` and `new` builtins have an entry below; a `changed` entry
lists the differences against the base entry, citing both executables, and repeats the
unchanged steps only where needed to read it.

---

## A. Math, random, flags, vectors and axes

The five new math builtins read only their arguments and write only the return register.
They compute on the x87 stack (80-bit) and store the result as a float. VERIFIED-CODE.
C runtime entries (identified by use and, for `_copysign`, by Ghidra's library match):
arctangent as2@0x47cce0, two-argument arctangent as2@0x47ce8a, `fmod` as2@0x47cc6a, `floor`
as2@0x479410, `_copysign` as2@0x43b442.

### 9. `atan` — as2@0x41f340 (changed)

- Signature, returns, latent: same (x float → float, no done flag).
- **Changed behaviour**: return atan(x), converted from radians to **degrees** (× 180 / π).
  v1.70 returned atan(x × π/180) in radians (v170@0x41a417..0x41a423: the argument is scaled
  before the call); `as2` calls the arctangent on x unscaled and scales the result
  (as2@0x41f346..0x41f359, constants 180.0 at as2@0x48f190 and π at as2@0x48f128).
  VERIFIED-CODE. The base quirk is gone.
- Corpus: none. Priority: P2.

### 10. `atan2` — as2@0x41f370 (new)

- Signature: `y` float, `x` float (t0, t1). Returns: float, degrees. Latent: no.
- Behaviour: return atan2(y, x) × 180/π, in (−180, 180]; no wrapping into [0, 360).
  VERIFIED-CODE as2@0x41f37e..0x41f39c.
- Edge cases: atan2(0, 0) = 0 (C runtime); NaN in gives NaN.
- Corpus: none in `as2`. Priority: P2.

### 15. `copysign` — as2@0x41f480 (new)

- Signature: `x` float, `sign` float. Returns: float. Latent: no.
- Behaviour: return |x| with the sign bit of `sign` (C runtime `_copysign` on doubles, as2@0x41f494);
  a `sign` of −0.0 gives a negative result. VERIFIED-CODE.
- Corpus: none in `as2`. Priority: P2.

### 16. `floor` — as2@0x41f4b0 (new)

- Signature: `x` float. Returns: float. Latent: no.
- Behaviour: return the largest integer ≤ x (C runtime `floor`, as2@0x41f4c6). VERIFIED-CODE.
- Corpus: none. Priority: P2.

### 17. `floor2` — as2@0x41f4e0 (new)

- Signature: `x` float, `step` float. Returns: float. Latent: no.
- Behaviour: return x − fmod(x, step) (the C `fmod`, whose result has the sign of x), i.e. x
  moved **toward zero** to a multiple of |step|: floor2(7, 5) = 5, floor2(−7, 5) = −5.
  VERIFIED-CODE as2@0x41f4e8..0x41f506.
- Edge cases: step = 0 gives NaN (fmod of a zero divisor).
- Corpus: none in `as2`. Priority: P2.

### 18. `fmod` — as2@0x41f510 (new)

- Signature: `x` float, `y` float. Returns: float. Latent: no.
- Behaviour: return the C `fmod(x, y)` (remainder with the sign of x). VERIFIED-CODE
  as2@0x41f51b.
- Edge cases: y = 0 gives NaN.
- Corpus: none in `as2`. Priority: P2.

---

## B. Entity creation, removal and hierarchy

### 31. `create` — as2@0x41f7d0 (changed)

- Signature: same. Returns: the new entity's reference, or 0.0.
- **Changed step 4**: the new reference is kept aside and written to the return register
  **after** step 8 (as2@0x41f9a5..0x41f9ac), instead of before step 5 (v170@0x41a888). The
  caller therefore always receives the new entity, whatever its `init` or first `main` left
  in the register (rcsl-vm.delta.md, quirk 9 gone). The failure path (nothing created) still
  writes 0.0, as2@0x41f893. VERIFIED-CODE.
- Other steps same, with `as2` addresses: definition lookup as2@0x411ba0, spawn
  as2@0x4124c0 (ground and water snap, mark decal), angles and player index copied
  (as2@0x41f8dc), `init` through as2@0x40bee0, shadow re-registration and bake
  (as2@0x41a0f0, as2@0x432120), enemy total **as2@0x5432c0** += 1 for class 2.0 without
  FL_NONTARGET (as2@0x41f996), one think as2@0x40cb30 (whose details, such as the new
  per-entity timer, are in rcsl-vm.delta.md). Civilians (class 5) are not counted.
- Edge cases: the base edge case "the returned reference may be stale data from step 4" no
  longer applies.
- Corpus: 320 / 1124 / 0; `effects\expl_big.scr` pc 7 (`create("mark", &self.origin)` right
  after a `TerraMorph`), pc 42 (`v16 = create("expl_ring", …)`, then
  `vec_copy(&self.angles, &v16[14])`: the result is used at once).
- Priority: P0.

### 38. `AttachEntity` — as2@0x41fa70 (changed)

- Signature: same.
- **Changed**: the raw child **and the raw parent** are tested; if either is 0 nothing happens
  (as2@0x41fa7d..0x41fa86). v1.70 tested only the child and stored a parent read without a
  test (v170@0x41aa3a..0x41aa49). Steps 1–5 otherwise same, with the attachment count at the
  root's +0x14 (as2@0x41fad0) and runtime flags +0x20 |= 0x24 (as2@0x41faaa). VERIFIED-CODE.
- Corpus: 10 / 17 / 0; `player\common\p_shield.scr` pc 15 (`AttachEntity($self, $player, …,
  1)`), `bosses\boss_2\boss_2.scr` (boss parts).
- Priority: P0.

### 39. `DetachEntity` — as2@0x41fae0 (new)

- Signature: `ent` entity. Returns: none. Latent: no.
- Behaviour (E = raw entity of `ent`; nothing happens if E is 0):
  1. P = E's parent (+0x08). If P ≠ 0: E's angles (fields 14, 15, 16) = P's fields 14, 15, 16
     (the parent's own angles, not the tag's orientation).
  2. If E holds a count on a root (+0x11 ≠ 0): if P ≠ 0, the root of P's parent chain (P
     itself if P has no parent) gets its attachment count (+0x14) decreased by 1; then +0x11
     = 0.
  3. E's parent = 0, tag pointer (+0x0C) = 0, `abs` flag (+0x10) = 0.
  All VERIFIED-CODE as2@0x41fae0..0x41fb40.
- Consequences: E becomes a root from its next think on. Its origin (fields 5–7) is left as
  the last think placed it (the tag position plus fields 8–10), so it stays where it was
  and moves only by its own script from then on. Runtime bit 0x20 (set by `AttachEntity`) is
  not cleared; GUESS: harmless for a root, since in v1.70 the think tests it only for
  children (v170@0x405a60); not re-read in as2@0x40cb30. This is the undo of `AttachEntity`.
- Edge cases: a definition child (in its parent's child array, +0x11 = 0) would lose its
  parent pointer but stay in the child array (GUESS: undefined; no shipped script does it,
  the three sites detach entities the boss attached with `AttachEntity`, VERIFIED-DATA).
  Detaching twice is harmless.
- Corpus: 1 / 3 / 0; `bosses\boss_2\boss_2.scr` pc 60 and 64 (in the callback handler, the
  rocket named by `cb_parm1`), pc 448 (then sets the detached part's field 2 to 2.0: it becomes
  an enemy).
- Priority: P1.

---

## C. Movement, orientation, waypoints and terrain

### 49. `RotateTo` — as2@0x4209b0 (changed)

- Signature: same.
- **Changed**: first the done flag is set to 0 (as2@0x4209e7); then if the `target`
  **reference** is 0 or self's raw entity is 0, return at once: nothing turns, done stays 0
  (as2@0x4209e3..0x4209f9). v1.70 had no test (v170@0x41b750) and crashed on a 0 reference.
  Otherwise steps 1–7 are the same, D8 defect included (inverse as2@0x417250), and the done
  flag is accumulated directly in the done-flag global (same result as v1.70's local).
  VERIFIED-CODE.
- Consequence: `lRotateTo` on a 0 target waits forever unless a timeout was set.
  `RotateToClamp` calls the same step, so it gets the test too.
- Corpus: 31 / 31 / 0; `RotateTo($player, …)` in turret scripts.
- Priority: P0.

### 50. `lRotateTo` — as2@0x4209b0 (changed)

- Same implementation as `RotateTo` (table alias as2@0x4209b0, v1.70 v170@0x41b750), with the
  same change: done 0 and no turn for a 0 target or self (as2@0x4209e3..0x4209f9). Used only
  with LCALL (70 sites, VERIFIED-DATA); such a wait never ends without a timeout.
- Priority: P0.

### 95. `TerraMorph` — as2@0x421a90 (new)

- Signature: `pos` vec (world position; only x and y are read), `name` string (path of an
  8-bit greyscale TGA "morph map", e.g. `morphmaps/map2.tga`). Returns: none. Latent: no.
- Terrain facts used (VERIFIED-CODE, terrain loader as2@0x41bd90, hmap.md "Vertex grid"):
  the terrain is a grid of (W+1) × (H+1) vertices, vertex (c, r) at x = 40c, y = 40r, with
  a height z(c, r) = hmin + (hmax − hmin) × raw / 255 kept as a float in the vertex array
  (as2@0x2102e5c, 12 bytes per vertex, index r × (W+1) + c); `hmin`, `hmax` from the level's
  `levels.txt` block (as2@0x2102dc4, as2@0x2102dc8); the level's water flag
  (as2@0x2103ca4) and water level (as2@0x2103cb0).
- Behaviour:
  1. **Stamp lookup** (as2@0x41a560). Compare `name` byte for byte (case-sensitive) with the
     names of the stamps already loaded in this level, in load order; use the first match.
     Otherwise, if 64 stamps are already loaded, stop (nothing changes). Otherwise load it:
     the stored name is `name` with its extension (from the last `.` after the last `/` or
     `\`) replaced by `.tga` (as2@0x422c60, as2@0x41a60a); the file is read with the raw TGA
     reader as2@0x41c3d0 (uncompressed types 1, 2 and 3 only, no row flip, the ID field is not
     skipped). If the file cannot be read, or its bits per pixel are not 8, stop (a failed
     load does not use up one of the 64 slots, so it is retried at the next call); otherwise
     the stamp is kept for the rest of the level. VERIFIED-CODE. The level loader empties the
     table (as2@0x41bd90). All five shipped stamps are type 3, 8 bits, descriptor 0x08,
     4×4 to 11×11 (VERIFIED-DATA).
  2. **Placement**. w, h = the stamp's width and height (TGA header); half-widths
     hw = floor(w / 2), hh = floor(h / 2). c0 = trunc(pos.x / 40 − hw), r0 = trunc(pos.y / 40 −
     hh), with trunc toward zero (as2@0x41a6a0..0x41a6f1; the cell size 40.0 is a constant
     here, as2@0x48f200). For positions with pos.x ≥ 40·hw this puts stamp pixel (hw, hh) on
     vertex (floor(pos.x / 40), floor(pos.y / 40)), the vertex at the lower-left corner of the
     cell that contains `pos`; there is no rounding to the nearest vertex.
  3. **Orientation and scale**: one stamp pixel per terrain vertex (40 world units); stamp
     column i goes to vertex column c0 + i (+x), and the i-th byte of stored row j goes to
     vertex row r0 + j (+y), in **file order**: the first stored row (the bottom row of a
     bottom-up TGA) is the lowest y. No rotation, no scaling, no flip. VERIFIED-CODE
     as2@0x41a77c..0x41a78b (pixel index = j × w + i).
  4. **Height change**: for each pixel (i, j) with c = c0 + i, r = r0 + j:
     - skip it if c < 0, c > W, r < 0 or r > H (vertices outside the grid are simply
       ignored; the rest of the stamp still applies);
     - if the level has water, skip it unless the vertex's current z ≥ water level (or the
       comparison is unordered) (as2@0x41a730..0x41a760): underwater ground never changes;
     - otherwise z(c, r) += (p − 128) × (hmax − hmin) / 255, p = the pixel value 0..255,
       computed in extended precision and stored as a float (as2@0x41a687..0x41a69a,
       as2@0x41a78f..0x41a7a1).
     So **128 is neutral**, each grey level is exactly one heightmap level of this map, the
     change is **additive** to the current height, and there is **no clamping** (a vertex
     can go below hmin or above hmax; morphs at the same place accumulate).
  5. Nothing else is done: no normals, no vertex colours or lighting, no terrain texture,
     tile or water data is recomputed, nothing is marked dirty, no entity is notified.
     VERIFIED-CODE: as2@0x41a670 writes only the vertex array, and the other terrain builders
     (as2@0x41b2e0, as2@0x41b580, as2@0x41b780, as2@0x41ba70, as2@0x41bb60: normals, lighting,
     textures, water weights; their split between these five was not read) are called only
     by the terrain loader as2@0x41bd90.
- What the change affects afterwards (VERIFIED-CODE unless marked):
  - **Rendering**: the terrain renderer copies the vertex positions from the vertex array
    every frame for every visible chunk (as2@0x437c60, as2@0x4377d0), so the new shape is
    visible from the next frame, instantly, with nothing animated over time. Its colours come
    from the static per-vertex lighting computed at load (normals as2@0x2102dcc) plus the
    dynamic lights (as2@0x41a7d0, using the load-time normals): the crater keeps the old
    shading and the old height-banded texture. Chunk culling boxes use hmin..hmax, so a vertex
    pushed outside that range may be culled slightly early (GUESS: not visible in practice).
  - **`TerrainHeight`** (as2@0x41a1e0) reads the vertex array, so queries return the new
    heights at once. Entities with FL_ONGROUND are snapped to TerrainHeight at every think
    (transform routine as2@0x40c750 calls it), so objects standing in the area sink or rise
    with the ground from their next think. VERIFIED-CODE for the call; the snap conditions
    are those of engine-behaviour.md §4 (not re-read for `as2`).
  - **Ground marks** and **baked projected shadows** are built from the vertex array when
    their entity is spawned (decal builder as2@0x431ef0 from the spawn routine as2@0x4124c0;
    shadow bake as2@0x432120 from `create`) and are not rebuilt: an older mark keeps the old
    shape, a mark created after the morph (the shipped explosions create `mark` right after
    `TerraMorph`) follows the crater. Per-frame draped shadows read the vertex array each
    frame (as2@0x432b80) and follow it. VERIFIED-CODE for the reads; which object uses which
    shadow kind: engine-behaviour and render deltas.
  - **Water surface**: vertices under the water level are never changed (step 4); the water
    mesh copies non-water vertices from the vertex array when drawn (as2@0x41d8d0).
  - **Persistence**: the change lasts until the level is loaded again, which rebuilds the
    vertex array from the heightmap (as2@0x41bd90). It is not saved.
  - **Limits**: no limit on the number of morphs; at most 64 distinct stamps per level.
- Edge cases: `pos` outside the map: only the part of the stamp that falls on the grid is
  applied (for −40·hw < pos.x < 0 the truncation toward zero shifts the stamp by one column
  compared with a floor; same for y). An empty or unreadable file: nothing happens.
  A stamp that is not 8-bit: nothing happens.
- Consequence of rcsl-vm.delta.md quirk 3: in `bonuses\satellite_strike\satellite_strike.scr`
  the `TerraMorph` of `init` (pc 50) is never reached in the original.
- Corpus: 7 / 8 / 0 (VERIFIED-DATA). Always `pos` = `&self.origin` and a string literal:
  `morphmaps/map2.tga` (`effects\expl_big.scr` pc 4), `morphmaps/map_medium.tga`
  (`effects\expl_medium.scr` pc 4, `effects\expl_meteorite_falled.scr`,
  `effects\expl_meteorite_medium_falled.scr`), `morphmaps/map1.tga` then, 0.6 s later,
  `morphmaps/map1_big.tga` at the same place (`effects\expl_abomb.scr` pc 6, 30),
  `morphmaps/map1_big.tga` (`rocket_launcher_big\expl.scr`), `morphmaps/map1.tga`
  (satellite strike, unreachable). `map_small.tga` is not used.
- Priority: P0 (explosions of mission 1).

### 67. `WaterHeight` — as2@0x420d80 (new)

- Signature: `x`, `y` float, world units. Returns: float. Latent: no.
- Behaviour (as2@0x41d530):
  1. If the level has no water (as2@0x2103ca4 = 0): return `TerrainHeight(x, y)`.
  2. Otherwise the same bilinear interpolation, bounds and "0 outside" rule as
     `TerrainHeight` (hmap.md), but over the **water surface grid** (as2@0x2102e6c, same
     layout as the vertex array) instead of the terrain vertices. VERIFIED-CODE.
- The water surface grid is written by the renderer for each visible chunk every frame
  (as2@0x41d8d0): a vertex with no water weight (weight < 0.0001, weights computed at load)
  or on the first or last row takes the terrain height; a water vertex takes the water level
  plus an animated wave term built from two C-runtime trigonometric calls of the vertex
  position and `time`, amplitude 16 (as2@0x2103cb4) × weight × 0.5 (VERIFIED-CODE for the
  structure; the exact wave arguments were not decoded: open question 3). Chunks not drawn
  keep older values.
- Corpus: none. Priority: P2.

---

## D. Weapons, targeting and damage

Target classes (changed): native code still recognises **enemies** by class 2.0; `as2` adds
**civilians**, class 5.0 (object keyword `civilian`). Which builtins consider civilians:
`RadialDamage` (always), `TraceLine` (mask bit 0x4), `TraceLineDamage` (touch bit 0x4); not
`LockTarget`, `Lightning`, `create`'s enemy count, `IsValidTarget` (no class test).

### 46. `Shoot` — as2@0x420190 (changed)

Compared step by step with the base entry (including issue 120's correction of step 1):

- Step 1, **changed**: return (no projectile, no flash) if S's on-screen bit (+0x20 & 0x08) is
  clear, if S's activation state (+0x1C) is 2, **or if S's field 4 (dead) is not exactly 0**
  (a NaN counts as not 0) (as2@0x42019b..0x4201bc; v1.70 had only the first two tests,
  v170@0x41b0db..0x41b0e9). An entity marked dead (by G_Damage or by its own script) can no
  longer fire, even while its death sequence still runs. VERIFIED-CODE.
- Step 2 muzzle: same (as2@0x40b580 identical to v170@0x4046a0 apart from offsets; tag
  lookup as2@0x417ef0, last-think base origin fields 41–43 and axis 44–52, scale > 0.01).
- Step 3 weapon lookup: same (as2@0x415630 identical to v170@0x40c990, same log message).
- Steps 4–8: same (projectile from W's `missile` built by as2@0x411e90 at (0, 0, 0); class
  4.0; origin and previous origin = M; velocity = normalised `dir`, angles by D6; model pushed
  forward by −min.y; velocity × `speed`). The object builder itself changed (field 23 is
  initialised from the definition: rcsl-vm.delta.md), not `Shoot`.
- Step 9: same (projectile `init` through the event runner as2@0x41f130, direct children's
  `init`, one think as2@0x40cb30).
- Step 10 difficulty scaling: same (compares `self` with the `player` globals
  as2@0x2106308 / 0x210630c; factor as2@0x49ded8).
- Step 11 two-player index and bits 0x2000 / 0x4000: same (as2@0x42048a..0x420528, flag
  as2@0x2219145).
- Step 12 flash: same (parent S, tag, `abs` = 1, active, counted; the root count increment
  moved into a helper, as2@0x40b260, which also tolerates a 0 pointer).
- Returns: none, as in v1.70. Corpus: 146 / 1092 / 0; the player scripts
  (`player\player1\player1.scr`), `bosses\boss_2\boss_2.scr`. Priority: P0.

### 48. `RadialDamage` — as2@0x4206b0 (changed)

- Signature: same.
- **Changed filter**: an entity is considered if not removed, its class is **2.0 or 5.0**
  (as2@0x420700..0x420724, constants 2.0 at as2@0x48f1c0 and 5.0 at as2@0x48f1b8) and its
  health > 0; v1.70 accepted class 2.0 only (v170@0x41b620..0x41b631). Distance, the
  `d ≤ radius or unordered` test, the damage frametime × damage × (d / radius) ×
  g_damage_factor (FDIVP divides the distance by the radius, as2@0x4207c8), the attacker (D4)
  and G_Damage (D5) are the same. Still no on-screen, FL_NONTARGET or touch-mode test; players
  are still never hit (see `RadialDamagePlayer`). VERIFIED-CODE.
- Corpus: 5 / 5 / 0; `effects\wave.scr` (100), `effects\p_wave.scr`, `effects\wave_big.scr`,
  `effects\wave_nvisible.scr`. Priority: P0.

### 56. `RadialDamagePlayer` — as2@0x420840 (new)

- Signature: `center` vec, `radius` float (world units), `damage` float (hit points per
  second at the rim). Returns: none (D3). Latent: no.
- Behaviour: for each player record i = 0 .. number of players − 1 (as2@0x49ded0), in order:
  1. P = the record's entity pointer (read without a test).
  2. Skip P if its removed bit (+0x20 & 0x01) is set or its health (field 34) is not > 0
     (NaN passes).
  3. d = |P.origin − center| (3D, rounded to float).
  4. If d ≤ radius or unordered: G_Damage(P, frametime × damage × (d / radius) ×
     g_damage_factor, attacker D4) (D5: a player with a non-zero freeze counter, in god mode
     or with field 4 ≠ 0 takes nothing; the damage handler runs).
  VERIFIED-CODE as2@0x420840..0x4209a7 (the same code as `RadialDamage` with the player loop
  in place of the live list and no class test).
- Consequences: as for `RadialDamage`, 0 at the centre and maximal at the rim; the caller
  grows the radius every frame. Enemy fire is scaled by g_damage_factor once here.
- Edge cases: radius 0 with a player exactly at the centre gives NaN damage (guard it, as the
  base does for `RadialDamage`). A record without an entity would crash the original (GUESS:
  the players' records always hold an entity during play).
- Corpus: 1 / 1 / 0; `rocket_launcher_big\wave.scr` pc 14 (`RadialDamagePlayer(&self.origin,
  100 × scale, 2000)`).
- Priority: P1.

### 66. `TraceLine` — as2@0x420fd0 (changed)

- Signature: same (`mask` int: 0x2 players, 0x1 enemies, **0x4 civilians**).
- **Changed behaviour** (as2@0x420fd0..0x421177; v1.70 v170@0x41bd40..0x41be96):
  1. Project both points (D9).
  2. m = ftol(mask). If m = 0: return 0.0.
  3. If m & 2: players as in v1.70 (field 4 = 0 and crossed: return that player).
  4. If m is exactly 2: return 0.0.
  5. Walk the live list newest first; stop at the first entity that is not removed, is on
     screen, is (class 2.0 and m & 1) or (class 5.0 and m & 4), has health > 0 and is
     crossed. Return its reference, or 0.0 if none.
  VERIFIED-CODE. For m = 1, 2 and 3 the result is the same as in v1.70.
- Corpus: none. Priority: P2.

### 67. `TraceLineDamage` — as2@0x421180 (changed)

- Signature: same.
- **Changed**: the filter is self's touch mode (+0x60) read as a bit set, like the touch pass
  (rcsl-vm.delta.md):
  1. Project both points (D9).
  2. m = self's touch mode. If m = 0: nothing.
  3. If m & 0x2: every player (field 4 = 0, crossed) takes G_Damage(player, damage, −1).
  4. If m is not exactly 2: every live-list entity that is not removed, on screen, (class 2.0
     and m & 1) or (class 5.0 and m & 4), a model (field 38 = 0), without
     FL_POINT_COLLISION, with field 4 = 0 and crossed takes G_Damage(E, damage, −1).
  VERIFIED-CODE as2@0x421180..0x42130f. v1.70 hit players only for mode exactly 2 and
  enemies only for mode exactly 1 (v170@0x41bea0..0x41bff4). Consequence: `TOUCH_ALL` (0xF)
  and mode 3 now hit players, enemies and (for 0xF) civilians; in v1.70 mode 3 hit nothing.
  Damage still unscaled, no score.
- Corpus: 2 / 2 / 0; `weapons\laser\biglaser_proj.scr` and its enemy twin. Priority: P0.

### 68. `Lightning` — as2@0x421310 (changed)

- **Signature changed**: `radius` float (t0, world units). Returns: none (D3). Latent: no.
- Behaviour, for each live-list entity E (next pointer read before E is processed):
  1. Skip unless E's field 4 = 0, E's on-screen bit is set and class = 2.0 (same test as
     v1.70; removed bit still not tested; civilians not struck).
  2. **Changed**: d = √((E.x − self.x)² + (E.y − self.y)²), the **horizontal** distance
     (z is ignored), rounded to float; skip unless d ≤ `radius` (or unordered)
     (as2@0x4213aa..0x421404). v1.70: 3D distance against the constant 500
     (v170@0x41c0ef).
  3. Queue the lightning render record from self's origin to E's origin (as2@0x430740; render
     flags and type as in v1.70).
  4. G_Damage(E, self's field 35 × frametime × g_damage_factor, −1). Same.
  5. **New**: if E's timer (entity + 0x78, which the entity update grows by frametime up to
     5.0) is ≥ 0.2 (as2@0x48f4c0): set it to 0 and spawn a `wavegun_hit` object at E's origin
     (definition index looked up by name at level start, as2@0x410f4d..0x410f5f, kept at
     as2@0x2106334;
     spawn routine as2@0x4124c0 with the ground and water snap; its `init` is **not** run, its
     `main` runs from the next entity pass) (as2@0x42144e..0x42146e). VERIFIED-CODE.
- Consequences: at most one hit effect per target every 0.2 s of that target's life; a target
  whose timer never reached 0.2 (spawned less than 0.2 s ago) gets no effect. D5 may run the
  target's handlers before step 5, and a target killed by step 4 still gets the effect.
- Corpus: 2 / 2 / 0 (VERIFIED-DATA): `bonuses\lightingbomb\lightingbomb_proj.scr` (radius
  500) and `weapons\lightinggun\lightinggun_proj.scr` (radius 200).
- Priority: P0.

---

## E. Effects, lights, camera and sound

No builtin of family E changed. `PlaceLight`, `CameraQuake`, `StartSound`,
`StartLoopingSound`, `StopLoopingSound` are the same code (sound registry as2@0x422af0 same
as v170@0x41ff90 apart from stack offsets). New in this family:

### 98. `GetMapPosOfs` — as2@0x421b20 (new)

- Signature: none. Returns: float, world units. Latent: no.
- Behaviour: return the y offset of the current camera mode: the engine's camera mode index
  (as2@0x49db68, an integer 0..3, initial 1, changed by the settings and a key,
  as2@0x415290) selects a 16-byte row of the camera table at as2@0x49db2c (pitch, height,
  y offset, field of view); the builtin returns the row's third float
  (as2@0x421b28). The camera update places the camera at y = `g_map_pos` + this offset
  (as2@0x41504c). Values (initialised data): 0.0 for modes 0, 1 and 2, 100.0 for mode 3.
  VERIFIED-CODE.
- Use: a script can convert `g_map_pos` to the camera's actual y. Not related to the script
  global `cameramode` (rcsl-vm.delta.md: nothing reads it).
- Corpus: none. Priority: P2.

---

## F. Level flow, player, HUD and items

"Self's player" is the record selected by self's +0x80. Integer arguments of the G_ builtins
are still truncated with the forced rounding control and range-tested unsigned
(VERIFIED-CODE, e.g. as2@0x421559..0x4215b3).

### 2. `EndLevel` — as2@0x40e730 (changed)

- Signature: same.
- **Changed behaviour** (v1.70 v170@0x407570 only set the HUD-hidden and paused flags and
  started the mission-complete sequence):
  1. Level-progress value as2@0x49ddf4 = current level index (as2@0x2219140) + 1 (GUESS: the
     highest level unlocked).
  2. For both player records (whatever the number of players): +0x158 = trunc(`p_lives`);
     +0x15C = trunc(+0x140 + `p_scores`) (int + float, then truncated); +0x160 = +0x144 +
     0.5 × `p_scores` / A + `p_stars` / B, where A = the level's total score of placed objects
     (as2@0x5432c4, summed by the map spawner) and B = the number of star items placed in the
     level (as2@0x543294, counted by the map loader) (as2@0x40e745..0x40e7b4). GUESS for the
     names of A, B and the record fields (a per-mission rating); VERIFIED-CODE for the
     arithmetic. A or B = 0 gives an infinite or NaN rating.
  3. Level-end flag as2@0x54329b = 1; the front end's level-end sequence as2@0x4234b0 is
     called with (current level index, 1) (it may push a debriefing menu); then the
     world-stopped flag as2@0x543298 = 1 (tested by the entity update and the player input,
     so no `main` runs and no input is applied from the next frame). VERIFIED-CODE.
  The front-end flow belongs to a frontend delta (not written yet).
- Corpus: 4 / 4 / 0; `eol.scr` and the bosses. Priority: P0.

### 3. `GameOver` — as2@0x410e70 (new)

- Signature: none. Returns: none. Latent: no.
- Behaviour: game-over flag as2@0x543299 = 1, level-end flag as2@0x54329b = 1, world-stopped
  flag as2@0x543298 = 1; then the music routine as2@0x422880 (stops the current module and,
  GUESS, starts the game-over tune), the game-over menu is built (as2@0x428db0, with a
  " Restart " button) and pushed (UI_PushMenu). VERIFIED-CODE for the sequence; it is exactly
  what the player input routine does when every player has `p_lives` < 0 (as2@0x413d02..
  0x413d51). The calling script finishes its invocation.
- Corpus: none in `as2`. Priority: P2.

### 1. `RespawnPlayer` — as2@0x421a50 (changed)

- Signature: same. The wrapper is the same code (self = player 1's entity: spawn 0; self =
  player 2's: spawn 1).
- **Changed G_SpawnPlayer** (as2@0x413b20 against v170@0x40b2f0):
  - Base step 2 is **gone**: there is no `p_lives < 0` test (v170@0x40b309..0x40b328 set the
    old entity's field 4 and stopped). A new helicopter is always created; game over is
    detected independently by the player input routine when `p_lives` < 0 (see `GameOver`).
    VERIFIED-CODE.
  - Step 4: the helicopter object is looked up by name, `player_1` .. `player_N`, from a
    table of 33-byte names at as2@0x49ddf9 indexed by the record's helicopter index (+0x88),
    then built; freeze counter (+0xB8) and action-disabled byte (+0xBC) reset. VERIFIED-CODE.
  - Steps 1, 3, 5 same (remove the old entity; clear both records' power-up and missile
    counts and selections; two-player x ∓ 100 after `init`). No immediate think.
- Corpus: 6 / 6 / 0; `player\player1\player1.scr` (and players 2, 3, 5, 6). Priority: P0.

### 80. `G_UsePowerUp` — as2@0x421620 (changed)

- Signature, return: same. The use routine as2@0x413710 is the same as v170@0x40b150.
- **Changed next selection** (as2@0x413680 against v170@0x40b0d0): when a use empties the
  selected kind `sel`, the candidates sel+1, sel+2, … sel+15 are tried in order, and a
  candidate whose value **before** the wrap-around is 6, 7 or 9 is skipped; otherwise the
  candidate taken is (value mod 16) if its count is non-zero. If none qualifies, the
  selection becomes −1 (the old kind has count 0). VERIFIED-CODE.
- Consequence: the timed effects that scripts show as counts (6 speed-up, 7 speed-down, 9
  shield: see `G_SetPowerUpCount`) are never auto-selected after the selected bomb runs out.
  A value above 15 is not skipped even if it wraps to 6, 7 or 9 (for example sel = 12 and
  candidate 22 → kind 6): reproduce as is.
- Corpus: 6 / 42 / 0. Priority: P0.

### 82. `G_SetPowerUpCount` — as2@0x421650 (new)

- Signature: `type` int, `count` int. Returns: none. Latent: no.
- Behaviour: c = trunc(count) (C conversion, toward zero), and 0 if negative
  (as2@0x42165d..0x421679); t = trunc(type) (forced truncation, as2@0x421683..0x421698).
  Self's player's power-up count t (record + 0xC0 + 4t) = c. The selection is not touched
  and **t is not range-checked** (as2@0x4216a2). VERIFIED-CODE.
- Edge cases: a type outside 0..15 overwrites other record fields in the original; ignore it
  (engine decision). Setting the selected kind to 0 leaves it selected; the next
  `G_UsePowerUp` then returns 0 without changing the selection.
- Consequences: scripts use kinds 6, 7 and 9 as countdown displays: the HUD shows the count
  of each kind (frontend delta).
- Corpus: 3 / 3 / 0 (VERIFIED-DATA): `player\common\p_shield.scr` pc 20
  (`G_SetPowerUpCount(9, 20 − self.age)` every frame), `p_speedup.scr` (6, 15 − age),
  `p_speeddown.scr` (7, 10 − age).
- Priority: P0.

### 86. `G_GetUpgrade` — as2@0x4217c0 (changed)

- **Changed**: i = trunc(index); if i > 8 as an unsigned number return 0.0 (as2@0x4217e6), else
  the upgrade level of weapon i of self's player (record + 0x11C + 4i). v1.70 accepted 0..19
  (v170@0x41c3f6). VERIFIED-CODE.
- Corpus: 16 / 16 / 0; the weapon pick-ups. Priority: P0.

### 87. `G_SetUpgrade` — as2@0x421830 (changed)

- **Changed**: stores trunc(value) only if trunc(index) < 9 (as2@0x421859), into record +
  0x11C + 4i; v1.70 bound 20 (v170@0x41c469). VERIFIED-CODE.
- Corpus: 10 / 10 / 0. Priority: P0.

### 99. `GetPlayerAccel` — as2@0x421b40 (new)

- Signature: `out` vec_out. Returns: none (the return register is not written). Latent: no.
- Behaviour: if self's raw entity is player 1's entity (record 0, +0x00): out = record 0's
  acceleration vector (+0x14C, +0x150, +0x154); then, if self's raw entity is player 2's
  entity: out = record 1's vector. Otherwise `out` is not written. VERIFIED-CODE
  as2@0x421b40..0x421b9e.
- The acceleration vector is the player's steering input, computed by the engine once per
  frame just before the entity pass (as2@0x413c50, called by the frame function
  as2@0x410fd0 at as2@0x411040 unless the game-over flag as2@0x543299 or the flag as2@0x54329a is set, and
  not at all once the world-stopped flag as2@0x543298 is set; VERIFIED-CODE):
  1. For each player: the vector = (0, 0, 0).
  2. If the record's action-disabled byte (+0xBC) is 0, with a = ftol(`p_action`): bit 0x80
     → x += 1; bit 0x10 → y += 1; bit 0x40 → x −= 1; bit 0x20 → y −= 1. If both x and y
     are then non-zero (a diagonal), the vector is scaled to length 1.
  3. Player 1 only: if its action-disabled byte is 0, the vector is still (0, 0) and mouse
     control is on (as2@0x49f908 ≠ 0; GUESS: the mouse option), the vector = (cursor.x −
     window centre x, window centre y − cursor.y) in pixels; if non-zero it is scaled to
     length **2.0**. The cursor is then put back to the window centre (every frame, mouse
     control or not).
  z is always 0. VERIFIED-CODE (constant 2.0 at as2@0x48f1c0; setlen helper as2@0x416dc0).
- Consequences: the player scripts add out × frametime × 1000 to the velocity
  (`player\player1\player1.scr` pc 1383..1395), so keys give up to 1000 units/s² and the
  mouse 2000, whatever the mouse speed. `p_action` bits are those of engine-behaviour.md
  §7.2 (0x10 forward, 0x20 back, 0x40 left, 0x80 right). Our engine maps touch and keys to
  the same bits.
- Edge cases: called by a non-player entity it leaves `out` unchanged.
- Corpus: 6 / 6 / 0; the five player scripts and one duplicate. Priority: P0.

### 100. `GetPlayersDistance` — as2@0x421ba0 (new)

- Signature: none. Returns: float. Latent: no.
- Behaviour: the result is −1.0 (as2@0x48f280) unless the game is in two-player mode
  (as2@0x2219145), both players' `p_lives` are ≥ 0 and self's raw entity is one of the two
  player entities; then it is self's origin y minus the other player's origin y (field 6):
  a **signed y offset**, not a distance (as2@0x421be6..0x421c16). VERIFIED-CODE.
- Corpus: none in `as2` (Gulf Thunder uses it). Priority: P2.

### 96. `IsMultiplayer` — as2@0x421ab0 (new)

- Signature: none. Returns: 1.0 in two-player mode (byte as2@0x2219145 ≠ 0), else 0.0.
  VERIFIED-CODE. Corpus: none in `as2`. Priority: P2.

### 97. `IsPlayerInGame` — as2@0x421ae0 (new)

- Signature: `index` int. Returns: 1.0 or 0.0. Latent: no.
- Behaviour: i = ftol(index); return 1.0 if player record i's entity pointer is non-zero, else
  0.0. No range check (as2@0x421ae8..0x421afa). VERIFIED-CODE.
- Edge cases: an index outside 0..1 reads other memory in the original; return 0.0. In a
  one-player game record 1 has no entity (GUESS: its pointer stays 0). The entity pointer is
  not cleared when the helicopter is removed, so "in game" means "was spawned this level".
- Corpus: none. Priority: P2.

---

## G. Other

`debug` and `sleep`: same.

---

## Notes for implementers (most important first)

1. **`TerraMorph` makes the terrain mutable.** Keep the vertex heights in a mutable array
   that `TerrainHeight` and the renderer read every frame; apply stamps additively, 128
   neutral, one grey level = (hmax − hmin)/255, no clamp, skip underwater vertices; do not
   recompute normals, lighting or textures if you want the original look (issue 200).
2. **`Shoot` refuses dead shooters** (field 4 ≠ 0) in addition to off-screen and leaving ones.
3. **Player movement comes from `GetPlayerAccel`**: the engine turns the direction bits of
   `p_action` (and the mouse for player 1) into a unit (or length-2) vector each frame.
4. **Civilians** (class 5.0) exist: `RadialDamage` hits them, `TraceLine` and
   `TraceLineDamage` hit them when asked by bit 0x4; `LockTarget` and `Lightning` ignore them.
5. **`TraceLineDamage` uses the touch mode as a bit set** (3 and 0xF hit players and enemies).
6. **`Lightning` takes a radius** and measures it horizontally; it spawns `wavegun_hit` at most
   every 0.2 s per target, without running its `init`.
7. **`create` returns the new entity**, never a value left by the new entity's scripts.
8. **`RespawnPlayer` always spawns**; game over is the engine's job (`p_lives` < 0).
9. **9 upgrade slots**; power-up auto-selection skips kinds 6, 7, 9; `G_SetPowerUpCount`
   writes a count directly (kinds 6, 7, 9 are timers shown as counts).
10. Null guards: `AttachEntity` ignores a null parent, `RotateTo` a null target (done 0).

## What an implementer must change

Against the builtins as the base spec describes them (and our `as3d` implementation):

1. Use the `as2` offsets and player-record layout of section 1 (9 upgrade slots, freeze
   counter and action-disabled byte, acceleration vector).
2. G_Damage (D5): cap the kill counter at the level's enemy total.
3. `create`: write the result after the new entity's `init` and first think.
4. `AttachEntity`: do nothing if the parent is null. Add `DetachEntity`.
5. `RotateTo`/`lRotateTo` (and `RotateToClamp` through it): with a null target or self, set
   done = 0 and return.
6. `Shoot`: add the dead-shooter test.
7. `RadialDamage`: accept class 5.0. Add `RadialDamagePlayer`.
8. `TraceLine`: mask 0 → 0, bit 0x4 civilians, mask exactly 2 → players only.
   `TraceLineDamage`: touch mode as a bit set with the civilian bit.
9. `Lightning`: read the radius from t0, horizontal distance, 0.2 s `wavegun_hit` effect using
   the per-entity timer of rcsl-vm.delta.md.
10. `atan`: degrees of atan(x). Add `atan2`, `copysign`, `floor`, `floor2`, `fmod`.
11. `EndLevel`: level statistics and the new level-end sequence (with the frontend delta).
    Add `GameOver`.
12. `RespawnPlayer`: drop the lives test; choose the helicopter `player_<n>` from the record.
13. `G_UsePowerUp`: skip 6, 7, 9 in the next selection. `G_GetUpgrade`/`G_SetUpgrade`: bound 9.
    Add `G_SetPowerUpCount`.
14. Add `GetPlayerAccel` with the engine's per-frame acceleration computation, and
    `GetPlayersDistance`, `IsMultiplayer`, `IsPlayerInGame`, `GetMapPosOfs` (with the camera
    table), `WaterHeight`.
15. Add `TerraMorph` with a stamp cache of 64 per level and a mutable terrain (issue 200).

## Open questions

1. `EndLevel` statistics: the meaning of record +0x140, +0x144 and +0x158..+0x160 and of the
   globals as2@0x5432c4 and as2@0x543294 is inferred from their arithmetic (GUESS); the
   frontend delta should confirm them from the screens that read them.
2. What the level-end sequence as2@0x4234b0 and the game-over menu show (frontend delta).
3. `WaterHeight`: the exact arguments of the two trigonometric wave terms in as2@0x41d8d0
   (unused builtin; belongs to a render delta).
4. `GetPlayerAccel`: the option behind as2@0x49f908 (mouse control) and the meaning of the flag
   as2@0x54329a that also suspends the input routine (GUESS: pause).
5. `DetachEntity` on a definition child (never done by shipped scripts).
6. D2 (stale references, freeing) was not re-read for `as2`.

## Corrections to other specs

Listed here, not applied.

| Spec, place | Says | Correct | Evidence |
|---|---|---|---|
| rcsl-builtins-table.delta.md, row `atan` | "same logic (calls another C runtime arctangent entry)" | Different result: `as2` returns atan(x) in degrees; v1.70 returned atan(x·π/180) in radians | v170@0x41a417..0x41a423 scale the argument; as2@0x41f348..0x41f359 scale the result by 180/π |
| rcsl-builtins-table.delta.md, row `RespawnPlayer` and "66 same code" | same code | The wrapper is the same, but its G_SpawnPlayer lost the `p_lives < 0` test: a helicopter is always spawned | v170@0x40b309..0x40b328 against as2@0x413b31..0x413b36 |
| rcsl-builtins-table.delta.md, row `G_UsePowerUp` | "same logic (register allocation only)" | The wrapper is, but the next-selection routine skips kinds 6, 7, 9 | v170@0x40b0d0 against as2@0x413680 (tests at as2@0x4136a0..0x4136aa) |
| rcsl-builtins-table.delta.md, rows `G_GetUpgrade`/`G_SetUpgrade` | "upgrade index bound is 9 instead of 20" | Correct as "index < 9", i.e. 9 slots 0..8 (`G_GetUpgrade` returns 0.0 for 9) | as2@0x4217e6, as2@0x421859 |
| rcsl-builtins-table.delta.md, "Code against v1.70" `same` | "identical instruction sequences" | True for the builtin bodies only; the called engine routines were not compared there (two differ: G_SpawnPlayer, G_Damage) | section "Spot checks" above |
| rcsl-vm.delta.md, `damage` row | "a per-player counter is no longer increased past a level value" | That counter is the kill counter (record + 0x148) and the level value is the enemy total as2@0x5432c0 that `create` and the spawner increase | as2@0x40bb46..0x40bb5d, as2@0x41f996 |
| base semantics §1 via as2 | player record stride and fields | See section 1: stride 0x164, freeze counter +0xB8, action-disabled +0xBC, 9 upgrades at +0x11C | as2@0x4218d0, as2@0x421960, as2@0x4217c0 |

## Symbols added

`re/symbols_as2_builtins.csv`: the engine routines and data named in this delta (stamp
routine, stamp cache, terrain arrays, player input, spawn and decal routines, C runtime
math entries, level statistics globals), with their v1.70 counterparts where paired.

## Checked sections

| Base section | Status |
|---|---|
| 1. Conventions | changed (offsets, addresses, player record, civilian class) |
| 2. Shared definitions | changed: D5; D1 has new null tests in three builtins; D2 not checked; D3, D4, D6, D7, D8 same; D9 same rule |
| Summary table | changed (101 rows above) |
| Builtins no shipped script calls | changed (26 for `as2`) |
| Entry template | same |
| A. Math, random, flags, vectors and axes | changed: `atan`; new `atan2`, `copysign`, `floor`, `floor2`, `fmod`; the rest same |
| B. Entity creation, removal and hierarchy | changed: `create`, `AttachEntity`; new `DetachEntity`; the rest same |
| C. Movement, orientation, waypoints and terrain | changed: `RotateTo`, `lRotateTo`; new `TerraMorph`, `WaterHeight`; the rest same |
| D. Weapons, targeting and damage | changed: `Shoot`, `RadialDamage`, `TraceLine`, `TraceLineDamage`, `Lightning`; new `RadialDamagePlayer`; `Damage`, `LockTarget`, `IsValidTarget`, `FreezeHealth` same |
| E. Effects, lights, camera and sound | same; new `GetMapPosOfs` |
| F. Level flow, player, HUD and items | changed: `EndLevel`, `RespawnPlayer`, `G_UsePowerUp`, `G_GetUpgrade`, `G_SetUpgrade`; new `GameOver`, `G_SetPowerUpCount`, `GetPlayerAccel`, `GetPlayersDistance`, `IsMultiplayer`, `IsPlayerInGame`; the rest same |
| G. Other | same |
| Notes for implementers | changed (above) |
| Open questions | base 1–7 not re-examined for `as2`; new ones above |
| Corrections to other specs | not applicable (base corrections concern v1.70 specs) |
| Symbols added to `re/symbols_v170.csv` | not applicable (see Symbols added) |

## Changelog

- 1.0 (B3): first version. 101 builtins: 70 same, 15 changed, 16 new; priorities from a
  mission-1 reachability scan (66 P0); `TerraMorph` fully specified; corrections to the
  table and VM deltas listed.
