# Missions status (WP-49)

All twenty missions played to `EndLevel` in the native engine, at commit `cece400` (WP-49,
after the spec issue 120 fix).

## How the runs were made

For each mission N:

1. `as3d_sim --level N --frames 120000 --pilot --record in_N.txt` with `AS3D_GOD_MODE=1`: the
   world-aware test pilot (`as3d::botInput(world, frame)`: lower middle of the play-field,
   lined up under the toughest enemy ahead, over pick-ups, out of the path of enemy fire), god
   mode, seed 1, Normal difficulty. The run's input is recorded as an input script.
2. `as3d_game --headless --level N --input-script in_N.txt --screenshot-every 600` (same
   god-mode flag) replays the input through the game loop and the renderer. Every replay ended
   at exactly the frame the simulation did (rendering does not change the simulation).
3. The same pilot without god mode (`AS3D_GOD_MODE=0`), simulation only, for the last column.

Screenshots: `out/m7/missions/<N>/frame_*.png` of the main checkout (gitignored, not in git).
The CI keeps a short version of step 1: `apps/tests/all_missions_test.cpp` runs 1500 frames of
every mission and requires no script error, no stall, no refused spawn and no stub builtin.

## Table

"End" is the frame of `EndLevel` (60 frames per second). Script errors and stalls were 0 in
every run, and no spawn was refused. Builtins not marked "implemented" that were called:
`EndLevel` (approximate: the level-end screen is still the WP-47 delay, not the menus) in all
missions, and `ShowTutorialHint` (approximate) in mission 1. `Lightning` was not called in any
run: the pilot never picked up the lightning bomb.

| Mission | Name | Night | Ends | End frame | Score | Lives left | Builtins used | Missing assets (warnings) | Without god mode |
|---|---|---|---|---|---|---|---|---|---|
| 1 | Tutorial | | yes | 13684 | 20090 | 2 | 52 | `hlanno2.tga`, `scripts\items\i_help.scr` | completes, 2 lives |
| 2 | Leaving the Home | | yes | 13854 | 23580 | 3 | 55 | `hlanno2.tga` | completes, 3 lives |
| 3 | Desert Strike | yes | yes | 13760 | 42060 | 3 | 50 | `hlanno2.tga` | completes, 3 lives |
| 4 | Area 55 | | yes | 13805 | 47140 | 3 | 54 | `hlanno2.tga`, `scripts\j_cannon.scr`, tags `tag_gun13..16` | game over at 9015 |
| 5 | Millburg | yes | yes | 13723 | 42710 | 3 | 51 | `hlanno2.tga`, tags `tag_gun13..16` | completes, 2 lives |
| 6 | Fear Factory (boss 1) | yes | yes | 55975 | 91750 | 4 | 56 | `hlanno2.tga`, tags `tag_gun13..16` | game over at 23295 |
| 7 | Cold Battle | | yes | 13542 | 54170 | 2 | 56 | `hlanno2.tga`, `scripts\j_cannon.scr` | completes, 0 lives |
| 8 | Snow Storm | yes | yes | 13812 | 40420 | 3 | 54 | `hlanno2.tga` | completes, 2 lives |
| 9 | Great River | | yes | 13718 | 55770 | 2 | 50 | `hlanno2.tga`, `scripts\j_cannon.scr` | completes, 0 lives |
| 10 | Forest Terror | | yes | 13801 | 51940 | 2 | 57 | `hlanno2.tga` | completes, 1 life |
| 11 | Tank City | yes | yes | 13226 | 25310 | 2 | 57 | `hlanno2.tga` | game over at 10668 |
| 12 | Oil Tycoon | | yes | 13694 | 63430 | 3 | 56 | `hlanno2.tga` | completes, 1 life |
| 13 | Small Arms Factories | | yes | 13848 | 93550 | 2 | 57 | `hlanno2.tga` | completes, 2 lives |
| 14 | Interception (boss 2) | yes | yes | 10352 | 113320 | 2 | 56 | `hlanno2.tga` | completes, 0 lives |
| 15 | N.I.T.O. Secret Base | yes | yes | 13424 | 52580 | 2 | 57 | `hlanno2.tga` | completes, 0 lives |
| 16 | Peatbogs | | yes | 13345 | 44000 | 3 | 57 | `hlanno2.tga` | game over at 9000 |
| 17 | The nuclear winter | yes | yes | 13354 | 63850 | 2 | 57 | `hlanno2.tga` | game over at 6743 |
| 18 | End is Near | | yes | 12478 | 66250 | 2 | 57 | `hlanno2.tga` | completes, 0 lives |
| 19 | The BadLands | yes | yes | 13646 | 56100 | 3 | 58 | `hlanno2.tga` | game over at 9692 |
| 20 | N.I.T.O. Headquarters (boss 3) | yes | yes | 81798 | 180000 | 4 | 58 | `hlanno2.tga` | game over at 19275 |

The missing files are missing from the shipped data, not from our loaders:
`models\misc\hlanno2.tga` (a placeholder texture is drawn), two scripts named by the data
but not shipped, and `rocketlauncher_rocketblock.mdl` without `tag_gun13`..`tag_gun16` (the
muzzle flashes of those guns are detached; spec issue 113).

## What the screenshots show

Every mission was looked at through contact sheets of its screenshots, with the boss fights
(6, 14, 20) and the night missions (3, 5, 6, 8, 11, 14, 15, 17, 19, 20) looked at more closely.
Terrain, water, fog, night lighting with the player's and the lamps' dynamic lights,
shadows, particles (smoke, fire, flamethrowers, weather), sprites, the HUD and the enemy and
boss health bars look right. Missions without a boss end with the helicopter landing on the
pad, the HUD hidden; the boss missions end on the boss's explosion.

What still looks or plays wrong, or cannot be judged:

* Boss fights take a long time with the pilot: boss 1 (mission 6) about 42000 frames of
  fighting, boss 3 (mission 20) about 68000. Health goes down steadily and the bosses die, so
  this looks like the pilot's weak weaponry (every mission starts with the level 1 machine gun)
  and boss 1's tanks and launchers soaking up the straight-ahead fire. There is no reference
  for how long the original's fights last (spec issue 113, section 3).
* In mission 20, `boss3_sphere` goes into the leaving state during the fight (spec issue 113,
  section 4); the fight still completes.
* Mission 14 ends earlier (frame 10352, map position 7176) than the others: `EndLevel` comes
  right after boss 2 explodes (last screenshots). Not checked against the original.
* Lightning bolts are drawn and unit tested but never appeared in these runs.
