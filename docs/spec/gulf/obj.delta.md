# Object definitions: Gulf Thunder delta (`gulf`)

Delta version 1.0. Amends [../as2/obj.delta.md](../as2/obj.delta.md) (which amends
[../obj.md](../obj.md)) for the game `gulf` (AirStrike II: Gulf Thunder v2.71). Tag:
`VERIFIED-DATA` over the 29 `.obj` files of `assets_extracted_games/gulf/objects` (956
blocks, 6516 statements, 27 distinct keys), checked by `tools/ref/test_defs.py --game gulf`,
`test_textblock.py --game gulf` and the C++ `defs_test.cpp` / `textblock_test.cpp` with
`AS3D_GAME=gulf`. All 43 brace-block files (29 `.obj`, 9 `.ps`, 4 `.wpn`, `levels.txt`) parse
with 0 errors. Behaviour is **specified in engine-behaviour.delta.md (pending)**.

## Statement reference

The same 27 keys as `as2`, with the same syntax, the same enum values and the same
handling (`sequelFlags`, `sequelTouch`, `skidMarks`, `speed`, `civilian`, canonical lines);
nothing else is new. Counts, statements (objects):

| Key or value | `gulf` | `as2` |
|---|---|---|
| `civilian` | 62 (62) | 103 (103) |
| `skid_mark` | 78 (38), at most 4 per block | 188 (93) |
| `speed` | 3 (3): 1.15, 1.3 and 1.5, in `player.obj` lines 128, 265, 365 | 6 (6) |
| `flag FL_ONWATER_NORMAL` | 4 objects | 10 |
| `flag FL_ONWATER_FLAT` | 1 object | 4 |
| `touch TOUCH_CIVILIAN` | 3 objects (`weapons.obj` line 1307, `_new.obj` line 32) | 4 |

Blocks with several `touch` statements: 2 (`as2`: 3). The three `TOUCH_CIVILIAN` blocks are
`TOUCH_CIVILIAN` + `TOUCH_PLAYER`, `TOUCH_ENEMIES` + `TOUCH_CIVILIAN` and `TOUCH_CIVILIAN`
alone. Examples: `objects/bikes.obj` line 52 (`skid_mark  11 19 9 "gfx/marks/jeepmark1.tga"`),
`objects/ships.obj` line 34 (`FL_ONWATER_NORMAL`), `objects/player.obj` line 46 (`civilian`).

## Record limits and duplicate names

956 objects, at most 21 attachments per object. 953 distinct names: `meteorite_flare`,
`meteorite_hvost` and `meteorite_hit` are each defined twice; the first definition wins the
name lookup, as in the base.

## Unresolved references

22 (`as2`: 218): 8 model paths, 8 skin paths and 6 attach targets (`PS_LASER_HIT`,
`PS_TERRABLAST`, `PS_PLASMA_LAZER2`, `PS_PLASMA_LAZER3`, `PS_PLASMA_LAZER_BIG2`,
`PS_PLASMA_LAZER_BIG3`). The list is `definitions.unresolved_references` in
`testdata/golden/gulf/expected.json`.

## Checked sections

| Base section | Status |
|---|---|
| Statement reference | same as `as2` |
| `type` | same |
| `blend` / `envmode` / `envmap` | same |
| `rflag` | same |
| `shadow` | same |
| `sort` | same |
| `flag` | same as `as2` (counts above) |
| `touch` | same as `as2` (counts above) |
| `player` / `enemy` / `item` | same |
| `scale` / `bbox_scale` / `min` / `max` | same |
| `frames` | same |
| `light` / `light_dir` | same |
| `script` | same |
| `attach` | same |
| Record limits | not checked (956 objects, under the cap of the first game) |
| Duplicate object names | changed (three names, listed above) |
| Unresolved references | changed (22, listed in `expected.json`) |
| Canonical serialization | same as `as2` |

## Changelog

- 1.0 (B6): first version.
