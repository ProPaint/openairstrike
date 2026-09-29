# 111: enemy health bar: how the two sprites make the bar

Status: open, choice made. Raised by WP-49 (item 3). Affects `engine/src/render/health_bar.cpp`.

`engine-behaviour.md` 6.5 gives the condition (maximum health > 150, class 2, damaged less than
1 s ago, not dead), the two sprite definitions (`hbar_empty`, `hbar_full`), the fill fraction
(health / maximum health) and the position. `frontend.md` 4.2 confirms there is no 2D boss bar:
bosses use the same 3D bar. Not given:

* how the fraction is applied to the sprites;
* which is drawn first;
* whether "ground units" means FL_ONGROUND, and which origin and radius are used.

Choice:

* The empty bar is drawn whole (its `min`/`max` rectangle, -16..16 by -1.3..1.3 units, as a
  billboard); the full bar is drawn after it, on top, with its right edge and its texture s
  cut to the fraction: `x1 = x0 + (x1 - x0) f`, `s1 = s0 + (s1 - s0) f`. With f <= 0 only the
  empty bar is drawn. Both keep their definition's blend (none), RF_NODEPTHTEST and the white
  vertex colour; no scale.
* Ground units are entities whose flags (field 3) have FL_ONGROUND (bit 1, so
  FL_ONGROUND_NORMAL too). The position uses the entity's world position (base origin, fields
  41..43), its bounding radius (+0x1BF) and the model box's z (unscaled).
* The bar is queued in the entity pass right after the entity's own records (so it is drawn in
  the sprite pass in think order), also for entities that are not drawn themselves
  (FL_NODRAW or no definition), since the think draws it independently of the render record.
