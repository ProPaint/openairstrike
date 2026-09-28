# Static model (`.mdl`)

Spec version 1.1. Reference implementation: `tools/ref/mdl.py`, exercised by
`tools/ref/test_mdl.py` against all 446 shipped `models/**/*.mdl` files. C++
implementation: `engine/include/as3d/model.h`, `engine/src/formats/mdl.cpp`,
exercised by `apps/tests/mdl_test.cpp`.

All integers are little-endian. All floats are IEEE-754 `f32`, little-endian.

Numbers quoted below (file counts, statistics) were produced by scripts run once
over the full shipped corpus while writing this spec; `tools/ref/test_mdl.py` re-
derives the load-bearing ones (byte-accounting, index bounds, normal length,
bbox containment, tag printability) on every run.

## Corpus overview — VERIFIED-DATA

446 files under `models/`. Of these:

- 442 begin with magic `MDL!` and version `2`.
- 4 are exactly zero bytes (see [Empty files](#empty-files)).
- Of the 442 non-empty files, 440 match this spec's layout byte-for-byte (parsed
  length == file length). 2 do not (see [Files that do not fit](#files-that-do-not-fit)).

## Header layout

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | magic `MDL!` |
| 0x04 | u32 | version, always `2` in shipped data |
| 0x08 | u32 | `smooth_normals`: `1` = per-vertex (smooth) normals, `0` = per-face (flat) normals |
| 0x0C | 64 | original texture path, NUL-terminated, cp1251, from the artist's machine |
| 0x4C | u32 | vertex count |
| 0x50 | u32 | uv count |
| 0x54 | u32 | face count |
| 0x58 | u32 | normal count |
| 0x5C | u32 | tag count |
| 0x60 | 24 | bounding box: 6x f32, `(min.x, min.y, min.z, max.x, max.y, max.z)` |
| 0x78 | ... | arrays (vertices, uvs, faces, normals, tags), see below |

Total file size (for the 440 conforming files):

```
0x78 + 12*vertex_count + 8*uv_count + 12*face_count + 12*normal_count + 56*tag_count
```

VERIFIED-DATA: holds exactly for 440/442 non-empty files.

### Version and magic — VERIFIED-CODE

`R_LoadModel` (entered around `0x4116f0` in `AirStrike3D.exe`) reads a 0x78-byte
header into a local buffer, then:

- compares the first 4 bytes against `MDL!` (`cmp DWORD PTR [esp+0x50],0x214c444d`
  at `0x41176d`); on mismatch it logs `"R_LoadModel ('%s'): File header corrupted."`
  (string at VA `0x447c38`) and aborts the load.
- compares the version field (buffer offset 4) against `2` and `3` (`0x41179f`,
  `0x4117a4`); anything else logs `"R_LoadModel ('%s'): Illegal model version."`
  (VA `0x447c64`). Version `3` is accepted by the loader but never appears in the
  shipped data (VERIFIED-DATA: 0/442) — likely a bumped format from later in
  development, or reserved for the sequel.
- if the file cannot be opened at all, it logs `"R_LoadModel: Couldn't open model
  file '%s'."` (VA `0x447c08`).

### `smooth_normals` flag (offset 0x08) — VERIFIED-DATA + VERIFIED-CODE

The loader reads buffer offset 8 (`cmp DWORD PTR [esp+0x58],0x0` at `0x4117ef`),
reduces it to a boolean (`setne dl` at `0x411845`) and stores it at `model+0xa0`
(`mov BYTE PTR [edi+0xa0],dl` at `0x411857`). Later, right after the normal array
is read from disk, it branches on that same boolean (`cmp BYTE PTR [edi+0xa0],0x0;
je 0x411b99` at `0x411959`): when set, it **recomputes** every vertex normal from
the face geometry (see [Normals](#normals)); when clear, it uses the on-disk flat
normals unmodified.

This is not a rendering hint layered on top of independently-meaningful data — it
literally selects which of two different array *shapes* the model uses:

| flag | meaning | normal count |
|---|---|---|
| 1 | smooth (per-vertex) normals | equals vertex count |
| 0 | flat (per-face) normals | equals face count |

VERIFIED-DATA: checked on all 440 conforming files. Excluding the 23 files where
`vertex_count == face_count` (both readings agree, so the flag can't be
cross-checked against counts alone), the flag predicts `normals == vertices` vs.
`normals == faces` with **zero exceptions** across the remaining 417 files. The
task brief's "0/1 correlates with model kind" turned out to be exactly this —
not directory, not presence of tags, just whether the exporter (3ds Max, per the
texture paths) produced smoothed or faceted normals for that particular mesh.

Distribution: flag is `1` in 417 files, `0` in 25 (matches the brief's numbers
exactly); of those 25, 2 are the known-broken files.

### Texture path (offset 0x0C, 64 bytes) — VERIFIED-DATA + VERIFIED-CODE

`lea esi,[esp+0x5c]` at `0x4117df` — buffer offset `0x5c - 0x50 = 0xc` — takes the
address of this field and passes it into a helper (`call 0x418cf0`) before the
model struct is populated further; this is the field consumed as "the" texture
path (later overridden by the object's own `skin` in most cases; note that all
observed models were originally exported with a **local Windows path** from the
artists' machines, e.g. `D:\3dsmax4\_____\AIRSTRIKE\Helics\apache\apache_green.tga`
— directory names were even redacted with underscores in some paths).

The field is 64 bytes, NUL-terminated when the path fits. VERIFIED-DATA: 431/442
files have a NUL inside the field; the remaining 11 fill the whole 64 bytes with
no terminator (the path was too long — e.g.
`models/mapobjects/vishka/vishka1.mdl` stores exactly 64 bytes ending
`...vishka_brownbricks.` with the `tga` extension truncated off). `tools/ref/mdl.py`
handles both cases identically (`split(b"\0", 1)[0]`, which is a no-op when no NUL
is present).

### Counts and bounding box (offsets 0x4C–0x78) — VERIFIED-CODE + VERIFIED-DATA

The 5 counts and the bounding box are read from the header buffer at fixed
offsets and stored directly into the model struct, in this order (each `push
size; call malloc; ...; call fread`-shaped sequence, at `0x411865`–`0x411956`):

1. vertex count (buffer `0x4c`) → allocate `count*12`, read into `model+0xa8`
2. uv count (buffer `0x50`) → allocate `count*8`, read into `model+0xb0`
3. face count (buffer `0x54`) → allocate `count*12`, read into `model+0xb8`
4. normal count (buffer `0x58`) → allocate `count*12`, read into `model+0xc0`

(tag count, buffer `0x5c`, is read the same way slightly further down, at
`0x411ba4`). The allocation sizes (`count*12`, `count*8`, `count*12`, `count*12`)
independently confirm the per-element sizes below, and their *order* confirms the
array order: **vertices, uvs, faces, normals, tags** — resolving the
"sources disagree on the order" ambiguity from the task brief in favour of the
first ordering, not the vertices/normals/uvs/faces alternative.

The bounding box (6 floats at buffer offset `0x60`, i.e. file offset `0x60`,
*immediately after the counts and before the vertex array*) is read directly with
`fld`/`fstp` into `model+0x84..0x98` (`0x4117e8`–`0x41185d`), i.e. it is **not**
a trailing block after the arrays as the task brief's "handful of files" hint
first suggested — the `+24` extra bytes in that formula belong right after the
header, not at the end of the file. (Once that's corrected, the same total-size
formula still holds, which is why the naive end-of-file guess also happened to
balance for the files it was checked against.)

VERIFIED-DATA: for all 440 conforming files, `(min, max)` exactly equals the
componentwise min/max of the file's own vertex array (max absolute difference
across the whole corpus: 0, i.e. bit-for-bit — these are f32 values computed once
by the exporter and never redone).

## Arrays (from file offset 0x78)

In order: vertices, uvs, faces, normals, tags. Each array is tightly packed, no
padding between or within arrays.

### Vertices — VERIFIED-DATA

`vertex_count` entries, 12 bytes each: `(x, y, z)` as `f32`. Position, in the
model's local space.

### UVs — VERIFIED-DATA

`uv_count` entries, 8 bytes each: `(u, v)` as `f32`. Mostly but not always in
`[0, 1]`: across all 440 conforming files, 15,621 UV pairs total, of which 1,336
(8.6%) have a component outside `[0, 1]` — consistent with intentional texture
tiling (observed range: about -2.4 to +20.8), not corruption.

### Faces — VERIFIED-DATA

`face_count` entries, 12 bytes each, as 6x `u16`: `(v0, v1, v2, uv0, uv1, uv2)` —
three vertex indices followed by three UV indices (not interleaved). VERIFIED-DATA:
checked on all 440 conforming files, 0 out-of-range vertex or UV indices anywhere
(covering every face in every file).

**Winding**: `(v0, v1, v2)` is counter-clockwise when viewed from the side the
surface normal points to (i.e. `normal ∝ (v1-v0) × (v2-v0)`, standard right-handed
convention). VERIFIED-DATA: computed the CCW cross product at every vertex of
every face, summed per vertex, and compared its direction (dot product sign)
against the model's own stored normal, for the 419 files with smooth normals that
also parse cleanly and have no out-of-range face indices. 15,222 of 15,276
per-vertex comparisons agree (99.65%); the 34 disagreements are spread over 20
files and are consistent with vertices where adjacent faces have sharply
divergent normals (a real hard edge/seam, where the winding is still consistent
but a simple unweighted sum can point the "wrong" way) rather than a winding
inconsistency in the source data.

### Normals

`normal_count` entries, 12 bytes each: `(x, y, z)` as `f32`. `normal_count`
equals **either** `vertex_count` **or** `face_count` — VERIFIED-DATA: true for
all 440 conforming files, with 0 exceptions (23 files have `vertex_count ==
face_count`, so both are trivially true there; 21 files have `normal_count ==
face_count != vertex_count`; the header's `smooth_normals` flag says which
regime applies, see above). The 21 face-indexed files are small,
hard-edged decoration meshes (banner flame quad strips, boss1 hinge/hatch
pieces, barrels, zabor fences, tracers, gun flashes, splash quads, ...) — exactly
where an artist would turn off vertex smoothing in the exporter.

**Length — GUESS on the exact algorithm, VERIFIED-DATA on the numbers.** Flat
(per-face) normals are unit length: 819/821 are within ±0.01 of 1.0 (min 0.0,
max 1.0000001, mean 0.9976); the 2 exceptions are exactly zero-length, both in
`models/banner/banner.mdl` (degenerate/zero-area triangles).
`tools/ref/test_mdl.py` uses a ±0.02 tolerance for this check (looser than the
±0.01 actually observed, as a safety margin) and reports zero-length exceptions
as warnings rather than failures. Smooth (per-vertex)
normals are **not** unit length in general: min 0.0, max 1.0000146 (i.e. never
more than 1.0 plus float32 slop), mean 0.836, and the disassembly explains why —
see next paragraph.

VERIFIED-CODE: when `smooth_normals` is set, `R_LoadModel` **discards whatever
was read from disk into the normal array and recomputes it** (`0x411959`–
`0x411b8c`): it zeroes the buffer, computes an unnormalized face normal
`(v1-v0) × (v2-v0)` for every face, and for every face adds that raw cross
product into the accumulator of each of its 3 vertices — with **no division by
count and no renormalization**. This is exactly a sum of unit-ish cross products
across a vertex's incident faces, which explains the observed distribution:
magnitude is close to 1.0 on smooth, coplanar-ish regions and drops well below 1
at creases/corners (by the triangle inequality it can never exceed 1 when the
per-face vectors were already ≤1, matching the corpus-wide max of ~1.0). Because
the exporter's own precomputed values already match this same formula closely,
reading the raw on-disk floats (as `tools/ref/mdl.py` does) reproduces the
runtime result to high fidelity, but strictly speaking the on-disk smooth-normal
bytes are redundant/overwritten data as far as the original engine is concerned.
`tools/ref/mdl.py` does not attempt this recompute; it reports the stored bytes
as-is, which is what "every byte accounted for" requires, and documents the
above so a C++ port can decide whether to reproduce the recompute or trust the
file.

Flat (per-face) normals, by contrast, are used as read with no runtime
recomputation (the branch at `0x411959` skips straight to `0x411b99` when the
flag is clear).

**Exact recompute formula (WP-14b, VERIFIED-CODE, `0x4119a0`–`0x411b59`).** For
every face `i = (v0, v1, v2, uv0, uv1, uv2)`:

```
e1 = position[v1] - position[v0]
e2 = position[v2] - position[v0]
fn = cross(e1, e2)          // NOT normalized, NOT divided by anything
accum[v0] += fn
accum[v1] += fn
accum[v2] += fn
```

after processing every face, `accum[v]` (still unnormalized) is the vertex's
final smooth normal. This is a plain sum, not a weighted or unweighted
*average* (there is no division by the number of incident faces anywhere in
the disassembly) — the "close to 1.0 on flat regions" behaviour noted above is
an accident of `cross(e1, e2)` already being close to unit length for the
roughly-unit-length, roughly-perpendicular edges typical of these meshes, not
a deliberate normalization.

**Runtime (re-)normalization — VERIFIED-CODE, none.** `GL_Init` (`0x412220`)
calls `glDisable(0xba1)` (`GL_NORMALIZE`) once at startup; grepping every
`glEnable`/`glDisable` call site in the executable (all 23 callers of either
import) turns up no other reference to `0xba1`, and no reference anywhere to
`0x803a` (`GL_RESCALE_NORMAL`) at all. So the original fixed-function renderer
never renormalizes a normal before lighting with it — the unnormalized,
sub-1.0-magnitude smooth normals computed above are what actually reach the
lighting equation in v1.70. A sharp crease is, by construction, dimmer than a
flat face lit the same way.

**Our loader's decision (not a v1.70 fact — a choice for this engine).**
`loadModel` reproduces the exact recompute above (so the *direction* matches
the original bit-for-bit, modulo float rounding), but then **normalizes** the
result before returning it in `ModelData::normals`, controlled by the named
constant `kNormalizeRecomputedNormals` in `engine/src/formats/mdl.cpp`
(currently `true`). Rationale: our renderer is not the fixed-function pipeline
being reverse-engineered, nothing downstream should have to know that a
"normal" might legitimately be 7% of unit length, and unnormalized normals
are a well-known footgun (they silently break any lighting model that assumes
unit length, e.g. anything using `dot(N, L)` directly for diffuse). This is a
deliberate, documented divergence from the original's visible behaviour, not
a bug — flip the constant to `false` to match v1.70 exactly if a future
reason to do so turns up.

## Tags

`tag_count` entries starting right after the normal array, 56 bytes each:

| Offset | Size | Field |
|---|---|---|
| 0 | 32 | name, NUL-terminated ASCII |
| 32 | 12 | position: 3x f32 `(x, y, z)`, in model space |
| 44 | 12 | direction: 3x f32 `(x, y, z)` |

VERIFIED-DATA: all 733 tag names across the corpus are printable ASCII (checked:
100%).

### The trailing 12 bytes — VERIFIED-DATA on the shape; **settled, WP-14b: dead data in v1.70**

Confirmed **not** to be Euler angles: across all 733 tags in the corpus, the
magnitude of this field is either exactly zero (229 tags) or within [0.9, 1.1] of
1.0 (504 tags) — a clean bimodal split with **zero** values anywhere in between.
Euler angles (degrees or radians) would not produce this distribution. This is a
unit direction vector when present, and the zero vector when the attachment
point has no meaningful direction (e.g. simple flare/light points, which in the
apache/comanche/helic\* family consistently have `(0,0,0)` here, while
gun/rocket/cannon/kabina/headlight tags consistently carry a unit vector).

**WP-14b traced every consumer and found that v1.70 never reads this field —
VERIFIED-CODE.** `R_LoadModel` (`0x4116f0`) reads each 56-byte on-disk tag
record into a stack buffer (`LEA EDX,[ESP+0xcc]; MOV EAX,0x38; CALL
0x00421120` at `0x411c07`–`0x411c14`, i.e. exactly the 56 bytes this spec
already documents), then copies only the **name** (byte-for-byte,
`0x411c19`–`0x411c3e`) and the **position** (from the corresponding stack
slots, into runtime offset `+0x44`) into the model's runtime tag array. The
runtime tag record is actually 80 bytes (`_malloc(tagCount * 0x50)` at
`0x411bc2`), not 56: bytes `+0x20`..`+0x43` (three 3-float "axis" rows, X/Y/Z)
are filled not from the file but from a **fixed 9-float constant at VA
`0x456cc4`** (`.data`, file offset `0x56cc4`), which is the plain 3×3
**identity matrix** `(1,0,0, 0,1,0, 0,0,1)` (read back and confirmed
byte-for-byte). The on-disk direction vector is read into a stack region with
no named destination and is never copied anywhere — it is loaded from disk and
immediately discarded. (The same function has a separate branch for a
hypothetical on-disk version `3`, which the loader accepts but which never
appears in shipped data: for version 3 it reads the whole 80-byte runtime
record directly with one `FUN_00421120` call, i.e. version 3's tag record
would already store 3 axis rows instead of a direction vector. Version 2's
loader path looks exactly like a compatibility shim for an older,
simpler on-disk shape that the axis-based runtime representation superseded
before any version-3 asset shipped.)

The tag lookup used by attachment (`0x410380`, called `"Tag '%s' not found in
model '%s'"` on miss, VA `0x447be4`) returns, for a found tag, exactly this
runtime data: position from `+0x44` and the three axis rows from `+0x20`,
`+0x2c`, `+0x38`. Since every shipped tag's axis rows are the fixed identity
constant, **every tag attachment in v1.70 resolves to an identity rotation,
regardless of the tag's on-disk direction vector.** The direction vector is
authored data (it is not zero-filled at random — it is a clean, deliberate
unit vector on weapon/light mounts) that the shipped engine build simply never
consumes; treat it as **dead data for gameplay purposes** in this version,
almost certainly a leftover from before the axis-matrix tag format was
finalized. GUESS (unproven, but the natural reading of the evidence): had a
version-3 asset shipped, this vector might have been *fed into* the axis
computation that produced those 3x3 rows at export time, rather than being
computed at load time — but nothing in v1.70 does that computation, so this
remains speculation about a codepath this executable doesn't have.

`tools/ref/mdl.py` and `ModelTag` still expose the raw on-disk direction
vector as `ModelTag::direction` (it is real, deliberately-authored data, and a
future format revision or a from-scratch reading of it might resurrect a use
for it) — but nothing in `loadModel`/`buildRenderMesh` derives an orientation
from it, matching v1.70's own behaviour.

#### The attach position/orientation formula — VERIFIED-CODE

Traced via the script builtin table at `0x456f70` (8-byte name-pointer/
function-pointer records; see `docs/spec/rcsl-container.md`) to `AttachEntity`
(`0x41aa30`, mostly parent/child linked-list bookkeeping) and, more usefully,
to the per-frame entity update `0x405a60`, which for every attached child
calls `0x404850(child->absFlag)` every time the parent updates (i.e.
**attachment is a live, continuously-recomputed relationship, not a one-shot
copy at spawn time** — a moving/rotating parent keeps carrying its attached
children every frame):

```
found, tagPos, tagAxis = FindTag(parentModel, tagName)   // 0x410380; tagAxis is always
                                                          // identity in v1.70, see above
if not found: detach the child (clear its parent link) and stop

childLocalOffset = child.declaredOffset                  // child+0x9b/0x9f/0xa3, a
                                                          // per-child constant from its
                                                          // own definition
if abs:
    local = tagPos + childLocalOffset
else:
    local = tagPos

if parent.scale > 0.01:
    local *= parent.scale

child.worldPos = parent.worldPos + parent.rotationMatrix * local

if not abs:
    child.worldPos += childLocalOffset   // added AFTER the transform, i.e. in world
                                          // space, unrotated by the parent

child.rotationMatrix = AnglesToAxis(child.ownAngles)      // 0x41e390 / the "AnglesToAxis"
                                                            // builtin, from the CHILD's own
                                                            // angle state, independent of
                                                            // the parent...
if abs:
    child.rotationMatrix = child.rotationMatrix * parent.rotationMatrix  // ...unless abs,
                                                            // which additionally composes
                                                            // it with the parent's rotation
                                                            // (0x41e650, a plain 3x3 matrix
                                                            // multiply, called twice for two
                                                            // tracked copies of the matrix)
```

**What `abs` changes (VERIFIED-CODE, `0x404850`):** without `abs`, a child
keeps its own independent world orientation (only its *position* is tag- and
parent-rotation-relative; its own declared local offset is added post-hoc in
world space) — this is the right model for e.g. a headlight beam or a flare
that shouldn't spin with a rotating turret. With `abs`, the child's position
offset is folded into the tag-relative, parent-rotated placement, **and** its
orientation is composed with the parent's rotation matrix, i.e. the child is
rigidly welded to the parent's full transform (position and orientation) —
the right model for e.g. a turret's gun barrel or a rocket pod that must
visibly track the parent's attitude. (`abs` is *not* about a coordinate
system, despite the name reading that way at first: it selects **rigid vs.
independent-orientation attachment**.)

Because the tag's own rotation contribution is always identity in v1.70, a
port of this system only needs `ModelTag::position` and the two attach-time
choices above (`abs` on/off just changes how the child's *own* declared
offset and orientation combine with the parent, not anything read from the
tag itself); a faithful C++ entity system implementing `attach` should apply
exactly this formula. This work package's loader (`loadModel`) only reads and
exposes the tag; the transform above belongs to the entity/attachment system,
out of scope for `engine/src/formats/mdl.cpp`.

### Tag name inventory — VERIFIED-DATA

158 distinct tag names across the corpus (733 tag instances total). By far the
most common are weapon/light attachment points following a `tag_<purpose><n>`
convention: `tag_gun*` (163 instances), `tag_flare*` (172), `tag_fara*` (63,
headlights), `tag_light*` (26), `tag_vint*` (49), `tag_cannon*` (32),
`tag_rocket*` (23), `tag_smoke*`, `tag_wheel*`,
`tag_kabina` (cockpit/cabin mount point), `tag_vint*` (Russian *винт*, "rotor/propeller"),
`tag_turret`, `tag_hvost` (Russian *хвост*, "tail"), `tag_bashnya` (Russian
*башня*, "turret/tower"), and one-off names for specific bosses/vehicles
(`tag_door_left`, `tag_sphere`, `tag_left_fan`, ...).

A handful of names are clearly **not** authored attachment points but leftover
3ds Max scene helpers exported by accident: `Axis0`–`Axis8` (2 files, both laser
cannon variants), `Point01` (1 file), `Dum_antenna1`/`Dum_antenna2` (1 file,
"dummy" helper naming), and `V2Grp01`/`V2Xfm01`–`V2Xfm20` (4 ship files: cutter,
cutter_cannon, ship_dead, ship_destroyer(_cannon) — 20 identically-named helper
points per ship, almost certainly a rigid-body or wave-deformer helper chain).
None of these appear in any `attach` statement in `assets_extracted/objects/*.obj`
— cross-checked against all 1,249 `attach` statements in the 30 `.obj` files —
supporting the idea that they are inert exporter noise rather than functional
tags. `tools/ref/mdl.py` still parses and reports them like any other tag, since
the file format itself does not distinguish them.

### Cross-check against `objects/*.obj` — VERIFIED-DATA

Every `attach` statement has the shape `attach [abs] [id "<ID>"] [night] <child>
<tag>`, where `<child>` and `<tag>` may each be a quoted string or a bareword
(e.g. `attach abs id "GUNS" "jeep_bug_big_guns" "tag_guns"`, or `attach
PS_LASER_GREEN_ISKRA origin`); in every observed line the tag is the *last*
token and the child reference is the second-to-last, regardless of how many
optional modifier tokens precede them. `origin` is used as an implicit tag name
(matches the task brief) and never needs to resolve to an actual tag record.

Parsing all 30 `objects/*.obj` files (863 object definitions, 1,249 `attach`
statements) and checking every non-`origin` tag against the tags actually stored
in the attaching object's own `model` file: **8 mismatches**, all pre-existing
in the shipped data (not something introduced by this analysis):

| Object | Model | Missing tag | Child |
|---|---|---|---|
| `jeep_bug`, `jeep_bug_green` | `jeeps/jeep_bug/jeep_bug.mdl` | `tag_fara1` | `svet_far` |
| `jeep_bug`, `jeep_bug_green` | `jeeps/jeep_bug/jeep_bug.mdl` | `tag_fara2` | `svet_far_wol` |
| `tank_flame_cannon` | `tanks/tank_flame/tank_flame_b.mdl` | `tag_fara` | `svet_far_wol` |
| `tank_big_guns` | `tanks/tank_big/tank_big_gun.mdl` | `tag_flare1`, `tag_flare2` | `helic_redflare` |
| `tank_big_guns` | `tanks/tank_big/tank_big_gun.mdl` | `tag_fara` | `svet_far_wol` |

In each case the model genuinely lacks any headlight/flare tag (e.g.
`jeep_bug.mdl`'s only tags are `tag_wheel1..4` and `tag_cannon`) — these read as
stale `.obj` content from an earlier model revision, not a parser bug; the
attach presumably silently no-ops in the original engine. 55 further objects
have `attach` lines but no own `model` line (they only ever attach at `origin`,
e.g. particle-effect composites like `campfire`), which is consistent and not a
mismatch.

## Coordinate system

- **Up axis: Z** — GUESS supported by consistent data, not proven by executable
  code. Reasoning: across `apache.mdl`/`comanche.mdl`/`helic1.mdl`, the Z extent
  of the bounding box is by far the smallest of the three axes (e.g. apache: X
  span ≈27, Y span ≈75, Z span ≈17), as expected for a helicopter that is much
  longer nose-to-tail than it is tall; and the main-rotor tag (`tag_vint1`) sits
  at a distinctly higher Z than the cabin tag (`tag_kabina`) and the fuselage
  centroid in every helicopter checked, exactly where a rotor mast should be
  relative to the cabin it's mounted on.
- **Forward/length axis: Y**, **right axis: X** — same reasoning (Y has the
  largest bbox span on every aircraft checked, matching nose-to-tail length).
- **Handedness: right-handed**, consistent with the CCW-front-face winding above
  (`normal = (v1-v0) × (v2-v0)`, standard right-hand rule).

### Winding vs. the renderer's GL state — VERIFIED-CODE, reconciliation is GUESS

`GL_Init` (`0x412220`) is the *only* caller of both `glFrontFace` and
`glCullFace` in the executable (confirmed via the import table's caller
lists), and sets:

```
glFrontFace(0x0901)   // GL_CW  -- clockwise-in-window-space triangles are "front"
glCullFace(0x0405)    // GL_BACK
glEnable(0x0B44)      // GL_CULL_FACE
```

and neither is ever called again, so this is the renderer's state for the
whole game. This does **not** contradict the CCW-in-object-space finding
above: `glFrontFace` classifies windows-space (post-projection) winding, which
depends on the model→view→clip transform actually used to submit each
triangle, not on the raw file data. WP-14b did not trace the specific vertex
submission order for the static-model draw path (the code found and read
during this package turned out to be level/BSP and shadow-projection
rendering, not the `.mdl` entity draw call) closely enough to say *whether* an
index/vertex order reversal or a chirality flip baked into the view/camera
setup is what reconciles object-space CCW with window-space CW — only that
both individual facts are independently verified. **Open question**, left for
whichever future work implements the render package: confirm the exact
model-draw call and its vertex order before assuming triangle order out of
`buildRenderMesh` can be fed to a `glFrontFace(GL_CCW)` pipeline unchanged.

## Texture V orientation — VERIFIED-CODE (WP-14b), settled

**The claim from v1.0 (GUESS, from a single texture's visual content) is now
proven from two independent code paths that combine to a definite answer.**

1. **The TGA loader uploads rows in file order, unflipped.** The original
   TGA decoder (`0x4187e0`, called from `0x418cf0`/`0x4189d0` while resolving
   a model's/skin's texture path) reads pixel data straight from the file into
   the output buffer sequentially, for all three supported image types (1
   indexed, 2 truecolor, 3 greyscale); the only per-pixel work is a channel
   reorder (BGR→RGB) or a colour-map lookup. There is no row-reversal loop,
   and the image descriptor's origin bit is never read by this function at
   all. `RegisterTexture` (`0x418a20`) then passes that buffer straight to
   `glTexImage2D` with no further reordering. Since every shipped `.tga` has
   origin bit 5 = 0 (bottom-left; VERIFIED-DATA in `docs/spec/tga.md`), the
   file's first stored row — the visual **bottom** of the picture — ends up
   as row 0 of the uploaded texture, which by OpenGL's own convention is
   where texture coordinate `v = 0` samples.
2. **Model UV coordinates are submitted to OpenGL unmodified.** The
   shadow-silhouette rendering pass (`0x414a70`, which reads a model's own
   vertex/UV/face arrays at the same struct offsets as `R_LoadModel` —
   `model+0xa8` vertices, `model+0xb8` faces, confirming it operates on `.mdl`
   data) calls `glTexCoord2f(uv.u, uv.v)` with the stored UV pair passed
   through with no arithmetic on `v` (no `1 - v`, no scale) anywhere in the
   function.

Combining both: **`v = 0` in a shipped `.mdl` file samples the bottom of the
texture as conventionally displayed, and `v = 1` samples the top** — exactly
the "OpenGL default" convention, and exactly what the banner-flame heuristic
in v1.0 of this spec guessed from a single example.

**Conversion required for this engine.** `engine/include/as3d/image.h`
documents this engine's own fixed convention: `decodeTga` always produces a
top-down `Image` (row 0 = the top of the picture), and the render package
uploads/samples it "as is", so **`v = 0` is the TOP of the picture in this
engine** — the opposite sense from v1.70. To make a loaded model look the same
under this engine as it did in the original, `loadModel` must apply:

```
v' = 1.0f - v
```

to every UV read from the file, and it does, controlled by the named constant
`kFlipV` in `engine/src/formats/mdl.cpp` (currently `true`). `tools/ref/mdl.py`
does **not** apply this flip (it is a byte-exact reference reader for the
file format, not an engine-convention adapter); `ModelData::uvs` in the C++
loader, by contrast, holds the *flipped* (engine-convention) coordinates,
and `tools/ref/mdl.py`'s `obj` export leaves `v` unflipped too, matching the
file, so a from-`mdl.py`-exported `.obj` and a from-`ModelData` render will
show mirrored-in-V results unless the viewer/importer is told which
convention it's getting.

## Empty files

4 files are exactly zero bytes:

- `models/items/ammo/asec.mdl`
- `models/items/ammo/bonus.mdl`
- `models/jeeps/jeep_bug_cannon.mdl`
- `models/mapobjects/ruins/stone_comb1.mdl`

VERIFIED-DATA: none of the 863 objects parsed from `objects/*.obj` reference any
of these 4 paths in a `model` line. They appear to be dead asset stubs (perhaps
placeholders for cut content — "asec"/"bonus" ammo pickups and a cannon variant
of the buggy jeep that was never shipped), not files the game ever tries to load.

## Files that do not fit

2 non-empty files do not match this spec's byte-accounting formula:

| File | Header counts (v,uv,f,n,t) | Expected size | Actual size | Diff |
|---|---|---|---|---|
| `models/mapobjects/bridges/bridge1.mdl` | 38, 24, 38, 38, 0 | 1680 | 1656 | -24 |
| `models/mapobjects/bridges/japbridge.mdl` | 122, 124, 122, 122, 1 | 5560 | 5224 | -336 |

Both are genuinely corrupted source assets, not evidence of a different layout:
their vertex-array region contains long runs of `0xFD` and `0xDD` bytes — the
classic MSVC debug-heap "no-man's-land" and "freed memory" fill patterns — mixed
in with otherwise-plausible float data. `bridge1.mdl`'s own bounding box is
nonsense (`min.x ≈ -4.2e37`). Both are still referenced by objects
(`bridge1`/`bridge1_single`'s sibling `bridge1_single.mdl` parses fine, as does
`japbridge`'s sibling `japbridge_zabor.mdl`/`japbridge_wo_zabor.mdl` — only these
exact two files are affected); notably, `bridge1`'s object definition in
`objects/mapobjects.obj` has its `shadow SHADOW_PROJECTED` line commented out,
which is consistent with the original developers having noticed *something* was
wrong with this particular asset and worked around it rather than fixing the
file. `tools/ref/mdl.py` raises a clear `MdlError` for both (reported size vs.
expected, and exactly where the array data runs out); `tools/ref/test_mdl.py`
asserts this exact pair is the complete set of non-parsing files, so a
regression (a new broken file, or one of these getting fixed upstream) fails
loudly instead of being silently absorbed.

## Golden data

`testdata/golden/mdl_summary.json` has one entry per shipped `.mdl` file
(`status` is `"ok"`, `"empty"`, or `"broken"`). For `"ok"` entries it records the
5 counts, the bounding box, the tag names, and two hashes, with no vertex data
embedded in the JSON itself:

- `sha1_arrays`: SHA-1 over the raw bytes from the end of the header (offset
  0x78) to end of file, i.e. the vertex/uv/face/normal/tag arrays exactly as
  stored on disk (little-endian `f32`/`u16`, in array order) — before any
  engine-side conversion. This is what `tools/ref/mdl.py` parses.
- `sha1_engine`: SHA-1 over the same arrays with the one engine-side
  conversion that is exactly reproducible across independent implementations
  applied: `v' = 1 - v` on every UV (see "Texture V orientation"). Positions,
  face indices, normals and tags are included unchanged. The smooth-normal
  recompute is deliberately **not** folded into this hash — matching it
  bit-for-bit would require the C++ and Python implementations to agree on
  float32 rounding/operation order, which isn't guaranteed; `apps/tests/mdl_test.cpp`
  instead checks recomputed normals numerically (direction and, after this
  engine's normalization, unit length) rather than by hash.

`tools/ref/test_mdl.py` regenerates this file if missing, and otherwise diffs
today's parse against it field-by-field.

## Open questions

- **Object-space winding vs. `glFrontFace(GL_CW)`** (WP-14b): both facts are
  individually VERIFIED-CODE/VERIFIED-DATA, but the exact model-draw vertex
  submission order that reconciles them was not traced (see "Winding vs. the
  renderer's GL state" above). Needed before a render package can assume
  `buildRenderMesh`'s triangle order is directly usable with a given
  `glFrontFace` setting.
- Version `3` is accepted by the loader but never observed in shipped data.
  WP-14b did establish one concrete fact about it: its on-disk tag record
  would be the full 80-byte axis-matrix runtime shape, not the 56-byte
  position+direction shape version 2 uses (see "The trailing 12 bytes") — but
  nothing confirms the rest of its layout (vertex/uv/face/normal arrays could
  differ too).
- Whether a hypothetical version 3 (or some other unshipped tool) ever
  computed the axis matrix now hardcoded to identity *from* the on-disk
  direction vector is speculation with no code in this executable to check it
  against.

## Changelog

- 1.1 (WP-14b): resolved both v1.0 open questions from the executable
  (Ghidra export at `re/out/v170/`, cross-checked against raw `objdump`
  disassembly). Texture V orientation: **settled**, `v=0` is the bottom of the
  picture in v1.70 (traced through both the TGA loader's row order and the
  model-UV-to-`glTexCoord2f` submission), now VERIFIED-CODE; this engine's
  `loadModel` applies `v' = 1 - v` to match its own top-down image convention.
  Tag trailing vector: **settled**, it is authored but unused dead data in
  v1.70 — every tag's rotation axes are hardcoded to identity at load time,
  traced all the way through the attach position/orientation formula used by
  the entity system (`0x404850`, `0x4046a0`) and the meaning of the `abs`
  modifier (rigid position+orientation attachment vs. independent child
  orientation). Added the exact vertex order for the smooth-normal recompute
  formula and confirmed `GL_NORMALIZE`/`GL_RESCALE_NORMAL` are never enabled
  in v1.70 (recomputed normals reach the original's lighting unnormalized);
  this engine's loader normalizes them instead (documented, deliberate
  divergence). Added the `glFrontFace`/`glCullFace` state as a new, partially
  open question rather than resolving the WP-14 winding claim further.
- 1.0 (WP-14): initial version. Header, array order/layout, tag layout, bounding
  box, `smooth_normals` flag, winding, up axis, and the 6 anomalous files
  (4 empty, 2 corrupt) all VERIFIED-DATA and/or VERIFIED-CODE as marked above.
  V-texture orientation and tag-direction semantics remain GUESS.
