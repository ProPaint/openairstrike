# Static model (`.mdl`)

Spec version 1.0. Reference implementation: `tools/ref/mdl.py`, exercised by
`tools/ref/test_mdl.py` against all 446 shipped `models/**/*.mdl` files.

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

## Tags

`tag_count` entries starting right after the normal array, 56 bytes each:

| Offset | Size | Field |
|---|---|---|
| 0 | 32 | name, NUL-terminated ASCII |
| 32 | 12 | position: 3x f32 `(x, y, z)`, in model space |
| 44 | 12 | direction: 3x f32 `(x, y, z)` |

VERIFIED-DATA: all 733 tag names across the corpus are printable ASCII (checked:
100%).

### The trailing 12 bytes — VERIFIED-DATA on the shape, GUESS on the semantics

Confirmed **not** to be Euler angles: across all 733 tags in the corpus, the
magnitude of this field is either exactly zero (229 tags) or within [0.9, 1.1] of
1.0 (504 tags) — a clean bimodal split with **zero** values anywhere in between.
Euler angles (degrees or radians) would not produce this distribution. This is a
unit direction vector when present, and the zero vector when the attachment
point has no meaningful direction (e.g. simple flare/light points, which in the
apache/comanche/helic\* family consistently have `(0,0,0)` here, while
gun/rocket/cannon/kabina/headlight tags consistently carry a unit vector).

GUESS: semantically this looks like an aim/orientation axis for the attachment
(e.g. the direction a turret or headlight should face, or the local up/forward
axis needed to build a full rotation for the attached child), rather than a full
orientation (no second axis or handedness bit is stored, so only one axis of
rotation could be reconstructed from it alone; if a full basis is needed at
runtime it is presumably derived from this vector plus a convention like "world
up" or the model's own transform).

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

## Texture V orientation — GUESS, not confidently resolved

Evidence gathered: `models/banner/banner.mdl` uses `models/banner/banner2.tga`, a
256×128 flame texture that (as decoded by PIL, and matching its TGA header's
bottom-left origin flag) shows a solid bright flame base across the bottom of the
image and thinner, darker, tapering flame licks at the top — i.e. a
conventionally-oriented "rising flame" picture when viewed top-down like a normal
image. Correlating the banner model's own vertices against their UVs: low-Z
(bottom of the 3D model) vertices consistently have `v ≈ 0`, and high-Z (top)
vertices have `v ≈ 1`. For the flame to visually rise correctly on the model
(solid base at the model's bottom, taper at the model's top), `v = 0` must sample
the **bottom** of the image as normally displayed — i.e. V increases upward in
image space, matching the TGA's own native bottom-left-origin row order and the
OpenGL/id-tech texture convention (consistent with this being an OpenGL-era
engine given the `R_`-prefixed renderer function names).

This is a single corroborating example, not a corpus-wide statistical proof, so
it is marked **GUESS**: `v = 0` is the bottom of the texture as conventionally
displayed; `v = 1` is the top.

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
5 counts, the bounding box, the tag names, and `sha1_arrays` — a SHA-1 over the
raw bytes from the end of the header (offset 0x78) to end of file, i.e. over
the vertex/uv/face/normal/tag arrays exactly as stored, with no vertex data
embedded in the JSON itself. `tools/ref/test_mdl.py` regenerates this file if
missing, and otherwise diffs today's parse against it field-by-field.

## Open questions

- The exact semantics of a tag's trailing unit vector (single aim axis vs. one
  axis of a fuller basis reconstructed some other way at runtime) are not nailed
  down; nothing in the parts of `R_LoadModel` examined here consumes it, so the
  consumer must be elsewhere (attachment/animation code, out of scope for this
  package).
- The V-orientation conclusion rests on one texture; it would be worth
  corroborating with a second, independently-oriented texture (e.g. something
  with legible text) if one turns up during later work.
- Version `3` is accepted by the loader but never observed in shipped data;
  nothing here confirms whether it uses the same layout.

## Changelog

- 1.0 (WP-14): initial version. Header, array order/layout, tag layout, bounding
  box, `smooth_normals` flag, winding, up axis, and the 6 anomalous files
  (4 empty, 2 corrupt) all VERIFIED-DATA and/or VERIFIED-CODE as marked above.
  V-texture orientation and tag-direction semantics remain GUESS.
