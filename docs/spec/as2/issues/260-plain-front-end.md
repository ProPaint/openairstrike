# 260: The plain front end of the sequels (package C8)

Status: temporary, ours. `FrontendStyle::PlainList` (as2, gulf) runs a plain mission list until
the sequels' own menus (comic screens, new atlases) are rebuilt by a later package. The first
game (`V170Menus`) is untouched.

## What it is

- Screens: main menu (Start Game, Top Scores, Options, Exit; no Information: the sequels'
  texts are not extracted), Start Game (mission list, difficulty, helicopter rows; no game
  mode, single player only, co-op comes later), in-game menu, hint box, Mission Complete
  (statistics, helicopter rows, Restart, Quit, Next), Game Over, Game Complete, name entry,
  Top Scores, Options and Controls (the existing builders with plain widgets), exit
  confirmation, loading screen ("Loading", the mission's name, a bar).
- Built from the menu widgets with text captions in the game font and our own rectangles and
  lines (`engine/src/ui/screens_plain.cpp`, `MenuSystem::plain`). No texture but the font and
  `menu\cursor_1/2.tga` is requested; the test records every path.
- Keyboard: in plain menus spinners and edit fields let Up, Down and Tab move the focus (the
  first game's keep them); Enter on the mission list starts that mission.
- Loading screen: hosts call `ui::drawLoadingScreen` without the front end, so a plain
  Frontend leaves the caption in process-wide state (reset in its destructor).

## Campaign rules applied

- Unlocking: `Progress::unlockAfterMission`: mission (i + 1) mod count, and helicopter
  `enableHelic` when inside the count, in table order (as2: entries 1 to 5 after missions 4, 7,
  10, 13, 16). A fresh save unlocks missions 1 and 2 and **helicopter 0 only**
  (`Progress::defaults(rules, 1)`, passed by `GameFlow`); the first game keeps 2.
- Both players start on helicopter entry 0 (as2 7.6). Lives: `GameRules::startLives`.
- Banking: as the first game's `Campaign::bank` (Next banks; Restart and Quit do not, Game
  Over Quit and Game Complete Continue bank and run the high-score check).
- Upgrades: with `upgradesCarryToNextMission`, `MissionReport` carries the upgrades and weapon
  at the end of a mission (`hasUpgrades`), and "Next" sets `MissionStart::carryUpgrades` with
  them; `GameFlow` writes them into the player records right after `startMission` returns.
  New game and Restart leave `carryUpgrades` false: applying the mission loadout table
  (as2 8.2) is the session's job and is not done by the front end.

## Where it differs from the sequel

- No campaign checkpoint and no "Continue" (10.3, `campaignCheckpoint`): the start button
  always begins a fresh campaign with the start lives and zero score, wherever it starts.
  The later package needs: store checkpoint mission, lives, score, rank at `onEndLevel`, and
  resume when the chosen mission equals it; nothing of that is saved (save format unchanged).
- Mission Complete has the helicopter rows directly instead of a "Choose Helicopter" button;
  no portrait dialogues, comics, "New helicopter" is a plain line.
- Game Complete shows our own two lines, not the sequel's text.
- The carried upgrades overwrite the level start's loadout after the load; if the session
  applies loadout inside `startMission` this is equivalent, if it applies later it must
  honour `LevelSetup` (needs a field in `game_session.h`, not ours).
- `GameView` needed no change: the banner is off in plain mode, the attract level uses the
  existing hooks (`GameRules::attractCount`).
