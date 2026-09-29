# 271: Mission loadout, carried upgrades and the checkpoint on the game app's side

Status: decided (engine choices). Raised by C6. Specs: engine-behaviour.delta.md 8.2, 10.3;
issues [232](232-end-level-statistics.md), [234](234-player-input-and-water-height.md) §3,
[260](260-plain-front-end.md) ("Upgrades", "Where it differs").

## Loadout and carried upgrades

- `World::startLevel` applies the rules' loadout row of the mission unless
  `World::carryUpgradesToNextLevel()` was called before the load (issue 234 §3).
- `LevelSetup` (apps/game/game_session.h) now carries `carryUpgrades`, `upgrades` and `weapon`.
  `GameSession::startMission(LevelSetup)` re-initialises the world, and when `carryUpgrades` is
  set and `GameRules::upgradesCarryToNextMission` holds, writes them into both player records
  and calls `carryUpgradesToNextLevel()` before the load: "Next" starts with the upgrades of
  the last mission; a new game and a Restart (`carryUpgrades` false) get the mission's
  loadout. The first game ignores the fields (its level start clears the upgrades).
- `GameFlow` copies `MissionStart::carryUpgrades`, `upgrades` and `weapon` into the setup; the
  former workaround that overwrote the player records after the load is gone.
- The direct flow (`as3d_game --level N`, the bot): a mission complete banks, loads the next
  mission and carries the upgrades (a new campaign after the last mission gets mission 1's
  loadout); a game over restarts the mission with its loadout and the lives it began with.

## The checkpoint in the mission report

`MissionReport` gained `hasCheckpoint`, `checkpointMission` (0-based: the mission after the
completed one, equal to the mission count after the last mission, which no start matches),
and per player `checkpointLives`, `checkpointScore`, `checkpointRank`: the values `EndLevel`
stored (`World::endLevel`, issue 232). They are filled only when `GameRules::campaignCheckpoint`
holds and the level was completed.

For the checkpoint rank to be the campaign's total as in the original, the world has to start
with the campaign's rank accumulator: `MissionStart::rankAccumulator` (new) goes through
`LevelSetup::rankAccumulator` into the player records. A front end that leaves it 0 gets only
the completed mission's part in `checkpointRank`.

## For the front-end package

- "Continue" (G_NewGame, 10.3): when the chosen mission equals the stored checkpoint mission,
  start it with the checkpoint's lives (`MissionStart::lives`), score (`banked`) and rank
  (`rankAccumulator`); otherwise with `GameRules::startLives`, 0, 0. Save the checkpoint unless
  a cheat was used (10.5). Nothing of this is done by the plain front end yet (issue 260).
- Fill `MissionStart::rankAccumulator` from the campaign.
