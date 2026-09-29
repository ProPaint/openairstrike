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

## 3. Entities

### 3.1 Object definitions: changed

`ParseObject` as2@0x4125a0 (v170@0x40a160), `G_InitObject` as2@0x411e90 (v170@0x409ba0).
VERIFIED-CODE unless marked.

Same: at most 2048 definitions (as2@0x4125d0 tests 0x800), 1-based, first definition of a name
wins (`G_FindObject` as2@0x411ba0), at most 64 `attach` lines per definition (as2@0x41311f),
precaching on first use, the `type`, `sort`, `blend`, `envmode`, `rflag`, `shadow` values and
the rule that a non-model type sets flag 0x1000. The definition record is 0x3604 bytes
(v1.70 0x32d9); an `attach` record is 200 bytes (v1.70 197).

The keyword set differs from v1.70 by exactly six words (a diff of the keyword strings of both
parsers): `civilian`, `speed`, `skid_mark`, `FL_ONWATER_NORMAL`, `FL_ONWATER_FLAT`,
`TOUCH_CIVILIAN`. Changed or new values:

| Key | as2 value | v1.70 | Evidence |
|---|---|---|---|
| `player` / `enemy` / `item` | class 1 / 2 / 3 | same | parser, class stored at definition +0x35E4 |
| `civilian` | **class 5** | – | as2@0x412e11 |
| `speed <float>` | stored per definition, copied into **field 23** at spawn | – | parser; `G_InitObject` as2@0x411f51..0x411f57 |
| `flag FL_ONWATER` | 0x4 | 0x4 | |
| `flag FL_ONWATER_NORMAL` | **0xC** (0x4 + 0x8) | – | parser |
| `flag FL_ONWATER_FLAT` | **0x204** (0x4 + 0x200) | – | parser |
| `touch TOUCH_ENEMIES` | ORs **0x1** | sets 1 | parser |
| `touch TOUCH_PLAYER` | ORs **0x2** | sets 2 | parser |
| `touch TOUCH_CIVILIAN` | ORs **0x4** | – | parser |
| `touch TOUCH_ALL` | sets **0xF** | sets 3 | parser |
| `skid_mark <x> <y> <w> "<texture>"` | appended to a per-definition list (3.1.2) | – | parser, records of 0x4C bytes |

`touch` values are now ORed, so several `touch` lines combine (VERIFIED-DATA: the shipped
combinations are 1, 2, 4, 5 and 6; `TOUCH_ALL` is not used). Class values seen in the data:
0 (1185 definitions), 1 (the 6 helicopters), 2 (238), 5 (103); `item` is still unused.

The class value (field 2) as the native code tests it:

| Class | Meaning | Native tests |
|---|---|---|
| 1.0 | player | – (players are found through the player records) |
| 2.0 | enemy | touch bit 0x1, particle damage, `RadialDamage`, `TraceLine(Damage)`, `Lightning`, `LockTarget`, kill counter, enemy total, health bar |
| 4.0 | projectile made by `Shoot` | – |
| 5.0 | civilian (new) | touch bit 0x4, particle damage, `RadialDamage`, `TraceLine(Damage)` |

#### 3.1.1 Civilians (class 5.0, `TOUCH_CIVILIAN`)

VERIFIED-CODE: the only native comparisons with 5.0 are in the touch pass (as2@0x40c314..0x40c346), the
particle damage (as2@0x40bbc0), `RadialDamage` (as2@0x4206b0), `TraceLine` (as2@0x420fd0) and
`TraceLineDamage` (as2@0x421180). Consequences:

- **Collision**: a civilian is a touch candidate only for touchers whose mode has bit 0x4
  (5.2). Player bullets carry `TOUCH_ENEMIES` only (VERIFIED-DATA), so **gunfire passes over
  civilians**; the definitions that touch civilians are `abomb_proj` (mode 5), `expl_wave_big`
  (4) and the two falling meteorites (6).
- **Damage**: civilians are damaged by what touches them, by splash (`RadialDamage`), by traces
  and by particle systems whose `touch` includes bit 0x4. The exact rules of those builtins are
  in `rcsl-builtins-semantics.delta.md (pending)`.
- **Score**: `G_Damage` awards the victim's field 36 to a player attacker whatever the class
  (6.1), so a civilian scores only if its definition has `score`: 5 of the 103 do (100 or 200,
  scaled by the difficulty like every placed object). VERIFIED-DATA.
- **Not an enemy**: not counted in the enemy total (spawner as2@0x40ea8b tests class 2.0), no kill
  credit (as2@0x40bb1b tests class 2.0), not a `LockTarget` candidate (as2@0x414c89), not hit by
  `Lightning` (as2@0x421399), no health bar (`G_ThinkEntity` as2@0x40cce7 tests 2.0).
- **No penalty.** No native code counts civilian losses; no script writes `p_scores` except the
  two score pick-ups, and `p_stars` only `star.scr` (VERIFIED-DATA). The mission statistics
  ignore civilians.
- What they are (VERIFIED-DATA): 60 buildings, 22 trees, 7 map objects, 10 civilian vehicles,
  3 planes and `player_bomber`, the friendly bomber of the "Air Support" power-up. 8 of them
  lay skid marks.

#### 3.1.2 Skid marks (`skid_mark`)

VERIFIED-CODE: parser as2@0x413268..0x4132fb, trail allocation `G_InitObject`
as2@0x412200..0x4122f0, update `G_UpdateSkidTrails` as2@0x414850, release `G_FreeEntity`
as2@0x40b300 and `G_FreeAllSkidTrails` as2@0x414800 (from `G_FreeLevel` as2@0x40e660), drawing
as2@0x430280 / as2@0x4300e0. None of this exists in v1.70.

**Definition.** Each `skid_mark x y w "texture"` line adds a record {x, y, w, texture name
(64 bytes)}. The record area of a definition holds 8 records; the parser does not check the
count (a ninth line would overwrite the class and flags of the definition). The data uses 2 per
definition (92 definitions) or 4 (1). x is the offset along the model's lateral axis (axis row
0), y along its forward axis (row 1), w the width of the mark; the data pairs +x and −x for the
left and right wheels (for example `jeeps.obj`: `17 21 16` and `-17 21 16`). VERIFIED-DATA.

**Who lays marks.** Every entity built from a definition with skid marks, when it is created
(map spawn, `create`, drops, definition children alike): `G_InitObject` sets field 88 (entity
+0x1E4) to the number of records and field 89 (+0x1E8) to an array of that many pointers, and
takes one **trail** per record from a pool of **64 trails** (0x64C bytes each, pool
as2@0x20c5da8, free list as2@0x20c5d9c, live list as2@0x20c5da0, rebuilt at every level start
in `G_StartLevel` as2@0x40e474..0x40e4b7). A trail is zeroed, linked at the head of the live
list, and remembers its owner entity, the texture, x, y and w. When the pool is empty the
pointer is 0 and that mark is simply not laid. VERIFIED-DATA: 80 enemy, 8 civilian and 5
class-0 definitions have skid marks (jeeps, BTRs, M113, trucks, rocket launchers, a buggy, the
intro Chinook).

**Update, every frame, including paused frames** (`G_UpdateSkidTrails`, after the entity
pass). For each live trail, in list order:

1. If it has no owner (the owner was freed: `G_FreeEntity` clears the owner of each of its
   trails) and no node left, the trail returns to the pool.
2. Otherwise: its node timer += frametime and every node's age += frametime. Nodes whose age
   is ≥ 10 s are removed from the front (oldest first).
3. The trail is appended to the frame's draw list (at most 64 per frame, as2@0x2113074; further
   trails are not drawn that frame).
4. With an owner, let F and L be the owner's axis rows 0 and 1 (entity +0x134, +0x140)
   reduced to their x, y components and normalised in 2D (left as is when of length 0), and
   B its base origin (fields 41..43):
   - when the node timer ≥ 5/12 s (0.4166667, as2@0x48f768): the timer −= 5/12 s and a new
     node is started (the node count grows; at 23 nodes the oldest is dropped instead,
     as2@0x414a93);
   - when there is no node yet, node 0 is started; otherwise the last node is the current one;
   - the current node gets B.xy; if the trail has nodes, its length += |B − previous B| (3D)
     and the previous B is stored;
   - the current node's two points: B + y·L + (x ∓ w/2)·F, each with z = terrain height
     at that point + 2 (`R_TerrainHeight` as2@0x41a1e0, constant as2@0x48f2d0), and the
     texture coordinate along the trail = the trail length at that moment.

So the last node follows the vehicle every frame, a node is fixed every 5/12 s whatever the
speed (a standing vehicle piles nodes on one spot), a trail holds at most 23 nodes (about 9.6 s)
and nodes vanish at 10 s. Marks lie on the terrain, never on water, and are not affected by
the scale field or by the owner's altitude.

**Drawing** (as2@0x4300e0, renderer package for the states): a strip through the nodes' point
pairs; u = 0 on the −w/2 side and 1 on the other; v = trail length / w (the texture repeats
every w units); colour white with alpha = 1 for ages up to 5 s, then 1 − (age − 5) / 5, 0 at
10 s; depth writes off; nothing drawn when 2 × nodes ≥ 4096.

Quirks of the original an implementation may drop: a newly started node keeps whatever age its
slot held (0 in a fresh trail); ages keep growing while the game is paused; when a trail is full
the committed node is written one slot past the visible ones. See
[issue 210](issues/210-skid-trail-details.md).

#### 3.1.3 `speed` on player helicopters

The parser stores the float; `G_InitObject` copies it into field 23 (`wp_speed`) of every entity
of that definition (as2@0x411f51..0x411f57); v1.70 left field 23 at 0 (v170@0x409c4f..0x409c6a).
Only the six helicopters carry `speed` (VERIFIED-DATA: `player_1` 1.0, `player_2` 1.25,
`player_3` 1.4, `player_4` 0.85, `player_5` 0.7, `player_6` 1.2), so for every other object
field 23 starts at 0 as before. No native code reads field 23 except the waypoint builtins.
What it scales is decided by the player scripts: 7.3.

#### 3.1.4 `FL_ONWATER_NORMAL` and `FL_ONWATER_FLAT`

Bit values in the table above; behaviour in 4.2. VERIFIED-DATA: `FL_ONWATER` alone on 25
definitions, `FL_ONWATER_NORMAL` on 10 (destroyers, cutters, the small boat, the submarine
`apl`), `FL_ONWATER_FLAT` on 4 (`avianos`, the two rocket boats, `ship_big`).

### 3.2 Entity memory layout: changed

An entity is **0x1F4** bytes (v1.70 0x1E3). The `as2` structure is the v1.70 structure with
natural 4-byte alignment and one new dword before the reference, and two new dwords before the
children list. VERIFIED-CODE: `G_InitObject` as2@0x411e90 against v170@0x409ba0 (every store
of the builder pairs up), `G_ThinkEntity` as2@0x40cb30, `G_FreeEntity` as2@0x40b300, and the
aligned-use table of rcsl-vm.delta.md.

Native part (before the reference):

| as2 | v1.70 | Type | Meaning |
|---|---|---|---|
| +0x00 / +0x04 | +0x00 / +0x04 | ptr | next / previous (+0x04 also the free-list link) |
| +0x08 | +0x08 | ptr | parent |
| +0x0C | +0x0C | ptr | parent tag name |
| +0x10 | +0x10 | u8 | `abs` flag |
| +0x11 | +0x11 | u8 | counted in the root's reference count |
| +0x12..0x13 | – | | padding |
| +0x14 | +0x12 | i32 | attachment reference count of a root |
| +0x18 | +0x16 | ptr | name (definition name, or attach `id`) |
| +0x1C | +0x1A | i32 | activation state 0 / 1 / 2 |
| +0x20 | +0x1E | u32 | runtime flags (same bits as v1.70) |
| +0x24 | +0x22 | i32 | drop definition |
| +0x28..+0x34 | +0x26..+0x32 | | light parameters (4 dwords) |
| +0x38 | +0x36 | u8 | spot-light flag |
| +0x39..0x3B | – | | padding |
| +0x3C..+0x48 | +0x37..+0x43 | | light parameters (4 dwords) |
| +0x4C | +0x47 | i32 | shadow type |
| +0x50 / +0x54 / +0x58 | +0x4B / +0x4F / +0x53 | | waypoint path / distance / last node |
| +0x5C | +0x57 | ptr | script thread |
| +0x60 | +0x5B | u32 | touch mode (now a bit set, 5.2) |
| +0x64..+0x6C | +0x5F..+0x67 | 3 floats | `bbox_scale` |
| +0x70 | +0x6B | float | maximum health |
| +0x74 | +0x6F | float | seconds since the last damage (2.0 after `init`) |
| **+0x78** | – | float | **new**: seconds since `Lightning` last spawned its hit effect on this entity; grows by frametime while < 5.0 (as2@0x40cb82..0x40cb9b); `Lightning` spawns `wavegun_hit` on a target only when it is ≥ 0.2 and then resets it to 0 (as2@0x42144e..0x42146b). Semantics of `Lightning`: builtins delta (pending) |
| +0x7C | +0x73 | i32 | looping sound channel |
| +0x80 | +0x77 | i32 | player index 0 / 1 |
| +0x84 | +0x7B | | reference; script fields 0..87 at +0x84 + 4k, same meaning as v1.70 |

After the reference every v1.70 offset is 9 larger (the base table's +0x153 model is +0x15C,
+0x1BB emitter is +0x1C4, +0x1BF radius is +0x1C8, the collision rectangle +0x1C3..+0x1D7 is
+0x1CC..+0x1E0), then:

| as2 | Field | Meaning |
|---|---|---|
| +0x1E4 | 88 | **new**: number of skid trails of this entity (from the definition, 3.1.2) |
| +0x1E8 | 89 | **new**: array of that many trail pointers (0 for a mark the pool could not supply) |
| +0x1EC | 90 | child count (v1.70 +0x1DB) |
| +0x1F0 | 91 | child array (v1.70 +0x1DF) |

Fields 88 and 89 are engine-internal; no script uses an index above 37 (rcsl-vm.delta.md).

Runtime flag bits at +0x20: the same bits with the same meaning (0x01 removed, 0x02 thought,
0x04 `main` active, 0x08 collidable, 0x10 health frozen, 0x20 `AttachEntity`, 0x100 locked).
VERIFIED-CODE for 0x01/0x02/0x04/0x08/0x20 in `G_ThinkEntity`, `G_RemoveEntity`, `G_RunEntities`,
`AttachEntity` as2@0x41fa70; 0x10 in `G_Damage` as2@0x40b9e0; 0x100 in `LockTarget`'s helper
as2@0x414c50 (identical instruction shape to v170@0x40c020).

`G_SyncRenderRecord` (v1.70 `G_SetupTransform`) now clamps the render colour copied from fields
28..31 to [0, 1] (as2@0x40c899..0x40c986); v1.70 copied it unclamped (v170@0x4058ac..0x4058b2). Renderer
detail.

### 3.3 Pool and lists: same

1024 entities (pool as2@0x4c2278, `memset` of 0x7D000 bytes and the free-list build in
`G_StartLevel` as2@0x40e44f..0x40e472), free list as2@0x4c2078, live list sentinel as2@0x4c2080,
newest pointer as2@0x4c2084, live count as2@0x221913c. Allocation `G_AllocEntity` as2@0x40b4b0
(identical shape to v170@0x404530, still no exhaustion check). Iteration newest to oldest.
Children outside the pool (heap, 0x1F4 bytes). Removal `G_RemoveEntity` as2@0x40b280: same
effect (bit 0x01 on the entity and its children, emitter stopped, root count decremented); the
shadow display list is no longer deleted here but in `G_FreeEntity`. Free at the start of the
next entity pass (the unlink is now inlined into `G_RunEntities`, before the call at as2@0x40cdf7;
`G_FreeEntity` as2@0x40b300 frees the thread, detaches the entity's skid trails, frees the
path, stops the emitter, frees the shadow list and the decal and recursively frees the children;
v1.70's separate tree walk v170@0x4044b0 is folded into it). Level end: `G_FreeAllEntities`
as2@0x40b510 frees every live entity the same way.

### 3.4 Creating entities: changed

**From the map (`G_ActivateMapObjects` as2@0x40e7e0, v170@0x407590).** Same cursor, same
window test and the same twelve steps (position from row and column, height from terrain or the
flat water level, script override, player index at random between the living players with the
same `rand` rule, projected shadow, yaw 30° steps or the path, `init`, drop, state 1 or 0,
health × g_health_factor, score rounding with the score factor, enemy total for class 2.0
without `FL_NONTARGET`, maximum level score). VERIFIED-CODE, same steps in the same order. The
in-memory map record is 0x20 bytes (v1.70 0x1F). Changes:

- **Intermission levels** (as2@0x40e815): the window test is skipped, so every placement of an
  attract level is spawned in the first call, whatever its row. v1.70 applied the window to
  attract levels too.
- **First call during loading**: `G_StartLevel` calls the spawner once before the first frame
  (§2), with the front edge at `g_map_pos + 800`; the objects of rows 0..20 therefore exist
  (and have run `init` and one think) when play starts. Their contribution to the enemy total
  and the maximum score is erased by the counter reset that follows `G_StartLevel` (10.2).
- The spawned entity's player index: same rule (as2@0x40e8fc..0x40e969).

**From scripts (`create` as2@0x41f7d0).** Same steps; it now returns the new entity even when
its `init` writes the return register (rcsl-vm.delta.md). Health and score of created entities
are still not scaled by the difficulty.

**Projectiles (`Shoot` as2@0x420190)**: 8.1.

**Drops**: `G_Damage` spawns the drop with `G_SpawnObject` as2@0x4124c0 (same code as
v170@0x40a080 apart from offsets).

### 3.5 Activation states: same

`G_RunEntities` as2@0x40cdc0, `G_InActivationArea` as2@0x40c000 and `G_InVisibleTerrainBox`
as2@0x40c090 (both identical in shape to v170@0x4050b0 and v170@0x405140, ratio 1.000),
`G_SetStateRecursive` as2@0x40cd70 (identical to v170@0x405c70): the three states, the band
y − r ∈ [g_map_pos + 16, g_map_pos + 800], x ± r within 0..1280, z + r ≥ `hmin`, and the
leaving test (sphere in the frustum and in the visible terrain box) are unchanged. `activate`
and `deactivate` are the same code. The issue 031 and 113 §4 questions carry over unchanged.

### 3.6 Event entry points: same

See rcsl-vm.delta.md "Event dispatch": `init`, `main`, `damage`, `callback` same; `touch` with
the new pair tests of 5.2.

---

## 4. Movement and transforms

### 4.1 Think order: changed (one timer)

`G_ThinkEntity` as2@0x40cb30 against v170@0x405a60, VERIFIED-CODE: the same steps in the same
order (removed entities skipped, bit 0x02, unless paused: age += frametime, time since damage
+= frametime, **new: the `Lightning` timer at +0x78 += frametime while below 5.0**
(as2@0x40cb82..0x40cb9b), resume `main` under the same conditions; parent's bit 0x08, tag
attachment, transform, screen bounds for roots and `AttachEntity` children, emitter holders
stop after moving their emitter, render queue and definition light unless `FL_NODRAW`, health
bar, children). The symbol-map match (medium) is confirmed.

### 4.2 Position and velocity: changed (water)

No native integration of velocity, gravity or friction, as in v1.70.

Ground and water following for roots, `G_SyncRenderRecord` as2@0x40c750 (v1.70
`G_SetupTransform` v170@0x4057c0), VERIFIED-CODE, in this order:

1. flag bit 0x1 (`FL_ONGROUND`): z = `R_TerrainHeight(x, y)` (as2@0x40c76f..0x40c78a; same
   function as v1.70, identical shape);
2. flag bit 0x4 (`FL_ONWATER`, also part of `_NORMAL` and `_FLAT`): if bit 0x200 is set
   (`FL_ONWATER_FLAT`), z = the level's flat water level (level record +0x1C0, the
   `l_waterlevel` value); otherwise **z = `G_WaterHeight(x, y)`** (as2@0x41d530), the height of
   the animated water surface (as2@0x40c798..0x40c7c8). v1.70 always used the flat level
   (v170@0x4057c0);
3. axis: bit 0x2 (`FL_ONGROUND_NORMAL`): from the terrain (`G_AlignToTerrain` as2@0x41a360,
   identical shape to v170@0x4165f0); else **bit 0x8 (`FL_ONWATER_NORMAL`): from the water
   surface** (`G_AlignToWater` as2@0x41d6d0, as2@0x40c80b..0x40c836); else from the angles.

`G_WaterHeight(x, y)` as the game rules see it (VERIFIED-CODE as2@0x41d530): when the level
has no water surface (no `water` line; flag as2@0x2103ca4 set from level record +0x140 by the
terrain loader as2@0x41be84), it returns the **terrain** height; otherwise, for a point inside
the water grid, the bilinear interpolation of the z of the four surrounding water-grid
vertices (the grid the water renderer animates, `R_WaterWaveVertices` as2@0x41d8d0), and 0
outside the grid (x < 0, y < 0, or beyond the grid's cell count × 40). The wave animation that
moves the vertices belongs to the renderer and to the `WaterHeight` builtin (pending).

`G_AlignToWater` builds the axis exactly as `G_AlignToTerrain` does (same instruction shape,
ratio 1.000 against v170@0x4165f0), with water heights: samples at (x − 10, y + 15),
(x + 10, y + 15) and (x, y − 25), up = the normalised plane normal, row 1 = the yaw direction
(cos yaw, sin yaw, 0) made orthogonal to up, row 0 = row 1 × up, each normalised (issue 034 §2
describes the same construction for the terrain).

Placement at spawn (`G_SpawnObject` as2@0x4124c0, same code as v170@0x40a080) still puts
`FL_ONWATER` objects at the flat level; the first think then moves them onto the waves.

So: `FL_ONWATER` floats up and down with the waves but keeps its own angles;
`FL_ONWATER_NORMAL` also pitches and rolls with the surface; `FL_ONWATER_FLAT` sits at the
still level (the big ships and the aircraft carrier).

### 4.3 Axis from angles: same

`AnglesToAxis` is the same code; the correction of rcsl-builtins-semantics.md (field 14 is
pitch, field 15 roll) applies unchanged.

### 4.4 Attachment to tags: changed (non-model parents)

`G_AttachToTag` as2@0x40b730 against v170@0x404850 (ratio 0.822), VERIFIED-CODE. Same for a
model parent: the tag is looked up, a missing tag detaches the child and **decrements the root's
reference count** when the child was counted (as2@0x40b75e..0x40b778; v170@0x404879..0x404891
does the same, see "Corrections"), `abs` and non-`abs` placement as in v1.70. New: when the
parent is not a model (render type ≠ 0) no tag is looked up and the tag position used is the
parent's own base origin (fields 41..43); the child then lands at parent base + parent axis ·
(parent base [+ offset]), which looks like an oversight. No shipped definition attaches
anything to a non-model (VERIFIED-DATA); only a script `AttachEntity` to a sprite would reach
it. An implementation may treat the tag position as 0 there (GUESS about the intent).

The definition-children build (`attach` with `abs`, `id`, `night`, particle-system children):
same rules (`G_InitObject` as2@0x412357..0x412429 against v170@0x409ba0): a `night` child is
built only on night levels (level record +0x1C8); `abs` sets +0x10; `id` names the child; the
emitter holder allocation is inlined (v170 `G_AllocEmitterEntity` v170@0x4099a0).

### 4.5 Waypoint paths: same

`G_EvalPath` as2@0x40cf20, `G_MoveAlongPath` as2@0x40d130, `G_RotateToPath` as2@0x40d260 and
`G_BuildPath` as2@0x40db10 have the instruction shapes of v170@0x405dd0, 0x405fe0, 0x406110 and
0x4069a0 (ratio 1.000; only structure offsets differ). The only difference for paths is the
initial field 23 (3.1.3), which no path-following object sets in its definition.

### 4.6 World bounds: same

1280 units wide, no clamp; leaving handled by the activation states; the player bounded by the
camera (7.3).

---

## 5. Collision

### 5.1 Shapes: same, with issue 120

`G_ComputeScreenBounds` as2@0x40ca00 against v170@0x405930, VERIFIED-CODE: the class-0 and
touch-0 exemption, the sphere test (`R_CullSphere` as2@0x41d310, identical shape), the model
box through `R_ProjectEntityBounds` as2@0x41cfd0, the point path through `R_WorldToScreen` and
`R_PointOnScreen` as2@0x41d270, the previous-point rule. **Issue 120 holds for `as2`**: the
call at as2@0x40ca80..0x40ca84 passes the maximum (entity +0x1D8) in ECX and the minimum
(+0x1CC) in EDX, exactly as v170@0x4059b3..0x4059b7, and `R_RectOnScreen` as2@0x41d2c0 tests
max.x ≥ 0, min.x < width, max.y ≥ 0, min.y < height: an **overlap** test. The point test
as2@0x41d270 is 0 ≤ p < size. Width and height are the video-mode size (as2@0x49f878,
0x49f87c); our 800×600 deviation applies.

### 5.2 Which pairs are tested: changed

`G_TouchEntity` as2@0x40c120 against v170@0x4051d0 (the symbol-map match is confirmed; the
changes are those of rcsl-vm.delta.md, restated here for the game rules). VERIFIED-CODE. The
entity is tested when its touch mode ≠ 0 and it has bit 0x08 (as before):

- **Players** (mode bit 0x2, as2@0x40c140): each player with health > 0, in player order, same
  model/point tests; first hit sets `other`, the toucher's player index, runs `touch` and ends
  the entity's pass.
- **Other entities** (mode ≠ 2, as2@0x40c1fa): candidates are list entities other than the
  toucher that are (mode bit 0x1 and class 2.0) **or (mode bit 0x4 and class 5.0)**, **not dead
  (field 4 = 0, new)**, with bit 0x08, not removed and not health-frozen. Same pair tests
  (model/model overlap, point/model swept segment `G_SegmentHitsRect` as2@0x417650 with the
  identical shape of v170@0x40ca10, model/point always, point/point never); the scan continues
  after a hit unless the toucher was removed.

| Mode (shipped values) | Players | Enemies (2.0) | Civilians (5.0) |
|---|---|---|---|
| 1 `TOUCH_ENEMIES` | – | yes | – |
| 2 `TOUCH_PLAYER` | yes | – | – |
| 4 `TOUCH_CIVILIAN` | – | – | yes |
| 5 | – | yes | yes |
| 6 | yes (first) | – | yes, unless a player was touched |
| 0xF `TOUCH_ALL` (unused) | yes (first) | yes | yes |

VERIFIED-DATA pairs: player bullets (32 definitions, mode 1), enemy fire and 54 ramming enemies
(mode 2), `abomb_proj` (5), `expl_wave_big` (4), the falling meteorites (6); helicopters have no
touch mode. v1.70's "items are ordinary TOUCH_PLAYER objects" holds.

The new dead-entity filter matters: a wreck whose script has not yet removed it (health ≤ 0,
field 4 = 1) no longer absorbs bullets. Players are still selected by health > 0 only.

### 5.3 What a hit triggers: same

Only the `touch` handler; damage is the script's `Damage` call.

### 5.4 Two-player push-apart: same

as2@0x40c5a8..0x40c73f (reached from `G_RunEntities`, §2): both players alive, rectangles
overlap, velocities ± normalize(p1.xy − p2.xy) × 2000 × frametime. VERIFIED-CODE, same constants
and order as v170@0x4055d0.

### 5.5 Traces: changed (civilians)

`TraceLine` as2@0x420fd0 and `TraceLineDamage` as2@0x421180 now also accept class 5.0 targets
(the 5.0 comparisons at as2@0x420fd0.. and as2@0x421180..). Their exact rules are in the
builtins semantics delta (pending).

---

## 6. Damage and death

### 6.1 `G_Damage`: changed (kill counter cap)

`G_Damage` as2@0x40b9e0 against v170@0x404ae0 (ratio 0.927; the symbol-map match is
confirmed), VERIFIED-CODE. Same skip conditions (dead; frozen bit 0x10; the entity is a player's
entity and that player's freeze counter, record +0xB8, ≠ 0; god mode for players; the player
loop runs over the number of players), same order (time since damage = 0, health −= amount,
`damage` handler, then at health ≤ 0: kill accounting, field 4 = 1, drop spawned and cleared,
score to a player attacker ≥ 0). One change:

- **Kill counter cap**: for a class-2.0 victim without `FL_NONTARGET`, the attacker's kill
  counter (player record +0x148) is increased only while it is below the level's enemy total
  (as2@0x40bb46..0x40bb5d: compare with as2@0x5432c0). v1.70 increased it unconditionally
  (v170@0x404c44). With attacker = −1 the counter index is −1 as in v1.70 (reads and writes 4
  bytes before player 1's record; harmless, ignore it).

### 6.2 Damage sources: changed (targets)

Attacker rule: same (`Damage` as2@0x420630 has the identical shape of v170@0x41b550).
`g_damage_factor` handling: same (`Damage` multiplies; `Shoot` multiplies enemy projectiles'
field 35).

| Source | as2 | Change against v1.70 (details: builtins semantics delta, pending) |
|---|---|---|
| `Damage` | as2@0x420630 | none |
| `RadialDamage` | as2@0x4206b0 | also hits class 5.0 (civilians) |
| `RadialDamagePlayer` (new) | as2@0x420840 | the `RadialDamage` shape over the player records instead of the entity list (rcsl-builtins-table.delta.md); used by 7 scripts (explosions, meteorites, the big rocket launcher) |
| `TraceLine`, `TraceLineDamage` | as2@0x420fd0, 0x421180 | class 5.0 accepted as well as 2.0 |
| `Lightning` | as2@0x421310 | takes the range as its argument (t0); spawns `wavegun_hit` at most every 0.2 s per target (+0x78) |
| Particle damage | `G_ParticleDamage` as2@0x40bbc0 | the particle system's touch mode is read as a bit set: bit 0x2 → every player whose rectangle contains the particle; otherwise the first list entity that is alive (health > 0), on screen, and (bit 0x1 and class 2.0) or (bit 0x4 and class 5.0), containing it. Attacker −1 |
| `Shoot` | as2@0x420190 | the shooter must also be alive (8.1) |

### 6.3 Difficulty: same

Table as2@0x49dee8 (VERIFIED-CODE, read from the executable), applied at each level start by
`G_BeginLevel` as2@0x410cd0 (as2@0x410cf5..0x410d2f), index as2@0x49ded4 clamped to 4, default
2; factors as2@0x49ded8 (damage), 0x49dedc (health), 0x49dee0 (score), 0x49dee4 (rank), all 1.0
before the first level:

| Index | g_health_factor | g_damage_factor | score factor | rank factor |
|---|---|---|---|---|
| 0 | 0.3 | 0.5 | 0.6 | 0.7 |
| 1 | 0.5 | 0.7 | 0.8 | 0.85 |
| 2 | 0.75 | 0.8 | 1.0 | 1.07 |
| 3 | 1.5 | 1.25 | 1.2 | 1.2 |
| 4 | 2.0 | 1.4 | 1.4 | 1.3 |

Identical to v170@0x4577d0, value by value. What each factor scales is unchanged: health
factor on placed objects (civilians included), damage factor in `Damage` and on enemy
projectiles, score factor in the spawner's score rounding, rank factor in the displayed rank
(10.4). The player's own health is never scaled (players are built directly by
`G_InitObject`, 7.4).

### 6.4 Score award: same

`G_AwardScore` as2@0x414350 has the identical instruction shape of v170@0x40bb20 (ratio 1.000):
nothing for a zero award, p_scores += award capped at 10⁹, one `score_num` per decimal digit at
(x − (12n/2 − 6) + 12k, y, z + 8). This settles issue 031 §6 for both games (VERIFIED-CODE:
`if award ≠ 0` at the top of both).

### 6.5 Enemy health bar: same

`G_DrawHealthBar` as2@0x40bd70: same code as v170@0x404e30 apart from offsets (ratio 0.936,
the differences are the moved fields): maximum health > 150, class 2.0, less than 1 s since the
last damage, not dead; `hbar_full` / `hbar_empty`.

### 6.6 Invulnerability: same

`FreezeHealth` as2@0x421930 and `PlayerFreezeHealth` as2@0x4218d0 have the identical shapes of
v170@0x41c530 and v170@0x41c4e0 (freeze counter now at record +0xB8). The spawn shield
`p_rshield.scr` freezes the player's health for 7 s (VERIFIED-DATA).

---

## Changelog

- 1.0 (B4): first version.
