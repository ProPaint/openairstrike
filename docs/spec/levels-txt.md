# Level list (`maps\levels.txt`)

Spec version 1.0. Reference implementation: `tools/ref/defs.py`.
Engine implementation: `engine/src/game/defs.cpp`
(`engine/include/as3d/defs.h`, `LevelDef`).

Builds on `docs/spec/text-blocks.md`. `maps\levels.txt` is a sequence of 24
**anonymous** blocks (VERIFIED-DATA), one per mission/cutscene, in file
order — `DefDatabase::levels()` preserves this order (it is meaningful: it
is mission progression order, `mission1`..`mission21` plus 4 intro/
intermission entries interleaved at the point they play). Code-derived
claims cite the v1.70 executable's per-block parser, unnamed in the Ghidra
export but referenced from the strings `enableHelic`, `intermission`,
`Too many levels in level-list file.` (address `0x00406340`), called from
`G_LoadLevelList` at `0x00406880`.

## Statement reference

| Key | Count | Type | Default | Confidence |
|---|---|---|---|---|
| `id` | 24 | string | empty | VERIFIED-DATA |
| `map` | 24 | string, `.hsc` path | empty | VERIFIED-DATA |
| `music` | 24 | string, `.mo3` path | empty | VERIFIED-DATA |
| `textures` | 24 | string, directory prefix | empty | VERIFIED-DATA |
| `hmin` | 24 | float | 0.0 | VERIFIED-DATA |
| `hmax` | 24 | float | 0.0 | VERIFIED-DATA |
| `fog` | 24 | 3 floats (rgb) + 2 floats (near/far) | none, `hasFog=false` | VERIFIED-CODE (shape) |
| `sun` | 24 | 9 floats | all 0 | VERIFIED-CODE (shape) |
| `water` | 23 | string + 2 floats | none, `hasWater=false` | VERIFIED-DATA |
| `name` | 20 | string | empty | VERIFIED-DATA |
| `night` | 14 | marker (bool) | `false` | VERIFIED-DATA |
| `enableHelic` | 8 | int | **-1** | VERIFIED-CODE |
| `intermission` | 4 | 6 floats | none, `hasIntermission=false` | VERIFIED-DATA |

13 distinct keys appear in the data, all mapped to typed `LevelDef` fields;
nothing is left uninterpreted. The parser recognizes no other keywords
(unlike `obj.md`/`ps.md`, every keyword the level-list parser tests is also
used at least once in the shipped file).

### `id` / `name`

`id` (VERIFIED-DATA, always present, 24/24) is a short internal identifier
(`"mission1"`, `"intro2"`, ...). `name` (20/24) is the player-visible mission
title (`"Mission 1: Tutorial"`); the 4 blocks missing it are exactly the 4
intro/intermission cutscenes (`id` values `intro1`..`intro4`), which carry an
`intermission` statement instead and are presumably never shown in a mission
list — VERIFIED-DATA correlation (checked exhaustively: every `id` matching
`intro\d` lacks `name` and has `intermission`; every other `id` has `name`
and lacks `intermission`), GUESS on the "why" (UI doesn't need a title for a
cutscene).

### `map` / `music` / `textures`

`map`: an `.hsc` heightmap/level-geometry file under `maps\`.  `music`: an
`.mo3` track under `music\`. `textures`: **not a single file** but a
directory prefix (e.g. `"textures\desert"`) — `validate()` checks that the
directory has at least one file under it in the mounted `Vfs`, rather than
checking a specific filename, since the actual per-surface texture names
are resolved elsewhere (in the `.hsc` map format, out of scope here).

### `hmin` / `hmax`

Floats; VERIFIED-DATA every one of the 24 levels sets both. GUESS: height
bounds for the level's terrain/heightmap, consistent with typical usage and
the field names in the executable's own local-variable naming in the
decompilation, but not confirmed against the `.hsc` renderer.

### `fog`

```
fog := "fog" r:float g:float b:float near:float far:float
```

VERIFIED-CODE shape (5 unconditional `atof` calls); VERIFIED-DATA every use
supplies all 5. `hasFog` is set whenever the statement appears (VERIFIED-CODE:
a presence-flag byte is set alongside the 5 floats). A level without `fog`
(none in the shipped data — all 24 have it) would render with no fog.

### `sun`

9 floats, VERIFIED-CODE shape and VERIFIED-DATA every use supplies all 9;
by the values actually used (e.g. mission 1's `sun 0.9 0.9 0.9 -1.0 -0.5 1.0
0.3 0.3 0.1`, VERIFIED-DATA) this is very plausibly (GUESS, not read from a
named field) 3 groups of 3: a bright/diffuse RGB, a direction vector
(components in roughly [-1, 1]), and a dimmer ambient/fill RGB — but
`defs.h`/`LevelDef::sun` deliberately keeps this as an opaque 9-float array
rather than splitting it into named sub-fields, since nothing here proves
the grouping.

### `water`

```
water := "water" texture:STRING level:float alpha:float
```

VERIFIED-DATA shape (23/24 levels; the one without it, VERIFIED-DATA
checked, still has `hmin`/`hmax`/`fog`/`sun` like the others — simply no
water plane in that mission). `level` (GUESS name; e.g. `-68`, `-65` in the
data — a height/altitude in the same units as `hmin`/`hmax`) and `alpha`
(consistently `0.4` in the shipped data, GUESS: water transparency).

### `night`

A bare marker (no arguments), VERIFIED-DATA. Present on 14 of 24 levels;
used elsewhere in the engine (VERIFIED-CODE, `G_InitObject @0x00409ba0`) to
gate whether `attach night` sub-objects are actually spawned — see
`docs/spec/obj.md`'s `attach` section.

### `enableHelic`

An int, VERIFIED-DATA (8/24 levels set it, e.g. `enableHelic 7`).
VERIFIED-CODE default: **-1** (the record's initial per-block setup writes
the literal `0xffffffff` into this field before the statement loop runs —
distinct from the "just memset to 0" default every other numeric `LevelDef`
field gets). GUESS on exact meaning (a helicopter-enemy spawn budget/count,
by name), VERIFIED-CODE only on the default value and the fact that it's a
plain int otherwise.

### `intermission`

```
intermission := "intermission" 6 floats
```

VERIFIED-DATA shape, present on exactly the 4 intro/intermission entries
(see "`id`/`name`" above). VERIFIED-CODE: presence sets a flag byte
alongside the 6 floats, exactly like `fog`. GUESS on the 6 floats' grouping
(plausibly a camera position + look-at point, by analogy with the values
used, e.g. `512 256 270  -50 0 0`).

## Level cap

VERIFIED-CODE: the parser refuses to start a 33rd block (`if (count == 0x20)
{ ...; return; }`, i.e. a hard cap of 32), logging `Too many levels in
level-list file.`. The shipped file has 24, well under the cap.

## Unresolved references

VERIFIED-DATA: running `validate()` against the shipped data finds **zero**
unresolved `map`/`music`/`textures`/`water` references — every level's map
file, music file, texture-set directory, and (where present) water texture
exist in the mounted `Vfs`.

## Canonical serialization

`tools/ref/defs.py`'s `canonical_level()` and `engine/src/game/defs.cpp`'s
`canonicalLevel()` produce, in order, one `key=value` line per field
(formatting rules as in `docs/spec/obj.md` "Canonical serialization"; the
golden summary keys each level by its `id`, since `levels.txt`'s blocks are
anonymous):

```
id, name, map, music, textures, hmin, hmax, hasFog, fogColor, fogNear,
fogFar, sun, hasWater, waterTexture, waterLevel, waterAlpha, night,
enableHelic, hasIntermission, intermission
```

## Changelog

- 1.0 (WP-1B): initial version.
