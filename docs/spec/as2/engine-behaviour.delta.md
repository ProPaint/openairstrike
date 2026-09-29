# Native game logic: AirStrike 2 delta (`as2`)

Delta version 1.0 (package B4). Amends [../engine-behaviour.md](../engine-behaviour.md) 0.1,
together with the corrections that already apply to it for v1.70 (issues
[120](../issues/120-player-fire-at-screen-edge.md), [031](../issues/031-entity-pass-details.md),
[034](../issues/034-entity-geometry-guesses.md), [113](../issues/113-all-missions-findings.md) and
the corrections table of [../rcsl-builtins-semantics.md](../rcsl-builtins-semantics.md)), for the
game `as2` (AirStrike 2 v2.51, `AirStrike3D II.exe`). Section numbers mirror the base spec.
Companions: [symbol-map.md](symbol-map.md), [rcsl-vm.delta.md](rcsl-vm.delta.md),
[rcsl-builtins-table.delta.md](rcsl-builtins-table.delta.md); the behaviour of the builtins is
the subject of `rcsl-builtins-semantics.delta.md (pending)` and is not specified here. Symbols
named or corrected by this package: `re/symbols_as2_game.csv`.

Tags as in [../README.md](../README.md). `VERIFIED-CODE` cites both executables where a
comparison is made (`v170@0x…`, `as2@0x…`). `VERIFIED-DATA` holds for the `as2` data in
`assets_extracted_games/as2/` (1532 object definitions in 39 `objects/*.obj` files, 631 scripts,
`maps/levels.txt`). Offsets are byte offsets; "field k" is script field k (rcsl-vm.delta.md).

Method. Every function the base spec cites was paired with its `as2` counterpart
(`re/symbols_as2.csv`, then checked here), and the two disassemblies were compared with absolute
addresses masked; where the instruction shapes are identical apart from structure offsets the
section says `same` and gives the ratio or the evidence. Where they differ, the `as2` code was
read. Data claims come from scans of the object definitions and disassembled scripts. Constants
were read from the executable's data sections.

## Summary for implementers

The game loop is the v1.70 loop. What an AS2 game needs on top of the first game's rules, by
impact on play:

1. **Player movement** is driven by a native acceleration vector (unit length, from the four
   direction bits, or from mouse motion) that the player script reads with `GetPlayerAccel`;
   the script multiplies its displacement by the helicopter's `speed` (field 23, 0.7 to 1.4).
   Player health comes from the helicopter definition (300 to 800) and is **clamped to the
   maximum health every frame** by the HUD (7.3, 7.4, 11.2).
2. **Weapons**: 9 upgrade slots (not 20); each mission starts from a fixed loadout table
   (18 rows) on a new game or a restart, and upgrades carry over on "Next"; no level is lost on
   death (8.2).
3. **Campaign checkpoint**: `EndLevel` stores lives, total score and rank for the next mission;
   starting that mission again from the menu resumes with them ("Continue"); it is saved in
   `game.bin` unless a cheat was used (10.3, 10.5).
4. **Civilians** (class 5.0): destructible scenery and friendly vehicles (103 definitions).
   Touched only by `TOUCH_CIVILIAN` (bit 0x4), hit by splash, trace and particle damage; not
   enemies, no kill credit, not targetable, no health bar; score only if their definition has
   one (3.1.1).
5. **Touch modes are a bit set** (`TOUCH_ALL` = 0xF) and dead entities are never touched (5.2).
6. **Water**: `FL_ONWATER` now follows the animated water surface; `FL_ONWATER_NORMAL` also
   tilts the entity to it; `FL_ONWATER_FLAT` keeps the flat level (4.2).
7. **Skid marks**: ground vehicles lay fading tyre trails, 64 trails at most (3.1.2).
8. **Statistics**: the objects spawned while the level loads are not counted in the enemy total
   or the maximum score, and the kill counter is capped at the enemy total (6.1, 10.2, 10.4).
9. Dead shooters cannot fire (8.1); intermission levels spawn all their objects at once (3.4).
10. 18 missions, 6 helicopters in the order `player_1, player_2, player_4, player_6, player_5,
    player_3` (unlock order), 2 attract levels, new cheat words (1.3, 7.6, 14).

Everything else in the base spec holds for `as2` as listed in "Checked sections".

---

## 1. Program structure

### 1.1 Start-up: changed

`WinMain` as2@0x405ca0 (v170@0x4207f0), `Sys_Init` as2@0x405a80 (v170@0x420620). VERIFIED-CODE.

- **No `play.url` check.** The v1.70 step 2 is absent.
- Command line (at most 50 arguments, same splitter): `-setup` (as2@0x402ab0) and `-nosound`
  (as2@0x4225a0) as before; new: `-notex` (as2@0x405a80, a renderer switch), and three debug
  switches: `-god` sets god mode when a new game starts (as2@0x410e52), `-obj` writes an
  `objects.txt` listing when a level starts (as2@0x410da2, `G_WriteObjectsTxt` as2@0x4119f0),
  `-campos N` starts every level at `g_map_pos` = N × 40 instead of 32 (as2@0x415290).
- `Sys_Init` order: drive check (as2@0x405930, a CD check enabled by a flag; dropped, see the
  deviations table of the README), seed `rand` with the low 16 bits of `timeGetTime`, the
  sine table, `G_LoadBin`, truncate `game.log`, `F_Init`, `CFG_Init`, `MW_Init`, `S_Init`,
  Direct3D creation, particle instances cleared, `timeBeginPeriod(1)`.
- `G_Init(0)` as2@0x410eb0 (v170@0x408cc0): see 1.3.

### 1.2 Main loop and timing: same

VERIFIED-CODE as2@0x405ca0 against v170@0x4207f0: `dt_ms = min(timeGetTime() − previous, 100)`,
`frametime = dt_ms × 0.001` (as2@0x49f920), `time` and the level clock (as2@0x49f90c) advance by
it, then `MW_PumpMessages`, `G_Frame`, `Joy_Frame`, `S_Update` in the same order. Pause
(as2@0x543298) rolls `time` and the level clock back in `G_Frame` as in v1.70; `frametime`
keeps its value while paused.

One addition: a play-time counter (as2@0x49f918, milliseconds) is decremented each frame and
written to the registry every 60 s (`Sys_WriteRegistryKey` as2@0x405880, value 3600 − elapsed
seconds) when the byte flag as2@0x2219138 is set. No code of the export writes that flag, so the
counter never runs in this build (VERIFIED-CODE: the only reference is in `WinMain`). An
implementation ignores it.

### 1.3 Game states: changed

The flags, VERIFIED-CODE (addresses from the paired uses; names as in the base):

| Flag | v1.70 | as2 |
|---|---|---|
| game ready | 0x1fdb2c6 | 0x2219144 |
| intro running | 0x458c97 | 0x4c2023 |
| paused | 0x458eab | 0x543298 |
| game over | 0x4d52d4 | 0x543299 |
| intermission level | 0x4d52d5 | 0x54329a |
| HUD hidden | 0x4d52d6 | 0x54329b |
| two players | 0x1fdb2c7 | 0x2219145 |
| god mode | 0x1fdb2c4 | 0x221913a |
| cheat used | 0x1fdb2d0 | 0x2219146 |
| current mission index (0-based) | 0x1fdb2cc | 0x2219140 |
| checkpoint mission (new, 10.3) | – | 0x49ddf4 (−1 = none) |

State changes against the base list:

1. **Boot** (`G_Init` as2@0x410eb0): the attract level is `intro1` when `rand()` is even,
   `intro2` when odd (two attract levels; the strings `intro3`, `intro4` remain in the table
   as2@0x49df38 but are never chosen). Both players' helicopter index = 0 (`player_1`). Loads
   `levels.txt`, all objects, `score_num`, `wavegun_hit` (used by `Lightning`) and all weapons.
2. **Intro**: the logo pages and a new intro comic (frontend package; symbol-map.md "intro").
3. **Attract level with main menu**: as before; the menu tree is new (frontend package).
4. **Start**: the player-selection menu (`M_HeliSelectMenu` as2@0x429530) replaces the Start
   Game grid. Its start button reads " Continue " when the selected mission index is ≥ 1 and
   equals the checkpoint mission, otherwise " Start " (" Accept " when the menu was opened from
   the mission-complete screen). Starting (`M_HeliSelectAction` as2@0x428fb0, case 1) frees the
   level, pops the menus, loads the selected mission and calls `G_NewGame` (as2@0x410dc0, the
   counterpart of v1.70 "new campaign" v170@0x408c40), described in 10.3.
5. **Playing**, 6. **Paused** (P or Pause, `G_SetPause` as2@0x410cb0, same code), 7. **In-game
   menu** (Esc, as2@0x4110f0): same as v1.70.
8. **Game over**: trigger unchanged (`G_PlayerFrame` as2@0x413c50: single player when
   p_lives < 0; two players when both are < 0). Effect `G_GameOver` as2@0x410e70 (v170@0x408c80,
   same effect: game over, HUD hidden, paused, music jump, game-over menu). New: the same function
   is the script builtin `GameOver` (no `as2` script calls it; the cheat `diediediemydarling`
   does). Menu (`M_GameOverAction` as2@0x428ac0): **Restart** frees the level, re-applies the
   mission's weapon loadout (8.2) and restarts the mission with the lives it began with;
   **Quit** banks the score, loads the attract level, shows the main menu and runs the
   high-score check.
9. **Mission complete**: changed, see 10.3.
10. **High scores** (`G_CheckHighScore` as2@0x414610, identical instruction shape to
    v170@0x40bde0): same.
11. **Exit**: saves `game.bin` (10.5).

`G_Frame` dispatch (as2@0x410fd0): same as v1.70 (nothing before game-ready, the intro frame
while the intro runs, otherwise §2).

---

## 2. Per-frame update order: changed

VERIFIED-CODE `G_Frame` as2@0x410fd0 against v170@0x408df0. The order of passes is the v1.70
order with one pass inserted and the statistics overlay split out of the renderer:

1. Pause time correction (same).
2. `R_BeginFrame` as2@0x430e50: clears the per-frame render lists (among them the new
   skid-trail draw list, count as2@0x2113074) and the buffers. Renderer package.
3. `V_UpdateCamera` as2@0x414e90: same (9.2, 9.3).
4. `G_ActivateMapObjects` as2@0x40e7e0: changed on intermission levels (3.4).
5. `G_PlayerFrame` as2@0x413c50, skipped on intermission levels and after game over (same
   condition, as2@0x41102e..0x41103e): changed (7.2).
6. `G_RunEntities` as2@0x40cdc0: the free pass, the entity pass and the collision pass. The
   collision pass (v1.70 `G_RunCollisions` v170@0x4055d0) is no longer called: the entity pass
   ends with a tail jump (as2@0x40cee7) into its body at as2@0x40c560..0x40c747, which the
   export attributes to the same function. Its content and position in the frame are unchanged
   (touch dispatch for every root in list order, then the two-player push-apart, §5).
   VERIFIED-CODE: same loops, same 2000 constant, same calls.
7. **New: `G_UpdateSkidTrails`** as2@0x414850 (called at as2@0x41104a), every frame, **also
   while paused** (3.1.2). The symbol map's name `G_UpdatePlayers` is wrong (see "Corrections").
8. Particles: `PS_Update` as2@0x419cd0 for every emitter, unless paused (same).
9. `HUD_Frame` as2@0x40ac80 (same condition: not intermission, not HUD hidden); it no longer
   draws the v1.70 mouse-control cursor (7.2).
10. `R_DrawScreenFade` as2@0x40b1c0 (v1.70 `SCR_DrawFade`).
11. **New position:** `R_DrawStats` as2@0x40aa40 (the debug counters, formerly inside
    `R_RenderView`).
12. `R_RenderView` as2@0x430a20, 13. `UI_Frame` as2@0x42b3c0, 14. `R_EndFrame` as2@0x431110.

**Level start runs one frame of the world.** `G_StartLevel` as2@0x40e1e0 ends, after spawning
the players, with `G_ActivateMapObjects` (as2@0x40e577), `G_RunEntities` (as2@0x40e57c) and one
`R_RenderView` (as2@0x40e586); v1.70 (v170@0x407080) only rendered. With the camera just reset,
the activation front edge is `g_map_pos + 800` (9.7), so every placement with row ≤
(32 + 800) / 40, i.e. rows 0 to 20, is spawned during loading, runs its `init` and one think
(with `main`, collision and touch). Consequences for the statistics: 10.2.

Determinism notes of the base: same (scripts before collisions; collision on the previous
frame's projection; `create` thinks once inside the builtin; the C runtime `rand`, seeded from
`timeGetTime`).

---

## Changelog

- 1.0 (B4): first version.
