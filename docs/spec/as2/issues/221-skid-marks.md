# 221: skid marks (tyre and track trails)

Status: open (engine decision). Raised by B5. Affects the object parser, the world update and
a new render pass. Behaviour of the original: render-pipeline.delta.md section 7.7.

## Facts

- `skid_mark a b w "texture"`: 188 statements on 93 AS2 objects (two per vehicle, left and
  right), textures `gfx/marks/jeepmark1.tga` and `tankmark1.tga`.
- Pool of 64 tracks per level; up to 23 sections each, one every 0.41667 s; fade from 5 to
  10 s; strips at terrain height + 2, BLEND_ALPHA, drawn after the ground marks and before
  the shadows; tracks outlive their vehicle until faded.
- Backward-facing strips are culled in the original.

## Choices proposed

1. Data-driven: any game whose objects carry `skid_mark` gets tracks (the base game has
   none), so no per-game switch is needed beyond the parser accepting the keyword.
2. Update the tracks in the world update after the entity pass, not while paused, as the
   original; store sections in a ring instead of shifting arrays (same result).
3. Reproduce the culling of reversed strips (faithful); drawing both faces is an option if
   it looks wrong in play.
