# Level list: AirStrike 2 delta (`as2`)

Delta version 1.0. Amends [../levels-txt.md](../levels-txt.md) 1.0 for the game `as2`
(AirStrike 2 v2.51), for the syntax and the data of `maps\levels.txt`. Tag: `VERIFIED-DATA`
over the shipped file (`assets_extracted_games/as2/maps/levels.txt`, an encrypted pak entry as in the
first game), checked by `tools/ref/test_defs.py --game as2` and the C++
`defs_test.cpp` with `AS3D_GAME=as2` (per-level hashes in
`testdata/golden/as2/defs_summary.json`). What the engine does with the values is not checked
here.

## Statement reference

Same syntax and same 13 keys, no new one and no new value. Blocks and statements:

| | first game | `as2` |
|---|---|---|
| blocks | 24 | 20 |
| statements | 261 | 211 |
| `id` / `map` / `music` / `textures` / `hmin` / `hmax` / `fog` / `sun` | 24 each | 20 each |
| `name` | 20 | 18 |
| `water` | 23 | 18 |
| `night` | 14 | 8 |
| `enableHelic` | 8 | 5 |
| `intermission` | 4 | 2 |

The ids are `mission1` to `mission18`, `intro1` and `intro2`; the two `intro` blocks have no
`name` and carry the `intermission` statement (in the first game: four such blocks). Every
music file is a `.mo3` (as before). Every block names a different map. The texture-set
directories are `textures\badlands`, `desert`, `grass`, `rock`, `rock_snow` and `snow`
(`rock_snow` is new; the first game had no `rock_snow`). `hmin` / `hmax` range over -400..350
(the first game -240..200). Example: block `mission1` at the top of the file.

### `enableHelic`

Present in 5 blocks with the values 1 to 5, one each (the first game: 8 blocks with the
values 2 to 9). The number of helicopters of the game is 6 (`tools/games.json`).

## Level cap

The data has 20 blocks; the cap of 32 is VERIFIED-CODE for v1.70 only and is not checked in
the `as2` executable. `maps/levels.txt` has no other statements than those above, and
`test_defs.py` logs none as unknown.

## Unresolved references

Zero, as in the first game: every level's map, music file, texture-set directory and water
texture exists (the 218 unresolved references of the game are all in object definitions, see
obj.delta.md). One map, `level_konion.hsc`, is named by no block (hmap.delta.md).

## Canonical serialization

Unchanged (`canonical_level` / `canonicalLevel`); `testdata/golden/as2/defs_summary.json` has
one hash per block, keyed by `id`.

## Checked sections

| Base section | Status |
|---|---|
| Statement reference | same (13 keys; counts above) |
| `id` / `name` | same (2 unnamed intro blocks instead of 4) |
| `map` / `music` / `textures` | same |
| `hmin` / `hmax` | same syntax |
| `fog` | same syntax |
| `sun` | same syntax |
| `water` | same syntax |
| `night` | same syntax |
| `enableHelic` | same syntax (values 1 to 5) |
| `intermission` | same syntax |
| Level cap | not checked (20 blocks) |
| Unresolved references | same (zero) |
| Canonical serialization | same |

## Changelog

- 1.0 (B6): first version.
