# 010: particle spawn origin after an emission step

Spec: `docs/spec/render-pipeline.md` section 6.4.

The pseudocode spreads spawn points along the frame's path with
`prevOrigin += step * (origin - start)` and says nothing about what `prevOrigin`
holds once the loop ends. With `n = floor(clock / interval)` and the strict test
`interval < clock`, the loop runs `n` times in the ordinary case (so `prevOrigin`
reaches `origin`), `n - 1` times when the clock lands exactly on a multiple of the
interval, and zero times in frames with no spawn (where `step` is `1/0`).

Choice made in `engine/src/game/particles.cpp`: `prevOrigin` is set to `origin` at
the end of every emission block, so the next frame's spawn path starts where this one
ended and never stretches over several frames. For an emitter that does not move (all
shipped test scenes) the choice has no visible effect. A moving emitter (a missile
trail) may spawn slightly different positions in frames without a spawn if the original
keeps the old `prevOrigin`; checking this needs the disassembly of 0x414600.

A second unspecified detail: `angle += yaw of the axis when oriented` (6.5) does not
say how the yaw is taken. The engine uses `atan2(row0.y, row0.x)` in degrees. Only
DRAW_VERT and DRAW_HORIZ systems read the angle.
