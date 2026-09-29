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

### 4.7 Terrain as the game rules see it (addition; no base section)

- **Height queries**: `R_TerrainHeight` as2@0x41a1e0 has the identical instruction shape of
  v170@0x416470 (ratio 1.000): bilinear on the 40-unit height grid, 0 outside the map. The grid
  now holds 12-byte vertices whose z is the height (as2@0x2102e5c), the same array the
  terrain drawer and the morphing use.
- **Water height**: 4.2.
- **Terrain morphing** (`TerraMorph`, `R_TerrainMorph` as2@0x41a670; details in the builtins
  delta, pending) adds a morph image's values to the z of that same vertex grid, skipping the
  vertices below the water level when the level has water. Every later height query sees
  the new ground: `FL_ONGROUND` and `FL_ONGROUND_NORMAL` roots follow it from their next think,
  new placements and drops are put on it, the `TerrainHeight` builtin returns it, skid marks
  are laid on it. There is no terrain collision in either game, and collision is screen-space
  (§5), so morphing changes hits only through the heights of the entities that follow the
  ground. VERIFIED-CODE (the shared array).
- **Tile sets**: the `as2` data has the atlases `tiles1..tiles9.tga` (v1.70: 1..5); the tile
  layer, the helipads included, is read only by the terrain loader and drawer
  (`R_LoadTerrain_Tiles` as2@0x41b780, renderer package); no game rule reads it. A level ends
  through `eol.scr` or a boss script calling `EndLevel` (VERIFIED-DATA: `eol.scr`,
  `boss_1_bashnya.scr`, `boss_2_kabina.scr`, `boss_3_helic.scr`).

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

`TraceLine` as2@0x420fd0 and `TraceLineDamage` as2@0x421180 (v170@0x41bd40, v170@0x41bea0) now also accept class 5.0 targets
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
| `Damage` | as2@0x420630 (v170@0x41b550) | none |
| `RadialDamage` | as2@0x4206b0 (v170@0x41b5d0) | also hits class 5.0 (civilians) |
| `RadialDamagePlayer` (new) | as2@0x420840 | the `RadialDamage` shape over the player records instead of the entity list (rcsl-builtins-table.delta.md); used by one script, the big rocket launcher's shock wave `rocket_launcher_big\wave.scr` (VERIFIED-DATA) |
| `TraceLine`, `TraceLineDamage` | as2@0x420fd0, 0x421180 (v170@0x41bd40, 0x41bea0) | class 5.0 accepted as well as 2.0 |
| `Lightning` | as2@0x421310 (v170@0x41c000) | takes the range as its argument (t0); spawns `wavegun_hit` at most every 0.2 s per target (+0x78) |
| Particle damage | `G_ParticleDamage` as2@0x40bbc0 (v170@0x404cb0) | the particle system's touch mode is read as a bit set: bit 0x2 → every player whose rectangle contains the particle; otherwise the first list entity that is alive (health > 0), on screen, and (bit 0x1 and class 2.0) or (bit 0x4 and class 5.0), containing it. Attacker −1 |
| `Shoot` | as2@0x420190 (v170@0x41b0d0) | the shooter must also be alive (8.1) |

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

## 7. The player

### 7.1 Player records: changed

Two records of **0x164** bytes at as2@0x20c5ad0 (player 1) and as2@0x20c5c34 (player 2); v1.70
0x171 bytes at v170@0x1ebe308. VERIFIED-CODE: every absolute access into both records in the
export was listed by offset and function, and the fields were identified from those functions
(`G_BeginLevel`, `G_NewGame`, `G_SpawnPlayer`, `G_PlayerFrame`, the item builtins, `G_BankScore`,
`PF_EndLevel`, `G_SaveBin`, the cheats, the HUD).

| as2 | v1.70 | Content |
|---|---|---|
| +0x00 | +0x00 | player entity pointer |
| +0x04..+0x87 | +0x04 | 11 key bindings {key1, key2, action bit} (same table, `G_InitActionBits` as2@0x410bf0, identical shape) |
| +0x88 | +0x88 | helicopter index 0..5 |
| +0x8C | +0x8C | `p_action` |
| +0x90 | – | `p_maxHealth` (new global; no code reads or writes it, rcsl-vm.delta.md) |
| +0x94 | +0x90 | `p_scores` |
| +0x98 | +0x94 | lives at level start (float) |
| +0x9C | +0x98 | `p_lives` |
| +0xA0 | +0x9C | `p_stars` |
| +0xA4 / +0xA8 / +0xAC | +0xA0 / +0xA4 / +0xA8 | `p_counter1..3` (`p_counter3` = 1.0 at level start) |
| +0xB0 | +0xAC | `p_speedfactor` |
| +0xB4 | +0xB0 | `p_weapon` |
| +0xB8 | +0xB4 | freeze-health counter |
| +0xBC | +0xB8 | u8 actions disabled (padded to 4 bytes) |
| +0xC0 | +0xB9 | 16 power-up counts |
| +0x100 | +0xF9 | current power-up (−1 none) |
| +0x104 | +0xFD | 5 missile counts |
| +0x118 | +0x111 | current missile type (−1 none) |
| +0x11C | +0x115 | **9** weapon upgrade levels (v1.70: 20) |
| +0x140 | +0x165 | banked campaign score (int) |
| +0x144 | +0x169 | rank accumulator (float) |
| +0x148 | +0x16D | kills this level |
| +0x14C..+0x154 | – | **new**: acceleration vector x, y, z (7.3), written by `G_PlayerFrame`, read by `GetPlayerAccel` |
| +0x158 | – | **new**: checkpoint lives (int, 10.3) |
| +0x15C | – | **new**: checkpoint banked score (int) |
| +0x160 | – | **new**: checkpoint rank accumulator (float) |

The `p_*` globals map into these records by the running entity's player index (+0x80), as in
v1.70.

### 7.2 Input to `p_action`: changed (mouse)

Same action bits and bindings (`G_InitActionBits` identical shape; `G_KeyEvent` as2@0x4110f0
against v170@0x408f10, ratio 0.970): a key ORs its bit into every non-disabled player that
binds it, a release clears it; `[Controls]` and `[Controls2]`; the one-shot edges 0x100, 0x200,
0x400 consumed by `G_PlayerFrame`; `PlayerDisableAction` as2@0x421960 same code. The only key
difference: F12 and PrintScreen no longer take a screenshot (the screenshot writer is gone).

`G_PlayerFrame` as2@0x413c50 (the symbol-map match, marked low, is confirmed: same edge
handling for 0x200, 0x100, 0x400 through `G_NextPowerUp` as2@0x413680, `G_NextMissile`
as2@0x413780, `G_NextWeapon` as2@0x413870, then the same game-over test). After the game-over
test it now **returns while paused** (as2@0x413d5e), and then, VERIFIED-CODE
as2@0x413d6b..0x413efa:

1. **Acceleration vector**, for each player: (ax, ay, az) = 0; unless the player's actions are
   disabled: right (0x80) ax += 1, forward (0x10) ay += 1, left (0x40) ax −= 1, backward (0x20)
   ay −= 1; when both ax and ay are non-zero the vector is scaled to length 1. So it is (0, 0),
   an axis unit vector or a unit diagonal.
2. **Mouse control** (`[System] MouseControl`, config default now **1**, as2@0x401b90; v1.70
   default 0): for player 1 only, when its actions are not disabled and its keyboard vector is
   (0, 0): read the cursor, take (cursor.x − window centre x, window centre y − cursor.y) as
   (ax, ay), scale it to length **2.0** (as2@0x48f1c0) when non-zero. Then (in every unpaused
   frame, mouse control or not) the cursor is put back at the window centre.

So AS2 mouse control steers by mouse **motion** (a relative device, twice the keyboard's
acceleration), not by the cursor position relative to the helicopter as v1.70 did
(v170@0x40b550); the v1.70 cursor sprite is gone from `HUD_Frame`. No direction bits are
synthesised any more.

### 7.3 Movement: changed

Movement is still script-side; the six `as2` player scripts (`scripts\player\player1..6\*.scr`)
share one routine (VERIFIED-DATA, same constants in all six; example `player1.scr`
pc 1382..1552):

1. a = `GetPlayerAccel()` (the record's vector, 7.2; `GetPlayerAccel` as2@0x421b40 copies
   record +0x14C..+0x154 of the calling helicopter's player).
2. velocity.xy += a.xy × 1000 × frametime.
3. Friction along the velocity: let d = velocity / |velocity|; each component is moved toward 0
   by 330 × frametime × |d.component|, not past 0. (v1.70: per axis, only while no key of that
   axis was held.) Friction now also acts while accelerating.
4. Each component clamped to ±150.
5. origin += velocity × frametime × `p_speedfactor` × **field 23** (the definition's `speed`).
6. origin.y += camera field 7 (scroll speed) × frametime (`movey`).
7. y clamped to [g_map_pos + 35, g_map_pos + 320].
8. Tilt: angles[14] = 30 × vy / 150, angles[15] = −25 × vx / 150.

`speed` therefore scales the **displacement**: top speed = 150 × speed units/s and the
effective acceleration and friction scale with it too (velocity is bounded at ±150 before the
factor). With the mouse the acceleration input is 2, i.e. twice as fast to build up.

The x clamp is native and unchanged: `V_UpdateCamera` as2@0x414e90 calls
`V_ClampToFrustumX` as2@0x41d4b0 (identical shape to v170@0x419b10) with margin 10.0 on player
1, or on every player with p_lives ≥ 0 in two-player mode. Issue 120 rule 8 applies.

Spawn fly-in (script, VERIFIED-DATA): start at (640, g_map_pos − 50, 100), `p_action` cleared,
vy = 120 and input ignored until y ≥ g_map_pos + 60; `p_speedfactor` = 1.0; spawn shield
`playerN_rshield` for 7 s.

### 7.4 Spawn, lives and respawn: changed

**Level start** (`G_BeginLevel` as2@0x410cd0; v170@0x408b30), for each player: p_lives = lives
at level start, p_scores = 0, p_stars = 0, kills = 0, p_counter3 = 1, `p_action` = 0 (VERIFIED-
CODE, loop as2@0x410d4d..0x410d61). **Weapon upgrades are not touched at level start** (v1.70 cleared
them and gave the machine gun): they are set by the mission loadout table on a new game and on
a restart, and carried over on "Next" (8.2).

**Lives.** A new game gives lives at level start = 2 (three helicopters) unless the mission is
the checkpoint mission (10.3). Banking copies p_lives to lives at level start (`G_BankScore`
as2@0x414540, same code as v170@0x40bd10). Restart reuses the lives at level start. Extra lives:
`i_life.scr` adds 1 to `p_lives` (VERIFIED-DATA); the cheat sets 99. The one-player HUD draws
min(p_lives, **10**) life icons at y = 555 (as2@0x407d20..0x407da0; v1.70: 5).

**`G_SpawnPlayer`** as2@0x413b20 (v170@0x40b2f0; the symbol-map match is confirmed):
1. removes the old entity;
2. **no p_lives test any more**: v1.70 marked the old entity dead and stopped when p_lives < 0;
   `as2` always builds a new helicopter. The player scripts test it themselves: on death they
   decrement `p_lives` and, when it is below 0, `deactivate` the wreck instead of calling
   `RespawnPlayer` (VERIFIED-DATA, `player1.scr` pc 1771..1777);
3. clears the missile and power-up counts and selections of **both** players (as v1.70);
4. builds the helicopter named by the table entry (index at record +0x88, table as2@0x49ddf8,
   7.6) with `G_InitObject`, sets the player index, the `player` global, clears the freeze
   counter and the actions-disabled byte, runs `init`;
5. two players: x −100 for player 1, +100 for player 2.

`RespawnPlayer` as2@0x421a50: same code (spawns the caller's player if the caller is a player
entity).

**Health** comes from the helicopter definition: `player_1` 500, `player_2` 400, `player_3`
300, `player_4` 600, `player_5` 800, `player_6` 600 (VERIFIED-DATA), never scaled by the
difficulty. The maximum health (+0x70) is the same value. **Every frame the HUD clamps the
player's health to its maximum** (`HUD_Draw1P` as2@0x407d20 at as2@0x407e67..0x407e7c for
player 1; `HUD_Draw2P` as2@0x408b00 for both players), so `i_armor100.scr` (health = 1000) is a
full repair and `i_armor50.scr` (+200) cannot overheal (VERIFIED-DATA for the scripts). The
clamp happens only while the HUD is drawn (not on intermission levels, not while the HUD is
hidden). An implementation should clamp in the game logic at the same point of the frame.

**Death** (script): no weapon level is lost (the six scripts never call `G_SetUpgrade`;
VERIFIED-DATA). v1.70's `player.scr` downgraded every weapon above level 1.

### 7.5 Two-player mode ("Cooperative"): changed in details

What the code shows (VERIFIED-CODE unless marked; co-op was not played):

- Selected in the player-selection menu (player count as2@0x49ded0, flag as2@0x2219145);
  "Cooperative" is the label.
- Separate records, bindings, lives, scores, weapons, missiles and power-ups per player; the
  missiles and power-ups of **both** are cleared whenever either respawns (7.4 step 3).
- Spawn: player 1 then player 2 (`G_StartLevel` as2@0x40e4e8..0x40e4fd), x ∓ 100.
- Camera follows the mean x of the players with p_lives ≥ 0 (same code as v1.70, 9.3); each
  such player is clamped to the frustum.
- Map objects: player index chosen by `rand` between the living players (same as v1.70).
  Projectiles carry the shooter's index and owner bits (8.1).
- Push-apart 2000 u/s² (5.4, same).
- Game over when both p_lives < 0 (same).
- No friendly fire: player projectiles only have `TOUCH_ENEMIES`, the player's blast waves
  use `RadialDamage` (enemies and civilians only), and the only `RadialDamagePlayer` call is
  the enemy big rocket launcher's shock wave (VERIFIED-DATA over the 631 scripts). Players are
  hurt by `TOUCH_PLAYER` objects, that shock wave, traces and particle systems with bit 0x2.
- Mission statistics are **not drawn** in two-player mode (`M_DrawMissionComplete`
  as2@0x427b60 tests the flag; v170@0x426360 drew them); high scores are only checked in one-player mode (same code).
- New builtins for scripts: `IsMultiplayer`, `IsPlayerInGame(i)` (1 when player i has an
  entity), `GetPlayersDistance` (the y distance from the other player when both have
  p_lives ≥ 0, else −1), the globals `player1` and `player2` (rcsl-vm.delta.md). No shipped
  script uses them.
- The two-player HUD (as2@0x408b00) is not decoded here (frontend package).
- Not verified: joining or leaving during a game, the camera when one player is dead for long,
  the checkpoint with two players (both records are saved, 10.5).

### 7.6 Helicopter choice: changed

Table as2@0x49ddf8 (v1.70 v170@0x457660, which the data map calls `g_missionUnlockTable`; see
"Corrections"), **6** records of 33 bytes {u8 unlocked, name[32]}, in this order (VERIFIED-CODE,
read from the executable):

| Index | Object | Unlocked by default | Unlocked by |
|---|---|---|---|
| 0 | `player_1` | yes | – |
| 1 | `player_2` | no | mission 4 (`enableHelic 1`) |
| 2 | `player_4` | no | mission 7 (`enableHelic 2`) |
| 3 | `player_6` | no | mission 10 (`enableHelic 3`) |
| 4 | `player_5` | no | mission 13 (`enableHelic 4`) |
| 5 | `player_3` | no | mission 16 (`enableHelic 5`) |

`G_MissionComplete` as2@0x427f40 unlocks entry `enableHelic` when 0 ≤ value < **6**
(v170@0x4269b0: < 10). The selection menu cycles the index modulo 6 (`M_HeliSelectAction`
as2@0x428fb0 cases 6 and 7). `G_Init` sets both players to index 0. The unlock flags are saved
in `game.bin`. The mission-complete screen shows "New helicopter is available." when the level
has an `enableHelic` value in 0..5 (as2@0x427d23..0x427d50), and offers "Choose Helicopter".

---

## 8. Weapons and items

### 8.1 Weapon definitions and `Shoot`: changed (one test)

`.wpn` parsing (`G_ParseWeapon` as2@0x415410, unique string "Too many weapons.", 256 records):
same. `Shoot` as2@0x420190 against v170@0x41b0d0 (ratio 0.904; the other differences are moved
offsets and the root reference count done by a helper): step 1 now also returns when the
shooter is **dead** (field 4 ≠ 0, as2@0x4201af..0x4201bc), so a destroyed enemy or a crashing
helicopter cannot fire any more. The corrections of rcsl-builtins-semantics.md for `Shoot`
(no return value, order of init and damage scaling) are the other agent's to re-check.

### 8.2 Weapon upgrades: changed

- **9 slots** (record +0x11C). `G_GetUpgrade(i)` returns 0 for i > 8 (as2@0x4217c0; v170@0x41c3d0
  used 19), `G_SetUpgrade(i, v)` stores for i < 9 (as2@0x421830; v170@0x41c440 used 20); both truncate (rcsl-builtins-semantics
  correction). v1.70: 20 slots.
- Weapon ids (VERIFIED-DATA, `items\ammo\*.scr` and the player scripts; maximum level from the
  pick-up's cap): 0 machine gun (4), 1 impulse gun (5), 2 plasma gun (7), 3 laser (8), 4 big
  laser (5), 5 lightning gun (5), 6 wave gun (4), 7 missile gun (5), 8 flamethrower (3). A
  pick-up raises the level by 1 up to the cap and sends `callback(player, 2, id, 0)` the first
  time.
- `G_NextWeapon` as2@0x413870: cycles `p_weapon` to the next owned slot modulo **9**.
- **Mission loadout table** (as2@0x48b3a8, 18 rows × 9 ints, read from the executable), applied
  by `G_SetMissionLoadout` as2@0x4138f0 (the symbol map's `G_ResetPlayers`) to **both** players:
  every slot is set to the row's value and `p_weapon` becomes the highest slot with a non-zero
  value. The row index is the mission index clamped to 0..17. It is applied by `G_NewGame`
  (every start from the menu), the mission-complete **Restart** and the game-over **Restart**
  (VERIFIED-CODE as2@0x410dc0, as2@0x427a20 case 2, as2@0x428ac0; v1.70 instead cleared the
  upgrades at every level start, v170@0x408b30), **not** by "Next" (as2@0x427a20
  case 3 banks and starts the next mission with the upgrades collected so far).

| Mission | 0 MG | 1 impulse | 2 plasma | 3 laser | 4 big laser | 5 lightning | 6 wave | 7 missile | 8 flame |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 1 | | | | | | | | |
| 2 | 4 | 3 | | | | | | | |
| 3 | 4 | 5 | | | | | | | |
| 4 | 4 | 5 | 3 | | | | | | |
| 5 | | 5 | 7 | | | | | | |
| 6 | | 5 | 7 | 3 | | | | | |
| 7, 8 | | | 7 | 3 | | | | 5 | |
| 9 | | | 7 | 5 | | | | 5 | |
| 10 | | | 7 | 8 | | | 3 | 5 | |
| 11 | | | 7 | 8 | | | 4 | 5 | |
| 12 | | | 7 | 8 | | 5 | 4 | 5 | |
| 13 | | | 7 | 8 | 3 | 5 | 4 | 5 | |
| 14 | | | 7 | 8 | 5 | 5 | 4 | 5 | |
| 15 to 18 | | | 7 | 8 | 5 | 5 | 4 | 5 | 3 |

(Empty = 0, not owned. The rows for missions 15 to 18 are identical.)

### 8.3 Missiles and power-ups: changed (kinds, cycling)

Same builtins and caps (`G_AddMissiles` as2@0x4216c0: type < 5, count capped at 99, selects the
type if none; `G_AddPowerUp` as2@0x421550: type < 16, cap 99; `G_UseMissile` / `G_UsePowerUp`
through as2@0x413800 / as2@0x413710 with the corrected v1.70 rule of rcsl-builtins-semantics.md;
new `G_SetPowerUpCount` as2@0x421650 sets a count directly, its index unchecked). Slot counts:
**5 missile types, 16 power-up slots** (as v1.70). Changes:

- **Power-up cycling** (`G_NextPowerUp` as2@0x413680 against v170@0x40b0d0) skips the slot
  numbers 6, 7 and 9 (as2@0x4136a4..0x4136ae); otherwise the next owned slot modulo 16 as before.
- Missile cycling (`G_NextMissile` as2@0x413780): same (modulo 5).
- Kinds used by the data (VERIFIED-DATA, pick-up scripts):

| Missile type | Pick-up | | Power-up slot | Name (pick-up script) | Pick-up |
|---|---|---|---|---|---|
| 0 | +20 | | 0 | lightning bomb (`i_lightingb`) | +2 |
| 1 | +12 | | 1 | A-bomb (`i_abomb`) | +1 |
| 2 | +20 | | 2 | rocket bomb (`i_rocketbomb`) | +4 |
| 3 | +15 | | 3 | cluster bomb (`i_clustbomb`) | +4 |
| 4 | +12 | | 4 | annihilator (`i_annihilator`) | +500, capped at 99 |
| | | | 5 | satellite strike (`i_satellitestrike`) | +2 |
| | | | 8 | air support bomber (`i_bomber`) | +1 |

The missile kinds keep their v1.70 pick-up counts. Cheat "glitteringprizes" fills slots 0..5
and 8, matching the kinds in use.

What resets and what carries over: missiles and power-ups are cleared at every spawn (level
start and each respawn) for both players; weapon upgrades carry over except where the loadout
table is applied (8.2); lives carry over through banking or the checkpoint (10.3).

### 8.4 Targeting: same

`LockTarget` through as2@0x414c50 (identical shape to v170@0x40c020) with the corrections of
rcsl-builtins-semantics.md: class 2.0 only, so civilians are never locked. `IsValidTarget`,
`PushPlayer` as2@0x4214a0 (identical shape, 4000 u/s²): same.

---

## 9. Scrolling and camera

### 9.1 The camera structure: same

Camera record as2@0x20df0a8 (`camera` global as2@0x49d900), same field layout (rcsl-vm.delta.md
"Camera structure"): fields 21 and 22 are the activation edges g_map_pos − 64 and
g_map_pos + 1000 (as2@0x414f7e, 0x414f8a in `V_UpdateCamera`).

### 9.2 Scrolling: same

`V_ResetCamera` as2@0x415290 (v170@0x40c660; the manual match is confirmed): g_map_pos = 32,
scroll factor 1, scroll speed 42, camera x 640, edges g_map_pos − 64 and g_map_pos + 800,
camera mode clamped to 0..3; the only addition is the debug option `-campos N` (g_map_pos =
N × 40). `V_UpdateCamera` as2@0x414e90 has the identical instruction shape of v170@0x40c260
(ratio 1.000, only structure offsets differ): g_map_pos += frametime × 42 × factor, 42.0 at
as2@0x48f430. Native code never changes the factor; scripts do (bosses, `eol.scr`,
`p_speeddown.scr`, VERIFIED-DATA).

### 9.3 Camera modes and placement: same

Mode table as2@0x49db28, read from the executable, identical to v170@0x457398:

| Mode | FOV | Pitch | Height | Y offset |
|---|---|---|---|---|
| 0 | 60 | −35 | 270 | 0 |
| 1 | 60 | −45 | 270 | 0 |
| 2 | 60 | −50 | 270 | 0 |
| 3 | 70 | −15 | 370 | +100 |

Mode index as2@0x49db68, `[System] Camera` default 1, F9 cycles (as2@0x4110f0 case 0x78). Follow
dead zone ±48, clamp [578, 702], mean x of the players with p_lives ≥ 0: same code. The new
builtin `GetMapPosOfs` returns the current mode's Y offset (as2@0x421b20). The `cameramode`
script global is not the engine's mode (rcsl-vm.delta.md). Projection rule (renderer, checked
only for the constants): near 4, far 2000 without fog, else the fog end but at least 1000
(`R_RenderView` as2@0x430a20, constants 4.0, 2000.0, 1000.0 as in v170@0x40e0d0).

### 9.4 CameraQuake: same

`CL_CameraQuake` as2@0x414d80 and the builtin as2@0x420e20 have the identical instruction shapes
of v170@0x40c150 and v170@0x41bb90 (ratio 1.000): 3 s, e^(−2t), yaw 2A·sin 24t, roll A·sin 12t,
FOV + A·sin 18t.

### 9.5 Screen and world: same

2D layer 800×600, player bounded by the frustum in x and by its script in y, collision in window
pixels (our fixed 800×600 deviation).

### 9.6 Intermission (attract) levels: changed (spawning)

Fixed camera from the `intermission` line with the same sway (pitch + 0.5·sin(0.5·time), roll +
1.4·sin(0.75·time), FOV 60; same code, level record +0x1E8..+0x1FC instead of +0x1A8..+0x1BC),
no players (as2@0x414eb6..0x414f2c against v170@0x40c286..0x40c2fc). New: every placed object of
the attract level is spawned in the first spawner call, during loading (as2@0x40e815; v1.70
applied the window, v170@0x407590) (3.4). `as2` has two attract levels, `intro1` (map `intro2.hsc`) and `intro2`
(map `intro1.hsc`), both with `intermission 150 256 200 -60 0 30` (VERIFIED-DATA).

### 9.7 Activation handover: changed (first call)

Same rows (≤ (g_map_pos + 1000) / 40 and ≥ (g_map_pos − 64) / 40), same activation band and
positions. The first spawner call happens inside `G_StartLevel` with the reset edges
(g_map_pos − 64 = −32 and g_map_pos + 800 = 832), i.e. rows 0 to 20 at load time (§2); from the
first frame on, the edges of `V_UpdateCamera` apply.

---

## 10. Level flow

### 10.1 Level list: same format, 18 missions

`levels.txt` parser `G_ParseLevel` as2@0x40d490 (unique string "Too many levels in level-list
file.", at most 32 records) with the same keyword set as v170@0x406340 (a diff of the keyword
strings of both parsers finds none added or removed; the `water` line's arguments belong to the
level package). The mission table as2@0x49df60 has **18** entries (`mission1` .. `mission18`,
missions 1 and 2 unlocked by default, read from the executable); the shipped `levels.txt` has
20 records, the 18 missions and `intro1`, `intro2` (VERIFIED-DATA). `enableHelic` defaults to
−1; the data uses 1 to 5 (7.6).

Mission roles (VERIFIED-DATA, names and maps in `levels.txt`; no native code treats them
specially, VERIFIED-CODE: the only uses of the mission index are the loading comic, the
portrait dialogues, the loadout table, the checkpoint and the unlock):

| Mission | Role | Map |
|---|---|---|
| 1 | tutorial ("Mission 1: Tutorial", `ShowTutorialHint` in its scripts) | `level1_tutor.hsc` |
| 6, 12, 18 | boss levels | `level_boss1/2/3.hsc` |
| 7, 13 | bonus levels ("Gold Isle", "Treasures of Ancients") | `bonus_level.hsc`, `bonus_level2.hsc` |

The loading screen shows one of three comic sets by mission index (0..6, 7..12, 13..17;
`SCR_SelectLoadingComic` as2@0x40acc0; frontend package).

### 10.2 Level start: changed

`G_BeginLevel` as2@0x410cd0 (v170@0x408b30): difficulty factors (6.3), clears HUD hidden,
pause and game over, resets the per-player state (7.4), calls `G_StartLevel`, **then** sets
the enemy total (as2@0x5432c0) and the maximum level score (as2@0x5432c4) to 0
(as2@0x410d96..0x410d9c), and writes `objects.txt` under `-obj`. Same order as v1.70.

`G_StartLevel` as2@0x40e1e0 (v170@0x407080), in order (VERIFIED-CODE): intermission flag from
the level record (+0x1E4), pause cleared; loading comic chosen and UI assets; HUD assets;
particle systems; the map (`G_LoadMap` as2@0x4113c0, which counts the `item_star` drops into
the star total as2@0x543294 as before); fog; per-placement precache; particle pool (256), entity
pool (1024), **skid-trail pool (64)**; `V_ResetCamera`; BASS; players spawned (not on
intermission levels); spawner cursor reset; `l_night`, `l_water`, `l_waterlevel`, level clock
0; **one spawner call, one entity pass and one render (§2)**; music; on mission levels the
start dialogue (`M_ShowPortraitDialog(mission, start)` as2@0x40e639), which pauses the game
while it is shown (missions with a start dialogue: 1, 2, 3, 5, 6, 8, 11, 12, 14, 15, 17, 18;
table as2@0x49d530).

Consequence (VERIFIED-CODE, the order of these calls): the enemies and scores of the objects
spawned by the load-time call (rows 0..20) are added to the counters and then erased by
`G_BeginLevel`. The enemy total and the maximum level score therefore count only the objects
spawned during play. Kills of load-time enemies still count, up to the enemy total (6.1). See
[issue 211](issues/211-load-time-spawn-and-statistics.md) for what an implementation should
do.

### 10.3 End of level: changed

**`EndLevel`** as2@0x40e730 (v170@0x407570), VERIFIED-CODE:

1. Checkpoint for the next mission: checkpoint mission (as2@0x49ddf4) = current mission index +
   1; for **both** player records: checkpoint lives (+0x158) = ftol(p_lives), checkpoint score
   (+0x15C) = ftol(banked score + p_scores), checkpoint rank (+0x160) = rank accumulator +
   p_stars / star total + 0.5 × p_scores / maximum level score (the banking formula of 10.4).
2. HUD hidden.
3. `M_ShowPortraitDialog(mission, end)`: when the mission has an end dialogue (missions 1, 2,
   3, 4, 5, 6, 9, 10, 11, 12, 15, 16) the game pauses behind it and the mission-complete screen
   follows it; otherwise the game pauses and `G_MissionComplete` runs at once.
4. Pause.

`EndLevel` no longer calls `G_MissionComplete` itself. `eol.scr` (VERIFIED-DATA) still disables
the player's actions, sets `p_speedfactor` 0.5, flies the player out, ramps the scroll down,
waits 0.5 s and calls `EndLevel`.

**`G_MissionComplete`** as2@0x427f40 (v170@0x4269b0): unlocks helicopter `enableHelic` when
0..5, unlocks mission (index + 1) mod **18**, and pushes the game-complete menu after mission 18
(index 17), otherwise the mission-complete menu. Its buttons (`M_MissionCompleteAction`
as2@0x427a20): **Quit** (attract level and main menu; no banking, no high-score check, as in
v1.70), **Restart** (loadout table applied, `G_BeginLevel`), **Choose Helicopter** (opens the
player-selection menu in "Accept" mode; frontend), **Next** (pops the menus, frees the level,
banks the score, mission index = (index + 1) mod 18, `G_BeginLevel`). Game complete
(`M_GameCompleteAction` as2@0x428140): bank, attract level, main menu, high-score check.

**`GameOver`** (new builtin, native `G_GameOver` as2@0x410e70): 1.3 state 8.

**`G_NewGame`** as2@0x410dc0 (v1.70 "new campaign" v170@0x408c40), run by the start button:
- if the selected mission index is 0, or differs from the checkpoint mission: checkpoint
  mission = −1 and both players' checkpoint values = lives 2, score 0, rank 0;
- then for both players: lives at level start = checkpoint lives, banked score = checkpoint
  score, rank accumulator = checkpoint rank;
- `-god` → god mode;
- loadout table for the mission (8.2), then `G_BeginLevel`.

So choosing the mission that follows the last completed one ("Continue") resumes the campaign
with its lives, score and rank; any other choice starts fresh with 3 helicopters.

### 10.4 Scoring and rank: changed (display conditions)

Banking (`G_BankScore` as2@0x414540): same instructions as v170@0x40bd10 (banked score =
ftol(banked + p_scores); rank accumulator += p_stars / star total + 0.5 × p_scores / maximum
level score; lives at level start = p_lives). Rank index (`G_RankIndex` as2@0x4145d0, identical
shape): Cheater if a cheat was used, else thresholds 3, 7.5, 14, 22, 30 (table as2@0x49e654,
same values), names Cheater, Rookie, Junior Pilot, Pilot, Master Pilot, Berserker, Elite (table
as2@0x49cba4, same strings). HUD score = ftol(p_scores) + banked (as2@0x407d20).

The mission-complete screen (`M_DrawMissionComplete` as2@0x427b60, v170@0x426360):
- the three lines appear at 1.0, 1.4 and 1.8 s as before, **only in one-player mode**;
- "Enemies destroyed" is drawn only when the enemy total ≠ 0 (v1.70 divided by it); kills are
  capped at the total (6.1), so it never exceeds 100 %;
- the rank line uses (p_stars / star total + 0.5 × p_scores / maximum level score + rank
  accumulator) × rank factor, as before (a level without stars still divides by zero; guard it);
- new line "New helicopter is available." at (400, 400) when the level's `enableHelic` is
  0..5.

No end-of-level bonus (same as v1.70).

### 10.5 Save file: changed

`G_SaveBin` as2@0x406c70 / `G_LoadBin` as2@0x4069e0 (same XOR and CRC scheme as v170@0x4011b0 /
v170@0x401050).
Payload of 0x574 bytes: a u32, 15 high scores × 40 bytes, **6** helicopters × 33 bytes,
**18** missions × 33 bytes; then a second block of 7 dwords XORed with the same key: the
checkpoint mission and, per player, checkpoint lives, score and rank. **When a cheat was used
in the session the checkpoint is saved as "none"** (mission −1, zeros). Default high scores
start with "Divo Master" (as2@0x49db78). Our engine keeps its own save format (package A3);
this section lists what is saved. Byte layout of the second block: not checked beyond its
content (save package).

### 10.6 Unlocking: changed

Completing mission i unlocks mission (i + 1) mod 18 and the helicopter entry named by
`enableHelic` (0..5, table order of 7.6) (`G_MissionComplete` as2@0x427f40; v170@0x4269b0: mod 20,
0..9). Nothing else.

---

## 11. HUD and menus

### 11.1 Coordinates: same

Virtual 800×600, 2D queue of 4096 quads (the `< 0x1000` checks in `HUD_Draw1P` as2@0x407d20).

### 11.2 One-player HUD: changed (art; rules noted here)

The drawing is new (frontend package; `HUD_Draw1P` as2@0x407d20 against v170@0x401ed0). The
game rules it carries, VERIFIED-CODE:

- health clamped to the maximum health every frame (7.4), for both players in two-player mode
  (`HUD_Draw2P` as2@0x408b00);
- the health bar's fill is health / maximum health (as2@0x407e88..0x407e8e), not health / 400;
- up to 10 life icons (7.4);
- score = ftol(p_scores) + banked score;
- the level-name typewriter (`HUD_DrawLevelName` as2@0x407ab0, ratio 0.978 against
  v170@0x401c60) and the 3 s cheat message (`HUD_DrawMessage` as2@0x406f80, ratio 0.965 against
  v170@0x401330): not re-checked beyond the shape;
- no mouse-control cursor (7.2).

### 11.3 Tutorial hints: same

`ShowTutorialHint` as2@0x4218b0 is the same code as v170@0x41c4c0 (rcsl-builtins-table.delta.md).

### 11.4 Menu tree: changed

New screens and flow (player selection with helicopter preview, portrait dialogues, comics,
options with a resolution list, second items page, credits); frontend package. The game-rule
parts are in 1.3 and 10.3.

---

## 12. Sound: not checked

Symbol-map verdict: 10 of 13 sound functions identical to v1.70, same BASS imports. Checked
here only: F5/F6 and F7/F8 still change the effects and music volume by 0.1 (as2@0x4110f0),
and game over still jumps the music (`S_ChangeMusic` as2@0x422880 from `G_GameOver` and
`G_PlayerFrame`). The level music is loaded and looped by `G_StartLevel` as before.

---

## 13. Rendering order and material state: not checked

Renderer package. For the game rules only: `R_RenderView` as2@0x430a20 draws the terrain, sets
the frustum, draws the decals (TYPE_MARK, as2@0x42ff70), **then the skid trails**
(as2@0x430280), the shadows, the opaque list, the water, the TRANS and EFFECT lists, the
particles, the sprites and the 2D list; the debug counters moved to `R_DrawStats` (§2). Near and
far planes: 9.3.

---

## 14. Cheats and debug features: changed

`G_CheatInput` as2@0x407110 (v170@0x4014c0), called from the character handler as2@0x4110a0
only outside intermission levels (same rule). Same mechanism: a rolling 32-character lower-case
buffer; on a match the buffer is cleared, the cheat-used flag is set (rank Cheater, and the
campaign checkpoint is not saved, 10.5) and `sounds\cheat.wav` plays. **All code words are new**
(VERIFIED-CODE, strings in the function):

| Code | Effect | Message |
|---|---|---|
| `invulnerability` | toggles god mode (player damage ignored in `G_Damage`) | "God Mode: Enabled" / "God Mode: Disabled" |
| `igonnaliveforever` | p_lives = 99 for both players | "All Lives: Enabled" |
| `showmetheweapons` | every weapon slot 0..8 at level 0 becomes level 1, both players | "All Weapons: Enabled" |
| `moremoreweapons` | +99 (cap 99) of every missile type 0..4, both players; selects type 0 if none | "All Missiles: Enabled" |
| `glitteringprizes` | +99 (cap 99) of power-up slots 0..5 and 8, both players; selects slot 0 if none | "All Power-Ups: Enabled" |
| `deadlineisnear` | `EndLevel` (10.3) | none |
| `diediediemydarling` | `GameOver` | none |

There is no credits cheat any more.

Keys: P or Pause pauses, Esc opens the in-game menu, F5..F8 volumes, F9 next camera mode; **no
screenshot key**. Command-line debug switches `-god`, `-obj`, `-campos N`, `-notex` (1.1). Config
`[Debug]` still has `ShowFPS`, `ShowTris`, `ShowTexBinds`, `ShowCounters` (read by
as2@0x401b90), drawn by `R_DrawStats`.

---

## 15. Constants and limits

Values for `as2`; "same" means equal to the base table (VERIFIED-CODE unless marked).

| Item | as2 | Evidence |
|---|---|---|
| Maximum frame delta | 100 ms (same) | as2@0x405ca0 |
| Entity pool | 1024 × **0x1F4** bytes; no overflow check | as2@0x40e1e0, as2@0x40b4b0 |
| Object definitions | 2048 (same) | as2@0x4125d0 |
| Attaches per definition | 64 (same) | as2@0x41311f |
| Skid marks per definition | 8 (record area; not checked by the parser) | 3.1.2 |
| Skid-trail pool / drawn per frame | **64 / 64** (new) | as2@0x40e474, as2@0x414850 |
| Skid node interval / nodes / life / fade start / height | **5/12 s / 23 / 10 s / 5 s / terrain + 2** | as2@0x414850, as2@0x4300e0 |
| Map objects | 16384 (same) | "Too many map objects", as2@0x4113c0 |
| Levels | 32 (same) | as2@0x40d490 |
| Missions / helicopters / high scores | **18 / 6** / 15 | as2@0x427f40, as2@0x49ddf8, as2@0x406c70 |
| Weapons (.wpn blocks) | 256 (same) | "Too many weapons.", as2@0x415410 |
| Weapon upgrade slots | **9** | as2@0x4217c0 |
| Missile types / power-up slots | 5 / 16, counts capped at 99 (same) | as2@0x4216c0, as2@0x421550 |
| Power-up slots skipped by cycling | **6, 7, 9** | as2@0x4136a4 |
| Scroll speed / start g_map_pos | 42 × factor / 32 (same) | as2@0x414e90, as2@0x415290 |
| Camera x range, dead zone, start | 578..702, ±48, 640 (same) | as2@0x414e90 |
| Camera modes | same table | as2@0x49db28 |
| Near / far plane | 4 / 2000 (fog end, at least 1000) (same) | as2@0x430a20 |
| Frustum x margin for players | 10 (same) | as2@0x414e90 |
| Map object instantiation / reset edge | g_map_pos + 1000 / + 800, back edge − 64 (same values) | as2@0x414f7e, as2@0x415290 |
| First spawner call | **during level load**, rows 0..20 | as2@0x40e577 |
| Script activation band | g_map_pos + 16 … + 800 (same) | as2@0x40c000 |
| Player speed | max 150 × `speed`, acceleration 1000, friction 330 along the velocity | player scripts (VERIFIED-DATA) |
| Player acceleration input | unit vector (keys), length 2.0 (mouse) | as2@0x413d6b.., as2@0x48f1c0 |
| Player y band | g_map_pos + 35 … + 320 (same) | player scripts (VERIFIED-DATA) |
| Player health | **300..800 from the definition, clamped to it every frame** | 7.4 |
| Lives | 2 spare at a fresh start (same), or the checkpoint | as2@0x410dd6 |
| Life icons | **10** | as2@0x407d20 |
| Spawn shield | 7 s (same) | `p_rshield.scr` |
| Mouse control | **default on, relative motion** | as2@0x401b90, 7.2 |
| Two-player push / spawn offset | 2000 / ±100 (same) | as2@0x40c5a8.., as2@0x413b20 |
| PushPlayer | 4000 (same) | as2@0x4214a0 |
| Lightning range | **argument** (500 and 200 in the data) | rcsl-builtins-table.delta.md |
| Lightning hit-effect interval | **0.2 s per target**, timer capped at 5 s | as2@0x42144e, as2@0x40cb82 |
| Lock-on cone | d.y ≥ 0.3 (same) | as2@0x414c50 |
| Score cap / digit spacing | 10⁹ / 12 units, z + 8 (same) | as2@0x414350 |
| Health bar | maximum health > 150, 1 s after a hit (same) | as2@0x40bd70 |
| CameraQuake | 3 s, same shape | as2@0x414d80 |
| Intermission sway | pitch 0.5·sin(0.5 t), roll 1.4·sin(0.75 t) (same) | as2@0x414e90 |
| Difficulty table | same | as2@0x49dee8 |
| Rank thresholds and names | same | as2@0x49e654, as2@0x49cba4 |
| 3D sound | not checked | – |

---

## Values for GameRules

For every field of `GameRules` (`engine/include/as3d/game_profile.h`), the `as2` value and its
evidence. "Changes the profile" marks a value that differs from what
`engine/src/game/game_profiles.cpp` holds today.

| Field | as2 value | Evidence | Changes the profile |
|---|---|---|---|
| `missionCount` | 18 | mission table as2@0x49df60 (18 records); unlock modulo 0x12 in as2@0x427f40 and as2@0x427a20 | no |
| `attractCount` | 2 | `G_Init` as2@0x410eb0 picks `intro1` or `intro2` | no |
| `helicopterCount` | 6 | table as2@0x49ddf8; modulo 6 in as2@0x428fb0; unlock bound < 6 in as2@0x427f40 | no |
| `heliObjects` | `player_1, player_2, player_4, player_6, player_5, player_3` | table as2@0x49ddf8, read from the executable | **yes**: the profile lists `player_1..player_6` in numeric order; `enableHelic n` unlocks entry n of the table order |
| `difficultyCount` | 5 | clamp to 4 in `G_BeginLevel` as2@0x410cd0 | no |
| `defaultDifficulty` | 2 | initial value of as2@0x49ded4 | no |
| `difficulty[0..4]` | {0.3, 0.5, 0.6, 0.7}, {0.5, 0.7, 0.8, 0.85}, {0.75, 0.8, 1.0, 1.07}, {1.5, 1.25, 1.2, 1.2}, {2.0, 1.4, 1.4, 1.3} | as2@0x49dee8 | no |
| `startLives` | 2 | `G_NewGame` as2@0x410dd6 (without a checkpoint) | no |
| `bonusMissions` | 7, 13 | `levels.txt` names and maps (VERIFIED-DATA); no native role | no |
| `bossMissions` | 6, 12, 18 | `levels.txt` maps `level_boss1..3` (VERIFIED-DATA); no native role | no |
| `scrollSpeed` | 42 | as2@0x48f430, `V_UpdateCamera` as2@0x414e90 | no |
| `startMapPos` | 32 | `V_ResetCamera` as2@0x415290 | no |
| `startCameraX` | 640 | `V_ResetCamera` as2@0x415290 | no |
| `cameraMinX`, `cameraMaxX` | 578, 702 | `V_UpdateCamera` as2@0x414e90 | no |
| `cameraFollow` | 48 | `V_UpdateCamera` as2@0x414e90 | no |
| `playerClampMargin` | 10 | argument of `V_ClampToFrustumX` in as2@0x414e90 | no |
| `cameraModeCount` | 4 | clamp 0..3 in as2@0x415290; F9 modulo 4 in as2@0x4110f0 | no |
| `defaultCameraMode` | 1 | `[System] Camera` default "1" (as2@0x401b90); initial as2@0x49db68 | no |
| `cameraModes[0..3]` | same four presets | as2@0x49db28 | no |
| `scoreDigitObject` | `score_num` | as2@0x410eb0 | no |
| `starItemObject` | `item_star` | `G_LoadMap` as2@0x4113c0 | no |
| `healthBarEmptyObject`, `healthBarFullObject` | `hbar_empty`, `hbar_full` | `G_DrawHealthBar` as2@0x40bd70 | no |
| `terraMorph` | true | `TerraMorph` builtin, `R_TerrainMorph` as2@0x41a670 | no |
| `civilians` | true | 3.1.1 | no |
| `skidMarks` | true | 3.1.2 | no |
| `waterFlags` | true | 3.1.4, 4.2 | no |
| `coop` | true | "Cooperative" label; 7.5 | no |

### AS2 rule constants without a field

Values that differ from v1.70 (or are new) and have no field yet, for the orchestrator to add.
v1.70 value in the last column.

| Proposed field | as2 value | Section | v1.70 |
|---|---|---|---|
| `weaponSlots` | 9 | 8.2 | 20 |
| `missionLoadout[18][9]` | the table of 8.2 (as2@0x48b3a8) | 8.2 | none: level start clears upgrades and gives slot 0 level 1 |
| `loadoutOnNext` | false (upgrades carry over on "Next") | 8.2 | upgrades cleared at every level start |
| `powerUpCycleSkip` | {6, 7, 9} | 8.3 | none |
| `killCapAtEnemyTotal` | true | 6.1 | false |
| `campaignCheckpoint` | true (checkpoint saved unless a cheat was used) | 10.3, 10.5 | false |
| `spawnAllOnIntermission` | true | 3.4, 9.6 | false |
| `spawnDuringLoad` | true (one spawner call, entity pass and render inside level start; counters reset after) | §2, 10.2 | false |
| `missionCompleteViaDialogue` | end dialogue table (missions with an end dialogue) then the mission-complete screen | 10.3 | `EndLevel` shows the screen at once |
| `startDialogueMissions` | 1, 2, 3, 5, 6, 8, 11, 12, 14, 15, 17, 18 | 10.2 | none |
| `lifeIconsMax` | 10 | 7.4 | 5 |
| `clampPlayerHealthToMax` | true | 7.4 | false |
| `healthBarScaleFromMax` | true (HUD fill = health / max) | 11.2 | fill = health / 400 |
| `accelInput` | true (native unit vector for `GetPlayerAccel`) | 7.2 | none |
| `mouseAccel` | 2.0, relative mouse motion | 7.2 | absolute cursor steering, 20 px dead zone |
| `mouseControlDefault` | 1 | 7.2 | 0 |
| `respawnChecksLives` | false (`G_SpawnPlayer` always spawns) | 7.4 | true |
| `deadShootersBlocked` | true | 8.1 | false |
| `touchModeBits` | true (bit set; `TOUCH_ALL` = 0xF; dead candidates skipped) | 5.2 | modes 1, 2, 3 |
| `waterFollowsWaves` | true (`FL_ONWATER` uses the animated surface) | 4.2 | flat level |
| `skidTrailPool`, `skidTrailsDrawn` | 64, 64 | 3.1.2 | none |
| `skidNodeInterval`, `skidMaxNodes`, `skidLife`, `skidFadeStart`, `skidHeightOffset` | 5/12 s, 23, 10 s, 5 s, 2 | 3.1.2 | none |
| `lightningEffectObject`, `lightningEffectInterval`, `lightningTimerCap` | `wavegun_hit`, 0.2 s, 5 s | 3.2 | none |
| `statsOnlyOnePlayer` | true | 10.4 | false |
| `newHeliMessage` | "New helicopter is available." | 10.4 | none |
| `cheatCodes` | the table of 14 | 14 | the v1.70 table |
| `screenshotKey` | none | 7.2, 14 | F12, PrintScreen |

v1.70 constants the first port left as literals, checked for `as2`: **all unchanged** — quake
shape (3 s, e^(−2t), 24/12/18 Hz terms), intermission sway (0.5·sin 0.5t, 1.4·sin 0.75t, FOV 60),
view window −64 / +1000 and +800 at reset, score digit spacing 12 and z + 8, two-player spawn
offset ±100, near 4 and far 2000 / fog end ≥ 1000, push-apart 2000, `PushPlayer` 4000, health bar
threshold 150 and 1 s, lock-on 0.3, activation band +16 / +800 with the 80 and 200 insets,
100 ms frame cap, 16384 map objects, 2048 definitions, 256 weapons, 32 levels.

---

## Checked sections

| Base section | Status for `as2` |
|---|---|
| 1 Program structure | changed (1.1, 1.3) |
| 1.1 Start-up | changed: no `play.url`, new debug switches, CD check (dropped) |
| 1.2 Main loop and timing | same (an inactive play-time counter added) |
| 1.3 Game states | changed: flag addresses, two attract levels, player-selection start, `GameOver` builtin, checkpoint |
| 2 Per-frame update order | changed: skid-trail update after the entity pass, statistics overlay split out, level start runs one spawner call, entity pass and render |
| 3 Entities | changed |
| 3.1 Object definitions | changed: `civilian`, `speed`, `skid_mark`, water flags, touch bits |
| 3.2 Entity memory layout | changed: 0x1F4 bytes, aligned layout, timer +0x78, fields 88/89 skid trails, children at 90/91 |
| 3.3 Pool and lists | same (skid-trail pool added, 3.1.2) |
| 3.4 Creating entities | changed: intermission levels spawn everything, first call during loading |
| 3.5 Activation states | same |
| 3.6 Event entry points | same (touch pairs in 5.2) |
| 4 Movement and transforms | changed (4.1, 4.2, 4.4) |
| 4.1 Think order | changed: `Lightning` timer |
| 4.2 Position and velocity | changed: water height and water alignment |
| 4.3 Axis from angles | same |
| 4.4 Attachment to tags | changed: non-model parents (unused by the data); definition children same |
| 4.5 Waypoint paths | same |
| 4.6 World bounds | same |
| (4.7 Terrain, `as2` addition) | new section: height queries same, morphing moves the ground for every height query, tile sets visual only |
| 5 Collision | changed (5.2, 5.5) |
| 5.1 Shapes | same (issue 120's overlap rule confirmed for `as2`) |
| 5.2 Which pairs are tested | changed: bit-set modes, civilians, dead candidates skipped |
| 5.3 What a hit triggers | same |
| 5.4 Two-player push-apart | same |
| 5.5 Traces | changed: civilians (details pending in the builtins delta) |
| 6 Damage and death | changed (6.1, 6.2) |
| 6.1 `G_Damage` | changed: kill counter capped at the enemy total |
| 6.2 Damage sources | changed: civilians as targets, `RadialDamagePlayer`, `Lightning` range, particle touch bits, `Shoot` dead check |
| 6.3 Difficulty | same |
| 6.4 Score award | same |
| 6.5 Enemy health bar | same |
| 6.6 Invulnerability | same |
| 7 The player | changed |
| 7.1 Player records | changed: 0x164 bytes, new fields |
| 7.2 Input to `p_action` | changed: acceleration vector, relative mouse control, no screenshot key |
| 7.3 Movement | changed: `GetPlayerAccel`, friction along the velocity, `speed` factor |
| 7.4 Spawn, lives and respawn | changed: no lives test in spawn, health from the definition clamped to it, 10 life icons, no downgrade on death, upgrades not reset at level start |
| 7.5 Two-player mode | changed in details (statistics hidden, new builtins); much not verified in play |
| 7.6 Helicopter choice | changed: 6 helicopters, table order, unlock bound |
| 8 Weapons and items | changed |
| 8.1 Weapon definitions and `Shoot` | changed: dead shooters cannot fire |
| 8.2 Weapon upgrades | changed: 9 slots, loadout table |
| 8.3 Missiles and power-ups | changed: kinds, cycling skips 6/7/9 |
| 8.4 Targeting | same |
| 9 Scrolling and camera | same except 9.6 and 9.7 |
| 9.1 The camera structure | same |
| 9.2 Scrolling | same (`-campos` debug option) |
| 9.3 Camera modes and placement | same |
| 9.4 CameraQuake | same |
| 9.5 Screen and world | same |
| 9.6 Intermission levels | changed: all objects spawned at load |
| 9.7 Activation handover | changed: first call at load with the +800 edge |
| 10 Level flow | changed |
| 10.1 Level list | same format; 18 missions |
| 10.2 Level start | changed: comic, skid pool, load-time spawn and entity pass, start dialogue |
| 10.3 End of level | changed: checkpoint, dialogue before the mission-complete screen, new buttons, `G_NewGame` |
| 10.4 Scoring and rank | changed: statistics only in one-player mode, zero guard, new-helicopter line; formulas same |
| 10.5 Save file | changed: 6 helicopters, 18 missions, checkpoint block |
| 10.6 Unlocking | changed: modulo 18, helicopter table order |
| 11 HUD and menus | changed (frontend package for the drawing) |
| 11.1 Coordinates | same |
| 11.2 One-player HUD | changed: health clamp, bar scale, 10 lives; art not checked |
| 11.3 Tutorial hints | same |
| 11.4 Menu tree | changed (not detailed here) |
| 12 Sound | not checked (volume keys and game-over music jump same) |
| 13 Rendering order and material state | not checked (skid-trail pass placed, near/far same) |
| 13.1 Frame render order | not checked (see 13) |
| 13.2 Material state | not checked |
| 14 Cheats and debug features | changed: new code words and effects, no screenshot, debug switches |
| 15 Constants and limits | changed (table above) |
| Open questions (base) | 1 settled by issue 120 (holds for `as2`); 2 not checked; 3 not decoded for `as2`; 4 model package; 5 same code in `as2`; 6 renderer; 7 not checked; 8 not checked |

---

## What an implementer must change

Against the engine as it runs `as3d`, ordered by how much of `as2` play depends on it:

1. **Player records and movement**: add the acceleration vector (7.2 step 1) and
   `GetPlayerAccel`; take the relative mouse input (length 2.0, player 1, only when no key);
   field 23 from the definition's `speed`; clamp the players' health to their maximum health
   every frame; spawn helicopters from the `as2` table in the order of 7.6; no lives test in
   `G_SpawnPlayer`.
2. **Weapons**: 9 upgrade slots; apply the mission loadout table on new game and on both
   restarts, not at level start and not on "Next"; power-up cycling skips 6, 7, 9.
3. **Touch pass**: modes as bit sets (`TOUCH_ALL` = 0xF), class 5.0 for bit 0x4, skip
   candidates whose field 4 is set; the same bit rules in particle damage.
4. **Civilians**: class 5.0 from `civilian`; excluded from enemy totals, kill credit, lock-on,
   `Lightning`, health bars; included where 3.1.1 says.
5. **Campaign checkpoint and level flow**: `EndLevel` writes the checkpoint and goes through
   the end dialogue to the mission-complete screen; `G_NewGame` restores or resets; unlock
   modulo 18; the buttons Quit, Restart, Choose Helicopter, Next; save the checkpoint unless a
   cheat was used.
6. **Water**: `FL_ONWATER` follows `G_WaterHeight`; `FL_ONWATER_NORMAL` aligns to the water
   plane; `FL_ONWATER_FLAT` uses the flat level; `G_WaterHeight` falls back to the terrain when
   the level has no water.
7. **Level start**: one spawner call, one entity pass and one render inside the level load,
   before the counters are reset (or the documented alternative of issue 211); kill counter
   capped at the enemy total; statistics drawn only in one-player mode.
8. **`Shoot`** refuses dead shooters.
9. **Skid marks**: parse `skid_mark`, keep up to 64 trails, update them after the entity pass
   (also while paused), draw them after the decals.
10. **Intermission levels** spawn all their objects at load.
11. **Entity layout**: nothing to change for scripts (field indices 0..87 unchanged); the
    engine-internal fields 88/89 hold the skid trails; the +0x78 timer serves `Lightning`.
12. **Cheats**: the seven new code words; drop the v1.70 ones for `as2`.
13. HUD: up to 10 life icons, health bar scaled by the maximum health (frontend package).

---

## Open questions

1. **Two-player play** (medium): only read from the code; the HUD, the camera with one player
   out of lives for long, and the checkpoint with two players were not seen running.
2. **Load-time spawn and statistics** (medium): whether to reproduce that rows 0..20 are not
   counted in the enemy total and the maximum score; issue 211.
3. **Skid-trail quirks** (low): stale age of a new node, ageing while paused, the node written
   past the visible end of a full trail; issue 210.
4. **Portrait dialogues** (low for the rules): how the end dialogue hands over to
   `G_MissionComplete` (its callbacks as2@0x4231b0 and 0x423050 lie inside another function in
   the export); the frontend package should read them.
5. **Non-model attach parents** (low): 4.4; unreachable from the data.
6. **Water grid bounds** (low): `G_WaterHeight` tests x and y against the grid size × 40 but
   indexes with the terrain cell size; equal in practice (40), not verified for every map.
7. **Mission-complete "Choose Helicopter"**: which action the " Accept " button runs
   (`M_HeliSelectAction` case 1 starts a new game on the current mission index); frontend
   package.

---

## Corrections to other specs

Listed here, not applied.

| Spec, place | Says | Correct | Evidence |
|---|---|---|---|
| symbol-map.md "New functions", `re/symbols_as2.csv` 0x00414850 | `G_UpdatePlayers`, "per-frame update that uses terrain heights and distances" | `G_UpdateSkidTrails`: updates the skid-mark trails (3.1.2); it touches no player record | its only data are the trail pool as2@0x20c5d9c/0x20c5da0 and the draw list as2@0x21a7bc8/0x2113074; constants 10.0, 5/12, 23, 2.0 |
| same, 0x00414800 | `G_LevelEndHelper` | `G_FreeAllSkidTrails` (returns every live trail to the pool; called by `G_FreeLevel` as2@0x40e660) | as2@0x414800 |
| same, 0x004138f0 | `G_ResetPlayers`, "resets the per-player state when a game or level starts" | `G_SetMissionLoadout`: sets the 9 upgrade slots and `p_weapon` of both players from the table as2@0x48b3a8 row (mission index clamped to 0..17) | as2@0x4138f0 |
| same, 0x00430280 / 0x004300e0 | `R_DrawMarks` (counterpart of v170@0x40d860) / `R_DrawMark` | `R_DrawSkidTrails` / `R_DrawSkidTrail` (new, draw the trail list); the counterpart of the v1.70 decal pass v170@0x40d860 is as2@0x42ff70 (`R_DrawGroundPass` in the map), which draws the decal list filled by `R_AddEntityToScene` | as2@0x430280 reads the trail draw list; as2@0x42ff70 reads the decal count as2@0x2112a5c, called between the frustum set-up and the trails in `R_RenderView` |
| symbol-map.md "Removed", `G_RunCollisions` v170@0x4055d0 | "inlined into G_RunEntities" | still a separate body at as2@0x40c560..0x40c747, entered by a tail jump from `G_RunEntities` (as2@0x40cee7); the export merges it into that function | as2@0x40cee7 |
| symbol-map.md "undecided", `G_NewCampaign` v170@0x408c40 | no decision | counterpart `G_NewGame` as2@0x410dc0 (adds the checkpoint restore) | 10.3 |
| `re/symbols_as2_data.csv` 0x0049ddf8 (v170 0x00457660) | `g_missionUnlockTable`, "mission unlock table" | the **helicopter** table {u8 unlocked, name[32]} × 6 (engine-behaviour.md 7.6 for v1.70) | `G_SpawnPlayer` as2@0x413b20 reads object names from it; `G_MissionComplete` unlocks `enableHelic` entries |
| `re/symbols_as2_vm.csv` 0x00410cd0 | `G_StartLevelState` | `G_BeginLevel` (as in `re/symbols_as2.csv`; counterpart of v170@0x408b30) | same function |
| rcsl-vm.delta.md, entity update | the +0x78 timer "that `Lightning` resets" | confirmed, and its meaning: seconds since `Lightning` last spawned `wavegun_hit` on the entity (at most every 0.2 s) | as2@0x42144e..0x42146b |
| rcsl-vm.delta.md, fields 88 and 89 | "new engine-internal dwords" | skid-trail count and trail array | 3.2 |
| issue 113 §1 | "The specs do not say whether the count is decremented on that detach" | the original decrements the root's reference count when a counted child is detached for a missing tag, in both games; the WP-49 choice is the original behaviour | v170@0x404879..0x404891, as2@0x40b75e..0x40b778 |
| issue 031 §6 | score digits for a zero award: a choice | the original creates nothing and adds nothing for an award of 0 (both games) | v170@0x40bb20, as2@0x414350: the whole body is guarded by `award ≠ 0` |
| engine-behaviour.md 7.3, 7.5, 9.3 | "each living player is clamped", "mean x of the living ones" | "living" means p_lives ≥ 0 (not health > 0), in both games | v170@0x40c454..0x40c49b tests record +0x98; as2@0x414e90 tests +0x9C |

---

## Symbols

`re/symbols_as2_game.csv` holds the functions and data named or corrected by this package
(columns `address,name,subsystem,description,confidence,evidence,v170_address`; rows that
correct `re/symbols_as2.csv` or `re/symbols_as2_data.csv` say so in the description).

---

## Changelog

- 1.0 (B4): first version.
