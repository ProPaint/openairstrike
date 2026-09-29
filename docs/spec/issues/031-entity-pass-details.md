# 031: entity pass details not covered by the specs

Status: open. Raised by WP-42a (game world). Affects `engine/src/game/world_think.cpp`,
`engine/src/game/level_runtime.cpp`.

## 1. Where runtime bit 0x02 ("already thought this frame") is cleared

engine-behaviour.md §4.1 says a think runs once per entity per frame (bit 0x02) but not
where the bit is cleared. Choice: at the start of `G_RunEntities`, right after freeing
removed entities, for every entity. Consequence: an entity created during the map spawn
step of a frame (an `init` that calls `create`) thinks inside `create` and again in that
frame's pass; one created during the pass (by a `main`) is newer than the walk and is not
visited again, as specified.

## 2. Leaving state: the second visibility test

hmap.md: a leaving entity is removed "as soon as it is no longer visible (0x419970) or a
second test (0x405140, not analysed) fails". Choice: only the frustum sphere test.

## 3. Dormant entities that never reach the activation band

A placement spawned within the window but whose bounding sphere never satisfies
`y − r ∈ [g_map_pos + 16, g_map_pos + 800]` (for example a large object in the first rows
of the map) stays dormant forever: nothing in the specs removes a dormant entity. We keep
it (bounded: at most one per such placement). The state-change function 0x405c70 was not
analysed.

## 4. Two-player assignment of map objects

engine-behaviour.md §3.4 step 3: "chosen at random between the living players". The
formula (which `rand()` draw, which threshold) is not given. Choice: bit 0 of the next
xorshift value; "living" = p_lives ≥ 0.

## 5. A drop spawned on a kill

engine-behaviour.md §6.1: "the drop object is spawned at the origin (ground/water
snapping)". Choice: like `create` (init, then one think), with no creator (angles 0,
player index 0).

## 6. Score digits for a zero award

§6.4 creates one `score_num` per decimal digit of the award. Choice: nothing is created
for an award ≤ 0 (the score itself is still added).
