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
removed from `as3d_input` on the 3DS; audio: the ndsp device (M2.3), music through libopenmpt
when its source tree is there and tremor (M2.4).
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
and `GameFlow` link `GameView`, which holds the desktop's layers; the 3DS composes its frame in
`ctr_frame.cpp` (M1.8).

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
  maps: M3.4 (`Shader::Env`, the env TEV presets).
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


## M1.7 The 2D layer on the PICA (engine/src/ui/pica/renderer2d_pica.cpp)

`Renderer2D::flush()` for the 3DS, the counterpart of `ui/gles/renderer2d_gl.cpp`; the quad list,
`begin()` mapping and `buildBatch2D` stay neutral.

- Batch: `buildBatch2D` with `tessellateShapes` forced on (no `fwidth` on the PICA: circles and
  rings are CPU fans with a one-pixel alpha ramp, as `batch2d.h` describes), then the vertices
  are copied as `FlatVertex` (z = 0, the 8-bit colour of the batch) into the frame arena: 24 bytes
  a vertex, a rectangle is 6 (144 B).
- Matrix: `picaTiltMatrix() * ortho(0, W, H, 0, -1, 1)` of the current target's upright size
  (`pica::currentTarget()`: 400x240 top, 320x240 bottom); the `begin()` origin (GL convention)
  shifts the layout in the target as `glViewport` does. This is `Mtx_OrthoTilt` without citro3d
  (the same tilt as the 3D projections, tested on the desktop); the vertex shader is the flat one.
- Per run: blend (`C3D_AlphaBlend` with the GL factors: Alpha SRC_ALPHA/ONE_MINUS_SRC_ALPHA, Add
  ONE/ONE, Filter DST_COLOR/ZERO, Opaque ONE/ZERO; alpha channel ZERO/ONE), TEV and textures,
  `C3D_DrawArrays`. Texture * vertex colour is `tevModulateTexColor`; an untextured run uses the
  vertex colour alone (the colours differ per quad inside one run, so `tevConstantColor`, one
  constant per stage, does not apply). A second texture uses `tevCombine2(mode)` on unit 1 and a
  new fourth shader, `flat2.v.pica` (`Shader::Flat2`, `Flat2Vertex` = `FlatVertex` + uv2, 32 B):
  the flat shader has no second texture coordinate. Only those runs pay the extra 8 B a vertex.
- State: fog off, depth test and write off, no culling, alpha test off during the flush; blend off,
  depth on and cull back after it (what the other PICA renderers expect). The scissor is the
  caller's: `as3d::setScissor` with GL rectangles before the flush (`Renderer2D` has no scissor
  of its own; the GL flush disables it).
- Pixel snapping (`ui2dPicaSetSnap`, default on): the 12x6 px glyphs of the bottom screen's 0.4
  mapping would be smeared by bilinear filtering across fractional pen positions (advances of 5.6
  px). The corners of every axis-aligned two-triangle quad with a texture are rounded to whole
  target pixels (shapes, lines, rotated quads and untextured fills keep their coordinates). The
  mapping itself cannot be rounded (the scale is 0.4); its offset is 0 at 320x240 and 40 px at
  400x240.
- Textures: created outside frames (`UiAssets::load`, `texture()`); a run that meets an unloaded
  texture draws white and counts it (`Ui2dPicaStats::missingTextures`, 0 in the test).
  `UiAssets` now passes `TextureClass::Font` (LA8) for `font.tga` and `font_alpha.tga` and
  `Alpha` (RGBA4) for the HUD atlases, life icon and cursors; `texture()` pictures keep `Auto`.
  The GL backend ignores the class. Mission 1 at the bottom layout: 13 textures, 519.1 KB, all in
  VRAM (font 256x256 LA8 128 KB, `mainbar` 64 KB, weapons, missiles and items atlases 64 KB each,
  life 2 KB, cursors, the test's two 8x8).
- Counters: `ui2dPicaStats()` / `ui2dPicaResetStats()` (flushes, runs, two-texture runs,
  vertices, arena bytes, arena failures, missing textures).

The touch controls were lifted out of `apps/game/touch_overlay.cpp` into the engine
(`as3d/touch_controls.h`, `engine/src/input/touch_controls.cpp`, `ui::drawTouchControls` with
`ui::TouchControlsState`); the game's `touch_overlay.h` aliases the state and re-exports the
function, so the desktop, Android and web call sites are unchanged. `as3d_input` now links
`as3d_ui`.

Mission 1 numbers (`mode=uitest`, Azahar, 120 bot frames):

| content | runs | vertices | arena |
|---------|------|----------|-------|
| bottom HUD + touch band (phase 2) | 16 | 16164 | 378.8 KB |
| font block bottom (phase 1) | 3 | 1170 | 27.4 KB |
| quads test (blends, 4 two-texture runs, discs) | 13 | 1380 | 33.0 KB |

The HUD plus touch band is the heavy case: 16 164 vertices (2.7 k quads: the touch icons are
filled with horizontal strips and the discs and rings are 48-segment fans), 379 KB, a quarter of
one 1.5 MB arena half. Azahar's GPU time is constant (0.5 ms here), so the fill cost of the 2D
layer needs the hardware run. The frame time in Azahar (CPU side, vsync-bound) is 16.8 ms in phases 1
and 3 and 33.6 ms in phase 2: the 16 k-vertex HUD frame (batch build and conversion on the emulated
ARM11, plus the frame's two screens) misses one vblank there; to measure on hardware.

Azahar check (2026-10-08): the bottom target dumped from the app (`uitest_2.ppm`, 320x240)
against the bottom half of `as3d_game --dual-screen --headless --level 1 --bot --frames 120
--screenshot-every 120` (400x480 frame, crop `40,240,360,480`),
`tools/3ds_screen_compare.py --direct --mask`: 8518 non-background pixels, mean abs error 5.18
(R 7.71, G 5.19, B 2.64), within 8: 0.680, within 16: 0.845, within 32: 0.972; over the whole
screen 0.58, 0.965 / 0.983 / 0.997. The layout, bars, icons, touch buttons, counts and text match
to the pixel; the residue is the RGBA4 atlases against the desktop's RGBA8, the pixel snap, and
the edges of the tessellated rings. The 24 probes of phase 3 (blend modes, five combine modes
of the second texture, a scissored fill, a tessellated disc and ring) all pass exactly
(`tools/3ds_picatest_check.py --ppm`).

Legibility of the font at the bottom screen's 0.4 mapping (glyphs 12x6 px, scale 1.0): upper case
and digits read clearly; lower case is thin but readable ("The quick brown fox" in the alpha
font, the additive ASCII rows); punctuation is the weakest. This is the input of the M2.2 gate
(filter choice, built-in 8x8 font): a 4x nearest crop is in out/ui/font_ascii_4x.png (local, game art).

For M1.8: the bottom HUD is drawn once per frame, after the world, on the bottom target:
`pica::drawOn({bottom, 320, 240, false})`, clear, `assets.hudTarget = Bottom320x240`,
`r2d.begin(320, 240)`, `ui::drawHud`, `flush`, then `r2d.begin(320, 240)` and
`ui::drawTouchControls`, `flush` (the bottom screen is not stereoscopic and does not depend on
the eye). Top-screen overlays (hint box, level end, pause) are drawn per eye after the world, on
that eye's target with `r2d.begin(400, 240)`: the quads are laid out in framebuffer pixels, so
an overlay at screen depth is the same 2D picture on both eyes (build it once per frame and
`flush` it twice; `flush` rebuilds the batch from the quad list, so keep the list and flush on
each eye, or add a parallax shift of a few pixels by calling `begin` with a different origin
per eye). Textures must exist before the frame (`UiAssets::load`, `precachePlay`, `texture()`
at the level start). Call `ui2dPicaResetStats()` once per frame if the counters are wanted per
frame.

Deferred: `hudStateOf` for the 3DS (a copy of the world part sits in `ui_test.cpp` until the
PICA `GameView` of M1.8); the top-screen overlays in stereo (the per-eye origin above is
untested); the scissor per quad (no such field in the quad list; the caller sets it around a
flush); the filter choice for the font and the 8x8 fallback (M2.2); GPU cost of 16 k 2D
vertices on hardware (a strip-based touch icon draws about 2.7 k quads; a texture or fewer strips
would cut it).

## M1.8 Frame composer and stereo (apps/3ds/source/ctr_frame.cpp)

The neutral half of `WorldRenderer::prepare` moved out of the GL renderer into
`engine/src/render/world_scene.{h,cpp}` (`WorldScene`, private to the render module and the 3DS
app): `worldViewOf`, `worldRenderOrder`, the entity pass in think order (model records with the
material per (definition, skin), list caps, sprites, health bars, ground-mark descriptions, skid
trails, bolts, emitters), the dynamic lights, the level lighting and fog, the cull volume, the
terrain change rectangles (`terrainChanges`), and the precache closure walk (`precache`, with
hooks for the shadow silhouettes and the particle textures). What only a backend can do stays
there: the GL renderer turns `ShadowRequest`s into silhouettes and cuts the mark geometry. The
GL goldens are unchanged (tools/ci.sh). `mesh_test.cpp` and `fx_test.cpp` use it instead of their
copies (their Azahar dumps are byte-identical to M1.5/M1.6's).

`engine/src/render/pica/world_renderer_pica.cpp` is the PICA `WorldRenderer` (same public API):

- `beginLevel`: the terrain (`TerrainRenderer::build`: its 12 textures, 1.7 MB, are the first VRAM
  tenants), one log line listing what is not drawn yet (M1.8 time: shadows, ground marks, skid trails, water,
  lightning, environment maps; since M3.4 nothing: the line says "all passes on the PICA"; `lastStats()` reports
  them as 0).
- `precache`: the `WorldScene` walk with `drawnFirst` (the meshes and skins of the entities alive
  now, then of the level's placements with their attachments) and `particlesFirst`, then the rest
  of the closure. No silhouettes. `AS3D_TEXTURES` logs the split.
- `prepare`: crater updates, `WorldScene::prepare`, then the two `pica::MeshPass`es (opaque; trans
  + effect) begun, submitted and finished: all per-model CPU work once per frame.
- `draw(eye)`, GL pass order: clear to the fog colour (black without fog), W-buffer depth mapping
  and level fog, projection = `picaEyeProjection(view, eye, aspect)` with the central view;
  terrain, opaque meshes, trans + effect meshes, particles, sprites (health bars included). The
  particle and sprite quads are built by the first eye and reused by the second (fx_pica.h).
- `drawBrightness`: a `pica::fillRect` (flat_fill.h) with `RenderBlend::Brightness`.
- `WorldRenderStats::drawCalls` / `vertices` (new, 0 on GL) sum every `draw()` since `prepare()`.
  `pica::worldPicaStats()` (world_pica.h) counts the records drawn from linear-memory textures.

The composer is the `FrameHook` of mode=bot and mode=play (`ctr_main.cpp`; mode=sim stays
headless). Per displayed frame, after the simulation steps:

    prepare      hudStateOf(session) (game_session.h, shared with the desktop and mode=uitest);
                 stereo = stereoFor(slider, camera-to-player depth); renderer.prepare(world,
                 Viewport{400x240, maxEyeOffset = eye offset, minFocalDistance = focal});
                 the top 2D quads (PAUSED, the perf line with perf=1), built once
    beginFrame   (C3D_FrameBegin)
    drawTop(0)   pica::frameBegin; drawOn(top left); renderer.draw(EyeView{-e, D}) (clears);
                 top quads flushed (zero parallax); brightness pass unless brightness = 0.5
    drawTop(1)   only with two eyes: the same on top right with EyeView{+e, D}
    drawBottom   drawOn(bottom); clear black; drawHud (HudTarget::Bottom320x240), drawHint,
                 drawTouchControls (the input's TouchMapper); pica::frameEnd
    endFrame     (C3D_FrameEnd)
    stats        perf.csv: prepare_ms, eyes, verts, draws (world + 2D), tex_vram_kb,
                 tex_linear_kb, lazy_loads, arena_kb (new column)
    afterFrame   run.ini dump_at=N: top_left.ppm, top_right.ppm, bottom.ppm + AS3D_DUMP

Level start (`begin`, before the first step): `pica::initBackend`, a loading frame (progress bar on
both screens and both eyes, "LOADING" once the font is loaded; at most one per 100 ms while the
precache reports progress), renderer init and `beginLevel`, `UiAssets::load`, then
`World::precacheScripts`, `WorldRenderer::precache`, `UiAssets::precachePlay`, `AS3D_PRECACHE`,
`LoadStats::reset`. At the end (`end`, before the context goes): `AS3D_LAZY_LOADS`, two empty
frames, the renderer and the UI released, `pica::shutdownBackend`. The spike's `render.cpp` and
`scene.v.pica` stay: mode=spike and the probe use them. The colour-cycling hook is gone.

Stereo. `eyeOffset = slider * kMaxEyeOffset` (4 world units at slider 1, run.ini `eye_offset=`),
`focal = max(camera-to-player depth, kMinFocalDistance = 40)` (run.ini `min_focal=`): the player's
helicopter sits on the screen plane, the ground goes into the screen, nearer things come out. With
A = 1/(aspect tan(fovY/2)) = 1.04, a point at depth d has the disparity 400 A e (1/D - 1/d) px;
mission 1's camera is 179 units from the helicopter, so the far limit is 9.3 px at slider 1 (2.3 %
of the width). Mono below the context's dead zone (slider <= 0.02): left eye only, `EyeView{}`.
Azahar's slider is qt-config.ini `factor_3d` (percent); run.ini `slider=` forces a value.

VRAM split, mission 1 (Azahar, budget 4.2 MB): VRAM 94 textures = 4193 KB in creation order:
terrain 1728 KB, the UI font and atlases, the models alive and placed (the player's Comanche
included), the 6 particle textures, then closure skins until full; linear memory 86 textures =
2315 KB (`level start` line): 44 model skins = 1382 KB (the other helicopters' colour variants,
the planes and the spawned items: `models\apache\apache_*`, `models\comanche\comanche_*`,
`models\helics\...`, `item_armor50`, ...) and 6 UI textures `precachePlay` adds. Of the 38 records
drawn at step 600, 6 use a linear texture (`mgun_flash`, `mgun_proj`: weapon effects spawned by
scripts). Before `drawnFirst` the player's own skin was in linear memory (9 of 38 records).

Azahar check (2026-10-08, `mode=bot level=1 frames=600 perf=1 dump_at=600 brightness=0.5`):

- End state after 600 steps equals `as3d_sim --level 1 --frames 600 --bot`: map_pos 452.0, list
  entities 39, slots 76 (max 81), created 243, freed 204, no script errors.
- Mono (factor_3d 0): eyes 1, no top_right. `top_left.ppm` against `as3d_game --headless --level 1
  --bot --frames 600 --size 400x240 --screenshot-every 600 --render-only
  terrain,models,particles,sprites` with `tools/3ds_screen_compare.py --direct --mask --ignore
  0,222,170,240` (the perf line): mean abs error 1.77/255 (non-background 1.85), 98.4 % within 8,
  99.3 % within 16. The difference sits on the player's helicopter (the env-mapped shield and the
  rotor), the help item (ENV_GLITTER), model silhouettes and the horizon band; the terrain, fog,
  lighting, particles and sprites match.
- Stereo (factor_3d 100, slider 1.00, e = 4, D = 178.7): the eyes differ in 91832 of 96000 pixels.
  Horizontal shift of the right eye against the left (best match per window): the helicopter
  0 px (zero parallax, both eyes project it at x = 141.3), the ground near it +4, mid distance
  +5 and +6, the far hills +7 (the far limit is 9.3). factor_3d 50: e = 2, shifts +2 and +3:
  half. The bottom screen is identical in both runs (HUD, the five touch buttons, the lives,
  weapon and missile panels).
- lazy_loads 0 in every row and `AS3D_LAZY_LOADS ... textures=0 models=0 scripts=0 ...`;
  `AS3D_PRECACHE mission=1 objects=291 textures=154 models=347 scripts=125`.
- Level start 9.4 s under Azahar (mission 1.6 s, beginLevel 2.3 s, UI 0.3 s, precache 5.2 s):
  the hardware run decides against the 3 s budget.

perf.csv (Azahar: its GPU time is a constant, its CPU time is not the console's):

    frame,step_ms,prepare_ms,cpu_ms,gpu_ms,eyes,verts,draws,tex_vram_kb,tex_linear_kb,linear_free_kb,lazy_loads,arena_kb
    280,3.689,1.322,27.824,0.501,1,26356,72,4193,2314,24854,0,389      (mono)
    200,3.913,1.257,29.713,0.751,2,33042,118,4193,2315,24854,0,392     (stereo)

The 2D band (HUD + touch discs) is about 16k of the vertices and most of the arena (M1.7).

Hardware checklist (owner, `mode=play level=1 perf=1`, then `mode=bot level=1 frames=3600 perf=1
dump_at=600`):

- slider 0: one eye, no ghosting; slider 50 %: depth visible, the helicopter on the screen
  plane; slider max: the far hills comfortable (9 px), no eye strain over a minute; if too strong
  or too weak, try `eye_offset=` 2..6 and report the value.
- HOME and resume, lid close and open: the game resumes paused-then-running, the picture is
  intact on both screens, no crash.
- START+SELECT quits cleanly (log.txt ends with the PERF_SUMMARY lines).
- touch: pause, next weapon, next missile, missile, power-up buttons react and their icons/counts
  update on the bottom screen; START pauses (PAUSED on the top screen at screen depth).
- perf.csv / PERF_SUMMARY: load_ms, frame times at slider 0 and max, lazy_loads 0, arena_kb.

## M2.3 Sound: the ndsp device (engine/src/audio/ndsp/audio_device_ndsp.cpp)

`IAudioDevice` over libctru's ndsp, selected by `AudioBackend::Platform` (`makePlatformAudioDevice`,
the ndsp backend directory). `AudioBridge` now opens `Platform` instead of `Sdl` (the sdl backend
returns its SDL device there, so desktop, Android and the web are unchanged).

- Init: `makePlatformAudioDevice` calls `ndspInit`; it needs the DSP firmware
  `sdmc:/3ds/dspfirm.cdc`. On failure it logs once (`audio: ndspInit failed (0x...)`) and returns
  null; `Audio::init` now returns false for a null device (it used to dereference it), and
  `AudioBridge` falls back to the Null device: the game runs silent. The device owns `ndspExit`.
- Output: one channel, `NDSP_FORMAT_STEREO_PCM16`, rate `NDSP_SAMPLE_RATE` (32728.5 Hz, the DSP's
  own: no DSP resampling, `NDSP_INTERP_NONE`), the mixer at `setDeviceRate(32728)` (15 ppm slow:
  inaudible). Four wave buffers of 1024 frames (4 KB each, 16 KB of linear memory, 125 ms queued,
  31.25 ms each), primed with four renders at open.
- Feeder: a libctru `Thread` on core 1 (`APT_SetAppCpuTimeLimit(30)` once; priority one step
  above the opening thread, 0x2F under the main loop; 32 KB stack; joinable), falling back to
  core 0 when core 1 is refused. It sleeps on a `LightEvent` that `ndspSetCallback` signals every
  DSP frame (about 4.9 ms) and, when the next buffer in play order is `NDSP_WBUF_DONE`, renders
  every finished buffer through `Mixer::renderS16`, flushes it (`DSP_FlushDataCache`) and queues
  it. `close()` stops and joins the thread, clears and resets the channel, waits 20 ms for the
  DSP to let go of the memory and frees it. `setPaused` is `ndspChnSetPaused`.
- Locking: the mixer's own `std::mutex` (render and every game-thread call take it). Finding:
  under devkitARM gcc 16 / current libctru `std::thread` and `std::mutex` compile, link and work
  (newlib's locks and the pthread layer are libctru LightLocks and Threads); `mode=audiotest`
  checks two threads adding 100000 each under one mutex (200000, 111 ms on Azahar). No
  `thread_compat.h` was needed. The feeder is still a libctru `Thread` because `std::thread`
  cannot pick core 1.
- Counters (`audio_device_ndsp.h`, `ndsp::stats()`): fills (after the priming), underruns (a fill
  that found no buffer still queued or playing while not paused: the DSP ran dry), the longest
  fill, time in `renderS16` (the feeder's CPU share), the peak sample since the last read,
  `ndspGetDroppedFrames`.

The 3DS loop (`apps/3ds/source/ctr_audio.cpp`, run.ini `audio=0|1`, default 1, never in `sim`)
drives an `AudioBridge` as `game_stack` and `game_loop` do: at level start the level's precache
set (`World::precacheScripts` when the renderer has not computed it; it only loads, the
simulation is unchanged; `precache=0` skips it, sounds then load on first use) goes to
`AudioBridge::precacheSounds` and `startLevel` stops the sounds;
after every step the world's sound queue is drained; the sound pauses with the world (START) and
on APT suspend/sleep and resumes on restore/wake; it stops at exit. Music is off: with
`AS3D_WITH_OPENMPT=OFF` the loop passes no module (M2.4). `log.txt` gets the device line and
`PERF_SUMMARY audio` / `PERF_SUMMARY audio_feed` lines.

Azahar (HLE DSP, 2026-10-08): without `dspfirm.cdc` `ndspInit` fails (0xD880A7FA), the Null
fallback is clean and the loop runs. Azahar's HLE accepts any component file: with a dummy
768-byte `dspfirm.cdc` (only the `DSP1` magic) `ndspInit` succeeds and the real path runs.

| run | result |
|---|---|
| `mode=audiotest`, no firmware | `Null device fallback`, 360 frames in 6012 ms, shutdown clean |
| `mode=audiotest`, dummy firmware | fills +32 every second (192 in 6 s), 0 underruns, 0 dropped; peak 11125 in the tone second (expected about 11000), 17152 / 24197 with the missile and the panned explosion; feeder on core 1 prio 0x2F; shutdown (join, ndspExit) 25.5 ms |
| `mode=bot level=1 frames=600 audio=1`, dummy firmware | map_pos 452.0, 39 entities, created 243, freed 204 (= the desktop); `audio device=ndsp played=97 missing=0 precached=22`, `audio_feed fills=344 (31.9/s) underruns=0`, frames over 17 ms 0 of 599 |
| the same, no firmware | the same end state; `audio device=null played=97` |

The fill time (7.5 ms a buffer) and feeder CPU (15 to 16 %) are emulator numbers, not hardware
ones. The level-start precache of mission 1 (scripts and 22 sounds) took 760 to 800 ms on Azahar.

On hardware (owner):

1. Dump the DSP firmware once: run the DSP1 homebrew (`DSP1.3dsx` from its releases page) from
   the Homebrew Launcher; it writes `sdmc:/3ds/dspfirm.cdc`.
2. `mode=audiotest`: `audiotest_log.txt` must show `platform device: ndsp: ...feeder core 1`,
   fills +32 every second, `underruns 0 (+0)` after the first second, a peak about 11000 in
   second 1, and the `max_fill` and `cpu` numbers (the real feeder cost: with 30 % of core 1 a
   fill must stay well under 31 ms). Listen for: a clean 440 Hz beep for 1 s, then the machine gun
   ten times, a missile, an explosion on the right; no crackle or gaps.
3. `mode=bot level=1 frames=3600 perf=1`: the end state unchanged, `PERF_SUMMARY audio_feed
   ... underruns=0 core1=1`, step and frame times as without sound (`audio=0` to compare).
4. `mode=play`: press HOME and come back, close the lid and open it: the sound stops and
   resumes with the game, no burst of stale sound, no hang; START pauses the sound.

Deferred: music (M2.4: libopenmpt measured on the 3DS, else pre-rendered Ogg and a tremor
decoder); per-entity 3D sound stays off as in the shipped config (`Sound3D=0`).

## M2.1 Front end (apps/3ds/source/ctr_menu_loop.cpp, the FlowView seam)

The seam. `GameFlow` needed five things of `GameView`: the level start (`beginLevel`), the frame
behind the front end (`drawFrame` with `FrameLayers`), the sequels' model view, the screen mode
and, through `GameStack`, `init` and the sound-precache hook. They are now the interface
`FlowView` (`apps/game/flow_view.h`, no graphics API; `FrameLayers` and `kDefaultBrightness`
moved there). `GameView` implements it unchanged; `GameStack` holds a `unique_ptr<FlowView>`
made by `StackConfig::makeView` (`makeGameView` on the GL hosts, the window loop reaches its
`GameView` through `gameView()`). The key bindings reach the `InputMapper` through an inline
`applyBindings` and a hook, so `game_flow.cpp` links without the mapper (it needs SDL's
scancodes; the 3DS has none). Chosen over splitting `GameView` because the 3DS draws its frame
very differently (two targets, two eyes, lists built outside the frame) and needs none of
`GameView`'s GL members; desktop, Android and the web keep their code paths (the goldens and the
front-end tests of tools/ci.sh). The 3DS builds `game_flow.cpp` and `game_stack.cpp` with
`game_session.cpp` and `audio_bridge.cpp`, not `game_view.cpp`.

The view (`GameFrame::makeFlowView`, ctr_frame.cpp): `init` brings up the backend, the 2D layer
(font and HUD atlases) and a renderer; `beginLevel` (every level, attract levels included):
a loading frame, two empty frames (the GPU done with the last level), a fresh `WorldRenderer`
and `UiAssets` (the last level's meshes and skins and the menu pictures loaded since leave VRAM:
without it mission 1 started with 2 MB of menu pictures in VRAM), terrain, precache (scripts,
renderer, `precachePlay`, the level's sounds through the stack's `AudioBridge`), loading
frames. `drawFrame` is called from `prepare` (outside the frame: the menu pictures load on first
use) and builds the bottom screen's list: the HUD (`HudTarget::Bottom320x240`), the touch
buttons during play, `Frontend::drawUnder` / `drawOver` at `computeMapping(320, 240)` (scale 0.4,
pixel-snapped by the PICA flush). The top screen: the world when the flow runs one (attract or
mission, mono or stereo as `play`, the profile's brightness), the game's title at screen depth
where the desktop's main menu has its 3D banner (and on black when there is no world), PAUSED
under the in-game menu. `GameFlow::loadingHook` draws `ui::drawLoadingScreen` on both screens
plus a visible progress bar (the original's is one virtual pixel high: 0.4 px here).

The flow on the 3DS (`FlowConfig` as the Android template: touch mode without the front end's
MENU button, one player, no mouse control; `consoleOptions`: no Configure controls, Screen,
hand or touch-speed rows; `FrontendContent::padNavigation`):

    boot -> intro pages (logo=1) -> attract level + main menu (bottom) -> Start Game / Options /
    Top Scores / Information -> Start: loading screen (both) -> playing (HUD + touch buttons on
    the bottom, world on top) -> START / touch pause: in-game menu (Resume, Options, Quit) ->
    mission complete / game over / name entry / top scores (bottom) -> Continue or Quit ->
    attract level + main menu; Exit ends the app.

Input: while a menu is up, `CtrInput::uiInputs` (D-pad = arrows with repeat after 24 frames
every 6, A = Enter, B / START = Escape, the touch screen = the pointer through the inverse of
`computeMapping(320, 240)`); during play only the pause button reaches the front end (Escape:
the in-game menu), the touch pause button through the step's `pausePressed`. The first game's
widgets keep Up / Down for themselves (spinners swallow them, the list moves its selection, the
helicopter grid takes every arrow), so Start was unreachable by keys: `MenuSystem::padMode`
(ours, on with `consoleOptions`): Up / Down always move the focus, Left / Right change the
focused spinner, slider, list selection or helicopter (the next unlocked). Desktop test: "game
flow: console options and pad navigation".

Saves: `GameFlow`'s own path, `<userDataDir>/<game>/profile.bin` = `sdmc:/3ds/openairstrike/as3d/
profile.bin`, written with `replaceFile` when leaving Options, after a mission and at the name
entry as on the desktop; APT suspend / sleep call `GameFlow::onBackground` (during play the
in-game menu opens, so the game comes back paused; then the save), exit calls `saveNow` (and
`GameStack::teardown` again). M2.5 checks the details on hardware.

Sound: the stack's `AudioBridge` on the platform device (`CtrAudio::attach` follows APT and
logs the summary); the menu sounds play; music is off while `AS3D_WITH_OPENMPT=OFF`
(`AudioBridge::setMusicEnabled(false)`, M2.4). Under menus the sound is not paused (desktop rule).

run.ini (apps/3ds/README.md, "Front end (M2.1)"): `mode=menu` or `mode=play` without `level=`;
`attract`, `logo`, `menu_bot`, `pad_script` (scripted pad presses and touches, parsed by the
neutral `parsePadScript`, tested), `dump_screens`, `dump_every`, `dump_prefix`. `level=N` keeps
the direct start of M1.

Azahar (2026-10-08, `mode=menu attract=1 logo=0 menu_bot=1 dump_screens=1 slider=0`, 950 frames,
pad_script: Options, back, Start Game, five Downs to Start, A; 300 bot frames; START, Resume;
START, Quit):

- AS3D_SCREEN main (frame 0) -> options (90) -> main (150) -> start (180) -> mission 1 loaded
  ("Mission 1: Tutorial") -> playing (300) -> ingame (620) -> playing (690) -> ingame (760) ->
  intro1 loaded -> main (830). Every screen dumped (`fe_<nn>_<screen>_{top,bottom}.ppm`).
- Front end ready (data, view, audio) in 1.1 s; the attract level start 5.6 s (beginLevel 1.2 s,
  precache 4.3 s, of which 0.8 s shadows); mission 1 from the menu 10.8 s, as the direct start.
- Mission 1 started from the menus: textures VRAM 94 = 4194 KB, linear 216 = 3264 KB (direct
  start: 99 / 210); the second attract level start gives exactly the first one's numbers (VRAM
  4169 KB, linear 316 KB: nothing of the mission is left). Menus at 60 fps (main menu: 5 frames
  over 17 ms of 419, the loads); mission frames as the direct mode under Azahar.
- `mode=bot level=1 frames=600` after the change: map_pos 452.0, 39 entities, created 243, freed
  204, as before.
- Main menu bottom against `as3d_game --dual-screen --headless --touch --attract 1 --no-logo
  --ui-script` (shot at frame 40, crop `40,240,360,480`), `3ds_screen_compare.py --direct --mask`:
  whole screen 4.13 (the desktop's 3D banner is on its bottom screen, the 3DS's title is on the top
  one); without the banner rows (`--ignore 0,0,320,90`) 0.98, masked 16.4 over 2839 pixels; without
  the corner rules as well 1.01, masked 16.7 over 2516 text pixels, best alignment at (0, 0). The
  text residue is the pixel snap and RGBA4 against the desktop's bilinear RGBA8 (glyph edges); the
  letterbox's long rule (`corner.tga`, a one-texel row stretched over 16 virtual rows, 6 px here)
  falls between the sampled rows on the PICA and is missing right of the corner pieces. Both are
  M2.2's (filter choice, snap, the 8x8 font): its inputs are the main, options and start dumps and
  4x nearest crops in out/3ds/m21/ (local, game art).

Hardware checklist (owner; copy `Settings.xml` and `texts_v170.txt` next to the paks; run.ini
`mode=play` alone):

- boot: intro pages (A speeds them up), then the main menu on the bottom screen at 60 fps over the
  attract level on top; text legible at arm's length (the M2.2 photo: main menu, Start Game,
  Options).
- D-pad and A / B through Options (volumes move with left / right and are heard), Top Scores,
  Information; touch taps on the same items land on them.
- Start Game: the list, difficulty, helicopter with left / right, Start; the loading screen on
  both screens; mission 1 plays as with `level=1`; START opens the in-game menu (PAUSED on top),
  Resume; Quit returns to the main menu over the attract level.
- HOME during play and back: the in-game menu is open, the game paused; close the lid and open:
  the same; `as3d/profile.bin` exists and survives a power cycle (Options values kept).
- `perf=1`: the menu frames under 16.7 ms, level start times.

Stubbed / deferred: music (M2.4); the save details on hardware, `aptSetSleepAllowed` policy and
no `*.tmp` (M2.5); the sequels' helicopter preview on the PICA (`drawModel` is a no-op) and their
front end on the 3DS untested; the main menu's 3D banner on the PICA (the title text stands in);
legibility (M2.2).

## M2.4 Music: libopenmpt measured, pre-rendered Ogg through tremor

Both paths are built and run end to end; which one the console uses is the hardware's verdict
(the checklist below). The game looks for the pre-rendered file first and falls back to the module.

What the games need. AirStrike 3D's levels name five modules, `music\track01.mo3` ..
`track05.mo3` (230 to 664 KB; the sequels ship the same five names). MO3 needs only libopenmpt's
own MO3 loader and, for its Vorbis-compressed samples, the bundled `stb_vorbis` the desktop
already compiles: no mpg123, libvorbis or zlib. The menu (attract level `intro1`) and mission 1
both play `track05.mo3`; the largest, `track04.mo3`, is the heaviest case measured.

libopenmpt on the 3DS (`AS3D_WITH_OPENMPT`, built from source). There is no devkitPro package;
the image's portlibs have libogg, tremor (libvorbisidec), libmpg123, libxmp, libmodplug and
libmikmod but not libopenmpt. `tools/3ds_build.sh` mounts the source tree that
`tools/fetch_third_party.sh openmpt` downloads once into `third_party_local/` (the desktop's
`libopenmpt-0.8.9+release.makefile.tar.gz`, SHA-256 `9273b88b...074f`, under
`${AS3D_DATA_ROOT:-repo}`), read-only at `/openmpt`, and passes `-DAS3D_3DS_OPENMPT_SRC=/openmpt`;
the root CMakeLists turns `AS3D_WITH_OPENMPT` on only then (else the stub, with a CMake warning).
`tools/3ds_ci.sh` sets `AS3D_3DS_FETCH=0`: no network, the stub when the tree is absent. The
library compiles unchanged with devkitARM (gcc 16), exceptions and RTTI on for that target only
(the C API catches everything; the engine stays `-fno-exceptions`), no pthread library (newlib's
locks are libctru's). Size: every format loader is reachable from the table in `Sndfile.cpp`, so
`--gc-sections` keeps all of them: +2.4 MB of 3dsx at -O2. The 3DS build compiles the loaders of
MO3, IT, XM, S3M and MOD only (`module.cmake` generates a stub probe and loader for the 54 others
of the table, and the two sample unpackers only they use), at -Os with the per-sample mixing files
(`Fastmix`, `MixFuncTable`, `MixerLoops`, `Sndmix`, `Snd_flt`, `Snd_fx`, `WindowedFIR`, `DSP`) at
-O2: the 3dsx goes from 1.35 MB (stub) to 2.55 MB, +1.2 MB (bss +0.37 MB); tremor adds 0.11 MB
(2.66 MB). The first full build takes about 6 minutes at -j2.

Tremor (`AS3D_WITH_TREMOR`, on for the 3DS): `music_tremor.cpp`, `OggMusic`, a `MusicSource`
like `MusicDecoder` (`music_source.h`, the mixer's interface): the whole file in memory, ov_read
into a 1024-frame buffer, linear resampling when the file's rate is not the device's (the tool
renders at 32728 Hz, so none), looping. `Audio::setPrerenderedMusicDir` (the 3DS loop gives
`sdmc:/3ds/openairstrike/<game>/music`): `playMusic("music\track05.mo3")` plays `track05.ogg`
from there when it exists, with `track05_gameover.ogg` as pattern order 35 (the game-over jump,
`AudioBridge::gameOverMusic`; after that part ends the music follows from its start, as the module
does when it loops), else the module, else nothing (`audio: cannot play music ...`).

Pre-rendering (owner, desktop, own data; the output is derived from the game: never committed,
`*.ogg` is in .gitignore): `audio_tool render-music --all <dir>` renders every module once (no
loop) at 32728 Hz plus its part from order 35, and encodes with oggenc, else ffmpeg's libvorbis
(quality 4, about 100 kbps): for AirStrike 3D 10 files, 10.5 MB, 98 to 196 s per module; peaks
0.39 to 0.93 (no clipping). `--paks <dir>` reads a sequel's paks.

The mixer change (desktop too, same output): the music is decoded outside the voice mutex, under a
music mutex of its own, so the game thread's play/stop never waits for a decode (up to 10 ms per
512 frames on Azahar); `hasMusic` is an atomic; `Audio::musicDecodeStats` (time in the decoder,
frames, longest call) and `Audio::setMusicInterpolation` (libopenmpt's filter length; the desktop
keeps the default, 8 taps).

The game (`ctr_audio.cpp`, `ctr_menu_loop.cpp`): run.ini `music=0|1` (default 1),
`music_taps=0|1|2|4|8` (default 0 = libopenmpt's 8 taps); the level and menu music go through
`AudioBridge::startLevel` as on the desktop (the front end's attract levels and missions), the
music pauses with the mixer (START in `bot` / `play` with `level=`, HOME, sleep; not under the
menus: the desktop rule), the volume follows the Options. `log.txt`: `audio: level start, N sounds
precached, music music\track05.mo3 (ogg|openmpt), in N ms` and
`PERF_SUMMARY music decoder=ogg|openmpt|off|none decoded_s=S cpu_share=X% max_decode_ms=X
underruns=N`.

The measurement: `mode=musictest` (apps/3ds/README.md): each module pre-rendered (when the file
is on the card) and as a module, 10 s each, then all again at 268 MHz on a New 3DS (the speedup
off: about an Old 3DS), per second the decoder share (time in `MusicSource::read` per second of
audio decoded), its longest call (512 frames = 15.6 ms of audio), the feeder's fills, underruns,
longest fill and CPU, then one `MUSICTEST ... -> USABLE|TOO SLOW` line per pass.

Azahar (2026-10-08, dummy `dspfirm.cdc`, New 3DS mode). Emulator numbers, not hardware ones: the
JIT's cost per instruction is not the ARM11's, Azahar does not enforce the 30 % core-1 limit, and
the two clocks give the same numbers.

| pass | load | decoder share (avg / worst s) | longest call | feeder CPU | heap |
|---|---|---|---|---|---|
| track05 module, 8 taps | 2.4 s | 28.3 / 48.8 % | 9.7 ms | 45 % | +3.4 MB |
| track04 module, 8 taps | 4.1 s | 37.7 / 45.1 % | 9.5 ms | 54 % | +6.7 MB |
| track04 module, 2 taps (5 s) | 5.1 s | 23.5 / 28.6 % | 5.9 ms | 40 % | +6.7 MB |
| track05 Ogg (5 s) | 0.3 s | 32.1 / 32.8 % | 13.8 ms | 47 % | +1.5 MB |
| track04 Ogg (5 s) | 0.5 s | 31.2 / 32.5 % | 14.0 ms | 48 % | +2.5 MB |

No underrun in any pass; the per-second peaks of the Ogg passes match the module's within 3 %
(7630 / 7717 / 6107 against 7413 / 7695 / 6155): the same music. The heap figure is the module
with its decoded samples (libopenmpt) or the Ogg file (tremor). `mode=bot level=1 frames=600`:
the end state as before (map_pos 452.0, 39 entities, created 243, freed 204); level start of the
sound 2.6 s with the module, 0.5 s with the Ogg, 0.2 s without music; heap high 14.8 MB (module),
12.9 MB (Ogg), 11.4 MB (none); frame times identical with and without music (46.7 to 47.2 ms
under Azahar either way). On Azahar the feeder without music is at 14 % (M2.3: 15 to 16 %).

Hardware checklist (owner): put the paks as before and, for the Ogg path, the output of
`audio_tool render-music --all` in `sdmc:/3ds/openairstrike/as3d/music/`.

1. `mode=musictest`: read the `MUSICTEST` lines of `musictest_log.txt`. The verdict per pass is
   `decoder_share` against 25 % (`worst_second` decides: the rule is per second, at 32728 Hz
   stereo); also `underruns=0`, `max_fill_ms` well under 31 ms per buffer, `feeder_cpu` under the
   30 % the app has on core 1 (`APT_SetAppCpuTimeLimit(30)`; the feeder's share includes the
   sound effects' mixing). Read the `clock=268MHz` lines for the Old 3DS. Then once with
   `music_taps=2` (linear) for the module path.
2. Decide: libopenmpt usable where its worst second is at most 25 %; else the Ogg files (M4.5
   makes them mandatory on the Old 3DS anyway, and they need less memory and load about 8 times
   faster). If both are above 25 %, the next levers are `APT_SetAppCpuTimeLimit` up to 80 and
   larger buffers.
3. `mode=bot level=1 frames=3600 perf=1`, with and without `music/` on the card and with
   `music=0`: `PERF_SUMMARY music decoder=...` names the path, `underruns=0`, step and frame
   times as with `music=0`. Listen: the music loops without a gap or a click at the loop point,
   no crackle.
4. `mode=play`: the menu music behind the main menu, the mission's music, game over jumps to the
   game-over part (Ogg: `_gameover.ogg`), HOME and the lid pause and resume it.

Deferred: the loop point of a pre-rendered file is its start (a module may loop to a restart
position, not order 0: none of the five was checked by ear); the Ogg file is held in memory (1 to
2.6 MB), streaming from the SD card if memory is short (M4.5); the sequels' music untested on the
3DS; `music_taps` stays a run.ini key (no Options row); the Old 3DS numbers themselves.

## M3.1 Shadows on the PICA (engine/src/render/pica/shadow_renderer_pica.cpp)

`ShadowRenderer` (as3d/shadow_render.h, unchanged public API) gets a PICA pimpl; the design notes
are in `shadow_pica.h`.

- Silhouettes: `generate()` runs the CPU rasteriser of M0.4d (`rasterizeShadowSilhouette`, within
  IoU 0.97 of the GL FBO path) and uploads the alpha as an A8 texture (`TextureClass::Shadow`),
  forced into linear memory (VRAM is full with the terrain, UI and skins; a shadow covers a small
  part of the screen), clamped, with the GL side's mipmaps, no CPU copy kept. A side above
  `kMaxShadowSide` = 128 is rasterised at 128 over the same bounds: at 400x240 the camera of
  mission 1 shows 1.16 px per world unit at the player's depth, so a 128-texel shadow already
  spans about 150 px and only the 256-unit projected shadows (5 of the 126 generated at level
  start) get coarser, about 2.3 px per texel under the bilinear filter. The PICA cannot read a
  skin texture back: the world renderer reloads the image of an alpha skin from the new
  `Material::texturePath` (12 skins, about 219 KB of CPU alpha in mission 1) and hands it over
  through `pica::setShadowSkinSource`; opaque skins cast like no skin, as on GL.
- Rasteriser speed-up (neutral, `shadow_raster.cpp`): the covered run of each row is solved from
  the edge functions, z/u/v are float planes per triangle, and the per-sample `fmod`, `lround` and
  64-bit-to-double conversions (library calls on the ARM11) are gone. The M0.4d test gives the
  same worst figures (mean |da| 0.486/255, IoU 0.9711 over the 111 silhouettes), 1.9x faster on
  the desktop and 2.4x under Azahar (60.4 to 24.8 ms per silhouette). `rasterizeShadowSilhouette`
  takes an optional side cap. Decal clipping (`decal_build.cpp`) no longer allocates per terrain
  triangle.
- Draw (`pica::ShadowPass`): `prepare()` places each `ShadowRequest` (silhouette or blob, the
  ground rectangle of `shadowRect`, now in the neutral `shadow_drape.cpp`) and drapes it over the
  terrain on the CPU (`buildTerrainDecal`, the GL code); projected shadows never move, so their
  draped geometry is cached until a crater changes the terrain; planar ones are draped every frame.
  Per eye, in the GL pass position (pass 5: after the terrain, before the opaque models): the first
  eye copies the vertices into the frame arena as `FlatVertex`, the second reuses them; flat shader
  with the eye's `picaEyeProjection` x central view, `decalMaterialKey` (alpha blend, depth test
  on, write off, `kPolygonOffsetDepth`, back faces culled, level fog), `tevShadow(255)` (constant
  black, the texture's alpha: the silhouette already holds 0..102), one draw per shadow. The
  sequels' multiplied look (`shadowMultiply`) uses the Filter blend with grey 1 - alpha.
- Residency and the late path: the cache is keyed by (mesh, skin texture if it has alpha, kind,
  quality, steps), the GL key without strings. `precache()` generates the placements' silhouettes
  (in their own rotation, the set the GL `beginLevel` makes) and the closure's planar ones. The 12
  rotations of every projected one of the closure (what the GL precache does) are too slow on the
  3DS CPU and come only with `pica::setShadowPrecacheRotations(true)` (shadowtest
  `shadow_rotations=1`). A silhouette first seen later (a script spawn in another rotation, or
  everything with `precache=0`) is generated in `prepare()`, outside the frame, at most one per
  frame, and drawn from the next frame; until then the shadow is the blob, a 32x32 radial A8
  texture (1.3 KB, 0.4 at the centre) over the same rectangle. Late silhouettes count as
  `LoadKind::Shadow` lazy loads and in `lateShadowMaps()`.
- Stats: `pica::shadowPicaStats()` (generated, failed, capped, bytes, generation time and its
  size histogram; per frame: silhouettes drawn, blobs, late, draped vertices, draws, arena bytes,
  placement and draping time); `WorldRenderStats::shadows`, `drawCalls`, `vertices` include them.
- `WorldScene::prepare` now lists the shadow requests of the model records also with
  `options.models` off, and `as3d_game --render-only` accepts `shadows` (reference images of the
  shadows without the models).

Mission 1 numbers (Azahar, `mode=shadowtest`; Azahar's CPU ticks are not the console's, so the
times are relative):

| | silhouettes | A8 bytes (linear) | generation | per silhouette (max) | capped to 128 |
|---|---|---|---|---|---|
| level start, default | 126 | 818.4 KB | 2.70 s | 21.4 ms (143.9) | 5 |
| level start, `shadow_rotations=1` | 726 | 5611.1 KB | 17.83 s | 24.6 ms (143.9) | 45 |

Sizes (larger side, all rotations): 16: 9, 32: 190, 64: 284, 128: 198, 256: 45. The GL desktop
holds 112 silhouettes after its `beginLevel` (591 more with its closure precache, M1.10a). In the
composer (`mode=bot level=1 frames=600`) the level start grows from 9.4 to 12.4 s (precache 5.2
to 8.1 s), `AS3D_LAZY_LOADS ... shadows=0` (no projected shadow appeared in another rotation in
600 steps), linear memory 24.0 MB free (0.8 MB less), VRAM unchanged (4193.6 KB; the 82 VRAM
fallbacks of that line were there before, 86 in M1.8).

Per frame after 120 bot frames: 18 shadows (16 projected, 2 planar) with 11 distinct silhouettes,
576 draped vertices, 18 draws per eye, 13.5 KB of arena. CPU in `prepare()`: 0.25 ms for the
shadows with the drape cache (1.9 ms the first frame, all 18 draped); the draw adds about 0.5 ms
of C3D CPU time (0.73 against 0.24 ms for the terrain alone). Late path (`precache=0`): frame 0
draws 18 blobs and generates one silhouette (64x128, 31.8 ms), frame 1 draws it with 17 blobs,
... frame 11 has all 18 silhouettes; the final image equals the precached one pixel for pixel. A
late frame costs the generation (3.5 to 47 ms here) plus, for a skin with alpha, its TGA reload
(about 20 ms): a visible hitch, hence the placements' precache.

Azahar check (2026-10-08, PPM dumps against `as3d_game --headless --level 1 --bot --frames 120
--size 400x240 --screenshot-every 120`):

- (a) terrain + shadows against `--render-only terrain,shadows`, `tools/3ds_screen_compare.py
  --direct --mask --darken shadowtest_t.ppm,terrain.png` (the new `--darken`: pixels more than 6
  below the terrain-only frame of the same side): 1337 darkened pixels against 1338, 1318 in
  both, IoU 0.971; mean abs error over their union 1.43/255; whole image 1.56/255 (the terrain
  alone: 1.56), 99.8 % within 8. The shapes, the palm fronds' partial alpha and the helicopter's
  planar shadow match; the residue is the terrain's.
- (b) the full frame against `--render-only terrain,models,particles,sprites,shadows`: 2.37/255,
  98.0 % within 8 (the env-mapped surfaces of M1.5). The composer at step 600 (`top_left.ppm`)
  against the same passes at 600: 1.77/255, 98.4 % within 8, as in M1.8 without shadows.

Deferred: the hardware run (GPU cost of the linear-memory A8 fetches, real generation times);
generating projected rotations ahead of need (e.g. the closure's in the background, a few per
frame, or a time budget inside the rasteriser) if the late hitch shows on hardware; ground marks
and skid trails (the neutral decal code is ready); freeing the CPU skins after the level start.

## M3.2 Ground marks and skid trails on the PICA (engine/src/render/pica/ground_marks_pica.cpp)

`pica::MarkPass` (ground_marks_pica.h) draws pass 4 of the frame, between the terrain and the
shadows, as the GL renderer. The geometry is the neutral one: `buildTerrainDecal` drapes each mark
(the GL rectangle: axis aligned, spawn origin plus the definition's bbox, never rotating; v flipped
back for the sequels' unflipped texture), `buildDecalRuns` gives the runs (`decalMaterialKey`: the
mark's blend, depth test on and write off, polygon offset `kPolygonOffsetDepth`, cull back, fog
colour from the blend), `buildSkidTrails` gives the trails as strips (Alpha, same state, per-section
alpha). Both become `FlatVertex` with the entity colour and alpha (marks) or white and the section
alpha (skids) as the vertex colour, so the draw is the existing `drawFlatRuns`: flat shader, TEV
texture x vertex colour, `applyMaterialState(run.key)` per run, one `C3D_DrawArrays` per run.

- Caching: the draped marks are kept until the set changes (entity slot and generation, colour,
  alpha, texture, blend) or a crater changes the terrain (`invalidate()`, like the shadow drapes:
  a re-drape follows the new heights, which the GL renderer does not do). A frame with an unchanged
  set costs one comparison of the descriptions. Skid trails are rebuilt every frame (their alpha
  fades with age). At most 16384 mark vertices are kept (400 KB of the 1.5 MB arena half); the
  scene already caps marks at 128.
- Arena: the first eye of an arena frame copies marks and skid vertices into the frame arena, the
  second eye reuses them (the M1.6 pattern). Textures are the `ResourceCache`'s, created outside
  frames: the mark skins by the level-start precache (`precacheObject` loads `def.skin` of every
  closure definition, mission 1: 6 mark definitions over 2 textures, `gfx\mark.tga` (the scorch, 10.6 KB) and `gfx\effects\expl1light.tga` shared by the 5 explosion light
  pools) and the skid textures from `def.skidMarks`; no extra hook
  was needed, and a texture first seen in `prepare()` is also created outside a frame.
- Neutral change: `WorldRenderOptions::marks` (default true) gates ground marks and skid trails;
  until now `options.sprites` did both. `as3d_game --render-only` takes `marks` and `skid` (both set
  the option); `sprites` no longer draws marks (the M1.6 sprite reference images that had a scorch
  in them now need `marks` as well). GL behaviour with the defaults is unchanged.
- `mode=markstest` (apps/3ds/source/mark_test.cpp, `frames=N` default 1400, `hold=`): mission 1 after
  N bot frames, mono dumps `markstest_t.ppm` (terrain), `_m` (terrain, marks, skid), `_s` (m with
  two synthetic skid trails: AirStrike 3D has none and the sequels' data is not on the card;
  `pica::setTestSkidTrails`), `_f` (full frame), `markstest_log.txt` with the per-frame counters.

Mission 1 at 1400 frames (Azahar): 1 mark (a scorch, 84 vertices = 28 triangles, 2.0 KB of arena,
10.6 KB of texture), 1 draw per eye, cut once in 0.30 ms and kept afterwards (0.02 ms of prepare
per frame); with the synthetic trails 2 trails, 44 segments, 92 vertices, 2 more draws, arena 4.1 KB.
Arena high water of the test 60 KB.

Azahar check (`tools/3ds_screen_compare.py --direct --mask --darken`, 400x240, against
`as3d_game --headless --level 1 --bot --frames 1400 --size 400x240 --screenshot-every 1400
--render-only terrain,marks,skid` and its `terrain` base): the scorch's darkened pixels 2103 on the
PICA against 2117 on the desktop, IoU 0.991, mean abs error over their union 0.99; the whole frame
differs by the water (not drawn yet: MAE 10.68, within 16 0.728, the same as the terrain-only
frames, 10.69 and 0.728, so the mark adds no error); full frame `terrain,models,particles,sprites,
shadows,marks,skid`: MAE 10.34, within 16 0.736 (the baseline of the water and the effects).
`mode=bot level=1 frames=600` is unchanged: map_pos 452.0, 39 entities, created 243, freed 204.

Deferred: water (7), lightning bolts and environment maps (done in M3.5/M3.3, environment maps M3.4); skid trails against a
desktop reference need the sequels' data (only the synthetic trails were drawn); the hardware run.

## M3.5 Water and M3.3 Lightning on the PICA (water_renderer_pica.cpp, lightning_renderer_pica.cpp)

The first game's water as the GL `WaterRenderer` draws it, and the lightning bolts. Both go through
the flat shader and `drawFlatRuns` (M1.6); nothing is created inside a frame.

- Water (`pica::WaterPass`, pass 7, after the opaque list, before the transparent list). The GL plane:
  one quad per 320 world units of map length (mission 1: 32 quads, 192 vertices), x 0..width*40,
  uv (0,0)-(width/160, 2) per quad, z = `computeWaterParams().planeZ` (level + sin t, t = time*pi*0.1),
  uv offset (0.4 sin t, 0.4 sin t/2), colour = sun*dir.z + ambient, alpha = the style's, alpha
  blend, depth test and write on, no culling, fog towards the level colour (`blendKey(Alpha, true,
  true, Off)`). The flat shader has neither a plane-z nor a uv-offset uniform, so the quads are
  rewritten into the frame arena by the first eye of an arena frame (z and uv already moved, colour
  and alpha in the vertex colour; 4.5 KB for mission 1) instead of using the terrain shader's detail
  offset: a few quads, no shader change. Each eye culls the quads with the GL's conservative
  clip-space test against its own matrix and draws adjacent visible quads as one run (5 of 32 quads,
  1 draw in mission 1). The texture (`gfx\water\water_lake2.tga`, 128x128) is created at beginLevel
  as an opaque RGB565 texture with mip chain (the plane's alpha is the style's: the GL ignores the
  texture alpha), after the terrain's, so it goes to VRAM while the budget has room: 42.6 KB, VRAM in
  mission 1 (`waterPicaStats().textureVram`). Sequel water (`WaterStyle::WaveGrid`: wave grid, two
  textures) is M5.1: `build()` logs a TODO and leaves the pass inactive.
- Lightning (`pica::LightningPass`, after the effect list, before the particles, as the GL).
  `prepare()` (outside frames) runs `buildLightningBolts` on the scene's bolts (at most 64 kept) into
  FlatVertex plus the run (additive, no depth test or write, no culling, fog towards black); the
  first eye copies them to the arena (288 B per bolt) and draws the run. `gfx\lightning2.tga` is the
  scene's cache texture (precached by the M1.10a closure). Mission 1 has no `Lightning()` call under
  the bot (it is the Lightning Bomb pick-up), so `pica::setTestBolts` (test hook, like
  `setTestSkidTrails`) supplies three synthetic bolts for the test.
- Neutral changes: `WorldRenderOptions::water` and `::lightning` (default true) gate pass 7 and the
  bolts in both backends (WorldScene collects no bolts without `lightning`); `as3d_game --render-only`
  takes `water` and `lightning`, and `terrain` alone no longer draws the water (until now it did, so
  the M1.4 to M3.2 desktop terrain references carried the water: M3.2's MAE 10.7 was that). The
  earlier 3DS test modes set `water = lightning = false` in their terrain-only phases.
  "Not drawn yet" at beginLevel then listed only the environment maps (drawn since M3.4).
- `mode=watertest` (apps/3ds/source/water_test.cpp, `frames=N` default 1400, `hold=`): mission 1 after
  N bot frames, mono dumps `watertest_t.ppm` (terrain), `_w` (terrain + water), `_l` (terrain + the
  synthetic bolts), `_f` (full frame), then 30 more bot frames and `_w2` (terrain + water), with
  `watertest_log.txt` (per frame: quads, drawn, draws, arena, planeZ, scroll offset, bolts, time).

Azahar check (2026-10-08, `tools/3ds_screen_compare.py --direct --mask`, 400x240, against
`as3d_game --headless --level 1 --bot --frames 1400 --size 400x240 --screenshot-every 1400
--render-only terrain,water`):

| frame | PICA phase | desktop list | MAE | within 16 |
|-------|------------|--------------|-----|-----------|
| 1400 before | terrain only | terrain,water | 10.15 | 0.743 |
| 1400 | w | terrain,water | 1.41 | 0.997 |
| 1400 | t | terrain | 1.43 | 0.999 |
| 1430 | w2 | terrain,water (--frames 1430) | 1.40 | 0.997 |
| 1400 | f | terrain,models,particles,sprites,shadows,marks,water,lightning | 1.95 | 0.982 |

On the 25244 pixels the water changes on the desktop: PICA terrain only MAE 34.3, with the water 1.16
(within 16: 0.992). The scroll: the logged offsets at time 23.333 s and 23.833 s are (0.3464, -0.2000)
and (0.3734, -0.2266), planeZ -67.134 and -67.066 (the same `computeWaterParams` as the GL); the
frame change 1400 to 1430 is MAE 15.6 on the PICA and 15.1 on the desktop, and the PICA's 1430 frame
matches the desktop's 1430 (1.40) and not its 1400 frame (15.35). Lightning (`l`): three bolts, 36
vertices, 0.84 KB, 1 draw, additive blue-white strokes on the terrain (no desktop reference: mission 1
has no real bolt with the bot; the geometry is the shared `buildLightningBolts`). Draws added: water 1,
lightning 1 per eye. Arena high water of the test 65 KB, 0 failures. `mode=bot level=1 frames=600`
unchanged: map_pos 452.0, 39 entities, created 243, freed 204.

Deferred: sequel water (M5.1: wave grid, two layers, animated per frame), environment maps (M3.4, done), a
real in-game bolt against the desktop (Lightning Bomb; needs a script that picks it up), hardware timing.

## M3.4 Environment maps on the PICA (shaders/env.v.pica, mesh_renderer_pica.cpp)

The GL draws an environment-mapped model in ONE pass (gles/shaders.cpp kMeshVertexSrc /
kMeshFragmentSrc; CPU reference as3d/envmap.h): the env coordinate per vertex, the skin and the env
texture combined in the fragment shader by mode, one blend (the material's). The PICA does the same in
one draw with two texture units; nothing else changes (blend, depth, fog by `applyMaterialState`, the
draw order of the lists).

- Condition, as GL: `Material::envTexture` set and valid, `envModeRaw` 1..3, and a smooth-normal
  mesh. Additionally (PICA only) a glitter or chrome material without a skin texture draws as a plain
  model (none in mission 1).
- `GpuMesh::upload` keeps a second stream for smooth-normal meshes: `pica::EnvNormal`, the folded
  normal |n| x 255 as u8 x4 (4 bytes per vertex, its own linear block), so `MeshVertex` stays 28 bytes;
  the GL coordinates only ever see |n| (render-pipeline.md 2.3). Mission 1 (the `mode=envtest`
  closure): 129 of the 130 meshes are smooth, 37.7 KB of normals, meshes 343.2 KB in all (was 0.30 MB:
  +12 %); the M1.10a closure at frame 1400 (`only=f`): 174 of 176 meshes, 50.7 KB of 460.9 KB.
  `meshMemoryStats()` reports `envBytes`/`envMeshes`.
- `shaders/env.v.pica` (`Shader::Env`): the mesh shader's position and light, plus texcoord1. Inputs v0..v3
  from the mesh stream, v4 the normal stream (`ShaderSet::setEnvVertices`, two attribute buffers).
  Uniforms per draw (`setEnvUniforms`): `mv` (rows 0..2 of view x model), `nmat` (3 rows), `envsel`,
  `envrot` (2 rows). Mapping of the GLSL:

  | GLSL (kMeshVertexSrc) | env.v.pica |
  |---|---|
  | `u = normalize(eyePos.xyz)` | `dp4` x3 with `mv`, `dp3`, `rsq`, `mul` |
  | `ne = uNormalMat * abs(aNormal)` (envNormalMatrix: inverse transpose of view x model, not renormalised) | `nmat` = the same matrix (CPU, per record), `dp3` x3 on v4 / 255 |
  | `r = u - 2 ne (ne . u)` | `dp3`, `add`, `mul`, `add` |
  | `m = 2 sqrt(r.x^2 + r.y^2 + (r.z + 1)^2)`, `st = r.xy / m + 0.5` | `q` by `dp3`, `max(q, 1e-12)`, `r.xy * 0.5 rsq(q)` (1/m without `rcp`) |
  | sequels (`uEnvView`): `st = 0.5 + 0.5 (mat3(uView) mat3(uModel) abs(aNormal)).xy` | `nmat` = mat3(view) x mat3(model), the same `dp3` |
  | both | `st = 0.5 + envsel.x ne.xy + envsel.y r.xy / m`: `envsel` (0, 1) sphere map, (0.5, 0) view normal (no branch) |
  | ENV_QUAD: rotation of st by time x 30 degrees about (0, 0) | `envrot` rows (c, -s), (s, c); identity rows otherwise |

  The view is the central camera view (GL `uView`) for both eyes: `MeshPass::setEnvironment(view, time,
  viewNormal)`, called by the world renderer with `wv.view`, the frame time and `RenderRules::envViewNormal`
  (MeshRenderer::begin passes its camera, `setTime`, `setEnvViewNormal`).
- TEV (tev.h, checked against `combineEnvMap` on a grid of texels in apps/tests/pica_core_test.cpp), TEXTURE0
  the skin, TEXTURE1 the env map, colour = PRIMARY (lit: the baked cube light, alpha 1) or CONSTANT
  (RF_NOLIGHTING: entity colour x tint):

  | mode | stage 0 | stage 1 |
  |---|---|---|
  | glitter `tevEnvGlitter` | rgba = skin x colour | rgb = previous + env (clamped as the framebuffer clamps), a = previous x env.a (the sequels: previous) |
  | chrome `tevEnvChrome` | rgba = env x colour | rgb = INTERPOLATE(skin, previous, skin.a), a = previous |
  | quad `tevEnvQuad` | rgba = env x colour | |

  All intermediates stay within [0, 1], so the per-stage clamps change nothing. The env texture is the
  material's `Texture2D` (the cache's, VRAM while the budget has room).
- `MeshPass::draw` switches between `Shader::Mesh` and `Shader::Env` per record in list order (the float
  uniforms are global registers: tint, mvp and cube again after a switch); `MeshDrawStats::envDraws`.

`mode=envtest` (apps/3ds/source/env_test.cpp): mission 1 after 120 bot frames (the M1.5 frame), the
WorldScene records through MeshPass with the world camera, no terrain: `a` the player and its
attachments, `b` the env records not on the player, `c` every record; `only=f frames=1400` instead draws the
full frame through the PICA WorldRenderer (M3.5's frame `f`). Mission 1 at frame 120: 5 env records, all
glitter or quad: the canopy `models\comanche\kabina2.tga` (glitter, 24 vertices), the two `points` help
items (glitter, 36), and the two shield shells (quad, skin `gfx\black.tga`, 176 vertices each: the orange
glow). Test a 7 draws (3 env), b 2 (2), c 35 draws (5 env), 1725 vertices, 57.4 KB; C3D CPU 1.41 ms for c
(1.42 ms in M1.5: the env records cost nothing measurable under Azahar). Full frame 1400: 49 mesh draws, 8 env.

Azahar check (2026-10-08), test c against `as3d_game --headless --level 1 --bot --frames 120 --size 400x240
--screenshot-every 120 --render-only models` (tools/3ds_meshtest_compare.py, all 3902 model pixels):

| | MAE | within 16 | interior (mask eroded 1 px, 2601 px) | half resolution (box 2x2, 1162 px) |
|---|---|---|---|---|
| M1.5 (no env) | 21.97 | 62.9 % | 27.22, 58.2 % | 17.62, 70.6 % |
| M3.4 | 5.79 | 81.2 % | 4.81, 84.5 % | 3.29, 93.7 % |

Over the helicopter (100..180 x 140..240, 2208 px): 35.0 / 42.5 % before, 6.97 / 75.0 % after; no
bias (mean signed difference -0.8, +0.2, +0.1). The residual is pixel-scale: the desktop reference is
rendered with 4x MSAA (apps/game/main.cpp `target.create(w, h, 4)`) and the shells are thin overlapping
additive layers with many inner edges and a high-contrast env texture; a one-pixel shift doubles the error
(12.2 to 19.4 MAE), the best of the 3x3 neighbourhood gives 88.3 %, the half-resolution comparison 93.7 %.
The within-16 target of 85 % over all model pixels at full resolution is therefore not met (81.2 %); the
MAE target (< 8) is. Full frame 1400 against `--render-only
terrain,models,particles,sprites,shadows,marks,water,lightning` (tools/3ds_screen_compare.py --direct --mask):
MAE 1.95 to 1.87, within 16 0.982 to 0.983 (the env surfaces are small in that frame). `mode=bot level=1
frames=600` unchanged: map_pos 452.0, 39 entities, created 243, freed 204.

Deferred: the front end's 3D banner model on the PICA (M2.1 draws the title text instead); a desktop
reference without MSAA (or the PICA comparison at 4x supersampling) to measure the env shading without
edge effects; hardware timing.
