# 234: Player acceleration, health clamp, loadout and water height in our engine

Status: decided (engine choices). Raised by C4. Specs: engine-behaviour.delta.md 7.2 to 7.4 and
8.2, rcsl-builtins-semantics.delta.md entries `GetPlayerAccel`, `WaterHeight`.

1. **Acceleration vector.** Built from the direction bits in the player frame, after the next-item
   edges and the game-over test, not while paused (7.2 step 1). Keyboard, touch and the test bot
   all produce direction bits. The relative mouse steering of step 2 (length 2.0, player 1 only,
   cursor re-centred) needs a relative mouse device the input layer does not provide yet; it is
   not wired (`GameRules::mouseAccel` is unread).
2. **Health clamp.** The original clamps while drawing the HUD. We clamp at the end of every
   frame, after the render pass, when the HUD would be drawn (not on intermission levels, not
   while the HUD is hidden after `EndLevel` or a game over).
3. **Loadout.** `World::applyMissionLoadout(mission)` applies the table row of the 1-based mission
   number, clamped to the table. `loadLevel` applies it at every level start unless
   `World::carryUpgradesToNextLevel()` was called before (the campaign's "Next"; the game app
   has to call it when it exists). A level that is not a mission (a map path, an attract level)
   uses row 1.
4. **`speed` on a player definition without the statement** gives 1.0 in field 23 (the neutral
   factor of the player scripts); every other definition without it keeps 0, as in the
   original. All six shipped helicopters have `speed`.
5. **`WaterHeight`.** The original interpolates its water grid, which its renderer animates with a
   wave term that is not decoded (open question 3 of the builtins delta). We interpolate a grid
   whose flooded vertices hold the water level and whose other vertices, and first and last
   rows, hold the terrain: the still-water surface. The builtin is marked Approximate; no
   shipped `as2` script calls it. Positions outside the map clamp to the edge, like our
   `TerrainHeight` (docs/spec/README.md, deviations).
6. **Kill counter cap, touch bits, dead shooters, civilians in splash and traces, respawn without
   the lives test, 9 upgrade slots, power-up cycling skip mask** follow the rule flags of
   `GameRules` exactly as the deltas state; the first game's values of those flags reproduce
   its behaviour (checked by `tools/regress_as3d.sh`).
