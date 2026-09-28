# Particle system definitions (`particles\*.ps`)

Spec version 1.0. Reference implementation: `tools/ref/defs.py`.
Engine implementation: `engine/src/game/defs.cpp`
(`engine/include/as3d/defs.h`, `ParticleSystemDef`).

Builds on `docs/spec/text-blocks.md`. Each `.ps` file is a sequence of named
blocks, one per particle system. VERIFIED-DATA against the 6 shipped
`particles/*.ps` files (80 blocks total: `explosions.ps`, `particles.ps`,
`water.ps`, `weapons.ps`, `weapons_enemy.ps`, `weather.ps`). All shipped
particle-system names start with `PS_`, a naming convention only (see
`docs/spec/obj.md`'s "Target resolution order" — the `attach` lookup never
checks the prefix). Code-derived claims cite the v1.70 executable's
per-block parser at `0x00412d80` (referenced from the strings `blend_mode`,
`emit_rate`, `life_time`, `Too many particle system descriptors.`) and the
particle-system name lookup `FUN_00412d10` at `0x00412d10`.

## Statement reference

| Key | Count | Type | Default | Confidence |
|---|---|---|---|---|
| `texture` | 80 | string `.tga` path + 2 ints (frame w/h) | none / (0,0) | VERIFIED-DATA |
| `blend_mode` | 80 | enum `BlendMode` (shared with obj's `blend`) | `None` (0) | VERIFIED-CODE |
| `emit_rate` | 80 | float | 0.0 | VERIFIED-DATA |
| `life_time` | 80 | float | 0.0 | VERIFIED-DATA |
| `init_size` | 80 | 2 floats | (0, 0) | VERIFIED-DATA |
| `init_color` | 80 | 4 floats (r,g,b,a) | (0,0,0, **1.0**) | VERIFIED-CODE (alpha default) |
| `fade_mode` | 80 | enum `FadeMode` | `None` (0) | VERIFIED-CODE |
| `init_frame` | 77 | 2 ints | (0,0), `hasInitFrame=false` | VERIFIED-DATA |
| `size` | 77 | float | 0.0 | VERIFIED-DATA |
| `init_velocity` | 74 | 6 floats | all 0 | VERIFIED-DATA |
| `init_offset` | 70 | 6 floats | all 0 | VERIFIED-DATA |
| `accel` | 59 | 3 floats | (0,0,0) | VERIFIED-DATA |
| `fade_factor` | 29 | float | **1.0** | VERIFIED-CODE |
| `rflag` | 27 | flag, OR-combined, **4-bit subset** of obj's `rflag` | 0 | VERIFIED-CODE |
| `coords` | 25 | enum `CoordMode` | `Decart` (0) | VERIFIED-CODE |
| `emit_mode` | 22 | enum `EmitMode` | `None` (0) | VERIFIED-CODE |
| `damage` | 5 | enum `TouchMode` + 3 numbers | none, `hasDamage=false` | VERIFIED-CODE (shape) |
| `anim_mode` | 3 | enum `AnimMode` | `None` (0) | VERIFIED-CODE |
| `draw_mode` | 3 | enum `DrawMode` | `None` (0) | VERIFIED-CODE |

19 distinct keys appear in the shipped data, all mapped to typed
`ParticleSystemDef` fields. The parser additionally recognizes 7 more
keywords that **never occur in any of the 80 shipped blocks**
(VERIFIED-DATA, 0 occurrences each) — not implemented as `ParticleSystemDef`
fields, since nothing in the shipped data would ever populate them, but
listed here for completeness per "nothing silently dropped": `texture_set`
(a texture atlas alternative to `texture`: count + up to 8 texture indices,
VERIFIED-CODE), `axis` (3 floats, default `(0,0,1)`, VERIFIED-CODE), `spin`
(1 float), `color` (4 floats — a second, separate color field from
`init_color`), `init_angle` (2 floats), `anim_speed` (1 int), `emit_time` (1
int/float). If any future data (a mod, or the sequel's assets) used these,
`validate()`/the loader would need extending; today they are a documented
gap, not a silent drop, because they are provably absent from the shipped
files (`tools/ref/test_defs.py` re-derives the exact key inventory on every
run against `docs/spec/text-blocks.md`'s own per-file-type table, which
lists precisely these 19 keys and no others for `.ps`).

### `texture`

```
texture := "texture" path:STRING frameW:int frameH:int
```

VERIFIED-DATA shape (all 80 uses supply all 3 arguments). `frameW`/`frameH`
GUESS as a texture-atlas frame grid size (paired with `init_frame`'s
starting frame index) — consistent naming, not confirmed against renderer
code.

### `blend_mode` / `fade_mode`

`blend_mode` VERIFIED-CODE values identical to `obj.md`'s `blend`:
`BLEND_ALPHA=1`, `BLEND_ADD=2`, `BLEND_FILTER=3` (0 = none). `fade_mode`
VERIFIED-CODE: `FADE_LINEAR=1`, `FADE_EXP=2` (0 = none) — controls how
`fade_factor` is applied over the particle's `life_time` (GUESS on the exact
curve; VERIFIED-CODE only on the enum values existing).

### `init_color`

4 floats (r, g, b, a). VERIFIED-CODE: the record's initial `memset` zeroes
everything, but a literal `1.0` (`0x3f800000`) is written into the alpha
component specifically, before the per-statement loop runs — so a particle
system that never writes `init_color` defaults to fully-opaque black,
never fully transparent. In the shipped data all 80 particle systems supply
all 4 components explicitly (VERIFIED-DATA), so this default is never
actually exercised by the 80 shipped blocks, but it is exercised by the
default-constructed `ParticleSystemDef` and matters for anything reading a
system that omits the key.

### `fade_factor`

VERIFIED-CODE default `1.0`, same mechanism (a literal float written before
parsing) as `init_color`'s alpha. 29 of 80 shipped systems set it
explicitly; the other 51 rely on this default.

### `rflag`

```
rflag := "rflag" ( "RF_NOLIGHTING" | "RF_NOCULLING" | "RF_NODEPTHTEST" | "RF_NODEPTHWRITE" )
```

Same bit values as `obj.md`'s `rflag` (`0x1`/`0x2`/`0x4`/`0x8`), OR-combined
across repeated statements, but VERIFIED-CODE: the `.ps` parser only ever
tests these 4 keywords, never `RF_NODLIGHT`/`RF_BANNER` (those two only
exist in `ParseObject`'s comparison chain). `ParticleSystemDef::rflag` is
therefore always a subset of `obj.md`'s `RFlagBits`; the loader (mirroring
the original) simply never recognizes the other two spellings for a `.ps`
block (not observed in the data either way).

### `coords`

VERIFIED-CODE values, notably **not** in keyword-alphabetical or
declaration order: `COORD_DECART=0`, `COORD_CILINDER=1`, `COORD_SPHERE=2`
(a Cartesian/cylindrical/spherical emission-space selector; `DECART`/
`CILINDER` sic, matching the executable's own spelling, presumably
transliterated).

### `emit_mode`

VERIFIED-CODE: `EMIT_ONCE=1`, `EMIT_DURATION=2` (0 = default/continuous,
GUESS on the exact meaning of the default state itself, VERIFIED-CODE only
on the two named values).

### `anim_mode` / `draw_mode`

`anim_mode` VERIFIED-CODE: `ANIM_LINEAR=1`, `ANIM_NORMAL=2`, `ANIM_LOOP=3`.
`draw_mode` VERIFIED-CODE: `DRAW_VERT=1`, `DRAW_HORIZ=2` (billboard
orientation, GUESS on exact meaning of "vert"/"horiz", VERIFIED-CODE on the
values existing and being mutually exclusive single-value fields, like
`obj.md`'s `type`/`shadow`/etc., not OR-combined).

### `damage`

```
damage := "damage" ( "TOUCH_ENEMIES" | "TOUCH_PLAYER" ) amount:NUM param2:NUM param3:NUM
```

VERIFIED-CODE shape: reads a `TouchMode`-valued keyword (same enum/values as
`obj.md`'s `touch`; `TOUCH_ALL` is checked for `obj`'s `touch` but *not*
wired up in the `.ps` `damage` branch — VERIFIED-CODE, only `TOUCH_ENEMIES`
and `TOUCH_PLAYER` are compared here), then 3 numbers: the first is run
through `atof` then a second, unidentified conversion function before being
stored (GUESS: a rounding/int-cast step, consistent with "damage amount"
being logically an integer even though written as a float in the source
text — e.g. `damage TOUCH_PLAYER 5 5 5`), the other two are stored as plain
floats (GUESS: radius or falloff parameters). All 5 shipped uses supply all
4 tokens.

## Record limit

VERIFIED-CODE: the particle-system table holds at most **256** entries
(`if (count == 0x100) { ...; return; }`, logging `Too many particle system
descriptors.` if exceeded). The shipped data (80 systems across 6 files) is
far under this cap.

## Unresolved references

VERIFIED-DATA: running `validate()` against the shipped data finds **zero**
particle-system `texture` references that fail to resolve — all 80
`texture` paths exist in the mounted `Vfs`. No particle system is itself the
*target* of a failing `attach` in the shipped `objects/*.obj` files except
via the single dangling `PS_LASER_HIT` reference documented in
`docs/spec/obj.md` (that failure is attributed to the `.obj` side, since the
particle system simply doesn't exist under that name anywhere).

## Canonical serialization

`tools/ref/defs.py`'s `canonical_particle_system()` and
`engine/src/game/defs.cpp`'s `canonicalParticleSystem()` produce, in order,
one `key=value` line per field (formatting rules as in `docs/spec/obj.md`
"Canonical serialization"):

```
name, texture, textureFrameW, textureFrameH, blendMode, rflag, coords,
drawMode, emitMode, emitRate, lifeTime, initOffset, initVelocity, initSize,
hasInitFrame, initFrame, initColor, accel, fadeMode, fadeFactor, size,
animMode, hasDamage, damageTouch, damageAmount, damageParam2, damageParam3
```

## Changelog

- 1.0 (WP-1B): initial version.
