# 401: Gulf Thunder rocket smoke and the operation 1 river, measured on aligned frames

Status: open for the smoke of the heat missile (cause not found; nothing changed), closed for
the water (no fault). Raised by F2. Spec: [../render-pipeline.delta.md](../render-pipeline.delta.md)
Measurements 1 and 2; base [../../render-pipeline.md](../../render-pipeline.md) 6.3 to 6.6.

## Method

The original under Wine (`tools/run_original.sh gulf -- -god`, 74 to 102 frames per second
with `ShowFPS=1`), operation 1 started from the Start Game list with no input; ours rendered
headless with no input (`as3d_game --game gulf --headless --level 1 --god --screenshot-every 5`
or 10), pairs chosen by `tools/compare_reference.py --best-of` over (100,60)-(700,520):
`op1_t12` / frame 1720 (ncc 0.80), `n1` / 460 (0.88), `n4` / 615 (0.90). Screenshots are in
`out/gulf/cmp/` and `out/gulf/cmp24/` of the main checkout (not committed).

## Water of operation 1: no fault

The "red +40 %" of `docs/missions-status-gulf.md` came from a region, (150,420)-(450,560),
that holds the player's helicopter (a different one in each run) and, in ours, the smoke of
three rockets. Regions of open water only, pair `op1_t12` / 1720:

| Region | Original RGB | Ours RGB | Ratio O/U |
|---|---|---|---|
| (230,290)-(380,340) mid-river | 18.3 32.4 62.2 | 20.1 33.7 62.2 | 0.91 0.96 1.00 |
| (420,250)-(520,300) | 26.5 37.6 61.8 | 26.4 37.0 61.3 | 1.01 1.02 1.01 |
| (560,420)-(690,500) | 22.7 33.5 59.9 | 21.7 32.6 59.9 | 1.05 1.03 1.00 |

Gulf Thunder's water textures are byte-identical to AirStrike 2's, and the corrected water
rule of `../../as2/render-corrections.md` C1 already applies to both games (same code path).
Nothing to change.

## Rocket smoke

Three kinds of enemy rocket are on screen in operation 1:

1. `e_missile_vsmall` (the ground launchers' rockets): `PS_MISTRAIL_SMALL` (ADD,
   `gfx\smoke.tga`, colour 1.5 1 1) on the rocket itself. Pair `n4` / 615: the trail runs over
   the same rows in both (y 259 to 346 in the original, 259 to 352 in ours) with the same tint
   (brightest pixels 200,168,156 against 196,174,160). **Same.**
2. `e_missile_small` of the helicopters: no particle system, only the `missile_small_hvost`
   flame model. Same look.
3. `e_missile_heat` (the diagonal rockets from the left bank) and `e_missile`: the definition
   child `e_missile_hvost` (flame model, ADD) carries `PS_MISTRAIL_GRAY` (ALPHA,
   `gfx\smoke2.tga`, 30 per second, life 0.35 s, size 3 + 15/s, alpha 0.6 fading). A particle
   system on a child of a child. **Different**: in the original the young rockets show the
   orange flame and no smoke at all (`op1_t12`), older ones show the smoke in separate clumps,
   one at the tail and one about 55 pixels behind with a gap between (`op1_t15`, and operation
   24's `p09`); ours draws a continuous, fairly opaque grey-white trail over the whole 0.35 s
   (about 90 pixels) that hides the flame. By the spec (6.3 to 6.6) our trail is what the
   system should produce: rate, life, size, fade and the frame cells are applied as written,
   and the drawing matches the original for the launcher rockets of item 1. The clumps look like
   emission in bursts at a nearly fixed position, as if the emitter's origin reached the
   instance only every quarter of a second, or the instance were missing for young rockets
   (pool of 256; ours holds 83 instances at that moment). Not explained by the spec; the
   emitter update for a particle system attached to a definition child of a definition child
   (`G_InitObject` recursion, the emitter holder's think and 0x4139e0's copy of the origin)
   needs to be read in the AirStrike 2 executable. Nothing changed in our engine.

The pink tint of ours is item 1's colour 1.5 1 1 clamped per channel after the fade (our shader
clamps the vertex colour to [0, 1], as the first game's GL did): the original shows the same
tint on those trails (ratio red/green 1.19 against our 1.13).

## AirStrike 2 and the first game

AirStrike 2's `weapons_enemy_missiles.obj` has the same `e_missile_hvost` with
`PS_MISTRAIL_GRAY` and the same launcher rocket with `PS_MISTRAIL_SMALL` (byte-identical
blocks), so its heat missiles will show the same difference in ours; not measured on its
original. The first game's rocket trails use other systems (`PS_MISTRAIL` with
`gfx\combust2.tga`) on the rocket itself; nothing was changed in the renderer, so its
regression screenshots are unchanged.
