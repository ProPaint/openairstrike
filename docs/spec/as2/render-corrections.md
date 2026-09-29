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

Evidence (VERIFIED-SCREEN), before → after this correction, see the numbers in the table of
section "Measurements".

## Changelog

- 1.0 (V1): first version.
