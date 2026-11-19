# Missions status: AirStrike 2 (package C6)

All eighteen AirStrike 2 missions played to `EndLevel` in the native engine, and both attract
levels run, at the head of package C6 (`as2`, v2.51 data). The first game's table is
`docs/missions-status.md`; the procedure is the same.

## How the runs were made

For each mission N:

1. `as3d_sim --game as2 --level N --pilot --god --frames 120000 --record in_N.txt
   --builtin-report rep_N.json`: the world-aware test pilot (`as3d::botInput(world, frame)`),
   god mode, seed 1, Normal difficulty, helicopter entry 1 (`player_2`, the tools' default).
   For the sequels the pilot reads touch modes as bit sets, dodges only below half its health
   and skips pick-ups it cannot reach (issue as2/273 §10); the first game's pilot is unchanged.
2. `AS3D_GOD_MODE=1 as3d_game --game as2 --headless --level N --input-script in_N.txt
   --frames E --screenshot-every 600 --dump-state game_N.json`, with E = the end frame + 120,
   replays the input through the game loop and the renderer (water, craters, skid trails,
   shadows all drawn).
3. `as3d_sim --game as2 --level N --god --input-script in_N.txt --frames E --dump-state
   simr_N.json`: the simulation alone on the same input. "Replay equal" says the two state
   dumps are byte-identical: rendering, craters and skid trails included, never changes the
   simulation.

The attract levels `intro1` and `intro2` were run 3600 frames in the simulation (no end: they
loop behind the menus) and behind the plain front end (`as3d_game --game as2 --headless
--ui-script ... --attract 1|2`).

Screenshots: `out/as2/missions/<N>/frame_*.png` and `out/as2/missions/intro1|intro2/` of the
main checkout (gitignored, never committed). The CI keeps a short version:
`apps/tests/as2_missions_test.cpp` runs every mission and both attract levels for 1500 frames
under the same pilot and requires no script error, no stall, no refused spawn and no stub; the
same file checks that drawing every frame of mission 2 with craters and live skid trails
leaves the state dump unchanged and that a crater changes the drawn terrain.

## Table

| Mission | Name | Role | Ends | End frame | Score | Kills / enemy total | Lives left | Script errors | Stalls | Refused spawns | Craters (stamps) | Replay equal | Screenshots |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | Tutorial | tutorial | yes | 13448 | 15390 | 34 / 40 | 2 | 0 | 0 | 0 | 2 | yes | 22 |
| 2 | Riot Call |  | yes | 12232 | 21850 | 56 / 61 | 3 | 0 | 0 | 0 | 1 | yes | 20 |
| 3 | Lost Paradise |  | yes | 13364 | 44220 | 78 / 85 | 2 | 0 | 0 | 0 | 3 | yes | 22 |
| 4 | Sand Assault |  | yes | 13832 | 40290 | 78 / 89 | 3 | 0 | 0 | 0 | 1 | yes | 23 |
| 5 | Blood'n'Oil |  | yes | 13235 | 62720 | 83 / 86 | 2 | 0 | 0 | 0 | 1 | yes | 22 |
| 6 | Machine of hate | boss 1 | yes | 9038 | 5530 | 27 / 37 | 2 | 0 | 0 | 0 | 0 | yes | 15 |
| 7 | Gold Isle | bonus | yes | 6193 | 129660 | 35 / 57 | 2 | 0 | 0 | 0 | 1 | yes | 10 |
| 8 | Deep Strike |  | yes | 13104 | 57340 | 66 / 77 | 3 | 0 | 0 | 0 | 3 | yes | 22 |
| 9 | Dark Water |  | yes | 13744 | 51690 | 74 / 81 | 2 | 0 | 0 | 0 | 3 | yes | 23 |
| 10 | Frozen Fury |  | yes | 13624 | 87920 | 100 / 104 | 2 | 0 | 0 | 0 | 1 | yes | 22 |
| 11 | Flashpoint |  | yes | 13047 | 28920 | 48 / 82 | 2 | 0 | 0 | 0 | 3 | yes | 21 |
| 12 | Flying Beast | boss 2 | yes | 4734 | 0 | 2 / 8 | 3 | 0 | 0 | 0 | 0 | yes | 8 |
| 13 | Treasures of Ancients | bonus | yes | 6002 | 166520 | 59 / 68 | 2 | 0 | 0 | 0 | 1 | yes | 10 |
| 14 | Close Combat |  | yes | 13421 | 148700 | 71 / 81 | 2 | 0 | 0 | 0 | 2 | yes | 22 |
| 15 | Burning Wasteland |  | yes | 13388 | 70530 | 43 / 77 | 3 | 0 | 0 | 0 | 2 | yes | 22 |
| 16 | Industrial Scar |  | yes | 13629 | 52860 | 36 / 84 | 2 | 0 | 0 | 0 | 1 | yes | 22 |
| 17 | Canyon Crisis |  | yes | 14024 | 21750 | 31 / 96 | 4 | 0 | 0 | 0 | 3 | yes | 23 |
| 18 | Judgement Day | boss 3 | yes | 52227 | 10000 | 15 / 59 | 2 | 0 | 0 | 0 | 3 | yes | 87 |


"End frame" is the frame of `EndLevel` (60 frames per second). "Kills / enemy total" is the
mission-complete statistic: the enemy total leaves out the objects spawned while the level
loads (rows 0 to 20, as the original, issue as2/211) and kills are capped at it, so the ratio is
not the share of enemies destroyed. "Craters" is the number of distinct `TerraMorph` stamps
the level loaded (every mission but the two first bosses made craters).

Attract levels: `intro1` (map `intro2.hsc`) spawns its 67 objects and `intro2` (map
`intro1.hsc`) its 88 at the load (spawnAllOnIntermission); 3600 frames each with no script
error, stall, refused spawn or builtin that is not implemented (5 and 11 builtins called).

## Two players

Co-op ("Cooperative" in Start Game, as2/frontend.md 3.4, 5.9) is playable on the desktop: a
helicopter per player on the selection screen's "Player:" spinner, `G_NewGame` and the restarts
for both records, the loadout for both, the two-player HUD (4.3), Mission Complete without the
statistics, game over when both are out, no high-score check, the checkpoint and Next with both
players' lives, scores and upgrades. The web and Android still offer one player (their input,
not the game).

Every mission was played with two players, each flown by its own world-aware pilot
(`as3d_sim --game as2 --level N --players 2 --two-pilots`, `as3d::botInputPlayers`: player 2's
home is 70 units right of player 1's; player 1 in `player_2`, the tools' default entry 1,
player 2 in `player_1`; seed 1, Normal): once in god mode to the end (up to 60000 frames), once
without god mode for 20000 frames. "Two pilots, god" is the frame of `EndLevel`; "Two pilots,
mortal" is how the run without god mode ended. No run had a script error, a stall, a refused
spawn or a forced respawn. The CI keeps a short version: `apps/tests/two_player_missions_test.cpp`
runs every mission with two pilots for 2400 frames in god mode and 1800 frames at Nightmare.

| Mission | Two pilots, god | Two pilots, mortal | Script errors | Stalls | Forced respawns |
|---|---|---|---|---|---|
| 1 | 13448 | ends at 13448 | 0 | 0 | 0 |
| 2 | 12232 | ends at 12232 | 0 | 0 | 0 |
| 3 | 13377 | ends at 13377 | 0 | 0 | 0 |
| 4 | 13832 | ends at 13848 | 0 | 0 | 0 |
| 5 | 13235 | ends at 13249 | 0 | 0 | 0 |
| 6 | 9975 | game over at 8427 | 0 | 0 | 0 |
| 7 | 6232 | ends at 6232 | 0 | 0 | 0 |
| 8 | 13103 | ends at 13116 | 0 | 0 | 0 |
| 9 | 13761 | ends at 13761 | 0 | 0 | 0 |
| 10 | 13624 | ends at 13624 | 0 | 0 | 0 |
| 11 | 13047 | ends at 13066 | 0 | 0 | 0 |
| 12 | 4005 | ends at 4003 | 0 | 0 | 0 |
| 13 | 6018 | ends at 6018 | 0 | 0 | 0 |
| 14 | 13435 | ends at 13435 | 0 | 0 | 0 |
| 15 | 13402 | ends at 13414 | 0 | 0 | 0 |
| 16 | 13629 | ends at 13629 | 0 | 0 | 0 |
| 17 | 14040 | ends at 14040 | 0 | 0 | 0 |
| 18 | 20797 | game over at 14562 | 0 | 0 | 0 |

Compared with the original under Wine (2026-11-19, `tools/run_original.sh as2`, screenshots in
`out/reference/as2/coop/` of the main checkout, not committed): the Game mode spinner
("Cooperative"), the selection screen's "Player: Player 1 / Player 2" spinner at y 441 with the
20 px taller panel, and the two-player HUD's positions (player 2's score at (600, 51) in its
mirrored frame, its weapon icon at (719, 91), its missile column on the right with the count's
right end at x 780) are as ours. Player 2's health bar and its weapon and missile frames (the
mirrored quads of 4.3) were missing under Wine (its mirrored score frame does show, as in issue
as2/280 item 1), but the owner remembers the original showing both health bars, one on each
side: a Wine run effect. Ours draws them, as 4.3 reads. Mission Complete with two players was not reached in the original.

## Builtins, errors, missing data

- Script errors, stalls and refused spawns: 0 in every run.
- Builtins called that are not marked implemented: `EndLevel` in every mission (approximate:
  the end dialogue, the level-end sequence and the use of the checkpoint belong to the front
  end, issue as2/232) and `ShowTutorialHint` in mission 1. `TerraMorph` and `WaterHeight` are
  implemented now; `GameOver` was not called.
- Missing in the shipped data (warnings, not our loaders): mission 6, `tag_fire01..04` in
  `models\helics\helic_small\helic_small3.mdl`; mission 18, `tag_firebig` in
  `models\bosses\boss_3_helic\helic_base.mdl` (the effects asking for them are detached, as
  issue 113 describes for the first game). No texture, model or script was missing.

## What the screenshots show

Contact sheets of every mission were looked at, full frames of the sea levels (8, 9, 14), the
boss fights (6, 12, 18), the bonus levels (7, 13), the snow levels and the lava levels.

- Terrain, fog, night lighting, shadows, particles, weapons (lightning bolts in mission 7,
  the wave gun in 11, the flamethrower from 14 on), health bars and the landing pads at the
  end of the ordinary missions look right.
- **Water and boats**: ships, boats, ice floes and debris ride the animated surface; the
  aircraft carrier and the big ships (`FL_ONWATER_FLAT`) stay at the still level; the
  destroyers (`FL_ONWATER_NORMAL`) pitch and roll with the waves.
- **Craters**: explosions dent the ground where they happen (the terrain and water meshes
  follow at the next frame; checked in pixels by `apps/tests/as2_missions_test.cpp`).
- **Skid trails**: tanks and jeeps leave paired fading tracks behind them, along their
  direction of travel (clearest on the snow of mission 10).
- **Bosses**: boss 1 (mission 6) is destroyed at frame 9038; boss 2 (mission 12) ends when its
  cabin, the vulnerable part, is shot down (4734; the other parts have no score, so the
  mission scores 0 with this pilot); boss 3 (mission 18) is a long fight: the train, then the
  base's turrets and sphere (frames 12000 to 46000), then the helicopter; it ends at 52227.
  There is no reference for how long the original's fights last.
- **Bonus levels** (7, 13): short (about 100 s), scored mostly by the treasure pick-ups
  (130000 and 167000 points), ending on the landing pad.

What still looks or plays wrong, outside this package:

- The three lava levels (15, 16 and the ground of 18) draw their `water_lava2` surface as dark
  blue water with red specks, the blend order doubt of issue as2/251 (renderer).
- The HUD is the first game's (another package is writing the sequel's).
- Without god mode the pilot is not a measure of difficulty; not run here.
