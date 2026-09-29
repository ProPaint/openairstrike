# 273: Choices made connecting the sequels' rules to the simulation and the renderer (C6)

Status: decided (engine choices). Raised by C6. Specs: engine-behaviour.delta.md 2, 3.1.2, 4.2,
9.7, 10.2; render-pipeline.delta.md 7.7, 12.5, 12.6; rcsl-builtins-semantics.delta.md 67, 95;
issues [210](210-skid-trail-details.md), [211](211-load-time-spawn-and-statistics.md),
[220](220-water-surface.md), [221](221-skid-marks.md), [230](230-terramorph-stamp-reading.md),
[251](251-water-rendering-choices.md).

## Craters

1. **Who owns the change list.** Nobody takes it: the world keeps its last 64 terrain changes
   under a revision count (`World::terrainRevision`, `terrainChangesSince`, both const), and
   each reader remembers the revision it has mirrored. `WorldRenderer` reads the changes at
   every `render()` and re-uploads the touched terrain and water chunks; a reader more than 64
   changes behind refreshes the whole grid. A headless tool and a renderer can run side by
   side, and reading never changes the simulation (the state dump of a rendered run equals the
   simulation's: `apps/tests/as2_missions_test.cpp`, and every replay of
   docs/missions-status-as2.md). This replaces `takeTerrainChanges` of issue 230 §7.
2. **Ground marks** are cut from the terrain when the set of marks changes, so a mark created
   after a crater follows it (the shipped explosions create their mark right after
   `TerraMorph`), as the original. Unlike the original, a mark cut earlier is cut again from
   the new heights whenever the set changes later, so it does not float over a newer crater.
3. **Skid trails** are laid at the grid's height when drawn (`buildSkidTrailStrip`); the
   original fixes the height of a node when it is written. The two differ only for a node that
   lies in a crater made after it.

## Skid trails

4. Issue 210: a new node starts at age 0 (§1, proposed choice); ages grow while paused (§2,
   the original, as the package asked); a full trail drops its oldest node (§3). The trail
   length grows by the distance the owner's base origin moved since the previous update, from
   the second update on. The delta ("if the trail has nodes, its length += |B − previous B|")
   leaves open whether the first update, which starts node 0, adds a distance from the zeroed
   "previous B" of a fresh trail; that would only shift where the texture starts.
5. Trails belong to the entity through its slot and generation; a trail whose owner slot was
   freed or reused loses the owner at the next update even if the free did not reach it.
6. At most 8 trails per entity (the definition's record area); more `skid_mark` lines are
   ignored (the original would overwrite other fields of the definition).

## Water

7. The simulation samples the sequels' animated surface (`waterHeightAt`, as3d/water.h) at the
   world's own clock `time()`, which stops while paused and restarts at 0 at each level start;
   `WorldRenderer` draws the waves at the same `time()`, so an `FL_ONWATER` boat sits on the
   drawn surface. The original's surface is refreshed only for the visible chunks (issue 220
   choice 2).
8. `FL_ONWATER_NORMAL` and `FL_ONWATER_FLAT` put the executable's values (0xC, 0x204) in
   field 3, so scripts reading the flags see the original's bits.

## Level start

9. The load-time entity pass (issue 211, choice 1: reproduced) projects with the reset camera's
   matrices; the original's are those of the last frame it rendered (the menu's attract level
   or the previous mission). `frametime` is the fixed step.

## The test pilot (not a game rule)

10. For the sequels only, `botInput(world, frame)` skips pick-ups above `mapPos + 190` (the
    pilot never flies higher than `mapPos + 170`, and on a boss level, where the scroll stops,
    it would hover under such a pick-up for good) and dodges only below half its health (the
    sequels' bosses fire without pause and a pilot that always dodges never lines up under
    them); touch modes are read as bit sets there. The first game's pilot is unchanged
    (`tools/regress_as3d.sh`).
