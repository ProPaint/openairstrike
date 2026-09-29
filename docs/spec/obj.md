# Object definitions (`objects\*.obj`)

Spec version 1.0. Reference implementation: `tools/ref/defs.py`.
Engine implementation: `engine/src/game/defs.cpp`
(`engine/include/as3d/defs.h`, `ObjectDef`).

Builds on `docs/spec/text-blocks.md` (the generic brace-block grammar):
each `.obj` file is a sequence of named blocks, one per object, whose
statements this document gives meaning to. All claims are VERIFIED-DATA
against the 30 shipped `objects/*.obj` files (864 blocks total) unless
tagged otherwise. Code-derived claims cite the v1.70 executable address
(VERIFIED-CODE); the primary source is `ParseObject` at `0x0040a160`
(disassembled/decompiled via the Ghidra export in `re/out/v170/`, not
included in the repository).

## Statement reference

| Key | Count | Type / unit | Default when absent | Confidence |
|---|---|---|---|---|
| `type` | 108 | enum `ObjectType` | `Model` (0) | VERIFIED-CODE |
| `model` | 700 | string, `.mdl` path | empty | VERIFIED-DATA |
| `skin` | 732 | string, `.tga` path | empty | VERIFIED-DATA |
| `envmap` | 69 | string, `.tga` path | empty | VERIFIED-DATA |
| `blend` | 283 | enum `BlendMode` | `None` (0) | VERIFIED-CODE |
| `envmode` | 69 | enum `EnvMode` | `None` (0) | VERIFIED-CODE |
| `rflag` | 463 | flag, OR-combined into `u32 rflag` | 0 | VERIFIED-CODE |
| `shadow` | 302 | enum `ShadowMode` | `None` (0) | VERIFIED-CODE |
| `sort` | 173 | enum `SortMode` | `Opaque` (0) | VERIFIED-CODE |
| `health` | 162 | int | 0 | VERIFIED-DATA |
| `damage` | 61 | int | 0 | VERIFIED-DATA |
| `score` | 132 | int | 0 | VERIFIED-DATA |
| `flag` | 450 | flag, OR-combined into `u32 flags` | 0 (see below for non-Model types) | VERIFIED-CODE |
| `touch` | 135 | enum `TouchMode` | `None` (0) | VERIFIED-CODE |
| `player` | 10 | marker, sets `kind = Player` | `kind = None` | VERIFIED-CODE |
| `enemy` | 159 | marker, sets `kind = Enemy` | `kind = None` | VERIFIED-CODE |
| `item` | 0 | marker, sets `kind = Item` | `kind = None` | VERIFIED-CODE (unused in data) |
| `scale` | 0 | float | 0.0 | VERIFIED-CODE (unused in data) |
| `bbox_scale` | 9 | 3 floats | (0.7, 0.7, 0.7) | VERIFIED-CODE |
| `min` | 108 | 4 floats | (0,0,0,0), `hasBbox=false` | VERIFIED-DATA (always paired with `max`) |
| `max` | 108 | 4 floats | (0,0,0,0), `hasBbox=false` | VERIFIED-DATA (always paired with `min`) |
| `frames` | 11 | 2 ints (start, end) | (0, 0) | VERIFIED-DATA |
| `light` | 13 | int + 3 floats | none, `hasLight=false` | VERIFIED-DATA (shape); names GUESS |
| `light_dir` | 7 | int + 3 floats + 3 floats + int | none, `hasLight(Dir)=false` | VERIFIED-DATA (shape); names GUESS |
| `script` | 611 | string, `scripts\...\*.scr` path | empty | VERIFIED-DATA; see `docs/spec/rcsl-container.md` |
| `attach` | 1251 (across 397 objects use ≥1) | see "attach" below | none | VERIFIED-CODE |

24 distinct keys appear in the shipped data; every one is listed above and
maps to a typed `ObjectDef` field. **Nothing is left uninterpreted.** `item`
and `scale` are recognized by the parser (VERIFIED-CODE) but never occur in
any of the 864 blocks (VERIFIED-DATA) — they are typed fields that simply
always hold their default in the shipped data.

### `type`

```
type := "type" ( "TYPE_MODEL" | "TYPE_SPRITE" | "TYPE_MARK" | "TYPE_HSPRITE" | "TYPE_VSPRITE" )
```

VERIFIED-CODE values (`ParseObject @0x0040a160`): `TYPE_MODEL=0`,
`TYPE_SPRITE=1`, `TYPE_MARK=2`, `TYPE_VSPRITE=4`, `TYPE_HSPRITE=3` (note the
executable's own string-comparison order tests `VSPRITE` before `HSPRITE`,
but the numeric values are as given here). An object whose `type` ends up
**not** `Model` automatically gets `FL_POINT_COLLISION` (`0x1000`) OR'd into
its `flags`, unconditionally (VERIFIED-CODE, same function, right after the
`type` branch) — this is the flag bit responsible for point-vs-box collision
on non-solid-geometry objects.

**Objects with no `model`** are exactly the ones authored with a non-Model
`type`, or a handful of pure marker/anchor objects with type `Model` (the
default) that never set `model` at all: `TYPE_SPRITE`/`TYPE_HSPRITE`/
`TYPE_VSPRITE` objects are billboards or oriented planes drawn from a `skin`
texture with no 3D mesh (a `.mdl` model is meaningless for them); `TYPE_MARK`
objects are decals/ground marks, also texture-only. A small number of
`Model`-typed, model-less blocks exist purely as **attach anchors** or
**light sources** with no visual representation of their own (e.g. several
`*_light` objects in `buildings.obj`/`effects.obj` exist only to carry a
`light`/`light_dir` statement and be `attach`ed to something else) — GUESS,
inferred from naming and the absence of both `model` and `skin`, not proven
against renderer code.

### `blend` / `envmode` / `envmap`

`envmap` (VERIFIED-DATA: a `.tga` path) also unconditionally ORs `0x20` into
the *same* bitfield `rflag`'s named bits live in (VERIFIED-CODE,
`ParseObject`, right after the `envmap` string copy) — a genuinely separate
bit from any of the named `RF_*` constants (it is not `RF_NODLIGHT`, which is
`0x200`). Since no `rflag` keyword string produces `0x20`, it can never be
set independently of `envmap`; `defs.h` names it `RF_ENVMAP_IMPLIED` and the
loader ORs it into `ObjectDef::rflag` whenever `envmap` is non-empty. It
carries no information beyond "is `envmap` set", but is included in `rflag`
for fidelity with the original's own bitfield.

`blend` VERIFIED-CODE values: `BLEND_ALPHA=1`, `BLEND_ADD=2`,
`BLEND_FILTER=3` (0 = no blending / opaque). `envmode` VERIFIED-CODE values:
`ENV_GLITTER=1`, `ENV_CHROME=2`, `ENV_QUAD=3` (0 = none). Both are shared
numeric spaces with `particles/*.ps`'s `blend_mode` (`BlendMode` is the same
enum for both).

### `rflag`

```
rflag := "rflag" ( "RF_NOLIGHTING" | "RF_NOCULLING" | "RF_NODEPTHTEST"
                  | "RF_NODEPTHWRITE" | "RF_NODLIGHT" | "RF_BANNER" )
```

One flag per statement; repeated `rflag` statements OR their bits together
(VERIFIED-CODE: each branch does `rflag |= bit`, never `=`). VERIFIED-CODE
bit values: `RF_NOLIGHTING=0x1`, `RF_NOCULLING=0x2`, `RF_NODEPTHTEST=0x4`,
`RF_NODEPTHWRITE=0x8`, `RF_NODLIGHT=0x200`, `RF_BANNER=0x10000`. A 7th bit,
`0x20`, lives in the same field but has no `rflag` keyword of its own — see
`envmap` below, which is the only way to set it. `particles/*.ps` has its
own, smaller `rflag` grammar (only the first four bits; see
`docs/spec/ps.md`).

### `shadow`

```
shadow := "shadow" ( "SHADOW_PROJECTED" | "SHADOW_PROJECTED_LOW" | "SHADOW_PROJECTED_HIGH"
                    | "SHADOW_PLANAR" | "SHADOW_PLANAR_LOW" | "SHADOW_PLANAR_HIGH"
                    | "SHADOW_PLANAR_PROJECTED" | "SHADOW_PLANAR_PROJECTED_LOW"
                    | "SHADOW_PLANAR_PROJECTED_HIGH" )
```

VERIFIED-CODE (`ParseObject @0x0040a160`): the keywords are first parsed to
`PROJECTED=1`, `PROJECTED_LOW=2`, `PROJECTED_HIGH=3`, `PLANAR=4`,
`PLANAR_LOW=5`, `PLANAR_HIGH=6`, `PLANAR_PROJECTED=7`,
`PLANAR_PROJECTED_LOW=8`, `PLANAR_PROJECTED_HIGH=9` — but immediately
afterward, three `if` statements remap `4→7`, `5→8`, `6→9`. The net,
final effect (what `ObjectDef::shadow` holds): **`SHADOW_PLANAR` and
`SHADOW_PLANAR_PROJECTED` are the same numeric value (7) and therefore
indistinguishable after loading**, likewise `*_LOW` (8) and `*_HIGH` (9) in
their pairs. `ShadowMode` in `defs.h` only has the 6 distinct final values
(`None=0`, `Projected=1/2/3`, and the merged `Planar*=7/8/9`).
`tank_small_green`'s `shadow SHADOW_PLANAR_LOW` (VERIFIED-DATA,
`objects/tanks.obj`) therefore loads as `ShadowMode::PlanarLow` = 8.

### `sort`

VERIFIED-CODE: `SORT_OPAQUE=0`, `SORT_TRANS=2`, `SORT_EFFECT=3`. Value `1`
has no keyword anywhere in the executable and is never produced by the
parser or seen in the data; it is simply unused (reserved, retired, or a
value some other, non-text-driven code path sets directly).

### `flag`

```
flag := "flag" ( "FL_ONGROUND" | "FL_ONGROUND_NORMAL" | "FL_ONWATER"
                | "FL_NODRAW" | "FL_TEMPORARY" | "FL_NONTARGET" | "FL_POINT_COLLISION" )
```

OR-combined across repeated statements (VERIFIED-CODE, same OR-in pattern as
`rflag`). VERIFIED-CODE bit values: `FL_ONGROUND=0x1`, `FL_ONWATER=0x4`,
`FL_NODRAW=0x10`, `FL_TEMPORARY=0x20`, `FL_NONTARGET=0x100`,
`FL_POINT_COLLISION=0x1000`. `FL_ONGROUND_NORMAL` is not a separate bit: it
literally ORs in the constant `3` (`0x1 | 0x2`) — i.e. it sets `FL_ONGROUND`
*and* a second bit (`0x2`) that has no keyword of its own anywhere in the
binary (`defs.h` names it `FL_ONGROUND_NORMAL_EXTRA`). `FL_POINT_COLLISION`
is also auto-applied to every non-`Model` object regardless of any `flag`
statement (see `type` above).

### `touch`

VERIFIED-CODE: `TOUCH_ENEMIES=1`, `TOUCH_PLAYER=2`, `TOUCH_ALL=3` (0 = none).
Also used, with the same numeric meaning, as the first argument of a
particle system's `damage` statement (`docs/spec/ps.md`).

### `player` / `enemy` / `item`

These three bare (no-argument) statements are **not** independent boolean
markers: VERIFIED-CODE, `ParseObject @0x0040a160` sets the *same* field
(`ObjectKind`) to `1`/`2`/`3` respectively for whichever one is written last
in the block (there is no OR-combination here, unlike `flag`/`rflag`; each
assigns, it doesn't merge). `item` never appears in any shipped `.obj` file.
A block with none of the three has `kind = None` (0) — most objects (scenery,
weapon projectiles, particles-carriers, etc.) fall in this default.

### `scale` / `bbox_scale` / `min` / `max`

`scale` (a single float) is recognized by the parser (VERIFIED-CODE) but
**never appears in the shipped data** — 0 uses across all 864 blocks
(VERIFIED-DATA). Its VERIFIED-CODE default is a raw zero from the record's
initial `memset`; nothing in `ParseObject` special-cases 0 to mean "1.0", so
whatever later code reads this field must treat 0 as "no override" on its
own (GUESS, out of `ParseObject`'s scope to confirm).

`bbox_scale` (3 floats, x/y/z multipliers on the model's bounding box)
VERIFIED-CODE defaults to `(0.7, 0.7, 0.7)` — set explicitly before the
per-statement loop runs, as the literal float bit pattern `0x3f333333`
(`= 0.7`) written into all three components.

`min`/`max` (4 floats each) always occur together in the shipped data (108
uses of each, always in the same 108 objects, VERIFIED-DATA) and are treated
as a single "has an explicit bounding box" flag on `ObjectDef` (`hasBbox`).
The 4th component's meaning is GUESS (unconfirmed; plausibly a bounding
radius or homogeneous padding — `ParseObject` just does 4 unconditional
`atof` calls per statement with no named 4th field visible in the
decompilation).

### `frames`

2 ints: VERIFIED-DATA shape (`frames <start> <end>`), used exclusively on
sprite-family objects for a frame range into a texture atlas; exact
semantics beyond "two integers" are GUESS.

### `light` / `light_dir`

```
light     := "light" radius:int r:float g:float b:float
light_dir := "light_dir" radius:int r:float g:float b:float dx:float dy:float dz:float angle:int
```

VERIFIED-CODE (`ParseObject`): both read the same 4 values first (1 `atol` +
3 `atof`); `light_dir` then reads 3 more floats and one more int-as-float.
Field *names* (`radius`, color, `direction`, `coneAngle`) are GUESS, inferred
from typical point/spot-light parameterization and the numeric ranges
actually used in the data (radius e.g. 100–400; direction components in
[-1, 1]; trailing value e.g. 60–110, consistent with a cone half-angle in
degrees) — not read from any string constant naming these fields, since none
exists. `light_dir` also implies `light` is present (`ObjectDef::hasLight`
is set by both).

### `script`

A path into `scripts\...\*.scr`, resolved via the RCSL script format —
see `docs/spec/rcsl-container.md`. `ObjectDef::script` stores the path
as-written; this package's `validate()` only checks the file exists, it does
not parse the script or resolve the object/sound names the script's own
`CASH` entries reference (out of scope; see `docs/spec/rcsl-container.md`).

### `attach`

```
attach := "attach" modifier* target tag
modifier := "abs" | "night" | "id" idname
target := IDENTIFIER | STRING
tag := IDENTIFIER | STRING
```

VERIFIED-CODE (`ParseObject @0x0040a160`): the parser repeatedly checks the
next token against the literal bare words `abs` and `night`, or `id`
followed by one more token (the id name, quoted or bare); as soon as a token
matches none of those three, it is consumed as `target`, and the token
*right after it* is unconditionally consumed as `tag` — regardless of
whether either is quoted. This explains every argument shape actually
observed in the data (VERIFIED-DATA, 1251 statements across 12 distinct
token-count/kind combinations, e.g. `(STR,STR)` ×397 for a plain
`attach "target" "tag"`, up to `(ID,STR,ID,STR,STR)` ×9 for
`attach id "NAME" abs "target" "tag"` with modifiers in any order); there is
no fixed argument-count grammar, only this modifier-then-target-then-tag
token loop. `tag` is frequently the literal bare word `origin` (VERIFIED-DATA,
e.g. `attach PS_CAMPFIRE "origin"`), meaning "attach at the object's own
origin" rather than a named tag point on the target's model — `origin` is
not special-cased by the parser itself, it is simply whatever string ends up
in the tag field, interpreted by the attach/render code (not part of this
package) as a sentinel.

`abs`: VERIFIED-DATA/CODE, sets `AttachDef::absolute` (GUESS on exact
runtime meaning: "absolute" orientation/position rather than relative to the
parent's, inferred from the keyword and its pervasive use on turret/gun
sub-objects that must not inherit the parent's own local transform quirks).
`night`: VERIFIED-CODE, `G_InitObject @0x00409ba0` — an attachment flagged
`night` is only instantiated when the game's global night-mode flag is set;
otherwise this specific `attach` is skipped entirely at spawn time (checked
per-attachment, so a single object can freely mix night-only and
always-on attachments, as `tank_small_green` does: compare its
`svet_far`/`tag_fara1` (night) against `tank_small_green_cannon`/`tag_turret`
(not night)). `id "NAME"`: VERIFIED-CODE, stores an arbitrary label
(`AttachDef::idName`) alongside the attachment; used by other game code
(outside `ParseObject`/`G_InitObject`, not investigated) to address a
specific attachment later, e.g. to detach/replace it at runtime — GUESS on
that specific use, VERIFIED-CODE only on the fact that it's stored.

There is a hard cap of 64 attachments per object (VERIFIED-CODE, checked
before each `attach` statement is parsed; `docs/spec/obj.md`'s loader emits
a load warning and drops any past the 64th rather than crashing — not
observed in the shipped data, where the maximum used is far below 64).

**Target resolution order (VERIFIED-CODE, `G_InitObject @0x00409ba0`):**
an attach `target` is looked up **as a particle-system name first**
(`FUN_00412d10 @0x00412d10`, exact case-sensitive string match against every
loaded `particles/*.ps` block name, in load order, starting at index 0); if
that lookup fails (returns -1), it is looked up **as an object name**
(`FUN_00409860 @0x00409860`, same match style, object table, starting at
index 1). If it resolves as a particle system, a particle-emitter instance
is spawned and attached; if it resolves as an object, an entire object
instance is recursively spawned (`G_InitObject` calls itself) and attached —
i.e. an attached object gets its own full stat block, health, further
attachments, etc., not just a visual. If *neither* lookup succeeds, the
original logs `ERROR: G_InitObject(%s): Unknown attach '%s'.` and skips that
attachment; our `validate()` reports the same case as a warning (see
"Unresolved references" below). The conventional `PS_` name prefix
(VERIFIED-DATA: every particle-system block name in the shipped
`particles/*.ps` files starts with `PS_`) is **only a naming convention**,
never checked by the lookup itself (VERIFIED-CODE) — an object named
`PS_anything` would equally be found by the particle-system lookup first.

## Record limits

VERIFIED-CODE: the object table holds at most **2048** entries
(`if (count == 0x800) { ...; return; }` in `ParseObject`, logging no message
of its own — `G_LoadObjects @0x0040afc0` would simply stop growing the
table); each object holds at most **64** `attach` entries (see "attach"
above). The shipped data (864 objects, at most a handful of attachments per
object) is far under both caps.

## Duplicate object names

VERIFIED-DATA: `objects/tanks.obj` defines `tank_dead` twice (lines 41 and
180; see `docs/spec/text-blocks.md`). VERIFIED-CODE, `FUN_00409860
@0x00409860` (used for every name-based object lookup: `attach` target
fallback and weapon `missile`/`flash`): the lookup scans the object table in
ascending load order and returns on the **first** exact match. So the
second `tank_dead` (line 180, `skin "models/bigjeep/dead.tga"`, a path that
does not even exist in the shipped data — see "Unresolved references") is
loaded into its own table slot (contributing to the 864 total) but is
**permanently unreachable** by name: nothing can ever resolve to it. Our
`DefDatabase::findObject`/`find_object` mirror this: first-defined-wins,
case-insensitive (the original is case-sensitive; all names in the shipped
data are consistently-cased, so this never matters in practice — a
deliberate simplification, not a behavioral claim about the original).

## Unresolved references (VERIFIED-DATA, this package's `validate()`)

Running `validate()` against the real game data (the three shipped paks
mounted through the `Vfs`) finds exactly 13 unresolved references, none of
which involve a duplicate-name ambiguity:

- 9 missing files: `boss3_l2proj`'s model and skin; `cannon4_gun`'s and
  `cannon4_green`'s scripts (both point at a since-moved/renamed path —
  the actual files exist at `scripts\cannons\cannon1\cannon.scr` and
  `scripts\cannons\cannonbase.scr`); `expl_wave`/`expl_wave_big`/
  `p_expl_wave`'s shared skin `models\misc\hlanno2.tga`; `expl1_hit`'s skin
  `gfx\expl3.tga` (only a `.wav` and a `.scr` of that stem exist, no image);
  `boss_tank_expl1`'s and `item_help`'s and `ship_destroyer_radar`'s
  scripts (each has a similarly-named script at a different subpath).
- 1 broken skin path on the **unreachable** second `tank_dead` (see above);
  since nothing can ever resolve to this object, the broken path is inert.
- 1 attach target that resolves to neither an object nor a particle system:
  `objects/weapons.obj`'s `satellite_hit` attaches `PS_LASER_HIT`, which
  does not exist in any shipped `particles/*.ps` file or as an object name.

**Comparison against `third_party_local/original/game.log`:** the shipped
log contains no `ERROR:` lines at all — only ~30 `Tag 'X' not found in model
'Y'` warnings. Those are a **different, narrower check** than anything in
this package: they come from the model-tag resolver (looking up a named
attachment point *inside* a `.mdl` file's own tag table), which requires
parsing the binary model format — entirely out of scope for this package
(`docs/spec/obj.md` never opens a `.mdl`). Conversely, none of our 13
unresolved references appear in the log, which is consistent, not
contradictory: `game.log` only reflects what the specific logged play
session actually instantiated (this build never spawned `boss3_l2proj`,
`satellite_hit`, or the unreachable `tank_dead`, for instance, or its
`ERROR:` calls would show), while `validate()` checks every reference in
every loaded definition exhaustively, whether or not anything in a given
session would ever reach it.

## Canonical serialization (for `testdata/golden/as3d/defs_summary.json`)

`tools/ref/defs.py`'s `canonical_object()` and `engine/src/game/defs.cpp`'s
`canonicalObject()` must produce byte-identical output for the same
`ObjectDef`. The canonical form is a sequence of `key=value` lines (joined
with `\n`, no trailing newline, hashed as raw bytes with SHA-1 — VERIFIED-DATA:
every field used here is pure ASCII in the shipped data, so this is
equivalent to UTF-8 encoding), one line per field **in this exact order**:

```
name, type, kind, model, skin, envmap, blend, envmode, rflag, shadow, sort,
health, score, damage, flags, touch, scale, bboxScale, hasBbox, bboxMin,
bboxMax, hasFrames, frameStart, frameEnd, hasLight, lightRadius, lightColor,
hasLightDir, lightDir, lightConeAngle, script, attachCount,
then for each attachment i in order: attach{i}.target, attach{i}.tag,
attach{i}.id, attach{i}.abs, attach{i}.night
```

Formatting rules: strings verbatim; ints in decimal; enums as their
underlying integer, decimal; bools as `1`/`0`; a `u32` bitmask (`rflag`,
`flags`) as `0x` + 8 lowercase hex digits; a float as `0x` + 8 lowercase hex
digits of its IEEE-754 binary32 bit pattern (i.e. `struct.pack('<f',
v)`/`unpack('<I', ...)` in Python, `memcpy` to a `uint32_t` in C++ — both
x86/x86-64, little-endian, so this is unambiguous); a fixed-size numeric
array as its elements joined with `,`, each formatted per its own type.

## Changelog

- 1.0 (WP-1B): initial version. Full statement inventory, all enum values,
  the `attach` grammar and resolution order, the first-wins duplicate rule,
  and the unresolved-reference list, all VERIFIED-CODE/VERIFIED-DATA against
  the v1.70 executable and the shipped data.
