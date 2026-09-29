# 251: choices made drawing the sequels' water, and what looks doubtful

Status: open (look unconfirmed). Raised by C5. Affects `engine/include/as3d/water.h`,
`engine/src/game/water.cpp`, `engine/src/render/water_renderer.cpp`,
`engine/src/render/terrain_shaders.cpp`. Behaviour: render-pipeline.delta.md 12.5, 12.6;
issue 220.

## Choices

1. **Waves on the GPU, same formula on the CPU** (issue 220 choice 2): the vertex shader
   evaluates `level + 16·w·0.5·(sin(0.75·row + T) + sin(col + T))` from (col, row, terrain z,
   w) per vertex; `waterHeightAt` evaluates the same term for the whole grid (the original only
   refreshes visible chunks, so off-screen boats read stale heights: not reproduced).
2. **Outside the map** `waterHeightAt` clamps to the edge like `Terrain::heightAt` (our
   deviation for `TerrainHeight`, README table); the original returns 0 there.
3. **Triangulation** of a wet cell: the terrain's split, (v00, v10, v11) and (v00, v11, v01);
   the original draws strips per row, whose diagonal was not decoded. Culling off.
4. **Weights and wet cells fixed at load** (issue 220 choice 3); `WaterRenderer::update`
   re-uploads only the terrain height of the non-animated vertices of the touched chunks.
5. **Normal of `waterHeightAt`**: the plane through (x − 10, y + 15), (x + 10, y + 15),
   (x, y − 25) of G_AlignToWater, turned upwards.
6. The chunk culling box spans the level ± 16 and the terrain heights of the chunk.

## Doubtful, to compare with a running original

- **Stage 1 blend order.** The delta gives `lerp(base, shine, shine.a)`. The shipped shine
  textures have alpha above 0.5 on 87 % of their texels (average alpha 206/255), so the shine
  picture dominates: the ocean is mostly the dark shine colours (average RGB 22, 40, 64) and
  the three **lava** levels (`water_lava2`, red) look like dark blue water with red showing
  through (missions 15, 16, 18; 16 has no wet cell at all). If the Direct3D arguments were
  the other way round (`lerp(shine, base, shine.a)`), the lava would be red. The argument
  order of as2@0x436440 should be re-read by a spec package.
- **Shallow water over dark ground.** The terrain under the level keeps the first game's
  underwater darkening (12.2), and the water's alpha only reaches the opacity 16 units below
  the level; where the level is just above a flat bed (mission 4's oasis, level −255) the bed
  is nearly black and shows through the thin water as a black patch.
