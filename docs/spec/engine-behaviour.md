# Native game logic (engine behaviour)

Spec version 0.1 (WP-24). Source: `AirStrike3D.exe` v1.70 (image base 0x400000), read through
the Ghidra export in `re/out/v170/`; the function names used below are listed in
`re/symbols_v170.csv`. The shipped data in `assets_extracted/` and the manual were used as a
cross-check.

Companions: [rcsl-container.md](rcsl-container.md), [rcsl-opcodes-v0.md](rcsl-opcodes-v0.md)
(script format and VM), [text-blocks.md](text-blocks.md) (definition file syntax),
[mdl.md](mdl.md) (models). The script VM runtime and the builtin signatures (rcsl-vm.md,
rcsl-builtins-table.md), the level file format (hmap.md), the typed definition loaders and
the detailed renderer (WP-25) belong to other packages. Where this document touches them it
records only what the native game loop needs, and marks the boundary.

Tags: `VERIFIED-CODE` (address of the function or instruction), `VERIFIED-DATA` (checked on
the shipped files), `GUESS`. Addresses are virtual addresses. "ftol" means truncation toward
zero (the executable converts every float to int through the helper at 0x440870).

Units: world units (1 map cell = 40 units), seconds, degrees, and screen pixels of a virtual
800×600 screen unless stated otherwise. World axes: +x to the right of the map (0..1280),
+y in the scroll direction (forward), +z up.

---

## 1. Program structure

### 1.1 Start-up (VERIFIED-CODE 0x4207f0 WinMain)

The heuristic "WinMain at 0x421df0" in `re/out/v170/SUMMARY.md` is wrong: 0x421df0 is the
message pump (`MW_PumpMessages`). WinMain is 0x4207f0. It performs these steps in order:

1. Sets the current directory to the executable's directory.
2. Requires `.\play.url` to exist. If it is missing, shows "Some files are corrupt…" and exits.
3. Splits the command line into at most 50 arguments. Only `-setup` and `-nosound` are
   checked, by `CFG_Init` and `S_Init`.
4. Loads `Settings.xml` (0x41eee0). On failure: "Can not load game settings." and exit.
5. Sets `time` = 0 and the level clock = 0.
6. Calls `Sys_Init` (0x420620). It does, in order:
   1. Seeds rand with the low 16 bits of timeGetTime.
   2. Fills a 450-entry float table (GUESS: sines).
   3. `G_LoadBin`.
   4. Truncates `game.log`.
   5. `F_Init`.
   6. `CFG_Init`: reads `config.ini`, and shows the setup dialog when `-setup` is given or
      `FirstRun` ≠ 0.
   7. `MW_Init` (window and GL).
   8. `S_Init`.
   9. Reserves 2048 texture names.
   10. Resets the particle pool.
   11. `timeBeginPeriod(1)`.
7. Calls `G_Init(0)` (0x408cc0).

### 1.2 Main loop and timing (VERIFIED-CODE 0x420970..0x4209c0)

The loop never exits normally; quitting goes through WM_CLOSE → `Sys_Quit`. Each iteration:

1. `dt_ms = timeGetTime() − previous` (unsigned 32-bit), then `dt_ms = min(dt_ms, 100)`.
2. `frametime = dt_ms × 0.001` s (global 0x1fb4bcc, script global `frametime`).
3. `time += frametime` (0x1fb4bd0, script global `time`). The level clock (0x1fb4a3c) also
   advances by frametime; it is reset to 0 at level load and drives the level-name
   typewriter.
4. `MW_PumpMessages` (0x421df0): drains the Windows queue. Keyboard, mouse and character
   input arrive through the window procedure here.
5. `G_Frame` (0x408df0): the whole game update and rendering (§2).
6. `Joy_Frame` (0x422bd0): polls the joystick and turns it into key events. These take effect
   in the next frame.
7. `S_Update` (0x41fc30): 3D sound listener and channel positions.

Consequences:

- The timestep is variable. There is no fixed step, no minimum delta (frametime can be 0)
  and no frame limiter, apart from the WaitVSync setting. The maximum delta is 0.1 s, so
  the game slows down below 10 fps.
- `frametime` is written only here (VERIFIED-CODE: the only store to 0x1fb4bcc is at
  0x42099f). No native time scaling exists.
- **Pause** (flag 0x458eab): `G_Frame` subtracts frametime back from `time` and from the level
  clock, so both stand still. `frametime` itself keeps its non-zero value.
  - Script `main` threads are not resumed and entity ages are not advanced (0x405a60).
  - Particles are not updated.
  - The camera and scroll are frozen (0x40c260 does nothing while paused).
  - Rendering continues.

### 1.3 Game states (VERIFIED-CODE unless marked)

There is no single state variable. The state is a combination of flags:

| Flag | Address | Meaning when set |
|---|---|---|
| game ready | 0x1fdb2c6 | `G_Init` done; `G_Frame` does nothing before this |
| intro running | 0x458c97 | the logo sequence is shown instead of the game |
| paused | 0x458eab | world frozen (see 1.2) |
| game over | 0x4d52d4 | player logic stops |
| intermission level | 0x4d52d5 | attract/cutscene level: no players, no HUD, fixed camera, no player logic |
| HUD hidden | 0x4d52d6 | an overlay menu is up |
| two players | 0x1fdb2c7 | two-player mode; player count at 0x4577b8 (1 or 2) |
| god mode | 0x1fdb2c4 | cheat |
| cheat used | 0x1fdb2d0 | rank becomes "Cheater", score posting disabled |

The menu layer is a stack of menus (depth 16, `UI_PushMenu` 0x4292f0 / `UI_PopMenu`
0x429360). It is drawn over whatever level is loaded; there is no separate "menu" screen.

The states, what they show, and how they are left:

1. **Boot (`G_Init` 0x408cc0).**
   - Picks one of the attract levels `intro1..intro4`. Draw r = ftol(rand/32767 × 4); if
     r < 3, redraw once and use the second draw, otherwise use 3.
   - Player 1's helicopter = 1 (p_comanche), player 2's = 0 (p_apache).
   - Builds the key-to-action table, loads UI textures and the font, builds and pushes the
     main menu.
   - Loads `levels.txt`, all objects, the `score_num` definition and all weapons.
   - If `ShowLogo` = 1, starts the intro pages; otherwise goes straight to the attract level.
2. **Intro logos (0x408070 / 0x408260).**
   - The pages come from Settings.xml `<Intros>`: a built-in "DivoGames" page and image pages.
   - Brightness is forced to 0.5 during the intro.
   - Each page's clock starts at −0.5 s and advances at frametime × speed. Any key or left
     click multiplies speed by 4 (cumulative).
   - An image page lasts 6 s, fading in during the first second and out during the last.
   - The DivoGames page lasts 8 s: `gfx\logo.tga` grows during 0–2 s, text appears during
     2–4 s, fade-out after 7 s.
   - At the end, it continues with the attract level.
3. **Attract level with main menu (0x408b30 on an intermission level).**
   - The cutscene level plays behind the menu, with the camera from the level's
     `intermission` line (§9.6).
   - Main menu items: Start Game, Top Scores, Options, Information, Exit (with a yes/no
     confirmation).
4. **Start Game menu (0x42b710).**
   - Mission list (unlocked missions only), difficulty spinner (default Normal), 1 or 2
     players, helicopter grid (5×2).
   - Start (code at 0x42b3b0): stores the difficulty, frees the attract level, pops all
     menus, sets the player count and mission index, then runs "new campaign" (0x408c40).
   - New campaign: clears the banked score and rank accumulator, sets lives-at-level-start
     to 2, then starts the level.
5. **Playing.** No flag set.
6. **Paused.** The P or Pause key toggles the paused flag without a menu. Unpausing clears
   p_action of both players (0x408b10).
7. **In-game menu (Esc, 0x428fa0).**
   - Sets HUD-hidden and paused.
   - Resume clears both flags and p_action. Esc or right click also resumes.
   - Options.
   - Quit: frees the level, loads the attract level and shows the main menu. No high-score
     check.
8. **Game over.**
   - Trigger (`G_PlayerFrame` 0x40b440): single player when p_lives < 0; two players when
     both p_lives < 0.
   - Effect (`G_GameOver` 0x408c80): sets game over, HUD hidden and paused; the music jumps
     to its game-over section (§12); pushes the game-over menu.
   - Restart: reloads the same mission with the lives it started with.
   - Quit: banks the score, then attract level, main menu and high-score check.
9. **Mission complete.** The `EndLevel` builtin (0x407570) sets HUD hidden and paused, then
   calls `G_MissionComplete` (0x4269b0). This:
   - unlocks the helicopter named by the level's `enableHelic` (0..9) and the mission
     (index + 1) mod 20;
   - shows the mission-complete screen with statistics (§10.4);
   - offers three buttons:
     - Continue: bank the score and start mission index + 1.
     - Restart: replay the mission.
     - Quit: attract level and main menu, with no banking and no high-score check (as coded).
   - After mission index 19 (the last), a "Congratulations!" screen replaces it. Its
     Continue banks the score, returns to the menu and runs the high-score check.
10. **High scores.** Checked in single-player mode only (0x40bde0). If the banked score beats
    one of the 15 entries, the name-entry dialog opens, the entry is inserted (0x40be90) and
    the Top Scores menu is shown.
11. **Exit.** Frees the level, saves `game.bin` (0x408dd0), writes `config.ini` and quits.
    Settings.xml may request opening a URL at exit.

`G_Frame` (0x408df0) dispatches as follows: nothing before game-ready; the intro frame while
the intro runs; otherwise the playing frame of §2. Menus are drawn inside the playing frame
(§2, step 11), so pause, game-over and mission-complete screens are all drawn over the frozen
level.

---

## 2. Per-frame update order (VERIFIED-CODE 0x408df0)

Order within one `G_Frame` while a level is loaded. Input from the window procedure has
already been applied to `p_action` during the message pump (1.2, step 4).

1. **Pause time correction.** If paused, `time` and the level clock are rolled back by
   frametime.
2. **`R_BeginFrame` (0x40e690).** Clears the per-frame render lists and counters, then clears
   the depth and colour buffers.
3. **`V_UpdateCamera` (0x40c260).** Skipped while paused. It:
   - advances the scroll (`g_map_pos`);
   - clamps the player(s) to the side frustum planes, using the planes from the previous
     frame;
   - moves the camera and applies the quake;
   - updates the activation window (§9).
4. **`G_ActivateMapObjects` (0x407590).** Instantiates the placed map objects whose row has
   entered the activation window. Each new entity runs its `init` handler here (§3.6).
5. **`G_PlayerFrame` (0x40b440).** Skipped on intermission levels and after game over. It:
   - handles the switch-weapon, switch-missile and switch-power-up edges;
   - checks for game over;
   - in mouse-control mode, turns the mouse position into movement bits (§7).
6. **`G_RunEntities` (0x405cc0):**
   1. Frees every entity that was removed during the previous frame and whose attachment
      reference count is below 1.
   2. Runs the state machine and think of every pool entity, newest first (§3.4, §3.5). A
      think does the following, in order:
      - age and damage-timer update;
      - resumes the script `main` thread;
      - attaches to the parent's tag;
      - sets the transform and snaps to the ground;
      - computes the screen-space collision rectangle;
      - queues the entity for rendering and adds its light;
      - draws its health bar;
      - recursively thinks its children.
   3. `G_RunCollisions` (0x4055d0): touch tests and touch handlers for every root entity, in
      list order, then the two-player push-apart (§5).
7. **Particles.** Unless paused, `P_UpdateEmitter` (0x414600) runs for every emitter in the
   emitter list. It emits, integrates and fades particles, and applies particle damage (§6.4).
8. **`HUD_Frame` (0x4041b0).** Skipped on intermission levels and while the HUD is hidden.
   Draws the level-name typewriter, the HUD, the cheat message and the mouse-control cursor.
9. **`SCR_DrawFade` (0x404370).** A full-screen fade quad. It never does anything, because
   its timer is never set anywhere (VERIFIED-CODE).
10. **`R_RenderView` (0x40e0d0).** Renders the 3D scene, then the queued 2D items (§13).
11. **`UI_Frame` (0x4299f0).** Draws and updates the menu on top of the stack, and the cursor.
12. **`R_EndFrame` (0x40e6f0).** Brightness quad, glFlush, SwapBuffers.

After `G_Frame`, WinMain polls the joystick and updates 3D sound (1.2).

Determinism notes for a reimplementation:

- Scripts run inside the entity update (6.2), before collisions (6.3). Touch handlers
  therefore see the positions scripts set in the same frame.
- Collision uses the screen projection computed from the camera matrices of the previous
  render (§5.1). The camera itself moved at step 3 of this frame.
- Entities created during 6.2 (by `create` or `Shoot`) get one immediate think inside the
  creating builtin. They are not visited again by the loop that frame, because the loop walks
  from newest to oldest and they are inserted at the newest end.
- The random number generator is the C runtime `rand()`, seeded from timeGetTime. The original
  is not deterministic across runs.

---

## 3. Entities

### 3.1 Object definitions (VERIFIED-CODE `ParseObject` 0x40a160, `G_FindObject` 0x409860)

- Up to 2048 definitions, stored 1-based. More gives the fatal error "ParseObject: Too many
  objects in object files."
- Lookup by name is linear; the **first** definition with a given name wins. This resolves the
  duplicate `tank_dead` question left open in text-blocks.md: the line-41 definition is used.
- An unknown key logs "Unknown object parameter".
- On first use, each definition's model, skin, envmap, script and attached objects are
  precached (`G_PrecacheObject` 0x4098d0).

Keyword values (VERIFIED-CODE 0x40a160):

| Key | Values (numeric) | Notes |
|---|---|---|
| `type` | TYPE_MODEL 0, TYPE_SPRITE 1, TYPE_MARK 2, TYPE_HSPRITE 3, TYPE_VSPRITE 4 | Any type other than MODEL also sets flag 0x1000 (point collision). |
| `flag` (OR) | FL_ONGROUND 1, FL_ONGROUND_NORMAL 3, FL_ONWATER 4, FL_NODRAW 0x10, FL_TEMPORARY 0x20, FL_NONTARGET 0x100, FL_POINT_COLLISION 0x1000 | FL_ONGROUND_NORMAL includes FL_ONGROUND. |
| `touch` | TOUCH_ENEMIES 1, TOUCH_PLAYER 2, TOUCH_ALL 3 | |
| `sort` | SORT_OPAQUE 0, SORT_TRANS 2, SORT_EFFECT 3 | Value 1 is never produced. |
| `blend` | BLEND_ALPHA 1, BLEND_ADD 2, BLEND_FILTER 3 | 0 means no blending. |
| `envmode` | ENV_GLITTER 1, ENV_CHROME 2, ENV_QUAD 3 | `envmap` also sets render flag 0x20. |
| `rflag` (OR) | RF_NOLIGHTING 1, RF_NOCULLING 2, RF_NODEPTHTEST 4, RF_NODEPTHWRITE 8, RF_NODLIGHT 0x200, RF_BANNER 0x10000 | Internal bits: 0x20 envmap, 0x1000 lightning, 0x4000 beam. |
| `shadow` | SHADOW_PROJECTED 1 / _LOW 2 / _HIGH 3, SHADOW_PLANAR 4 / _LOW 5 / _HIGH 6, SHADOW_PLANAR_PROJECTED 7 / _LOW 8 / _HIGH 9 | 4–6 are remapped to 7–9 after parsing, so PLANAR behaves as PLANAR_PROJECTED. |
| `player`, `enemy` | class 1 and class 2 | `item` class 3 exists in code but is unused in the data. |
| `health`, `damage`, `score` | floats | Copied into script fields 34, 35 and 36. |
| `bbox_scale` | 3 floats | Default 0.7, 0.7, 0.7. Scales the model box used for collision. |
| `min`, `max` | 4 numbers each | Sprite extent and UVs. |
| `attach` | at most 64 per definition; extra ones are silently ignored | Modifiers `abs`, `night`, `id <name>` in any order, then object (or particle system) name and tag name. |

The class value (field 2) is what native code tests. 2.0 means "enemy": it can be damaged
by `RadialDamage`, `TraceLineDamage` and `Lightning`, counts toward kills and enemy totals,
can be locked on, and is a candidate for TOUCH_ENEMIES. Projectiles made by `Shoot` get
class 4.0. The `enemy` keyword therefore marks every target the player can shoot, not only
hostile units (VERIFIED-CODE 0x404ae0, 0x41b5d0, 0x40c020, 0x4051d0).

### 3.2 Entity memory layout (VERIFIED-CODE, `G_InitObject` 0x409ba0 and users)

An entity is 0x1e3 bytes. Script field n is the 4-byte value at entity + 0x7b + 4n. The
script globals `self`, `other` and `player` hold entity + 0x7b (a "handle"). Field 0 holds the
entity base pointer, so builtins recover the entity from a handle with one extra
indirection.

Native (non-script) part:

| Offset | Type | Meaning |
|---|---|---|
| +0x00 / +0x04 | ptr | next / previous in the active list (+0x04 is also the free-list link) |
| +0x08 | ptr | parent entity (0 for roots) |
| +0x0c | ptr | parent tag name |
| +0x10 | u8 | `abs` attach flag |
| +0x11 | u8 | counted in the root's reference count |
| +0x12 | i32 | number of pool entities attached below this root; a removed root is freed only when this is below 1 |
| +0x16 | ptr | name: the definition name for roots, the attach `id` for children (matched by AttachActivate etc.) |
| +0x1a | i32 | activation state 0 / 1 / 2 (3.5) |
| +0x1e | u32 | runtime flags (table below) |
| +0x22 | i32 | drop: the object definition to spawn at death (from the map record) |
| +0x26..+0x43 | | light parameters from the definition |
| +0x47 | i32 | shadow type |
| +0x4b / +0x4f / +0x53 | | waypoint path pointer / distance travelled / last node index |
| +0x57 | ptr | script thread |
| +0x5b | i32 | touch filter (TOUCH_*) |
| +0x5f..+0x67 | 3 floats | bbox_scale |
| +0x6b | float | maximum health (scaled like health) |
| +0x6f | float | seconds since the last damage; set to 2.0 after init |
| +0x73 | i32 | looping sound channel |
| +0x77 | i32 | player index (0 or 1). Selects which player record `player` and the `p_*` globals refer to while this entity's script runs. This is an entity field; rcsl-container.md calls it a definition field. |
| +0x7b | | script fields (next table) |
| +0x153 / +0x157 | | model handle / skin |
| +0x15b / +0x15f / +0x163 | | blend / envmap / envmode |
| +0x167..+0x173 | 4 floats | render colour (copied from fields 28–31) |
| +0x177..+0x1a3 | | sprite min(x, y, z, u, v), max(…), frames[2] |
| +0x1a7 | i32 | sprite frame (ftol of field 33) |
| +0x1ab / +0x1b7 | float | sprite rotation / yaw copy |
| +0x1af / +0x1b3 | | shadow handle / shadow display list |
| +0x1bb | ptr | particle emitter; when set, the entity only carries an emitter |
| +0x1bf | float | bounding radius (from the model, or the sprite extent) |
| +0x1c3..+0x1d7 | 6 floats | screen-space collision rectangle: min x, y, depth; max x, y, depth (§5.1) |
| +0x1db / +0x1df | | child count / child array |

Runtime flags at +0x1e:

| Bit | Meaning |
|---|---|
| 0x01 | removed (free deferred) |
| 0x02 | already thought this frame |
| 0x04 | script `main` active. Set at init, by `activate` and by AttachActivate; cleared by `deactivate`. |
| 0x08 | collidable this frame: on screen (§5.1). Inherited from the parent for attached children. Required by Shoot, TraceLine, LockTarget and the touch tests. |
| 0x10 | health frozen (`FreezeHealth`) |
| 0x20 | attached with `AttachEntity`: gets its own collision rectangle although it has a parent |
| 0x100 | chosen by LockTarget |

Script fields (VERIFIED-CODE unless marked):

| Field | Offset | Meaning |
|---|---|---|
| 0 | +0x7b | entity base pointer |
| 1 | +0x7f | age in seconds (+= frametime per unpaused think) |
| 2 | +0x83 | class (1 player, 2 enemy, 3 item, 4 projectile), float |
| 3 | +0x87 | FL flags, float. Scripts change them with SetFlag/ClearFlag. Extra runtime-only bits: 0x2000 and 0x4000 mark projectiles of player 1 and player 2 in two-player mode. |
| 4 | +0x8b | dead: set to 1.0 when health reaches ≤ 0; further damage is ignored |
| 5..7 | +0x8f | origin x, y, z |
| 8..10 | +0x9b | offset from the attachment tag |
| 11..13 | +0xa7 | origin at the end of the previous think (GUESS: used for swept tests and by Shoot) |
| 14..16 | +0xb3 | angles x, y, z in degrees. Their use in the axis formula is given in §4. |
| 17..19 | +0xbf | velocity. Native code never integrates it (§4); scripts do. |
| 20..22 | +0xcb | no native use (script scratch) |
| 23 | +0xd7 | path speed (units/s) |
| 24 | +0xdb | turn rate (degrees/s) for RotateTo and RotateToNextWP |
| 25 | +0xdf | path bank factor |
| 26, 27 | | no native use |
| 28..31 | +0xeb | colour RGBA, initialised to 1.0 |
| 32 | +0xfb | scale, copied to the renderer every frame; above 0.01 it also scales tag offsets |
| 33 | +0xff | sprite frame; score digits store their digit here |
| 34 | +0x103 | health |
| 35 | +0x107 | damage (from the definition; scripts pass it to `Damage`) |
| 36 | +0x10b | score value |
| 37 | +0x10f | current waypoint delay |
| 38 / 39 / 40 | +0x113 / +0x117 / +0x11b | type / rflags / sort |
| 41..43 | +0x11f | base origin: world position after attachment |
| 44..52 | +0x12b | 3×3 axis, rows forward, left, up |

VERIFIED-DATA: over the 339 scripts, fields used through `self` are 1, 4, 5, 8, 14, 17, 20,
23, 24, 25, 28, 32, 33, 34, 35 and 37. Through `player` they are 4, 5, 14 and 34. Through
`camera` they are 6 and 9 (camera fields are a different structure, §9.1).

### 3.3 Pool and lists (VERIFIED-CODE)

**Pool.**
- 1024 entities of 0x1e3 bytes (array at 0x458eb0), rebuilt at every level load (0x407080).
- Free list linked through +0x04, head at 0x458cc4.
- Live count at 0x1fdb2c8.

**Allocation (`G_AllocEntity` 0x404530).**
- Takes the head of the free list, zeroes it, and inserts it at the newest end of a circular
  doubly linked list. The sentinel is 0x458cc8; 0x458ccc points to the newest entity.
- There is no check for exhaustion: the original crashes beyond 1024. A reimplementation
  should fail gracefully (GUESS: refuse the spawn).

**Iteration.**
- Every pass walks from the newest entity to the oldest.

**Children.**
- Attach children and emitter holders are allocated outside the pool (heap).
- They are not in the list. Their parent updates, draws and frees them recursively.

**Removal (`G_RemoveEntity` 0x404410, builtin `remove`).**
- Marks the entity and all its children with bit 0x01, stops its emitter, deletes its shadow
  display list and decrements the root's reference count.
- The memory is released at the start of the next `G_RunEntities` (0x405cc0, first pass).
  `G_FreeEntity` (0x404590) unlinks the entity, frees the thread and child tree, and returns
  it to the free list.
- A removed entity is skipped by every later test in the same frame.

### 3.4 Creating entities

**From the map (`G_ActivateMapObjects` 0x407590, every playing frame).**

Map records are sorted by row at load time. A cursor walks them:
- while the cursor's row ≤ (g_map_pos + 1000) / 40, the record is consumed;
- if its row ≥ (g_map_pos − 64) / 40 it is spawned, otherwise it is skipped for good.

For each spawned record:
1. Position = (column × 40 + 20, row × 40 − 20, 0).
   Height: terrain height with FL_ONGROUND, `l_waterlevel` with FL_ONWATER.
2. A per-placement script override replaces the definition's script.
3. Player index (+0x77) = 0 in single-player mode. In two-player mode it is chosen at random
   between the living players, or is the survivor if one player is dead.
4. Projected shadows are baked.
5. Yaw (field 16) = record yaw step × 30°. With a path: path pointer set, yaw = the path's
   first segment yaw, field 37 = the first node's delay.
6. The `init` handler runs (0x404fa0, recursively on children, which inherit +0x77).
7. The drop is copied into +0x22.
8. State = 1 (dormant) unless FL_TEMPORARY, in which case state = 0.
9. Health and maximum health ×= g_health_factor.
10. score = 10 × ftol(score × score_factor / 10) (§6.3).
11. Enemies-in-level (0x4d52f8) += 1 if class = 2 and not FL_NONTARGET.
12. Maximum level score (0x4d52fc) += score.

The on-disk record layout and the path format belong to the level package (hmap.md). The
in-memory map record (0x1f bytes) has: +0 u16 definition index, +2 u16 column, +4 u16 row,
+6 u8 yaw step, +7 i32 drop definition, +0xb i32 script override (−1 none), +0xf path pointer,
+0x1b shadow.

**From scripts (`create(name, position*)`, 0x41a7a0).**
1. Name lookup: the script's CASH entries of kind 0 first, then the definition table.
2. Spawn at the position vector passed in t1, with ground and water snapping.
3. Copies the creator's angles (fields 14–16) and player index.
4. Runs `init` and bakes the shadow.
5. Counts the entity as an enemy if applicable.
6. Runs one think immediately.
7. Returns the handle.

Health and score of created entities are **not** scaled by difficulty.

**Projectiles (`Shoot`, §8.1).** Created the same way as by `create`.

### 3.5 Activation states (VERIFIED-CODE `G_RunEntities` 0x405cc0, `G_InActivationArea` 0x4050b0)

- **State 1, dormant.** The entity is positioned, drawn and collidable, but its `main` does
  not run. It switches to 0, recursively, once it is inside the activation area.
- **State 0, active.**
  - FL_TEMPORARY entities and everything made by `create` or Shoot start here.
  - An entity without FL_TEMPORARY that leaves the activation area goes to state 2.
  - FL_TEMPORARY entities never leave by area; their scripts remove them.
- **State 2, leaving.** The entity keeps running while its bounding sphere is inside the
  view frustum and inside the bounding box of the visible terrain. It is removed when either
  test fails.

The activation area is satisfied when all of these hold (r = bounding radius, x, y, z =
origin):

- y − r ≥ g_map_pos + 16
- y − r ≤ g_map_pos + 800
- x + r ≥ 0 and x − r ≤ 1280
- z + r ≥ the level's `hmin`

(These are the camera window edges g_map_pos − 64 and g_map_pos + 1000, moved in by 80 and
200 units.)

`activate` / `deactivate` (0x404a40 / 0x404a90) only set or clear runtime bit 0x04 and the
emitter's enable flag. A deactivated entity still renders and collides. A finished,
non-looping path deactivates its entity in `MoveToNextWP`.

### 3.6 Event entry points

| Entry point | Native trigger |
|---|---|
| `init` | Entity creation: map spawn step 6, `create`, `Shoot`, player spawn. Parent first, then children. `G_RunInitHandler` 0x404fa0. |
| `main` | Resumed in each think when runtime bit 0x04 is set, the state is not 1 and the game is not paused (0x405a60). A child's main does not run while its parent is dormant. |
| `damage` | Inside `G_Damage` after health is reduced (§6.1). |
| `touch` | Inside the collision pass, with `other` set (§5.3). |
| `callback` | Builtins `callback`, `AttachCallback` and `ParentCallback` (cb_msg, cb_parm1, cb_parm2). Native code also sends one when a weapon is first picked up (§8.3). |

While any handler runs, `self` = that entity's handle. `player` and the `p_*` globals are
resolved through that entity's +0x77 (rcsl-container.md, DEFS).

---

## 4. Movement and transforms (VERIFIED-CODE)

### 4.1 Think order

`G_ThinkEntity` (0x405a60) runs once per entity per frame (runtime bit 0x02). For an
attached entity the parent is thought first. Steps:

1. Unless paused: age += frametime, time-since-damage += frametime, resume `main` (3.6).
2. With a parent: inherit runtime bit 0x08 from the parent, then `G_AttachToTag` (0x404850).
3. `G_SetupTransform` (0x4057c0).
4. Roots, and children attached with AttachEntity: `G_ComputeScreenBounds` (0x405930).
5. Emitter holders: move the emitter to the entity and stop here.
6. Unless FL_NODRAW: queue for rendering (`R_AddEntity` 0x40de50) and add the definition's
   light.
7. Health bar (§6.5).
8. Think every child.

### 4.2 Position and velocity

- There is **no native integration of velocity, no gravity and no friction**. Fields 17..19
  are plain storage.
  - Scripts move entities with `move`/`movex`/`movey`/`movez` (origin += v × frametime) and
    turn them with `rotate*` (angles += v × frametime). They implement their own
    velocity and gravity.
  - The only native writers of velocity are Shoot (initial velocity) and the two-player
    push-apart (§5.4).
- **Ground following, every frame (roots only):**
  - FL_ONGROUND: z = TerrainHeight(x, y) (0x416470, bilinear on the 40-unit height grid, 0
    outside the map).
  - FL_ONWATER: z = `l_waterlevel`.
  - FL_ONGROUND_NORMAL (value 3): the axis comes from the terrain normal combined with the
    yaw (0x4165f0).
  - Otherwise the axis comes from the angles.

### 4.3 Axis from angles (`AnglesToAxis` 0x41e390)

With x, y, z = angle fields 14, 15, 16 in degrees (sx = sin x, cx = cos x, and so on):

- forward = (cy·cz, cy·sz, sy)
- left = (sx·sy·cz − cx·sz, sx·sy·sz + cx·cz, −sx·cy)
- up = (−sx·sz − cx·sy·cz, sx·cz − cx·sy·sz, cx·cy)

So field 16 is yaw about +z (0 = facing +x), field 15 is pitch (nose up for positive values)
and field 14 is roll. The script builtins `vec_toyaw` and Shoot subtract 90° because models
face +y (GUESS for the model convention; the model package owns it).

After the transform, the base origin (fields 41–43) and fields 11–13 are set to the origin.

### 4.4 Attachment to tags (`G_AttachToTag` 0x404850)

- The tag is looked up by name in the parent's model (`R_GetTag` 0x410380; the name "origin"
  is special). If it is missing, the child is detached and the log says "Tag '%s' not found
  in model '%s'".
- Without `abs`: child position = parent base + parent axis · tag position + field 8..10
  offset. The offset is not rotated. The child's axis comes from its own angles only.
- With `abs`: child position = parent base + parent axis · (tag position + offset). Child
  axis = own axis × tag axis × parent axis.
- The keyword looks inverted relative to its effect (GUESS about the author's intent; the
  behaviour is VERIFIED-CODE).
- The child's origin (fields 5–7) is overwritten with the result every frame, so scripts
  cannot move attached children except through the offset fields 8–10.
- How the tag's axis is built from the model's tag vectors is the model package's question
  (mdl.md); this package only records that a tag axis is used for `abs` children.

### 4.5 Waypoint paths (0x405dd0 / 0x405fe0 / 0x406110)

- Paths are built at level load by sampling a cubic Bézier through the waypoints every 8
  units (0x4069a0).
- `MoveToNextWP(setYaw)`:
  - distance += field 23 × frametime; node = distance / 8;
  - position (x, y) follows the path, and z comes from ground snapping;
  - with setYaw, yaw = the path tangent;
  - if field 25 ≠ 0, pitch = path curvature × field 25 / 4.8;
  - at each waypoint it sets field 37 to the waypoint's delay and reports "done" (for the
    latent call).
- `RotateToNextWP` turns yaw toward the path direction at field 24 °/s, snapping when within
  one step, and is done below 0.1°.

### 4.6 World bounds

- The map is 1280 units wide (32 cells). Nothing clamps entities to it.
- Leaving the play area is handled only by the activation states (3.5): non-temporary
  entities are removed once they are out of the frustum or the visible terrain.
- Temporary entities (projectiles, effects) must remove themselves. Their scripts do this
  with timers or by comparing y against `g_map_pos` (VERIFIED-DATA).
- The player is bounded by the camera (§7.3).

---

## 5. Collision (VERIFIED-CODE)

### 5.1 Shapes: screen-space rectangles

Collision is **two-dimensional, in screen space**. `G_ComputeScreenBounds` (0x405930) runs
in each think:

- **Class 0 without a touch filter** (scenery): runtime bit 0x08 is set and no rectangle is
  computed.
- **Otherwise**, bit 0x08 is cleared unless the bounding sphere (radius +0x1bf) is inside the
  view frustum (0x419970; planes of the previous render). Then:
  - **TYPE_MODEL without FL_POINT_COLLISION:** the model's bounding box (the MDL header box),
    scaled by `bbox_scale` about its centre (GUESS for the pivot; default factor 0.7), is
    placed with the entity transform. Its 8 corners are projected to window pixels
    (`R_ProjectEntityBounds` 0x419630); the rectangle is their min/max, with depth. Bit 0x08
    is set only if the whole rectangle is inside the window: min ≥ 0 and max < width/height
    of the current video mode (0x1fa9210 / 0x1fa9214).
  - **Sprites, marks and FL_POINT_COLLISION:** the rectangle is a segment. min = this frame's
    projected origin, max = the previous frame's projected origin (both equal on the first
    frame). Bit 0x08 is set if the point is on screen.

Consequences:

- **Altitude is ignored.** A projectile hits anything whose screen rectangle it overlaps,
  whatever the height. The player's guns hit ground units, and enemy fire hits the player,
  purely by screen overlap. This is how the manual's "air and ground targets" works.
- **Only objects entirely on screen can be hit.** An enemy partly outside the window is
  immune.
- **Rectangles are in real window pixels**, so hit tolerances scale with the resolution. A
  reimplementation should compute them in a fixed virtual resolution (GUESS: 800×600) to be
  resolution-independent. The difference only matters at the screen border.
- The projection uses the matrices of the previous frame's render (the frustum is read back
  from GL in `R_SetupFrustum` 0x419040).

### 5.2 Which pairs are tested

`G_RunCollisions` (0x4055d0) calls `G_TouchEntity` (0x4051d0) for every root entity in list
order that has a touch filter ≠ 0 and runtime bit 0x08, and is not removed.

**TOUCH_PLAYER or TOUCH_ALL:**
- Each player whose health > 0 is tested, in player order:
  - the toucher is a model: rectangle overlap;
  - the toucher is a point: the current point must lie inside the player's rectangle.
- On the first hit: `other` = the player's handle, the toucher's +0x77 = that player's index,
  the toucher's `touch` handler runs, and the function **returns**. Only one player is
  touched per frame, and the enemy scan is skipped even for TOUCH_ALL.

**TOUCH_ENEMIES or TOUCH_ALL:**
- Candidates are all other list entities with class 2, bit 0x08, not removed and not
  health-frozen.
- The test depends on the pair:

| Toucher | Candidate | Test |
|---|---|---|
| model | model | rectangle overlap |
| point | model | the swept segment is clipped against the rectangle (`G_SegmentHitsRect` 0x40ca10). If the segment is shorter than about 0.7 px, the midpoint is tested instead. |
| model | point | always a hit (no test). Never happens in the data: every class-2 object is a model without point collision (VERIFIED-DATA). |
| point | point | never |

- For each hit: `other` = the candidate's handle and the `touch` handler runs. The scan
  continues unless the toucher was removed, so one projectile can touch several enemies in
  one frame if its script does not remove it.

Nothing else is tested: there are no enemy–enemy collisions, no terrain collisions, and
items are ordinary TOUCH_PLAYER objects.

VERIFIED-DATA pairs in the shipped objects:
- player bullets carry TOUCH_ENEMIES;
- enemy bullets and hazards carry TOUCH_PLAYER;
- 42 enemy definitions carry TOUCH_PLAYER (ramming);
- player helicopters have no touch filter;
- pick-up items use TOUCH_PLAYER.

### 5.3 What a hit triggers

Only the `touch` handler runs. The engine applies **no damage natively on touch**. The
scripts call `Damage(other, self[35])` and remove themselves (VERIFIED-DATA, e.g. weapon and
enemy bullet scripts).

### 5.4 Two-player push-apart (0x4055d0)

When both players are alive and their rectangles overlap:
- n = normalize(p1.xy − p2.xy);
- player 1 velocity (fields 17–18) += n × 2000 × frametime;
- player 2 velocity −= the same amount.

### 5.5 Traces

`TraceLine(start*, end*, mask)` and `TraceLineDamage(start*, end*, damage)` (0x41bd40,
0x41bea0) also work in screen space: both points are projected and the 2D segment is tested
against the rectangles (§6.2, §8).

---

## 6. Damage and death

### 6.1 `G_Damage(entity, amount, attacker)` (VERIFIED-CODE 0x404ae0)

1. Ignored when any of these holds:
   - field 4 (dead) ≠ 0;
   - runtime bit 0x10 (FreezeHealth);
   - the entity is a player's entity and that player's freeze counter ≠ 0 (PlayerFreezeHealth);
   - god mode (cheat) is on, for players.
2. Time-since-damage = 0; health (field 34) −= amount. No difficulty factor is applied here;
   the callers apply it.
3. The `damage` handler runs synchronously. It sees the new health, with field 4 still 0.
4. If health ≤ 0 afterwards:
   - if class = 2 and not FL_NONTARGET: the attacker's kill counter += 1 (player record
     +0x16d). With attacker = −1 this writes 4 bytes before player 1's record (0x1ebe304), a
     harmless bug to ignore;
   - field 4 = 1.0;
   - if a drop is set: the drop object is spawned at the origin (ground/water snapping) and
     the drop is cleared;
   - if attacker ≥ 0: `G_AwardScore` (6.3).
5. **The entity is not removed natively.** Its `damage` handler spawns explosions and debris
   and calls `remove` (VERIFIED-DATA, e.g. tanks\tank.scr).

### 6.2 Damage sources (VERIFIED-CODE)

**Attacker rule.** In single-player mode the attacker is 0. In two-player mode it is 0 if the
calling entity's field 3 has 0x2000, otherwise 1. TraceLineDamage, Lightning and particle
damage pass −1 (no score).

| Source | Amount | Targets |
|---|---|---|
| `Damage(handle, a)` 0x41b550 | a × g_damage_factor | the given entity |
| `RadialDamage(center*, radius, rate)` 0x41b5d0 | frametime × rate × (d / radius) × g_damage_factor, for d = 3D distance ≤ radius. **The damage grows toward the edge and is 0 at the centre** (VERIFIED-CODE: the division at 0x41b6d5 is distance / radius, not the reverse). | all list entities with class 2 and health > 0. Never players. Called every frame by the script, so `rate` is per second. |
| `TraceLineDamage(start*, end*, dmg)` 0x41bea0 | dmg per call (not scaled by frametime or difficulty) | If the caller's touch filter is TOUCH_PLAYER (2): every player crossed by the segment. If TOUCH_ENEMIES (1): enemies with class 2, bit 0x08, type 0 (model), not flag 0x1000, alive. |
| `Lightning()` 0x41c000 | self field 35 × frametime × g_damage_factor | every enemy with class 2 and bit 0x08 within 500 units; draws a beam to each |
| Particle systems with a `damage` key (0x404cb0 from 0x414600) | lerp(a, b, age / life), applied on every n-th particle; no difficulty factor | TOUCH_PLAYER: every player whose rectangle contains the projected particle. Otherwise: the first enemy with class 2 and bit 0x08 containing it. |
| `Shoot` 0x41b0d0 | sets the projectile's field 35 from its definition, × g_damage_factor if the shooter is not a player | applied by the projectile's touch script |

**The g_damage_factor difference matters.** Player bullets carry their unscaled damage and
apply it with `Damage`, which multiplies by g_damage_factor. Enemy projectiles are scaled at
Shoot **and** again by `Damage` in their touch script, so enemy damage to the player scales
with factor² (VERIFIED-CODE for both multiplications; GUESS whether that was intended). A
reimplementation must reproduce both multiplications.

### 6.3 Difficulty (VERIFIED-CODE 0x408b30, table 0x4577d0)

The difficulty index (0x4577bc, 0..4, default 2) is chosen in the Start Game menu. The
factors are applied at each level start:

| Index | Name | g_health_factor | g_damage_factor | score factor (0x4577c8) | rank factor (0x4577cc) |
|---|---|---|---|---|---|
| 0 | Very Easy | 0.3 | 0.5 | 0.6 | 0.7 |
| 1 | Easy | 0.5 | 0.7 | 0.8 | 0.85 |
| 2 | Normal | 0.75 | 0.8 | 1.0 | 1.07 |
| 3 | Hard | 1.5 | 1.25 | 1.2 | 1.2 |
| 4 | Nightmare | 2.0 | 1.4 | 1.4 | 1.3 |

- **g_health_factor** multiplies the health of map-placed objects at spawn (3.4). Scripts may
  also read it (it is a script global).
- **g_damage_factor**: see 6.2.
- **Score factor**: 3.4 step 10.
- **Rank factor**: mission-complete rank (10.4).
- Before the first level both factors are 1.0 (initial values in the data section, VERIFIED-CODE).

### 6.4 Score award (VERIFIED-CODE `G_AwardScore` 0x40bb20)

- p_scores += ftol(field 36), capped at 10⁹.
- The number is shown as floating digits: one `score_num` entity per decimal digit (n
  digits), created at (x − (n × 12 / 2 − 6) + 12k, y, z + 8) for k = 0..n−1.
- Each digit entity runs its `init`, then field 33 = the digit. The `score_num` script
  animates and removes them.

### 6.5 Enemy health bar (VERIFIED-CODE 0x404e30)

Drawn for an entity when all of these hold: maximum health > 150, class = 2, less than 1 s
since it was last damaged, and not dead.

- Sprites: `hbar_full` / `hbar_empty` definitions; the fill fraction is health / maximum
  health.
- Position: ground units at (x, y − 0.6 r, z + 1.2 × model min z); others at
  (x, y + 0.6 r, z + 1.2 × model max z).

### 6.6 Invulnerability

- `FreezeHealth(handle, on)` (0x41c530): sets or clears runtime bit 0x10. A frozen entity
  is also excluded from TOUCH_ENEMIES candidates.
- `PlayerFreezeHealth(on)` (0x41c4e0): increments (on) or decrements (off) the freeze counter
  of the calling entity's player (record +0xb4), so calls nest.
- The player's spawn shield (`p_rshield`) uses PlayerFreezeHealth for 7 s (VERIFIED-DATA,
  player.scr).

---

## 7. The player

### 7.1 Player records (VERIFIED-CODE)

There are two records of 0x171 bytes at 0x1ebe308 (player 1) and 0x1ebe479 (player 2):

| Offset | Content |
|---|---|
| +0x00 | player entity pointer (the `player` global is entity + 0x7b) |
| +0x04 | 11 key bindings of {key1, key2, action bit} (0x408a50) |
| +0x88 | helicopter index 0..9 |
| +0x8c | `p_action` (float holding bits) |
| +0x90 | `p_scores` |
| +0x94 | lives at level start |
| +0x98 | `p_lives` |
| +0x9c | `p_stars` |
| +0xa0 / +0xa4 / +0xa8 | `p_counter1` / `p_counter2` / `p_counter3` (script-owned; p_counter3 = 1.0 at level start) |
| +0xac | `p_speedfactor` (script-owned) |
| +0xb0 | `p_weapon` (current primary weapon id) |
| +0xb4 | freeze-health counter |
| +0xb8 | u8 actions disabled |
| +0xb9 | 16 power-up counts |
| +0xf9 | current power-up (−1 none) |
| +0xfd | 5 missile counts |
| +0x111 | current missile type (−1 none) |
| +0x115 | 20 weapon upgrade levels (0 = not owned) |
| +0x165 | banked campaign score (int) |
| +0x169 | rank accumulator |
| +0x16d | kills this level |

All `p_*` script globals point into these records, for the player selected by the running
entity's +0x77.

### 7.2 Input to `p_action` (VERIFIED-CODE 0x408f10, 0x408a50, WndProc 0x421ee0, 0x422bd0)

A key press ORs the bound bit into `p_action` of every player that has the key bound and is
not disabled. A release clears the bit. Bindings come from `[Controls]` (player 1) and
`[Controls2]` (player 2) in config.ini: two key codes per action.

| Bit | Action | Config key | Built-in default |
|---|---|---|---|
| 0x001 | primary fire | KeyPrimaryAttack | Ctrl, joy button 1 |
| 0x002 | fire missile | KeyMissileAttack | Shift, joy button 2 |
| 0x004 | use power-up | KeyUsePowerUp | Space, joy button 3 |
| 0x010 | forward (up the screen) | KeyMoveForward | Up, joy up |
| 0x020 | backward | KeyMoveBackward | Down, joy down |
| 0x040 | left | KeyMoveLeft | Left, joy left |
| 0x080 | right | KeyMoveRight | Right, joy right |
| 0x100 | next missile type | KeySwitchMissiles | 1, Joy5 |
| 0x200 | next power-up | KeySwitchPowerUp | 2, Joy6 |
| 0x400 | next weapon | KeySwitchWeapon | 2, joy4 |

Key codes are Windows virtual-key codes, extended as follows (VERIFIED-CODE, key-name
routine 0x422da0 and its table at 0x456ae0):

- 200–202 are "Mouse 1–3".
- 203–206 are named "joy1".."joy4".
- 207–238 are named "Joy1".."Joy32" (code − 206), so the default 211 and 212 display as Joy5
  and Joy6. How `Joy_Frame` numbers physical buttons into these two ranges was not traced
  (GUESS: buttons 1–4 → 203–206, buttons 5 and up → 206 + n).
- 239 and 240 are the mouse wheel down and up (press only).
- 241–244 are the stick left/right/up/down.

The shipped `config.ini` binds player 1's fire, missile and power-up to the mouse buttons
(200, 201, 202) and player 2's to joy1–joy3. The stick threshold is JoystickThresholdX/Y
(default 0.3) applied to (raw − 32768) / 32768.

`G_PlayerFrame` (0x40b440) consumes bits 0x200, 0x100 and 0x400 as one-shot edges (it clears
them after acting):
- 0x200: next owned power-up (0x40b0d0);
- 0x100: next owned missile type (0x40b1b0);
- 0x400: next owned weapon in cyclic order over the 20 upgrade slots (0x40b280).

Bits 0x01, 0x02, 0x04 and the four direction bits are level-held. The player script reads
them (`BAND p_action, n`, VERIFIED-DATA).

`PlayerDisableAction(on)` (0x41c560) sets record +0xb8; setting it also zeroes `p_action`.
While it is set, key events are ignored for that player. The end-of-level autopilot
(`eol.scr`) uses it (VERIFIED-DATA).

### 7.3 Movement (VERIFIED-DATA player.scr, player_a.scr, player_c.scr; x limit VERIFIED-CODE)

Movement is implemented by the player script, not natively. The three scripts share the
same movement logic:

- Per axis: while a direction bit is held, velocity += 1000 × frametime (units/s²) in that
  direction. Otherwise the velocity decays toward 0 by 330 × frametime. Each component is
  clamped to ±150 units/s.
- origin += velocity × frametime × `p_speedfactor`. In addition, origin.y += the camera's
  scroll speed (camera field 7) × frametime, so the player keeps pace with the scroll.
- y is clamped by the script to [g_map_pos + 35, g_map_pos + 320].
- x is clamped **natively** in `V_UpdateCamera` (0x40c260 via 0x419b10): the player's x is
  pushed inside the left and right frustum planes of the previous frame, with a 10-unit
  margin. In two-player mode each living player is clamped.
- Tilt: pitch = 30 × vy / 150 degrees, roll = −25 × vx / 150 degrees (angles fields; signs
  as in the script).

**Mouse control** (`MouseControl=1`, player 1 only, skipped while actions are disabled;
VERIFIED-CODE 0x40b550). Every frame, sc = the centre of the player's screen rectangle,
converted to the 800×600 virtual screen with the factor at 0x4d52f1. The direction bits are
then forced:

| Bit | Set when | Cleared otherwise |
|---|---|---|
| right (0x80) | mouse x > sc.x + 20 | yes |
| left (0x40) | mouse x < sc.x − 20 | yes |
| forward (0x10) | mouse y < 600 − sc.y − 20 | yes |
| backward (0x20) | mouse y > 600 − sc.y + 20 | yes |

The helicopter therefore flies toward the cursor, using the keyboard movement model. The
dead zone is 20 px. The flip of sc.y means the projection's y axis points up.

### 7.4 Spawn, lives and respawn

**Level start (0x408b30).** For each player:
- p_lives = lives at level start;
- p_scores, p_stars and kills = 0; p_counter3 = 1;
- upgrades cleared, then upgrade[0] = 1 (machine gun) and p_weapon = 0;
- `p_action` = 0.

**Lives.**
- A new campaign sets lives at level start = 2, so the player has 3 helicopters.
- Banking at level end copies p_lives into lives at level start, so lives carry over between
  missions (VERIFIED-CODE 0x40bd10).
- Restart after game over or from the menu reuses the lives the level began with.

**`G_SpawnPlayer` (0x40b2f0).** Called by `RespawnPlayer()` (0x41c640) and at level load.
1. Removes the old entity.
2. If p_lives < 0: marks the old entity dead and stops.
3. Otherwise it clears the missile and power-up counts and selections. The routine clears
   them for **both** players (VERIFIED-CODE); a reimplementation may keep this.
4. Creates the helicopter object named in the helicopter table (index at +0x88), sets +0x77,
   stores the `player` pointer and runs `init`.
5. In two-player mode the x position is offset −100 (player 1) or +100 (player 2).

**Script side (VERIFIED-DATA player.scr).**
- Health 400. This matches the HUD bar scale (11.2).
- Start at (640, g_map_pos − 50, 100).
- A 7 s spawn shield, and a fly-in at vy = 120 with input ignored until y ≥ g_map_pos + 60.
- On death: crash with gravity 40 and explosions, p_lives −= 1, every weapon upgrade above
  level 1 loses a level, then `RespawnPlayer()`.
- Below 200 and 100 health the model changes to damaged versions.

### 7.5 Two-player mode

- Selected in the Start Game menu (player count 0x4577b8 = 2, flag 0x1fdb2c7).
- Player 2 has its own bindings (`[Controls2]`), record, lives, score and helicopter choice.
  Clicks in the helicopter grid alternate between the players.
- The camera follows the mean x of the living players (§9.3).
- Map objects are assigned randomly to a living player (+0x77), which decides who gets the
  item or score through the `p_*` globals.
- Projectiles carry their owner (field 3 bits 0x2000 / 0x4000), which picks the scorer.
- The game is over when both players have p_lives < 0.
- High scores are not recorded in two-player mode.
- The two-player HUD (0x402a90) is not decoded here.

### 7.6 Helicopter choice

The helicopter table (0x457660, 10 records of 33 bytes: u8 unlocked, name[32]) lists
p_apache, p_comanche, p_apache_white, p_apache_impala, p_comanche_white, p_comanche_lava,
p_apache_blue, p_comanche_sand, p_comanche_blue and p_comanche_green.

- Entries 0 and 1 are unlocked by default; the unlock flags are saved in game.bin.
- The level key `enableHelic n` unlocks entry n when that mission is completed.
- VERIFIED-DATA (levels.txt): mission3 → 7, mission5 → 4, mission7 → 2, mission9 → 3,
  mission11 → 9, mission13 → 8, mission15 → 6, mission17 → 5.
- Each helicopter object names its own script; `player.scr`, `player_a.scr` and
  `player_c.scr` are variants of the same logic.

---

## 8. Weapons and items

### 8.1 Weapon definitions and `Shoot` (VERIFIED-CODE 0x40c770, 0x41b0d0)

- `weapons/*.wpn` blocks are stored as 0x2c-byte records at 0x1ebe668 (at most 256; "Too many
  weapons."): name[32], flash object definition, missile object definition, speed. Lookup by
  name logs "ERROR: Unknown weapon '%s'."

`Shoot(weapon, tag, direction*)`:
1. Does nothing unless the shooter has runtime bit 0x08 (on screen) and is not in state 2.
   **Off-screen enemies cannot fire.**
2. Muzzle = shooter base origin + axis · (tag position × scale field 32) (`G_GetTagWorldPos`
   0x4046a0).
3. Spawns the `missile` definition at the muzzle:
   - class 4.0, previous origin = origin;
   - the direction is normalised;
   - angles: x = −pitch of the direction, y = 0, z = yaw − 90;
   - the origin is advanced along the direction by −(model bbox min y) (the nose sits at the
     muzzle);
   - velocity (fields 17–19) = direction × `speed`.
4. Runs the projectile's `init` (and its children's), then one think.
5. Field 35 (damage) × g_damage_factor if the shooter is not a player (§6.2).
6. Two-player mode: the projectile gets the shooter's +0x77 and field 3 bit 0x2000 (player 1)
   or 0x4000 (player 2).
7. Spawns the `flash` definition attached to the shooter at the tag.
8. Returns the projectile handle.

The projectile's own script moves it with `move(velocity)` and handles hits in `touch`.

### 8.2 Weapon upgrades (VERIFIED-CODE 0x41c3d0 / 0x41c440; data VERIFIED-DATA)

- `G_GetUpgrade(i)`: the upgrade level of weapon i, or 0 when i > 19.
- `G_SetUpgrade(i, v)`: stores it when i < 20.
- Both builtins **round** their float arguments to nearest instead of truncating.
- Weapon ids used by the scripts (maximum level in parentheses): 0 machine gun (3),
  1 impulse (5), 2 plasma (4), 3 laser (4), 4 big laser (3), 5 BPG (4), 6 GOROX (3),
  7 meteorite (3), 8 wave (3), 9 flamethrower (3).
- The first time a weapon is picked up, the native code sends `callback(player, 2, id, 0)` to
  the player entity (GUESS: the script switches weapon on that message).
- The next-weapon key cycles through owned weapons (8.1 bit 0x400).

### 8.3 Missiles and power-ups (VERIFIED-CODE 0x41c210..0x41c3b0, 0x40b0d0..0x40b280)

- `G_AddMissiles(type, n)`, type < 5:
  - count += n, capped at 99;
  - if no type is selected, selects this one.
- `G_GetMissiles()`: the selected type, or −1.
- `G_UseMissile()`:
  - if the selected count > 0: decrements it and returns 1;
  - otherwise selects the next type that has missiles and returns 0.
- Power-ups have the same three builtins over 16 slots (`G_AddPowerUp`, `G_GetPowerUp`,
  `G_UsePowerUp`). Four kinds are used in the data.

Shipped kinds (VERIFIED-DATA, item and player scripts):

| Missile type | Name | Pickup | Script fire interval |
|---|---|---|---|
| 0 | small missiles | +20 | 0.3 s |
| 1 | big missiles | +12 | 0.5 s |
| 2 | heat-seeking | +20 | 0.4 s |
| 3 | big heat-seeking | +15 | 0.6 s |
| 4 | M.A.D. | +12 | 0.4 s |

| Power-up | Name | Pickup |
|---|---|---|
| 0 | lightning | +2 |
| 1 | nuclear bomb | +1 |
| 2 | rocket strike | +4 |
| 3 | cluster bomb | +4 |

The use interval for power-ups is 0.5 s.

### 8.4 Targeting (VERIFIED-CODE 0x40c020, 0x41bb40, 0x41bb60)

- `LockTarget()` searches list entities with class 2, bit 0x08, not FL_NONTARGET, not dead,
  health > 0, and not already locked (runtime bit 0x100). It takes d = normalize(target −
  player origin) of the caller's player and requires d.y ≥ 0.3 (ahead of the player). It
  returns the nearest in 3D (search starts at 9999) and sets bit 0x100 on it, so two missiles
  do not lock the same target. It returns 0 when there is none.
- `IsValidTarget(handle)`: true if the handle is non-null, field 4 = 0 and health > 0.
- `PushPlayer()`: player velocity.xy += normalize(player − self).xy × 4000 × frametime.

---

## 9. Scrolling and camera (VERIFIED-CODE 0x40c660, 0x40c260, 0x40c150)

### 9.1 The camera structure

The `camera` script global (0x456f6c) points to 0x1ebe5f4; camera field n is at 0x1ebe5f4
+ 4n:

| Field | Meaning |
|---|---|
| 0..2 | camera origin |
| 3..5 | camera angles: pitch, yaw, roll (degrees) |
| 6 | camera x velocity (scripts may set it; camera x += field 6 × frametime) |
| 7 | scroll speed, rewritten every frame = field 9 × 42 |
| 8 | 0 |
| 9 | scroll factor (0x1ebe618) |
| 10..13 | viewport x, y, w, h |
| 14 | vertical field of view |
| 15..17 | view origin |
| 18..20 | view angles |
| 21 | activation back edge = g_map_pos − 64 |
| 22 | activation front edge = g_map_pos + 1000 |

Scripts use field 6 as the base of a 3-vector (fields 6..8, i.e. camera velocity) and read
field 7 to move with the scroll. They write field 9 (VERIFIED-DATA).

### 9.2 Scrolling

- At level start (0x40c660): g_map_pos = 32, scroll factor = 1, scroll speed = 42, camera x
  = 640.
- Each unpaused frame: g_map_pos += frametime × 42 × scroll factor. The base speed is
  **42 units/s**, the same for every level.
- Native code never changes the factor after the reset. Scripts do:
  - bosses set it to 0 to stop the scroll;
  - `eol.scr` ramps it down at the end of a level;
  - `p_speeddown.scr` slows it (VERIFIED-DATA).

### 9.3 Camera modes and placement

Config `[System] Camera` (0..3, default 1). F9 cycles the modes in game. The table is at
0x457398, 16 bytes per mode:

| Mode | Menu name | FOV (vertical) | Pitch | Height z | Y offset |
|---|---|---|---|---|---|
| 0 | Low Pitch | 60° | −35° | 270 | 0 |
| 1 | Default | 60° | −45° | 270 | 0 |
| 2 | High Pitch | 60° | −50° | 270 | 0 |
| 3 | Top-Down | 70° | −15° | 370 | +100 |

The pitch is measured from straight down, tilted toward +y. The modelview is
RotX(pitch) · RotY(yaw) · RotZ(roll) · Translate(−origin), with GL looking down −z before
the rotation.

Each unpaused frame:
- camera y = g_map_pos + Y offset; camera z = height;
- camera x += frametime × field 6;
- follow target = the player's x (two players: the mean of the living ones);
- if target − camera x > 48, camera x = target − 48; if target − camera x < −48, camera x =
  target + 48 (a ±48 dead zone);
- camera x is then clamped to [578, 702];
- yaw and roll are 0 except for the quake.

Projection (`R_RenderView` 0x40e0d0): gluPerspective(FOV, width / height, near 4, far).
Far = 2000 without fog. With fog, far = the fog end, but at least 1000.

Visible ground at z = 0 for a 4:3 window, computed from the table (GUESS: derived numbers,
not read):

| Mode | Distance ahead of camera y (bottom / centre / top of screen) | Half-width of the ground (bottom / centre / top) |
|---|---|---|
| Low Pitch | 24 / 189 / 579 | 181 / 254 / 426 |
| Default | 72 / 270 / 1008 | 186 / 294 / 695 |
| High Pitch | 98 / 322 / 1531 | 192 / 323 / 1037 |
| Top-Down | −35 / 199 / 541 | 301 / 358 / 440 |

### 9.4 CameraQuake (VERIFIED-CODE 0x41bb90, 0x40c150)

`CameraQuake(A)` sets the amplitude to A and the quake clock to 3.0 s. While the clock runs
(unpaused), with t = elapsed time and e = exp(−2t):

- yaw = 2A · e · sin(24t)
- roll = A · e · sin(12t)
- FOV = mode FOV + A · e · sin(18t)

Shipped scripts use A = 0.3, 0.5, 0.6, 1.0 or 1.4 (VERIFIED-DATA). A new call restarts the
quake.

### 9.5 Screen and world

- The 2D layer uses a virtual 800×600 screen with y down. The mouse is converted to it by
  the factor at 0x4d52f1.
- The player is bounded in x by the frustum (§7.3) and in y by its script.
- Collision rectangles are in window pixels (§5.1).

### 9.6 Intermission (attract) levels

Levels with an `intermission x y z pitch yaw roll` line (intro1..4) use a fixed camera at
that position:
- pitch sways by 0.5 · sin(0.5 · time) and roll by 1.4 · sin(0.75 · time);
- FOV is 60;
- g_map_pos stays at its initial value;
- no players are spawned.

### 9.7 Activation handover (for the level package)

- Placed objects are instantiated when their row ≤ (g_map_pos + 1000) / 40, and only if
  their row ≥ (g_map_pos − 64) / 40.
- Their scripts start when their bounding sphere lies in the band y − r ∈ [g_map_pos + 16,
  g_map_pos + 800] (3.5).
- In-memory positions are (col × 40 + 20, row × 40 − 20).

---

## 10. Level flow

### 10.1 Level list (VERIFIED-CODE 0x406340 / 0x406880)

- `maps\levels.txt` holds at most 32 records ("Too many levels in level-list file.").
- Keys: `id`, `name`, `map`, `music`, `textures`, `enableHelic` (default −1), `hmin`, `hmax`,
  `sun` (9 floats), `water` (texture, flag, level), `night`, `fog` (5 floats), `intermission`
  (6 floats).
- The mission table (20 entries, saved in game.bin) maps mission index to level id.
- Unknown ids log "ERROR: Unknown level identifier '%s'."

### 10.2 Level start (VERIFIED-CODE 0x408b30 → 0x407080)

`G_BeginLevel` (0x408b30):
1. Applies difficulty (§6.3).
2. Clears pause, HUD-hidden and game-over.
3. Resets per-player state (7.4).
4. Calls `G_StartLevel` (0x407080).
5. Resets the enemy total, maximum score and kill counters.

`G_StartLevel` (0x407080). A progress bar is drawn at each step (0x404230: `menu\loading.tga`
at (272, 172), bar at (10, 595) of width progress × 780).
1. Sets the intermission flag from the record; clears pause.
2. HUD textures and `sounds\type.wav`.
3. All particle systems (`particles/*.ps`).
4. Font and UI textures.
5. The map (0x4091f0, level package), which also counts `item_star` drops into the star
   total (0x4d52d0).
6. Fog setup.
7. For every placed map object: builds its path, precaches its script's CASH list,
   registers its model, skin and shadow (the "precache" step).
8. Resets the particle pool (256 emitters) and the entity pool (1024).
9. Resets the camera and scroll (9.2).
10. Starts BASS.
11. Spawns the players (not on intermission levels).
12. Resets the activation cursor; sets `l_night`, `l_water`, `l_waterlevel`; level clock = 0.
13. Renders one frame.
14. Loads and loops the level's `music` module.

The loading steps printed in `game.log` follow this order.

### 10.3 End of level

- The level script calls `EndLevel()`. Typically `eol.scr` first disables player actions,
  flies the player out and ramps the scroll to 0 (VERIFIED-DATA).
- The native side is described in §1.3 state 9.
- Cheats can also trigger it (§14).

### 10.4 Scoring and rank (VERIFIED-CODE 0x40bd10, 0x426360, 0x40bda0)

The mission-complete screen shows, appearing at t = 1.0, 1.4 and 1.8 s:
- "Enemies destroyed: kills × 100 / enemies-in-level %";
- "Stars collected: p_stars / star total";
- "Your current rank: (p_stars / star total + 0.5 × p_scores / maximum level score +
  accumulator) × rank factor".

Banking (on Continue or Quit from game over), per player:
- banked score = ftol(banked + p_scores);
- rank accumulator += p_stars / star total + 0.5 × p_scores / maximum level score;
- lives at level start = p_lives.

A level with no stars divides by zero (original behaviour; guard it).

Rank index (0x40bda0):
- 0 ("Cheater") if a cheat was used;
- otherwise the first i in 1..5 with value < threshold[i], with thresholds 3, 7.5, 14, 22,
  30; else 6.

Rank names: Cheater, Rookie, Junior Pilot, Pilot, Master Pilot, Berserker, Elite.

The HUD score shows ftol(p_scores) + banked.

There is no separate end-of-level bonus beyond these ratios (VERIFIED-CODE for the screens
read; GUESS that nothing else adds score).

### 10.5 Save file (VERIFIED-CODE 0x401050 / 0x4011b0, checked on the shipped game.bin)

Total size 1858 bytes:

| Part | Content |
|---|---|
| float | 1.0 (anything else: "Illegal version") |
| 256 bytes | XOR key (new random bytes on each save) |
| u32 | CRC-16/CCITT (polynomial 0x1021, initial 0xffff, no final XOR) of the encrypted payload |
| 0x63a bytes | payload, byte i XORed with key[i & 0xff] |

Payload:

| Offset | Content |
|---|---|
| 0 | u32, zeroed after loading and otherwise unused |
| 4 | 15 high scores × 40 bytes: name[32], i32 score, i32 rank |
| 604 | 10 helicopters × 33 bytes: u8 unlocked, object name[32] |
| 934 | 20 missions × 33 bytes: u8 unlocked, level id[32] |

- The third-party description ("6 player slots, 18 mission entries") is wrong.
- Default contents are compiled in: helicopters 0 and 1 and missions 1–2 unlocked; high
  scores from "Divo Master" 1,000,000 down.
- The file is written only at program exit.
- Our engine will use its own format; this section only documents what must be imported.

### 10.6 Unlocking

Completing mission i unlocks mission (i + 1) mod 20 and the helicopter named by the level's
`enableHelic`. Nothing else is unlocked.

---

## 11. HUD and menus (brief; a later package details them)

### 11.1 Coordinates

All 2D drawing is in a virtual 800×600 screen with the origin top-left. The 2D queue holds
4096 quads per frame (0x40cdb0).

### 11.2 One-player HUD (VERIFIED-CODE 0x401ed0, textures from 0x401aa0)

Textures: `gfx\ui\mainbar.tga`, `life.tga`, `weapons.tga`, `missiles.tga`, `items.tga`,
`font.tga`, the mouse-control cursor, and `menu\cursor_1/2.tga`.

| Element | Position | Mapping |
|---|---|---|
| Health bar frame | (10, 10), 180×21 | mainbar UV (0, 0.836)–(0.703, 1.0), grey 0.5 |
| Health bar fill | (12, 10), width = health / 400 × 172 | UV s up to health / 400 × 174/256, t 0.672–0.836. Full bar = 400 health. |
| Lives | icons at x = 15 + 32i, y = 555 | min(p_lives, 5) icons, grey 0.63 |
| Weapon box | (10, 32), 70×39 | icon 66×35 at (11, 35); `weapons.tga` UVs from a table indexed by p_weapon |
| Missiles | one frame per owned type at (10, 73 + 41k) | icon from `missiles.tga`; count right-aligned at 0.75 scale; orange (0.82, 0.25, 0) if selected, else dark red (0.5, 0.125, 0); the selected frame is drawn twice |
| Score | mirrored bar at (610, 10), 180×21 | number at (630, 12), colour (0.75, 0.19, 0) |
| Power-ups | owned kinds 0–3 at (720, 32 + 41k) | icon from `items.tga`; count shown if > 1 |

- Number font: `font.tga` is a grid of 8 columns × 16 rows; a glyph is 32 × 16 × scale
  pixels (0x4258f0).
- The HUD is not drawn on intermission levels or while HUD-hidden.
- The level-name typewriter (0x401c60):
  - starts after 1 s of level time;
  - types the level `name` at 8 characters/s, with `type.wav` per non-space character;
  - centred at x = 400, y ≈ 500;
  - holds 3 s after the last character, then fades over 1 s.
- Cheat messages show for 3 s, fading in the last second (0x401330).

### 11.3 Tutorial hints (VERIFIED-CODE 0x41c4c0 → 0x42c000)

`ShowTutorialHint(text)`:
- pauses the world;
- splits the text into lines on `^` (at most 16 lines of 64 characters) and justifies them;
- shows a centred message box of width max(360, widest + 40) and height max(160,
  18 × lines + 80) with an OK button;
- OK resumes.

It is shown regardless of the `ShowHints` setting, which controls menu tooltips (GUESS: the
scripts do not check it either; not verified).

### 11.4 Menu tree (VERIFIED-CODE)

- **Main**
  - Start Game: mission, difficulty, players, helicopter, Back/Start.
  - Top Scores (with Post).
  - Options:
    - video: Resolution, Refresh rate, Color Depth, Fullscreen, Brightness;
    - audio: Sound Volume, Music Volume, 3D Sound;
    - Camera, Mouse Control;
    - Configure keys, for player 1 and player 2.
  - Information: story ×2, overview, primary weapons ×3, missiles ×2, items, credits.
  - Exit (yes/no).
- **In game (Esc):** Resume, Options, Quit.
- **Game over:** Restart, Quit.
- **Mission complete:** Quit, Restart, Continue.
- **Game complete:** Continue.
- **Name entry** for high scores.

Menu mechanics:
- Tooltips appear after 1 s of hovering when ShowHints = 1.
- Tab and the arrow keys move the selection; Esc or the right mouse button goes back.

---

## 12. Sound (VERIFIED-CODE 0x41fa60..0x41ff90)

**Setup.**
- BASS at 22050 Hz, with 3D when `Sound3D` = 1 (`-nosound` disables sound).
- 3D factors: distance 1.0, rolloff 2.0, doppler 0.
- At most 511 registered samples ("S_RegisterSound: Too many registered samples.").
- 3D samples use minimum distance 400 and maximum distance 700.

**Script builtins.**
- `StartSound(name)`: plays the sample on the calling entity (3D, attached to it).
- `StartLoopingSound(name)`: stops the entity's previous loop (channel at +0x73) and starts a
  looping one.
- `StopLoopingSound()`: stops it.
- Names are resolved through the script's CASH entries of kind 3.

**Positioning.**
- At most 32 entity-attached channels.
- Each frame (0x41fc30) every attached channel is placed at (entity x, 0, entity y): height
  is ignored.
- The listener is at (view x, 0, view y).
- Without 3D sound, sounds play centred at full volume.
- There is no screen-based panning code besides BASS 3D.

**UI sounds** (menu, typewriter, cheat) play in 2D.

**Music.**
- The level's `music` module (.mo3) is loaded at level start and loops.
- On game over and on quit, the current module jumps to pattern order 35, or 0 if the module
  is shorter (GUESS: each module contains a game-over section at order 35).
- Keys F5/F6 and F7/F8 change the effects and music volume by 0.1.

---

## 13. Rendering order and material state (first table; WP-25 goes deeper)

### 13.1 Frame render order (`R_RenderView` 0x40e0d0)

1. Terrain: quadtree with detail texture, unless the scene has no world.
2. Frustum setup, read back from GL (0x419040).
3. Decals (TYPE_MARK, at most 128).
4. Shadows (0x40db20).
5. Opaque model list (SORT_OPAQUE, at most 512).
6. Water (0x4160d0): alpha-blended strips 1280 wide at `l_waterlevel`.
7. SORT_TRANS list (at most 128).
8. SORT_EFFECT list (at most 256).
9. Particles, with depth writes off.
10. Debug counters (ShowFPS etc.).
11. Sprites (types 1, 3 and 4, at most 512).
12. 2D: ortho 800×600, then the 2D queue.

There is no sky: the clear colour is the fog colour. Fog is linear, from the level's `fog`
line. A full list silently drops further items. Lists are drawn in insertion order, which
is entity list order, newest first (GUESS: no depth sorting inside TRANS and EFFECT; WP-25
to confirm).

### 13.2 Material state

| Setting | GL state | Function |
|---|---|---|
| BLEND_ALPHA 1 | blend SRC_ALPHA, ONE_MINUS_SRC_ALPHA | 0x412be0 (with a state cache) |
| BLEND_ADD 2 | blend ONE, ONE; fog colour swapped (GUESS: to black) | 0x412be0 |
| BLEND_FILTER 3 | blend DST_COLOR, ZERO; fog colour swapped to white | 0x412be0 |
| internal 4 | blend DST_COLOR, SRC_COLOR (2× modulate), used by the brightness quad | 0x412be0, 0x40e6f0 |
| ENV_GLITTER 1 | second texture unit: sphere-mapped envmap added | 0x4127e0 |
| ENV_CHROME 2 | base texture alpha mixes base and reflection | 0x4127e0 |
| ENV_QUAD 3 | single unit sphere map, texture matrix rotating 30°/s | 0x4127e0 |
| RF_NOLIGHTING 1 | flat entity colour instead of software lighting | 0x410ac0 |
| RF_NOCULLING 2 | face culling off | 0x410ac0 |
| RF_NODEPTHTEST 4 | depth test off | 0x410ac0 |
| RF_NODEPTHWRITE 8 | depth mask off | 0x410ac0 |
| RF_NODLIGHT 0x200 | lighting without dynamic lights | 0x410530 |
| RF_BANNER 0x10000 | texture scrolls 0.1/s, colour pulses 0.75 + 0.25·sin(1.5·time) | 0x410ac0 |
| SHADOW_PROJECTED 1–3 | silhouette texture baked on the terrain once at spawn (does not follow motion); LOW and HIGH only change the texture size | 0x40f2a0, 0x414a70 |
| SHADOW_PLANAR(_PROJECTED) 4–9 | silhouette re-projected onto the terrain every frame | 0x40f8c0 |

Environment modes need multitexture; without it the envmap is ignored. Only models with
smooth normals take the envmap path.

**Defaults (GL_Init 0x412220).**
- Depth test LEQUAL.
- Back-face culling, front faces CCW.
- Flat shading.
- Textures wrap REPEAT with env MODULATE.
- The alpha function is GEQUAL 0.7, but alpha test is not enabled.

**Lighting (0x410530).**
- Software lighting from a six-direction ambient cube built from the level's `sun` values.
- Up to 32 dynamic lights per frame (`PlaceLight`, and definition `light`). Each adds
  (0.7 · dot + 0.3) · (r − d) / r × colour.

**Brightness.** A full-screen modulate quad with grey level = `Brightness` from config.

---

## 14. Cheats and debug features (VERIFIED-CODE 0x4014c0, 0x408f10)

Typed text (WM_CHAR) is lower-cased into a rolling 32-character buffer, only outside
intermission levels. When the buffer contains a code word:
- the buffer is cleared;
- the "cheat used" flag is set (rank Cheater, no score posting);
- `sounds\cheat.wav` plays;
- a message is shown.

| Code | Effect |
|---|---|
| `iwannabe` | toggles god mode: player damage ignored ("God Mode: Enabled/Disabled") |
| `endisnear` | p_lives = 99 for both players ("All Lives: Enabled") |
| `armorychamber` | every weapon not owned becomes owned at level 1 |
| `launchmenow` | +99 of every missile type (cap 99); selects type 0 if none selected |
| `iamstronger` | +99 of power-ups 0..3 (cap 99) |
| `messwiththebest` | EndLevel (mission complete) |
| `dieliketherest` | game over |
| `givemecredit` | credits message box |

Keys:
- F12 or PrintScreen: screenshot to `screenshots\`.
- P or Pause: pause.
- Esc: in-game menu.
- F5/F6: effects volume −/+ 0.1.
- F7/F8: music volume −/+ 0.1.
- F9: next camera mode.

Config `[Debug]`: ShowFPS, ShowTris, ShowTexBinds and ShowCounters print the render counters
(triangles, texture binds, models, sprites, marks, entities).

---

## 15. Constants and limits

| Item | Value | Evidence |
|---|---|---|
| Maximum frame delta | 100 ms | VERIFIED-CODE 0x420976 |
| Entity pool | 1024 × 0x1e3 bytes; no overflow check | VERIFIED-CODE 0x407080, 0x404530 |
| Object definitions | 2048 | "Too many objects", 0x40a160 |
| Attaches per definition | 64 (extra ignored) | 0x40a160 |
| Map objects | 16384 | "Too many map objects", 0x4091f0 |
| Levels | 32 | 0x406340 |
| Missions / helicopters / high scores | 20 / 10 / 15 | game.bin |
| Weapons (.wpn blocks) | 256 | "Too many weapons.", 0x40c770 |
| Weapon upgrade slots | 20 | 0x41c3d0 |
| Missile types / power-up slots | 5 / 16, counts capped at 99 | 0x41c2f0, 0x41c210 |
| Particle systems / emitters | 256 / 256 | 0x412d80, 0x413780 |
| Registered models | 1024 | "R_RegisterModel: Too many registered models." |
| Textures | 2048 | "RegisterTexture: Too many registered textures." |
| Shadow maps | 4096 | "R_RegisterShadow: Too many shadowmaps." |
| Sound samples / entity channels | 511 / 32 | 0x41ff90, 0x41fdf0 |
| Dynamic lights per frame | 32 | 0x40dd00 |
| Render lists | opaque 512, trans 128, effect 256, sprites 512, marks 128, 2D 4096 | 0x40de50, 0x40cdb0 |
| Menu stack / items per menu | 16 / 64 | 0x4292f0 |
| Script stall limit | 10,000 instructions per run | rcsl-opcodes-v0.md |
| Map width | 1280 units (32 cells of 40) | 0x4050b0 |
| Scroll speed | 42 units/s × scroll factor | 0x40c260 |
| Start g_map_pos | 32 | 0x40c660 |
| Camera x range | 578..702, dead zone ±48, start 640 | 0x40c260 |
| Camera height / pitch / FOV | see 9.3 | table 0x457398 |
| Near / far plane | 4 / 2000 (fog end, at least 1000) | 0x40e0d0 |
| Frustum x margin for players | 10 units | 0x419b10 |
| Map object instantiation | rows up to g_map_pos + 1000 | 0x407590 |
| Script activation band | g_map_pos + 16 … + 800 | 0x4050b0 |
| Player speed | max 150 u/s, acceleration 1000 u/s², friction 330 u/s² | player.scr (VERIFIED-DATA) |
| Player y band | g_map_pos + 35 … + 320 | player.scr (VERIFIED-DATA) |
| Player health | 400 | player.scr, HUD scale |
| Lives | 2 spare at campaign start | 0x408c40 |
| Spawn shield | 7 s | player.scr |
| Mouse dead zone | 20 px | 0x40b550 |
| Two-player push | 2000 u/s² | 0x4055d0 |
| Two-player x spawn offset | ±100 | 0x40b2f0 |
| PushPlayer | 4000 u/s² | 0x41c170 |
| Lightning range | 500 units | 0x41c000 |
| Lock-on cone | d.y ≥ 0.3 (normalised) | 0x40c020 |
| Score cap | 10⁹ | 0x40bb20 |
| Score digit spacing | 12 units, z + 8 | 0x40bb20 |
| Health bar | maximum health > 150, shown for 1 s after a hit | 0x404e30 |
| CameraQuake duration | 3 s | 0x41bb90 |
| 3D sound | minimum 400, maximum 700, rolloff 2 | 0x41fa60 |

---

## Open questions (prioritised by impact on a playable game)

1. **Exact screen projection for collision** (high). The rectangle depends on the projection
   and viewport of the previous frame, the window resolution, and the pivot of
   `bbox_scale`. A reimplementation that renders at another aspect ratio sees different hit
   boxes. Decide on a fixed virtual resolution; confirm the pivot in 0x405930 / 0x419630.
2. **Joystick button numbering** into the codes 203–206 and 207–238 (low for keyboard
   play). See `Joy_Frame` 0x422bd0.
3. **The two-player HUD** layout (0x402a90) (low).
4. **Model forward axis and tag axes** (medium): the model package owns them. Shoot and
   `vec_toyaw` subtract 90°, which suggests models face +y.
5. **g_damage_factor applied twice to enemy projectiles** (medium). Reproduce as coded;
   confirm in play.
6. **Render order inside the TRANS and EFFECT lists** (medium visual). WP-25.
7. **Music game-over order 35** (low).
8. **Fields 11–13** ("previous origin") exact use (low).

## Changelog

- 0.1 (WP-24): first version. Main loop, frame order, entity system, collision, damage, player,
  weapons, camera, level flow, HUD, sound, render state, cheats, limits.
