# Level map (`maps/*.hsc`, magic `HMAP`)

Spec version 1.0. Reference implementation: `tools/ref/hmap.py`, exercised by
`tools/ref/test_hmap.py` against all 24 shipped `maps/*.hsc` files.

All integers are little-endian. Floats are IEEE-754 `f32`, little-endian.
Code addresses refer to `AirStrike3D.exe` v1.70 (image base `0x400000`).

This document covers the file format and what the engine builds from it: the
terrain mesh, its texturing and lighting, water, placed objects, waypoint
paths, spawning, activation and scrolling. Per-level parameters come from
`maps/levels.txt` (syntax in [text-blocks.md](text-blocks.md)); how the engine
reads that file is described here as well, because the terrain cannot be
built without it.

## Corpus overview — VERIFIED-DATA

24 files: `intro`, `intro2`, `intro3`, `intro4`, `level1`, `level3`, `level3_2`,
`level4`, `level4_2`, `level5`, `level8` … `level21`. All start with `HMAP`
and version 2, and all 24 parse to exactly their file length with the layout
below. Each is referenced by exactly one block of `maps/levels.txt`.

| | intro2, intro3 | the other 22 |
|---|---|---|
| grid (W x H cells) | 32 x 128 | 32 x 256 |

Totals: 7991 placements, 1629 type-table names, 217 item-table names,
907 placements with waypoint paths (2562 waypoints), 104 with a per-placement
script, 609 carrying a drop item.

## File layout

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | magic `HMAP` |
| 0x04 | u32 | version, always 2 |
| 0x08 | u32 | `W`: grid width in cells (world x). 32 in every file |
| 0x0C | u32 | `H`: grid height in cells (world y, the scroll direction). 256 or 128 |
| 0x10 | u32 | `N`: number of placement records |
| 0x14 | u32 | `T`: number of names in the object type table |
| 0x18 | u32 | `I`: number of names in the item table |
| 0x1C | … | type table: `T` strings |
| | … | item table: `I` strings |
| | `W*H*4` | grid |
| | … | `N` placement records, variable length |

Nothing follows the last placement record (VERIFIED-DATA, 24/24).

VERIFIED-CODE (loader `0x4091f0`): the 28-byte header is read in one call
(`0x409263`); the magic is compared as the immediate `0x50414d48`
(`0x409280`, error "File header corrupted."), the version against 2
(`0x409295`, "Illegal file version."), and `N` against 0x4000 (`0x4092ab`,
"Too many map objects."). The loader's local tables hold at most 1024 type
names and 256 item slots; nothing checks `T` or `I`.

The names `W`/`H` for the fields at 0x08/0x0C are VERIFIED-CODE: the terrain
builder (`0x416fd0`) is called with (0x08, 0x0C) as (columns, rows), puts
column `c` at world x = 40·c and row `r` at world y = 40·r, and the renderer,
TerrainHeight and the water plane all use x ∈ [0, 1280] = [0, 40·32].

### Strings

Both name tables and the per-placement script path use the same encoding:
`u8 n` followed by `n` bytes, the last of which is NUL (the length includes
the terminator). VERIFIED-DATA: every string in the corpus has exactly one NUL,
at its end. VERIFIED-CODE `0x4092d0`–`0x409301`: one byte is read, then that
many bytes into a 260-byte buffer. VERIFIED-DATA: all strings are printable
ASCII without spaces.

### Grid

`W*H` cells of 4 bytes, row-major: cell (column `c`, row `r`) starts at
`4·(r·W + c)`; row 0 is the start of the level (smallest world y).
VERIFIED-CODE: `0x409412` reads the whole block, then `0x409440` splits it
into a height array (byte 0 of every cell) and a 3-byte-per-cell tile array
(bytes 1..3), both passed to the terrain builder (`0x409807`).
VERIFIED-DATA (pictures from `hmap.py png`, checked by eye for level1,
level18, level21 and intro2): byte 0 with this stride shows continuous
terrain (hills, river beds, lakes).

| Byte | Name | Range in the corpus | Meaning |
|---|---|---|---|
| 0 | height | 0..255 | terrain height sample, see [Terrain geometry](#terrain-geometry) |
| 1 | tile set | 0..5 | 0 = no overlay; `n` = draw a tile from `tiles\tiles<n>.tga` over this cell |
| 2 | tile index | 0..54 | tile number inside the atlas |
| 3 | tile rotation | 0, 3, 6, 9 (and 49, 3 cells) | overlay rotation, see [Tile overlays](#tile-overlays) |

VERIFIED-DATA: when byte 1 is 0, bytes 2 and 3 are 0 too (all 24 files). There
are no other per-cell layers: no texture weights, no lightmap and no
passability data are stored; the terrain texture is derived from the height
(see [Terrain texturing](#terrain-texturing)).

### Placement record

| Offset | Size | Field |
|---|---|---|
| +0 | u16 | type index, 0-based into the type table |
| +2 | u16 | `x`: cell column, 0..W-1 |
| +4 | u16 | `y`: **1-based** cell row: the object stands in row `y-1` |
| +6 | u8 | rotation, in steps of 30° (0..11) |
| +7 | u8 | drop item: 0 = none, else 1-based index into the item table |
| +8 | u16 | `K`: number of waypoints |
| +10 | u8 | `S`: length of the script path including its NUL, 0 = no script |
| +11 | `S` | script path, e.g. `scripts\helics\helic4\bot3.scr` |
| +11+S | u16 | path flag: nonzero = closed (looping) path. Only meaningful when `K > 0` |
| +13+S | `32*K` | waypoints |

VERIFIED-CODE (`0x4094cb`–`0x409574`): reads 10 bytes, 1 byte, the string when
that byte is nonzero, 2 bytes, then `K·32` bytes when `K > 0`. The record is
stored into a 31-byte in-memory slot (array at `0x4d5300`); the type index is
replaced by the resolved object definition, the item index by the resolved
item definition (0 stays 0), the script path by a loaded-script handle
(`0x41c980`; -1 when absent).

Value ranges, VERIFIED-DATA: `x` 0..31, `y` 2..256, rotation 0..11, all type and
item indices in range, every script file exists under `scripts/`.

The path flag is only consulted when `K > 0` (VERIFIED-CODE `0x409604`: it
becomes a boolean in the path structure, and only paths are allocated). In
the data it is 0 (895 paths) or 0xFFFF (12 paths) when `K > 0`, and assorted
leftover values (0, 0xFFFF, 0x2466, 0x3Exx) when `K = 0` (VERIFIED-DATA).
Readers must ignore it when there are no waypoints.

#### Why `y` is 1-based — VERIFIED-CODE + VERIFIED-DATA

- The spawner (`0x4075c5`–`0x4075f5`) computes the spawn position as
  `(40·x + 20, 40·y + 20 − 40, 0)`, i.e. the centre of cell (x, y−1).
- Waypoints are converted without the −1 (`0x409646`: `40·v + 20`). In all 907
  paths the first waypoint is exactly cell (x, y−1) of its placement.
- In the 17 levels that end on a 3x3 helipad drawn with `tiles5.tga`, the
  `end_of_the_level` marker sits on the pad's centre cell (x, y−1).

#### Waypoint (32 bytes)

| Offset | Type | Field |
|---|---|---|
| +0 | i32 | x, cell column (observed 0..34; a path may leave the map sideways) |
| +4 | i32 | y, cell row, **0-based** (observed 0..241) |
| +8 | i32 | unknown: 1 (2137), 0 (420) or 2 (5). No v1.70 function examined reads it — GUESS: editor leftover (segment type or speed class) |
| +12 | f32, f32 | incoming Bézier control point (x, y), in cell units |
| +20 | f32, f32 | outgoing Bézier control point (x, y), in cell units |
| +28 | f32 | delay, returned by the script builtin `GetWaypointDelay` |

VERIFIED-CODE (`0x409638`–`0x409735`): after reading, every waypoint's x, y
(integers) and the four control coordinates (floats) are converted to world
units with `v·40 + 20` (constants 40.0 at `0x44b7e8`, 20.0 at `0x44bc30`);
+8 and +28 are left as read. Observed delays: 0 (1928), 100 (473), 50 (87),
and small values 1..60.

## Type and item tables, name resolution

Each name is looked up among the object definitions from `objects/*.obj`
(`0x409860`): exact, case-sensitive comparison, first match wins, 1-based
definition number; an unknown name logs `ERROR: Unknown object '%s'.` and
resolves to 0 (VERIFIED-CODE). VERIFIED-DATA: all 1629 type names and 217 item
names of the corpus resolve; `test_hmap.py` keeps an (empty) exception list.
Every type-table entry is used by at least one placement.

The **type table** is the palette of objects placed on the map; a
placement's type index selects from it.

The **item table** lists the power-ups (`item_*` definitions in
`objects/items.obj`, e.g. `item_star`, `item_armor50`, `item_missile_heat`)
that placed objects may **drop when destroyed**. VERIFIED-CODE: the spawner
copies the resolved item definition into the new entity (`0x4077b9`,
entity +0x22), and the damage handler (`0x404ae0`, at `0x404c59`–`0x404c73`) creates
an entity of that definition when the entity's health reaches 0, then clears
the field. Items that simply lie on the map are ordinary placements whose type
is an `item_*` definition (217 of them in the corpus).

The loader counts the placements whose drop item is `item_star` (exact
10-byte compare with the string at `0x4474cc`) into the global `0x4d52d0`
(VERIFIED-CODE `0x409383`, `0x4095c7`). GUESS: the "stars found / total"
figure of the mission results. 144 placements carry a star.

## Terrain geometry

### World frame — VERIFIED-CODE

- x grows to the right across the map, y grows along the scroll direction
  (the level starts at y = 0), z is up. One cell is 40 world units
  (`0x416fd0` stores 0x28 in the cell-size global `0x1f5a2c8`).
- A W x H map is 40·W x 40·H units: 1280 x 10240 for the 256-row maps.
- On screen +x is right and +y is up (see [Camera](#camera)).

### Vertex grid — VERIFIED-CODE `0x416fd0`, `0x4185b0`

The mesh has (W+1) x (H+1) vertices; vertex (c, r) is at
`x = 40·c`, `y = 40·r`. The cell heights are **not** used directly as vertex
heights: the builder first resamples the W x H height bytes to a
(W+1) x (H+1) byte grid with the engine's resampler (below), then

    z = (hmax − hmin) · (raw / 255) + hmin

with `hmin`, `hmax` from the level's `levels.txt` block (VERIFIED-CODE: the
level record's +0x114 and +0x118, filled from `hmin`/`hmax` by `0x406340`).

**Resampler** (`0x4185b0`, also used for the map textures). Input `sw x sh`
bytes, output `dw x dh` bytes:

- 16.16 fixed-point steps `stepx = ((sw−1)·65536) div (dw−1)` and
  `stepy = ((sh−1)·65536) div (dh−1)` (integer division, truncating).
  Output sample (i, j) reads the source at `(i·stepx, j·stepy)` accumulated
  step by step.
- Integer part `ix`, `iy`; fractions `fx = (accx & 0xFFFF) / 65536`, same for
  `fy`.
- Neighbours a = src(ix, iy), b = src(ix+1, iy), c = src(ix, iy+1),
  d = src(ix+1, iy+1), clamped at the right/bottom edge (b := a in the last
  column, c := a and d := b in the last row, d := c in the last column).
- `top = trunc(a·(1−fx) + b·fx)`, `bottom = trunc(c·(1−fx) + d·fx)`,
  `result = trunc(top·(1−fy) + bottom·fy)`. All three conversions truncate
  (the x87 control word is switched to round-toward-zero around each store).
  Computing this in IEEE doubles gives the same bytes, because every
  intermediate is exact.

For the terrain, vertex (c, r) therefore samples the cell grid at
`(c·31/32, r·255/256)` for the 256-row maps: the terrain is stretched by one
cell's worth over the map. `hmap.vertex_heights()` implements this exactly.

### Triangulation — VERIFIED-CODE `0x416fd0`, `0x415730`

Each cell (c, r) with corners v00 = (c, r), v10 = (c+1, r), v11 = (c+1, r+1),
v01 = (c, r+1) is split along the diagonal v00–v11 into triangles
(v00, v10, v11) and (v00, v11, v01). The renderer draws it as triangle strips,
one per cell row, alternating v(c, r+1), v(c, r) for c = 0..W; this gives
counter-clockwise triangles seen from above.

### Chunks — VERIFIED-CODE `0x416fd0`, `0x416ba0`, `0x415730`

Rows are grouped in chunks of 8 (H/8 chunks: 32 or 16). Each chunk owns
8 strips of 2·(W+1) indices, the list of tile overlays of its cells, and the
index `chunk / 4` of its generated map texture. A binary tree over the chunks
(`0x416ba0`) with bounding boxes x ∈ [0, 1280], y ∈ chunk range,
z ∈ [hmin, hmax] is used for frustum culling (`0x4199c0`). Chunking is a
rendering optimisation; a reimplementation may cull differently.

### Normals — VERIFIED-CODE `0x416fd0`

1. Two face normals per cell, the normalised cross products
   `(v10 − v00) × (v11 − v00)` and `(v11 − v00) × (v01 − v00)` (they point up).
2. Each vertex gathers the face normals of the up to six triangles touching
   it: both triangles of cell (c, r), the first triangle of cell (c−1, r),
   both triangles of cell (c−1, r−1) and the second triangle of cell (c, r−1),
   where those cells exist.
3. The combination is unusual: **per component**, the engine takes `acos` of
   each face normal's component, averages these angles, takes `cos` of the
   average, and finally normalises the vector (CRT helpers identified as
   `acos` = `0x440d30`, `cos` = `0x440b70`, `sqrt` = `0x440a50`). A plain
   average of the face normals looks nearly the same; the exact rule is in
   `hmap.vertex_normals()`.

### Static lighting (vertex colours) — VERIFIED-CODE `0x416fd0`

From the level's `sun` statement, `sun R G B  dx dy dz  aR aG aB`:
sun colour, direction towards the light (normalised by the engine), ambient.
For each vertex with normal N:

- `d = N · normalize(dx, dy, dz)`; if `d > 1` then `d = 1`.
- Underwater factor `f = 1`, except when the level has water and the vertex
  z is below the water level: `f = (z − hmin) / (water_level − hmin)`.
- If `d > 0`: channel `= trunc(min(255, (sun_k · d + ambient_k) · 255 · f))`.
- If `d ≤ 0`: channel `= trunc(ambient_k)` — **without** the ×255, so
  effectively 0 (black) for every shipped ambient value (all are < 1). This
  is a bug in the original; slopes facing away from the sun render black
  there. A reimplementation should reproduce it only if faithfulness matters
  more than looks.

The colours are stored as unsigned bytes and used as the vertex colour of
both the terrain and the tile overlays (`glColorPointer`, `GL_UNSIGNED_BYTE`).
A pristine copy is kept (`0x1f5a2dc`): each frame the renderer adds the
contribution of dynamic point lights (script builtin `PlaceLight`) to the
vertices of visible chunks (`0x415730`), then clamps to 255. The per-light
term involves `N·L·0.7 + 0.3`, the light radius and an optional cone; its
exact falloff was not analysed (GUESS level; belongs to a lighting spec). The
`night` flag does not change the terrain lighting; it only sets the script
global `l_night` (VERIFIED-CODE `0x407080`).

### TerrainHeight — VERIFIED-CODE `0x416470`

Used by the script builtin `TerrainHeight(x, y)` (`0x41baf0`) and by the
engine to put objects on the ground. Bilinear interpolation of the **vertex**
grid z: `ix = floor(x/40)`, `iy = floor(y/40)`, `fx`, `fy` the fractions;
`h0 = z(ix, iy) + fx·(z(ix+1, iy) − z(ix, iy))`,
`h1 = z(ix, iy+1) + fx·(z(ix+1, iy+1) − z(ix, iy+1))`,
result `h0 + fy·(h1 − h0)`. It returns 0 when x < 0, y < 0, x > 40·(W+1) or
y > 40·(H+1). Note the bounds are one cell too generous: for x in
(40·W, 40·(W+1)] or y in (40·H, 40·(H+1)] the original reads past the row
or the array. A reimplementation should clamp to the map instead.

The result is the bilinear surface, not the rendered triangle surface, so
objects can float or sink by a fraction of a unit on steep cells.

## Terrain texturing

### Height-banded base texture ("mapTexture") — VERIFIED-CODE `0x4167f0`

`textures "textures\desert"` names a directory holding `texture1.tga` ..
`texture4.tga` (all 256x256) and `detail.tga`. At load time the engine
generates one 256x256 RGB texture per 32 rows of the map, named
`mapTexture0`, `mapTexture1`, … (H/32 of them):

1. Resample the W x 32 height bytes of rows `32·i .. 32·i+31` to 256x256 with
   the resampler above (so a map texture spans source rows 32·i to 32·i+31
   while being displayed over 32 rows: another one-row stretch).
2. For each texel: `t = h / 86.0`; `lo = floor(t)`, `hi = ceil(t)`,
   `w = t − lo`; colour = `trunc(texture[lo+1] · (1 − w) + texture[hi+1] · w)`
   per channel, reading the **same texel position** (u, v) from both source
   textures.

So texture1 is the lowest ground, texture2 is reached at raw height 86,
texture3 at 172, and texture4 only contributes above 172 and never fully
(t ≤ 2.97). This height blend is the whole "terrain type" mechanism: there
are no stored texture indices or blend weights, and transitions follow the
contour lines. The source textures map 1:1 onto a 32x32-cell block
(8 texels per cell).

GUESS: which image row is texel row 0 depends on how the original TGA loader
(`0x4187e0`) stores rows; the textures are isotropic, so this does not matter
visually.

### Texture coordinates and multitexturing — VERIFIED-CODE `0x416fd0`, `0x412720`, `0x415730`

- Unit 0: the chunk's map texture, `(u, v) = (c/32, r/32)` for vertex (c, r),
  i.e. one map texture across the full width and 32 rows. Rows beyond 32 rely
  on wrapping; `RegisterTexture` (`0x418a20`) never sets a wrap mode, so the
  GL default `GL_REPEAT` applies. Vertex colours modulate it (default
  `GL_MODULATE`; smooth shading is enabled for the terrain pass).
- Unit 1 (when multitexture is available): `detail.tga` with
  `(u, v) = (c·0.25, r·0.25)`: one repeat per 4x4 cells (160 world units).
  With `GL_ARB_texture_env_combine` the unit uses `GL_COMBINE` with
  `GL_ADD_SIGNED` for RGB; otherwise `GL_MODULATE`.
- Without multitexture only unit 0 is drawn.

### Tile overlays — VERIFIED-CODE `0x416d00`, `0x415730`

Cells with a nonzero tile set get an extra textured quad (roads, helipads,
concrete) drawn after the terrain of their chunk, on the same four vertices
(so it follows the terrain), with the same vertex colours, as
`GL_TRIANGLES` (v00, v11, v01) and (v00, v10, v11), blended with
`GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA`, depth writes off and
`glPolygonOffset(−1, −1)`. Within a chunk the quads are drawn sorted by
texture.

Atlas `tiles\tiles<set>.tga` is split into 64x64 tiles (the shipped atlases
are 256x64, 256x128, 128x64, 256x64 and 256x256). With
`perRow = width/64`, `perCol = height/64`, `du = 1/perRow`, `dv = 1/perCol`:

    u0 = du · (index mod perRow)          u1 = u0 + du
    v0 = 1 − dv − dv · (index div perRow)  v1 = v0 + dv

(in GL texture space, t = 1 at the top of the image as displayed). Tile 0 is
the top-left tile, numbering row by row. The code also subtracts an integer
`2 div width` and adds `1 div width`, which are 0 for every real atlas.

Corner assignment by the rotation byte, with A = (u0, v0), B = (u1, v0),
C = (u1, v1), D = (u0, v1):

| byte 3 | v00 | v10 | v11 | v01 | on screen |
|---|---|---|---|---|---|
| 0 (and any other value) | A | B | C | D | upright |
| 3 | D | A | B | C | rotated 90° counter-clockwise |
| 6 | C | D | A | B | 180° |
| 9 | B | C | D | A | 90° clockwise |

VERIFIED-DATA (pictures): with this table the road tiles of every level join
into continuous roads with matching T-junctions and corners.

Odd cells (VERIFIED-DATA, listed in `test_hmap.py`): 6 cells in `level12` and
`level18` use tiles2 indices 32 and 54 (the atlas has 8 tiles) and one of
them rotation 49. The engine does not validate them: rotation 49 falls into
the default case, and indices past the atlas produce v coordinates outside
[0, 1] that wrap (`GL_REPEAT`), which amounts to
`(index mod perRow, (index div perRow) mod perCol)`.

## Water — VERIFIED-CODE `0x406340`, `0x416fd0`, `0x4160d0`, `0x407080`

`water "gfx\water\water_lake1.tga" -68 0.4` = texture, **water level**
(world z) and **opacity** (alpha). Stored at level record +0x140, +0x180,
+0x184. The texture name has its extension replaced by `.tga` before loading.
A level has water when the texture name is not empty (`level13` has no
`water` statement).

- Drawn after the opaque scene: for every terrain chunk that passed culling,
  one quad from (0, 320·k) to (1280, 320·k + 320) at `z = level + sin(t)`,
  texture coordinates (0, 0)–(8, 2) (one repeat per 160 world units), normal
  (0, 0, 1), colour `sun · sun_dir.z + ambient` (sun direction normalised),
  alpha = the opacity argument, blend `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA`.
- Animation: `t += frametime · π · 0.1` (a 20 s period). The texture matrix
  is translated by `(0.4 · sin t, 0)` and `(0, 0.4 · sin(t/2))`; the plane
  bobs by ±1 unit through `sin t`.
- Terrain vertices below the water level are darkened by the underwater factor
  described under [Static lighting](#static-lighting-vertex-colours).
- Script globals: `l_water` = 1 when the level has water, `l_waterlevel` =
  the water level (set at level start, `0x407080`).
- Objects flagged `FL_ONWATER` (flag bit 4) have their z set to the water
  level on every update (`0x4057c0`, `0x40a080`), ignoring the bobbing. In the
  corpus 59 path placements are `FL_ONWATER` boats; their paths stay on the
  lakes (checked on the pictures).

## Fog, clear colour, far plane — VERIFIED-CODE `0x406340`, `0x407080`, `0x40d2a0`, `0x40e0d0`

`fog r g b start end`: linear fog (`GL_LINEAR`) with that colour and range,
also used as the clear colour. When a level has no fog statement fog is
disabled (all 24 blocks have one). Two configuration flags (`0x1fa9240`,
`0x1fa923c`) force fog off. Far clip plane: `max(end, 1000)` with fog, 2000
without; near plane 4.

## Objects: spawning and activation

### Level start — VERIFIED-CODE `0x407080`

Load the map (`0x4091f0`), set fog, then for every placement: precompute its
path (`0x4069a0`) and register its model, skin and shadow. Then the camera is
reset (`0x40c660`), the player(s) are created (`0x40b2f0`), the spawn cursor
is set to the first placement, and the script globals `l_night`, `l_water`,
`l_waterlevel` are set.

### Spawn order — VERIFIED-CODE `0x40975d`

After loading, the placement array is bubble-sorted by `y`, swapping only
when strictly greater, i.e. a **stable** sort by `y`. The file itself is not
sorted (VERIFIED-DATA: none of the 24 files is in `y` order).

### Spawning — VERIFIED-CODE `0x407590`, called every frame from `0x408df0`

Using the spawn window `near = g_map_pos − 64`, `far = g_map_pos + 1000`
(set every frame by `0x40c260`), the spawner walks the sorted placements from
the cursor:

- stop (for this frame) at the first placement with `y > far / 40`;
- skip placements with `y < near / 40` for good (never spawned);
- otherwise create the entity (`0x40a080`) at `(40·x + 20, 40·y − 20, 0)`,
  i.e. about 980 units ahead of `g_map_pos`, and advance the cursor.

On creation:

- yaw = rotation · 30° (entity +0xbb);
- if the placement has a path: the entity gets it, its yaw becomes the path's
  initial heading, and its current delay the first waypoint's delay;
- if the placement has a script, a new script instance of it replaces the
  object's default script (`0x41c7d0`);
- the drop item is attached (see above);
- z: 0 as given, then immediately replaced by `TerrainHeight(x, y)` for
  `FL_ONGROUND` (bit 1; `FL_ONGROUND_NORMAL` = bits 1|2) or by the water level
  for `FL_ONWATER` (bit 4). Other objects (aircraft) start at z = 0 and are
  lifted by their scripts. VERIFIED-CODE flag values from the `.obj` parser
  `0x40a160`; the snap in `0x40a080` and, every update, in `0x4057c0`.
- `FL_ONGROUND_NORMAL` objects are also tilted to the terrain: the engine
  samples TerrainHeight at (x−10, y+15), (x+10, y+15) and (x, y−25) relative
  to the object and builds its axes from that plane and the yaw (`0x4165f0`).
- The entity starts **dormant** (state 1), except `FL_TEMPORARY` (0x20)
  objects which start active (state 0) (`0x4077bc`–`0x4077d9`).
- Its health values (+0x6b, +0x103) are multiplied by the script global
  `g_health_factor` (`0x4577c4`). This is the only difficulty handling on
  this path: placements carry no difficulty, group or delay fields.

### Activation and removal — VERIFIED-CODE `0x405cc0`, `0x4050b0`

Every frame each entity is tested against the activity zone, with `rad` its
bounding radius (entity +0x1bf):

    y − rad ≥ g_map_pos + 16   and   y − rad ≤ g_map_pos + 800
    x + rad ≥ 0                and   x − rad ≤ 1280
    z + rad ≥ hmin

- dormant (1) and inside the zone → active (0);
- active and outside the zone, unless `FL_TEMPORARY` → leaving (2);
- leaving: removed as soon as it is no longer visible (`0x419970`) or a
  second test (`0x405140`, not analysed) fails.

The state changes go through `0x405c70` (GUESS: it also delivers the script
callbacks for activation; not analysed). In practice objects appear ~1000
units ahead, start acting once within 800 units, and are removed after they
fall behind the camera.

## Waypoint paths — VERIFIED-CODE `0x4069a0`, `0x405dd0`, `0x405fe0`, `0x406110`, `0x41bab0`

Paths live only in the `.hsc` (no other file holds them); each placement owns
its own list. A path of K ≥ 2 waypoints is a chain of cubic Bézier segments:
segment i goes from waypoint i to waypoint i+1 (wrapping to 0 when the path
loops) with control points `P0 = wp[i]`, `P1 = wp[i].out_ctrl`,
`P2 = wp[i+1].in_ctrl`, `P3 = wp[i+1]` (all in world units after the load-time
conversion). An open path has K−1 segments, a looping path K.

Preprocessing (`0x4069a0`, paths with K < 2 are skipped) walks each segment
in parameter steps of 0.001 and records a sample every 8 world units of arc
length: the curve parameter (segment index + local t), the heading in degrees
at that point, and the heading change to the next sample. Movement then works
in arc length:

- `MoveToNextWP(turn)` / `lMoveToNextWP`: `s += speed · frametime` (entity
  speed at +0xd7); position = the curve at the parameter interpolated from the
  sample table at `s / 8`; with `turn` nonzero the yaw follows the curve. It
  returns 1 when a waypoint is passed (the entity's current delay becomes that
  waypoint's delay) or when an open path ends (a "finished" flag is set and
  the last waypoint's delay is taken), 0 otherwise. A looping path restarts
  at `s = 0`.
- `RotateToNextWP()`: turns the yaw towards the path heading with the
  entity's turn rate (+0xdb) · frametime.
- `GetWaypointDelay(i)`: delay of waypoint `i mod K`.

The path heading replaces the placement's rotation byte at spawn.
Observed use (VERIFIED-DATA): 665 ground vehicles, 59 boats, 183 aircraft or
flag-less objects carry paths.

## Scrolling, g_map_pos and camera

### g_map_pos and the scroll speed — VERIFIED-CODE `0x40c660`, `0x40c260`

`g_map_pos` (global `0x4d52d8`, exported to scripts) is the world y of the
scroll position. It starts at **32.0** and each frame (outside the
intermission mode) grows by `frametime · 42 · speed_factor`. The speed factor
is camera field 9 (`0x1ebe618`), reset to 1.0 at level start and otherwise
changed only by scripts through the `camera` global. VERIFIED-DATA (scripts,
disassembled with `tools/rcsl_disasm.py`): `eol.scr` (the script of the
`end_of_the_level` object), `boss1.scr`, `boss.scr` (boss2), `boss3.scr` and
`player/p_speeddown.scr` write `camera[9]`; nothing else does. The engine
itself never stops or clamps the scroll. At full speed the end marker of a
256-row level (around y = 9600) comes within 320 units of `g_map_pos` after
about (9600 − 320 − 32) / 42 ≈ 220 s.

### Level end — VERIFIED-DATA + VERIFIED-CODE

The map format has no end or start fields. The end is an ordinary placement:
`end_of_the_level` (`objects/misc.obj`: `FL_ONGROUND FL_NODRAW FL_TEMPORARY`,
script `scripts\eol.scr`), placed on a helipad in 17 levels (level3_2 has a second
marker at y = 256, off the pad). Its script ramps `camera[9]` down to 0 once the marker is within
320 units of `g_map_pos`, steers the player onto it, lands it and calls the
builtin `EndLevel` (`0x407570`). The three boss levels (level5, level15,
level21) have no marker; there the boss scripts stop the scroll and end the
level. A `helic_area_eol` object also stands on the final helipad in some
levels (GUESS: the landing-zone visual).

### Player start — VERIFIED-CODE `0x40b2f0`

The player is not in the map (VERIFIED-DATA: no `p_*` player definition is
placed). It is created from a hard-coded name table (`p_apache` … by
helicopter choice) at the world origin (`0x1fdb32c`, a never-written zero
vector); its script positions it relative to `g_map_pos`. In two-player mode
the players are offset by ∓100 in x.

### Camera — VERIFIED-CODE `0x40c660`, `0x40c260`, `0x40e0d0`

The config value `Camera` selects one of four presets (clamped to 0..3,
table at `0x457398`, 4 floats each):

| Camera | fov (°) | pitch (°) | height z | y offset |
|---|---|---|---|---|
| 0 | 60 | −35 | 270 | 0 |
| 1 (default) | 60 | −45 | 270 | 0 |
| 2 | 60 | −50 | 270 | 0 |
| 3 | 70 | −15 | 370 | 100 |

- Position: `x` starts at 640 (map centre) and follows the player with a
  ±48 unit dead zone, clamped to [578, 702]; `y = g_map_pos + y offset`;
  `z = height`.
- Projection: `gluPerspective(fov, width/height, 4, far)` (far as above).
- View: `glRotatef(pitch, 1, 0, 0)`, `glRotatef(a, 0, 1, 0)`,
  `glRotatef(b, 0, 0, 1)`, `glTranslatef(−x, −y, −z)`. With `a = b = 0`
  the camera looks along +y and down, tilted `|pitch|` degrees away from
  vertical (−45: the view centre hits the ground about 270 units ahead of
  `g_map_pos`); screen up is +y and screen right is +x.
- `CameraQuake` (`0x40c150`) shakes `a`, `b` and the fov for a while.

### Intro maps — VERIFIED-CODE `0x408cc0`, `0x40c260`

`intro.hsc` … `intro4.hsc` are the animated **main-menu backgrounds**. At
start-up the game picks one of the level-list ids `intro1` … `intro4` at
random (`0x408cc0`, pointer table `0x457820`) and loads it like a level.
Their `levels.txt` blocks carry `intermission x y z pitch a b` instead of a
name; in intermission mode the camera is fixed at (x, y, z) with fov 60,
`pitch + 0.5·sin(0.5·time)`, `a`, and `b + 1.4·sin(0.75·time)`, and
`g_map_pos` does not advance. As the spawn window stays at
`g_map_pos + 1000 = 1032`, only placements with y ≤ 25 ever spawn; 3
placements of `intro` and 4 of `intro4` lie beyond and never appear.
The intro maps hold menu variants of vehicles (`*_menu` objects) driving
looping paths near the start of the map.

## levels.txt as read by the engine — VERIFIED-CODE `0x406880`, `0x406340`

Up to 32 blocks ("Too many levels in level-list file."), each into a
0x1C0-byte record. Keys are compared case-sensitively. Relevant fields:

| Key | Record offset | Use |
|---|---|---|
| `id` | +0x000 | lookup key (`0x4062c0`) |
| `name` | +0x010 | display name |
| `map` | +0x050 | path of the `.hsc` |
| `music` | +0x090 | music file |
| `textures` | +0x0D0 | directory of texture1..4 and detail |
| `enableHelic` | +0x110 | int, default −1 |
| `hmin`, `hmax` | +0x114, +0x118 | height range |
| `sun` | +0x11C..+0x13C | colour, direction, ambient (9 floats) |
| `water` | +0x140, +0x180, +0x184 | texture, level, opacity |
| `night` | +0x188 | flag |
| `fog` | +0x189, +0x18C..+0x194, +0x19C, +0x1A0 | flag, colour, start, end |
| `intermission` | +0x1A4, +0x1A8..+0x1BC | flag, 6 floats |

## Reference implementation

`tools/ref/hmap.py` parses a file into `HmapFile` (header, names, the raw
grid, placements with waypoints) and raises `HmapError` unless every byte is
accounted for. It also implements the resampler, vertex heights, normals and
static lighting as specified, a Bézier path sampler, and the commands
`info`, `json`, `objects` and `png` (pictures: height, tile layers, a lit and
textured preview with tiles and water, and a placement map with paths).

`tools/ref/test_hmap.py` checks the whole corpus (see its docstring) and
compares against `testdata/golden/as3d/hmap_summary.json`.

## Open questions

- Waypoint field +8 (values 0/1/2) is never read by the functions examined
  (GUESS: unused). A search of the remaining entity code could settle it.
- The state-change function `0x405c70` and the visibility tests `0x419970`,
  `0x405140` were not analysed; whether scripts receive activation callbacks
  is open.
- Row order of TGA pixel data in the original loader (affects only the
  orientation of the terrain textures, which are isotropic).
- The purpose of the star counter `0x4d52d0` (readers at `0x40bd10`,
  `0x426360`, `0x4270a0` not analysed).
- The random choice of the intro map (distribution) was not decoded.

## Changelog

- 1.0 (WP-15): first version. Layout verified on all 24 files; loader,
  terrain builder, renderer, water, spawner, activation, camera and waypoint
  code read from the executable.
