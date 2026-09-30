# Missions status: AirStrike II: Gulf Thunder (packages F3, F2)

All twenty-four Gulf Thunder operations and both attract levels run in the native engine
(`gulf`, v2.71 data) with AirStrike 2's rules and Gulf Thunder's counts, its 24-row mission
loadout table (`engine/src/game/game_profiles.cpp`, `rulesGulf`) and its own HUD tables. The
procedure is the one of `docs/missions-status-as2.md`. **All 24 operations end** under the test
pilot in god mode, the two boss operations included (operation 10 at frame 10800, operation 24
at frame 38047). The Gulf Thunder rules found in the executable are in
`docs/spec/gulf/engine-behaviour.delta.md`; this page says what a player would notice and where
it was fixed or is specified.

## How the runs were made

Binaries built at the head of package F2. For each operation N, one run at a time, each binary
under `ulimit -v 4000000` and a `timeout`:

1. `as3d_sim --game gulf --level N --pilot --god --frames 120000 --record in_N.txt
   --builtin-report rep_N.json --dump-state sim_N.json`: the world-aware test pilot, god mode,
   seed 1, Normal difficulty, helicopter entry 1 (`player_2`, the tools' default), 60 frames
   per second.
2. `AS3D_GOD_MODE=1 as3d_game --game gulf --headless --level N --god --input-script in_N.txt
   --frames E --screenshot-every 600 --dump-state game_N.json --no-audio`, E = end frame + 120:
   the same input through the game loop and the renderer.
3. `as3d_sim --game gulf --level N --god --input-script in_N.txt --frames E --dump-state
   simr_N.json`; "Replay equal" = the two dumps are byte-identical.

Attract levels `intro1` and `intro2` (both on map `intro.hsc`): 3600 frames in the simulation
(`--level intro1|intro2`). Screenshots: `out/gulf/missions/<N>/frame_*.png` of the main checkout
(gitignored, never committed); run logs and dumps in `out/gulf/runs2/`. A sample of every
operation (four frames each) was looked at.

Comparison with the original: `tools/run_original.sh gulf -- -god` with every operation
unlocked in the private copy's save, operations 1, 10 and 24 played from the Start Game list,
screenshots in `out/gulf/cmp/` and `out/gulf/cmp24/`; ours rendered headless or with
`as3d_viewer --game gulf level N --scroll S --camx X`, aligned with `tools/compare_reference.py
--best-of` (section "Comparison with the original").

## Table

| Op | Name | Role | Ends | End frame | Score | Kills / enemy total | Lives left | Script errors | Stalls | Refused spawns | Craters | Replay equal | Screenshots |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | Recon | | yes | 12423 | 97320 | 80 / 85 | 4 | 0 | 0 | 0 | 1 | yes | 20 |
| 2 | Steel Beach | | yes | 13225 | 35510 | 92 / 101 | 2 | 0 | 0 | 0 | 3 | yes | 22 |
| 3 | Toxic Night | | yes | 12818 | 63140 | 51 / 82 | 5 | 0 | 0 | 0 | 1 | yes | 21 |
| 4 | Break-Through | | yes | 13059 | 60780 | 88 / 113 | 4 | 0 | 0 | 0 | 1 | yes | 21 |
| 5 | Chemical Valley | | yes | 12461 | 204550 | 58 / 82 | 4 | 0 | 0 | 0 | 1 | yes | 20 |
| 6 | Abandoned Nuclear Plant | | yes | 12684 | 331200 | 89 / 104 | 2 | 0 | 0 | 0 | 2 | yes | 21 |
| 7 | Mass Destruction | | yes | 12618 | 226600 | 109 / 114 | 4 | 0 | 0 | 0 | 1 | yes | 21 |
| 8 | Thunder Sky | | yes | 12881 | 154320 | 111 / 118 | 3 | 0 | 0 | 0 | 4 | yes | 21 |
| 9 | Secret Labs | | yes | 12878 | 201100 | 67 / 78 | 2 | 0 | 0 | 0 | 1 | yes | 21 |
| 10 | Prototype of Evil | boss 1 | yes | 10800 | 127000 | 27 / 43 | 2 | 0 | 0 | 0 | 1 | yes | 18 |
| 11 | Shooting Practice | bonus | yes | 6862 | 631530 | 66 / 94 | 5 | 0 | 0 | 0 | 1 | yes | 11 |
| 12 | Area 961F | | yes | 12851 | 317700 | 82 / 89 | 3 | 0 | 0 | 0 | 1 | yes | 21 |
| 13 | Shell Shock | | yes | 12721 | 96250 | 91 / 109 | 2 | 0 | 0 | 0 | 3 | yes | 21 |
| 14 | Nuclear Leak | | yes | 12515 | 385700 | 86 / 99 | 4 | 0 | 0 | 0 | 3 | yes | 21 |
| 15 | Devastation | | yes | 12642 | 202000 | 66 / 75 | 2 | 0 | 0 | 0 | 0 | yes | 21 |
| 16 | Highland Area | | yes | 12140 | 254900 | 132 / 134 | 3 | 0 | 0 | 0 | 1 | yes | 20 |
| 17 | Gromigan Strait | | yes | 12084 | 256700 | 101 / 108 | 5 | 0 | 0 | 0 | 1 | yes | 20 |
| 18 | Air-raid drill | bonus | yes | 6936 | 797900 | 65 / 70 | 5 | 0 | 0 | 0 | 1 | yes | 11 |
| 19 | Cannon Fodder | | yes | 12225 | 281400 | 112 / 114 | 4 | 0 | 0 | 0 | 1 | yes | 20 |
| 20 | Forsaken Town | | yes | 12286 | 573900 | 117 / 119 | 3 | 0 | 0 | 0 | 1 | yes | 20 |
| 21 | Urban Trip | | yes | 11798 | 182050 | 98 / 110 | 6 | 0 | 0 | 0 | 3 | yes | 19 |
| 22 | Metal Storm | | yes | 12280 | 325400 | 115 / 125 | 3 | 0 | 0 | 0 | 0 | yes | 20 |
| 23 | Close to Headquarters | | yes | 11633 | 461700 | 79 / 92 | 3 | 0 | 0 | 0 | 1 | yes | 19 |
| 24 | Phoenix Burning | boss 2 | yes | 38047 | 174250 | 138 / 159 | 6 | 0 | 0 | 0 | 1 | yes | 63 |

"End frame" is the frame of `EndLevel`. "Kills / enemy total" is the mission-complete
statistic as for AirStrike 2 (load-time spawns left out of the total, kills capped at it).
"Craters" is the number of distinct `TerraMorph` stamps loaded. Every operation starts with its
row of the loadout table (engine-behaviour.delta.md 8.2) and the pick-ups of that one run.

Attract levels: `intro1` and `intro2` each spawn 52 objects at the load and run 3600 frames with
no script error, stall or refused spawn.

The CI keeps a short version: `apps/tests/gulf_missions_test.cpp` runs every operation and both
attract levels for 1500 frames under the same pilot (no script error, stall, refusal or stub)
and plays operation 1 to its end (frame 12423, score 97320, 4 lives);
`apps/tests/gulf_rules_test.cpp` plays operation 24 to its end.

## Builtins, errors, missing data

- Script errors, stalls and refused spawns: 0 in every run, attract levels included. No stub
  builtin is called; no unknown global.
- "Tag '' not found in model ''": 17 warnings in operation 10, none elsewhere. Traced: the
  boss's mortar `boss#2_cannon` (`objects/boss#2.obj`, `FL_NODRAW`, no model, attached to
  `boss#2`) calls `Shoot("boss#2_rocket_wpn", "", dir)` (`scripts\bosses\boss#2\boss#2_cannon.scr`:
  the point string is empty); the weapon's flash is then attached to a shooter without a model
  with an empty tag, and detached (render-pipeline and engine-behaviour rules for a missing tag,
  issue 113). The data does this in the original too (same code, same script); nothing to fix.
- The original logs "ERROR: Unknown object 'sphere_expl'" in operation 24 (its `game.log`): a
  shipped script creates an object that no `.obj` defines; ours ignores the create the same way.
- Missing in the shipped data (warnings, not our loaders): operation 8, `tag_gunfire`,
  `tag_rocketfire1`, `tag_rocketfire2` in `models\helics\mi_24\mi_24.mdl` (issue 113).
- Replay: equal in all 24 operations.

## Per-operation notes

- **1 to 9, 12 to 17, 19 to 23**: play through to the landing pad; terrain, fog, night levels
  (3, 8), rain (8, 14), shadows, tyre tracks, water with boats and ships, craters, health bars
  and the HUD look like the original's. With the loadout the pilot scores higher than in F3's
  runs from operation 3 on.
- **11, 18 (bonus)**: short (about 115 s), scored mostly by pick-ups, end on the pad.
- **10 (boss 1, "Prototype of Evil")**: the boss (`boss#2`, a jeep of 50,000 health with a
  shield phase) is destroyed at frame 10800 and `boss#2_expl.scr` ends the level. The original
  ends the operation the same way; left alone with the machine gun it ends in "Game Over" when
  the scroll passes the level's `game_over` marker with the boss alive (seen under Wine at about
  190 s), which is what F3's runs without the loadout showed in ours.
- **24 (boss 2, "Phoenix Burning")**: the base (`boss#1`, `scripts\bosses\boss#1\boss#1.scr`)
  falls as in the original (played under Wine with `-god`, screenshots `out/gulf/cmp24/p12` to
  `p33`): the four minarets (7000 each) are shot down; each halves the health of the central
  sphere (1,000,000) and the fourth sets it to 1 and damages it, so the script destroys it; the
  core (7000) appears with its shield phases; its death calls `EndLevel` (the original then
  shows the game-complete page). Our run ends at frame 38047 (10.5 minutes: the pilot also
  destroys the two pods of 16,000 first). It never ended before because the test pilot lined up
  under the toughest enemy, the sphere, for good, and never looked at the last minaret 234 units
  to the side; the Gulf Thunder pilot now skips parts of 500,000 health or more and, when the
  scroll has stopped with nothing in its band, looks further to the side
  (`engine/src/input/input_actions.cpp`). No rule, builtin or data fault.
- Operation 5 starts with the plasma gun at level 5 in ours (row: 4): an `item_plasmagun` placed
  at the start is taken while the level loads (`fx_item_taken` present after the load). The
  original's HUD showed the plasma gun at level 6 a few seconds into the operation (more
  pick-ups on the way); whether the first one is taken during the load there was not checked.

## Comparison with the original (operations 1, 10, 24)

Best alignment (normalised correlation of blurred luma over the scenery):

| Pair | ncc | Region | Original RGB | Ours RGB | Ratio O/U |
|---|---|---|---|---|---|
| op 1 `op1_t12` / frame 1720 | 0.80 | scene (100,60)-(700,520) | 45.7 46.5 53.0 | 47.0 47.3 52.1 | 0.97 0.98 1.02 |
| same | | open river (230,290)-(380,340) | 18.3 32.4 62.2 | 20.1 33.7 62.2 | 0.91 0.96 1.00 |
| same | | open river (420,250)-(520,300) | 26.5 37.6 61.8 | 26.4 37.0 61.3 | 1.01 1.02 1.01 |
| op 1 `n4` / frame 615 (launcher rocket trail) | 0.90 | trail rows | 200 168 156 (brightest) | 196 174 160 | same length |
| op 10 `b10_1` / viewer scroll 256 | 0.99 | scene (100,60)-(700,380) | 76.0 59.9 31.8 | 76.1 59.5 31.5 | 1.00 1.01 1.01 |
| same | | river (420,220)-(640,360) | 61.0 55.1 49.4 | 60.7 54.9 49.3 | 1.01 1.00 1.00 |
| op 24 `p12` / viewer scroll 8766 | – | right hangar (590,80)-(780,190) | | | 0.99 1.04 1.01 |
| same | | ground (20,380)-(110,480) | | | 1.03 1.03 1.02 |

The base of operation 24 is at the same place and scale in both; the regions near the sphere and
the shots are lit by their dynamic lights in the original's frame and not in the viewer's, so
only unlit regions are compared. In the middle of operation 10 the original's frames were taken
with explosions and burning stacks lighting the factory; no aligned pair of unlit scenery was
found there.

## What is wrong in Gulf Thunder today

Ordered by how much a player would notice.

| # | Symptom | Cause | Status | Spec |
|---|---|---|---|---|
| 1 | Every operation started with the machine gun at level 1 | `rulesGulf` had no loadout table | **fixed** (the orchestrator's table; checked row by row, `gulf_rules_test.cpp`) | engine-behaviour.delta.md 8.2 |
| 2 | Boss operation 10 ended in a game over, operation 24 never ended | 10: the level-1 machine gun (item 1); 24: the test pilot aimed at the script-killed sphere | **fixed**: 10 ends at 10800, 24 at 38047; checked against the original's operation 24 | this page, per-operation notes |
| 3 | The HUD showed AirStrike 2's weapon icons and level caps; the hint box was the first game's black box | `makeGulf()` inherited AirStrike 2's tables | **fixed**: Gulf Thunder's UV table and caps, the level bar running past the cap (laser 7 over 5, wave gun 4 over 3) as in the original, the hint panel from `interface_gulf.tga` (issue 400 for the Ok button's width) | frontend.delta.md 3.1, 3.15, 4.2, 4.4 |
| 4 | Menus are AirStrike 2's plain list (ours); the original's are white and red on a letterboxed, scan-line tinted title with the Gulf Thunder logo | front end not implemented for the sequels (`FrontendStyle::PlainList`) | open (front-end package) | frontend.delta.md |
| 5 | Portrait dialogues: only three in Gulf Thunder (operation 10 start and end, 24 start) | table gulf@0x49b378 | open (front-end package); the texts are extracted (`dialog.10.*`, `dialog.24.*`) | frontend.delta.md 3.19 |
| 6 | The heat missiles' smoke: none on young rockets and short clumps in the original, a continuous grey trail in ours | not explained by the spec (a particle system on a definition child of a child); the launcher rockets' trails match | open, measured (issue 401) | render-pipeline.md 6.3 to 6.6 |
| 7 | River water of operation 1 "red +40 %" | no fault: the region held a helicopter and rocket trails; open water matches within 0.91 to 1.05 | closed (issue 401) | – |
| 8 | Front-end texts | `tools/extract_exe_texts.py` and the web page read `tools/exe_texts/gulf.json`; 11 of its 230 addresses are not at their string and are left out, some others point at a neighbour | extraction done; `gulf.json` needs correcting (issue 402) | frontend.delta.md 7 |
| 9 | Campaign: `enableHelic 3` in operations 16 and 19 | our code ignores it as the original does | none (checked) | engine-behaviour.delta.md 7.6 |
| 10 | Save: the original's `game.bin` payload is 0x5d8 bytes | only matters for importing an original save | none | engine-behaviour.delta.md 10.5 |
| 11 | "Tag '' not found in model ''" in operation 10 | the data (a model-less mortar shoots from point "") | none (traced) | this page |

## Open questions

1. The heat missiles' smoke (issue 401): how the original emits a particle system held by a
   child of a child.
2. Whether a pick-up at the start of operation 5 is taken during the load in the original too.
3. Behind the menus: the attract levels were checked in the simulation only; rendering them
   behind the front end needs the sequel's menus (item 4).

## Changelog

- 1.0 (F3): first version.
- 1.1 (F2): the loadout table, operation 24 ended (pilot), the Gulf Thunder HUD, all 24
  operations re-run and ending, operations 1, 10 and 24 compared with the original, smoke,
  water and texts measured (issues 400 to 402).
