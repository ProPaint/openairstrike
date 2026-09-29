# Static model: Gulf Thunder delta (`gulf`)

Delta version 1.0. Amends [../as2/mdl.delta.md](../as2/mdl.delta.md) (which amends
[../mdl.md](../mdl.md)) for the game `gulf` (AirStrike II: Gulf Thunder v2.71). Tag:
`VERIFIED-DATA` over the 601 files of `assets_extracted_games/gulf/models`, checked by
`tools/ref/test_mdl.py --game gulf` and the C++ `mdl_test.cpp` with `AS3D_GAME=gulf`
(golden `testdata/golden/gulf/mdl_summary.json`).

## Corpus overview

Same format, version 2 only.

| | `as2` | `gulf` |
|---|---|---|
| files | 847 | 601 |
| parse (`ok`) | 841 | 597 |
| empty | 5 | 4 |
| broken | 1 | **0** |
| smooth-normal / flat-normal files | 789 / 52 | 489 / 108 |
| most vertices / faces in one file | 605 / 876 | 612 / 874 |
| tags in all files | 1348 | 815 |
| total bytes | 2,993,348 | 2,339,148 |

Every one of the 597 well-formed files parses with the base's byte accounting: there is
**no broken file**. `bridge1.mdl` does not exist in this game (`models/mapobjects/bridges/`
has `bridge_new`, `bridge_new02`, `bridge_new02_a` and `most_new_wood`). Two flat normals have
zero length (degenerate triangles: `models/mapobjects/houses/house08.mdl`, normal 7, and
`models/tanks/tiger/tiger_base.mdl`, normal 11), which the test reports as warnings, as it does
for the first game's `banner.mdl`.

## Empty files

4 files are 0 bytes: `models/items/ammo/asec.mdl`, `models/items/ammo/bonus.mdl`,
`models/mapobjects/houses/angar_rightdoor.mdl` and `models/mapobjects/ruins/stone_comb1.mdl`
(`models.empty` in `expected.json`; `as2` has one more, in `boss_2`).

## Checked sections

| Base section | Status |
|---|---|
| Corpus overview | changed (table above) |
| Header layout | same |
| Version and magic | same |
| `smooth_normals` flag | same |
| Texture path | not checked |
| Counts and bounding box | same |
| Arrays: Vertices, UVs, Faces | same |
| Arrays: Normals | same |
| Tags | same layout |
| The trailing 12 bytes | not checked |
| The attach position/orientation formula | not checked |
| Tag name inventory | not checked |
| Cross-check against `objects/*.obj` | not checked |
| Coordinate system | not checked |
| Winding vs. the renderer's GL state | not checked |
| Texture V orientation | not checked |
| Empty files | changed (4 files) |
| Files that do not fit | changed (none) |
| Golden data | same schema |
| Open questions | not checked |

## Changelog

- 1.0 (B6): first version.
