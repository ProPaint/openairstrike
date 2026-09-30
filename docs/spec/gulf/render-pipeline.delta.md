# Render pipeline: Gulf Thunder delta (`gulf`)

Delta version 1.0 (package F1). Amends [../as2/render-pipeline.delta.md](../as2/render-pipeline.delta.md)
and [../as2/render-corrections.md](../as2/render-corrections.md) (which amend
[../render-pipeline.md](../render-pipeline.md)) for the game `gulf`. Player-visible
differences only.

Method. (1) Code: every renderer function of Gulf Thunder was compared with its AirStrike 2
counterpart ([symbol-map.md](symbol-map.md)): the frame (`R_BeginFrame`, `R_RenderView`
gulf@0x42f340, `R_EndFrame`), Direct3D state (`D3D_SetDefaultStates` gulf@0x42df40, the blend
cache gulf@0x42e470), terrain and water (`R_UpdateWaterChunk` gulf@0x41c300, the terrain
loader and drawer), the decal and skid-trail passes (gulf@0x42e890, gulf@0x42eba0), shadows,
particles, sprites, models, lights, fog and texture loading are all the **same code**,
read-only constants included. Nothing of the renderer differs in code. (2) Look: the running
original (`tools/run_original.sh gulf`) against our renderer on aligned frames with
`tools/compare_reference.py` (section "Measurements"). Only data can make Gulf Thunder look
different from AirStrike 2: its textures, maps, `levels.txt` lighting and fog, object
definitions and particle systems (the data deltas of this directory).

## Summary for implementers

Render Gulf Thunder exactly as AirStrike 2 ([../as2/render-pipeline.delta.md](../as2/render-pipeline.delta.md)
with [../as2/render-corrections.md](../as2/render-corrections.md)). The visible differences
are the front end's (title bar with scan lines, [frontend.delta.md](frontend.delta.md)) and
what the data brings; see "Measurements" for what was checked on screen.

## Measurements (VERIFIED-SCREEN)

The original (`tools/run_original.sh gulf -- -god`) and our game were run on operations 1, 2
and 3 with the player left alone, and aligned with `tools/compare_reference.py --best-of`
(blurred-luma correlation over the scenery, region (100, 60)–(700, 520), vertical shift search
40 px): operation 1 0.76–0.80, operation 2 0.75–0.85, operation 3 0.56–0.77 (moving objects
dominate the lower scores; the scenery matches). Region means, original / ours:

| Region | Original RGB | Ours RGB | Ratio (R, G, B) | Verdict |
|---|---|---|---|---|
| Op 1 whole scene | 45.7 46.5 53.0 | 47.0 47.3 52.1 | 0.97 0.98 1.02 | same |
| Op 1 river (150,420)–(450,560) | 23.6 29.3 54.5 | 33.5 35.8 56.4 | **0.70 0.82 0.97** | ours lighter and greyer |
| Op 1 river bank (600,150)–(780,400) | 78.6 64.5 51.1 | 79.5 65.1 51.1 | 0.99 0.99 1.00 | same |
| Op 2 whole scene (sea, beaches) | 122.5 114.5 72.7 | 121.9 113.8 71.7 | 1.01 1.01 1.01 | same |
| Op 3 whole scene (night) | 67.6 63.7 39.9 | 68.0 63.2 39.6 | 0.99 1.01 1.01 | same |

Seen the same on screen: terrain texturing and lighting, fog, the night lighting of operation
3, shadows of helicopters and palms, tyre tracks, ships and boats on the water, the brightness
overlay, the soft translucent shores of the sea (reference shots `op2_*` of
`out/reference/gulf/`, index there).

Differences, all from our renderer or the data, none from a Gulf Thunder code change (the code
is AirStrike 2's):

1. **River water of operation 1** (region above): our river is lighter and greyer (red +40 %,
   green +22 %) while the sea of operation 2 matches. Follow AirStrike 2's water rules
   ([../as2/render-corrections.md](../as2/render-corrections.md) C1, the base texture over the
   shine layer through the base's alpha) with Gulf Thunder's water textures and the level's
   `water` line; check the river's texture pair and colour in `maps/levels.txt` first
   (not traced).
2. **Rocket smoke trails**: ours are long, bright, pinkish white; the original's are short thin
   orange streaks (operation 1) and short white puffs (operation 2). The particle code is
   AirStrike 2's (6.1–6.6 same code), so the cause is in our particle drawing or in how we read
   Gulf Thunder's particle systems (not traced; a first check: the blend and colour keys of the
   rocket-trail systems in `particles/*.ps`).

These two are the only renderer items of `docs/missions-status-gulf.md`.

## Checked sections

| Section of ../as2/render-pipeline.delta.md | Status for `gulf` |
|---|---|
| 0 Conventions | same |
| 1.1 Order of a game frame | same code |
| 1.2 Draw lists and sorting | same code |
| 1.3 Viewport, video modes, aspect | same code |
| 1.4 Vertical sync | not checked |
| 1.5 Brightness | same code (overlay seen on screen) |
| 2.1 Baseline state, projection and view | same code |
| 2.2 Fog | same code; values from levels.txt |
| 2.3 Sun, 2.4 Dynamic lights, 2.5 Night levels | same code |
| 3.1 to 3.5 Object geometry | same code |
| 4.1 to 4.4 Materials | same code |
| 5.1 to 5.4 Shadows | same code |
| 6.1 to 6.6 Particles | same code; our rocket trails differ on screen (Measurements 2) |
| 7.1 to 7.9 Special effects | same code (skid marks seen on screen in the reference shots) |
| 8.1 to 8.4 2D rendering | same code (the screens' content: frontend.delta.md) |
| 9.1 to 9.3 Resources | same code |
| 10 Shader plan | same |
| 11 Limits | same |
| 12.1 to 12.6 Terrain and water | same code; our operation 1 river differs on screen (Measurements 1) |
| render-corrections.md C1 to C3 | apply (same code) |

## Changelog

- 1.0 (F1): first version.
