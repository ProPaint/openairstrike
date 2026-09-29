# 211: objects spawned while the level loads are missing from the statistics

Status: open, decision needed. Raised by B4 (`as2` game rules). Affects the level start and
the mission statistics of [../engine-behaviour.delta.md](../engine-behaviour.delta.md) §2,
3.4, 6.1, 10.2 and 10.4.

## Facts (VERIFIED-CODE)

- `G_StartLevel` (as2@0x40e1e0) calls the map spawner once (as2@0x40e577) right after the
  camera reset, with the activation front edge at g_map_pos + 800 = 832, then one entity pass
  (as2@0x40e57c) and one render. Rows 0 to 20 are spawned there; their `init` and one think run.
- The spawner adds each class-2.0 object without `FL_NONTARGET` to the enemy total
  (as2@0x5432c0) and every object's rounded score to the maximum level score (as2@0x5432c4).
- `G_BeginLevel` (as2@0x410cd0) calls `G_StartLevel` and then sets both counters to 0
  (as2@0x410d96..0x410d9c). v1.70 had the same order but spawned nothing inside `G_StartLevel`
  (v170@0x407080), so it counted every object.
- `G_Damage` (as2@0x40b9e0) raises a player's kill counter only while it is below the enemy
  total (as2@0x40bb46..0x40bb5d), so kills of the uncounted objects still count but the ratio
  never exceeds 100 %.

## Effect

"Enemies destroyed" and the rank use totals that leave out the first 21 rows of every map
(the objects on screen at the start and just ahead). A player who destroys everything still
reaches 100 % thanks to the cap; the maximum level score is smaller than the score available,
so the rank's score term (0.5 × p_scores / maximum) can exceed 0.5 per mission.

## Choices

1. Reproduce the original: run the spawner (and one entity pass) inside the level load, before
   the counters are reset. Statistics and ranks then match the original game.
2. Count everything (reset the counters before the load-time spawn). Fairer totals, different
   ranks from the original.

Proposed: choice 1, since rank thresholds were tuned against the original's numbers.
