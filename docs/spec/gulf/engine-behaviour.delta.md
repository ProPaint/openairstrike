# Native game logic: Gulf Thunder delta (`gulf`)

Delta version 1.0 (package F1). Amends [../as2/engine-behaviour.delta.md](../as2/engine-behaviour.delta.md)
(which amends [../engine-behaviour.md](../engine-behaviour.md)) for the game `gulf`
(AirStrike II: Gulf Thunder v2.71, `AirStrike3D II - Gulf.exe`). Section numbers mirror the
base spec. Companions: [symbol-map.md](symbol-map.md), [rcsl-vm.delta.md](rcsl-vm.delta.md),
[rcsl-builtins-semantics.delta.md](rcsl-builtins-semantics.delta.md),
[frontend.delta.md](frontend.delta.md).

Tags: `VERIFIED-CODE` cites `gulf@0x…` and the AirStrike 2 counterpart `as2@0x…`. "Same code"
is the `same` class of the symbol map: identical instructions with addresses masked **and**
the same read-only constants, so a `same` function has the same rule values too. Tables in
writable data were read from the executable and compared separately (symbol-map.md, "Data").
`VERIFIED-DATA` holds for `assets_extracted_games/gulf/` (665 scripts, `maps/levels.txt`,
`objects/*.obj`).

## Summary for implementers

Gulf Thunder runs AirStrike 2's rules. Only counts and tables differ, by impact on play:

1. **Mission loadout table**: 24 rows, other values; operation 1 starts with the machine gun
   at **level 4** (AirStrike 2: level 1). It exists and must be set (the profile has none
   today) (8.2).
2. **Weapon slots** carry other weapons (photon gun and plasma laser are new; missile gun and
   flamethrower are never available): the upgrade slot meaning is script data, the count is 9
   as in AirStrike 2 (8.2).
3. **24 missions**, unlock and "Next" modulo 24; missions 1 and 2 unlocked by default
   (10.1, 10.3, 10.6).
4. **3 helicopters** `player_1`, `player_2`, `player_3` (in that order), unlock bound 3:
   operation 4 unlocks `player_2`, operations 9 and 10 unlock `player_3`, the `enableHelic 3`
   of operations 16 and 19 does nothing (7.6).
5. **Portrait dialogues** only at the start and end of operation 10 and at the start of
   operation 24 (10.2, 10.3).
6. Save payload for 3 helicopters and 24 missions (10.5).

Everything else (movement, lives, checkpoint, civilians, touch bits, water, skid marks,
statistics, cheats, camera, collision, damage, two-player rules) is AirStrike 2's: the code is
the same.

## 1. Program structure

### 1.1 Start-up: same

`WinMain` gulf@0x405c00 and `Sys_Init` gulf@0x4059e0: same code (window title
"Airstrike II: Gulf - Divo Games" gulf@0x0048a174; another CLSID under the registry key
written by gulf@0x4057e0; both irrelevant to an implementation).

### 1.2 Main loop and timing: same

Same code.

### 1.3 Game states: same (with other flag addresses)

Same code (`G_Init` gulf@0x40f8e0, `G_Frame`, `G_NewGame` gulf@0x40f7f0, `G_GameOver`
gulf@0x40f8a0). Flags: current mission gulf@0x02216f88, two players gulf@0x02216f8d, god mode
gulf@0x02216f82, cheat used gulf@0x02216f8e, checkpoint mission gulf@0x0049bc6c (−1),
paused gulf@0x005410e0, game over gulf@0x005410e1, intermission gulf@0x005410e2, HUD hidden
gulf@0x005410e3 (`re/symbols_gulf_data.csv`). The attract level is `intro1` or `intro2` by
`rand` parity (table gulf@0x0049bd50, same four names); both blocks of `levels.txt` use
`maps\intro.hsc` (VERIFIED-DATA). The intro before the attract level has **no comic**
(frontend.delta.md 3.2).

## 2. Per-frame update order: same

Same code (`G_Frame`, `G_RunEntities` gulf@0x40cd50, `G_StartLevel` gulf@0x40e170).

## 3. Entities: same

Same code for definitions, layout, pool, creation, activation and events (`G_InitObject`
gulf@0x4108c0, `ParseObject` gulf@0x410fd0). Civilians, skid marks, `speed`, water flags: as
AirStrike 2.

## 4. Movement and transforms: same

Same code.

## 5. Collision: same

Same code.

## 6. Damage and death: same

Same code (`G_Damage` gulf@0x40b970). The difficulty table gulf@0x0049bd00 has AirStrike 2's
values; default difficulty 2.

## 7. The player

### 7.1 to 7.5: same

Same code: player records of 0x164 bytes at gulf@0x020c3918 and gulf@0x020c3a7c, the
acceleration vector, relative mouse control, spawn (`G_SpawnPlayer` gulf@0x412550),
`G_PlayerFrame` gulf@0x412680, health clamped to the maximum by the HUD, 10 life icons, two
players. Helicopter data (VERIFIED-DATA, `objects/player.obj`): `player_1` health 500, speed
1.15; `player_2` 400, 1.3; `player_3` 300, 1.5.

Two-player use by the data (VERIFIED-DATA): Gulf Thunder's scripts call `IsMultiplayer` (1
script), `GetPlayersDistance` (3), `RadialDamagePlayer` (3) and use the globals `player1` and
`player2` (1 script, `bonus_level_start.scr`), `cameramode` (11); the rules of the AirStrike 2
delta 7.5 apply to them. Co-op was not played.

### 7.6 Helicopter choice: changed

Table gulf@0x0049bc70, **3** records of 33 bytes {u8 unlocked, name[32]} (VERIFIED-CODE, read
from the executable):

| Index | Object | Unlocked by default | Unlocked by (VERIFIED-DATA, `enableHelic` of `levels.txt`) |
|---|---|---|---|
| 0 | `player_1` | yes | – |
| 1 | `player_2` | no | operation 4 (`enableHelic 1`) |
| 2 | `player_3` | no | operation 9 or 10 (`enableHelic 2` in both) |

`G_MissionComplete` gulf@0x426560 unlocks entry `enableHelic` when 0 ≤ value < **3**
(as2@0x427f40: < 6). Operations 16 and 19 carry `enableHelic 3`: out of range, so nothing is
unlocked and the mission-complete screen shows no "New helicopter is available." line (its
test gulf@0x426361 uses the same bound 3). The selection menu cycles modulo 3
(`M_HeliSelectAction` gulf@0x4275e0).

## 8. Weapons and items

### 8.1 Weapon definitions and `Shoot`: same

Same code (`Shoot` gulf@0x41ebd0).

### 8.2 Weapon upgrades: changed (data)

- **9 slots**, same code (`G_GetUpgrade` gulf@0x420200, `G_SetUpgrade` gulf@0x420270,
  `G_NextWeapon` gulf@0x4122a0).
- **Weapon ids** (VERIFIED-DATA, `scripts\items\ammo\*.scr`: the slot each pick-up raises and
  its cap):

| Slot | Weapon (pick-up object) | Cap | AirStrike 2 slot of the same weapon |
|---|---|---|---|
| 0 | machine gun (`item_machinegun`) | 4 | 0 |
| 1 | impulse gun (`item_impulsegun`) | 5 | 1 |
| 2 | plasma gun (`item_plasmagun`) | 7 | 2 |
| 3 | **photon gun** (`item_photongun`) | 6 | new |
| 4 | laser (`item_laser`) | 8 | 3 |
| 5 | big laser (`item_laser_big`) | 5 | 4 |
| 6 | **plasma laser** (`item_plasma_lazer`) | 5 | new |
| 7 | lightning gun (`item_lightinggun`) | 5 | 5 |
| 8 | wave gun (`item_wavegun`) | 4 | 6 |
| (9) | missile gun (`item_missilegun`) | 5 | 7 |
| (10) | flamethrower (`item_flamefrower`) | 3 | 8 |

  Slots 9 and 10 are beyond the 9 slots the builtins accept: `G_GetUpgrade` returns 0 and
  `G_SetUpgrade` ignores them. No map places those two pick-ups and no script creates them
  (VERIFIED-DATA), so they never appear. The player scripts use the ids above.
- **Mission loadout table** gulf@0x00489bc0 (read from the executable), 24 rows × 9 ints,
  applied by `G_SetMissionLoadout` gulf@0x412320 with the row index clamped to **0..23**
  (as2: 0..17), on new game and on both restarts, not on "Next" (same code as AirStrike 2
  otherwise). `p_weapon` becomes the highest slot with a non-zero level.

| Operation | 0 MG | 1 impulse | 2 plasma | 3 photon | 4 laser | 5 big laser | 6 plasma laser | 7 lightning | 8 wave |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 4 | | | | | | | | |
| 2 | 4 | 5 | | | | | | | |
| 3 | 4 | 5 | 4 | | 3 | | | | |
| 4 | | 5 | 4 | 4 | 4 | | 2 | | |
| 5 | | | 4 | 4 | 4 | 3 | 2 | 3 | 4 |
| 6 to 24 | | | 7 | 6 | 7 | 4 | 4 | 4 | 4 |

  (Empty = 0. Rows 6 to 24 are identical. Note that the table gives slot 4 (laser) level 7
  although the HUD's level cap for that slot is 5, below.)
- **HUD level caps** (table gulf@0x0049c09c, read by `HUD_Draw1P`/`HUD_Draw2P`, same code):
  4, 5, 7, **6**, 5, 5, 4, 5, 3 for slots 0..8. Only slot 3 changed from AirStrike 2
  (4, 5, 7, 8, 5, 5, 4, 5, 3), so the caps of slots 4, 6 and 8 are AirStrike 2's and do not
  match the pick-ups' caps (8, 5, 4). The HUD draws the empty level bar 7 × cap wide and the
  filled part 7 × level wide (AirStrike 2 frontend 4.2, same code), so a laser at level 6 to 8
  overruns its empty bar; reproduce it (frontend.delta.md 4.2).

### 8.3 Missiles and power-ups: same

Same code and the same kinds (VERIFIED-DATA, every `G_AddPowerUp`, `G_AddMissiles` and
`G_SetPowerUpCount` call of the corpus has AirStrike 2's slot and amount): missiles 0 +20,
1 +12, 2 +20, 3 +15, 4 +12; power-ups 0 lightning bomb +2, 1 A-bomb +1, 2 rocket bomb +4,
3 cluster bomb +4, 4 annihilator +500, 5 satellite strike +2, 8 bomber +1; timers 6 speed-up,
7 slow-down, 9 shield. Cycling skips 6, 7, 9.

### 8.4 Targeting: same

## 9. Scrolling and camera: same

Same code (`V_UpdateCamera` gulf@0x4138c0, `V_ResetCamera` gulf@0x413cc0); camera modes
gulf@0x0049b9a0 same values; default mode 1.

## 10. Level flow

### 10.1 Level list: same format, 24 missions

The mission table gulf@0x0049bd78 has **24** entries `mission1`..`mission24`, 1 and 2
unlocked by default (VERIFIED-CODE, read). `levels.txt` has 26 blocks: 24 operations and two
attract blocks (VERIFIED-DATA). Roles (VERIFIED-DATA; no native code treats them specially):

| Operation | Role | Map |
|---|---|---|
| 10 "Prototype of Evil" | boss | `level10_boss.hsc` |
| 24 "Phoenix Burning" | boss (last) | `level24_boss.hsc` |
| 11 "Shooting Practice" | bonus | `level11_bonus.hsc` |
| 18 "Air-raid drill" | bonus | `level18_bonus.hsc` |

There is no tutorial operation: no `ShowTutorialHint` call is reachable from operation 1 (10
other scripts call it, see the builtins delta).

### 10.2 Level start: changed (dialogues, loading screen)

Same code (`G_BeginLevel` gulf@0x40f700, `G_StartLevel` gulf@0x40e170): load-time spawn,
counters reset, skid pool. Differences are data:

- **Start dialogues**: the dialogue table gulf@0x0049b378 (48 pointers, mission × 2 + end)
  has a start dialogue for operation **10** and operation **24** only (as2: 12 missions).
- The loading screen shows the same comic for every operation (frontend.delta.md 3.16).

### 10.3 End of level: changed (dialogues, modulo 24)

`EndLevel` gulf@0x40e6c0: same code (checkpoint, HUD hidden, end dialogue, pause). An **end
dialogue** exists for operation **10** only; every other operation goes straight to the
mission-complete screen. `G_MissionComplete` gulf@0x426560: unlocks mission (index + 1) mod
**24** and pushes the game-complete screen after operation 24 (index 23), else the
mission-complete screen; "Next" (`M_MissionCompleteAction` gulf@0x426040) moves to (index + 1)
mod 24. `G_NewGame`: same code (checkpoint rule, lives 2).

### 10.4 Scoring and rank: same

Same code; rank thresholds gulf@0x0049c488 and names gulf@0x0049aba4 have AirStrike 2's values.

### 10.5 Save file: changed

`G_LoadBin` gulf@0x406940, `G_SaveBin` gulf@0x406bd0: the AirStrike 2 scheme with a payload
of **0x5d8** bytes (as2: 0x574): a u32, 15 high scores × 40 bytes, **3** helicopters × 33,
**24** missions × 33 (1495 bytes), one more byte (GUESS: padding, never read), then the same
checkpoint block. Our engine keeps its own save format; this lists what is saved.

### 10.6 Unlocking: changed

Completing operation i unlocks operation (i + 1) mod 24 and helicopter `enableHelic` when it
is 0..2 (7.6).

## 11. HUD and menus

### 11.1 Coordinates: same

### 11.2 One-player HUD: same code

`HUD_Draw1P` gulf@0x407c80 and `HUD_Draw2P` gulf@0x408a60 are the same code as AirStrike 2's;
they read the weapon icon table gulf@0x0049c0c0 and the level caps above, whose values differ
(frontend.delta.md 4.4).

### 11.3 Tutorial hints: same

### 11.4 Menu tree: same

Same builders and actions; the look differs (frontend.delta.md).

## 12. Sound: not checked

The sound functions are all the same code (symbol-map.md); the music and sound files are data.

## 13. Rendering order and material state: not checked here

Renderer functions are the same code; see [render-pipeline.delta.md](render-pipeline.delta.md).

## 14. Cheats and debug features: same

`G_CheatInput` gulf@0x407070 is the same code with the same strings: the seven code words of
the AirStrike 2 delta (`invulnerability`, `igonnaliveforever`, `showmetheweapons`,
`moremoreweapons`, `glitteringprizes`, `deadlineisnear`, `diediediemydarling`) and their
effects; `showmetheweapons` raises slots 0..8, which in Gulf Thunder are the nine weapons of
8.2. Debug switches as AirStrike 2.

## 15. Constants and limits

All AirStrike 2 values except:

| Item | gulf | as2 | Evidence |
|---|---|---|---|
| Missions | **24** | 18 | gulf@0x426560, gulf@0x0049bd78 |
| Helicopters | **3** | 6 | gulf@0x0049bc70, gulf@0x4275e0, gulf@0x426560 |
| Loadout rows | **24** | 18 | gulf@0x412320 |
| Save payload | **0x5d8** | 0x574 | gulf@0x406940 |
| Missions with a start / end dialogue | **10, 24 / 10** | 12 / 12 missions | gulf@0x0049b378 |

## Values for GameRules

Every field of `GameRules` (`engine/include/as3d/game_profile.h`) for `gulf`. "Changes the
profile" marks a value that differs from `rulesGulf()` in `engine/src/game/game_profiles.cpp`
today.

| Field | gulf value | Evidence | Changes the profile |
|---|---|---|---|
| `missionCount` | 24 | gulf@0x0049bd78, modulo 0x18 in gulf@0x426560 | no |
| `attractCount` | 2 | `G_Init` gulf@0x40f8e0 (same code) | no |
| `helicopterCount` | 3 | gulf@0x0049bc70, gulf@0x4275e0 | no |
| `heliObjects` | `player_1, player_2, player_3` | gulf@0x0049bc70 | no |
| `difficultyCount`, `defaultDifficulty`, `difficulty[0..4]` | 5, 2, AirStrike 2's table | gulf@0x0049bd00, gulf@0x0049bce8 | no |
| `startLives` | 2 | `G_NewGame` gulf@0x40f7f0 (same code) | no |
| `bonusMissions` | 11, 18 | `levels.txt` (VERIFIED-DATA) | no |
| `bossMissions` | 10, 24 | `levels.txt` (VERIFIED-DATA) | no |
| `scrollSpeed`, `startMapPos`, `startCameraX`, `cameraMinX`, `cameraMaxX`, `cameraFollow`, `playerClampMargin` | 42, 32, 640, 578, 702, 48, 10 | camera functions same code | no |
| `cameraModeCount`, `defaultCameraMode`, `cameraModes` | 4, 1, AirStrike 2's presets | gulf@0x0049b9a0 | no |
| `scoreDigitObject`, `starItemObject`, `healthBarEmptyObject`, `healthBarFullObject` | `score_num`, `item_star`, `hbar_empty`, `hbar_full` | same code | no |
| `weaponSlots` | 9 | gulf@0x420200 | no |
| `missionLoadout` | the 24-row table of 8.2 | gulf@0x00489bc0 | **yes**: today `nullptr`, so level starts give slot 0 level 1 |
| `upgradesCarryToNextMission` | true | same code | no |
| `deathLosesWeaponLevel` | false | player scripts (no `G_SetUpgrade` on death, VERIFIED-DATA) | no |
| `powerUpCycleSkip` | {6, 7, 9} | same code | no |
| `accelInput`, `mouseAccel`, `mouseControlDefault` | true, 2.0, true | same code | no |
| `respawnChecksLives` | false | same code | no |
| `clampPlayerHealthToMax`, `healthBarScaleFromMax`, `lifeIconsMax` | true, true, 10 | HUD same code | no |
| `deadShootersBlocked`, `touchModeBits`, `killCapAtEnemyTotal`, `statsOnlyOnePlayer`, `campaignCheckpoint` | true | same code | no |
| `lightningEffectObject`, `lightningEffectInterval`, `lightningTimerCap` | `wavegun_hit`, 0.2, 5 | same code | no |
| `spawnAllOnIntermission`, `spawnDuringLoad` | true, true | same code | no |
| `waterFollowsWaves`, `skidTrailPool`, `skidNodeInterval`, `skidMaxNodes`, `skidLife`, `skidFadeStart`, `skidHeightOffset` | true, 64, 5/12, 23, 10, 5, 2 | same code | no |
| `terraMorph`, `civilians`, `skidMarks`, `waterFlags`, `coop` | true | same code; `TerraMorph` called by 8 scripts | no |

### Gulf Thunder rule values without a field

| Proposed field | gulf value | Section |
|---|---|---|
| `startDialogueMissions` | 10, 24 | 10.2 |
| `endDialogueMissions` | 10 | 10.3 |
| `hudWeaponLevelCaps` | 4, 5, 7, 6, 5, 5, 4, 5, 3 | 8.2 |
| `hudWeaponIcons` | table gulf@0x0049c0c0 | frontend.delta.md 4.4 |

## Checked sections

| Base section | Status for `gulf` |
|---|---|
| 1 Program structure | same (1.1, 1.2, 1.3 same code; no intro comic, frontend) |
| 2 Per-frame update order | same |
| 3 Entities (3.1 to 3.6) | same |
| 4 Movement and transforms (4.1 to 4.7) | same |
| 5 Collision (5.1 to 5.5) | same |
| 6 Damage and death (6.1 to 6.6) | same |
| 7.1 to 7.5 The player | same (helicopter data listed) |
| 7.6 Helicopter choice | changed: 3 helicopters, unlock bound 3 |
| 8.1 Weapon definitions and `Shoot` | same |
| 8.2 Weapon upgrades | changed: weapon ids (data), 24-row loadout table, HUD caps |
| 8.3 Missiles and power-ups | same (kinds VERIFIED-DATA) |
| 8.4 Targeting | same |
| 9 Scrolling and camera (9.1 to 9.7) | same |
| 10.1 Level list | changed: 24 missions, roles |
| 10.2 Level start | changed: start dialogues 10 and 24 |
| 10.3 End of level | changed: end dialogue 10, modulo 24 |
| 10.4 Scoring and rank | same |
| 10.5 Save file | changed: payload 0x5d8 |
| 10.6 Unlocking | changed: modulo 24, bound 3 |
| 11.1, 11.3, 11.4 | same |
| 11.2 One-player HUD | same code, other tables (frontend) |
| 12 Sound | not checked (same code) |
| 13 Rendering order | see render-pipeline.delta.md |
| 14 Cheats and debug features | same |
| 15 Constants and limits | changed (table above) |

## What an implementer must change

1. Give `rulesGulf()` the loadout table of 8.2 (`missionLoadout`, 24 rows).
2. Start and end dialogues per the table (10.2, 10.3), when the sequel front end exists.
3. Nothing else in the game rules.

## Open questions

1. Co-op was not played (as for AirStrike 2).

## Changelog

- 1.0 (F1): first version.
