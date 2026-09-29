# 034: entity geometry the specs leave open

Status: open. Raised by WP-42a. Affects `engine/src/game/world.cpp`,
`engine/src/game/world_think.cpp`.

## 1. Bounding radius (+0x1BF)

engine-behaviour.md §3.2: "bounding radius (from the model, or the sprite extent)". The
formula is not given. Choice: the length of the corner of the model's header box that is
farthest from the model origin (component-wise maximum of |min| and |max|); for sprites
the same over the `min`/`max` quad corners (x, y only; render-pipeline.md §11.3 says the
four numbers are x, y, s, t). The radius drives the frustum test, the activation band and
the leaving test.

## 2. FL_ONGROUND_NORMAL axis

hmap.md "Spawning": the engine samples TerrainHeight at (x−10, y+15), (x+10, y+15) and
(x, y−25) and "builds its axes from that plane and the yaw" (0x4165f0). Choice: up = the
plane normal (pointing up); forward = the yaw direction (cos yaw, sin yaw, 0) made
orthogonal to up; left = up × forward, as rows forward, left, up of the D7 convention.
Recomputed every think (roots only), not only at spawn.

## 3. Tag missing on a definition child

engine-behaviour.md §4.4: a missing tag detaches the child. A definition child is not in
the pool list, so detaching would orphan it; we keep it at the parent's origin instead
(pool entities attached with `AttachEntity` or as muzzle flashes are detached as
specified).
