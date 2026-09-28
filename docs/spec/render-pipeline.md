# Render pipeline (everything except terrain)

Spec version 1.0. Work package WP-25. No reference parser: this document describes run-time
behaviour of the v1.70 renderer, not a file format. The data statistics quoted as
VERIFIED-DATA were produced with throw-away scripts built on `tools/ref/defs.py`, `tga.py` and
`mdl.py` over all shipped files in `assets_extracted/`.

Scope. How the original draws models, sprites, ground marks, shadows, particles, native effects,
the brightness overlay, and the 2D layer (HUD, menus, font, cursor), plus the global state they
share (frame order, projection, fog, lighting, textures). Out of scope and owned by other
specifications: terrain, terrain detail layers and water geometry (`hmap.md`), camera placement
and game-rule timing (`engine-behaviour.md`), model file layout, tag directions and UV
orientation (`mdl.md` 1.1). Where this document touches them it says so and stops.

Addresses are v1.70 virtual addresses of functions (see `re/symbols_v170_render.csv` for the
proposed names). Confidence tags follow `docs/spec/README.md`.

## 0. Conventions used in this document

| Item | Meaning | Tag |
|---|---|---|
| World axes | Z is up. Terrain height is a function of (x, y); terrain cells are 40 world units square. Planar decals are built in the XY plane. | VERIFIED-CODE 0x40f070, 0x40d920 |
| Angles | Degrees everywhere in entity data; converted with pi/180. | VERIFIED-CODE 0x41e390 |
| Original texture coordinate `t` | The original uploads TGA rows in file order with no flip (0x4187e0, 0x418a20). Every shipped TGA stores its bottom row first (`tga.md`), so in the original **t = 0 is the bottom row of the picture and t = 1 its top row**. All `t` values quoted below are in this original convention. | VERIFIED-CODE + VERIFIED-DATA |
| Our engine | `docs/graphics.md`: images are uploaded top row first and v = 0 is the top. Convert every original coordinate with **v = 1 − t**; s is unchanged. For model UVs `mdl.md` 1.1 is authoritative. | VERIFIED-CODE (derivation) |
| rand01 | `rand() / 32767`, uniform in [0, 1] inclusive (MSVC `rand`, 15 bits). | VERIFIED-CODE 0x414290 |
| srand11 | `2 × (rand01 − 0.5)`, uniform in [−1, 1]. | VERIFIED-CODE 0x414290 |
| frametime | Seconds, clamped to 0.1 (see `rcsl-vm.md`). Written `dt` below. | VERIFIED-CODE (rcsl-vm.md) |
| time | Accumulated game time in seconds (global at 0x1fb4bd0), used by animated texture effects. | VERIFIED-CODE |

## 1. Frame structure

### 1.1 Order of a game frame

The game frame function (0x408df0) runs, in order: begin frame (0x40e690), camera update
(0x40c260), map spawner, entity pass, particle update for every live particle system instance
(0x414600, skipped while paused), two bookkeeping passes, the screen fade overlay submission
(0x404370), the scene render (0x40e0d0), the UI frame (0x4299f0, which ends by drawing the 2D
list, section 8), and end frame (0x40e6f0). VERIFIED-CODE 0x408df0.

The scene render (0x40e0d0) executes these passes. "World pass" means the pass is skipped when the
view's `noworld` flag is set (menu model previews, 0x401b30). VERIFIED-CODE 0x40e0d0 unless noted.

| # | Pass | Function | Depth test | Depth write | Blend | Notes |
|---|---|---|---|---|---|---|
| 0 | Clear | 0x40e690 (called before the game update) | – | – | – | `glClear(DEPTH)` then `glClear(COLOR)`. Stencil never cleared per frame. Clear colour = level fog colour (black before a level loads). No sky is drawn: the background is the clear colour. |
| 1 | Viewport, projection, view | 0x40e0d0 | enabled here | – | – | Section 2.1. |
| 2 | Terrain | 0x416020 → 0x415730 | on | on | – | World pass. Owned by `hmap.md`. Includes the terrain's own alpha-blended tile decals. |
| 3 | Frustum extraction | 0x419040 | – | – | – | World pass. Six planes from projection × modelview; used by the next frame's entity visibility test. |
| 4 | Ground marks (TYPE_MARK) | 0x40d860 | on | **off** | per mark | World pass. Polygon offset fill enabled, factor −1, units −1. Section 3.4. |
| 5 | Shadows | 0x40db20 | on | **off** | alpha | World pass. Polygon offset −1/−1. Walks the opaque list then the transparent list and draws the shadow of every record that has one. Section 5. |
| 6 | Opaque list (sort 0 and 1) | 0x410ac0 per record | per record | per record | per record | Perspective hint FASTEST. |
| 7 | Water | 0x4160d0 | on | on | alpha | World pass. Owned by `hmap.md`. |
| 8 | Transparent list (SORT_TRANS = 2) | 0x410ac0 | per record | per record | per record | |
| 9 | Effect list (SORT_EFFECT = 3) | 0x410ac0 | per record | per record | per record | Perspective hint NICEST from here on. Lightning bolts are records in this list (section 7.1). |
| 10 | Particles | 0x4149c0 → 0x413a50 | on unless the system has RF_NODEPTHTEST | **off for every particle** | per system | World pass. Section 6. |
| 11 | Debug counters text | 0x40e0d0 | – | – | – | World pass, only with `ShowFPS`, `ShowTris`, `ShowTexBinds`, `ShowCounters` in config.ini. |
| 12 | Sprites (TYPE_SPRITE, TYPE_HSPRITE, TYPE_VSPRITE) | 0x40d7a0 → 0x40d370 | on unless RF_NODEPTHTEST | on unless RF_NODEPTHWRITE | per sprite | Drawn after particles. Section 3.3. |
| 13 | 2D list | 0x40df90 + 0x40cee0 | **off** | – | per quad | Fog disabled during the pass and re-enabled afterwards if the level has fog. Section 8. The UI frame calls the scene render a second time with no view, which only flushes the 2D list (0x40e070). |
| 14 | Brightness overlay and swap | 0x40e6f0 | as left (off) | – | DST_COLOR, SRC_COLOR | Section 1.5. |

No GL lighting, stencil, alpha test, `GL_NORMALIZE` or colour material is ever enabled: the
only `glEnable` calls for `GL_LIGHTING`, `GL_ALPHA_TEST` and `GL_NORMALIZE` are the disables in
`GL_Init`, and no function references `GL_STENCIL_TEST`. VERIFIED-CODE (all `PUSH` sites of
these enums).

### 1.2 Draw lists and sorting

Entities are not drawn directly. Each visible entity copies its 0xA8-byte render record into one
of five per-frame arrays (0x40de50). The record is embedded in the entity at +0x113 and refreshed
from the entity fields every update (0x4057c0). VERIFIED-CODE.

| Record `type` (object `type`) | Record `sort` | List | Capacity | Drawn in pass |
|---|---|---|---|---|
| 0 TYPE_MODEL | 2 SORT_TRANS | transparent | 128 | 8 |
| 0 TYPE_MODEL | 3 SORT_EFFECT | effect | 256 | 9 |
| 0 TYPE_MODEL | anything else (0, 1) | opaque | 512 | 6 |
| 1 SPRITE, 3 HSPRITE, 4 VSPRITE | ignored | sprite | 512 | 12 |
| 2 TYPE_MARK | ignored | mark | 128 | 4 |

VERIFIED-CODE 0x40de50. A record submitted to a full list is silently dropped.

**No list is sorted.** Records are drawn in submission order. Submission happens during the
entity pass: an entity submits its own record before recursing into its attachments, so parents
draw before children; the order between top-level entities is the entity-pass order (newest
first, see `rcsl-vm.md`). VERIFIED-CODE 0x405a60, 0x40e0d0 (no comparison or sort call in any
list walk). Entities with `FL_NODRAW` (flags bit 0x10, script-writable) are not submitted and add
no light. VERIFIED-CODE 0x405a60.

The sort statistics of the data: SORT_TRANS is used only with BLEND_ALPHA (75 objects);
SORT_EFFECT with BLEND_ADD (84), BLEND_ALPHA (1), no blend (2); 112 BLEND_ADD objects and 10
BLEND_ALPHA objects are left in the opaque list. VERIFIED-DATA.

### 1.3 Viewport, video modes and aspect ratio

`VideoMode` (config.ini, default 1) selects the window size: 0 = 640×480, 1 = 800×600,
2 = 1024×768, 3 = 1152×864, 4 = 1280×960, 5 = 1280×1024, 6 = 1600×1200, 7 = 1920×1440,
8 = 2048×1536; any other value falls back to 800×600 with a log line. VERIFIED-CODE 0x41cdc0.

The 3D viewport is the whole window (the camera sets x = 0, y = 0, width, height;
0x40c260), and the perspective aspect is `width / height` (integer operands converted to double),
so 5:4 modes see a narrower field horizontally; there is no letterboxing. VERIFIED-CODE 0x40c260,
0x40e0d0. The 2D layer always uses a virtual 800×600 space stretched to the full window
(section 8). VERIFIED-CODE 0x40df90.

The requested pixel format is double-buffered RGBA with 32 depth bits and no stencil; the shipped
log shows the driver returned 24-bit depth and 8-bit stencil. VERIFIED-CODE 0x4218b0 (PFD at
0x457d60), VERIFIED-DATA game.log.

### 1.4 Vertical sync

At the start of each frame, if `WGL_EXT_swap_control` exists, the swap interval is set to 1 when
`WaitVSync` ≠ 0 and to 0 otherwise. VERIFIED-CODE 0x40e690, 0x412050.

### 1.5 Brightness

There is no gamma ramp (no `SetDeviceGammaRamp` import). At the very end of the frame, after all
3D and 2D drawing, the renderer disables texturing, sets blend `(GL_DST_COLOR, GL_SRC_COLOR)`,
and draws one quad over the virtual 800×600 area with colour `(b, b, b)` where b =
`Brightness`. The result is `dst × b + b × dst = 2·b·dst`, clamped by the framebuffer:
`Brightness` 0.5 is neutral, the default 0.6 brightens by 1.2×. Then blending is disabled,
`glFlush`, `SwapBuffers`. VERIFIED-CODE 0x40e6f0, 0x41cef0 (default "0.6"). While the intro logo
pages run, the brightness is temporarily forced to 0.5 and restored afterwards. VERIFIED-CODE
0x408070.

Fog is still enabled when this quad is drawn, but at eye depth 0 it is below the fog start, so it
has no effect. VERIFIED-CODE (state sequence) — the conclusion is arithmetic.

## 2. Global state

### 2.1 Baseline state, projection and view

`GL_Init` (0x412220) sets once, and every later pass restores what it changes:

| State | Value | Tag |
|---|---|---|
| `GL_LIGHTING`, `GL_NORMALIZE`, `GL_ALPHA_TEST` | disabled (never enabled later) | VERIFIED-CODE |
| `GL_TEXTURE_2D`, `GL_DEPTH_TEST`, `GL_CULL_FACE` | enabled | VERIFIED-CODE |
| Depth function | `GL_LEQUAL` | VERIFIED-CODE |
| Alpha function | `GL_GEQUAL 0.7` (inert: alpha test is never enabled) | VERIFIED-CODE |
| Front face / cull face | `GL_CCW` / `GL_BACK` | VERIFIED-CODE |
| Polygon mode | fill, both faces | VERIFIED-CODE |
| Shade model | `GL_FLAT`; models with smooth normals and the terrain switch to `GL_SMOOTH` for their draw and back | VERIFIED-CODE |
| Texture wrap S and T | `GL_REPEAT` (set on texture object 0 at init; no other wrap call exists, so every texture uses the GL default `GL_REPEAT`) | VERIFIED-CODE |
| Texture environment | `GL_MODULATE` | VERIFIED-CODE |
| Clear colour / clear stencil | (0, 0, 0, 1) / 0 | VERIFIED-CODE |

Projection (0x40e0d0): `gluPerspective(fovy, width/height, near = 4.0, far)` with
`far = 2000` when the level has no fog, else `far = max(fogEnd, 1000)`. VERIFIED-CODE. The
fields of the view structure used here: viewport (x, y, w, h), `fovy` in degrees, origin, three
angles, `noworld` byte. VERIFIED-CODE.

View matrix: `Rx(angle0) · Ry(angle1) · Rz(angle2) · T(−origin)` built with `glRotatef` in
that order (standard OpenGL rotation convention, right-handed, degrees). VERIFIED-CODE 0x40e0d0.
Camera values seen in passing (owned by `engine-behaviour.md`): camera mode table at 0x457398,
four modes of (fovy, angle0, height, y offset) = (60, −35, 270, 0), (60, −45, 270, 0),
(60, −50, 270, 0), (70, −15, 370, 100); `Camera` in config.ini selects the mode (default 1);
intermissions use fovy 60 and `angle2 = level value + 1.4·sin(...)`; `CameraQuake` perturbs
angle1, angle2 and fovy for 3 seconds (0x40c150, 0x41bb90). VERIFIED-CODE for the table and the
fact these fields are written; their gameplay meaning is out of scope.

Billboard axes. Sprites and particles do not read the view matrix: they use rows 0 and 1 of
`AnglesToAxis(angles)` (0x41e390). That axis set equals the rows of `Rx(a0)·Ry(a1)·Rz(−a2)`,
so it matches the true camera right/up vectors only when angle2 = 0 (normal play) and is
mirrored in roll otherwise (intermissions, camera quake). VERIFIED-CODE (formula at 0x41e390
checked numerically against the rotation products). Row 0 is the camera's right vector R and row
1 its up vector U when angle2 = 0.

### 2.2 Fog

Set when a level starts (0x407080 → 0x40d2a0) from the level's `fog r g b near far`:

| State | Value | Tag |
|---|---|---|
| Mode | `GL_LINEAR` | VERIFIED-CODE |
| Colour | (r, g, b) from levels.txt; also becomes the clear colour | VERIFIED-CODE |
| Start / end | `near` / `far` | VERIFIED-CODE |
| Enabled | when the level has `fog` (all 24 do), unless `DisableDistanceFog` ≠ 0 or the software renderer was detected | VERIFIED-CODE 0x40d2a0, 0x41cef0 |
| Hint | none (implementation default; drivers use eye-space depth or distance) | VERIFIED-CODE (no `GL_FOG_HINT`) |

Fog factor: `f = clamp((far − d) / (far − near), 0, 1)`, colour = `f·c + (1 − f)·fogColour`,
with `d` the eye-space distance (GL 1.x implementations use |z_eye|). VERIFIED-CODE for the
mode; the formula is the GL definition.

The fog colour is swapped per blend mode by the state cache (0x412be0 and every inline copy of
it): while BLEND_ADD is active the fog colour is (0, 0, 0, 0) (so additive things fade to
nothing, not to grey), while BLEND_FILTER is active it is (1, 1, 1, 1) (multiplicative things fade
to "no change"), and it is restored to the level colour when switching to any other mode.
VERIFIED-CODE 0x412be0 (0x1fdb318 is never written, so it is zero; 0x457368 holds 1.0 × 3).

Passes with fog disabled: the 2D layer only. All 3D passes, including particles and sprites, are
fogged. VERIFIED-CODE 0x40e0d0.

Level values: fog near 350–650, far 800–1000 (VERIFIED-DATA, `levels-txt.md` table), so the far
clip is always 1000.

### 2.3 Lighting: the sun

There are no GL lights. Lighting is computed on the CPU per model per frame and sent as vertex
colours. VERIFIED-CODE (no `glLight*`/`glMaterial*` imports; 0x410530).

The nine `sun` numbers of levels.txt (`LevelDef::sun`) are, in order (VERIFIED-CODE, level
parser 0x406340 stores them at level +0x11C..+0x13C; 0x416fd0 copies them at level load):

| Index | Meaning | Global | Tag |
|---|---|---|---|
| 0–2 | sun colour S (diffuse) | 0x1f5b988 | VERIFIED-CODE |
| 3–5 | direction **towards** the sun L, normalised at load | 0x1f5b97c | VERIFIED-CODE (normalised with 0x41e220); VERIFIED-DATA every level has L.z > 0 |
| 6–8 | ambient colour A | 0x1f5b994 | VERIFIED-CODE |

Model lighting uses an "ambient cube" of six colours, one per signed local axis of the model
(0x410530). With `a0, a1, a2` the world-space directions of the model's local X, Y, Z axes (the
rows of the record axis matrix):

```
for i in 0..2:
    d = dot(a_i, L)
    C[+i] = A + max(d, 0) * S        # slot i
    C[-i] = A + max(-d, 0) * S       # slot i+3
```

Dynamic lights are then added to the six slots (section 2.4). Each vertex (smooth-normal model)
or each face (flat-normal model) has three precomputed slot indices and weights from its
**model-space** normal n (0x411520, run once at model load):

```
idx_x = (n.x >= 0) ? 0 : 3 ;  idx_y = (n.y >= 0) ? 1 : 4 ;  idx_z = (n.z >= 0) ? 2 : 5
a = acos(|n.x|) / (pi/2) ;  b = acos(|n.z|) / (pi/2)
w_x = b - a*b ;  w_y = a*b ;  w_z = 1 - b            # w_x + w_y + w_z = 1
colour = w_x*C[idx_x] + w_y*C[idx_y] + w_z*C[idx_z]  # alpha 1.0 (glColor3fv)
```

VERIFIED-CODE 0x411520 (acos is `__CIacos` at 0x440d30; the divisor is the double π/2 at
0x44b7d8), 0x410530. The colour is clamped to [0, 1] by GL. Normals are unit length at this point:
smooth normals are recomputed and renormalised at load, flat normals are stored normalised
(`mdl.md`, with the correction in section 11.3).

Side effect to reproduce: 0x411520 **overwrites the model's normal array with the absolute values
of the components**. Environment mapping later sends these folded normals to GL (section 4.3).
VERIFIED-CODE.

Smooth-normal models are drawn with `GL_SMOOTH` and per-vertex colours; flat-normal models with
`GL_FLAT` and one colour per face. VERIFIED-CODE 0x410ac0. Data: 596 smooth + 15 flat lit models,
84 smooth + 5 flat unlit. VERIFIED-DATA.

The entity colour (fields 28–31) is **not** applied to lit models: the lit path emits only the
computed colour with alpha 1. It is applied only with RF_NOLIGHTING (section 4.2).
VERIFIED-CODE 0x410ac0.

### 2.4 Dynamic lights

A per-frame array of at most 32 lights (0x1f286a8, 0x34 bytes each), emptied at the end of every
scene render. VERIFIED-CODE 0x40dd00, 0x40dd80, 0x40e0d0. Sources:

| Source | Position | Colour | Radius | Cone | Tag |
|---|---|---|---|---|---|
| Object `light radius r g b` (entity light, submitted with the render record every update) | entity origin | (r, g, b) | radius (integer) | none | VERIFIED-CODE 0x405a60 |
| Object `light_dir radius r g b dx dy dz angle` | entity origin | (r, g, b) | radius | direction = local (dx, dy, dz) rotated by the entity axis (`d.x·a0 + d.y·a1 + d.z·a2`); outer cosine `cos(0.5·angle)`, inner cosine `cos(0.4·angle)` (angle in degrees = full cone) | VERIFIED-CODE 0x405a60, 0x41e600, 0x40dd80 |
| Script `PlaceLight(position, colour, radius)` | argument 0 | argument 1 | argument 2 | none | VERIFIED-CODE 0x41bb20 |

Lights submitted after the 32nd in a frame are dropped. Light colours in the data go up to 2.5
(VERIFIED-DATA: e.g. `svet_big` 2.5, 2.5, 0.6).

**On models** (0x410530, skipped when the record has RF_NOLIGHTING or RF_NODLIGHT): each light is
evaluated once at the entity origin O:

```
V = P_light - O ; dist = |V| ; V = V / dist              (if dist != 0)
if dist > radius: skip
spot = 1
if spot light:
    c = dot(-V, D)
    if c < cosOuter: skip
    if c < cosInner: spot = (c - cosOuter) / (cosInner - cosOuter)
att = (radius - dist) / radius
for i in 0..2:
    d = dot(a_i, V)
    k = (0.7*|d| + 0.3) * att * spot
    if d >= 0: C[+i] += k * colour   else: C[-i] += k * colour
```

VERIFIED-CODE 0x410530. Note the constant 0.3 "wrap" term on the lit side only.

**On terrain** (0x415730, for the record): each terrain vertex with normal N and position P gets,
per light, `V`, `dist`, `spot` as above, and if `dist <= radius` and `dot(N, V) > 0`:
`k = (0.7·dot(N,V) + 0.3) · (radius − dist)/radius · spot`, added to the vertex's baked colour as
`colour·k·255`, clamped to 255 per channel. VERIFIED-CODE 0x415730 (the division is
`(radius − dist)/radius`; the decompiler shows it inverted). The terrain colour pipeline itself
belongs to `hmap.md`.

Sprites, marks, particles, lightning and the 2D layer are never lit. VERIFIED-CODE (none of
their draw functions reads the sun or light globals).

Lights are not GL lights, not additive quads and not light maps; the only "light pools" on the
ground are separate additive TYPE_MARK objects (section 7.4). VERIFIED-CODE.

### 2.5 Night levels

`night` changes nothing in the renderer itself: the only readers are the attachment spawner (an
`attach night ...` is created only on night levels, `obj.md`) and the script global `l_night`.
VERIFIED-CODE (0x458eac is written in 0x407080 and read by no render function; level +0x188 read
by 0x409ba0). Everything night-specific comes from data: darker `sun` and `fog` values, and the
night-only attachments, which are lights and flare sprites. VERIFIED-DATA: the night attachments
are `svet_far` (76 uses), `svet_far_wol` (61), `svet_far_helic` (36, a spot light
`light_dir 400 1.1 1.1 0.8  0 1 −0.5  60`), `svet_fonar*`, `svet_big`, `boss3_fonar_light*`.

## 3. Geometry of each object type

### 3.1 Render record

| Offset | Content | Source (entity field or object key) | Tag |
|---|---|---|---|
| +0x00 | type 0..4 | object `type` | VERIFIED-CODE |
| +0x04 | render flags | object `rflag`, plus internal bits 0x1000 (lightning), 0x4000 (line) | VERIFIED-CODE |
| +0x08 | sort | object `sort` | VERIFIED-CODE |
| +0x0C | origin | entity origin | VERIFIED-CODE 0x4057c0 |
| +0x18 | 3×3 axis, rows = local X, Y, Z in world | `AnglesToAxis(angles)` or terrain-aligned | VERIFIED-CODE 0x4057c0 |
| +0x3C | scale | entity field 32 (starts at 0; the object `scale` key is overwritten every update and never reaches the renderer) | VERIFIED-CODE 0x409ba0, 0x4057c0 |
| +0x40 | model index (marks: display list) | object `model` | VERIFIED-CODE |
| +0x44 | skin texture | object `skin`, else the model's own texture | VERIFIED-CODE 0x410ac0 |
| +0x48 | blend mode | object `blend` | VERIFIED-CODE |
| +0x4C / +0x50 | env map texture / env mode | object `envmap` / `envmode` | VERIFIED-CODE |
| +0x54 | colour RGBA | entity fields 28–31, initial (1, 1, 1, 1) | VERIFIED-CODE 0x409ba0, 0x4057c0 |
| +0x64..+0x88 | sprite quad: x0, y0, –, s0, t0, x1, y1, –, s1, t1 | object `min x y s t`, `max x y s t` | VERIFIED-CODE 0x40a160 |
| +0x8C / +0x90 | frame grid columns / rows | object `frames cols rows` | VERIFIED-CODE 0x40a160, 0x40d370 |
| +0x94 | frame index | `int(field 33)` | VERIFIED-CODE 0x4057c0 |
| +0x98 | sprite rotation (degrees) | yaw (angles[2]), sprites only | VERIFIED-CODE 0x4057c0 |
| +0x9C / +0xA0 / +0xA4 | shadow record / static shadow display list / yaw for planar shadows | section 5 | VERIFIED-CODE |

### 3.2 TYPE_MODEL (0x410ac0)

1. A record with model index 0 draws nothing (marker and light-only objects).
2. Modelview = view · M where M has columns (a0, a1, a2, origin): a vertex v is placed at
   `origin + v.x·a0 + v.y·a1 + v.z·a2`. If `scale > 0.001`, a uniform `glScalef(scale)` follows.
3. Per-record state from `rflag` (section 4.2) and blend mode (section 4.1).
4. Texture: the record skin, else the model's default texture, else **no bind at all** (the
   previously bound texture stays; quirk).
5. Triangles are sent as triples (vertex, UV) in file order with GL_CCW front faces (`mdl.md`).
6. Colour: lit (section 2.3) or the entity RGBA (RF_NOLIGHTING). Environment mapping only on
   smooth-normal models with an `envmap` (section 4.3).

VERIFIED-CODE 0x410ac0.

### 3.3 Sprites (0x40d370)

All three sprite types draw one quad whose corners come from the object's `min`/`max`:
positions (x0, y0) and (x1, y1), texture coordinates (s0, t0) and (s1, t1). They are unlit, use
the entity RGBA as vertex colour, the record blend mode, depth test off with RF_NODEPTHTEST,
depth write off with RF_NODEPTHWRITE, and **face culling stays enabled** (RF_NOCULLING is not read).
VERIFIED-CODE.

Frame grid. When `frames cols rows` is present (cols ≠ 0), the texture coordinates of `min`/`max`
are replaced by cell `f = int(field 33)`:

```
du = 1/cols ; dv = 1/rows
s0 = (f mod cols) * du          ; s1 = s0 + du
t0 = 1 - dv - (f div cols) * dv ; t1 = t0 + dv        (original t; frame 0 = top-left cell)
```

VERIFIED-CODE 0x40d370. There is no frame-rate key: the frame index is whatever the script writes
into field 33 (`rcsl-vm.md`). VERIFIED-DATA: grids used are 2×1, 10×1 (`score_num`, the ten
digits of `gfx\scores.tga` 256×32), 3×4 (`expl1_hit`, whose texture is missing), 2×4; all divide
their texture sizes evenly.

Corner order is always (x0, y0, s0, t0), (x1, y0, s1, t0), (x1, y1, s1, t1), (x0, y1, s0, t1).

| Type | Placement | Scale | Tag |
|---|---|---|---|
| 1 TYPE_SPRITE | Billboard: local x along R, local y along U (section 2.1), centred on the origin, rotated in the view plane by the record rotation (yaw, degrees, positive = counter-clockwise about the view axis), then scaled by (scale, scale) if scale > 0.001 | yes | VERIFIED-CODE 0x40d7a0, 0x40d370 |
| 3 TYPE_HSPRITE | Horizontal: translate to origin, rotate by yaw about world Z, quad in the XY plane at z = 0 (lies flat, front face up) | no | VERIFIED-CODE |
| 4 TYPE_VSPRITE | Vertical: translate, rotate by yaw about world Z, quad in the XZ plane: vertex (x, 0, y) — local y becomes height; front face towards −Y | no | VERIFIED-CODE |

Data: 101 SPRITE (99 BLEND_ADD, 2 hbar health bars unblended), 1 HSPRITE (`big_clouds`,
BLEND_ALPHA, `gfx\clouds\clouds1.tga` 256×256 32-bit, 256×256 units), 0 VSPRITE. `min`/`max`
occur on exactly the 108 sprite and mark objects. VERIFIED-DATA.

### 3.4 TYPE_MARK (ground decals, 0x40d860)

When a mark entity is spawned (0x40a080), the renderer builds a GL display list once: every
terrain triangle of the 40-unit cells overlapping the world-axis-aligned rectangle
`origin.xy + [min.xy, max.xy]` is clipped against that rectangle (0x40e840, Sutherland–Hodgman on
the four edges), keeping the terrain heights, with texture coordinates
`s = (x − Xmin)/(Xmax − Xmin)`, `t = (y − Ymin)/(Ymax − Ymin)` (the object's `min`/`max` s, t
values are ignored for marks). The decal therefore follows the terrain but not the entity: it is
fixed where it spawned and never rotates. VERIFIED-CODE 0x40a080, 0x40f070, 0x40e840.

Draw state per mark: depth test on, depth write off, polygon offset fill (−1, −1), the record blend
mode, the skin texture, colour = entity RGB with alpha 1. VERIFIED-CODE 0x40d860.

Data: `mark` (BLEND_FILTER, `gfx\mark.tga`, ±50 units, the scorch mark) and five explosion
light pools `expl*_light` (BLEND_ADD, `gfx\effects\expl1light.tga`, ±60 to ±200 units).
VERIFIED-DATA.

### 3.5 Use of the entity render fields

| Field | TYPE_MODEL | Sprites | Marks | Tag |
|---|---|---|---|---|
| 28–31 colour | RF_NOLIGHTING only (RGBA); RF_BANNER overrides RGB | RGBA | RGB (alpha forced 1) | VERIFIED-CODE |
| 32 scale | uniform scale when > 0.001 | SPRITE only, XY | no | VERIFIED-CODE |
| 33 frame | no (models have one frame) | cell index when `frames` set | no | VERIFIED-CODE |
| angles[2] yaw | via the axis | quad rotation | no | VERIFIED-CODE |

## 4. Materials

### 4.1 Blend modes

The blend state cache (0x412be0, current mode at 0x1f3ca50) implements:

| Mode | `glBlendFunc` | Blending | Fog colour while active | Depth write | Alpha test | Tag |
|---|---|---|---|---|---|---|
| 0 none | – | disabled | level colour | as record | never | VERIFIED-CODE |
| 1 BLEND_ALPHA | SRC_ALPHA, ONE_MINUS_SRC_ALPHA | on | level colour | **on** unless RF_NODEPTHWRITE | never | VERIFIED-CODE |
| 2 BLEND_ADD | ONE, ONE | on | (0, 0, 0) | **on** unless RF_NODEPTHWRITE | never | VERIFIED-CODE |
| 3 BLEND_FILTER | DST_COLOR, ZERO | on | (1, 1, 1) | **on** unless RF_NODEPTHWRITE | never | VERIFIED-CODE |
| 4 (internal) | DST_COLOR, SRC_COLOR | on | level colour | – | never | VERIFIED-CODE (brightness quad only) |

Because alpha test is never enabled, fully transparent texels of BLEND_ALPHA models still write
depth unless RF_NODEPTHWRITE is set. VERIFIED-CODE.

Alpha source. Lit models send vertex alpha 1, so their transparency comes from the texture alpha
alone; unlit models, sprites and particles multiply texture alpha by the colour alpha.
VERIFIED-CODE. Textures without an alpha channel (24-bit and 8-bit paletted) have alpha 1.

VERIFIED-DATA (objects with a skin): BLEND_ALPHA is used with 65 32-bit skins and 3 24-bit skins
(`boss3_fonar1`, `boss3_fonar2`, `fonar_stadion`, which therefore blend as opaque); BLEND_ADD with
128 24-bit, 56 paletted, 1 32-bit, and 5 missing skins; BLEND_FILTER with 1 paletted skin
(`mark`); no blend with 407 24-bit, 52 paletted, 13 32-bit. No paletted skin is used with
BLEND_ALPHA. 83 lit models use BLEND_ALPHA (texture alpha only). Particles: BLEND_ADD 64
(32 paletted, 9 24-bit, 23 32-bit), BLEND_FILTER 12 (all paletted), BLEND_ALPHA 4 (2 32-bit,
2 paletted: `PS_JEEP_SMOKE` and `PS_SPICE_STORM2`, whose alpha therefore comes from the fade
only).

### 4.2 Render flags

| Flag | Models | Sprites | Particles | Tag |
|---|---|---|---|---|
| RF_NOLIGHTING 0x1 | no sun, no dynamic lights; vertex colour = entity RGBA | n/a (always unlit) | parsed, unused | VERIFIED-CODE 0x410ac0 |
| RF_NOCULLING 0x2 | `glDisable(GL_CULL_FACE)` for the model | not read (culling stays on) | parsed, unused | VERIFIED-CODE |
| RF_NODEPTHTEST 0x4 | depth test off for the model | depth test off | depth test off for the whole system | VERIFIED-CODE |
| RF_NODEPTHWRITE 0x8 | `glDepthMask(0)` for the model | depth write off | no effect (particles never write depth) | VERIFIED-CODE |
| 0x20 (set by `envmap`) | enables the environment path on smooth models | – | – | VERIFIED-CODE 0x410ac0 |
| RF_NODLIGHT 0x200 | sun only, no dynamic lights | – | – | VERIFIED-CODE 0x410ac0 |
| RF_BANNER 0x10000 | texture matrix: translate s by `time × 0.1`; colour RGB = `0.75 + 0.25·sin(1.5·time)` (alpha kept); the texture matrix is reset after the draw | – | – | VERIFIED-CODE 0x410ac0 (constants 1.5 at 0x44bbc8, 0.1 inline) |
| 0x1000 (internal) | the record is a lightning bolt (section 7.1) | | | VERIFIED-CODE |
| 0x4000 (internal) | additive white `GL_LINES` segment between two points, no depth test, no texture; no code sets this bit in render flags (only the unrelated entity flag field), so it looks like a debug leftover | | | VERIFIED-CODE 0x410270; setter not found |

RF_BANNER's colour only shows through RF_NOLIGHTING; the single RF_BANNER object (`banner`) also
has RF_NOLIGHTING, RF_NOCULLING, RF_NODEPTHTEST and BLEND_ADD. VERIFIED-DATA.

### 4.3 Environment mapping

Active when the model has smooth normals, the record has bit 0x20 (an `envmap`), and
`GL_ARB_multitexture` is available and not disabled by `DisableMultitexture`; otherwise the model
draws plainly. The GL normal sent with each vertex is the folded normal |n| of section 2.3, in
model space; GL transforms it by the inverse transpose of the modelview (no renormalisation, so a
scaled model gets normals of length 1/scale). Texture coordinates come from
`GL_SPHERE_MAP` generation. VERIFIED-CODE 0x4127e0, 0x410ac0.

Sphere map (GL 1.x definition): with `u` the unit vector from the eye to the vertex and `n'` the
eye-space normal, `r = u − 2·n'·dot(n', u)`, `m = 2·sqrt(r.x² + r.y² + (r.z + 1)²)`,
`s = r.x/m + 0.5`, `t = r.y/m + 0.5`.

| Mode | Unit 0 | Unit 1 | Result | Tag |
|---|---|---|---|---|
| 1 ENV_GLITTER | skin, model UV, `MODULATE` with vertex colour | env map, sphere-map s/t, `COMBINE` with `COMBINE_RGB = ADD` (arg0 = texture, arg1 = previous; alpha modulate) | `rgb = skin·colour + env ; a = skin.a·colour.a·env.a` | VERIFIED-CODE |
| 2 ENV_CHROME | env map, sphere-map s/t, `MODULATE` with vertex colour | skin, model UV (sent with `glMultiTexCoord2fARB(GL_TEXTURE1)`), `COMBINE`, `COMBINE_RGB = INTERPOLATE`, source2 = texture, operand2 = SRC_ALPHA | `rgb = skin.rgb·skin.a + env·colour·(1 − skin.a)` (the skin is **not** lit) | VERIFIED-CODE; unused by data |
| 3 ENV_QUAD | env map only, sphere-map s/t through a texture matrix rotating by `time × 30` degrees about the texture origin (0, 0), `MODULATE` | off | `env(rotated s,t) · colour`; the skin is not drawn | VERIFIED-CODE |
| 0 with `envmap` | – | – | plain skin | VERIFIED-CODE |

After the draw, units 1 and 0 have texgen disabled and identity texture matrices, unit 1 is
disabled. VERIFIED-CODE 0x412b70.

VERIFIED-DATA: 69 objects have `envmap`, all on smooth-normal models. 63 use ENV_GLITTER
(`gfx\envmap1..3.tga`, `gfx\chrome2.tga`, `gfx\effects\moln.tga`), 6 use ENV_QUAD (player shields
`p_*shield`, `p_*moln` lightning shells with `gfx\effects\shield.tga` / `moln.tga`, all
RF_NOLIGHTING + BLEND_ADD over a `gfx\black.tga` skin, and `boss1_sphere`), none uses ENV_CHROME.

### 4.4 Culling, filtering, wrap, alpha, colour key

* Culling: back faces culled with CCW front faces everywhere except models with RF_NOCULLING, the
  lightning bolt, and shadow-texture generation. Sprites, particles, marks and 2D quads are
  culled too; their corner orders face the viewer as specified. VERIFIED-CODE.
* Filtering (0x418a20): textures registered through the normal path (models, skins, sprites,
  particles, UI textures loaded by 0x4298c0, shadow maps) are mipmapped with
  `gluBuild2DMipmaps`; `GL_TEXTURE_MAG_FILTER = GL_LINEAR`; `GL_TEXTURE_MIN_FILTER =
  GL_LINEAR_MIPMAP_NEAREST` when `TextureFilter` = 0 ("Bilinear", the default) and
  `GL_LINEAR_MIPMAP_LINEAR` otherwise ("Trilinear"). Textures registered through the "pic" path
  (0x418d60: menu pictures, cursors, HUD bars) have no mipmaps and use `GL_LINEAR` for both. In
  software mode both paths use `GL_NEAREST` without mipmaps. VERIFIED-CODE.
* Wrap: `GL_REPEAT` for every texture (section 2.1). Sprite atlases and particle frame formulas
  rely on it (section 6.6). VERIFIED-CODE.
* Alpha: only 32-bit TGAs carry alpha; 8-bit paletted TGAs become RGB (alpha 1). Blending with
  alpha needs a 32-bit texture; additive and filter blending are what the paletted textures are
  for. VERIFIED-CODE 0x4187e0 + VERIFIED-DATA (section 4.1).
* Colour key: none. VERIFIED-CODE (no pixel inspection in the loader).
* `gfx\ui\font_alpha.tga` is registered at start-up but its handle is never read: the font is drawn
  additively from `font.tga` alone (section 8.3). VERIFIED-CODE 0x4298c0 (the only reference to
  0x1fd8f64 is its store).

## 5. Shadows

### 5.1 Shadow types after loading

`R_RegisterShadow(type, model, skin, rotation)` (0x415640) caches one shadow texture per
(model, skin, rotation); the rotation key is forced to 0 unless the type is projected.
`R_GenerateShadowMap` (0x414a70) then renders the texture and **rewrites the type**:
2 and 3 → 1, 5 and 6 → 4, 8 and 9 → 7. So LOW/HIGH only change the texture resolution, and three
draw types exist. VERIFIED-CODE.

| Draw type | Comes from (after the parser remap of `obj.md`) | Light direction for the silhouette | Rotation | How it is drawn | Data |
|---|---|---|---|---|---|
| 1 projected | SHADOW_PROJECTED(_LOW/_HIGH) | the sun direction L | baked, 30° steps | static terrain-clipped display list | 163 objects (all SHADOW_PROJECTED): buildings, bosses, map objects |
| 7 planar | SHADOW_PLANAR* and SHADOW_PLANAR_PROJECTED* | straight down (0, 0, 1) | applied at draw time from yaw | terrain-clipped mesh rebuilt every frame | 137 objects: all vehicles, helicopters, the player, bushes |
| 4 blob | raw types 4–6 | straight down | yaw | flat quad at terrain height | unreachable: the object parser remaps 4–6 to 7–9 |

VERIFIED-CODE 0x415640, 0x414a70, 0x40db20; VERIFIED-DATA for the counts (PLANAR 9, PLANAR_LOW
92, PLANAR_HIGH 36).

### 5.2 Silhouette texture generation (0x414a70), once per level at precache

1. For every model vertex v: rotate about Z by `rotation × 30°` (x' = x·cos − y·sin,
   y' = x·sin + y·cos), then project along the light: `p.x = x' − v.z·L.x/L.z`,
   `p.y = y' − v.z·L.y/L.z`. Compute the bounding rectangle, floored/ceiled to integers:
   (Xmin, Ymin, Xmax, Ymax) in world units, stored in the shadow record.
2. Texture size: `ex = log2(Xmax − Xmin)` if that width < 256 else 8; `ey = log2(Ymax − Ymin)` if
   the height < 128 else 7; W = 2^int(ex + 0.5), H = 2^int(ey + 0.5). LOW halves both (integer
   truncation), HIGH doubles both. Maximum default size 256×128.
3. Render at double resolution (2W × 2H pixels) into the back buffer: ortho
   `(0, windowW, 0, windowH, −99999, 99999)`, clear colour and depth to white/far, depth test on,
   culling off, texturing on with the object's skin (or the model's texture), blending ALPHA,
   colour black, translate by (−Xmin·sx, −Ymin·sy) with `sx = 2W/(Xmax−Xmin)`,
   `sy = 2H/(Ymax−Ymin)`; vertices at (p.x·sx, p.y·sy, v.z). Skin texels with alpha < 1 cast
   partial shadow.
4. Draw type 1 exactly (not LOW/HIGH) only: a white untextured quad over the rectangle at z = 0
   with the depth test on. It erases the silhouette wherever the highest geometry at that pixel
   is at or below model z = 0, i.e. foundations sunk into the ground cast no shadow.
   VERIFIED-DATA: 93 of the 163 projected-shadow objects have vertices below z = 0 (e.g.
   `bigdom` down to −3.5).
5. Depth test off, blend ADD, fog colour black, an untextured quad of colour (0.6, 0.6, 0.6)
   over the rectangle: covered pixels become 0.6 grey, the rest stays 1.0.
6. `glReadPixels(0, 0, 2W, 2H, GL_RED)` from the back buffer, 2×2 box filter down to W×H
   (0x4184e0, integer average), then build a 32-bit RGBA image with RGB = (114, 114, 114) and
   A = 255 − red. Register it as texture `"<model name>_shadow<N>"` with mipmaps. Restore the
   clear colour to the fog colour and clear colour and depth. VERIFIED-CODE (the RGBA conversion
   at 0x415523 is a block the decompiler drops).

Resulting shadow alpha: `A = 1 − min(1, (1 − coverage) + 0.6) = clamp(coverage − 0.6, 0, 0.4)`
per high-resolution pixel before filtering; opaque geometry gives 0.4. Row 0 of the image is
Ymin, column 0 is Xmin (original t = 0 at Ymin). VERIFIED-CODE.

Caveat reproduced by the original: the 2W × 2H area must fit in the window; HIGH shadows of large
objects in small video modes are cropped. VERIFIED-CODE (render target is the back buffer).

### 5.3 Drawing shadows (pass 5, 0x40db20)

Common state: depth test on, depth write off, polygon offset fill (−1, −1), blend ALPHA, texture
= the shadow texture, `glColor3f(0, 0, 0)`. Output: `dst·(1 − A)`, darkening by at most 40 %.
Fogged with the level fog. The opaque list is walked first, then the transparent list; records
without a model or without a shadow are skipped. Effect-list records never cast shadows.
VERIFIED-CODE.

* Type 1 (projected, static). At spawn (map spawner 0x407590 and `create` 0x41a7a0), a display
  list is built exactly like a mark (section 3.4) over `origin.xy + (Xmin, Ymin)..(Xmax, Ymax)`,
  with `s = (x − X0)/(X1 − X0)`, `t = (y − Y0)/(Y1 − Y0)`. It never moves afterwards and is freed
  with the entity. Rotation key: map objects use their placement byte (units of 30°); entities
  made by the `create` builtin pass `int(yaw in degrees)` as the key, which the generator also
  multiplies by 30° (quirk: correct only for yaw 0). VERIFIED-CODE 0x407080, 0x407590, 0x41a7a0.
* Type 7 (planar). Every frame: take the rectangle (Xmin..Xmax, Ymin..Ymax), rotate it about the
  entity origin by the entity yaw (angles[2], pitch and roll ignored), translate to the entity
  (x, y), clip every terrain triangle of the covered cells against the four rotated edges, and
  emit the clipped polygons at terrain height with texture coordinates from the two edge
  equations (s runs along the rotated X edge, t along the rotated Y edge). The shadow is always
  directly under the entity, whatever its altitude; its size and darkness never change with
  height and the sun direction is ignored. Helicopters (`PLANAR_HIGH`, 26 enemies and the 10
  player models) therefore show a top-down silhouette straight below them. VERIFIED-CODE
  0x40fde0, 0x40f8c0, 0x40f300; VERIFIED-DATA for the users.
* Type 4 (blob, unreachable from data): a flat textured quad over the rectangle, rotated by the
  yaw of axis rows 0 and 1 projected to XY, at z = terrain height under the origin.
  VERIFIED-CODE 0x40d920.

### 5.4 Recommended equivalent for OpenGL ES 3.0

GUESS (design recommendation, faithful by construction):

1. Generate the silhouettes at level load exactly as in 5.2, but into an FBO (RGBA8, 2W × 2H,
   depth attachment) instead of the back buffer, using the unlit program of section 10 for all
   three steps, then read back, box-filter, convert to (114, 114, 114, 255 − R) and upload with
   mipmaps. This also removes the window-size cropping; keep the cropping only if pixel-exact
   parity matters.
2. Draw both projected and planar shadows as terrain decals with the decal program of section 10:
   re-draw the terrain triangles of the 40-unit cells overlapping the (rotated) rectangle, compute
   `uv` in the vertex shader from world xy (`uv = Rinv·(xy − origin) − rectMin) / rectSize`),
   `discard` fragments outside [0, 1]², output `vec4(0, 0, 0, texture(shadow, uv).a)` with the
   state of 5.3. Discarding outside the rectangle is equivalent to the CPU clipping. Static
   projected shadows can cache their triangle index range at spawn like the original display
   list.

## 6. Particles

### 6.1 Objects involved

* Descriptors: up to 256 parsed `particles\*.ps` blocks, 0x138 bytes each (0x412d80).
* Instances: a fixed pool of 256 particle-system instances (0x60 bytes, pool at 0x1f502b8),
  one per particle-system attachment. An instance allocated from an empty pool is not created.
  VERIFIED-CODE 0x413780, 0x4137d0.
* Particles: each instance owns an array of `count` particles of 14 floats: age, frame (int),
  p0, p1, p2, v0, v1, v2, size, angle, r, g, b, a. VERIFIED-CODE 0x414290.

### 6.2 Keys and what the code does with them

| Key | Stored | Used by | Tag |
|---|---|---|---|
| `texture path cols rows` | texture (mipmapped), grid **columns and rows** | draw | VERIFIED-CODE |
| `texture_set n t1..t8` | random texture per instance | create | VERIFIED-CODE (absent from data) |
| `blend_mode` | 0–3 | draw, fade | VERIFIED-CODE |
| `rflag` | RF_NODEPTHTEST used; the other three parsed but unused | draw | VERIFIED-CODE |
| `coords` | 0 DECART, 1 CILINDER, 2 SPHERE | spawn, draw | VERIFIED-CODE |
| `draw_mode` | 0 billboard, 1 DRAW_VERT, 2 DRAW_HORIZ | draw | VERIFIED-CODE |
| `emit_mode` | only EMIT_ONCE (1) is special; EMIT_DURATION (2) behaves like the default continuous mode | create, update | VERIFIED-CODE |
| `emit_rate` | **integer** (`atol`) | create, update | VERIFIED-CODE; VERIFIED-DATA all 80 are integers |
| `emit_time` | parsed (`atol`, stored as float), **never read** | – | VERIFIED-CODE |
| `life_time` | float | update, draw | VERIFIED-CODE |
| `init_offset x y z dx dy dz` | base and spread | spawn | VERIFIED-CODE |
| `init_velocity vx vy vz dx dy dz` | base and spread | spawn | VERIFIED-CODE |
| `init_size base spread` | | spawn | VERIFIED-CODE |
| `init_frame base range` | integers | spawn | VERIFIED-CODE |
| `init_angle base spread` | degrees | spawn | VERIFIED-CODE (absent from data) |
| `init_color r g b a` | default alpha 1 | spawn, fade | VERIFIED-CODE |
| `accel ax ay az` | units/s² | update | VERIFIED-CODE |
| `size` | size growth per second | update | VERIFIED-CODE |
| `spin` | angle growth, degrees per second | update | VERIFIED-CODE (absent from data) |
| `color r g b a` | colour growth per second; **alpha component ignored** | update | VERIFIED-CODE (absent from data) |
| `fade_mode` | only FADE_LINEAR (1) is implemented; FADE_EXP (2) does nothing | update | VERIFIED-CODE; VERIFIED-DATA all 80 systems use FADE_LINEAR |
| `fade_factor` | default 1.0 | update | VERIFIED-CODE |
| `anim_mode` | only ANIM_LINEAR (1) is implemented; ANIM_NORMAL and ANIM_LOOP do nothing | update | VERIFIED-CODE; VERIFIED-DATA only ANIM_LINEAR is used (3 systems) |
| `anim_speed` | parsed, **never read** | – | VERIFIED-CODE |
| `axis x y z` | normalised, default (0, 0, 1), **never read** | – | VERIFIED-CODE |
| `damage touch amount p2 p3` | radial damage from particles | update (gameplay) | VERIFIED-CODE |

"Never read": no code other than the parser references these descriptor offsets, and the four
functions that access descriptors through a base pointer (0x413820, 0x414290, 0x414600,
0x413a50) do not use them. VERIFIED-CODE.

### 6.3 Instance lifecycle

* Creation. When an object with `attach PS_NAME tag` is initialised (0x409ba0 → 0x4099a0 →
  0x413820), an instance is allocated at the tag position. Its **oriented** flag is the
  attachment's `abs` modifier. Pool size:
  `count = emit_rate` for EMIT_ONCE, else `count = int(emit_rate × life_time + 4)`.
  EMIT_ONCE spawns all `count` particles immediately; other modes mark every particle dead
  (age = life_time + 1). The emission clock starts at 0 and the instance starts emitting at once.
  VERIFIED-CODE. VERIFIED-DATA: pools range 1–404 (`PS_SNOW`), sum 2136 over the 80 descriptors.
* Every entity update (0x405a60 → 0x4139e0) copies the attachment's world position and axis into
  the instance ("origin" and "axis"); if the instance was just (re)started, "previous origin" is
  set to the same position.
* `AttachDeactivate(id)` / `deactivate` (0x404a90): stop emitting (existing particles live on and
  fade); `AttachActivate(id)` / `activate` (0x404a40): resume emitting and reset the emission
  clock. Both recurse into the attachment's own children. Attachments start active.
  VERIFIED-CODE 0x41aa90, 0x41ab50, 0x404a40, 0x404a90, 0x409ba0.
* When the owning entity is removed (0x404410), the instance stops emitting and is freed as soon
  as all its particles are dead. VERIFIED-CODE 0x404410, 0x414600.
* EMIT_ONCE instances never emit again, even after `AttachActivate`. VERIFIED-CODE 0x414600.

### 6.4 Emission and update (0x414600, once per game frame, not while paused)

```
clock += dt
if not stopped and emit_mode != EMIT_ONCE:
    interval = 1 / emit_rate
    n = floor(clock / interval) ; step = 1/n
    start = prevOrigin
    while interval < clock:                       # strict comparison
        spawn(particle[ring])                     # section 6.5, uses prevOrigin
        prevOrigin += step * (origin - start)     # spawn points spread along the frame's path
        clock -= interval
        ring = (ring + 1) mod count               # oldest particle is recycled
for each particle with age <= life_time:
    size  += size_rate * dt
    angle += spin * dt
    rgb   += color.rgb * dt
    vel   += accel * dt
    pos   += vel * dt                              # after the velocity update
    if fade_mode == FADE_LINEAR:
        k = (1 - age/life_time) * fade_factor
        BLEND_ALPHA:  a   = init_color.a * k
        BLEND_ADD:    rgb = init_color.rgb * k
        BLEND_FILTER: rgb = init_color.rgb * (fade_factor - k)
        (no blend: nothing)
    if anim_mode == ANIM_LINEAR:
        frame = int(age / life_time * cols * rows)
    age += dt
```

VERIFIED-CODE 0x414600 (fade and animation read from the disassembly). The fade overwrites what
`color` added for the faded channels. Particles are simulated in the coordinate space of their
`coords` mode (6.5): DECART particles are in world space and stay where they were emitted;
CILINDER and SPHERE particles are relative to the **current** instance origin and follow the
emitter.

### 6.5 Spawn (0x414290)

```
age = 0
if coords == DECART: base = init_offset.xyz + prevOrigin   else: base = init_offset.xyz
p   = base + init_offset.dxyz * (srand11, srand11, srand11)          # three independent draws
vel = init_velocity.xyz + init_velocity.dxyz * (srand11, srand11, srand11)
if oriented and coords == DECART:
    vel = vel.x*axis0 + vel.y*axis1 + vel.z*axis2                     # attachment axis rows
rgba  = init_color
angle = init_angle.base + init_angle.spread * srand11  (+ yaw of the axis when oriented)
size  = trunc(init_size.base) + init_size.spread * rand01             # base truncated to integer
frame = init_frame.base + trunc(rand01 * init_frame.range)
```

VERIFIED-CODE. Offsets are never rotated, only velocities. The size truncation is visible in
the data: `PS_MISTRAIL` 1.5, `PS_MISTRAIL_BLACK` 2.5, `PS_ISKORKA` 1.5, `PS_LBOMB_HIT` 1.5,
`PS_SPICE_STORM` 1.5 all lose their fraction. VERIFIED-DATA. `rand01` can be exactly 1, so the
frame can reach `base + range` with probability 1/32768.

Coordinate modes (p0, p1, p2 are the three position components, simulated with the three velocity
components):

| Mode | p0 | p1 | p2 | World position at draw time |
|---|---|---|---|---|
| DECART (0) | x | y | z | (p0, p1, p2) |
| CILINDER (1) | azimuth, degrees | radius | height | `O + (cos a·p1, sin a·p1, p2)`, a = int(p0) wrapped to [0, 360) |
| SPHERE (2) | azimuth, degrees | elevation, degrees | radius | `O + (cos a·cos e·p2, sin a·cos e·p2, sin e·p2)`, a = int(p0), e = int(p1) |

VERIFIED-CODE 0x413a50 (sin table at 0x1fa92a0 built by 0x41e810, cos = the same table shifted by
90 entries; integer-degree lookup). O is the instance's current origin. VERIFIED-DATA: 55 DECART,
24 CILINDER (fires, smoke columns, pick-up swirls; e.g. `init_velocity 144 0 30 0 20 30` = 144°/s
spin, radius speed ±20, rise 30 ± 30), 1 SPHERE (`PS_LASER_NAKOPL`).

### 6.6 Drawing (0x413a50, pass 10)

Per system: `glPushMatrix`; blend mode from the descriptor (fog colour swapped as in 4.1); depth
test off if RF_NODEPTHTEST; depth write off for all particles; culling on; texture = the
instance texture; for BLEND_FILTER the texture environment becomes `COMBINE` with
`COMBINE_RGB = ADD` (rgb = texture + vertex colour, alpha = texture·colour), restored to MODULATE
after the system. One quad per particle with `age <= life_time`, vertex colour = particle RGBA.
VERIFIED-CODE.

Frame cell (original t):

```
du = 1/cols ; dv = 1/rows
u0 = (frame mod cols) * du
v0 = 1 - dv - (frame div rows) * dv       # NOTE: divides by rows, not by cols
```

VERIFIED-CODE (two `IDIV`s, by +0x70 then by +0x74). With `GL_REPEAT` this equals the expected
cell only when cols = rows or rows = 1. For the 4×2 atlases it selects cells
(col, row) = (0,0), (1,0), (2,1), (3,1), then repeats, so only four of the eight cells are ever
shown. VERIFIED-DATA: 27 descriptors use 4×2 or 4×1 grids with random frames up to 7 or 8; two
ANIM_LINEAR systems (`PS_MISTRAIL_BLACK`, `PS_MISTRAIL_BLACK2`) are 4×2. Reproduce the formula
as written.

Corners, with P the particle's world position, s its size (half extent) and texture corners
(u0, v0) to (u0 + du, v0 + dv):

| Draw mode | Axis X | Axis Y | Uses angle | Tag |
|---|---|---|---|---|
| 0 billboard | R (camera right, section 2.1) | U (camera up) | **no** | VERIFIED-CODE |
| 1 DRAW_VERT | (cos a, 0, sin a) | (−sin a, 0, cos a) | yes, a = int(angle) | VERIFIED-CODE |
| 2 DRAW_HORIZ | (cos a, sin a, 0) | (−sin a, cos a, 0) | yes | VERIFIED-CODE |

Vertices in order: `P − s·X − s·Y` (u0, v0), `P + s·X − s·Y` (u0 + du, v0),
`P + s·X + s·Y` (u0 + du, v0 + dv), `P − s·X + s·Y` (u0, v0 + dv). VERIFIED-CODE for the
billboard and for the first corner of the two rotated modes; the other rotated corners follow the
same pattern in the listing.

No sorting: systems are drawn in instance-list order, most recently created first (new instances
are appended at the list tail and both the draw and the update walk from the tail backwards),
particles in array (ring) order. VERIFIED-CODE 0x4149c0. Particles are never lit.

Weather uses ordinary descriptors: `PS_SNOW` (DRAW_VERT, BLEND_ADD, 200 particles/s,
`gfx\flare_white_red_small.tga`), `PS_RAIN` (DRAW_VERT, BLEND_ADD, `gfx\rain.tga`),
`PS_SPICE_STORM(2)` (sandstorm), attached to the `snow`, `rain`, `rain_constant` objects of
`nature.obj` whose script `scripts\snow.scr` only moves the emitter with the scroll.
VERIFIED-DATA.

## 7. Special effects

### 7.1 Lightning (0x41c000 builtin, 0x40fe60 draw)

The `Lightning` builtin submits, for every live enemy (class 2) within 500 units, a record with
internal flag 0x1000, sort 3 (effect list), start = the caller's origin, end = the target's
origin, and applies damage. VERIFIED-CODE 0x41c000. The draw (0x40fe60):

* texture `gfx\lightning2.tga` (mipmapped); depth test off, culling off, blend ADD, fog colour
  black, colour white;
* `d = end − start`, `len = |d|`, `p1 = PerpendicularVector(d̂)` (0x41e130: the axis of smallest
  |component|, orthogonalised and normalised), `p2 = d̂ × p1`;
* texture matrix translate `(time × 3, 0)`; two crossed quads, each 16 units wide:
  corners `start ∓ 8·p`, `end ± 8·p` for p = p1 then p2, texture s from 0 at the start to
  `len/96` at the end, t from 0 to 1 across;
* afterwards texture matrix identity, texture, culling and depth test re-enabled, blending off.

VERIFIED-CODE. `gfx\lightning1.tga` is not referenced by the executable. VERIFIED-CODE (strings).

### 7.2 Lasers, tracers, projectiles, flares

There is no other native beam or tracer code: every `glBegin` caller is accounted for in this
document (2D quads, sprites, blob shadow, brightness, decal lists, planar shadows, lightning, debug
line, models, particles, shadow generation, terrain, water). Lasers, tracers, muzzle flashes,
projectile glows, lens-flare-like glows and hit sparks are models, sprites and particle systems
from the data. VERIFIED-CODE (import callers of `glBegin`/`glDrawElements`). The `gfx\flare*.tga`
family is used by SPRITE objects with BLEND_ADD (e.g. `whiteflare_small` uses the left half of
`flare_white_red_small.tga` via `min … 0 0` / `max … 0.5 1`); their size is the `min`/`max`
rectangle times the entity scale; any fading is the script writing the entity alpha or colour.
No occlusion query or depth read-back exists. VERIFIED-DATA + VERIFIED-CODE.

### 7.3 Player shield and bonus shells

The shields and "moln" shells are ENV_QUAD models over `gfx\black.tga` with RF_NOLIGHTING and
BLEND_ADD (section 4.3): a sphere-mapped `gfx\effects\shield.tga` or `moln.tga` rotating at 30°/s
about the texture origin, tinted and faded by the script-controlled entity colour.
VERIFIED-DATA + VERIFIED-CODE.

### 7.4 Explosion light on the ground

Two mechanisms, both data-driven: additive ground decals (`expl*_light` TYPE_MARK objects,
section 3.4) and dynamic lights placed by scripts with `PlaceLight` (12 scripts, e.g.
`effects\expl1light.scr`, campfires, the player), which light terrain and models
(section 2.4). VERIFIED-DATA (strings in the scripts) + VERIFIED-CODE.

### 7.5 Clouds, marks, scores, banners

* Clouds: `big_clouds` HSPRITE (section 3.3) placed by the map. VERIFIED-DATA.
* Scorch marks: TYPE_MARK `mark`, BLEND_FILTER. VERIFIED-DATA.
* Floating score numbers: `score_num` SPRITE, `gfx\scores.tga` 10×1 grid, one digit per entity
  (frame = field 33), BLEND_ADD, RF_NODEPTHTEST, ±6×±8 units. VERIFIED-DATA.
* Banner: `objects\banner.obj`, RF_BANNER model (section 4.2). VERIFIED-DATA.

### 7.6 Screen overlays

* Screen fade (0x404370, every frame): while a global fade value f is > 0 it moves by ±dt
  towards 1 (fade in) or 0 depending on a flag, and a full-screen 2D rectangle of colour
  (0x458ca4) with alpha `int(f·255)` is drawn with BLEND_ALPHA. VERIFIED-CODE; which events set
  the flag and colour is behaviour (`engine-behaviour.md`).
* Menus and pause screens darken or tint with full-screen BLEND_FILTER or untextured BLEND_ALPHA
  rectangles in the 2D list (e.g. 0x427670, 0x407b50). VERIFIED-CODE.
* `CameraQuake` affects rendering only through the view angles and fovy (section 2.1).
  VERIFIED-CODE 0x40c150.

## 8. 2D rendering

### 8.1 Setup (0x40df90)

Viewport = full window, projection `glOrtho(0, 800, 600, 0, −99999, 99999)` (y down, origin at
the top-left), modelview identity, depth test off, blending on with the ALPHA function as
default, fog off, texturing on, culling unchanged (2D quads face the viewer). VERIFIED-CODE.
The virtual 800×600 space is stretched to any window size and aspect (non-uniform on 5:4
modes). VERIFIED-CODE.

### 8.2 The 2D list (0x40cdb0 and helpers, drawn by 0x40cee0)

Up to 4096 quads per frame, each: x, y, w, h (virtual pixels), s0, t0, s1, t1, texture, RGBA,
blend mode. Drawn in submission order after the whole 3D scene. VERIFIED-CODE.

| texture value | Draw | Tag |
|---|---|---|
| > 0 | textured quad: (x, y) gets (s0, t1), (x, y+h) gets (s0, t0), (x+w, y+h) gets (s1, t0), (x+w, y) gets (s1, t1) | VERIFIED-CODE |
| 0 | untextured filled rectangle | VERIFIED-CODE |
| −1 | untextured line | VERIFIED-CODE |
| −2 | untextured rectangle outline (polygon mode line) | VERIFIED-CODE |

"Draw pic" (0x40ce50) uses the texture's pixel size as w, h and the full texture (0, 0, 1, 1),
so pictures appear upright at one texel per virtual pixel. Colours are passed packed as
0xAABBGGRR (0x40ccd0). VERIFIED-CODE.

### 8.3 Font (0x425290)

* Texture `gfx\ui\font.tga` (256×256, 8-bit paletted, so RGB without alpha) for bytes 0x00–0x7F,
  `gfx\ui\font_rus.tga` for bytes 0x80–0xFF (glyph index = byte − 0x80). `font_rus.tga` is not
  shipped, so its registration fails and those characters would draw as solid additive
  rectangles. VERIFIED-CODE + VERIFIED-DATA.
* Glyph grid: 8 columns × 16 rows of 32×16-texel cells; glyph c occupies
  `s0 = (c mod 8)/8`, `t0 = 15/16 − (c div 8)/16`, `s1 = s0 + 30/256`, `t1 = t0 + 15/256`
  (original t: the left 30 texels and the lower 15 texel rows of the cell).
* Each glyph is a 30×15 virtual-pixel quad at the pen position, **BLEND_ADD** (black is
  transparent). The pen advances by the table below. Flag 0x4000 centres the string on x, 0x2000
  right-aligns it; flag 0x10000 enables markup: `{` switches to white, `}` back to the call colour,
  and neither is drawn. The call colour's alpha is forced to 1. VERIFIED-CODE.

Advance widths in virtual pixels, table at 0x456598 (rows not listed are all 0). VERIFIED-CODE.

| Row | +0 … +F |
|---|---|
| 0x10 |  8 17 17 12 14 14 11 19 19  9  9 20 21 21 12  0 |
| 0x20 | 10  3  7 12 14 19 16  4  5  5  9 12  3  7  4 15 |
| 0x30 | 14  8 12 13 14 13 13 13 13 13  3  3 11 11 11 11 |
| 0x40 | 18 15 13 13 14 11 11 14 14  3 11 13 11 17 15 13 |
| 0x50 | 13 15 13 13 13 13 15 23 15 14 13  5  8  5 11 12 |
| 0x60 |  5 11 11 11 11 11  7 11 10  3  4 10  3 18 10 11 |
| 0x70 | 11 11  9 11  9 10 11 16 10 10 10  6  3  6 11  0 |
| 0xA0 |  0  0  0  0  0  0  0  0 12  0  0  0  0  0  0  0 |
| 0xB0 |  0  0  0  0  0  0  0  0 12  0  0  0  0  0  0  0 |
| 0xC0 | 15 13 12 14 13 12 14 12 14 14 13 14 15 13 13 13 |
| 0xD0 | 11 13 12 13 12 12 12 12 15 15 15 14 12 12 15 14 |
| 0xE0 | 11 12 11 11 13 11 14 11 13 13 13 13 15 13 12 13 |
| 0xF0 | 13 12 12 14 12 12 13 13 13 14 14 14 11 11 14 11 |

### 8.4 Mouse cursor

Unless `UseSystemMouse` is set, at the end of the UI frame the cursor is drawn as two pics at
(mouseX − 4, mouseY − 2) virtual pixels: `menu\cursor_1.tga` with BLEND_ALPHA, then
`menu\cursor_2.tga` with BLEND_ADD, white. VERIFIED-CODE 0x4299f0, 0x401aa0. The HUD layout is in
`engine-behaviour.md`.

## 9. Resources

### 9.1 Texture loading (0x4187e0, 0x418a20)

* Name → registry lookup by exact, case-sensitive string compare; on a miss, `<name>.tga` is
  loaded (0x4189d0 appends ".tga" after stripping the extension). VERIFIED-CODE 0x418c80,
  0x418cf0.
* TGA types handled: 1 (colour-mapped, expanded to 24-bit RGB through a 24-bit palette),
  2 (24 or 32-bit, BGR(A) swapped to RGB(A) in place), 3 (8-bit grey, uploaded as
  `GL_LUMINANCE`). RLE types, the image ID field and the origin bit are not handled; rows are
  uploaded in file order. VERIFIED-CODE. Every shipped file fits these limits (`tga.md`).
* Upload: internal format = components (1, 3 or 4), format `GL_LUMINANCE`/`GL_RGB`/`GL_RGBA`,
  `GL_UNSIGNED_BYTE`; mipmaps from `gluBuild2DMipmaps` (box filter, which also rescales
  non-power-of-two images; the shipped files are all powers of two up to 256), followed by a
  redundant level-0 `glTexImage2D`. VERIFIED-CODE.
* Registry: 2048 slots (index 0 means "none"), 0x94 bytes each; "RegisterTexture: Too many
  registered textures." when full. All textures are freed together (0x418dd0). VERIFIED-CODE.

### 9.2 Models and other registries

| Registry | Capacity | Tag |
|---|---|---|
| Models | 1024 (index 0 = none), "R_RegisterModel: Too many registered models." | VERIFIED-CODE 0x411e70, 0x407080 |
| Shadow maps | 4096, "R_RegisterShadow: Too many shadowmaps." | VERIFIED-CODE 0x415640 |
| Particle descriptors / instances | 256 / 256 | VERIFIED-CODE |

Precache at level start (0x407080): for every object placed in the map, its model, skin and
shadow are registered, which is when all shadow textures are generated; the loading bar advances
by 0.3/N per object. VERIFIED-CODE.

### 9.3 Screen projection used by collision and traces

This answers the first open question of `engine-behaviour.md` (collision rectangles). It is
renderer code, so it is documented here; the collision rules themselves stay in
`engine-behaviour.md` §5.

Capture (0x419040, `R_SetFrustum`, pass 3 of the render, right after the terrain): the renderer
reads back `GL_MODELVIEW_MATRIX` and `GL_PROJECTION_MATRIX` as doubles and `GL_VIEWPORT` as
integers, and stores their product `C = P · V` (0x418e30, 4×4 double multiply). At that moment the
modelview holds only the view matrix of section 2.1 (no model transform). The six frustum planes
are extracted from `C` at the same time. The entity pass runs before the render, so every
collision and trace test in frame n uses `C` and the viewport of frame n − 1. VERIFIED-CODE
0x408df0 order, 0x40e0d0, 0x419040.

Point projection (0x419540, `R_WorldToScreen`), for a world point p:

```
clip = C · (p, 1)
winX = vp.x + (vp.w · clip.x / clip.w + videoWidth ) / 2
winY = vp.y + (vp.h · clip.y / clip.w + videoHeight) / 2      # GL window coordinates: y = 0 at the BOTTOM
depth = (V · (p, 1)).z                                         # eye-space z (negative in front)
```

`vp` is the viewport read back from GL and `videoWidth`/`videoHeight` the video mode size
(0x1fa9210/0x1fa9214). Because the 3D viewport is always the full window (section 1.3), this is
exactly `gluProject` without the depth range, in real window pixels, y up. No clipping or `w <= 0`
guard exists. VERIFIED-CODE 0x419540 (the `(... + videoWidth) / 2 + vp.x` form uses the video
size, not `vp.w`, in the second term; both are equal in practice).

Entity rectangle (0x419630, `R_ProjectEntityBounds`), for a record with a model:

* The eight corners are `(bs.x · X, bs.y · Y, bs.z · Z)` with X ∈ {min.x, max.x}, Y ∈ {min.y,
  max.y}, Z ∈ {min.z, max.z} of the **MDL header bounding box** (model +0x84..+0x98) and `bs` the
  object's `bbox_scale` (default 0.7). **The pivot of `bbox_scale` is the model origin**, not the
  box centre: each bound is multiplied component-wise. VERIFIED-CODE.
* Each corner is transformed by `C · M`, where M is the record's axis and origin (section 3.2)
  **without the entity scale** (field 32 is ignored here), then projected with the formula above.
* The rectangle is the min and max of the eight projected (winX, winY) (initialised to ±9999);
  its depth is the eye-space z of the record origin. VERIFIED-CODE.
* Records without a model (sprites, marks) use the projected origin only (0x419540).

On-screen tests used by `engine-behaviour.md` §5: 0x4198d0 (a point strictly inside
`0 < x < videoWidth`, `0 < y < videoHeight`) and 0x419920 (a rectangle with min ≥ 0 and max ≤
the video size). VERIFIED-CODE. The sphere test for visibility is 0x419970: the entity is visible
when `dot(plane, origin) + radius > 0` for all six planes of the previous frame. VERIFIED-CODE.

Recommendation (GUESS): to be resolution-independent, evaluate the same formulas with a fixed
800×600 viewport and the 4:3 projection of the camera, keeping y up; results then match the
original exactly at 800×600 and proportionally elsewhere.

## 10. Shader plan (OpenGL ES 3.0)

GUESS (design), derived from sections 2–8. Two programs cover everything outside terrain and
water; a third mode flag avoids a separate env program.

Common vertex outputs: `fogF = clamp((fogEnd − dEye)/(fogEnd − fogStart), 0, 1)` with
`dEye = −eyePos.z` (use 1.0 when fog is off). Common fragment tail:
`rgb = mix(uFogColour, rgb, fogF)` where `uFogColour` is chosen per blend mode on the CPU
(level colour; (0,0,0) for ADD; (1,1,1) for FILTER). Blend, depth test, depth mask, culling and
polygon offset stay GL state set from the tables of section 4.

**Program A: `model`** (TYPE_MODEL).

| Input | Content |
|---|---|
| attributes | position, uv, normal (the folded |n|, model space), `ivec3 slot` + `vec3 weight` (section 2.3), precomputed at load |
| uniforms | MVP, modelview, normal matrix (inverse transpose, not renormalised), `vec3 uSlot[6]` (computed on the CPU per record: ambient cube + dynamic lights), `bool uUnlit`, `vec4 uEntityColour`, `int uEnvMode`, `float uEnvAngle` (QUAD: time·30°), `vec2 uUvScroll` (RF_BANNER: (time·0.1, 0)), samplers `uSkin`, `uEnv`, fog |
| vertex colour | `c = uUnlit ? uEntityColour : vec4(clamp(Σ weight_k·uSlot[slot_k], 0, 1), 1)`; flat-normal models: compute per face (flat shading via `flat` qualifier or unshared vertices) |
| sphere uv | `u = normalize(eyePos)`, `n' = normalMatrix·normal`, `r = u − 2n'(n'·u)`, `m = 2·sqrt(r.x² + r.y² + (r.z+1)²)`, `st = r.xy/m + 0.5`; QUAD: rotate st about (0,0) by uEnvAngle |
| fragment | `base = texture(uSkin, uv + uUvScroll) · c`; GLITTER: `rgb = base.rgb + env.rgb, a = base.a·env.a`; CHROME: `rgb = mix(env.rgb·c.rgb, skin.rgb, skin.a)`; QUAD: `out = env·c`; then fog |

**Program B: `unlit`** (sprites, marks, shadow decals, particles, lightning, brightness, 2D,
shadow generation).

| Input | Content |
|---|---|
| attributes | position (3D or 2D), uv, vertex colour RGBA |
| uniforms | MVP, `int uTexMode` (0 untextured, 1 modulate, 2 add = FILTER particles), `mat3 uTexMatrix` (lightning scroll), sampler, fog, `int uAlphaFromTexOnly` (shadow decals output `vec4(0,0,0,tex.a)`) |
| fragment | untextured: `col`; modulate: `tex·col`; add: `vec4(clamp(tex.rgb + col.rgb, 0, 1), tex.a·col.a)`; then fog (2D: fog off) |

Decal variant of B (marks, projected and planar shadows): position = terrain vertex, uv computed
in the vertex shader from world xy and the rectangle (optionally rotated), `discard` outside
[0,1]² (section 5.4).

Brightness: B untextured, colour `(b, b, b, 1)`, blend `(DST_COLOR, SRC_COLOR)` over an 800×600
ortho quad. GLES 3.0 accepts these blend factors.

## 11. Limits, open questions, corrections

### 11.1 Limits and constants

| Constant | Value | Tag |
|---|---|---|
| Near plane / far plane | 4 / 2000 without fog, max(fog far, 1000) with fog | VERIFIED-CODE |
| Virtual 2D resolution | 800 × 600 | VERIFIED-CODE |
| Draw lists | opaque 512, transparent 128, effect 256, sprites 512, marks 128, 2D quads 4096 | VERIFIED-CODE |
| Dynamic lights per frame | 32 | VERIFIED-CODE |
| Light wrap / diffuse | 0.3 + 0.7·cos, linear falloff to the radius | VERIFIED-CODE |
| Spot cone | outer cos(0.5·angle), inner cos(0.4·angle) | VERIFIED-CODE |
| Polygon offset for decals | factor −1, units −1 | VERIFIED-CODE |
| Terrain cell | 40 units | VERIFIED-CODE |
| Shadow darkness | alpha ≤ 0.4, colour black at draw | VERIFIED-CODE |
| Shadow texture default max | 256 × 128 (LOW ½, HIGH ×2), rendered at 2× and box-filtered | VERIFIED-CODE |
| Projected shadow rotation step | 30° | VERIFIED-CODE |
| RF_BANNER | s scroll 0.1/s, colour 0.75 ± 0.25 at 1.5 rad/s | VERIFIED-CODE |
| ENV_QUAD rotation | 30°/s | VERIFIED-CODE |
| Lightning | half width 8, texture repeat every 96 units, scroll 3/s, range 500 | VERIFIED-CODE |
| Particle pool | emit_rate (ONCE) or int(rate·life + 4); 256 instances | VERIFIED-CODE |
| Glyph | 30 × 15 virtual pixels, 8 × 16 grid | VERIFIED-CODE |
| Texture registry / models / shadows | 2048 / 1024 / 4096 | VERIFIED-CODE |
| Brightness neutral value | 0.5 (default 0.6) | VERIFIED-CODE |

### 11.2 Open questions, most visible first

1. Planar shadows ignore altitude and sun: confirm on a running original that helicopter shadows
   sit directly below them and do not shrink (the code says so, the visual check would settle
   whether another path adds an offset). GUESS that no other path exists.
2. Exact fog distance metric of the original drivers (|z_eye| versus radial distance); visible at
   the screen edges near the fog end. GL leaves it to the implementation.
3. Billboard roll mismatch during camera quakes and intermissions (section 2.1): whether
   `angle2` is ever non-zero outside quakes and intermissions (`engine-behaviour.md`).
4. Particle frame row formula (`frame div rows`): reproduce as is; a visual check on a 4×2 smoke
   atlas (`PS_CAMPFIRE`) would confirm only four cells appear.
5. The corner order of the rotated particle quads beyond the first corner (section 6.6) was read
   from a long FPU listing; worth a second reading if particles appear mirrored.
6. Who sets the screen fade flag and colour (0x458c95, 0x458ca4): behaviour spec.
7. Whether `create`-spawned projected shadows (key = int(yaw)) occur in practice.
8. The texture-binding cache is left pointing at the env map after an ENV_GLITTER draw although
   unit 0 still holds the skin; an object drawn next with the env map as its skin would get the
   wrong texture. Probably never happens in the data.
9. Characters ≥ 0x80 in English strings (would draw as boxes since `font_rus.tga` is missing).
10. The internal render flag 0x4000 (white line) has no known setter.

### 11.3 Corrections to other specifications found while reading the code

For the owners of those documents; not edited here.

* `obj.md` `frames`: the two integers are the sprite atlas **columns and rows**, not a start and
  end frame. VERIFIED-CODE 0x40a160, 0x40d370.
* `obj.md` `min`/`max`: for the sprite and mark types (the only 108 objects using them) the four
  numbers are `x y s t`: a 2D quad corner and its texture coordinate, not a bounding box.
  VERIFIED-CODE 0x40a160 (stores to +0x149, +0x14D, +0x155, +0x159 and +0x15D, +0x161, +0x169,
  +0x16D) + VERIFIED-DATA.
* `obj.md` `scale`: never reaches the renderer (overwritten every update by entity field 32).
* `ps.md` `texture`: the two integers are grid columns and rows. `emit_rate` is parsed as an
  integer. `emit_time`, `anim_speed` and `axis` are never read; FADE_EXP, ANIM_NORMAL and
  ANIM_LOOP are not implemented; `color` changes RGB only.
* `mdl.md` 1.1 normals (its lines "no renormalization" and "never renormalizes a normal before
  lighting"): each face normal is normalised (0x41e220 called at 0x411a50) and each accumulated
  smooth vertex normal **is** renormalised (0x41e220 called at 0x411b67 with ESI = the vertex's
  normal), and 0x411520 then replaces every stored normal by its absolute value. Lighting never
  uses the normal directly anyway, only the acos weights of section 2.3. VERIFIED-CODE.
* `mdl.md` 1.1 winding ("`glFrontFace(0x0901)` // GL_CW"): **0x0901 is `GL_CCW`** (`GL_CW` is
  0x0900). `GL_Init` sets `glFrontFace(GL_CCW)` + `glCullFace(GL_BACK)` (PUSH 0x901 at 0x41245c,
  PUSH 0x405). The model draw (0x410ac0) submits `(v0, v1, v2)` in file order, and the model
  matrix is a proper rotation (the axis of 0x41e390 has determinant +1, checked numerically), the
  view is a product of `glRotatef` calls and the projection is `gluPerspective`, so nothing
  mirrors the geometry: object-space CCW stays CCW in window space and is the front face. There
  is nothing left to reconcile; a GLES pipeline with `glFrontFace(GL_CCW)`, `glCullFace(GL_BACK)`
  and the file's index order is faithful. VERIFIED-CODE.
* `levels-txt.md` `sun`: diffuse colour, direction towards the sun (normalised at load), ambient
  colour; `fog` near/far are the GL linear fog start/end and also drive the far clip plane.
* `rcsl-vm.md` field 33: the sprite atlas cell index; field 32: model and SPRITE scale (0 means
  unscaled); fields 28–31: used by unlit models, sprites (RGBA) and marks (RGB).

### 11.4 Comparison with `engine-behaviour.md` §13

The frame order of §13.1 matches section 1.1 pass for pass; this document adds the clear, the
brightness overlay and confirms its GUESS: **no list is depth-sorted** (section 1.2), including
SORT_TRANS and SORT_EFFECT. The material table of §13.2 agrees with sections 4 and 5, with these
refinements and one disagreement:

* BLEND_ADD fog colour: §13.2 says "GUESS: to black"; it is black, VERIFIED-CODE (section 2.2).
* ENV_CHROME: agrees, but the skin in the chrome combine is unlit and no shipped object uses
  ENV_CHROME (section 4.3).
* **Disagreement:** §13.2 lists "SHADOW_PLANAR(_PROJECTED) 4–9 … re-projected every frame". The
  values 4–6 never reach the renderer (the object parser remaps them to 7–9, `obj.md`), and the
  shadow generator then folds 8 and 9 into 7; the per-frame path handles only 7, while a raw 4
  would take a different flat-quad path (0x40d920). Also the planar path does not project along
  the sun at all: it drapes a top-down silhouette directly below the entity (section 5.3).
  "Re-projected" should read "re-draped under the entity, rotated by yaw".
* Lighting line "(0.7 · dot + 0.3) · (r − d)/r": correct for terrain vertices; for models it is
  evaluated once at the entity origin per signed local axis, with |dot| (section 2.4).
* Collision projection (§5.1 and open question 1 there): answered in section 9.3 (pivot of
  `bbox_scale` = model origin, GL window pixels with y up, previous frame's matrices, entity
  scale ignored).

## Changelog

- 1.0 (WP-25): initial version. Includes the collision screen projection (section 9.3), the
  winding settlement for `mdl.md` and the comparison with `engine-behaviour.md` §13 (section
  11.4). Not listed in `docs/spec/README.md` by this package (file outside its scope); note that
  the README's "Smooth model normals: unnormalised sums" row contradicts section 11.3.
