# 270: The sequels' ORed `touch` statements, without a change to the definition goldens

Status: decided (engine choice). Raised by C6. Settles the proposal of
[issue 233](233-touch-bits-from-the-loader.md). Specs: engine-behaviour.delta.md 3.1 and 5.2.

## Facts

- The sequels' parser ORs every `touch` statement into the mode (TOUCH_ENEMIES 0x1,
  TOUCH_PLAYER 0x2, TOUCH_CIVILIAN 0x4) and TOUCH_ALL sets 0xF; the first game's parser sets
  the mode from the last statement (VERIFIED-CODE in the delta).
- `ObjectDef::touch` keeps the first game's rule and `ObjectDef::sequelTouch` the civilian
  bit, and both are part of the canonical serialization that `tools/ref/defs.py` produces and
  the goldens of all three games hash.

## Choice

`sequelTouchModeOf(TextBlock)` (`engine/src/game/defs_enums.h`, next to the loader's other
keyword tables) builds the sequels' mode from every `touch` statement of the definition's own
text block, and the world uses it for the entity's touch mode whenever
`GameRules::touchModeBits` is set (`engine/src/game/world.cpp`). A definition without a text
block (none today) falls back to the old combination of the two fields.

- The loader's fields and the canonical serialization are unchanged, so the definition goldens
  of all three games are unchanged (no golden regenerated); `tools/ref/defs.py` needs no
  change.
- With the shipped data the result is the same as before for every definition (at most one
  non-civilian `touch` line each, combinations 1, 2, 4, 5, 6); a definition with both
  `touch TOUCH_ENEMIES` and `touch TOUCH_PLAYER` now gets 3, as in the original, where it got 2.
- An unknown spelling ORs nothing in the sequels (the first game's loader turns it into
  "none"). Particle systems' `damage` touch value is unchanged (issue 233, second point).
