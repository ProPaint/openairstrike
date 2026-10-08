# Nintendo 3DS port

Working notes of the port (plan: M1.0 onwards). The feasibility spike is in
[3ds-spike.md](3ds-spike.md); its app is [apps/3ds](../apps/3ds/README.md).

## Build (M1.1)

    tools/3ds_build.sh [all|clean|netload <ip>]    # -> build-3ds/apps/3ds/openairstrike.3dsx
    tools/3ds_ci.sh                                # compile-only check for agents

Docker image `devkitpro/devkitarm` (pinned by digest in the script), 3ds-cmake `3DS.cmake`
toolchain. `AS3D_BUILD_JOBS=N` limits the parallelism. The root `CMakeLists.txt` adds only
`apps/3ds` when `NINTENDO_3DS` is set (no desktop app, tests, viewer, web or tools: they need
SDL, GL or Python). `engine/CMakeLists.txt` builds core, vfs, formats, script, game, audio, input,
ui, render and platform with the pica/ndsp/ctr backend defaults; a module with no files in its
backend directory yet builds its neutral part only. `input_mapper.cpp` (SDL scancodes) is
removed from `as3d_input` on the 3DS; audio uses `AS3D_WITH_OPENMPT=OFF` and the null device.
The 3DS build uses gnu++17 (newlib needs `_GNU_SOURCE` for `fseeko`). `run.ini` `mode=spike|probe`
works as before.

## M1.2 Platform backend and main loop

`engine/src/platform/ctr/` is the 3DS platform backend: `graphics_context_ctr.cpp` (gfx, 3D on,
the bottom screen format fix, citro3d, targets top left/right 400x240 RGBA8 + D24S8 and bottom
320x240 RGBA8; the private header `ctr_context.h` gives `ctrContext()` with `topTarget(eye)`,
`bottomTarget()`, `beginFrame()/endFrame()`, `sliderValue()`, `eyeCount()`), `rw_stream_ctr.cpp`
(stdio streams) and `user_data_ctr.cpp` (`sdmc:/3ds/openairstrike/`). `as3d::setLogSink`
(core.h) lets the app route the engine log to `log.txt`. The loop, run.ini keys and outputs are in
[apps/3ds/README.md](../apps/3ds/README.md). The loop drives `GameSession` directly: `GameStack`
and `GameFlow` link `GameView`, which needs the PICA render backend (M1.3, M1.8).

## M1.0 GPU probe

What the PICA200 costs for the choices the renderer has to make: where textures live, what
format they have, how many targets a frame fills, how much geometry and blending it takes.
The probe is `apps/3ds/source/probe.cpp`; it draws the spike's scene (mission 1 terrain and the
Comanche, the same scripted pilot from frame 0 in a freshly loaded mission for every variant, so
every variant draws the identical frames) in eight variants. Each variant draws 10 warm-up frames,
then 120 measured ones, with `C3D_FRAME_SYNCDRAW` (so the frame period is quantised to 16.7 ms
steps at 60 Hz).

### Variants

| | texture memory and format | targets | scene | what it isolates |
|---|---|---|---|---|
| A | RGBA8, linear memory (`C3D_TexInit`) | top left | terrain once | baseline, as the spike |
| B | RGBA8, VRAM (`C3D_TexInitVRAM`) | top left | terrain once | texture fetch from VRAM instead of linear memory |
| C | RGB565, VRAM (16-bit texels, same 8x8 Morton order) | top left | terrain once | half the texel bytes |
| D | as C | top left and right (`gfxSet3D(true)`, `Mtx_PerspStereoTilt`, iod 0) | terrain once per eye | pure cost of two eyes |
| E | as C | as D | terrain 3 times per eye (about 30k vertices per eye) | vertex rate |
| F | as C | top left | terrain once + 4 full-screen alpha-blended textured quads | fill rate and blend cost |
| G | RGB565, VRAM, CPU-generated mip chain, trilinear (`GPU_LINEAR` mip filter) | top left | terrain once | cost of mipmapping; the 256x256 texture has 6 levels |
| H | as C | top left and bottom (320x240, one textured quad) | terrain once | the three-target frame |

Logged per variant: average, 95th percentile and (in the file) maximum of `C3D_GetDrawingTime`
(GPU), `C3D_GetProcessingTime` (CPU of the frame) and the frame period from `svcGetSystemTick`;
the world step time; `vramSpaceFree` and `linearSpaceFree` after the variant's textures and
targets exist; the vertices submitted per frame. Logged once: `APT_CheckNew3DS`,
`vramSpaceFree` before `gfxInit`, and the step time of 60 headless frames before and after
`osSetSpeedupEnable(true)` (applied only on a New 3DS).

Notes for reading the numbers:

- "verts" is the number of indices submitted per frame (all eyes and targets). Variant E submits
  the terrain three times, but with the standard `GPU_GREATER` depth test the repeated passes
  are rejected by the depth test. That is the point: the vertex rate is measured, the fill is not.
- The right eye is cleared to a sky colour slightly different from the left eye's, so a capture
  can tell the two targets apart.
- RGB565 has no alpha. The terrain and the helicopter skin are opaque, so nothing changes.
- The mip chain is a 2x2 box filter of the nearest-resampled 256x256 level, written level by
  level into the VRAM texture by the CPU.
- Frame period near 16.7 ms means the variant makes 60 fps; the GPU and CPU times show the
  headroom. A frame period of 33.3 ms means a missed vblank.

### How to run (hardware)

1. Put the paks in `sdmc:/3ds/openairstrike/as3d/` (see the app README).
2. Upload `run.ini` with ftpd to `sdmc:/3ds/openairstrike/run.ini`, containing:

        mode=probe

   (`mode=spike`, or deleting the file, gives the spike again. Optional keys: `hold=N` keeps
   drawing N frames after each variant; `only=CH` runs just those variants.)
3. Start the Homebrew Launcher, press Y, and from this machine `tools/3ds_build.sh netload
   192.168.1.133` (builds, then 3dslink). Or copy `openairstrike.3dsx` to `sdmc:/3ds/` and start it.
4. The probe runs unattended (about 15 s), prints the table on the bottom screen and appends it
   to `sdmc:/3ds/openairstrike/probe_log.txt`. START quits. Fetch the log with ftpd.

### Azahar correctness run (2026-10-07)

Azahar is not a timing reference: its GPU times are constants (0.25 ms per eye) and the frame
period is vsync. Only what was drawn was checked, with one screenshot per variant (they show
game art and are not committed: `out/3ds/probe/mono/variant_A.png` to `variant_H.png`,
`out/3ds/probe/stereo/variant_D.png` and `variant_E.png` with Azahar's side-by-side 3D mode).

| variant | Azahar result |
|---|---|
| A | terrain and helicopter drawn, as the spike |
| B | identical image to A (RGBA8 in VRAM) |
| C | RGB565: colours right, no visible banding on the sand texture |
| D | both eyes drawn (side by side, identical content at iod 0) |
| E | both eyes drawn, three terrain passes, 57k indices per frame |
| F | four blended quads washed over the scene (terrain and helicopter textures alternate) |
| G | mip levels in effect: smoother distant terrain, no shimmer |
| H | top screen as C, bottom screen shows the textured quad |

Two findings that matter for the port, both found by the H variant: `consoleInit(GFX_BOTTOM)`
leaves the bottom screen in RGB565 and single-buffered, while the citro3d display transfer
writes RGB8. A bottom render target needs `gfxSetScreenFormat(GFX_BOTTOM, GSP_BGR8_OES)` and
`gfxSetDoubleBuffering(GFX_BOTTOM, true)` first, or the screen shows noise. And
`vramSpaceFree()` reads 0 until the VRAM heap has served one allocation, so the probe allocates
and frees a few bytes before reading it.

VRAM budget seen (6.00 MB at start): the top target takes 0.73 MB, the second top target
0.75 MB, the bottom target with a 16-bit depth buffer 0.44 MB; the two scene textures take 384 KB
as RGBA8, 192 KB as RGB565, 255 KB with mip chains.

### Results on hardware (2026-10-07, owner's New 3DS, 3dslink build 8ff82e4)

New 3DS, `osSetSpeedupEnable(true)` applied: 60 headless frames of mission 1 step at
4.107 ms before and 0.991 ms after (x4.14). `vramSpaceFree` before anything: 6.00 MB;
linear free 30.21 MB. The first run crashed in variant B with a data abort writing to
0x1F05DC00: VRAM is not CPU-writable on hardware (Azahar allows it); textures now go through
`C3D_TexLoadImage` from a linear staging buffer (a GX copy for VRAM textures).

All times in ms; mission 1, the same frames in every variant.

| variant | GPU avg | GPU p95 | CPU avg | CPU p95 | frame avg | frame p95 | VRAM free MB | linear free MB | vertices/frame |
|---|---|---|---|---|---|---|---|---|---|
| A linear RGBA8, mono | 12.34 | 12.67 | 0.05 | 0.05 | 16.85 | 16.74 | 5.27 | 29.52 | 7260 |
| B VRAM RGBA8 | 2.65 | 2.76 | 0.05 | 0.05 | 16.71 | 16.74 | 4.89 | 29.90 | 7260 |
| C VRAM RGB565 | 2.04 | 2.14 | 0.05 | 0.05 | 16.71 | 16.73 | 5.08 | 29.90 | 7260 |
| D C + stereo (two top targets) | 3.80 | 4.09 | 0.07 | 0.07 | 16.71 | 16.74 | 4.35 | 29.90 | 14520 |
| E D with terrain x3 | 8.82 | 8.91 | 0.11 | 0.11 | 16.71 | 16.74 | 4.35 | 29.90 | 39096 |
| F C + 4 full-screen alpha quads | 3.82 | 3.95 | 0.08 | 0.08 | 16.71 | 16.74 | 5.08 | 29.90 | 7260 |
| G C + CPU mip chain, trilinear | 2.01 | 2.09 | 0.05 | 0.05 | 16.71 | 16.74 | 5.02 | 29.90 | 7260 |
| H C + bottom screen target | 2.72 | 2.88 | 0.06 | 0.07 | 16.71 | 16.74 | 4.64 | 29.90 | 7266 |

Simulation step during the rendered variants, speed-up on: 2.1 to 2.3 ms (5.1 ms in A, which
ran before the speed-up took effect on that path).

Decisions taken from the numbers:

- **Textures live in VRAM.** Linear-memory texture reads were the whole GPU cost of the spike
  (A 12.3 ms, B 2.65 ms). The VRAM budget after the targets is about 4.2 MB; hot textures
  first, spill to linear with a priority by draw frequency.
- **RGB565 and the other 16-bit formats by default**: C is 23% cheaper than B and halves the
  VRAM footprint. Mipmaps are free (G equals C): keep them for 3D textures.
- **Stereo is affordable from M1**: two eyes cost 1.9x the GPU time (D 3.8 ms for this scene);
  budget 14 ms of GPU per frame at 60 Hz.
- **Vertex budget**: E minus D gives 24.6k extra vertices for 5.0 ms, about 5k vertices per ms.
  At 60 Hz in stereo the GPU affords roughly 50k vertices per frame in total (25k per eye)
  once fill is accounted for; terrain plus the models of a busy mission fit.
- **Fill**: one full-screen alpha-blended 400x240 layer costs about 0.45 ms (F). Particles and
  2D overlays are cheap; the particle cap is about CPU, not fill.
- **The bottom screen may be drawn with citro3d every frame**: 0.7 ms (H), and the HUD is far
  smaller than a full-screen textured quad. Set `gfxSetScreenFormat(GFX_BOTTOM, GSP_BGR8_OES)`
  and double buffering before creating its target (consoleInit leaves it RGB565).
- **CPU**: with the speed-up the simulation of mission 1 takes about 2 ms per frame; the
  stereo submit is negligible (0.07 ms). The New 3DS frame budget is comfortable; the Old 3DS
  (no speed-up: 5 ms here, 8 ms in mission 14) is the one that needs the M4 tiers.

## M1.3 PICA core (engine/src/render/pica)

| File | What |
|---|---|
| `pica_frame.h/.cpp` | the frame interface: `initBackend`, `frameBegin`, `drawOn(FrameTarget)`, `frameEnd`, `frameArena()`, `shaders()`, depth mapping, fog, `picaEyeProjection`; never creates targets (platform/ctr does, M1.8 connects them) |
| `gfx_pica.cpp`, `texture_budget.h` | `Texture2D` via texconv (format from `TextureOptions::cls`, pow2 8..512, mip chain, swizzle), VRAM first within `TextureBudget` (4.2 MiB), linear beyond, `Residency` forces; VRAM upload from linear staging with `C3D_TexLoadImage`; `pica::textureStats()` |
| `render_state_pica.cpp` | `render_state.h` and `applyMaterialState` over citro3d (GEQUAL depth, CCW cull, GL rectangles mapped onto the rotated framebuffer, clears as GX fills or a scissored quad) |
| `tev.h/.cpp` | TEV presets as data + `applyTev` |
| `linear_arena.h/.cpp` | double-buffered per-frame linear memory (2 x 1.5 MB), one flush per frame |
| `shader_set.h/.cpp`, `shaders/*.v.pica` | flat, mesh (baked ambient-cube weights), terrain (static + dynamic-light streams); assembled and embedded by `engine/src/render/module.cmake` |
| `fog_pica.cpp` | fog table from `as3d/fog_lut.h` |
| `eye_projection_pica.cpp`, `pica_space.h` | `Mtx_PerspStereoTilt` with iod = 2·A·eyeOffset, screen = A·focalDistance (A = 1/(aspect·tan(fovY/2))) |

Verified conventions:

- Fog table: entry i is depth i/128 with depth 1 at the near plane (citro3d `FogLut_Exp`,
  `FogLut_CalcZ`), 256 floats with differences; zFlip off. With the Z-buffer and planes 4..2000
  the game's fog range sits in entries 0..4, so the table is up to 0.2 off the GL linear fog
  (z 500: 0.60 against 0.71); a W-buffer (`C3D_DepthMap(false, -1/near, 0)`, Azahar-checked,
  hardware not yet) reproduces it exactly. M1.4 decides which mapping the world uses.
- Eye projection: citro3d's translation term iod/2 is in clip units, so the naive iod = 2e,
  screen = D mapping is off by A (4 % at 60 degrees); with the mapping above
  `picaEyeProjection` equals tilt * `eyeProjection` * T(-e) to 5e-7 (on the console).
- Cull: Back = `GPU_CULL_BACK_CCW` (the tilt is a rotation). Depth: GEQUAL; a disabled test also
  drops the depth write, as GL.

`mode=picatest` in run.ini runs `apps/3ds/source/pica_test.cpp` (no game data): it writes
`picatest_log.txt` (byte accounting, matrix comparison, fog values) and `picatest_checks.txt`
(expected pixels); `tools/3ds_picatest_check.py shot.png picatest_checks.txt` checks an
Azahar screenshot (screen located by corner markers). 2026-10-08 on Azahar: 81/81 probes
within tolerance (texture formats exact, blends, depth, cull, viewport, scissor, terrain TEV
off by 1, mesh shader, both fog rows, stereo markers at the GL eye positions); texture bytes
39424 = sum of texconv::byteSize, VRAM free dropped by exactly the VRAM share (35328).

## M1.4 Terrain on the PICA (engine/src/render/pica/terrain_renderer_pica.cpp)

`TerrainRenderer` (as3d/terrain_render.h, unchanged public API) gets a PICA pimpl over the M0.4a
geometry (as3d/terrain_build.h); `terrain_pica.h` adds the byte counters and the overlay bias.

- Buffers (linear memory): one `TerrainVertexPica` buffer per 8-row chunk (uv1 and the normals
  dropped: the detail uv comes from the position in `terrain.v.pica`, the normals stay on the CPU
  for the lights), one per chunk and tile atlas for the overlay quads, one u16 index list shared
  by every chunk (a shorter last chunk draws its prefix), one zero light stream for unlit draws.
- Textures: base blocks 256x256 RGB565 with mip chains, detail RGB565 with mips, atlases by
  content (`TextureClass::Auto`: RGBA5551 or RGBA4), all through the VRAM-first budget.
- `render(params)`: `params.projection` is the eye's `picaEyeProjection`, `params.view` the
  central view. Per call: W-buffer depth mapping for the near plane read from the projection,
  fog table for its near/far and the level's fog range, cull against projection * view (the GL
  planes with the far plane replaced by the PICA's clip z <= 0), so each eye culls itself. Chunks:
  cull off, depth test + write, `tevTerrain2Stage`, base on unit 0 (bound per 32-row block),
  detail on unit 1, `setTerrainDetail` per chunk with the whole repeats of the chunk's first row
  taken out (small interpolated uvs). Overlays: alpha blend, depth test without write,
  `kPolygonOffsetDepth` through `applyDepthMap`, `tevModulateTexColor`. The state is left as GL
  leaves it (depth test + write, no blend, cull off).
- Dynamic lights: per visible chunk the lights whose radius reaches its box, nearest first, at
  most 8; `terrainDynamicLightColors` gives the lit colour, the stream holds lit minus baked (the
  shader adds and clamps, so the sum is the CPU's exact byte). Overlay vertices copy the stream
  of their grid vertex. Computed once per frame and chunk (keyed by the arena's frame number), so
  the second eye reuses them.
- `update()` (craters): `updateChunkHeights`, then new buffers for each touched chunk and its
  overlays; the old ones are freed two frames later (the frame in flight may still read them).
- `options.wireframe` is ignored (the PICA has no line primitive).

Mission 1 numbers (Azahar, `mode=terraintest`): 32 x 256 cells, 32 chunks, 9504 chunk vertices,
3 tile atlases with 246 quads (1476 vertices). Linear memory 257.3 KB of vertex buffers + 4.2 KB
of index list and zero stream (`linearSpaceFree` dropped 263.9 KB); 12 textures, 1727.6 KB, all
in VRAM (8 base blocks, detail, 3 atlases). From the world camera after 120 bot frames: 5 chunks
visible, 7 draw calls, 7746 indices; 3 lights light 2 chunks with 2376 bytes of arena. A crater
on 7x7 vertices re-uploads 2 chunks in 0.38 ms. `build()` takes 2.3 s under Azahar (base texture
generation, mip chains and swizzling; to be measured on hardware against the 3 s load budget).

Azahar check (2026-10-08). The test dumps its top target to `terraintest_<phase>.ppm` (exact
pixels; window captures add the emulator's scaling and gave MAE 5.5). Phase 1 against
`as3d_game --headless --level 1 --bot --frames 120 --size 400x240 --screenshot-every 120
--render-only terrain` with `tools/3ds_screen_compare.py`: mean abs error 1.56/255 (R 1.80,
G 1.25, B 1.63), 99.8 % of pixels within 8, 100 % within 16. Same framing, orientation, detail
layer, fog and tiles; the residue is RGB565 quantisation and filtering in the distant rows. The
crater (19k pixels change) and the three lights (red left, green right, blue further, 33k pixels)
land where placed. Grazing view of the road tiles: the overlay pass changes 620 pixels (solid
tile areas, no speckle), and the image with bias 0 equals the image with 2^-18 and with 2^-22
pixel for pixel: the overlays reuse the terrain's vertices, so equal depths pass GEQUAL and the
bias only absorbs rounding. `kPolygonOffsetDepth` is now 2^-22 (pica_frame.h explains the w
scaling of the offset under the W-buffer). Azahar's GPU times are constants (0.25 ms): the
terrain's GPU cost needs the hardware run.

## M1.5 Meshes on the PICA (engine/src/render/pica/gpu_mesh_pica.cpp, mesh_renderer_pica.cpp)

`GpuMesh` (as3d/scene.h) and `MeshRenderer` get PICA bodies; the neutral side (ResourceCache,
Material, materialKeyFor, ambient_cube) is unchanged.

- `GpuMesh::upload` (gpu_mesh_pica.h) takes the buildRenderMesh list and bakes the six ambient-cube
  weights per vertex from its normal (`bakeAmbientCubeWeights`), so the stream is `MeshVertex`
  (28 bytes) + u16 indices in linear memory, flushed once. buildRenderMesh already refuses more
  than 65535 corners, so there is no splitting; `upload` logs and refuses a larger list.
  `pica::meshMemoryStats()` counts the bytes. The destructor hands both blocks to
  `pica::deferLinearFree` (pica_frame.h), freed at the next `pica::frameBegin()`
  (C3D_FrameBegin has waited for the GPU), at once when the backend is down, the rest at
  `shutdownBackend()`.
- `pica::MeshPass` (mesh_renderer_pica.h) is the per-eye contract: `begin(lighting, lights)`,
  `submit()` per record and `finish()` once per frame (all CPU work: the six cube colours of
  `ambientCubeColors` with the dynamic lights of `accumulateModelLights`, exactly the GL shader's
  cPos/cNeg; the MaterialKey; the TEV kind), then `draw(picaEyeProjection(view, eye, aspect) *
  view.view)` per eye (uniforms, `applyMaterialState`, texture, TEV, `C3D_DrawElements`). The
  caller sets the depth mapping, fog and viewport. Order as GL's mesh renderer: stable by
  SortBucket (opaque, trans, effect), submission order inside (render-pipeline.md 1.2: no depth
  sort; depth write follows RF_NODEPTHWRITE, as GL). TEV: lit textured `tevModulateTexColor`
  (tint 1, so alpha = skin alpha), RF_NOLIGHTING `tevModulateTexConstant` with entity colour x
  material tint (GL uColor), untextured the vertex colour or the constant alone. Environment
  maps are not drawn yet: those materials draw as plain textured models (M3.4).
- `MeshRenderer` (begin/submit/end) wraps one MeshPass: `end()` sets the W-buffer mapping for
  the camera's near plane, the level fog (or none when fogStart is beyond the far plane) and
  draws with `picaTiltMatrix() * camera.projMatrix() * camera.viewMatrix()` (mono). M1.8's world
  pass should use MeshPass directly: two passes (opaque list; trans + effect lists) prepared in
  `prepare()`, drawn per eye in `draw(EyeView)`, so the cube colours are computed once.
  `pica::meshFrameStats()` sums draws, vertices, triangles and bytes since `frameBegin()`.

Mission 1 numbers (Azahar, `mode=meshtest`, the closure of the level loaded as
`WorldRenderer::precache` does without shadows): 130 meshes, 0.30 MB of linear memory; 113
textures (skins), 3.13 MB, all in VRAM (with no terrain loaded; with M1.4's 1.7 MB of terrain
textures the 4.2 MB VRAM budget overflows, so some skins will go to linear memory: M1.8/M1.10
decide the order). After 120 bot frames: 35 model records (17 opaque, 10 trans, 8 effect), 3
dynamic lights; 35 draws, 1725 vertices, 1453 triangles, 57 KB read; C3D CPU time 1.42 ms for
the 35 draws under Azahar (GPU time is a constant 0.25 ms there: hardware run pending).

Azahar check (2026-10-08). Test b (every model record of the frame, world camera) read back as
`meshtest_b.ppm` against `as3d_game --headless --level 1 --bot --frames 120 --size 400x240
--screenshot-every 120 --render-only models` with `tools/3ds_meshtest_compare.py` (mask: the
desktop pixels that differ from the clear colour): over all 3902 model pixels mean abs error
21.97/255, 62.9 % within 16; the difference is the environment maps the PICA does not draw yet:
the player's `p_apache_shield` shell (black skin + `gfx\effects\shield.tga` env, additive: the
orange glow around the helicopter on the desktop, nothing on the 3DS) and the ENV_GLITTER help
item. Excluding those two rectangles: 1531 pixels, mean abs error 3.98/255, 91.3 % within 16
(the rest are silhouette pixels of the palms and texture filtering; the lit colours match: no
quantisation step of the u8 weights shows). Whole image: 1.21/255, 98.5 % within 16. Rotor
discs (trans list, alpha) and the helicopter's flares (effect list) draw in the GL order and
look the same. The checks files (`meshtest_checks_<t>.txt`: corner markers, clear colour on a
grid away from every model, a model's largest front triangle covered) pass 4/4, 28/28, 28/28.
Test c adds a red, a green and a blue point light near the player: only the blue channel moves
visibly (+13 on average over the helicopter), red and green being already clamped at 1 by the
sun and ambient of mission 1, as GL clamps them.

## M1.6 Particles, sprites and health bars on the PICA (engine/src/render/pica/)

`ParticleRenderer` (as3d/particle_render.h) and `SpriteRenderer` (as3d/sprite_render.h) get PICA
pimpls (`particle_renderer_pica.cpp`, `sprite_renderer_pica.cpp`) over the neutral builders of
M0.4c (`buildParticleQuads`, `buildSpriteBatches`). The public API is unchanged except for one
new method, `ParticleRenderer::preload(path)` (GL: loads into the renderer's own cache; the GL
`WorldRenderer::precache` now calls it instead of drawing an invisible particle per texture).
There is no health bar renderer to port: `buildHealthBar` (health_bar.cpp) makes two
`SpriteInstance`s that the world appends to the sprite list, so the bars go through the sprite
renderer (the plan's `health_bar_pica.cpp` is not needed). `fx_pica.h` has the caps, the counters
(`fxPicaStats()`) and `capRuns`; `fx_draw_pica.h/.cpp` the shared upload and draw.

- Build: the builders fill `ParticleVertex` / `SpriteVertex` (9 floats) vectors and DrawRuns;
  the renderer cuts them at 1536 particle quads (the sprite builder caps itself at 512, the plan's
  768 is the upper bound) with `capRuns`, converts to `FlatVertex` (24 bytes, colour rounded to
  u8) straight into the frame arena, and counts quads, dropped quads and arena bytes.
- Draw (`drawFlatRuns`): W-buffer depth mapping and fog table from the projection's planes (as
  the terrain), `Shader::Flat` bound once with `mvp = projection * view`, the vertex stream set
  once, then per run `applyMaterialState(run.key)` (blend, depth test without write for
  particles, cull, the fog colour per blend: black for Add, white for Filter), the TEV
  (`tevModulateTexColor`; `tevClampAddTexColor` for texMode 2 = the Filter blend's clamp-add
  look; the vertex colour alone for a run without texture), the texture on unit 0 and one
  `C3D_DrawArrays`. The state is left as GL leaves it (blend off, depth test + write).
- Textures: `ParticleRenderer::preload` reads the TGA and creates the `Texture2D` (mipmaps,
  repeat, `TextureClass::Auto`: RGB565, RGBA5551 or RGBA4 by the pixels) outside any frame and
  counts a `LoadKind::Texture`. A texture first seen inside `draw()` is never created there: the
  run draws with a white 8x8 fallback and `particleLateTextures` counts it (0 after a precache).
  Sprite textures come with the instances (the caller's cache).

Per-eye contract for M1.8 (the same text is in `fx_pica.h`): both renderers keep `draw(list,
params)` and are called once per eye. The first call of an arena frame (`frameArena().frames()`)
builds the quads with the billboard axes of `params.view` into the arena; further calls of the
same frame with the same list pointer, count and axes (the second eye: the eye offset is a
translation, so the central and the eye view share their rotation) reuse the vertices and only
redo the per-eye part: matrix, depth mapping and fog table, fog colour, shader bind, one draw per
run. So `params.view` is the CENTRAL view and `params.projection` the eye's `picaEyeProjection`
(as the terrain), and the world must not step between the two eyes. Sprites additionally key on
`cullBackFaces`. Textures: preload at the level start (the precache).

Mission 1 (`mode=fxtest`, 1400 bot frames; at frame 300 there is almost nothing to draw): 35
emitters, 85 live particles (75 add, 10 filter) = 85 quads, 12.0 KB of arena, 6 draws; 19
sprites, 2.7 KB, 10 draws; no health bar on screen. 6 particle textures, 212.9 KB (all textures
of the test 280.2 KB, 16, all in VRAM, none in linear memory), 0 late textures. The synthetic
stress of 1536 quads (three emitters of 512: alpha, additive, filter) uses 216.0 KB of the
1.5 MB arena half, 3 draws, 0 failures, nothing dropped; two eyes in the screen halves cost the
arena of one build (14.6 KB). Azahar's GPU time is the constant 0.25 ms: the fill cost of the
stress needs the hardware run.

Azahar check (2026-10-08, `tools/3ds_screen_compare.py --mask`, 400x240 PPM dumps against
`as3d_game ... --frames 1400 --render-only ...`; mask = pixels that differ from the clear colour
in either image):

| phase | pixels | mean abs error | within 8 | within 16 | within 32 |
|-------|--------|----------------|----------|-----------|-----------|
| 1 particles | 971 | 1.62 | 0.955 | 0.981 | 0.995 |
| 2 sprites (health bars) | 2440 | 1.12 | 0.975 | 0.990 | 0.998 |
| 3 particles + sprites | 2829 | 1.29 | 0.971 | 0.988 | 0.996 |

Additive glows, the clamp-add fire, the alpha smoke and the draw order match; the residue is the
RGBA4/RGB565 quantisation and the texture filtering. Phases 2 and 3 leave out a 75x70 rectangle
(`--ignore 225,170,300,240`): the desktop `sprites` pass also draws the ground marks (the black
explosion scorch there), which the PICA does not have yet.

Deferred: ground marks, skid trails, decals and lightning on the PICA (M3 or later; the neutral
builders exist); the GL-free entity pass that collects the sprite list (a copy sits in
`fx_test.cpp` until the PICA WorldRenderer of M1.8); the `LoadKind::Texture` count of the GL
`ParticleRenderer` lookup (GL loads stay in the Other bucket); the GPU fill cost on hardware.

