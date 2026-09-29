# 120: the player cannot fire when partly off screen

Status: answered (spec correction). Raised by the owner's play test. Affects
`engine/src/game/collision.cpp` (on-screen bit), and every user of runtime bit 0x08
(`Shoot`, `TraceLine`, `TraceLineDamage`, `LockTarget`, `Lightning`, particle damage, the
touch pass).

## Symptom

On desktop, with the helicopter partly outside the window (left, right or bottom), the fire
sound plays but no projectile appears and nothing is hit. In the original, firing works
there.

## Cause (VERIFIED-CODE)

The specs describe the on-screen test for model entities the wrong way round. The original
sets runtime bit 0x08 when the projected box **touches** the window (any part visible), not
when it lies **entirely inside** it.

- `R_RectOnScreen` (0x419920) receives the rectangle **maximum** (+0x1CF) as its first
  operand and the **minimum** (+0x1C3) as its second (call site 0x4059b3..0x4059b7; the
  maximum is the buffer initialised to −9999 in 0x419630, the minimum the one initialised
  to +9999). It returns true when max.x ≥ 0, min.x < video width, max.y ≥ 0 and
  min.y < video height. That is an overlap test with the window.
  `engine-behaviour.md` §5.1 and `render-pipeline.md` §9.3 read it as "min ≥ 0 and max ≤ /
  < size".
- The player script plays the sound first and then calls `Shoot` with `self` = the root
  helicopter: `player_a.scr` pc 20 `StartSound`, then pc 26, 30, 34… `Shoot` (VERIFIED-DATA).
  `Shoot` (0x41b0d0) returns at once when the shooter's bit 0x08 is clear (0x41b0db). So
  with the containment rule the sound plays and no projectile is made, which is exactly
  the symptom.
- At every limit the original allows, the helicopter's projected box crosses the window
  border (worked example below). The containment rule clears the bit there; the original's
  overlap rule keeps it set.

The implementer's other choices (bbox pivot at the model origin, no entity scale, fixed
800×600 viewport) match the original at 4:3 and are not the cause.

## Findings

### 1. Who computes bit 0x08, and who shoots

- `G_ComputeScreenBounds` (0x405930) is the only writer of bit 0x08 besides the parent
  copy. `G_ThinkEntity` (0x405a60) calls it for roots, and for children with runtime bit
  0x20 (`AttachEntity`). Other children copy the parent's bit 0x08 (0x405ad6..0x405ae2).
  VERIFIED-CODE.
- It has no special case for players, player index or definition flags. The only exemption
  is class 0 with touch filter 0 (bit forced on, 0x405930..0x405947). Class is read as
  float field 2 (+0x83) and truncated; player helicopters are class 1 with no touch filter,
  so they take the normal path. VERIFIED-CODE.
- Shooter = the root helicopter (`self` of the player script's `main`). Muzzles are tags,
  not entities. Mounted children (`FLAMETHROWER` etc.) are not used as shooters by
  `player_a.scr`. VERIFIED-DATA.

### 2. The exact test (0x405930)

1. If truncate(class) = 0 and touch filter (+0x5B) = 0: set bit 0x08; stop.
2. Clear bit 0x08. Sphere test `R_CullSphere` (0x419970) with centre = origin (fields 5..7,
   +0x8F) and radius +0x1BF: for each of the six planes of the previous frame (0x1FA7D68,
   16 bytes per plane), d = n·c + plane.w + radius. If d ≤ 0 for any plane, stop (bit
   stays clear, rectangle not updated). VERIFIED-CODE.
3. Model entity (type +0x113 = 0) without flag 0x1000 (read as float field 3, +0x87,
   truncated): project the box (0x419630), then set bit 0x08 iff
   `max.x >= 0 && min.x < W && max.y >= 0 && min.y < H` (0x419920), with W, H the
   integer video mode size (0x1FA9210/0x1FA9214). VERIFIED-CODE.
4. Otherwise (sprite, mark, point collision): max := previous min, then min := projected
   origin (0x419540). Bit 0x08 set iff `0 <= x < W && 0 <= y < H` (0x4198d0; the lower
   bound is inclusive, not strict as `render-pipeline.md` §9.3 says). If the old max was
   (0, 0) (first frame), max := min. VERIFIED-CODE.

Box projection (0x419630), settled:

- Box = the MDL header box of the entity's current model (model record +0x84..+0x98, the
  file's 0x60..0x74), model index at entity +0x153. VERIFIED-CODE.
- Corner i (i = 0..7): (bs.x·(i&1 ? max.x : min.x), bs.y·(i&2 ? max.y : min.y),
  bs.z·(i&4 ? max.z : min.z)), bs = `bbox_scale` (+0x5F, default 0.7). **Pivot = model
  origin** (component-wise multiply); this settles the GUESS in `engine-behaviour.md`
  §5.1. VERIFIED-CODE.
- Placed with the render record's axis rows (+0x12B..+0x14B, i.e. entity +0x113 +0x18..+0x38)
  and origin (+0x11F), no entity scale, then C (projection × view of the previous frame).
  Pixel: x = vp.x + (vp.w·cx/cw + W)/2, y = vp.y + (vp.h·cy/cw + H)/2 (y up). No guard for
  cw ≤ 0: a corner behind the eye gives a mirrored or huge value; the original does
  nothing about it. VERIFIED-CODE.
- Min/max update quirk: per corner, if value < min then min := value, **else** if
  value > max then max := value (a value that lowers the minimum never raises the
  maximum). For a box this only matters if the first corners are visited in decreasing
  order; the result is otherwise the plain min/max. VERIFIED-CODE.
- Model without a model index (+0x153 = 0): min = max = projected origin. VERIFIED-CODE.

Bounding radius (+0x1BF), resolves issue 034 §1: set at creation (0x409ba0, 0x409f0b).
For a model entity: the model record's +0x9C, computed at load (`R_LoadModel` 0x4116f0,
0x411d42) by `R_ModelRadius` (0x411d70) = the largest distance from the model origin to
the 8 corners of the header box. Not scaled by `bbox_scale` or entity scale. For other
types the same function runs on the sprite extents (+0x177 / +0x18B). The WP-42a choice in
034 is mathematically identical. VERIFIED-CODE. (Whether a later model change updates the
radius was not checked.)

### 3. Player limits (VERIFIED-DATA and VERIFIED-CODE)

- y: `player_a.scr` pc 1164..1179 clamps origin.y to [g_map_pos + 35, g_map_pos + 320].
  VERIFIED-DATA.
- x: `V_UpdateCamera` (0x40c260; 0x40c480 per player in two-player mode, 0x40c59d
  otherwise) calls `V_ClampToFrustumX` (0x419b10) on the player's origin (+0x8F..+0x97)
  with margin 10.0 (float at 0x44b944), in world units. For plane k (a, b, c, d), let
  x_k = (−d − b·y − c·z)/a at the origin's y and z. Plane 1 (0x1FA7D78, left) gives the
  lower bound x_1 + 10, plane 0 (0x1FA7D68, right) the upper bound x_0 − 10. Only the
  origin point is tested; the model's width is ignored. VERIFIED-CODE.
- Player z = 100 (spawn, player.scr); tilt: angles[14] = 30·vy/150, angles[15] =
  −25·vx/150 (pc 1181..1194). VERIFIED-DATA.

### 4. Worked example (default camera mode 1, Apache)

Camera: eye (cx, g, 270), view tilted 45° from straight down toward +y, vertical FOV 60°,
4:3. Focal length in pixels f = (H/2)/tan 30° = 519.6 at 800×600 (665.1 at 1024×768).
For a point with offsets (dx, dy, dz) from the eye: depth = 0.7071·(dy − dz),
up = 0.7071·(dy + dz), winX = W/2 + f·dx/depth, winY = H/2 + f·up/depth.

Apache box (apache.mdl header, VERIFIED-DATA): min (−13.66, −39.61, −3.70),
max (13.76, 35.51, 13.55); scaled by 0.7: x −9.56..9.63, y −27.73..24.86, z −2.59..9.49.
Radius = |(13.76, −39.61, 13.55)| = 44.07.

**Bottom limit, at rest, 800×600** (origin (640, g + 35, 100), camera x 640):
- origin: dy = 35, dz = −170: depth 144.96, up −95.46, winY = 300 − 519.6·0.6585 = −42.2
  (the origin itself is below the window).
- nose-top corner (y +24.86, z +9.49): dy 59.86, dz −160.51: depth 155.8, up −71.2,
  winY = 62.6.
- tail-bottom corner (y −27.73, z −2.59): dy 7.27, dz −172.59: depth 127.2, up −116.9,
  winY = −177.6.
- Rectangle y: [−177.6, 62.7]. Containment: min.y < 0, **bit clear** (ours). Overlap:
  max.y = 62.7 ≥ 0, min.y < 600, **bit set** (original).
- Sphere: the origin is 35·cos 15° − 170·sin 15° = −10.2 units outside the bottom plane;
  −10.2 + 44.07 > 0, so the sphere test passes.

**Right limit, at rest, 800×600** (camera x at its clamp 702, origin y = g + 35):
- frustum half-width at depth 144.96: 144.96 · (4/3)·tan 30° = 111.59, so the right plane
  is at x = 813.59 and the clamp gives x = 803.59.
- origin winX = 400 + 519.6·101.59/144.96 = 764.1.
- tail-top-right corner (x +9.63, y −27.73, z +9.49): dx 111.22, depth 118.6,
  winX = 400 + 519.6·0.9375 = 887.1.
- Rectangle x: [690.9, 887.1]. Containment: max.x ≥ 800, **clear**. Overlap: min.x <
  800, **set**.

All limits (from `re/ghidra_scripts/scratch_edge_fire.py`, rectangle as (min.x, min.y) –
(max.x, max.y), y up):

| Case (y = g + 35) | 800×600 rectangle | 1024×768 rectangle | Containment | Overlap |
|---|---|---|---|---|
| left, x = 476.41, at rest | (−86.8, −177.6) – (109.3, 62.7) | (−111.1, −227.3) – (139.9, 80.2) | clear | set |
| left, moving left (roll +25°) | (−86.2, −178.5) – (104.4, 66.8) | (−110.3, −228.4) – (133.7, 85.5) | clear | set |
| right, x = 803.59, at rest | (690.9, −177.6) – (887.1, 62.7) | (884.4, −227.3) – (1135.5, 80.2) | clear | set |
| right, moving right | (695.7, −178.5) – (886.3, 66.8) | (890.5, −228.4) – (1134.5, 85.5) | clear | set |
| bottom, x = 640, at rest | (358.1, −177.6) – (442.2, 62.7) | (458.4, −227.3) – (566.0, 80.2) | clear | set |
| bottom, moving back (pitch −30°) | (361.4, −183.9) – (438.9, 56.5) | (462.5, −235.4) – (561.8, 72.3) | clear | set |

Further: at the right limit the rectangle crosses x = W at every y of the band (max.x = 887
at g + 35, 832 at g + 320, 800×600), and with the containment rule the bottom edge is
crossed for every y below g + 74. So our engine refuses fire along the whole side limits
and in the lower 39 units of the band. The Comanche gives the same verdicts
(comanche.mdl box). 1024×768 has the same aspect, so the verdicts are identical and the
pixels scale by 1.28.

### 5. Enemies

The same rule applies to every entity: an enemy whose projected box touches the window
(and whose bounding sphere is inside the frustum) has bit 0x08, so in the original it **can
fire and can be hit** while partly off screen. Hits are tested against the unclipped
rectangle, so a projectile can hit the off-screen part. It loses the bit only when its box
no longer touches the window, or its sphere leaves the frustum. `Shoot` also refuses
shooters in activation state 2 (leaving). The spec line "an enemy partly outside the window
is immune" is wrong. VERIFIED-CODE (same function, no class-specific path).

## Required engine behaviour

1. **Bit 0x08, scenery.** If truncate(class) = 0 and touch filter = 0: set the bit, do not
   compute a rectangle.
2. **Sphere gate.** Otherwise clear the bit; if n·origin + w + radius ≤ 0 for any of the six
   frustum planes of the collision camera (previous frame), stop there (bit clear,
   rectangle kept from before).
3. **Model box.** For a model entity without flag 0x1000: corners = header-box bounds × bbox_scale
   component-wise (pivot = model origin), transformed by the entity axis and origin (no
   entity scale), projected with the collision camera to pixels with y up. Rectangle =
   min/max of the 8 corners.
4. **Overlap, not containment.** Set bit 0x08 iff max.x ≥ 0 and min.x < W and max.y ≥ 0
   and min.y < H, where W×H is the collision viewport (800×600 under our deviation).
5. **Point colliders.** Bit set iff 0 ≤ x < W and 0 ≤ y < H for the projected origin
   (lower bound inclusive).
6. **Radius.** radius = max over the 8 header-box corners of |corner| (unscaled).
7. **Children** without bit 0x20 copy the parent's bit 0x08; the others follow rules 1–5.
8. **Player x clamp.** origin.x is clamped to [x_left + 10, x_right − 10], where x_left and
   x_right solve the left and right frustum planes of the **same** camera used for rules
   2–4, at the origin's y and z.

Test cases (collision viewport 800×600, camera mode 1, Apache, bbox_scale 0.7, z = 100,
g = g_map_pos; tolerance about 0.5 px):

| Test | Setup | Expected bit 0x08 |
|---|---|---|
| T1 | origin (640, g + 35, 100), camera x 640, angles 0 | set; rectangle y ≈ [−177.6, 62.7] |
| T2 | origin (803.59, g + 35, 100), camera x 702 | set; rectangle x ≈ [690.9, 887.1] |
| T3 | origin (476.41, g + 35, 100), camera x 578 | set; rectangle x ≈ [−86.8, 109.3] |
| T4 | T1, player fires | a projectile is created (sound and projectile) |
| T5 | enemy whose rectangle is (780, 300) – (860, 360) | set; can fire, can be hit |
| T6 | enemy whose rectangle is (801, 300) – (860, 360) | clear; `Shoot` does nothing |
| T7 | rectangle with max.x = 0 exactly | set (≥ 0 is inclusive) |
| T8 | rectangle with min.x = 800 exactly | clear (< W is strict) |

## Corrections to other specs

- `engine-behaviour.md` §5.1: replace "Bit 0x08 is set only if the whole rectangle is inside
  the window: min ≥ 0 and max < width/height" with the overlap rule (rule 4). Replace the
  pivot GUESS with "model origin, VERIFIED-CODE". The point test is 0 ≤ x < W, not strict.
- `engine-behaviour.md` §5.1 consequences: delete "Only objects entirely on screen can be
  hit. An enemy partly outside the window is immune." Replace it with "Objects are
  collidable while any part of their projected box touches the window."
- `engine-behaviour.md` §3.3 bit table, 0x08: "on screen" means "box touches the window".
- `engine-behaviour.md` §7.3: the x clamp applies to the origin point only, margin 10.0
  (0x44b944); plane 0 = right (upper bound), plane 1 = left (lower bound).
- `render-pipeline.md` §9.3: 0x419920 is "the rectangle overlaps the window (max ≥ 0,
  min < size)"; 0x4198d0 is "0 ≤ p < size". Note the min/max update quirk.
- `rcsl-builtins-semantics.md` `Shoot` step 1: "off-screen shooters" means shooters whose
  box does not touch the window; a partly visible shooter fires.
- `re/symbols_v170_render.csv` and `re/symbols_v170.csv` rows for 0x419920 say "rectangle
  inside"; corrected rows are appended to `re/symbols_v170.csv`.
- `docs/spec/README.md` deviation "fixed 800×600 viewport": this is correct only if the
  player's x clamp (rule 8) uses the same 4:3 collision camera. If the rendered frustum is
  wider than 4:3 and the clamp uses it, the helicopter can move past the collision
  viewport and lose bit 0x08 even with rule 4. GUESS (consequence of the deviation, not
  original behaviour).
- Issue 034 §1: answered (rule 6, same as the WP-42a choice). Issue 030: the "corner
  behind the eye" choice (treat as not projectable) is a deviation. The original has no
  guard. It cannot occur for the player or for ground and air units in the play band.

## Remaining uncertainty

- The owner's observation is fully explained by rule 4; no other candidate is needed.
- Not checked: whether changing an entity's model later (damaged versions) refreshes the
  radius; how the original behaves when a corner is behind the eye (not reachable in normal
  play).
