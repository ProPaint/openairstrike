# Object definitions: AirStrike 2 delta (`as2`)

Delta version 1.0. Amends [../obj.md](../obj.md) 1.0 for the game `as2` (AirStrike 2 v2.51),
for the syntax of `objects\*.obj` in the data. Tag: `VERIFIED-DATA` over the 39 `.obj` files
of `assets_extracted_games/as2/objects` (1533 blocks, 10,852 statements, 27 distinct keys),
checked by `tools/ref/test_defs.py --game as2`, `test_textblock.py --game as2` and the C++
`defs_test.cpp` / `textblock_test.cpp` with `AS3D_GAME=as2`. What the new keywords do is
**specified in engine-behaviour.delta.md (pending)**; this file records only their syntax,
where they occur, and how the reference loaders keep them.

The generic block syntax ([../text-blocks.md](../text-blocks.md)) is unchanged: all 53
brace-block files of the game (39 `.obj`, 9 `.ps`, 4 `.wpn`, `maps/levels.txt`) parse with 0
errors.

## Statement reference

Three keys are new (24 in the first game, 27 here). Counts are statements (objects using them).

| Key | Statements (objects) | Arguments | Example |
|---|---|---|---|
| `civilian` | 103 (103) | none, like `player` / `enemy` | `objects/flora.obj` line 136 |
| `skid_mark` | 188 (93) | `x y width "texture"`: three numbers, then a quoted `.tga` path | `objects/jeeps.obj` line 310: `skid_mark  17 21 16 "gfx/marks/jeepMark1.tga"` |
| `speed` | 6 (6) | one number, a factor | `objects/player.obj` line 126: `speed 1.0` |

- `civilian` appears at most once per block. A block holds at most 4 `skid_mark` statements;
  the loaders keep at most 64, like `attach`.
- The six `speed` values are 0.7, 0.85, 1.0, 1.2, 1.25 and 1.4, one in each of six blocks of
  `player.obj` (lines 126, 270, 362, 489, 618, 759). Same spelling as the weapon `speed`
  statement ([../wpn.md](../wpn.md)) but a different block kind; there is no clash.
- The other 24 keys keep their grammar and their argument shapes. No new value appears in the
  enum-like arguments (`type`, `blend`, `envmode`, `rflag`, `shadow`, `sort`) except the two
  below.

### `flag`

Two spellings are new: `FL_ONWATER_NORMAL` (10 objects, e.g. `objects/ships.obj` line 39) and
`FL_ONWATER_FLAT` (4 objects, e.g. `objects/avianos.obj` line 56). The bit values the
executable gives them are not recorded here. The loaders keep them in `sequelFlags`
(`SEQ_FL_ONWATER_NORMAL` = 0x1, `SEQ_FL_ONWATER_FLAT` = 0x2, our own bits, one per spelling);
`flags` is not changed by them.

### `touch`

`TOUCH_CIVILIAN` is new (4 objects; `objects/weapons.obj` line 1098). It occurs in blocks
that write **several `touch` statements**, which the first game's data never does (0 blocks;
here 3 blocks): `TOUCH_CIVILIAN` then `TOUCH_PLAYER` (2), `TOUCH_ENEMIES` then
`TOUCH_CIVILIAN` (1), and `TOUCH_CIVILIAN` alone (1). In v1.70 a later `touch` replaces an
earlier one. The reference loaders therefore record `TOUCH_CIVILIAN` in `sequelTouch`
(`SEQ_TOUCH_CIVILIAN` = 0x1) and leave `touch` as the other statements set it; whether the
sequel ORs the statements is for engine-behaviour.delta.md.

### Unknown keywords

`DefDatabase` (C++) and `defs.py` log a keyword no loader knows once per kind (object, weapon,
particle system, level) and keyword, as `<file>: unknown <kind> keyword '<key>' at line N`,
and otherwise ignore it. VERIFIED-DATA: none occurs in the data of any of the three games
(`total_load_warnings` is 0 in the golden summaries).

## Record limits

The data has 1533 objects (the first game's cap of 2048 is VERIFIED-CODE for v1.70 only) and
at most 19 attachments per object. 1532 distinct names: `ship_big_helic_dead` is defined
twice, and the first definition wins the lookup as in the base.

## Unresolved references

`validate()` finds 218 (the first game: 13), all in objects: 82 model paths, 134 skin paths
and 2 attach targets (`PS_TERRABLAST` on `terra_blast_spark`, `PS_LASER_HIT` on `satellite_hit`) that name no file
or definition. The list is in `testdata/golden/as2/expected.json`
(`definitions.unresolved_references`); the tests require exactly this set. Most are paths
whose folder or stem differs from the shipped file (`models\mapobjects\misc\windmill.mdl`,
`models/tanks/sau/sau_metal.tga`); the loaders resolve none of them, as in the base.

## Canonical serialization

The base list of lines is unchanged and comes first, so every definition without the new
syntax hashes as in the first game. After the last `attach` line, in this order and **only for
what is present**:

```
civilian=1                                            if civilian
speed=<float bits>                                    if a speed statement was read
sequelFlags=<0x + 8 hex digits>                       if nonzero
sequelTouch=<0x + 8 hex digits>                       if nonzero
skidMarkCount=<n>                                     if n > 0, then for each i:
skidMark{i}=<x bits>,<y bits>,<width bits>,<texture>
```

Floats are formatted as in the base. `tools/ref/defs.py` (`canonical_object`) and
`engine/src/game/defs.cpp` (`canonicalObject`) produce byte-identical text;
`testdata/golden/as2/defs_summary.json` holds the hashes.

## Checked sections

| Base section | Status |
|---|---|
| Statement reference | changed (`civilian`, `skid_mark`, `speed`) |
| `type` | same |
| `blend` / `envmode` / `envmap` | same |
| `rflag` | same |
| `shadow` | same |
| `sort` | same |
| `flag` | changed (`FL_ONWATER_NORMAL`, `FL_ONWATER_FLAT`) |
| `touch` | changed (`TOUCH_CIVILIAN`, several `touch` statements per block) |
| `player` / `enemy` / `item` | same (syntax) |
| `scale` / `bbox_scale` / `min` / `max` | same (syntax) |
| `frames` | same (syntax) |
| `light` / `light_dir` | same (syntax) |
| `script` | same (syntax) |
| `attach` | same (grammar) |
| Record limits | not checked (data is under the cap; the executable's caps are not read) |
| Duplicate object names | same rule; the duplicate is `ship_big_helic_dead` |
| Unresolved references | changed (218, listed in `expected.json`) |
| Canonical serialization | changed (extra lines only when present) |

Behaviour of every statement, including the meaning of the flag bits, is not checked here.

## Changelog

- 1.0 (B6): first version.
