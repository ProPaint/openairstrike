# 272: The sequels' mouse control in our input layer

Status: decided (engine choices). Raised by C6. Spec: engine-behaviour.delta.md 7.2 step 2;
issue [234](234-player-input-and-water-height.md) §1 (not wired until now).

## What the original does

With `[System] MouseControl` = 1 (the sequels' default), for player 1 only, when its actions
are not disabled and the keys give a zero vector: the cursor's offset from the window centre
this frame, scaled to length 2.0 (`GameRules::mouseAccel`) when not zero; the cursor is put
back at the window centre every unpaused frame. The mouse steers by its motion; there is no
cursor sprite (the first game steered toward the cursor).

## Our engine

- **Simulation.** `PlayerInput::mouseSteer` and `mouse[2]` (x right, y up) carry the motion of
  the step; `World::computeAccel` uses it for player 1 as above when `GameRules::accelInput`
  holds and `mouseAccel` > 0. Only the direction is used, so the unit of the motion does not
  matter. The first game never reads it.
- **Input.** `FrameInput::mouseSteer`, `mouseDx`, `mouseDy` (merged by summing). They are not
  part of input scripts: a run steered with the mouse is not recorded and cannot be replayed
  (keys, touch, the bots and the scripts are unaffected).
- **Desktop window** (`apps/game/game_loop.cpp`): while `GameFlow::relativeMouseActive()`
  (playing, no menu, MouseControl on, not in touch mode, a game with these rules) the pointer
  is captured with SDL's relative mouse mode, which is the original's re-centring; the
  motion gathered between two simulation steps goes to the next step. Menus, pause and the
  background release the capture. `GameFlow` no longer applies the first game's cursor
  steering or draws the cursor sprite for such a game.
- **Default.** A fresh profile gets MouseControl on, with the mouse-button bindings it implies
  (`Settings::applyMouseControlBindings`), for a game whose `GameRules::mouseControlDefault`
  holds, except in touch mode and on the web (where capturing the pointer needs the page's
  pointer lock; the setting can still be turned on in Options there). A saved profile keeps
  its setting.
- Touch and keyboard are unchanged: the direction bits come first, the mouse only fills a
  zero key vector.
