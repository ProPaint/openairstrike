# Level list: Gulf Thunder delta (`gulf`)

Delta version 1.0. Amends [../as2/levels-txt.delta.md](../as2/levels-txt.delta.md) (which
amends [../levels-txt.md](../levels-txt.md)) for the game `gulf` (AirStrike II: Gulf Thunder
v2.71). Tag: `VERIFIED-DATA` over `assets_extracted_games/gulf/maps/levels.txt`, checked by
`tools/ref/test_defs.py --game gulf` and the C++ `defs_test.cpp` with `AS3D_GAME=gulf`.

## Statement reference

Same syntax and the same 13 keys as `as2`. Blocks and statements:

| | `as2` | `gulf` |
|---|---|---|
| blocks | 20 | 26 |
| statements | 211 | 270 |
| `id` / `map` / `music` / `textures` / `hmin` / `hmax` / `fog` / `sun` | 20 each | 26 each |
| `name` | 18 | 24 |
| `water` | 18 | 25 |
| `night` | 8 | 6 |
| `enableHelic` | 5 | 5 |
| `intermission` | 2 | 2 |

The ids are `mission1` to `mission24`, `intro1` and `intro2` (the two intro blocks have no
`name` and carry `intermission`). **Two blocks name the same map**: `intro1` and `intro2` both
have `map maps\intro.hsc`; all other maps are named once. The first game and `as2` never name
a map twice. Every music file is a `.mo3`. The texture-set directories are `textures\desert`,
`grass`, `rock` and `wasterland` (`wasterland` is new). `hmin` / `hmax` range over -250..250.

### `enableHelic`

5 blocks with the values 1, 2, 2, 3 and 3 (values repeat here; in `as2` they are 1 to 5,
one each). The helicopter count of the game is in `tools/games.json` (`gulf`).

## Level cap, unresolved references, canonical serialization

26 blocks (the cap of 32 is VERIFIED-CODE for v1.70 only). No unresolved map, music, texture
directory or water texture (the game's 22 unresolved references are in object definitions,
obj.delta.md). Canonical serialization unchanged; `testdata/golden/gulf/defs_summary.json` keys
the levels by `id`, and 26 ids are distinct.

## Checked sections

| Base section | Status |
|---|---|
| Statement reference | same as `as2` (counts above) |
| `id` / `name` | same (2 unnamed intro blocks) |
| `map` / `music` / `textures` | changed (a map named twice; new texture directory) |
| `hmin` / `hmax` | same syntax |
| `fog` | same syntax |
| `sun` | same syntax |
| `water` | same syntax |
| `night` | same syntax |
| `enableHelic` | same syntax (values repeat) |
| `intermission` | same syntax |
| Level cap | not checked (26 blocks) |
| Unresolved references | same (zero) |
| Canonical serialization | same |

## Changelog

- 1.0 (B6): first version.
