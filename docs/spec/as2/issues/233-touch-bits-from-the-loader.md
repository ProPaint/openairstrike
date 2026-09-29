# 233: Touch modes as bit sets from the definition loader

Status: open (loader detail), harmless with the shipped data. Raised by C3. Specs:
engine-behaviour.delta.md 3.1 (`touch` values are ORed, `TOUCH_ALL` sets 0xF), rcsl-vm.delta.md
(touch dispatch), obj.delta.md.

- The original's parser ORs every `touch` statement into the mode. Our loader keeps the last
  `TOUCH_ENEMIES` / `TOUCH_PLAYER` / `TOUCH_ALL` statement in `ObjectDef::touch` and the
  `TOUCH_CIVILIAN` statements apart in `ObjectDef::sequelTouch`. The world builds the sequels'
  mode as `touch` (with `TOUCH_ALL` read as 0xF) OR 0x4 for `TOUCH_CIVILIAN`
  (`engine/src/game/world.cpp`). That equals the original's OR whenever a definition has at most
  one non-civilian `touch` line, which holds for every shipped definition (combinations 1, 2,
  4, 5, 6). A definition with `touch TOUCH_ENEMIES` and `touch TOUCH_PLAYER` would get 2 here and
  3 in the original. Proposed: the loader records the sequels' OR directly.
- Particle systems: the `damage` statement's touch value comes from the same parser; the
  sequels read it as a bit set (engine-behaviour.delta.md 6.2) and we do the same with the value
  the loader gives (`TOUCH_ALL` = 3, which selects the players as 0xF would).
