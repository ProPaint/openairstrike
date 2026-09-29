# 290: visible differences from the original that the renderer cannot fix

Status: open (other packages). Raised by V1 (render corrections from the running original,
[../render-corrections.md](../render-corrections.md)). Found on aligned screenshots of the
original under Wine and of our game headless (`out/reference/as2/aligned/`).

## 1. No tyre or track trails in play

The original lays dark curved trails behind moving vehicles (`m4_s03` behind the tank at
(380–640, 290–330), `m4_s12` near the jeep, `m10_s04` behind the tank at upper right). Our game
draws none: the renderer's skid-trail pass exists (render-pipeline.delta.md 7.7,
`SkidTrailRenderer`, `WorldRenderer::setSkidTrails`) and the viewer's `--skid` option draws
trails, but nothing in the game keeps the trails (engine-behaviour.delta.md 3.1.2, issue 221:
update after the entity pass, 64 tracks, 23 sections). Needs: the track update in the world
(simulation package) and one call per frame `renderer.setSkidTrails(world.skidTrails(),
count)` before drawing (game view or `WorldRenderer::drawWorld`).

## 2. Floating objects do not ride the waves

`FL_ONWATER` entities are placed at the flat water level (`World::snapToGround`), where the
delta 12.6 (VERIFIED-CODE as2@0x40c750) puts them on the animated surface (`waterHeightAt`, in
`as3d/water.h`, already written for this) and tilts `FL_ONWATER_NORMAL` ones with it. With
waves of up to ±16 units, parts of flat floating objects end under the water surface in our
game: mission 3's lily pads are partly hidden (`m3_seq_b` against our frame 860, lower pads),
where the original shows them whole. Needs: the entity sync of the simulation package to use
`waterHeightAt` for `FL_ONWATER` and the plane normal for `FL_ONWATER_NORMAL` (the viewer's
level renderer already does). This changes `as2` state dumps only.

## 3. The mission start dialogue

In the original the level does not scroll while the start dialogue is shown (portrait,
typed text); in our headless game the scroll starts at once and no dialogue is seen in the
frames (mission 3: our frame 570 matches the original's picture taken about 8 s after the
dialogue closed). Front end or game flow, not the renderer; it does not affect the look of a
frame, only when a place is reached.
