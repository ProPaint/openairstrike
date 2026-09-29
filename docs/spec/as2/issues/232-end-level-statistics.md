# 232: `EndLevel` checkpoint statistics: division by zero and the mission number

Status: decided (engine choices). Raised by C3. Spec: rcsl-builtins-semantics.delta.md entry
`EndLevel`, engine-behaviour.delta.md 10.3.

1. **Zero divisors.** The checkpoint rank is rank accumulator + 0.5 × `p_scores` / A +
   `p_stars` / B, with A the level's maximum score and B its star count. A level with no scored
   placement or no star gives an infinite or NaN rank in the original (the delta says so). The
   mission-complete screen guards the same division (10.4, "guard it"). We count a term with a
   zero divisor as 0, so the stored rank stays finite.
2. **Which mission.** "Current level index + 1" is the 0-based index of the running mission plus
   one, i.e. its 1-based number: `World::checkpointMission()` returns the mission number given
   to `World::startLevel` (from `loadLevel`'s reference, "7" or "mission7"), 0 for a level that
   is not a mission.
3. **What is Approximate.** The statistics, the checkpoint values and the world stop are
   simulated. The end dialogue, the level-end sequence (as2@0x4234b0) and the campaign use of
   the checkpoint belong to the front end and the campaign package; `EndLevel` stays marked
   Approximate until they exist. `GameOver` likewise: the game-over menu and music are the
   front end's.
