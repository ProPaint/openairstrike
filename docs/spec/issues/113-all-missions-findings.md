# 113: findings of the all-missions run (WP-49 item 7)

Status: open, choices made. Raised by WP-49. Affects `engine/src/game/world_think.cpp`.

## 1. A pool entity detached for a missing tag no longer pins its root

`engine-behaviour.md` 3.3: a removed root is freed only when its attachment reference count
(+0x12) is below 1; `G_RemoveEntity` decrements the root's count through the entity's
parent. `attachToTag` detaches a pool entity whose tag is missing ("Tag '%s' not found",
the original detaches the child). The specs do not say whether the count is decremented on
that detach. If it is not, the later `remove` of the detached entity has no parent to
decrement through and the old root is never freed.

This happens in missions 4, 5 and 6: `rocketlauncher_rocketblock.mdl` lacks `tag_gun13` to
`tag_gun16`, so the muzzle flashes of the mobile rocket launchers (and boss 1's launchers)
are detached; in mission 6 some thirty removed launchers stayed in the pool for the whole
boss fight (removed, invisible, not updated, but occupying pool slots).

Choice: detaching for a missing tag decrements the old root's count (once, as for every
other attach in our engine). If the original leaks here, the difference is invisible (a
removed entity is neither drawn nor touched) except for pool pressure.

## 2. Shipped data problems seen in every run (not engine bugs)

* `models\misc\hlanno2.tga` is missing (a placeholder texture is used).
* `scripts\items\i_help.scr` (mission 1) and `scripts\j_cannon.scr` (mission 4) are named by
  the data but not shipped; the entities run without a script.
* `rocketlauncher_rocketblock.mdl` has no `tag_gun13`..`tag_gun16` (section 1).

## 3. Boss fights are long for the test pilot

With the machine gun every mission starts with (`frontend.md` 5) and what the pilot picks up,
bosses take long: mission 6 (boss 1) completes at frame 55975 and mission 20 (boss 3) at
81798, against about 13700 for most missions. Boss 1's sphere is shielded by the tanks and
launchers in front of it, which absorb the straight-ahead fire. Nothing points to a
simulation error (no script errors, health decreases steadily, `EndLevel` is reached), but
no reference exists for how long the original's fights last.

## 4. An attached boss part in the leaving state

In mission 20, `boss3_sphere` (created by `create`, no FL_TEMPORARY, attached to `boss3` with
`AttachEntity`) goes to state 2 (leaving) during the fight because its bounding sphere fails
the activation-area test while the scroll is stopped; it keeps running (its sphere stays in
the frustum), so the fight completes, but `Shoot` refuses leaving shooters. The specs apply
the area test to every pool entity, attached or not (`engine-behaviour.md` 3.5); we follow
them. Whether the original exempts attached pool entities is not known.
