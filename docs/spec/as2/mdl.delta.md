# Static model: AirStrike 2 delta (`as2`)

Delta version 1.0. Amends [../mdl.md](../mdl.md) 1.1 for the game `as2` (AirStrike 2 v2.51),
for the file format and the data of `models/**/*.mdl`. Tag: `VERIFIED-DATA` over the 847
files of `assets_extracted_games/as2/models`, checked by `tools/ref/test_mdl.py --game as2`
(byte accounting, index ranges, finite floats, normal lengths, vertices inside the box,
printable tag names, golden diff against `testdata/golden/as2/mdl_summary.json`) and the C++
`mdl_test.cpp` with `AS3D_GAME=as2` (`loadModel` reproduces the counts, the box, the tag names
and the raw-array hash of all 841 well-formed files). Rendering, winding and attach maths are
not checked here.

## Corpus overview

Same format, no field added. Every well-formed file has `MDL!` version 2 (the first game's
version 3 was never seen either).

| | first game | `as2` |
|---|---|---|
| files | 446 | 847 |
| parse (`ok`) | 440 | 841 |
| empty (0 bytes) | 4 | 5 |
| non-empty, header counts do not fit the file (`broken`) | 2 | 1 |
| smooth-normal files / flat-normal files | 417 / 23 | 789 / 52 |
| most vertices / faces in one file | 274 / 372 | 605 / 876 |
| tags in all files | 733 | 1348 |
| total bytes | 870,972 | 2,993,348 |

Every `ok` file has at least one UV. The base's rule "normals are per vertex when
`smooth_normals`, else per face" holds for all 841; no flat normal has zero length here (the
first game has two, in `banner.mdl`).

## Empty files

5 files are 0 bytes: `models/bosses/boss_2/boss_2_boss_2_base_turret_r.mdl`,
`models/items/ammo/asec.mdl`, `models/items/ammo/bonus.mdl`,
`models/mapobjects/houses/angar_rightdoor.mdl` and `models/mapobjects/ruins/stone_comb1.mdl`
(the first game's `models/jeeps/jeep_bug_cannon.mdl` is not in this game). They are listed in
`expected.json` (`models.empty`). A model path that names one of these is not checked here
(the base's remark for the first game, that no definition names its four, is not repeated).

## Files that do not fit

One non-empty file does not parse: `models/mapobjects/bridges/bridge1.mdl`, with the same
header counts, the same expected size and the same size difference (-24) as in the first
game, so very probably the same file. `japbridge.mdl`, the first game's second broken file,
does not exist here. The reader's recovery (`loadModel` returns the whole arrays that fit and
a warning) is unchanged; `expected.json` lists the file under `models.broken` with the
difference -24.

## Checked sections

| Base section | Status |
|---|---|
| Corpus overview | changed (table above) |
| Header layout | same |
| Version and magic | same (version 2 only) |
| `smooth_normals` flag | same |
| Texture path | not checked |
| Counts and bounding box | same |
| Arrays: Vertices | same |
| Arrays: UVs | same |
| Arrays: Faces | same |
| Arrays: Normals | same (count rule verified; unit lengths within the test's tolerance) |
| Tags | same layout (names printable ASCII) |
| The trailing 12 bytes | not checked |
| The attach position/orientation formula | not checked |
| Tag name inventory | not checked |
| Cross-check against `objects/*.obj` | not checked |
| Coordinate system | not checked |
| Winding vs. the renderer's GL state | not checked (the sequels use Direct3D 8) |
| Texture V orientation | not checked |
| Empty files | changed (5 files) |
| Files that do not fit | changed (`bridge1.mdl` only) |
| Golden data | same schema (`mdl_summary.json`) |
| Open questions | not checked |

## Changelog

- 1.0 (B6): first version.
