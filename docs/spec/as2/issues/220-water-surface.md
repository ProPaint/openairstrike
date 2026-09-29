# 220: AirStrike 2 water surface

Status: open (engine decision). Raised by B5. Affects `engine/src/game/defs.cpp` (level
parser), the terrain and water renderer, `WaterHeight`, entity water snapping. Behaviour of
the original: render-pipeline.delta.md sections 12.5 and 12.6.

## Facts

- `water "<base>" "<shine>" <level> <opacity>` in all 18 AS2 levels with water; our parser
  reads the v1.70 form (texture, level, opacity), so today AS2 water is invisible and lake
  beds are black.
- The water is a grid on the terrain vertices of the wet cells, alpha = depth weight
  (0 at the shore, 1 at 16 units of depth) × opacity, vertex waves of up to ±16 units, two
  scrolled layers blended by the shine alpha, unlit.
- Entities with `FL_ONWATER` follow the waves; `FL_ONWATER_NORMAL` also tilts with them;
  `FL_ONWATER_FLAT` stays at the level.

## Choices proposed

1. Parse both forms by argument type (a quoted second argument means the AS2 form), per game.
2. Compute the waves on the GPU from (row, col, T) for drawing, and on the CPU with the same
   formula for `WaterHeight` and entity snapping, over the whole grid (the original only
   updates visible chunks, so off-screen entities read stale heights; not worth
   reproducing).
3. Keep the depth weights fixed at level load, as the original (a `TerraMorph` crater under
   the level does not get water).
