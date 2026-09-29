# 033: held keys and scripts that clear p_action

Status: engine decision, recorded for review. Raised by WP-42a.
Affects `World::applyInput` (engine/src/game/level_runtime.cpp).

## Facts

- engine-behaviour.md §7.2: a key press ORs its bit into `p_action`, a release clears it.
- The player's fly-in (`player\player.scr`, the subroutine at pc 1196, run by LCALL every
  update until the helicopter is 60 units past `g_map_pos`) writes `p_action = 0` each
  update (VERIFIED-DATA, disassembly).
- The original receives WM_KEYDOWN repeatedly while a key is held (Windows typematic
  repeat), so a fire key held through the fly-in sets its bit again after the fly-in.
  Mouse buttons do not repeat; the joystick path (0x422bd0) was not traced.

## Decision

`PlayerInput` carries the held state. Each frame, for a player whose actions are not
disabled, new presses and all held **level** bits (0x001..0x080: fire, missile, power-up,
directions) are ORed in and releases cleared; the one-shot switch bits (0x100, 0x200,
0x400) are applied on the press only, because `G_PlayerFrame` and the player script
consume them. Without this, a bot or player holding fire from the start never fires after
the fly-in.

## To confirm

Whether the mouse-button path (the shipped config binds fire to mouse 1) behaves like the
keyboard repeat, i.e. whether a held mouse button in the original stops firing after the
fly-in until it is clicked again.
