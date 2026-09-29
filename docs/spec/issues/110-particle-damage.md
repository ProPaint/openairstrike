# 110: particle damage: parameter roles and targets

Status: open, choice made. Raised by WP-49 (item 1). Affects `engine/src/game/world_particles.cpp`.

## What the specs say

* `ps.md` `damage`: `damage TOUCH_x amount p2 p3`; `amount` goes through `atof` and a second,
  unidentified conversion (GUESS: an integer cast); p2 and p3 are plain floats of unknown role.
  Only TOUCH_ENEMIES and TOUCH_PLAYER are recognised.
* `engine-behaviour.md` 6.2: particle damage is `lerp(a, b, age / life)`, "applied on every
  n-th particle", no difficulty factor, attacker -1. TOUCH_PLAYER: every player whose rectangle
  contains the projected particle; otherwise the first enemy with class 2 and bit 0x08 that
  contains it.
* `engine-behaviour.md` 2 step 7: done in `P_UpdateEmitter`, once per unpaused frame.

Not said: which of the three numbers is n, a and b; what "every n-th" counts; whether the
amount is per frame or per second; whether dead or health-frozen enemies are candidates.

## Choice

* n = int(amount) (the integer-converted first number), at least 1; a = p2, b = p3. The four
  shipped uses read naturally that way: `15 10 0`, `15 10 2`, `15 12 3` (player flamethrowers
  1 to 3, damage fading from 10 or 12 at birth to 0 to 3 at death) and `25 10 0` (the boss 3
  flamethrower against the player).
* "Every n-th particle" is the particle's index in the instance's pool: indices 0, n, 2n, ...
  with age <= life_time, after the frame's update.
* The amount is applied per frame, not scaled by frametime (6.2 lists no frametime factor; the
  original therefore dealt more damage per second at higher frame rates). With our fixed 60 Hz
  step a flamethrower particle crossing an enemy deals roughly 10 per frame while inside it.
* The particle's world position (6.5 table) is projected with the previous frame's matrices,
  like every other collision test, and tested against the entity's screen rectangle (inclusive).
* TOUCH_ENEMIES candidates: pool entities, newest first, class 2, on screen (0x08), not
  removed, not dead, not health-frozen (6.6 excludes frozen entities from TOUCH_ENEMIES
  candidates). The first match takes the damage and the particle stops there.
* TOUCH_PLAYER: every living, on-screen player entity containing the particle. God mode and
  PlayerFreezeHealth protect as for any damage (`G_Damage`).

The particle simulation itself moved from the renderer into the World (issue 050 section 2 is
superseded): `World::particles()`, advanced after the entity pass and before the render pass
of `World::step`, with its own `as3d::Rng` restarted at every level start from the world
seed. Its state (instance count, live particles, a hash of every particle, damage totals) is
part of `World::dumpStateJson`.
