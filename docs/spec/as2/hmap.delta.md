# Level map: AirStrike 2 delta (`as2`)

Delta version 1.0. Amends [../hmap.md](../hmap.md) 1.0 for the game `as2` (AirStrike 2 v2.51),
for the file format and the data of `maps/*.hsc`. Tag: `VERIFIED-DATA` over the 21 files of
`assets_extracted_games/as2/maps`, checked by `tools/ref/test_hmap.py --game as2` and the
C++ `hmap_test.cpp` with `AS3D_GAME=as2` (both parse every file to exactly its length and
compare the per-layer hashes and the placement hash with
`testdata/golden/as2/hmap_summary.json`). Behaviour of the terrain, the objects and the level
end is not checked here.

## Corpus overview

Same format, no new field. All 21 files start with `HMAP` and version 2 and parse to exactly
their file length with the layout of the base.

| | first game | `as2` |
|---|---|---|
| files | 24 | 21 (`level1_tutor`, `level2` to `level5`, `level8` to `level11`, `level14` to `level17`, `level_boss1` to `level_boss3`, `bonus_level`, `bonus_level2`, `level_konion`, `intro1`, `intro2`) |
| grid 32 x 256 / 32 x 128 | 22 / 2 | 20 / 1 (`intro1` is 32 x 128) |
| placements | 7991 | 11,808 |
| waypoints | 2562 | 2342 |
| placements with a path | 907 | 669 |
| placements with a script | 104 | 94 |
| placements with a drop item | 609 | 637 |
| type-table names / item-table names | 1629 / 217 | 1950 / 189 |

`maps/levels.txt` has 20 blocks and names 20 of the 21 maps, once each (the two intros
included); `level_konion.hsc` is named by no block, an unused map (`maps.reference_exceptions`
in `expected.json`). VERIFIED-DATA.

## Grid

Same layout. The tile set byte (byte 1) has a wider range:

| Byte | Range in `as2` | Note |
|---|---|---|
| 1 tile set | 0..6 and 9 (cells: set 1 2027, 2 170, 3 1835, 4 584, 5 1666, 6 251, 9 117) | the first game: 0..5. Sets 7 and 8 have atlases but no cell uses them |
| 2 tile index | 0..54 as before | |
| 3 tile rotation | 0, 3, 6, 9 and the odd values listed below | |

Readers must accept tile sets up to at least 9 (the reference test's range check is
`tile_sets` in `expected.json`; `engine/src/formats/hmap.cpp` stores the byte as read).
VERIFIED-DATA: when the tile set is 0, the index and rotation are 0 too (all 21 files).

Odd cells (tile index past the atlas or a rotation other than 0, 3, 6, 9), listed in
`expected.json` (`maps.odd_tiles`): `level15` (12, 209) set 4 index 8 rotation 1; `level16`
(16, 70) set 4 index 20 rotation 150; `level4` (25, 79) and (31, 79) set 2 index 5 rotation 3.
The first game's six odd cells (`level12`, `level18`) do not exist here.

## Tile overlays

Atlas `tiles\tiles<set>.tga`, 64 x 64 tiles as in the base. The atlases of the game
(all bottom-left origin, 32-bit with alpha):

| Set | Size | Tiles | Cells using it |
|---|---|---|---|
| 1 | 512 x 64 | 8 | 2027 |
| 2 | 256 x 64 | 4 | 170 |
| 3 | 256 x 64 | 4 | 1835 |
| 4 | 256 x 128 | 8 | 584 |
| 5 | 512 x 64 | 8 | 1666 |
| 6 | 256 x 256 | 16 | 251 |
| 7 | 256 x 64 | 4 | 0 |
| 8 | 256 x 64 | 4 | 0 |
| 9 | 512 x 64 | 8 | 117 |

The first game's atlases were 256 x 64, 256 x 128, 128 x 64, 256 x 64 and 256 x 256: the
numbers and sizes are not the same, so a set number does not mean the same picture in the two
games (`textures` are per game). The 3 x 3 helipad is now set 6: its centre is set 6, index 5
(the first game: set 5, index 5; the same 256 x 256 atlas size).

## Placement record and waypoints

Same. VERIFIED-DATA: every type and item index is in range, `x` 0..31, `y` 1-based, rotation
below 12, every per-placement script exists, and every path starts at the cell (x, y - 1) of
its placement. Two waypoints break the base's range checks and are kept as they are (listed in
`maps.odd_waypoints`): `level8` placement 0, waypoint 1 lies at row -4, above the map;
`level_boss1` placement 6, waypoint 21 has a delay of 10,000,000.

## Type and item tables, name resolution

Same. One name does not resolve to any `objects/*.obj` definition: `cutter_small_dead` in
`level8` (1 placement). The first game had none. The executables report such a name in
`game.log` as `ERROR: Unknown object '<name>'.` (seen for the unresolved names of the third
game's `level02`, whose shipped `game.log` shows that session; the `as2` log has no such line
because that session never loaded `level8`). The reference and C++ loaders keep the name and
the placement as data; what the engine does with the placement is for engine-behaviour.delta.md.

## Level end

`end_of_the_level` markers stand on a helipad centre (set 6, index 5) in 15 maps, and every
marker is on one. The maps without a marker: the two intros, the three boss levels
(`level_boss1` to `level_boss3`) and `level_konion`.

## Checked sections

| Base section | Status |
|---|---|
| Corpus overview | changed (table above) |
| File layout | same |
| Strings | same |
| Grid | changed (tile sets up to 9, odd cells) |
| Placement record | same |
| Why `y` is 1-based | same (VERIFIED-DATA: paths start at row y - 1) |
| Waypoint (32 bytes) | same |
| Type and item tables, name resolution | same layout; one unresolved name |
| Terrain geometry | not checked |
| Terrain texturing (height-banded base texture) | not checked |
| Tile overlays | changed (atlas sizes, sets 6 to 9, helipad is set 6) |
| Water | not checked |
| Fog, clear colour, far plane | not checked |
| Objects: spawning and activation | not checked |
| Waypoint paths | not checked (the data ranges are above) |
| Scrolling, g_map_pos and camera | not checked |
| Level end | changed (data only: 15 markers, all on a pad) |
| levels.txt as read by the engine | see levels-txt.delta.md |
| Reference implementation | same (`tools/ref/hmap.py` parses all 21 files) |
| Open questions | not checked |

## Changelog

- 1.0 (B6): first version.
