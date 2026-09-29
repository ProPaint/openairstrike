# 200: `TerraMorph` needs a mutable terrain

Status: open (engine decision). Raised by B3. Affects the terrain, height query, decal and
shadow code once `as2` is supported. Behaviour of the original: rcsl-builtins-semantics.delta.md,
entry `TerraMorph`.

## Facts (VERIFIED-CODE, as2 addresses in the delta)

- The builtin adds (p − 128) × (hmax − hmin) / 255 to the float height of each terrain vertex
  under an 8-bit stamp, one stamp pixel per vertex, skipping vertices outside the grid and,
  in levels with water, vertices below the water level. No clamp; morphs accumulate.
- Only the vertex heights change. The original does not recompute normals, static vertex
  lighting, the height-banded terrain textures, tile overlays or water weights; the
  renderer reads the heights every frame; `TerrainHeight` reads them at once; ground marks
  and baked shadows built earlier keep their old shape.
- The shipped explosions stamp craters 4 to 11 vertices wide (160 to 440 world units) and
  create a `mark` decal right afterwards.

## Choices for our engine

1. The terrain height grid must become mutable per level, and the render mesh must be
   updated for the changed chunks (the original re-reads the whole grid every frame; we may
   upload only the chunks a stamp touched).
2. Lighting: the original keeps the old normals and colours, so a crater is shaded like flat
   ground. Proposed: reproduce that by default (faithful and cheap); recomputing the normals
   and static colours of the touched vertices may be offered as an option later.
3. Collision is screen-space and unaffected. Entities snapped to the ground follow the new
   height at their next think, as in the original.
4. Culling: chunk bounds that assume hmin..hmax may be widened by the stamp's range.
