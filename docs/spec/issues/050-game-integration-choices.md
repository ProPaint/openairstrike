# 050: game integration: choices where the specs are silent

Status: open, choices made. Raised by WP-47 (desktop game integration). Affects
`engine/src/render/world_render*.cpp`, `engine/src/input/`, `apps/game/`.

## 1. Render submission order is the think order

`render-pipeline.md` 1.2 says records are drawn in submission order and that submission
happens during the entity pass, newest root first, parents before children. Section 4.1 of
`engine-behaviour.md` adds that an entity attached with `AttachEntity` makes its root think
first. The renderer therefore submits in think order, not in list order: a pool entity attached
to another is drawn after its root. This is visible: the player's spawn shield (`p_*_shield`, a
newer pool entity attached to the player, BLEND_ADD in the opaque list, depth write on) would
otherwise be drawn first and hide the helicopter body behind its depth.
`worldRenderOrder()` implements it; covered by `game_integration_test.cpp`.

## 2. Particles live on the render side

The world keeps emitter holders but no particles. `WorldParticles` mirrors each holder with a
`ParticleEmitter` advanced once per unpaused fixed step after `World::step`, seeded from its
own `Rng` (seed 1 at level start, one draw per new instance), so rendering cannot change the
simulation. Consequences:

* The particle `damage touch amount ...` statement (`render-pipeline.md` 6.2, 6.4, "applies
  particle damage") has no effect: the simulation does not simulate particles. Four shipped
  systems carry it, all flamethrowers: the player's `PS_FLAMEFROWER_HOR`, `_HOR2`, `_HOR3`
  (TOUCH_ENEMIES) and the enemy `PS_FLAMEFROWER_DIAG` (TOUCH_PLAYER). The player's flamethrower
  therefore does no damage yet. Fixing it means moving the particle simulation into the world
  (drawing from the world's Rng, since the damage changes the game) with the renderer reading
  the world's particles; the damage formula of 6.4 is not specified in detail.
* An instance is created the first frame its holder has thought (its attachment position is
  known). When the 256-instance pool is full the holder never gets one, like the original;
  which holders lose out follows our discovery order (oldest root first), not the original's.
* A deactivated holder (`AttachDeactivate`, `deactivate`) stops emitting; reactivation calls
  `activate()` (clock reset) only on the inactive-to-active edge. A removed or freed holder stops
  its instance, which is freed once its particles are dead.

## 3. Projected shadow rotation key of live entities

Section 5.3: map objects pass their placement byte, `create`-spawned entities `int(yaw)`. The
renderer does not know which way an entity was spawned; it uses `round(yaw / 30) mod 12` of the
entity's root at the time it first sees the entity. That is exact for map objects (yaw = byte
x 30) and differs from the original's quirk for created entities with a non-zero yaw.
The shadow stays at the position where it was first seen (the original bakes it at spawn).

## 4. Dynamic light order

At most 32 lights are kept, the first submitted. The original interleaves `PlaceLight` calls
(during `main`) with definition lights (after `main`, per entity). The renderer takes the
frame's `PlaceLight` queue first, then the definition lights in record order. The order only
matters with more than 32 candidates.

## 5. Controls

* Player 1 uses the shipped `config.ini` `[Controls]` bindings rather than the built-in
  defaults: arrows, Ctrl or left mouse fire, Shift or right mouse missile, Space or middle mouse
  power-up, 1 next missile, 2 next weapon, 3 and 4 next power-up. Either Ctrl or Shift key works
  (VK_CONTROL and VK_SHIFT cover both sides). Joystick codes are not mapped.
* Player 2 has no default keys (its shipped keys equal player 1's).
* A key press is latched until the next simulation frame, so a tap shorter than one frame still
  reaches `p_action` once, as the original's WM_KEYDOWN would.
* Mouse control (`MouseControl=1` in the shipped config, mouse position to direction bits in
  `G_PlayerFrame`) is not implemented.
* Return or keypad Enter confirm a tutorial hint (the original clicks an OK button).
* Escape quits (there is no in-game menu yet); P or Pause toggles the pause, ignored while a
  hint box, the mission-complete or the game-over state holds the pause. Unpausing clears
  `p_action` of both players (`engine-behaviour.md` 1.3 state 6).

## 6. Level flow without menus

Until the menus exist, the mission-complete and game-over screens are replaced by a delay of
180 frames (3 s) on the frozen level, then Continue (bank the score, lives carried over, next
mission; after mission 20 the campaign starts again at mission 1) or Restart (same mission,
lives it started with). Every mission starts with the level 1 machine gun only (`frontend.md` 5;
the world's level reset already does this).

## 7. Sound

* 3D sound is off, as in the shipped `config.ini` (`Sound3D=0`): every sound plays centred at
  full volume (`engine-behaviour.md` 12). Volumes are the shipped `SfxVolume` and `MusicVolume`
  (0.5).
* One loop per entity; a loop stops when its entity is removed or freed (the original stops the
  channel with the entity, GUESS).
* The jump of the music to its game-over section (pattern order 35) and the F5 to F8 volume keys
  are not implemented.
