# 112: projected-shadow key at spawn; game-over music jump

Status: open, choices made. Raised by WP-49 (item 5). Supersedes issue 050 sections 3 and 7
(music jump part).

## Projected-shadow rotation key

`render-pipeline.md` 5.3: map objects use their placement byte, entities made by `create`
pass `int(yaw in degrees)`, which the generator multiplies by 30 degrees (correct only for yaw
0). Not said: at which moment of `create` the yaw is read, and what other spawn paths (Shoot
projectiles, drops, score digits, the player) pass.

Choice: the World records the key on the root at spawn (`Entity::shadowKey`,
`hasShadowKey`): the placement byte in the map spawner (before a path turns the entity), and
`ftol(yaw)` in `create` right after the spawn, when the yaw is the one copied from the
creator and before the new entity's `init` runs. The renderer wraps the key to 0..11 (30
degree steps modulo 360 degrees) and uses the root's key for every attachment. Entities from
other spawn paths keep the previous rule: the root's yaw rounded to 30 degree steps when the
renderer first sees them.

## Game-over music jump

`engine-behaviour.md` 12 / `frontend.md` 3.10: on game over the current module jumps to
pattern order 35, or 0 if the module is shorter. All five shipped modules have more than 35
orders (checked by `audio_test.cpp`). `as3d::Audio::jumpMusicToOrder(kGameOverMusicOrder)`
does the jump (row 0 of that order, looping continues from there). The call belongs to the
game's audio bridge (`apps/game/audio_bridge.cpp`, on the game-over edge), which WP-49 may not
change; see the WP-49 report.
