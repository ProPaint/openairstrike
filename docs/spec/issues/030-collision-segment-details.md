# 030: screen segment test details

Status: open. Raised by WP-42a (game world). Affects `engine/src/game/collision.cpp`.

## Question

rcsl-builtins-semantics.md D9 says a segment shorter than √0.5 pixel "tests its end
point" (engine-behaviour.md §5.2 said "about 0.7 px, the midpoint"). Which end is the end
point of `G_SegmentHitsRect` (0x40ca10)?

- For `TraceLine` / `TraceLineDamage` the segment is (from, to).
- For a point collider in the touch pass the stored rectangle is (min = this frame's
  projected origin, max = the previous frame's). Whether 0x40ca10 is called with
  (min, max) or (max, min) decides whether a near-stationary projectile tests its current
  or its previous position.

## Choice made

`World::segmentHitsRect(a, b, rect)` tests `b` when |b − a|² < 0.5. The touch pass calls
it with (min, max), i.e. the previous position; the traces call it with (from, to).

Also not in the specs and chosen here:

- The original has no guard for a point behind the eye (w ≤ 0) in 0x419540; we treat such
  a point as not projectable: an entity with a corner behind the eye is not on screen, a
  trace with an end behind the eye hits nothing.
- The rectangle depth is the projected depth of the origin (the original stores the
  eye-space z, used by nothing we implement).

## To settle

Read the argument order at the call sites of 0x40ca10 in 0x4051d0 and 0x41bd40/0x41bea0,
and the comparison at the start of 0x40ca10.
