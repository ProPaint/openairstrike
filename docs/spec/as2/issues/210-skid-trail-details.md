# 210: skid-trail details an implementation may simplify

Status: open, choices proposed. Raised by B4 (`as2` game rules). Affects the skid-mark trails
of [../engine-behaviour.delta.md](../engine-behaviour.delta.md) 3.1.2.

The trail update `G_UpdateSkidTrails` (as2@0x414850) and the trail drawer (as2@0x4300e0) have
three behaviours that look accidental. None changes the gameplay; they only change how the
marks look.

## 1. Age of a newly started node

When the node timer reaches 5/12 s a new node is started in the slot after the last one, but
the slot's age is not reset (as2@0x414a9e..0x414adb writes the position and then the
points, never the age). In a fresh trail the slot is zero. After old nodes expired, the remaining nodes were
shifted down by one slot and the slot now reused still holds the age of the node that was
there before the shift, which can be close to 10 s: that node is drawn nearly transparent and
expires almost at once. VERIFIED-CODE.

Proposed choice: set the age of a new node to 0.

## 2. Ageing while paused

`G_UpdateSkidTrails` runs from `G_Frame` unconditionally (as2@0x41104a), and `frametime` keeps
its value while the game is paused, so marks keep fading during the pause, the in-game menu
and the tutorial hints. VERIFIED-CODE.

Proposed choice: do not age trails while paused (a deviation to list in the README table if
taken), or keep the original. Either is invisible in normal play except for long pauses.

## 3. Full trail

With 23 nodes, committing a node shifts nodes 1..23 down to 0..22 and writes the new node into
slot 23, one past the 23 visible slots (as2@0x414ab3..0x414ac6); the visible last node (22) is
then the previously committed one, which keeps being moved with the vehicle. In effect the
oldest node is dropped and the head keeps following the vehicle. VERIFIED-CODE.

Proposed choice: keep at most 23 nodes, drop the oldest when a new one is committed.
