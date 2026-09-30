# Missions status: AirStrike II: Gulf Thunder (package F3)

All twenty-four Gulf Thunder operations and both attract levels run in the native engine as
it is today (`gulf`, v2.71 data), with AirStrike 2's rules and Gulf Thunder's counts
(`engine/src/game/game_profiles.cpp`, `rulesGulf`), no engine change. The procedure is the
one of `docs/missions-status-as2.md`. 22 operations end; the two boss operations do not
(operation 10 ends in a game over, operation 24 never ends). The Gulf Thunder rules found in
the executable are in `docs/spec/gulf/engine-behaviour.delta.md`; this page says what a player
would notice today and where the fix is specified.

## How the runs were made

Binaries built at the head of this branch (`AS3D_CI_GAMES=as3d tools/ci.sh`, `ci: OK`). For
each operation N, one run at a time, each binary under `ulimit -v 4000000` and `timeout 900`:

1. `as3d_sim --game gulf --level N --pilot --god --frames 120000 --record in_N.txt
   --builtin-report rep_N.json --dump-state sim_N.json`: the world-aware test pilot, god mode,
   seed 1, Normal difficulty, helicopter entry 1 (`player_2`, the tools' default), 60 frames
   per second.
2. `AS3D_GOD_MODE=1 as3d_game --game gulf --headless --level N --god --input-script in_N.txt
   --frames E --screenshot-every 600 --dump-state game_N.json --no-audio`, E = end frame + 120
   (120000 when the operation does not end): the same input through the game loop and the
   renderer.
3. `as3d_sim --game gulf --level N --god --input-script in_N.txt --frames E --dump-state
   simr_N.json`; "Replay equal" = the two dumps are byte-identical.

Attract levels `intro1` and `intro2` (both on map `intro.hsc`): 3600 frames in the simulation
(`--level intro1|intro2`). Screenshots: `out/gulf/missions/<N>/frame_*.png` of the main checkout
(gitignored, never committed); run logs and dumps in `out/gulf/runs/`.

Comparison with the original: `tools/run_original.sh gulf -- -god`, operations 1, 2 and 3
started from the Start Game list with the helicopter the save selects (Red Hawk, `player_1`),
the player left alone, screenshots about every 3 s from the first frame of play
(`out/gulf/cmp/opN_tNN.png`). Ours: the same operations rendered with no input,
`as3d_game --game gulf --headless --level N --god --frames 2700 --screenshot-every 10 --size
800x600`, aligned with `tools/compare_reference.py --best-of` over the scenery (region
100,60 to 700,520, vertical shift search 40 px) and measured region by region.

## Table

| Op | Name | Role | Ends | End frame | Score | Kills / enemy total | Lives left | Script errors | Stalls | Refused spawns | Craters | Replay equal | Screenshots |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | Recon | | yes | 12423 | 97320 | 80 / 85 | 4 | 0 | 0 | 0 | 1 | yes | 20 |
| 2 | Steel Beach | | yes | 13212 | 34210 | 89 / 101 | 2 | 0 | 0 | 0 | 3 | yes | 22 |
| 3 | Toxic Night | | yes | 12840 | 55230 | 49 / 82 | 4 | 0 | 0 | 0 | 1 | yes | 21 |
| 4 | Break-Through | | yes | 13059 | 60780 | 88 / 113 | 4 | 0 | 0 | 0 | 1 | yes | 21 |
| 5 | Chemical Valley | | yes | 12482 | 184650 | 42 / 82 | 3 | 0 | 0 | 0 | 1 | yes | 21 |
| 6 | Abandoned Nuclear Plant | | yes | 12939 | 163500 | 55 / 104 | 2 | 0 | 0 | 0 | 2 | yes | 21 |
| 7 | Mass Destruction | | yes | 12611 | 215700 | 111 / 114 | 3 | 0 | 0 | 0 | 1 | yes | 21 |
| 8 | Thunder Sky | | yes | 12890 | 139140 | 103 / 118 | 3 | 0 | 0 | 0 | 4 | yes | 21 |
| 9 | Secret Labs | | yes | 12967 | 118100 | 45 / 78 | 3 | 0 | 0 | 0 | 1 | yes | 21 |
| 10 | Prototype of Evil | boss 1 | **no: game over at 12147** | – | 17800 | 17 / 43 | 2 | 0 | 0 | 0 | 1 | no (see notes) | 200 |
| 11 | Shooting Practice | bonus | yes | 6861 | 386850 | 39 / 94 | 4 | 0 | 0 | 0 | 0 | yes | 11 |
| 12 | Area 961F | | yes | 12966 | 227700 | 64 / 89 | 2 | 0 | 0 | 0 | 1 | yes | 21 |
| 13 | Shell Shock | | yes | 12735 | 68150 | 71 / 109 | 2 | 0 | 0 | 0 | 3 | yes | 21 |
| 14 | Nuclear Leak | | yes | 12542 | 287500 | 70 / 99 | 5 | 0 | 0 | 0 | 3 | yes | 21 |
| 15 | Devastation | | yes | 12613 | 184700 | 57 / 75 | 2 | 0 | 0 | 0 | 0 | yes | 21 |
| 16 | Highland Area | | yes | 12179 | 249700 | 125 / 134 | 3 | 0 | 0 | 0 | 1 | yes | 20 |
| 17 | Gromigan Strait | | yes | 12240 | 246200 | 85 / 108 | 4 | 0 | 0 | 0 | 1 | yes | 20 |
| 18 | Air-raid drill | bonus | yes | 6936 | 439700 | 45 / 70 | 5 | 0 | 0 | 0 | 1 | yes | 11 |
| 19 | Cannon Fodder | | yes | 12402 | 231900 | 100 / 114 | 3 | 0 | 0 | 0 | 2 | yes | 20 |
| 20 | Forsaken Town | | yes | 12334 | 491000 | 109 / 119 | 3 | 0 | 0 | 0 | 1 | yes | 20 |
| 21 | Urban Trip | | yes | 12002 | 151050 | 84 / 110 | 5 | 0 | 0 | 0 | 2 | yes | 20 |
| 22 | Metal Storm | | yes | 12690 | 295400 | 97 / 125 | 3 | 0 | 0 | 0 | 0 | yes | 21 |
| 23 | Close to Headquarters | | yes | 11759 | 356900 | 65 / 92 | 3 | 0 | 0 | 0 | 1 | yes | 19 |
| 24 | Phoenix Burning | boss 2 | **no** (120000 frames) | – | 84150 | 58 / 109 | 4 | 0 | 0 | 0 | 1 | yes | 200 |

"End frame" is the frame of `EndLevel`. "Kills / enemy total" is the mission-complete
statistic as for AirStrike 2 (load-time spawns left out of the total, kills capped at it).
"Craters" is the number of distinct `TerraMorph` stamps loaded. Every run is with the
upgrades the pilot picks up during that one operation, starting from machine gun level 1,
because our profile has no Gulf Thunder loadout table (see the first item below).

Attract levels: `intro1` and `intro2` each spawn 52 objects at the load and run 3600 frames with
no script error, stall or refused spawn; they call only `sin`, `rotatez`, `random`, `remove`.

## Builtins, errors, missing data

- Script errors, stalls and refused spawns: 0 in every run, attract levels included.
- No unknown global or builtin: every builtin called is marked implemented except `EndLevel`
  (approximate, as for AirStrike 2: the end dialogue and the checkpoint belong to the front
  end) and `GameOver` (approximate) in operation 10. `ShowTutorialHint` is never called
  (Gulf Thunder has no tutorial operation).
- Missing in the shipped data (warnings, not our loaders): operation 8, `tag_gunfire`,
  `tag_rocketfire1`, `tag_rocketfire2` in `models\helics\mi_24\mi_24.mdl` (36 warnings; the
  effects asking for them are detached as issue 113 describes). Operations 10 and 24: 19 and 3
  warnings "Tag '' not found in model ''", an attach with an empty tag on a parent without a
  model (not traced to its script yet; open question 2). No texture, model or script missing.
- Replay: equal in 23 operations. Operation 10 differs only because the game app, after the
  script's `GameOver`, goes on (the plain front end restarts the operation until frame 120000)
  while the simulation stops in the game-over state; up to frame 12147 the two are the same run.

## Per-operation notes

- **1 to 9, 12 to 17, 19 to 23**: play through to the landing pad; terrain, fog, night levels
  (3, 8), rain (8, 14), shadows, tyre tracks, water with boats and ships, craters, health bars
  and the HUD frames look like the original's. Operation 8 ("Thunder Sky") is a purple-lit
  night level with rain, lightning and water; nothing looked broken.
- **11, 18 (bonus)**: short (about 115 s), scored mostly by pick-ups (387 k and 440 k), end on
  the pad.
- **10 (boss 1, "Prototype of Evil")**: the scroll never stops; the boss (a flying machine with
  swept wings) is fought over a base; at frame 12147 the camera passes the level's `game_over`
  placement (`objects\misc.obj` definition `game_over`, script `scripts\misc\game_over.scr`:
  calls `GameOver` and removes itself once the camera is more than 300 units past it) and the
  game ends although the player is invulnerable and has 2 lives. The pilot had destroyed 17
  of 43 enemies with the machine gun at level 1.
- **24 (boss 2, "Phoenix Burning")**: the scroll stops at the boss base (camera scroll speed 0
  in the final state, the base on screen from about frame 12000 to the end) and the fight never ends in 120000 frames (33 minutes): with only the
  machine gun at level 1 the pilot does not destroy the base.

## Comparison with the original (operations 1, 2, 3)

Best alignment (normalised correlation of blurred luma over the scenery): operation 1 0.76 to
0.80, operation 2 0.75 to 0.85, operation 3 0.56 to 0.77 (lower where the original's first
frames had moved on further, and moving objects dominate). The same places come up in the same
order at matching speed; the level geometry, object placement and enemy waves match.

| Region | Original mean RGB | Ours mean RGB | Ratio O/U (R, G, B) |
|---|---|---|---|
| Op 1 scene (100,60)-(700,520) | 45.7 46.5 53.0 | 47.0 47.3 52.1 | 0.97 0.98 1.02 |
| Op 1 river (150,420)-(450,560) | 23.6 29.3 54.5 | 33.5 35.8 56.4 | **0.70 0.82 0.97** |
| Op 1 bank (600,150)-(780,400) | 78.6 64.5 51.1 | 79.5 65.1 51.1 | 0.99 0.99 1.00 |
| Op 2 scene | 122.5 114.5 72.7 | 121.9 113.8 71.7 | 1.01 1.01 1.01 |
| Op 3 scene | 67.6 63.7 39.9 | 68.0 63.2 39.6 | 0.99 1.01 1.01 |

Terrain, fog and lighting match within 3 % (night lighting of operation 3 included). What
differs, visible to a player:

1. **Starting weapon**: the original starts operation 1 with the machine gun at level 4 (four
   bullets in the weapon box), operation 2 with the impulse gun selected, operation 3 with a
   higher weapon selected; ours always starts with the machine gun at level 1.
2. **Helicopter**: the original flies Red Hawk (`player_1`, the save's selection); our runs use
   entry 1 (`player_2`), the tools' default: not a defect of the engine, but an aligned
   comparison should pass the same entry.
3. **River water in operation 1** is lighter and greyer in ours (red +40 %, green +22 %); sand
   shores and the blue sea of operation 2 match. Same family as AirStrike 2's water corrections.
4. **Missile smoke**: our enemy and player rockets draw long, bright, pinkish-white smoke
   trails; the original's are short and thin (small orange streaks in operation 1, short white
   puffs in 2). Particle drawing or the Gulf Thunder particle definitions (not traced).
5. **HUD**: same frames and layout as AirStrike 2's in both; the score and the weapon and
   missile boxes are in the same places.

## What is wrong in Gulf Thunder today

Ordered by how much a player would notice.

| # | Symptom | Cause (as far as known) | Kind | Spec to follow |
|---|---|---|---|---|
| 1 | Every operation starts with the machine gun at level 1; later operations are far too hard; the original gives a fixed loadout per operation (op 1 MG 4; op 2 MG 4 + impulse 5; from op 6 on plasma 7, photon 6, laser 7, big laser 4, plasma laser 4, lightning 4, wave 4) | `rulesGulf` sets `missionLoadout = nullptr`; the executable has a 24-row table (gulf@0x489bc0) | rule value | `docs/spec/gulf/engine-behaviour.delta.md` 8.2 and "Values for GameRules" (`missionLoadout`) |
| 2 | Boss operation 10 ends in a game over when the scroll reaches the `game_over` marker; boss operation 24 cannot be won | most likely item 1: the game rules and every builtin are AirStrike 2's code (symbol-map.md), so the only rule difference that touches the fight is the loadout (from operation 6 on: plasma 7, photon 6, laser 7 and five more weapons instead of a level-1 machine gun); `GameOver` from `game_over.scr` is the original's behaviour when the boss survives the scroll (the builtin is the same code). Not proven until a run with the table | rule value (consequence) | engine-behaviour.delta.md 8.2; re-run after the fix |
| 3 | The HUD shows the wrong weapon icon for slots 1 to 8 and AirStrike 2's level caps (e.g. the impulse gun shows AirStrike 2's impulse cell, the photon gun shows the laser's); the Information pages list AirStrike 2's weapon icons | confirmed in our code: `makeGulf()` in `engine/src/ui/hud_layouts.cpp` inherits AirStrike 2's weapon UV table and `weaponLevelMax`; Gulf Thunder's table is gulf@0x49c0c0 and its caps gulf@0x49c09c = {4, 5, 7, 6, 5, 5, 4, 5, 3} (the HUD code itself is AirStrike 2's) | HUD | `docs/spec/gulf/frontend.delta.md` 4.2 and 4.4; weapon ids engine-behaviour.delta.md 8.2 |
| 4 | Menus are AirStrike 2's plain list (ours); the original's are white and red on a letterboxed, scan-line tinted title with the Gulf Thunder logo; no intro comic in Gulf Thunder | front end not implemented for the sequels (`FrontendStyle::PlainList`) | frontend | `docs/spec/gulf/frontend.delta.md` over `docs/spec/as2/frontend.md` |
| 5 | Portrait dialogues: Gulf Thunder has only three (operation 10 start and end, operation 24 start); with AirStrike 2's rules any dialogue table of AirStrike 2 would be wrong for Gulf Thunder | table gulf@0x49b378 | frontend, rule value | frontend.delta.md (dialogues), engine-behaviour.delta.md 10.2 and 10.3 |
| 6 | Enemy and player rockets leave long bright pink-white smoke; the original's trails are short and thin | not traced; the original's particle update and drawing are AirStrike 2's code (render-pipeline.delta.md, Measurements 2), so the difference is in our particle rendering or in how we read Gulf Thunder's `.ps` definitions | renderer | `docs/spec/gulf/render-pipeline.delta.md` (particles) |
| 7 | River water in operation 1 lighter and greyer than the original's (red channel +40 %) | water colour composition, as in AirStrike 2's render corrections; Gulf Thunder uses its own water textures | renderer | render-pipeline.delta.md (water) over `docs/spec/as2/render-corrections.md` |
| 8 | Campaign: `enableHelic 3` in operations 16 and 19 must unlock nothing and show no "New helicopter" line | our code already ignores it (`Progress::unlockAfterMission` and the plain mission-complete screen test `enableHelic < helicopterCount` = 3), matching the original's bound (gulf@0x426560, gulf@0x426361); nothing to fix | none (checked) | engine-behaviour.delta.md 7.6, 10.6 |
| 9 | Save: the original's `game.bin` payload is 0x5d8 bytes (3 helicopters, 24 missions) | only matters for importing an original save; our own save format is independent | save | engine-behaviour.delta.md 10.5 |
| 10 | "Tag '' not found in model ''" warnings in operations 10 and 24 | an attach with an empty tag; not traced | data or builtin | open question 2 |

## Open questions

1. Whether the boss operations 10 and 24 end once the loadout table is applied (item 2): re-run
   both under the pilot after the fix.
2. Which script attaches with an empty tag in operations 10 and 24, and whether the original
   does the same silently.
3. The alignment of operation 3 is weaker (0.56 to 0.77): the original's shots were taken while
   the owner's checkpoint was active ("Continue", score 193030); the scenery still matches.
4. Behind the menus: the attract levels were checked in the simulation only; rendering them
   behind the front end needs the sequel's menus (item 4).

## Changelog

- 1.0 (F3): first version.
