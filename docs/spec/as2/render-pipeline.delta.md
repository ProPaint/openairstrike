# Render pipeline: AirStrike 2 delta (`as2`)

Delta version 1.0 (B5). Amends [../render-pipeline.md](../render-pipeline.md) 1.0 (with issues
[070](../issues/070-render-extras-choices.md), [110](../issues/110-particle-damage.md),
[111](../issues/111-health-bar-drawing.md), [112](../issues/112-shadow-key-and-music-jump.md))
for the game `as2` (AirStrike 2 v2.51, `AirStrike3D II.exe`), and, in its section 12, the
renderer-side parts of [../hmap.md](../hmap.md) (terrain texturing, lighting, tiles, water),
which the base render spec leaves to that document. Companions:
[symbol-map.md](symbol-map.md), [rcsl-builtins-semantics.delta.md](rcsl-builtins-semantics.delta.md)
(`TerraMorph`, `WaterHeight`, `PlaceLight`, `Lightning`), [rcsl-vm.delta.md](rcsl-vm.delta.md)
(entity offsets). Names of the functions cited: `re/symbols_as2_render.csv`.

The AirStrike 2 renderer is a Direct3D 8 rewrite of the v1.70 OpenGL renderer. Our engine
keeps its OpenGL ES 3.0 renderer, so this delta states **what is seen on screen**, expressed
against the base spec; Direct3D states are named only where the look depends on them.

## Method

- Every Direct3D device call of the game code (as2@0x401000–0x44ef00, the statically linked
  Direct3DX library above it excluded) was labelled by vtable slot and decoded state name with
  `re/tools/d3d8_annotate.py` (reads the gitignored export; prints labels, never commits code).
  The full list of render and texture-stage states the game sets is in section 1.1; every
  state not in that list keeps its Direct3D 8 default.
- Every render function of `re/symbols_as2.csv` (subsystems render, gl, frame, 2d, sprite,
  shadow, decal, texture, terrain, light, material, effect, particle, cull, model) was read,
  with the disassembly where the decompiler drops blocks (particle corners, shadow constants,
  water waves, skid-mark texture scale). Constants were read from the executable.
- Data: `re/tools/scan_render_data.py --game as2` (and `--game as3d` for comparison) over the
  1533 object and 109 particle definitions, 20 levels, all 761 TGA files and 23 maps.
- Our renderer: `as3d_viewer --game as2 level 2|3|4|8` (normal and `--overview`) on this
  branch's build (section "What our renderer does with AS2 data today").
- Cross-check: the new skid-mark update (gulf@0x413280), the water stages (gulf@0x434d60) and
  the shadow pass (gulf@0x42ed60) carry the same constants in Gulf Thunder.
- Ground truth pictures: the AS2 manual (`third_party_local/games/as2/Manual/`) has no in-game
  screenshot (only logo, button and title images), so none could be used.

Tags as in [../README.md](../README.md). `VERIFIED-CODE` cites `as2@` and, where a counterpart
exists, `v170@`. `INFERRED-SEQUEL` is not used. `VERIFIED-DATA` holds for the `as2` files
scanned as above.

## 0. Conventions

| Item | AirStrike 2 | Tag |
|---|---|---|
| World axes, angles, rand01 | same as the base | VERIFIED-CODE (math functions identical, symbol-map.md) |
| Texture coordinate convention | Textures are loaded by Direct3DX, which honours the TGA origin bit, so **v = 0 is the top row of the picture** (the convention of our engine). Wherever the base spec quotes an original `t`, AS2 sends `v = 1 − t`: model UVs (as2@0x418270, only when Direct3D is up), sprites (as2@0x4371e0), particles (as2@0x435070), tile overlays (as2@0x4377d0) and 2D quads (as2@0x42e1c0). The result on screen is therefore the same as v1.70 for all of these. Exceptions, where AS2 uses the coordinate **unflipped** (so the picture is mirrored top-to-bottom relative to v1.70): ground marks (section 3.4), the detail texture (section 12.3), the water (section 12.5); shadow textures are generated in the matching orientation and look the same (section 5.2). | VERIFIED-CODE |
| Frame time, game time | same globals (as2@0x49f920, as2@0x49f924) | VERIFIED-CODE (symbol-map.md) |

## 1. Frame structure

### 1.1 Order of a game frame — changed

Begin frame as2@0x430e50 (v170@0x40e690): empties the draw lists, clears colour to the fog
colour and depth to 1.0 (no stencil), begins the scene. End frame as2@0x431110 (v170@0x40e6f0):
brightness quad (1.5), end scene, present. VERIFIED-CODE.

Scene render as2@0x430a20 (v170@0x40e0d0). "Bias" is the Direct3D depth bias used instead of
the base's polygon offset; it only prevents z-fighting of coplanar decals, the order on screen
is the draw order.

| # | Pass | as2 function | Depth test | Depth write | Bias | Blend | Against the base |
|---|---|---|---|---|---|---|---|
| 0 | Clear | as2@0x430e50 | – | – | – | – | same |
| 1 | Viewport, projection, view | as2@0x430a20 | on | – | – | – | same values; see 2.1 for the view rotation order and the billboard axes |
| 2 | Terrain, then per patch its tile overlays | as2@0x437f80 → as2@0x437c60, as2@0x4377d0 | on | on (overlays off) | overlays 1 | none (overlays ALPHA) | same order; texturing and lighting in section 12 |
| 3 | Frustum | as2@0x41ca10 | – | – | – | – | same |
| 4 | Ground marks (TYPE_MARK) | as2@0x42ff70 (v170@0x40d860) | on | off | 2 | per mark | changed colour alpha (3.4) |
| 5 | **Skid marks (new)** | as2@0x430280 | on | off | 3 | ALPHA | new pass, section 7.7 |
| 6 | Shadows | as2@0x430440 (v170@0x40db20) | on | off | 4 | **FILTER** (was ALPHA) | changed, section 5.3 |
| 7 | Opaque list | as2@0x433f40 | per record | per record | – | per record | same |
| 8 | Water | as2@0x438860 | on | on | – | ALPHA | changed, section 12.5 |
| 9 | Transparent list | as2@0x433f40 | per record | per record | – | per record | same |
| 10 | Effect list (lightning inside) | as2@0x433f40 | per record | per record | – | per record | same |
| 11 | Particles | as2@0x435970 | on unless RF_NODEPTHTEST | off | – | per system | same |
| 12 | Debug counters | – | – | – | – | – | moved out of the scene render into a 2D overlay (as2@0x40aa40), same `Show*` keys; not a 3D pass any more |
| 13 | Sprites | as2@0x42fe80 | on unless RF_NODEPTHTEST | on unless RF_NODEPTHWRITE | – | per sprite | **culling off for the whole pass** (3.3) |
| 14 | 2D list | as2@0x4309b0 | off | – | – | per quad | same (fog off during the pass) |
| 15 | Brightness quad | as2@0x430eb0 (called by as2@0x431110) | off | – | – | DST_COLOR, SRC_COLOR | same |

VERIFIED-CODE for every row.

Render states the game code sets anywhere (complete list from the annotator scan): lighting
off (once, as2@0x42f620); solid fill (wireframe only for 2D outlines); cull none/clockwise
(clockwise culling keeps counter-clockwise triangles, as the base); depth enable, depth write,
depth bias 0–4; shade flat/Gouraud; blend enable, blend operation add, source/destination
factors of 4.1; fog enable, table fog mode linear, fog colour, start, end. Texture stages:
colour and alpha operations and arguments, texture coordinate index, texture transform flags,
min/mag filter linear, mip filter point or linear. **Never set, so at their Direct3D default:**
alpha test (off), stencil (off), specular (off), dithering (off), range-based fog (off),
Direct3D lights and materials (not used), texture addressing (wrap), depth function
(less-or-equal), anisotropy. VERIFIED-CODE (scan of as2@0x401000–0x44ef00).

So, as in v1.70, there is **no alpha test**: cutout art (fences, trees, bushes) with
BLEND_ALPHA writes depth over its transparent texels unless RF_NODEPTHWRITE is set, exactly as
the base 4.1 says.

### 1.2 Draw lists and sorting — same

as2@0x430740 has the identical instruction shape of v170@0x40de50 (symbol-map.md): same five
lists, same capacities, same routing by type and sort. No list is sorted (as2@0x430a20 walks
them in order). VERIFIED-CODE. Data (TYPE_MODEL, VERIFIED-DATA): SORT_TRANS + BLEND_ALPHA 140,
SORT_TRANS + no blend 3, SORT_EFFECT + BLEND_ADD 58, + BLEND_ALPHA 1, + no blend 1; left in the
opaque list: BLEND_ADD 61 (54 SORT_OPAQUE, 7 SORT_ONGROUND), BLEND_ALPHA 27.

### 1.3 Viewport, video modes, aspect — same look

Modes come from the Direct3D adapter (as2@0x4012b0, the options screen lists them); the 3D
viewport is the whole back buffer and the aspect is width/height (as2@0x430a20). The 2D layer
is still a virtual 800×600 space stretched to the window (as2@0x430880). VERIFIED-CODE.

### 1.4 Vertical sync — not checked

The swap-interval call of v170@0x40e690 is gone; the presentation interval is a device
creation parameter, not read. No visible effect.

### 1.5 Brightness — same

as2@0x430eb0 (named "fade overlay" in symbol-map.md: it is the brightness quad) draws an
untextured 800×600 quad of colour (b, b, b) with DST_COLOR, SRC_COLOR after the 2D list:
result `2·b·dst`. VERIFIED-CODE. The screen fade of the base 7.6 is as2@0x40b1c0, identical
shape to v170@0x404370.

## 2. Global state

### 2.1 Baseline state, projection and view — changed (view rotation order, billboard axes)

- Projection: right-handed perspective (as2@0x44d258 is the Direct3DX right-handed builder),
  `fovy` from the view, aspect width/height, near 4, far 2000 without fog else max(fog end,
  1000). Same as the base. VERIFIED-CODE as2@0x430a20.
- View: translate by −origin, then rotate about Z by angle2, about X by angle0, about Y by
  angle1 (a yaw-pitch-roll matrix, as2@0x430a20). The base applies Z, then Y, then X. The two
  agree whenever angle1 = 0, which holds in play and in both AS2 intermissions
  (`intermission ... -60 0 30`, VERIFIED-DATA); only `CameraQuake` perturbs angle1, so a quake
  shakes along a slightly different path. Rotation signs are the same. VERIFIED-CODE.
- Camera mode table as2@0x49db28 = v170@0x457398 (same four modes). VERIFIED-CODE. Intermission
  camera: AS2 adds `0.5·sin` to angle0 besides the base's `1.4·sin` on angle2 (as2@0x414e90);
  behaviour, reported for the game-rules delta.
- **Billboard axes** (sprites, particles): taken from the actual view matrix (its rotation
  transposed, stored by as2@0x430a20 and read by as2@0x42fe80 and as2@0x435070). They equal the
  true camera right and up vectors also when angle2 ≠ 0, so the base's "mirrored in roll"
  quirk of section 2.1 is **gone**: in intermissions (roll 30° ± 1.4°) and camera quakes,
  sprites and particles face the camera correctly. VERIFIED-CODE.

### 2.2 Fog — same

Linear table (per-pixel) fog, start/end = level near/far, depth-based (range fog never
enabled), colour = level colour and also the clear colour (as2@0x42fd30, as2@0x430e50). Swapped
per blend mode as in the base: black while ADD, white while FILTER, restored otherwise
(as2@0x42fb50). Off during the 2D list (as2@0x4309b0). Enabled when the level has `fog`
(as2@0x40e1e0); `DisableDistanceFog` is still read from config.ini (as2@0x401b90) but no reader
was found (GUESS: no effect). VERIFIED-CODE. Data: all 20 levels have fog, near 500–900, far
800–1000 (VERIFIED-DATA), so the far clip is always 1000.

### 2.3 Lighting: the sun — same

No Direct3D lighting (off at as2@0x42f620; the vertex format as2@0x42f0f0 has position,
diffuse colour and two texture coordinates, no normal). Models are lit on the CPU with the
same six-slot ambient cube, dynamic lights and acos weights: as2@0x433800 (v170@0x410530),
weights as2@0x4180a0 (identical shape to v170@0x411520, including the folding of normals to
absolute values). Colours clamped to [0, 1] per channel, alpha 1. Lit models ignore the entity
colour; RF_NOLIGHTING uses the entity RGBA. Flat-normal models draw one colour per face, smooth
ones Gouraud. VERIFIED-CODE.

### 2.4 Dynamic lights — same

32 per frame (as2@0x4305f0, as2@0x430670, identical shapes to v170@0x40dd00, v170@0x40dd80);
model formula as the base (as2@0x433800); terrain formula `(0.7·N·V + 0.3)·(r − d)/r·spot`,
added ×255 and clamped (as2@0x41a7d0). Sprites, marks, skid marks, particles, lightning and 2D
are unlit. VERIFIED-CODE.

### 2.5 Night levels — same

None of the render functions read for this delta references the night flag (as2@0x4c207c);
night comes from data (darker `sun` and `fog`, night attachments). 8 of the 20 levels are
`night` (VERIFIED-DATA). VERIFIED-CODE for the functions read; an exhaustive reader search was
not made. Note the water of a night level is no longer darkened by the sun
(section 12.5).

## 3. Geometry of each object type

### 3.1 Render record — same

Same record copied by as2@0x430740; the entity sync as2@0x40c750 now clamps the colour to
[0, 1] before it reaches the renderer (the base's GL clamped at draw time: same look) and
handles the water alignment flags (section 12.6). Record flag 0x80 makes the model pass skip
its texture (as2@0x433f40); no keyword or code sets it (GUESS: debug, like 0x4000).
VERIFIED-CODE.

### 3.2 TYPE_MODEL — same, except a missing texture and RF_BANNER

Same steps (as2@0x433f40). Differences:

- A model whose skin and default texture both failed to load is drawn with **no texture**
  (Direct3D texture unset: white × vertex colour, a plainly lit grey model), where v1.70 kept
  the previously bound texture (base quirk 3.2.4). 135 object definitions reference texture
  files that are not shipped; about 10 of them are reachable in play (`cannon4_gun`,
  `cannon5_gun`, `cannon7_lighting_gun`, `katusha_dead`, `sinogoga`, `tank_big_almostdead`,
  `tank_big_rockets_dead`, `vagon04`, `wavegun_small1_proj`, `wavegun_small2_proj`).
  VERIFIED-CODE as2@0x4359d0 + VERIFIED-DATA.
- RF_BANNER is still parsed but the model pass no longer scrolls or pulses anything; no AS2
  object uses it (VERIFIED-DATA), so nothing is visible. VERIFIED-CODE as2@0x433f40.

### 3.3 Sprites — changed (culling)

Same quad, frame grid, placement, scale, colour and depth flags (as2@0x4371e0), with the true
camera axes of 2.1. **Face culling is off for the whole sprite pass** (as2@0x42fe80), so
back-facing HSPRITE/VSPRITE quads and mirrored billboards are visible; v1.70 culled them.
Data: 111 TYPE_SPRITE (109 BLEND_ADD, 2 unblended health bars), 3 TYPE_HSPRITE (`big_clouds*`,
BLEND_ALPHA), 0 VSPRITE (VERIFIED-DATA). VERIFIED-CODE.

### 3.4 TYPE_MARK — changed (alpha, texture orientation)

Same terrain-clipped decal built once at spawn (as2@0x431ef0, as2@0x431240), drawn in pass 4
(as2@0x42ff70) with the record's blend and skin. Differences, VERIFIED-CODE:

- the vertex colour carries the **entity alpha** (v1.70 forced alpha 1); only visible for a
  BLEND_ALPHA mark with a script-faded alpha, which the data does not have (marks are FILTER
  `mark`, `mark_big` and ADD `expl*_light`);
- the decal texture coordinate is used unflipped, so the mark picture appears mirrored along
  world y compared with v1.70; the shipped mark textures are radially symmetric, so this is
  not visible.

Marks created after a `TerraMorph` crater follow the new heights; marks built earlier keep the
old shape (rcsl-builtins-semantics.delta.md). Data: 8 marks (`mark`, `mark_big` new at ±100
units, six `expl*_light`), VERIFIED-DATA.

### 3.5 Use of the entity render fields — same

## 4. Materials

### 4.1 Blend modes — same

as2@0x42fb50 (v170@0x412be0): 1 ALPHA = SRC_ALPHA, INV_SRC_ALPHA; 2 ADD = ONE, ONE (fog black);
3 FILTER = DEST_COLOR, ZERO (fog white); 4 = DEST_COLOR, SRC_COLOR (brightness); 0 = blending
off. Depth write stays on unless RF_NODEPTHWRITE; no alpha test. Alpha sources as the base:
lit models send alpha 1, unlit models, sprites and particles multiply texture alpha by colour
alpha (stage 0 modulate on colour and alpha, as2@0x435ad0). VERIFIED-CODE.

**Material state table** (VERIFIED-DATA, `scan_render_data.py`: every combination of type,
blend, render flags, sort, environment mode and texture format that occurs in the 1533 AS2
definitions; count in the last column). States follow from 4.1, 4.2 and 4.4 (all textures:
bilinear/trilinear with mipmaps, wrap; no alpha test anywhere). "Tex" = skin, else the model's
own texture; "missing" = file not shipped (drawn untextured, 3.2).

| Type | Blend | Render flags | Sort | Env | Tex | Count | Blend state | Depth test / write | Cull |
|---|---|---|---|---|---|---|---|---|---|
| MODEL | none | – | OPAQUE | – | paletted | 681 | off | on / on | back |
| MODEL | none | – | OPAQUE | – | 24-bit / 32-bit / missing / no model | 20 / 24 / 104 / 75 | off | on / on | back |
| MODEL | none | – | OPAQUE | GLITTER | paletted / missing | 74 / 4 | off | on / on | back |
| MODEL | none | – | TRANS | – | paletted | 3 | off | on / on | back |
| MODEL | none | – | EFFECT | – | no model | 1 | – | – | – |
| MODEL | none | NOCULLING | OPAQUE | – | paletted / 24-bit / missing | 95 / 18 / 14 | off | on / on | none |
| MODEL | none | NOCULLING+NODEPTHWRITE | OPAQUE | – | 32-bit | 1 | off | on / off | none |
| MODEL | none | NOLIGHTING | OPAQUE | GLITTER | paletted / 24-bit | 9 / 1 | off | on / on | back |
| MODEL | alpha | – | OPAQUE | – | paletted | 1 | alpha (texture alpha 1) | on / on | back |
| MODEL | alpha | – | OPAQUE | GLITTER | 32-bit | 6 | alpha | on / on | back |
| MODEL | alpha | – | TRANS | – | 32-bit / missing | 20 / 1 | alpha | on / on | back |
| MODEL | alpha | NOCULLING | OPAQUE | – | 32-bit | 20 | alpha | on / on | none |
| MODEL | alpha | NOCULLING | TRANS | – | 32-bit / paletted / missing | 68 / 1 / 2 | alpha | on / on | none |
| MODEL | alpha | NOCULLING+NODEPTHWRITE | TRANS | – | 32-bit / missing | 45 / 1 | alpha | on / off | none |
| MODEL | alpha | NOCULLING+NOLIGHTING | TRANS | – | 32-bit | 2 | alpha, entity alpha | on / on | none |
| MODEL | alpha | NOCULLING+NODEPTHTEST+NOLIGHTING | EFFECT | – | missing | 1 | alpha | off / on | none |
| MODEL | add | – | EFFECT | – | paletted | 1 | add | on / on | back |
| MODEL | add | NOCULLING | OPAQUE | GLITTER | missing | 1 | add | on / on | none |
| MODEL | add | NOCULLING+NODEPTHTEST | EFFECT | – | paletted | 2 | add | off / on | none |
| MODEL | add | NOCULLING+NODEPTHTEST+NOLIGHTING | EFFECT / OPAQUE | – | paletted (24+9) / missing (3) | 36 | add | off / on | none |
| MODEL | add | NOCULLING+NODEPTHWRITE | OPAQUE | – | paletted | 11 | add | on / off | none |
| MODEL | add | NOCULLING+NODEPTHWRITE+NOLIGHTING | EFFECT | – | paletted / 24-bit / missing | 15 / 1 / 1 | add | on / off | none |
| MODEL | add | NOCULLING+NOLIGHTING | EFFECT / OPAQUE | – | paletted / missing | 1 / 1 | add | on / on | none |
| MODEL | add | NODEPTHTEST+NOLIGHTING | EFFECT / ONGROUND / OPAQUE | – | 32-bit (5) / paletted (2+5+2) | 14 | add | off / on | back |
| MODEL | add | NODEPTHTEST+NOLIGHTING | EFFECT | GLITTER | paletted | 2 | add | off / on | back |
| MODEL | add | NODEPTHWRITE | OPAQUE | – | paletted | 6 | add | on / off | back |
| MODEL | add | NODEPTHWRITE+NOLIGHTING | OPAQUE / ONGROUND | – | paletted | 5 / 2 | add | on / off | back |
| MODEL | add | NOLIGHTING | EFFECT / OPAQUE | – | 32-bit / paletted / missing | 1 / 1 / 1 | add | on / on | back |
| MODEL | add | NOLIGHTING | EFFECT | GLITTER | paletted | 1 | add | on / on | back |
| MODEL | add | NOLIGHTING | OPAQUE | QUAD | paletted | 18 | add (shields, shells) | on / on | back |
| SPRITE | add | – | OPAQUE / ONGROUND / EFFECT | – | paletted (17+3+1) / 32-bit (1) | 22 | add | on / on | **none** |
| SPRITE | add | NODEPTHTEST (± NOLIGHTING, ± NOCULLING) | OPAQUE / EFFECT | – | paletted (19+13+26+12+2) / 32-bit (1) / missing (1) | 74 | add | off / on | **none** |
| SPRITE | add | NODEPTHWRITE (± NOLIGHTING) | OPAQUE / EFFECT / ONGROUND / `SORT_flag` | – | paletted (5+5+1+1) / 24-bit (1) | 13 | add | on / off | **none** |
| SPRITE | none | NODEPTHTEST | EFFECT | – | paletted | 2 | off (health bars) | off / on | **none** |
| HSPRITE | alpha | – | EFFECT | – | 32-bit | 3 | alpha (clouds) | on / on | **none** |
| MARK | filter | – | OPAQUE | – | paletted | 2 | filter | on / off, bias 2 | back |
| MARK | add | – | OPAQUE | – | paletted | 6 | add | on / off, bias 2 | back |

RF_NOLIGHTING on sprites, RF_NOCULLING on sprites and SORT on sprites have no effect (as the
base). `SORT_flag` (one sprite in `zabors.obj`) is not a keyword and counts as sort 0. 71
distinct combinations in the object data; particles: 11 combinations, table in 6.6.

### 4.2 Render flags — same keywords, RF_BANNER inert

Keyword table of the object parser as2@0x4125a0 (v170@0x40a160): RF_NOLIGHTING, RF_NOCULLING,
RF_NODEPTHTEST, RF_NODEPTHWRITE, RF_NODLIGHT, RF_BANNER, no new render flag. Effects as the
base for models (as2@0x433f40), sprites (as2@0x4371e0) and particles (as2@0x435070), except:
sprites ignore RF_NOCULLING because culling is off anyway (3.3), RF_BANNER does nothing (3.2).
The internal bits 0x1000 (lightning) and 0x4000 (debug line, as2@0x4336f0) are kept.
VERIFIED-CODE. Data (VERIFIED-DATA): RF_NOCULLING 339, RF_NOLIGHTING 152, RF_NODEPTHTEST 129,
RF_NODEPTHWRITE 101, RF_NODLIGHT 0, RF_BANNER 0 uses. New non-render keywords of the parser:
`civilian`, `speed`, `TOUCH_CIVILIAN`, `skid_mark` (7.7), `FL_ONWATER_NORMAL` = 0x4|0x8 and
`FL_ONWATER_FLAT` = 0x4|0x200 (12.6). VERIFIED-CODE as2@0x4125a0.

### 4.3 Environment mapping — changed (coordinate formula)

Active under the same conditions (smooth model, `envmap`, multitexture available). The
coordinates are no longer the GL sphere map: they are computed on the CPU per vertex from the
folded model normal n transformed by the model's world matrix (including the entity scale)
and the view matrix: `s = 0.5 + 0.5·n_view.x`, `t = 0.5 + 0.5·n_view.y` (as2@0x433e60), used as
a Direct3D v (v = 0 top of the picture). So the environment picture is laid over the model as
seen from the camera, without the eye-direction reflection term; a scaled model spreads the
coordinates by its scale and they wrap. VERIFIED-CODE.

| Mode | as2@0x435e70 | Against the base |
|---|---|---|
| ENV_GLITTER | stage 0: skin × vertex colour (colour and alpha); stage 1: env map added to the colour, alpha taken from stage 0 | colour same; alpha no longer multiplied by the env alpha |
| ENV_CHROME | env map blended with the skin by the skin alpha | unused by data (as v1.70) |
| ENV_QUAD | env map × vertex colour, env coordinates rotated by `time × 30°` about the texture origin (0, 0) | same except the coordinate formula above |

Data: 116 objects with `envmap`: GLITTER 98 (`envmap1..4`, `chrome2`, `chrome3`, `moln`), QUAD
18 (`shield`, `moln` player shields and shells), no CHROME. VERIFIED-DATA.

### 4.4 Culling, filtering, wrap, alpha, colour key — same, one exception

- Culling: counter-clockwise front faces, back faces culled, off for RF_NOCULLING models, the
  lightning bolt, shadow generation and (new) the whole sprite pass. VERIFIED-CODE.
- Filtering (as2@0x42f620): min and mag linear on every stage, mip filter point
  (`TextureFilter` 0, bilinear) or linear (trilinear). Textures loaded as "pics"
  (as2@0x438550: menu pictures, cursors, HUD, comics) have a single level; all others a full
  Direct3DX mip chain (box filter). Same as the base. **Shadow textures are single-level (no
  mipmaps)** in AS2 (as2@0x4366f0), v1.70 mipmapped them. VERIFIED-CODE.
- Wrap on every texture (no addressing state is ever set). No alpha test, no colour key
  (textures are created with colour key 0, as2@0x4380e0). VERIFIED-CODE.
- `gfx\ui\font_alpha.tga` is still registered and unused (as2@0x42b1f0).

## 5. Shadows

### 5.1 Shadow types — same

Same keywords and remapping (as2@0x4125a0, as2@0x41a0f0 identical to v170@0x415640; types
2/3→1, 5/6→4, 8/9→7 after generation, as2@0x4366f0). Data: SHADOW_PROJECTED 357, PLANAR_LOW
179, PLANAR_HIGH 33, PLANAR 12, PLANAR_PROJECTED_HIGH 6 (VERIFIED-DATA). The string
`%s_shadow%i` is the name under which each generated silhouette is registered (as v1.70's
`<model>_shadow<N>`); shadows are still baked silhouette textures.

### 5.2 Silhouette generation — changed

as2@0x4366f0 (v170@0x414a70), once per level at precache. Same projection along the sun
(projected types) or straight down (planar), same 30° rotation steps, same bounding rectangle,
same `+0.5`-rounded power-of-two exponents, LOW halves, HIGH doubles. Differences
(VERIFIED-CODE):

1. Height limit: both width and height use the "below 256 texels, else exponent 8" rule; the
   v1.70 height limit was 128 (exponent 7). Default maximum 256×256 (v1.70 256×128). After
   LOW/HIGH each side is halved until 2·side ≤ 512, so no shadow texture exceeds 256×256
   (HIGH shadows of large objects are smaller than v1.70's 512×256 but are no longer cropped
   by the window).
2. Rendered into a dedicated 512×512 render target (as2@0x42eed0), cleared white, orthographic
   projection with y down (row 0 = Ymin), silhouette in black with BLEND_ALPHA and the skin
   texture (partial shadow from skin alpha, as the base), culling off.
3. **The base step 4 is gone**: no white quad at model z = 0 erases the parts at or below the
   ground, so sunken foundations cast shadow too. Data: 252 of the 345 projected-shadow
   objects have vertices below z = 0 (VERIFIED-DATA). Seen on screen: a thin extra band of
   shadow along the sun-facing base of those buildings.
4. The 0.6 grey additive quad is kept (colour 0xFF999999, depth test off): covered pixels 0.6,
   the rest 1.0.
5. The 2W×2H area is copied into a W×H single-level texture with a linear filter (at exactly
   2:1 this is the same 2×2 average as the base box filter), **keeping the grey colour**
   (no conversion to (114, 114, 114, 255 − R)). Row 0 is Ymin as in the base, so the
   orientation on the ground is the same.

### 5.3 Drawing shadows — changed (blend, fog)

as2@0x430440: depth test on, write off, bias 4, texture = the grey silhouette, vertex colour
white, **blend FILTER** (destination × texture colour). Covered ground is multiplied by 0.6 at
most: the same maximum darkening of 40 % and the same soft edge as v1.70 (`dst·(1 − A)` with
`A = 1 − grey`). Only the fog differs: in FILTER mode the fog colour is white, so a fogged
shadow fades to "no change", where v1.70's black-with-alpha shadow turned towards the fog
colour. Visible near the top of the screen, where the fog starts. Walk order (opaque then
transparent list), projected shadows as static decals built at spawn, planar shadows re-draped
every frame directly under the entity by its yaw (as2@0x433180 → as2@0x432b80, same algorithm
as v170@0x40fde0: no sun offset, no altitude scaling), blob path unreachable: all same.
VERIFIED-CODE. Shadows on craters: planar shadows follow a `TerraMorph` crater (re-draped per
frame), projected shadows built before it keep their old shape.

### 5.4 Recommended equivalent — amended

For `as2`, generate as in 5.2 (FBO, 512×512 cap, no ground-erase step), keep the texture grey
(or keep the base's alpha form, which is equivalent), draw with FILTER-equivalent blending
(`dst × grey`) and the white FILTER fog colour, no mipmaps. GUESS (design).

## 6. Particles

### 6.1–6.5 Objects, keys, lifecycle, emission, spawn — same

Parser as2@0x418c50 (unique string, same keyword set: no new particle keyword in AS2), pool
functions of identical shape, `PS_Update` as2@0x419cd0 score 0.999 with v170@0x414600, spawn
as2@0x419960 (symbol-map.md). Keyword use (VERIFIED-DATA, 109 systems against v1.70's 80):
same keys; `fade_mode` 110 statements (one system has two), all FADE_LINEAR; `anim_mode`
ANIM_LINEAR 8; `emit_mode` EMIT_ONCE 29; `coords` CILINDER 25, SPHERE 1; `draw_mode` VERT 4,
HORIZ 1; all `emit_rate` integers; no `texture_set`, `spin`, `color`, `init_angle`, `axis`,
`anim_speed`, `emit_time`. Grids: 4×2 52, 2×1 19, 1×1 14, 4×4 14, 4×1 7, 2×2 2, 3×3 1.

### 6.6 Drawing — same, with the true camera axes

as2@0x435070 (v170@0x413a50): same state (blend per system with fog swap, depth test off with
RF_NODEPTHTEST, depth write off, culling on), BLEND_FILTER systems add the texture to the vertex
colour, one quad per live particle as two indexed triangles, corners `P ∓ s·R ∓ s·U` in the
base order, the same frame-cell formula **including the `frame div rows` quirk** (two integer
divisions by columns then rows, as2@0x435389, as2@0x435397; 52 systems use 4×2 grids), the same
integer-degree sine table for CILINDER/SPHERE and the rotated modes. R and U are the true
camera axes (2.1). VERIFIED-CODE.

Particle state combinations (VERIFIED-DATA): ADD 32-bit 14, ADD paletted 30, ADD +NODEPTHTEST
32-bit 20 / paletted 16; ALPHA 32-bit 7, ALPHA paletted 1 (alpha from the fade only), ALPHA
+NODEPTHTEST 32-bit 3 / paletted 1; FILTER 32-bit 1, FILTER paletted 10, FILTER +NODEPTHTEST
paletted 6.

## 7. Special effects

### 7.1 Lightning — same drawing

as2@0x433260 (v170@0x40fe60): `gfx\lightning2.tga` (registered by as2@0x4077b0), depth test
off, culling off, ADD, two crossed quads 16 units wide, texture repeat every 96 units along the
bolt, scroll `time × 3`. The new radius argument and the `wavegun_hit` effect are builtin
behaviour (rcsl-builtins-semantics.delta.md). `gfx\lightning_big.tga` is used by data only.
VERIFIED-CODE.

### 7.2 Lasers, tracers, flares — same (data-driven)

### 7.3 Shields — changed only through 4.3

### 7.4 Explosion light on the ground — same

`expl*_light` ADD marks and `PlaceLight` lights (2.4).

### 7.5 Clouds, marks, scores, banners — same

`big_clouds`, `big_clouds2`, `big_clouds3` HSPRITEs; `mark`, new `mark_big`; the banner model
is not used by AS2.

### 7.6 Screen overlays — same

Screen fade as2@0x40b1c0 identical shape to v170@0x404370.

### 7.7 Skid marks — new

Objects may carry up to several `skid_mark a b w "texture"` statements (parser as2@0x4125a0;
188 statements on 93 objects, textures `gfx/marks/jeepmark1.tga` 64×64
32-bit and `tankmark1.tga` 32×32 32-bit, VERIFIED-DATA). Each statement gives the entity a
track (G_InitObject as2@0x411e90, pool of 64 tracks for the level, as2@0x40e1e0; when the
pool is empty the entity gets none). Update every frame after the entity pass (as2@0x414850,
named `G_UpdatePlayers` in symbol-map.md: it is this update). VERIFIED-CODE:

- The track keeps up to 23 cross-sections. With O the entity origin, X̂ and Ŷ the horizontal
  directions of its local X and Y axes (normalised), a section has two points
  `O + b·Ŷ + (a − w/2)·X̂` and `O + b·Ŷ + (a + w/2)·X̂`, each at terrain height + 2 units.
  (Vehicles face local Y; `a` is the lateral wheel offset, `b` the offset along the vehicle,
  `w` the track width; typical data `±17 21 16`.)
- Every 1/2.4 s (0.41667 s) a new section is appended; between appends the newest section
  follows the entity. When 23 sections exist the oldest is dropped.
- Each section ages with the frame time; alpha = 1 up to 5 s, then falls linearly to 0 at
  10 s; sections older than 10 s are removed. A track whose entity is gone stays until its
  last section has faded, then returns to the pool.
- Texture coordinates: u = 0 on the `a − w/2` edge, 1 on the `a + w/2` edge; v = distance the
  entity has travelled (3D, accumulated per frame) ÷ w, used as a Direct3D v (square texels,
  one repeat per `w` units of travel).
- Drawing (pass 5, as2@0x430280 → as2@0x4300e0): one triangle strip per track, oldest section
  first, vertex colour white with the section's alpha, Gouraud, BLEND_ALPHA with the texture's
  alpha, depth test on, write off, bias 3, back faces culled (a track drawn while reversing
  along −Y has its triangles facing down and is culled), fogged. At most 64 tracks per frame.

### 7.8 Health bars — same

`hbar_empty` / `hbar_full` unchanged (VERIFIED-DATA), drawn by as2@0x40bd70 with the same
structure as v170@0x404e30 (maximum health > 150, two sprites). Culling is off for them now,
which changes nothing for a camera-facing billboard.

### 7.9 Statistics overlay — debug only

as2@0x40aa40 draws FPS, triangles, texture binds, models, sprites, marks and entities with the
game font in the 2D layer when `ShowFPS`, `ShowTris`, `ShowTexBinds`, `ShowCounters` are set; it
is not part of the 3D pass. VERIFIED-CODE.

## 8. 2D rendering

### 8.1 Setup — same

as2@0x430880: viewport = back buffer, orthographic 0..800 × 0..600 with y down, depth off, fog
off during the list (as2@0x4309b0). VERIFIED-CODE.

### 8.2 The 2D list — changed (new primitives)

Up to 4096 quads (as2@0x4179f0), drawn in order by as2@0x42e1c0 (v170@0x40cee0) with the same
texture-value meaning (> 0 textured, 0 fill, −1 line, −2 outline) and the same corner-to-UV
assignment (after the v flip of section 0). New, VERIFIED-CODE:

- every quad is shifted by −0.5 virtual pixel in x and y before drawing (Direct3D pixel-centre
  alignment; at 800×600 exactly a half pixel, invisible);
- an optional rotation in degrees about the quad centre;
- an optional second texture with its own coordinates and a combine mode: 1 blend by the
  second texture's alpha, 2 add, 3 modulate, other values replace (as2@0x435bf0). Used by the
  new menu drawers (header gradient as2@0x417a90, frames as2@0x417b60); the front-end delta owns
  where they are used.

### 8.3 Font — same

as2@0x425d50 (v170@0x425290): same `gfx\ui\font.tga` (byte-identical file), same 8×16 grid,
30×15 glyphs, ADD, same `{`/`}` markup and alignment flags; the advance-width table
as2@0x49cc50 is identical to v170@0x456598 (all 256 entries compared). VERIFIED-CODE +
VERIFIED-DATA.

### 8.4 Mouse cursor — same textures

`menu\cursor_1.tga`, `menu\cursor_2.tga` registered by as2@0x4077b0. Drawing not re-read.

## 9. Resources

### 9.1 Texture loading — changed

as2@0x4380e0 (v170@0x4187e0 + v170@0x418a20), VERIFIED-CODE:

- Textures are read **from the loose `data\` directory** with the C runtime (not through the
  paks): the name as given, else with `.tga`, else with `.jpg`; then created by the
  Direct3DX file loader with the file's own format, default size (the file size, rounded up
  to powers of two where the device needs it) and a full box-filtered mip chain (single level
  for pics). Every texture of the shipped paks is also present loose (1610 loose files).
- Consequences for the look: TGA types 1, 2, 3, their RLE variants, the origin bit, files
  without footer and non-power-of-two sizes are all handled by Direct3DX. Data (VERIFIED-DATA,
  761 TGA): no RLE, no top-origin file, 42 files without footer (intro comic frames), one
  non-power-of-two texture (`models/mapobjects/elektro/power_plant.tga` 256×257, stretched to
  the rounded size: the whole picture still maps to 0..1), 8-bit greyscale `gfx/ui/snow.tga`
  (shows as grey, as with v1.70's luminance upload); no JPEG or PNG file is shipped, so the
  `.jpg` fallback and the libjpeg/libpng code are never used by the data (the loading comics
  are named without extension and resolve to `.tga`).
- Missing textures: the registration fails and the index is 0; drawing with index 0 unsets the
  texture (3.2).
- Registry: 2048 slots (as2@0x4384b0, as2@0x438550), shared with the generated terrain and
  shadow textures (as2@0x438310, as2@0x438260).
- Morph maps (`morphmaps/*.tga`, 8-bit grey 4×4 to 11×11) are read by the game's own TGA
  reader as2@0x41c3d0, not by Direct3DX, and never drawn.
- Command-line `-notex` disables every texture bind (as2@0x405a80, as2@0x4359d0): debug.

### 9.2 Registries — same capacities

Models 1024, shadow records 4096, particle descriptors and instances 256/256 (identical
functions, symbol-map.md), textures 2048.

### 9.3 Screen projection for collision — same

as2@0x41ca10 reads the view and projection back (as2@0x431170) right after the terrain pass
and stores their product; as2@0x41cee0 and as2@0x41cfd0 apply the base formulas unchanged
(window pixels with y up, `bbox_scale` about the model origin, 8 corners of the MDL box, entity
scale ignored, ±9999 initialisation including the same min/max update quirk), in single
instead of double precision. The previous-frame timing is unchanged (the entity pass runs before
the render). VERIFIED-CODE. The game-rules delta can keep the base section 9.3 and issue 120.

## 10. Shader plan — amended

The base programs cover AS2 with these additions (GUESS, design): a per-game flag for the
environment coordinate formula (4.3); a skid-mark strip program (the unlit program with
per-vertex alpha, 7.7); the water program of 12.5 (two scrolled layers blended by the second
layer's alpha, per-vertex alpha, vertex waves); the shadow decal output as `dst × grey` (5.3).

## 11. Limits, questions, corrections

### 11.1 Limits and constants — as the base, except

| Constant | AS2 | Tag |
|---|---|---|
| Decal ordering | depth bias: tiles 1, marks 2, skid marks 3, shadows 4 | VERIFIED-CODE |
| Shadow texture maximum | 256×256, no mipmaps | VERIFIED-CODE as2@0x4366f0 |
| Skid marks | 64 tracks, 23 sections, a section every 0.41667 s, fade 5–10 s, height +2 | VERIFIED-CODE as2@0x414850 |
| Water | layers ×2 and ×1.5 of the detail scale, wave amplitude 16, depth fade over 16 units | VERIFIED-CODE section 12.5 |

### 11.2 Open questions of the base, as far as AS2 answers them

1. Planar shadows ignore altitude and sun: also in AS2 (5.3).
2. Fog metric: depth-based (range fog off) in AS2.
3. Billboard roll mismatch: gone in AS2 (2.1).
4. Particle row formula: kept in AS2 (6.6).
8. The env-map texture-binding quirk: the AS2 stage cache (as2@0x435ad0) rebinds stage 0 when
   leaving the env state, so it cannot happen. VERIFIED-CODE.

### 11.3 Corrections to other specifications — see the section at the end.

## 12. Terrain and water (renderer side of `hmap.md`)

The loader is split into steps (symbol-map.md); what is drawn:

### 12.1 Geometry — same

Grid of (W+1)×(H+1) vertices, cell 40 units (as2@0x41bd90), heights resampled from the map
exactly as v1.70, `hmin + (hmax − hmin)·h/255`; chunks of 8 rows drawn as triangle strips per
row with frustum culling on a node tree (as2@0x437c60); no level of detail. Interior runs of
vertices that are all at the same height below the water level are skipped from the strips
(as2@0x41b780): the remaining triangles cover the same flat area, so nothing changes on
screen. Heights change at run time with `TerraMorph` (rcsl-builtins-semantics.delta.md,
issue 200); the renderer reads them every frame. VERIFIED-CODE.

### 12.2 Vertex normals and static lighting — changed

- Normal of a vertex: `normalize(normalize(−sx, 0, 1) + normalize(0, −sy, 1))` with
  `sx = (z(c+1) − z(c−1)) / 80`, `sy = (z(r+1) − z(r−1)) / 80` (central differences, 0 on the
  map border) (as2@0x41b2e0); v1.70 averaged the acos of up to six face normals. Nearly the
  same shading.
- Static colour (as2@0x41b580): `d = clamp(N·L, 0, 1)`, channel = `min(255, (sun·d + ambient)
  ·f·255)` with the same underwater factor f. **The v1.70 bug of hmap.md is fixed**: slopes
  facing away from the sun get the ambient colour instead of black. This is what our engine
  already does for every game (README deviations table), so for `as2` that deviation is
  faithful.
- Dynamic lights on terrain: same (2.4). `TerraMorph` does not recompute normals or colours:
  craters keep the lighting of the ground they replaced (the renderer only moves vertices).
- Vertex colour of tiles: the same per-vertex colours.

VERIFIED-CODE.

### 12.3 Texture layers — same, detail orientation mirrored

Map textures generated from `texture1..4` exactly as v1.70 (as2@0x41ab30: same `h/86` blend,
same resampling, one 256×256 texture per 32 rows, coordinates (c/32, r/32)). Detail texture
`detail.tga` with coordinates (c/4, r/4), combined by "add signed" (base + detail − 0.5) when
the device can, else modulate-2×, else modulate (as2@0x4362a0; the base: add signed or
modulate). The detail picture is loaded top-row-first and its coordinates are not flipped, so
it appears mirrored top-to-bottom compared with v1.70 (the detail textures are noise: not
visible). VERIFIED-CODE.

### 12.4 Tile overlays — same, sets 1–9, half-texel inset

Per chunk, cells with a tile set ≠ 0 get a quad on the cell's four vertices, drawn after the
chunk's terrain, BLEND_ALPHA, depth write off, bias 1, grouped by atlas (as2@0x41b780,
as2@0x4377d0). Atlas `tiles\tiles<n>.tga` for any n (as2@0x41b020), tiles of 64×64 texels,
`perRow = width/64`, `perCol = height/64`, index and rotation (0, 3, 6, 9, anything else = 0)
exactly as hmap.md. New: each tile's texture rectangle is inset by half a texel on every side
(`+0.5/width`, `+0.5/height`), which v1.70's integer arithmetic reduced to 0. VERIFIED-CODE.

Atlases (VERIFIED-DATA, all 32-bit): tiles1 512×64 (8 tiles), tiles2 256×64 (4), tiles3 256×64
(4), tiles4 256×128 (8), tiles5 512×64 (8), tiles6 256×256 (16, helipad and landing pads),
tiles7 256×64, tiles8 256×64 (both unused), tiles9 512×64 (8). Sets used by the 23 maps: 1, 2,
3, 4, 5, 6, 9; set 6 appears as 9-cell pads in most maps (16, 27, 45, 48 cells in some).
Rotation bytes: 0 4840, 3 1585, 6 119, 9 104, 1 and 150 once each. Four cells use an index past
their atlas (`level4` tiles2 index 5 twice, `level15` tiles4 index 8, `level16` tiles4 index
20): they wrap as in hmap.md.

### 12.5 Water — changed (most visible difference after the tracks)

Level statement: `water "<base texture>" "<shine texture>" <level> <opacity>` (VERIFIED-DATA:
18 of 20 levels; the parser reads four arguments, stored at level +0x1c0, +0x1c4 and the two
names, as2@0x40d490/as2@0x41bd90). Textures: `water_ocean_1` + `water_ocean_1_shine`,
`water_sand_1` + `water_sand_1_shine`, `water_lava2` (paletted) + `water_ocean_1_shine`; the
others 256×256 32-bit.

Geometry (as2@0x41bb60, VERIFIED-CODE):
- The water is a vertex grid on the terrain grid, not one quad per chunk. Each vertex gets a
  depth weight `w = clamp((level − terrain z)/16, 0, 1)` (0 for terrain above the level).
- Only cells with at least one corner at or below the water level are drawn (strips per row
  within each chunk); dry land has no water above it.

Animation, every frame for the visible chunks (as2@0x41d8d0, as2@0x438860), VERIFIED-CODE:
- `t += frametime·π·0.1` as in v1.70 drives the texture scroll; the waves use the game time T.
- A vertex with `w ≥ 0.0001` not on the first or last map row is at
  `level + 16·w·0.5·(sin(0.75·row + T) + sin(col + T))` (row and col are grid indices); other
  vertices sit at the terrain height (and are fully transparent). So deep water moves up and
  down by up to ±16 units in a travelling pattern, the shore stays still. The v1.70 bobbing of
  the whole plane by `sin t` is gone.
- Per-vertex alpha = `w × opacity`: the water **fades out towards the shore over the first 16
  units of depth** instead of cutting the terrain along a hard line.

Colour (as2@0x436440), VERIFIED-CODE:
- Stage 0: base texture at coordinates `2·(c/4, r/4) + (0.4·sin(t/2) + 0.2, −0.2·sin(t/4) − 0.3)`
  (twice the density of the detail scale: one repeat per 80 units).
- Stage 1: shine texture at `1.5·(c/4, r/4) + (0.4·sin t, 0.4·sin(t/2))`; the result is
  `lerp(base, shine, shine.a)`.
- Alpha = the vertex alpha only (texture alphas do not reach the blend). Blend ALPHA, depth
  test and write on, fogged, drawn after the opaque list.
- **Unlit**: the vertex colour does not reach the colour (stage 0 selects the texture), so the
  water keeps the texture's own brightness on night levels (v1.70 multiplied it by
  `sun·L.z + ambient`). The per-vertex sun lighting computed with the wave normals is used only
  on a device without multitexture.
- Texture coordinates used as Direct3D v (not flipped against v1.70's convention).

Terrain under water keeps the v1.70 underwater darkening factor (12.2). `TerraMorph` skips
vertices below the level and never rebuilds the water strips or weights.

### 12.6 Entities on water — changed

Entity sync as2@0x40c750 (v170@0x4057c0), VERIFIED-CODE:
- `FL_ONWATER` (0x4): z = `WaterHeight(x, y)`, the animated surface of 12.5 (v1.70: the fixed
  level), so boats ride the waves.
- `FL_ONWATER_FLAT` (0x204): z = the water level, no waves.
- `FL_ONWATER_NORMAL` (0xC): z as `FL_ONWATER`, and the axis is aligned to the plane through
  the surface at (x − 10, y + 15), (x + 10, y + 15), (x, y − 25), keeping the yaw
  (as2@0x41d6d0): boats pitch and roll with the waves.
- Data: FL_ONWATER 25, FL_ONWATER_NORMAL 10, FL_ONWATER_FLAT 4 objects (VERIFIED-DATA).

## What our renderer does with AS2 data today

`as3d_viewer --game as2 level 2`, `3` (with `--overview` and `--scroll 6300` over the lake),
`4`, `8` render without errors, 0 missing terrain textures, tile sets 6 and 9 load (the tile
code takes any set number). Findings:

1. **No water is drawn.** `engine/src/game/defs.cpp` reads `water` as texture, level, opacity,
   so an AS2 line gives level = the shine texture name (0) and opacity = the real level (for
   example −97): the water is invisible and the lake beds render black (level 3). The level
   parser must accept the two-texture form (per-game).
2. Skid marks are not parsed or drawn (unknown keyword `skid_mark`).
3. `FL_ONWATER_NORMAL` and `FL_ONWATER_FLAT` are unknown flags.
4. Everything else in the pictures (models, lighting, fog, shadows, sprites, tiles) looks as
   expected from the base rules; the differences of the list below are not visible in still
   pictures except the shadow fog and the missing water.

## What our renderer must change for AirStrike 2

Ordered by how visible the difference is. "Switch" = needs a per-game switch.

1. **Water** (amends hmap.md "Water" / base pass 7): parse `water base shine level opacity`;
   draw a grid on the terrain vertices of the wet cells with depth-faded alpha, two scrolled
   layers blended by the shine alpha, unlit, and the vertex waves; `WaterHeight` for
   `FL_ONWATER`. Switch. ([issue 220](issues/220-water-surface.md))
2. **Skid marks** (new, base section 7): parse `skid_mark`, keep up to 64 tracks, draw them in
   a new pass between marks and shadows. Switch (the base game has no such keyword, so it can
   be data-driven). ([issue 221](issues/221-skid-marks.md))
3. **Boats on water** (12.6): `FL_ONWATER_NORMAL`/`_FLAT` and wave-following z. Switch.
4. **Shadow fog and ground-erase** (5.2 step 4, 5.3): no ground-erase step for `as2` (bands at
   building bases); shadows fade to "no change" in fog. Switch.
5. **Environment coordinates** (4.3): normal-based coordinates on chrome, glass and shields.
   Switch.
6. **Billboard axes with roll** (2.1): true camera axes for sprites and particles during
   intermissions and quakes. Switch.
7. **Shadow texture size** (5.2): height limit 256 instead of 128, cap 256×256, no mipmaps.
   Switch.
8. **Sprite pass without culling** (3.3). Switch.
9. **Missing textures draw untextured** (3.2; about 10 reachable objects). Switch, or a
   single rule if our engine's placeholder is already white.
10. **Terrain normals** by central differences (12.2). Switch; nearly invisible.
11. **Tile half-texel inset** (12.4), **mark alpha and orientation** (3.4), **detail orientation**
    (12.3), **2D half-pixel shift** (8.2): invisible with the shipped data; may be skipped.
12. **New 2D primitives** (8.2: rotation, second texture): needed by the front-end delta.
13. View rotation order (2.1): only during camera quakes; may be skipped.

## Looks the same, verified

Frame order and list routing (1.1, 1.2); no alpha test, no stencil, no specular (1.1); clear
to the fog colour; brightness quad (1.5); projection, near/far planes, camera table (2.1); fog
mode, colours per blend mode, start/end, 2D without fog (2.2); CPU lighting with the ambient
cube, acos weights, dynamic lights on models and terrain (2.3, 2.4); night handled by data
(2.5); model drawing, RF flags, blend modes, alpha sources (3.2, 4.1, 4.2); sprite frames and
placement (3.3); mark decals (3.4); culling, filtering, wrap, mipmaps (4.4); shadow types,
darkness (40 % max), softness, placement, planar shadows straight below (5.1–5.3); particles
entirely, including the 4×2 row quirk (6); lightning, flares, explosion light, clouds, score
digits, screen fade, health bars (7); 2D setup and font (8.1, 8.3); collision projection
(9.3); terrain geometry, base texture generation, detail combine, tile layout and rotation,
underwater darkening, terrain vertex lighting on back slopes as our engine already does (12).

## Checked sections

| Base section | Status |
|---|---|
| 0 Conventions | changed (texture v convention; same look except marks, detail, water) |
| 1.1 Order of a game frame | changed (skid-mark pass, shadow blend, sprite culling, debug text moved) |
| 1.2 Draw lists and sorting | same |
| 1.3 Viewport, video modes, aspect | same |
| 1.4 Vertical sync | not checked |
| 1.5 Brightness | same |
| 2.1 Baseline state, projection, view | changed (rotation order, billboard axes) |
| 2.2 Fog | same |
| 2.3 Lighting: the sun | same |
| 2.4 Dynamic lights | same |
| 2.5 Night levels | same |
| 3.1 Render record | same (flag 0x80 noted) |
| 3.2 TYPE_MODEL | changed (missing texture, RF_BANNER inert) |
| 3.3 Sprites | changed (culling off) |
| 3.4 TYPE_MARK | changed (alpha, orientation; invisible with the data) |
| 3.5 Entity render fields | same |
| 4.1 Blend modes | same |
| 4.2 Render flags | same (RF_BANNER inert) |
| 4.3 Environment mapping | changed |
| 4.4 Culling, filtering, wrap, alpha, colour key | changed (sprites unculled, shadow textures without mipmaps) |
| 5.1 Shadow types | same |
| 5.2 Silhouette generation | changed |
| 5.3 Drawing shadows | changed (blend FILTER, fog) |
| 5.4 Recommended equivalent | changed (amended) |
| 6.1–6.5 Particles: objects, keys, lifecycle, emission, spawn | same |
| 6.6 Particle drawing | same (true camera axes, 2.1) |
| 7.1 Lightning | same |
| 7.2 Lasers, tracers, flares | same |
| 7.3 Shields | changed (through 4.3) |
| 7.4 Explosion light | same |
| 7.5 Clouds, marks, scores, banners | same |
| 7.6 Screen overlays | same |
| 8.1 2D setup | same |
| 8.2 The 2D list | changed (new primitives, half-pixel shift) |
| 8.3 Font | same |
| 8.4 Mouse cursor | not checked (textures same) |
| 9.1 Texture loading | changed |
| 9.2 Registries | same |
| 9.3 Collision projection | same |
| 10 Shader plan | changed (amended) |
| 11.1 Limits | changed |
| 11.2 Open questions | changed (answered for AS2) |
| 11.3 Corrections | see below |
| 11.4 Comparison with engine-behaviour §13 | not checked |
| hmap.md terrain texturing, lighting, tiles, water (this delta's 12) | changed |

## Open questions

1. Whether `DisableDistanceFog` still has a reader (2.2); none found.
2. Presentation interval and `WaitVSync` (1.4).
3. The exact cursor drawing (8.4).
4. Which of the ~10 reachable objects with missing textures actually appear in play (3.2).
5. A running original to confirm the look of the water shoreline fade and the wave amplitude
   (the manual has no screenshot).

## Corrections to other specs

| Spec, place | Says | Correct | Evidence |
|---|---|---|---|
| symbol-map.md, `re/symbols_as2.csv` 0x42ff70 `R_DrawGroundPass` "new pass" | new pass | the TYPE_MARK pass, counterpart of v170@0x40d860 | draws the mark list (count as2@0x2112a5c filled by TYPE_MARK records in as2@0x430740) |
| same, 0x430280 `R_DrawMarks` / 0x4300e0 `R_DrawMark` | mark pass | new skid-mark pass / one skid-mark strip | trail list as2@0x2113074 filled by as2@0x414850 |
| same, 0x414850 `G_UpdatePlayers` | per-player update | skid-mark track update | constants 0.41667, 10, 23 sections, trail pool |
| same, 0x414800 `G_LevelEndHelper` | level-end helper | frees every skid-mark track | walks the track list back to the free list |
| same, 0x430eb0 `R_DrawFadeOverlay` | full-screen fade | brightness quad | DEST_COLOR/SRC_COLOR with the `Brightness` value |
| same, 0x433e60 `R_SetupModelLight` | model-pass helper | environment coordinates | writes the second UV set from normals × world × view |
| same, 0x41bb60 `R_LoadTerrain_Copy` | copy step | water depth weights and water strips | writes the weight array and the per-chunk strips |
| same, 0x438260 `R_ShadowMapHelper` | helper | registers the generated shadow texture | copies the name into the texture registry |
| hmap.md "Water" (for `as2`) | `water tex level alpha` | `water base shine level opacity`, grid water, waves | 12.5; affects levels-txt.md and `engine/src/game/defs.cpp` |
| rcsl-builtins-semantics.delta.md, `WaterHeight` open question 3 | wave term not given | `level + 16·w·0.5·(sin(0.75·row + T) + sin(col + T))` | 12.5, as2@0x41d8d0 |

## Changelog

- 1.0 (B5): first version.
