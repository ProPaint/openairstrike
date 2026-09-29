# Level map: Gulf Thunder delta (`gulf`)

Delta version 1.0. Amends [../as2/hmap.delta.md](../as2/hmap.delta.md) (which amends
[../hmap.md](../hmap.md)) for the game `gulf` (AirStrike II: Gulf Thunder v2.71). Tag:
`VERIFIED-DATA` over the 25 files of `assets_extracted_games/gulf/maps`, checked by
`tools/ref/test_hmap.py --game gulf` and the C++ `hmap_test.cpp` with `AS3D_GAME=gulf`
(golden `testdata/golden/gulf/hmap_summary.json`). The format is the base's, as in `as2`.

## Corpus overview

| | `as2` | `gulf` |
|---|---|---|
| files | 21 | 25: `intro`, `level01` to `level09`, `level10_boss`, `level11_bonus`, `level12` to `level17`, `level18_bonus`, `level19` to `level23`, `level24_boss` |
| grid | 20 x (32 x 256), 1 x (32 x 128) | all 25 are 32 x 256 |
| placements | 11,808 | 18,504 |
| waypoints / placements with a path | 2342 / 669 | 3348 / 1303 |
| placements with a script / a drop item | 94 / 637 | 161 / 1388 |
| type-table / item-table names | 1950 / 189 | 1970 / 172 |

`maps/levels.txt` has 26 blocks and names each map once except `intro.hsc`, which both intro
blocks name (`maps.reference_exceptions` in `expected.json`; levels-txt.delta.md).

## Grid and tile overlays

The tile set byte ranges over 0..3, 5 and 6 (cells: set 1 4872, 2 4, 3 1998, 5 677, 6 369);
sets 4, 7, 8 and 9 exist as atlases (the same atlases as `as2`: same sizes) but no cell uses
them. The helipad is set 6, index 5 as in `as2`. No cell has an out-of-atlas index or an odd
rotation (`maps.odd_tiles` is empty; `as2` has 4), and no waypoint breaks the range rules
(`as2`: 2).

## Type and item tables

Seven names in five maps do not resolve to an object definition (10 placements): `stone1_gray`
(3) and `tank_medium2_dead` (1) in `level02`, `plane_fighter_green` in `level08` (1) and
`level21` (1), `vagonetka01` (2) in `level14`, `barrel` (1) in `level17`, `hungar01_ruin` (1)
in `level21`. The shipped `game.log` of the game shows `ERROR: Unknown object 'stone1_gray'.`
and `ERROR: Unknown object 'tank_medium2_dead'.`, both names of `level02`. The list is `maps.unresolved_names` in `expected.json`.

## Level end

22 maps have an `end_of_the_level` marker and every marker stands on a helipad centre (set 6,
index 5). Without a marker: `intro`, `level10_boss` and `level24_boss`.

## Checked sections

| Base section | Status |
|---|---|
| Corpus overview | changed (table above) |
| File layout | same |
| Strings | same |
| Grid | changed (sets up to 6 used; no odd cells) |
| Placement record | same |
| Why `y` is 1-based | same |
| Waypoint (32 bytes) | same |
| Type and item tables, name resolution | changed (seven unresolved names) |
| Terrain geometry | not checked |
| Terrain texturing | not checked |
| Tile overlays | same as `as2` |
| Water | not checked |
| Fog, clear colour, far plane | not checked |
| Objects: spawning and activation | not checked |
| Waypoint paths | not checked |
| Scrolling, g_map_pos and camera | not checked |
| Level end | changed (22 markers, all on a pad) |
| levels.txt as read by the engine | see levels-txt.delta.md |
| Reference implementation | same |
| Open questions | not checked |

## Changelog

- 1.0 (B6): first version.
