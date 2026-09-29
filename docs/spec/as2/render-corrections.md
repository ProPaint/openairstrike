# Render pipeline: corrections for AirStrike 2 (`as2`) from the running original

Version 1.0 (V1). Corrects [render-pipeline.delta.md](render-pipeline.delta.md) 1.0 where the
running original (docs/running-originals.md) shows something else; where the two differ, this
document wins for `as2` (and for `gulf`, which uses the `as2` delta). Tags as in
[../README.md](../README.md); a new tag is used here:

| Tag | Meaning |
|---|---|
| `VERIFIED-SCREEN` | measured on aligned screenshots of the original under Wine and of our game, numbers given |

## Method

- The original runs under Wine with software rendering (Mesa llvmpipe behind wined3d), windowed
  800×600, `Brightness=0.6` (the shipped value, `~/tools/wine/games/as2/config.ini`),
  `TextureFilter=0`. Screenshots go to `out/reference/as2/` (gitignored), listed in its
  `index.md`; the aligned pairs are in `out/reference/as2/aligned/`.
- **Alignment.** The original is started on a mission and left without input (the player stays
  in the middle); frames are taken at intervals, some paused. Our game renders the same mission
  headless with no input (`as3d_game --game as2 --headless --level N --god --frames F
  --screenshot-every 10`), one frame every 10 ticks (7 map units). `tools/compare_reference.py
  --best-of` then picks our frame whose blurred luma correlates best with the original's over a
  region of static scenery (normalised correlation, so it finds the place even when colours
  differ), with a vertical shift search. Pairs used here correlate at 0.89 to 0.95. Moving
  things (units, bullets, the player's model) differ; the regions measured avoid them.
- **Measurement.** `tools/compare_reference.py ORIGINAL OURS --region x0,y0,x1,y1,name`: mean
  RGB, standard deviation, luma, ratio of channel means, histograms, difference image,
  side-by-side picture. "O" is the original, "U" ours; means are 0–255 PNG values.
- Where a screenshot disagreed with the delta, the code at the cited address was re-read (the
  device calls with their pushed arguments, and the loader that fills the globals they read).
  Only the corrected behaviour is written here.

## C1. Water colour: which texture is in which stage (delta 12.5)

**The delta says:** stage 0 holds the base texture at `2·(c/4, r/4) + (0.4·sin(t/2) + 0.2,
−0.2·sin(t/4) − 0.3)`; stage 1 the shine texture at `1.5·(c/4, r/4) + (0.4·sin t,
0.4·sin(t/2))`; the result is `lerp(base, shine, shine.a)`.

**The original does:** the stage set-up (as2@0x436440) is as the delta describes (stage 0
selects its texture's colour; stage 1 blends its own texture over the result of stage 0 by
**its own texture's alpha**; the alpha that reaches the frame buffer is the vertex alpha; the
first texture matrix is the ×2 one, the second the ×1.5 one). But the two textures are the
other way round: the caller (as2@0x438860) binds to stage 0 the texture loaded from the level's
**second** name (the shine, level +0x180, stored at as2@0x2103cac by as2@0x41ba70) and to stage
1 the texture of the **first** name (the base, level +0x140, as2@0x2103ca8); the level parser
(as2@0x40d490) copies the first quoted name to +0x140 and the second to +0x180. VERIFIED-CODE.

**Corrected rule:**
- shine layer at `2·(c/4, r/4) + (0.4·sin(t/2) + 0.2, −0.2·sin(t/4) − 0.3)`;
- base layer at `1.5·(c/4, r/4) + (0.4·sin t, 0.4·sin(t/2))`;
- colour = `lerp(shine, base, base.a)` = base·base.a + shine·(1 − base.a);
- alpha = vertex alpha (weight × opacity), unlit, fogged: unchanged.

With the shipped textures: `water_ocean_1` and `water_sand_1` have a mean alpha of 231/255, so
the base shows at about 90 % and the shine adds a faint finer pattern; `water_lava2` is
paletted (alpha 1 everywhere), so **the lava is the base texture alone**. The shine's bright
texels never show as white dots.

Evidence (VERIFIED-SCREEN): the table in section "Measurements", water rows.

**What darkens the water.** Nothing beyond the delta: the water is unlit and fogged, its alpha
is `w × opacity` with opacity 0.4 to 0.5 in the shipped levels (0.7 in one), so about half of
what is seen is the terrain under it (darkened by the underwater factor of 12.2), and the
brightness overlay (C2) multiplies the whole frame by 1.2. With the corrected blend these
reproduce the original within a few percent (lake, sea, night sea, lava), so no extra factor
exists. The "lighting about 0.75" estimated from single screenshots was this half-and-half mix.

## C2. Brightness overlay — the delta is right (1.5), confirmed

The frame is multiplied by `2 × Brightness` after the 2D layer; `Brightness=0.6` (the shipped
`config.ini` value, used in every capture) gives ×1.2. Our game draws it; our viewer's `level`
command did not, which is why the first comparison pictures (`compare_m3_lake.png`, made with
the viewer) showed our ground darker and less saturated. On an aligned mission 3 frame the
viewer without the overlay measured exactly 1/1.2 of the original on grass and fog (ratios
1.20 to 1.23 on every channel); with it, 1.00 to 1.02. VERIFIED-SCREEN. The first game shows
no general difference either (section "The first game").

## C3. Translucent polygons on the sea — in the original too

The pale translucent polygons over mission 8's sea are the `ldina` and `iceberg` objects
(additive blend, no culling, `iceberg` with an ENV_GLITTER environment map), placed by the map.
The original shows them at the same places (aligned frames `m8_s03` to `m8_s06`), with the
same colour: ice floe (240,350)-(380,420) original (42, 56, 83), ours (42, 56, 80). They are
not a fault; nothing is changed. VERIFIED-SCREEN.

## Confirmed without change (VERIFIED-SCREEN)

Measured on aligned pairs, region means, original against ours, ratio of the channel means
within the stated range. Pictures in `out/reference/as2/aligned/`, ours made by the build of
this correction.

| Look | Mission, frame pair | Result |
|---|---|---|
| Terrain colour, detail combine, vertex lighting (day) | 3 `m3_seq_a` / 570; 4 `m4_s01` / 1330, `m4_s03` / 1780 | 0.95–1.03 |
| Terrain at night, lamp light pools (dynamic lights) | 2 `m2_s02` / 510, `m2_s06` / 1500; 18 `m18_s05` / 460 | 0.98–1.05 |
| Snow terrain, fog | 10 `m10_s04` / 1490 | 0.99–1.01 |
| Fog colour and range | 3, 10, 18 (top band) | 0.99–1.02 |
| Shore fade (alpha over 16 units of depth), no foam line | 3, 2 | same shape and softness |
| Things under the water seen through it | 8 (planks), 3 (lake bed) | same mechanism (alpha 0.4–0.5) |
| Boat wakes (curved ripple sprites) | 8 `m8_s06` / 1060 | present in both |
| Shadows: darkness, softness, direction (buildings, palms, helicopters) | 4 `m4_s12` / 3740, 10 `m4_s04` | same at 2× zoom |
| Player rotor disc (Sky Keeper: red blade tips give a red ring) | 4, 10, 18 | present in both |
| Blue flashes on the ground of mission 15 (weather lightning) | 15 | transient in both (3 % of our frames, 1 of 13 of the original's) |

## Measurements

Water, before (the delta's rule) → after (C1), original in the last column; means of RGB.

| Mission, pair, region | Before | After | Original |
|---|---|---|---|
| 3 lake, `m3_seq_a` / 570, (330,180)-(470,300) | 42, 46, 41 | 38, 44, 51 | 37, 45, 52 |
| 8 sea, `m8_s05` / 790, (0,250)-(200,560) | 18, 29, 44 | 13, 27, 57 | 13, 27, 57 |
| 8 sea, same pair, (560,300)-(800,560) | 18, 29, 45 | 13, 27, 58 | 13, 28, 58 |
| 2 night lake, `m2_s02` / 510, (520,380)-(780,560) | 16, 27, 44 | 11, 25, 58 | 11, 26, 58 |
| 15 lava, `m15_s07` / 1970, (100,170)-(230,250) | 37, 27, 40 | 82, 17, 18 | 83, 19, 19 |
| 15 lava, same pair, (640,230)-(780,330) | 35, 26, 38 | 89, 14, 12 | 90, 16, 13 |

The standard deviation of the sea's red channel fell from 16 to 7 (original 7): the white
speckles are gone.

## The first game

The same comparison for AirStrike 3D (`out/reference/as3d/aligned/`, mission 1 `m1_s03`
against our viewer at scroll 998, camera x 702 because the original's player had drifted
right): far water (460,90)-(620,180) original (108, 109, 84), ours (107, 108, 84); sand
regions 0.94–1.03. No general brightness, colour or fog difference: the first game's
rendering is not changed by this document.

## Changelog

- 1.0 (V1): first version.
