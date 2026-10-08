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
  textures) is M5.1 (done: see "M5.1").
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

Deferred: sequel water (M5.1, done), environment maps (M3.4, done), a
real in-game bolt against the desktop (Lightning Bomb; needs a script that picks it up), hardware timing.

**Water on hardware (2026-10-08): large triangles lose depth precision on the PICA.** On the New 3DS,
mission 1 from about step 2000 (dump at step 2700) showed the plane over the terrain: the pale water
covered ground, roads and the compound, while the models stood out of it. Azahar and the desktop were
clean at the same step, and the console was clean at step 1400. The wash had straight screen-space edges
(a quad's diagonal across a cliff: washed on one side, clean on the other), so it was per triangle and
not per distance. The plane's triangles are half of a 1280 x 320 unit quad, with their vertices far
outside the screen. The console's triangle setup interpolates depth across them too coarsely, Azahar
computes in float. No corner was behind the camera (corner w 83 to 1348). The fix: each 320-unit quad is
cut into tiles of about 80 units (16 x 4), each culled against the eye with the same test, the texture
mapping unchanged (`pica::setWaterOptions`, `kDefaultWaterTile`). Mission 1: 5 of 32 quads, 220 to 234
tiles, about 1400 vertices and 33 KB of arena per eye, still 1 draw. Azahar `watertest_w` at 1400 is the
same picture as with the quads (MAE 0.00), 1.41 against the desktop as before. On the console at step
2700 (drop 1262d25), the MAE against the desktop frame went from 21.1 to 5.9, the rest being the HUD the
desktop draws on the same screen. The rule applies to every pass: a depth-tested triangle must stay small
in world units. The terrain (40-unit cells), the wave grid (per cell), marks, shadows and models are small.
The full-screen quads (scissored clears through `fillQuad`, the brightness pass, the 2D layer) are at a
constant depth or have no depth test, so they are not affected. Run.ini switches for hardware A/B
(README): `water_tile=N` (0 = the GL's quads), `water=0`, and `depthmap=z` (the world passes use the
Z-buffer mapping, with the fog table rebuilt for it).

Second rule (2026-10-08, after the owner's play session on drop 4): **never submit a water triangle
that crosses the near plane, and never one that is large on screen.** In the attract level behind the
menus (level 3, planeZ about -60, 8 or 9 of 32 quads visible), the corners of the visible 1280 x 320
quads reach w = -84, behind the camera. So each tile is now also cut against the eye's near plane on the
CPU: Sutherland-Hodgman against w >= 1.01 near + 0.01 (w is affine in the plane's x and y), the polygon
fanned into triangles, tiles wholly behind dropped. A tile whose part in front of the camera spans more
than 0.6 NDC (`kMaxTileSpan`; 2 = the whole screen) is split in four, at most 3 times
(`kMaxTileSplits`) and not below 10 units (`kMinTileSize`). The vertices of one eye are built in a CPU
vector and copied to the arena, still one draw. Azahar measurements:
- Attract level 3 (`mode=menu attract=3`, mono and stereo, 900 frames): 379 to 429 tiles (9 or 10 of
  them split). No tile was cut at the near plane: the smallest w drawn is 261. The negative corners
  belong to tiles of the visible quads that the cull drops. So the near cut is a guard for closer
  cameras; the attract level did not need it.
- Mission 1 bot run 600 to 2700, every 100 steps: against the GL quads (`water_tile=0`) MAE at most
  0.003, at most 3 pixels off by more than 16. No splits; the smallest w drawn is 257.
- The cut itself, with a test build cutting at w = 400 instead of the near plane (mission 1, step 1400):
  11 or 12 tiles cut each frame, smallest w drawn exactly 400. The water ends on a straight edge, with
  no gap or overlap between the cut tiles.
- A raised plane (planeZ + 0.5, a test build) changes only shoreline pixels (at most 250 per frame,
  MAE at most 0.34) and floods nothing. So the terrain away from the river is not within a fraction of
  a unit of the water: the step-2700 wash on the console was an error of several units, not a near tie.

The `pica water: draw N` line (every 300 draws, one per eye) logs: planeZ, the quads, the tiles (and
how many were split), the w range of the visible quads' corners, the tiles cut at the near plane, the
smallest w drawn (never below the near plane) and the largest tile span in NDC. The owner's play
session still showed a wash "a few seconds at a time". If it persists with this drop, the next A/B
switches are `water_tile=40` (smaller triangles) and `gpu_kick=0`. The second one tests the other
hypothesis: the wash covers the terrain but never the models, as if the terrain's depth were missing,
which a clear or a draw reordered by the mid-frame submission would also produce.

**GPU command buffer (2026-10-08).** Gulf Thunder mission 1 in stereo stopped under Azahar at bot frame
345 (step about 690): the log ended and the emulator kept running. citro3d's default buffer
(`C3D_DEFAULT_CMDBUF_SIZE`, 256 KB) was 93 % full on the frame before: 435 draws for the two eyes and both
screens. The next frame overflowed. libctru drops the commands that do not fit, the frame's end is never
signalled, and the next `C3D_FrameBegin` waits forever. The as3d stereo hardware run with up to 424 draws
was close to the same limit. The context now creates a 1 MB buffer (`graphics_context_ctr.cpp`,
`kCmdBufBytes`; 768 KB more linear memory), and `endFrame` warns (`ctr: GPU command buffer N % of 1024 KB
used in one frame`) at each new 10 % step of the high-water mark above 50 %. Gulf mission 1 in stereo now
runs 600 steps (map_pos 614.3, 119 entities, created 733, freed 614) and 3000 steps (map_pos 3240.8,
479 draws at the end, no warning).

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


## M2.5 Saves: the write path, the policy, the checklist (engine/src/core/fs_util.cpp, apps/3ds/source/ctr_save.cpp)

What is saved: one file, `sdmc:/3ds/openairstrike/<game>/profile.bin` (settings, high scores, unlocks,
sequel checkpoint) plus `launcher.bin` (the chosen game). High scores are inside the profile, so there is no
separate dirty flag: every profile write carries them.

**Audit of the path as it was (M2.1).** `saveProfileFile` already wrote `profile.bin.tmp` and
`replaceFile`d it (remove then rename when the rename over an existing file fails, as FAT does on the
console SD), and removed the temporary on a failed write or replace. What was unsafe or missing:

- no `fsync`: `fclose` alone was relied on to put the data on the card (see below);
- no directory creation on a save (only `userDataDir()` made `sdmc:/3ds/openairstrike`, `gameDataDir` the
  game directory; a deleted directory made every save fail);
- `writeLauncherChoice` and the import's `writeWhole` repeated the pattern without the flush, and
  `moveInto` left a half-copied temporary when `copy_file` failed;
- a temporary left by a write cut short by a power loss stayed on the card forever (the next save only
  overwrote it): the owner's "no `*.tmp`" check would fail after a power cut;
- a failing save (full card, card out) logged `cannot save the profile` on every attempt;
- nothing stopped the lid from closing in the middle of a write (sleep mode is entered at once);
- no log line to read on hardware.

**Now** (`as3d::writeFileAtomic`, shared by the profile, the launcher choice and the import writes):
`<dst>.tmp` in the same directory (the directory is made if missing; `makeDirectories` skips the device
root `sdmc:`) -> `fwrite` -> `fflush` + `fsync` -> `fclose` -> `replaceFile` -> the temporary removed on every
failure. `loadProfileForGame` removes a stale `<path>.tmp` before reading. `GameFlow::saveProfile` logs an
error only when the failure differs from the last one (a recovered save clears it).

**fsync on the SD.** newlib's `fsync()` calls the devoptab's `fsync_r`; libctru's `archive_dev.c` (the
`sdmc:` device) defines `archive_fsync`, which calls `FSFILE_Flush` (verified in the pinned image's
`libctru.a`: `archive_dev.o` has `archive_fsync` and the undefined reference `FSFILE_Flush`). Azahar:
`fsync(sdmc file) = 0` (`savetest_log.txt`). `fclose` then does `FSFILE_Close`. So when `AS3D_SAVE ... ok=1`
is logged, the temporary's data was flushed before the rename; the rename itself is an FS-service operation
on the FAT metadata. Limit, not removable from here: a power cut between the remove of the old file and
the rename (the FAT fallback path, which is the normal path on hardware) leaves `profile.bin` missing and
the complete `profile.bin.tmp`. The load does not recover it (it deletes the temporary): the profile
starts fresh, as the first run. The window is a few milliseconds and the guard below keeps the lid and
the HOME menu's sleep out of it; a power-off from the HOME menu is not an abrupt cut (the system closes the
app). A battery pull or a crash in the window is the only way in. Recovering the temporary at load was
considered and not done: it would resurrect a file whose write may not have completed on a card whose
`fsync` is unverified on hardware (the temporary is the newer, complete file only if `ok` was reached).

**Policy.**

| Event | What happens |
|---|---|
| level end, a score entered, Options left, game chosen, "Change game" | the front end calls `save()` (`engine/src/ui`), as on the desktop |
| HOME (APT suspend) and lid (APT sleep) in a menu or in play | `GameFlow::onBackground`: the in-game menu opens during play (the game comes back paused), then `saveNow()`; the world is paused, audio suspended, perf flushed |
| exit (START+SELECT, HOME "Close", the menu's Exit) | `saveNow()` after the loop (`ctr_menu_loop.cpp`), `GameStack::teardown` |
| power off from the HOME menu | the app is suspended first, so the suspend save already happened |
| `aptSetSleepAllowed` | `false` only inside a `FileWriteScope` (every `writeFileAtomic`), `true` after; installed in `ctr_main.cpp` through `as3d::setFileWriteHook`; HOME is never blocked (`aptSetHomeAllowed` untouched) |

A closed lid during a save is therefore refused for the few milliseconds of the write (libctru replies
sleep as not allowed to the OS while the flag is false) and the SLEEP hook itself saves
inside the hook, before the app yields. Only the menu mode has a profile; `mode=play level=N`, `bot` and `sim`
write none.

**Log.** Every profile write logs `AS3D_SAVE profile bytes=N ms=X ok=1` (`ok=0` on failure, with the
`cannot ...` error line before it). Beside it `apt: suspend|sleep|restore|wake up` lines. In `log.txt`.

**Azahar `mode=savetest`** (`source/ctr_save.cpp`, no game data needed; output `savetest_log.txt`,
`savetest_engine_log.txt`, files under `savetest/`): `fsync` returns 0; 50 saves with changed options and
scores, each read back and compared byte for byte (0 mismatches; 14 ms average, worst 17 ms on the
emulator, meaningless as hardware time); no `*.tmp`; the sleep hook ran 50 times and `aptIsSleepAllowed()`
was false inside each write and true after; a directory (non-empty) named like the target makes the save
fail with `cannot replace ...` leaving no temporary and the directory intact; a missing directory
`fresh/a/b` is created; a stale `profile.bin.tmp` is deleted at load; no `*.tmp` anywhere in the tree.
Azahar's `rename` replaces files (host `rename`), so it does not reproduce the FAT refusal: that path is
covered by the desktop test below and by the hardware checklist. Desktop: `apps/tests/fs_util_test.cpp`
(`writeFileAtomic` with the first rename forced to fail, a blocking directory, a destination under a
regular file, the scope hook counted, no device-root directory) and `launcher_test.cpp`.

**Hardware checklist (owner).** `mode=menu` (or `mode=play` without `level=`) in run.ini, the paks in place.

1. Change an option (e.g. Music volume), leave Options. `log.txt` shows an `AS3D_SAVE profile ... ok=1` line.
2. Press HOME, come back: the option is still set; `log.txt` shows `apt: suspend`, `AS3D_SAVE`, `apt: restore`.
3. Close the lid, open it: `apt: sleep`, `AS3D_SAVE`, `apt: wake up`; the option is still set.
4. Change a second option, HOME, and power off from the HOME menu. Reboot, start the game: the option persists.
5. Play a level to the end (or lose) and enter a high score: the score is in the table after a restart.
6. `ls` over ftpd in `sdmc:/3ds/openairstrike/as3d/`: `profile.bin`, no `profile.bin.tmp`; none in `/3ds/openairstrike/`.
7. Run `mode=savetest` once on the console: `PASSED: 0 failed checks`, and read the `AS3D_SAVE ... ms=` times
   and the `fsync` line in `savetest_log.txt` (the numbers that matter are the hardware ones; Azahar's are not).
   The "blocked target" check there is the only one that exercises the failing replace on real FAT.

Deferred: recovery of a complete `profile.bin.tmp` after a cut in the remove-rename window (see above;
decide with the hardware save times); co-op/shared config (todo list); the HOME-menu "Close" is assumed to end
`aptMainLoop` (M1.2) and is part of checklist step 4.

## M5.1 The sequels' render rules on the PICA (water_renderer_pica.cpp, shaders/water.v.pica)

The audit of the GL WorldRenderer and its renderers against the PICA ones (every `rules.` field of
as3d/render_rules.h and every `GameId` use under engine/src/render) found one sequel rule the PICA skipped:
the wave-grid water (`WaterStyle::WaveGrid`). It is drawn now; the rest was already on the PICA path:

| rule (render_rules.h) | where the PICA has it |
|---|---|
| `water` WaveGrid | new: `pica::WaterPass` grid path (below) |
| `terrainCentralNormals`, `tileHalfTexelInset` | neutral `terrain_build.cpp` (TerrainRenderer::build takes the rules) |
| `shadowEraseBelowGround`, `shadowHeightExpCap`, `shadowMaxSide` | neutral `shadow_raster.cpp` (ShadowRenderer::setRules) |
| `shadowMipmaps`, `shadowMultiply` (DecalLook::ShadowMultiply) | shadow_renderer_pica.cpp, `ShadowPass::setMultiply` (M3.1) |
| `spriteCulling` | `SpriteViewParams::cullBackFaces` in the PICA world renderer |
| `envViewNormal` | `MeshPass::setEnvironment`, `envsel` of env.v.pica (M3.4) |
| `missingTextureWhite` | `ResourceCache::setMissingTextureWhite` (shared) |
| `markEntityAlpha`, `markTextureUnflipped` | neutral WorldScene mark descriptions, MarkPass (M3.2) |
| `skidMarkPass` and the skid parameters | WorldScene collects the trails, `MarkPass::setSkids` with `skidTrailParams` (M3.2) |
| `ui2dHalfPixelShift` | the PICA Renderer2D honours `setHalfPixelShift` (batch2d.h); no front end turns it on, the desktop's neither: intentionally the same |
| `billboardTrueAxes` | both games true; the shared particle and sprite builders |

There is no other `GameId` switch in the renderers (the HUD tables are M5.3); particles, sprites, health bars,
lightning and the brightness pass are the same code for the three games.

Wave grid (`pica::WaterPass`, the GL grid path of gles/water_renderer.cpp and kWaterGridVertexSrc):

- Build (beginLevel, as the GL): `buildWaterSurface(terrain, true, the level's waterShine)`,
  `buildWaterGridGeometry` (8-row chunks over the whole map, two triangles per wet cell). The GL keeps
  the whole grid in one VBO (16-byte `WaterGridVertex`: column, row, terrain height, weight) and animates
  it in the vertex shader. The PICA has no sine in its shader, so the wave is computed on the CPU and only
  the height goes to the GPU: per chunk the vertices a wet cell uses are compacted (each chunk's range
  starts on a multiple of 4 vertices) into a static stream `WaterGridStaticPica` (x, y as floats, colour
  255, 255, 255, alpha = weight x opacity: 12 bytes) with u16 indices local to the chunk, both in linear
  memory for the level.
- Per frame (`prepare`, outside frames): `computeWaterGridFrame` and the two sines of the wave tabulated per
  row (`sin(0.75 r + T)`) and per column (`sin(c + T)`). Per eye: `visibleWaterChunks` with the eye's
  matrix; a visible chunk not yet animated in this arena frame gets its heights written into the frame
  arena (one float per vertex, animateWaterGrid's operations with the tables; the other eye reuses them),
  then one `C3D_DrawElements`. Craters: `WaterPass::update` refreshes the dry vertices' heights
  (`updateWaterGridHeights`, which the PICA world renderer now calls with the terrain's update).
  Self-check: the first 16 heights of every animated chunk are compared with `animateWaterGrid`
  (`maxHeightError`): 0 in every run.
- `shaders/water.v.pica` (`Shader::Water`, two streams: v0 xy and v1 colour static, v2 the height):
  position (x, y, z, 1) x mvp; texcoord0 = (pos.xy + worig) x wbase.xy + wbase.zw, texcoord1 the same with
  wshine. The GL computes 1.5 (c, r)/4 + offset and 2 (c, r)/4 + offset, i.e. 1.5/160 and 2/160 per world
  unit; per chunk `worig` = (0, -y0) and the y offset carries `offset + y0 s` without its whole repeats, so the
  coordinates stay below about 16 (float24 interpolation).
- TEV `tevWaterGrid` (tev.h, checked in pica_core_test): TEXTURE0 the base, TEXTURE1 the shine, rgb =
  INTERPOLATE(base, shine, base.a) = mix(shine, base, base.a), alpha = the vertex alpha. State as the GL:
  alpha blend, depth test and write on, no culling, the level fog (W-buffer mapping, the fog table of the
  frame). The base layer is RGBA8 when its alpha varies (water_ocean_1: RGBA4 would band the colour and the
  mix), RGB565 when opaque; the shine layer RGB565 (its alpha is not used). An absent shine texture is an
  8x8 black one (the GL's 1x1).
- Stats: `waterPicaStats()` grid fields (cells, chunks, wet cells, grid and used vertices, static and index
  bytes, both textures and their placement; per frame chunks animated, vertices, arena bytes, time, chunks
  visible, triangles, draws).

Numbers (Azahar, `mode=sequeltest`, 600 bot frames; the grid is 32 x 256 cells in all three missions, the GL
VBO 9504 vertices = 148.5 KB):

| mission | water | wet cells | used vertices (static) | indices | per frame (one eye sees) | arena per frame | CPU animate | textures |
|---|---|---|---|---|---|---|---|---|
| AirStrike 2 mission 1 | none (no `water` in levels.txt) | | | | | | | |
| AirStrike 2 mission 2 | water_ocean_1 + shine, level -68, 0.5 | 415 | 584 (6.8 KB) | 2490 (4.9 KB) | 4 chunks, 470 vertices, 696 triangles, 4 draws | 1.84 KB | 0.11 ms | base 256x256 RGBA8 341.2 KB + shine 256x256 RGB565 170.6 KB, VRAM |
| Gulf Thunder mission 1 | water_ocean_1 + shine, level -150, 0.5 | 1010 | 1416 (16.6 KB) | 6060 (11.8 KB) | 7 chunks, 770 vertices, 1154 triangles, 7 draws | 3.01 KB | 0.19 ms | the same |

So no cap or decimation is needed (far below the 20 KB per frame budget; the whole grid as 32-byte Flat2Vertex
would have been 24 to 47 KB per frame). The water costs 0.4 to 0.5 ms of C3D CPU time per eye under Azahar
(terrain + water 0.64 to 0.71 ms against 0.22 to 0.31 ms for the terrain alone).

`mode=sequeltest` (apps/3ds/source/sequel_test.cpp, `game=`, `level=`, `frames=` default 600): the end states
equal the desktop's `as3d_sim --game G --level N --frames 600 --bot`:

| mission | map_pos | list entities (max) | slots (max) | created / freed | kills | player origin, health, score |
|---|---|---|---|---|---|---|
| as2 1 | 452.0 | 103 (105) | 138 (142) | 402 / 299 | 0 of 2 | (708.1, 584.5), 400, 0 |
| as2 2 | 452.0 | 81 (96) | 177 (199) | 573 / 492 | 1 of 3 | (699.2, 563.7), 397, 120 |
| gulf 1 | 614.3 | 119 (123) | 248 (257) | 733 / 614 | 3 of 5 | (705.4, 748.2), 400, 1200 |

Azahar check (2026-10-08, `tools/3ds_screen_compare.py --direct --mask`, 400x240, against `as3d_game --game G
--headless --level N --bot --frames 600 --size 400x240 --screenshot-every 600 --render-only LIST`; whole image):

| mission | t (terrain) | w (terrain,water) | w2 (630 frames) | f (terrain,models,particles,sprites,shadows,marks,skid,water,lightning) |
|---|---|---|---|---|
| as2 1 | 1.44, 1.000 | 1.44, 1.000 (no water) | 1.43, 1.000 | 1.65, 0.995 |
| as2 2 | 0.99, 1.000 | 1.01, 1.000 | 0.99, 1.000 | 1.31, 0.994 (4 skid trails) |
| gulf 1 | 1.27, 1.000 | 1.27, 1.000 | 1.23, 1.000 | 1.72, 0.987 |
| as3d 1 (regression) | | 1.51, 1.000 | | 1.72, 0.994 |

(MAE /255, fraction within 16.) On the pixels the water changes on the desktop (terrain against terrain,water
differing by more than 8): as2 2, 41596 pixels, MAE 0.96, 99.9 % within 5 (the PICA terrain alone there:
16.3); gulf 1, 24231 pixels, MAE 1.01, 99.8 % within 5 (alone: 15.1). The 630-frame images match the desktop's
630 frame as well, so the waves and both scrolls move as on the GL. The as2 mission 2 full frame is the first
comparison with real skid trails (M3.2 had only synthetic ones). AirStrike 3D mission 1 through the same test
is unchanged (flat plane, 5 quads, end state map_pos 452.0, 39 entities, created 243, freed 204).

Memory after the level-start precache (the sequels' paks are 47 and 43 MB; M5.4 re-measures):

| mission | textures VRAM | textures linear | meshes (env normals) | terrain (linear vertices + indices; textures) | linear free |
|---|---|---|---|---|---|
| as2 1 | 72 = 4300.8 KB | 287 = 5175.1 KB | 227 = 1090.9 KB (116.8) | 248.9 + 4.2 KB; 11 = 1695.6 KB VRAM | 21303 KB |
| as2 2 | 66 = 4300.8 KB | 373 = 6565.1 KB | 265 = 1229.1 KB (127.2) | 296.9 + 4.6 KB; 13 = 1823.6 KB VRAM | 19708 KB |
| gulf 1 | 43 = 4300.8 KB | 260 = 6740.4 KB | 215 = 1216.8 KB (86.9) | 291.1 + 4.3 KB; 12 = 1791.6 KB VRAM | 19544 KB |
| as3d 1 | 104 = 4300.5 KB | 188 = 2296.9 KB | 176 = 460.9 KB (50.7) | 257.3 + 4.2 KB; 12 = 1727.6 KB VRAM | 24806 KB |

VRAM is full in all three (the 4.2 MB budget); the sequels put 5.2 to 6.7 MB of textures in linear memory
(the first game: 2.3 MB; their meshes are 2.4 to 2.7 times the first game's). The two water textures take 512 KB of VRAM because they are created at
beginLevel, right after the terrain's. Precache 12.6 / 15.8 / 11.8 s under Azahar (as3d 7.9 s), frame arena
high water 133 to 170 KB.

The sequels' paks are on the emulator's card under `sdmc:/3ds/openairstrike/as2/` and `gulf/` (pak0, pak1,
pak2 and for Gulf Thunder pak4, Settings.xml and the texts file), copied from the install.

Deferred: the sequels' front ends (M5.2), their HUD tables on the 3DS (M5.3), the game selector and the
memory re-measure (M5.4); the third missions of each game and the hardware run; whether the water textures
should yield VRAM to the models (RGBA4 base, or created after the precache) if hardware shows the
linear-memory skins.

## M4.2 Simulation hot spots (engine/src/game, engine/src/script)

Goal: a cheaper `World::step` for the Old 3DS 60 Hz simulation (the M1.0 hardware run: mission 1
at 4.1 ms per step at 268 MHz without the speed-up, mission 14 about 8 ms), with **zero behaviour
change**: integer, container, lookup and allocation changes only; no float operation reordered or
retyped.

**Tools.** `as3d_sim --time` prints one `time:` line (mean, median, p99 and worst step in ms, the
level load) and one `phases:` line (mean ms per step of each phase of `World::step`: camera,
spawn, player, entities, collisions, after; `World::setPhaseTiming`, measurement only).
`tools/sim_bench.sh` prints the table below for the standard set (missions 1, 6, 14 of each game
and the three heaviest of each, 3600 bot frames, best of 3 runs); `tools/sim_bench.sh --all 600`
sweeps every mission. The heaviest missions by that sweep: as3d 20, 19, 16; as2 3, 14, 8;
gulf 23, 18, 7 (the sequels' busy missions cost 2 to 2.5 times as3d 14).

**Profile (desktop, before).** `perf` is not usable on this machine (`perf_event_paranoid` 4) and
valgrind is not installed, so the profile came from a SIGPROF sampler preloaded into `as3d_sim`
(interrupted PC symbolized with addr2line, library samples attributed by symbol and caller;
level loads included, about 10 % of the samples). Self time, as3d 1/6/14 (16.4k samples) and
as2 3/8/14 + gulf 7/18/23 (36k samples):

| symbol | as3d | sequels |
|---|---|---|
| `ScriptThread::decode` (RCSL operand decoding) | 17.9 % | 21.7 % |
| `World::runEntities` (the RT_THOUGHT sweep of all 4096 slots) | 8.7 % | 4.4 % |
| `World::touchEntity` (list walk per toucher) | 2.9 % | 9.9 % |
| `ScriptThread::runInvocation` | 4.7 % | 5.5 % |
| `BuiltinReport::noteCall` + its string compares | 5.2 % | 5.1 % |
| `ModelData::findTag` + `asciiEqualCI` (tag lookup per child per frame) | 4.0 % | 3.8 % |
| `World::setupTransform` + sinf/cosf | 6.1 % | 5.3 % |
| `ParticleEmitter::update`, `WorldParticles::update` | 4.0 % | 4.5 % |
| `World::think`, `computeScreenBounds`, `projectPoint`, `Terrain::heightAt` | 9.0 % | 7.2 % |
| `Entity::f`/`setF` calls (out of line) | 1.6 % | 2.5 % |
| level load (file syscalls, parsing) | ~10 % | ~4 % |

Allocations: 28 mallocs per step on as3d 14, 47 on as2 3, 50 on gulf 23 (per-step entity lists,
spawned threads).

**Changes, in order, with the desktop step time after each** (as3d 14 / as2 14 / gulf 23, mean ms
over 3600 bot frames, best of 5, all binaries run round-robin in one session):

| commit | change | as3d 14 | as2 14 | gulf 23 |
|---|---|---|---|---|
| 38bc62d | baseline (`--time` added) | 0.104 | 0.228 | 0.255 |
| b4215ab | RT_THOUGHT cleared on the slots that thought (list kept by `think`), not a 4096-slot sweep; `freeRemoved` scratch reused | 0.100 | 0.221 | 0.245 |
| f558fc0 | touch pass: candidate snapshot kept between touchers until a touch handler runs | 0.095 | 0.202 | 0.218 |
| 2620829 | builtin report: per-thread cached entry index (`BuiltinReport::entryFor`/`noteCallAt`) | 0.088 | 0.191 | 0.201 |
| effda39 | `attachToTag`: the child keeps its tag lookup (model pointer + tag name as the key) | 0.088 | 0.185 | 0.199 |
| 6989490 | `Entity::f/setF/v3/setV3/flagBits` inline | 0.077 | 0.172 | 0.181 |
| 01cb19b | entity lists without allocations (particle scratch, `World::EntityListScope`) | 0.076 | 0.168 | 0.182 |
| c431b9a | VM `decode` inline, operands resolved in place, role table | 0.066 | 0.142 | 0.154 |
| 4cfc421 | `setupTransform`: last angles-to-axis result reused while the angle bits are unchanged | 0.062 | 0.139 | 0.148 |
| 6019de1 | hashed builtin and global name lookups at thread binding | 0.062 | 0.138 | 0.147 |
| 1e49c37 | player index only for player globals; `decodeEntityAddr` inline | 0.058 | 0.130 | 0.137 |
| b006d85 | VM operand helpers forced inline, frame window handled in place | 0.050 | 0.115 | 0.120 |

Mallocs per step after: 11 (as3d 14), 30 (as2 3), 29 (gulf 23); what remains is the spawn path
(a script thread per spawned entity: frame, stack, binding tables, path strings).

**Standard set, before and after** (`tools/sim_bench.sh`, desktop x86-64 RelWithDebInfo, mean ms
per step; the before/after columns from an interleaved run of both binaries, best of 5):

| game | mission | before | after | after / before |
|---|---|---|---|---|
| as3d | 1 | 0.057 | 0.026 | 0.45 |
| as3d | 6 | 0.063 | 0.029 | 0.46 |
| as3d | 14 | 0.116 | 0.055 | 0.48 |
| as3d | 16 | 0.092 | 0.043 | 0.46 |
| as3d | 19 | 0.113 | 0.055 | 0.49 |
| as3d | 20 | 0.121 | 0.058 | 0.48 |
| as2 | 1 | 0.152 | 0.073 | 0.48 |
| as2 | 3 | 0.174 | 0.081 | 0.46 |
| as2 | 6 | 0.129 | 0.062 | 0.48 |
| as2 | 8 | 0.167 | 0.083 | 0.49 |
| as2 | 14 | 0.247 | 0.123 | 0.50 |
| gulf | 1 | 0.133 | 0.064 | 0.48 |
| gulf | 6 | 0.218 | 0.105 | 0.48 |
| gulf | 7 | 0.241 | 0.113 | 0.47 |
| gulf | 14 | 0.235 | 0.118 | 0.50 |
| gulf | 18 | 0.285 | 0.129 | 0.45 |
| gulf | 23 | 0.273 | 0.129 | 0.47 |

Per phase (gulf 23, ms): entities 0.210 to 0.103, collisions 0.042 to 0.010, after (particles,
render pass) 0.017 to 0.016.

**ARM caveat.** These are x86-64 numbers. The biggest single desktop win (the VM `decode`
rewrite) removed store-forwarding stalls that are an x86 effect; on the ARM11 that change saves
the call and copy overhead only. Conversely the memory-bound changes (the 4096-slot sweep, the
touch list walks, the tag string compares, the per-call report search, software sinf/cosf) should
gain more on the Old 3DS (16 KB L1, no L2, slow FCRAM, newlib libm) than here. A uniform scaling
of the desktop ratio would put mission 14 near 4 ms at 268 MHz, but that is not a measurement:
the owner's hardware run (`mode=spike` or `mode=bot` step times, with and without the speed-up) is the
number to use. Expected order: 25 to 45 % less on the ARM11.

**Proof of identity.** `as3d_sim --dump-state` after 3600 frames, before (38bc62d) and after,
for all 62 missions of the three games, in three configurations: `--bot`; `--pilot --god
--upgrades 3 --items 5` (more weapons, more collisions); `--bot --players 2 --seed 7
--difficulty 3`. All 310 dumps and the summaries (which list the builtin call counts and the
unknown globals) are byte-identical. New tests: the report cache follows report changes
(`script_test`), a touch handler's removal is seen by the next toucher in the same pass
(`collision_test`; it fails if the snapshot is not invalidated), nested `EntityListScope`s
(`world_test`); ci.sh (goldens, precache_test, world_test) passes for as3d, as2 and gulf.

**Memory.** `Entity` grows by the tag cache and the axis cache: 840 to 960 bytes on x86-64,
about 95 bytes more per slot on the ARM11, so about 0.4 MB for the 4096 slots (M4.4 budget).

**Left as is, and why.**
- `ParticleEmitter::update`, `computeScreenBounds`, `projectPoint`, `Terrain::heightAt`, the
  rest of `setupTransform`: float arithmetic; any restructuring would risk the operation order.
- `freeTree` walks the list once per freed entity to detach AttachEntity children; a skip when no
  list entity has a parent was measured and gave nothing (attached entities are common), so it
  was dropped.
- The spawn path (one `ScriptThread` per spawned entity: 2 KB frame, stack reserve, binding
  vectors, path normalisation and `std::map` lookups of the program and model): about 20
  allocations per step on the busy sequel missions; a thread pool is the next step if the
  hardware run shows malloc time.
- TerraMorph's stamp lookup is a short linear string compare on a rare call (no stamps in as3d;
  a few in the sequels): not on the per-step path.
- `DefDatabase::find*` lowercases its key into a `std::string` per call (hashed since M1.10a):
  a few calls per step.

## M4.1 Quality profile (apps/3ds/source/quality_profile.{h,cpp}, engine/src/render/pica/quality_pica.h)

The Old 3DS has the New 3DS's GPU but a 268 MHz ARM11 without L2: the CPU side is the problem
(M1.0, M4.2). The profile is the list of things it gives up; the simulation is never one of them.

`QualityProfile` is chosen at the start of both loops (`ctr_loop.cpp`, `ctr_menu_loop.cpp`) from
`APT_CheckNew3DS`, or from run.ini `quality=auto|new|old` (`auto`, the default, detects; `new` and
`old` force a preset, to test the Old profile on a New 3DS or in Azahar). It is applied after the
graphics context exists and before the level starts, and logged once:

    AS3D_QUALITY profile=old hardware=new renderHz=30 particleCap=768 spriteCap=384 blobShadowsOnly=1 terrainDetail=0 stereoAllowed=1 prerenderedMusic=1

| field | New 3DS | Old 3DS | where it acts |
|---|---|---|---|
| `renderHz` | 60 | 30 | the loops: `C3D_FrameRate(30)`, two sim steps per drawn frame |
| `particleCap` | 1536 quads | 768 | `particle_renderer_pica.cpp` (`pica::renderQuality()`) |
| `spriteCap` | 768 quads | 384 | `sprite_renderer_pica.cpp` |
| `blobShadowsOnly` | no | yes | `world_renderer_pica.cpp`: no silhouette is generated (level start or late), every shadow is the blob over the model's bounds |
| `terrainDetail` | yes | no | `terrain_renderer_pica.cpp`: the detail texture is not loaded (170 KB less) and the terrain draws with the 1-stage TEV `tevModulateTexColor` |
| `stereoAllowed` | yes | yes | the loops: with false `info.eyes` stays 1. Kept true on the Old 3DS: the slider decides (two eyes double the GPU work; the owner leaves it at 0 if 30 Hz is not held) |
| `preferPrerenderedMusic` | no | yes (informational) | M2.4 already plays `music/*.ogg` when present, the module otherwise; on the Old 3DS ship the Ogg files (libopenmpt on 268 MHz does not fit) |

The PICA knobs live in `pica::RenderQuality` (`quality_pica.h`, `setRenderQuality`), a process-wide
setting whose defaults are the New 3DS values, so the tests and probes are unchanged. Only
`engine/src/render/pica/` and `apps/3ds/` changed; no neutral or desktop file.

The loop change. The simulation stays a 60 Hz fixed step, one `input.frame()` per step, audio
untouched (`audio.afterStep` per step). `C3D_FrameRate(30)` makes `C3D_FrameBegin(SYNCDRAW)` wait for
every second vblank, so one iteration of the loop is 33 ms and the clock accumulator yields two
steps. The bounded catch-up goes from 2 to 4 steps per drawn frame at 30 Hz (the same two
frames' worth of slack, `QualityProfile::maxCatchUp()`); a longer stall is still dropped. perf.csv
keeps its columns and gets a last one, `sim_steps` (steps run before that frame was drawn); `step_ms`
is the sum of those steps, so at 30 Hz a frame's `step_ms` is two steps' time, and the report's
per-frame step limit is then a per-two-steps limit.

Azahar evidence (New 3DS emulation, `mode=bot level=1 frames=600 perf=1 dump_at=600 brightness=0.5
slider=0 audio=0`, same emulator, timings only relative):

| | `quality=new` | `quality=old` |
|---|---|---|
| log | `profile=new ... renderHz=60 particleCap=1536 ...` | `profile=old ... renderHz=30 particleCap=768 spriteCap=384 blobShadowsOnly=1 terrainDetail=0` |
| displayed frames for 600 steps | 302 | 204 |
| `sim_steps` histogram | 2: 299, 1: 2, 0: 1 | 2: 104, 3: 4, 4: 95, 0: 1 |
| end state | map_pos 452.0, 39 entities, created 243, freed 204 | the same |
| shadow silhouettes at level start | 126, 818 KB, 2699 ms of generation | 0 |
| precache | 8120 ms | 5284 ms |
| level start (mission and renderer) | 12402 ms | 9477 ms |
| textures VRAM / linear | 4193 KB (101) / 3179 KB (209) | 4193 KB (98) / 2190 KB (85) |

Azahar does not keep real time (a displayed frame took 50 ms there, the 30 Hz target is 33 ms), so
the clock asks for 3 to 4 steps in about half of the old-profile frames: on hardware the steady
state is the 2 of the first column. The dropped silhouette generation (2.7 s here, the same figure
M3.1 measured) is most of the 2.9 s saved at level start.

Top-screen dump at step 600 against `quality=new`: mean absolute error 6.13 / 255, 29 % of the pixels
differ by more than 8. What differs: the terrain, which loses the detail texture's high-frequency
grain (the old profile's ground is smoother and a little blurrier, the base texture is 256 pixels
per 32 rows); the shadows, which are the blob (a soft round patch over the model's footprint,
no helicopter silhouette). Models, sprites, HUD and fog are identical, and the particle and sprite
caps did not bite in this frame (the caps only matter in heavy explosion scenes).

Not verified here: the Old 3DS hardware setting of Azahar (`is_new_3ds=false` in `qt-config.ini`)
with `quality=auto`. The config is shared with another agent's runs and the change was not allowed
from this session; `selectQuality(Auto, false)` is the only line that differs, and the same preset
was exercised with `quality=old`.

Hardware checklist for the Old 3DS (the owner has none yet; a New 3DS runs the same code paths with
`quality=old` in run.ini):

1. `mode=play level=14 perf=1 quality=old` (or `mode=bot level=14 frames=3600 perf=1 quality=old`):
   `log.txt` shows the AS3D_QUALITY line, no `silhouettes` in AS3D_SHADOWS, no ERROR.
2. perf.csv: `sim_steps` is 2 in steady state; the `frame` period p95 near 33 ms
   (PERF_SUMMARY frame_ms); `step_ms` per frame under 33 ms with room (two steps; M4.2 tables the per-step cost).
3. The end state of `mode=bot level=1 frames=600` is unchanged (map_pos 452.0, 39 entities,
   created 243, freed 204).
4. Look: ground without grain, blob shadows, 30 Hz motion; the slider at 0 and then up (stereo at
   30 Hz: GPU p95 under 33 ms?). If not, set `stereoAllowed=false` in `oldProfile()`.
5. Music: with `music/*.ogg` on the card the module path is not used (`log.txt` audio lines).
6. On a New 3DS, `quality=old` must show the same look and `sim_steps` 2; `quality=new` is the
   default there.

Deferred: M4.3 (ETC1 texture cache, which cuts the 3.5 to 5 s precache and its VRAM/linear split),
M4.4 (heap and linear budgets per profile: the Old 3DS has 64 MB APPLICATION memory in the
default mode against 124 MB on the New one; `RenderQuality` has no memory fields yet).

## M5.2 The sequels' front ends on the 3DS (ctr_frame.cpp drawModel, the pad gaps, the hint panel)

The sequels' front end already ran through the M2.1 seam (`mode=menu game=as2|gulf`: Settings.xml and
`texts_as2.txt` / `texts_gulf.txt` from the game's directory, the sequels' screens of engine/src/ui). What was
missing or broken, and what changed:

- **The model view** (helicopter selection, `FlowView::drawModel`) was a no-op. The composer now calls
  `Frontend::drawModelViews` between `drawUnder` and `drawOver`, as GameView does, and `CtrFlowView::drawModel`
  prepares it for the **top screen** (prepare, outside the frame): the parts (`apps/game/model_view.h`, now shared
  with GameView: `modelViewParts` walks the object tree as before, `entityMatrix` the view's angles and origin) in a
  `ResourceCache` of its own (released with the renderer at every level start, so mission VRAM is unchanged), one
  `pica::MeshPass` with the GL camera (origin, identity view, the view's 60 degrees and planes 1..1000), the default
  `SceneLighting` (ambient cube, the GL MeshRenderer's W-buffer mapping and linear fog 650..900, none at the
  model's distance) and the environment inputs of a fresh GL MeshRenderer (identity view, time 0, sphere map).
  Per eye: viewport and scissor over the rectangle, depth cleared inside it, `picaEyeProjection` of that view
  (stereo: zero parallax at the model's distance 100, eye offset as the world's). The desktop draws the view on its
  bottom screen inside the virtual rectangle (170, 160, 460 x 270) at 0.4 px per virtual pixel; the top screen
  shows it at 0.8 (368 x 216 px, centred), the world is not drawn while it is up, and the 2D layer around it is
  what the desktop has there: the selection's dot grid (`drawModelViewBackdrop`, Gulf Thunder's scan lines under
  it) and, over the model, the panel's static fill (`drawModelViewStatic`, the noise `panel()` multiplies the scene
  with; `as2::staticFill` is now shared). The bottom screen keeps the whole selection panel (name, arrows, bars;
  the grid without the model).
- **Pad mode gaps**: on the helicopter selection Left / Right are the arrows whatever has the focus (the
  arrows are pictures, Up / Down only reached them one by one); the name entry's touch keyboard is a grid for the
  D-pad (Left / Right along a row, Up / Down between rows, the name above, Ok below, Del right of the name) and a
  pressed key keeps the focus in pad mode. Everything else (main menu, Start Game with the mission list on
  Left / Right, Options without Configure controls, Information pages, dialogues, in-game menu, Mission Complete,
  Game Over, Top Scores) worked by pad as it was.
- **The hint panel** (`HintStyle::SequelPanel`) ignored `hintScale`: the bottom tables of both sequels now have
  1.5, `layoutHint` scales text, line pitch and box, and with a scale above 1 puts the Ok button above the touch
  band (y 472, 30 high, `kHudBottomBandTop` 510) and the panel above the button (its bottom frame 18 below the box)
  with its title tab inside the screen; the interactive screen (`SequelScreens::hint`) uses that layout, its Ok
  button finger-sized in touch mode. The desktop's layout is unchanged (scale 1, button at y 520); test "hud
  layout: the sequels' hint panel on the bottom screen keeps its Ok button above the touch band".
- **menu_bot's clock**: the bot stepped on every world step of a mission, also under the sequels' start dialogue
  (which pauses the world), so its N-th step was not `mode=bot`'s. The loop now counts the running steps (the
  world's steps but those the front end holds: start dialogue, in-game menu, end screens; hint boxes count, they
  pause the world in `mode=bot` too) and the bot flies only those. New run.ini keys for scripted runs:
  `menu_bot_steps` (end state after N running steps), `end_at` / `end_kind` / `end_score` (the mission ends as
  EndLevel or the last life would, player 1's score set: the name entry), `unlock_heli` (a fresh sequel save has
  helicopter 0 only, `as3d_sim`'s default is 1), `ui_fixed_dt` (the menus' clock at 1/60 s per frame, as
  `--ui-script`); the log gets `AS3D_FOCUS` on every focus change and the intro pages by number (`intro1` ...).
- Desktop tests (game_flow_test.cpp, for the game under test when it is a sequel): "the sequels' front end by pad,
  model view and mission 1 as --level 1" (pad from the main menu through Options, Start Game, the selection with
  Left / Right, Start, the dialogue by Enter; then the 3DS loop's clock: 600 running steps equal a direct start's
  600 bot frames with the same helicopter: map_pos, time, entities, created / freed, score, player) and "the
  sequels' end screens by pad" (in-game menu, Mission Complete -> Quit, Game Over -> Quit -> the name entry typed
  by the D-pad -> Top Scores -> main menu).

Model view numbers (Azahar, `ui_fixed_dt=1`, the selection open 40 frames: angles (104.86, -80.00, 0)):
AirStrike 2 `player_1` 4 parts, 2209 vertices, 938 triangles, 4 draws of which 2 environment-mapped, parts loaded
in 282 ms on first use (8 to 11 ms again, from the cache); Gulf Thunder `player_1` 2 parts, 880 vertices, 627
triangles, 2 draws (1 env), 261 ms. Against the desktop's `as3d_game --game G --dual-screen --size 800x960
--headless --touch --attract 1 --no-logo --ui-script` (bottom screen at 0.8 px per virtual pixel, the same
taps and frame; the desktop's view crop (216, 608, 368 x 216), the 3DS top (16, 12, 368 x 216), the desktop's panel
text, arrows and bars left out), `3ds_screen_compare.py --direct --mask`: AirStrike 2 0.95 (99.0 % within 16),
over the model's pixels only 5.1 (92.8 % within 16); Gulf Thunder 2.53 (96.4 %), model 6.0 (86.8 %); the rest is
the noise's random offsets, the scan lines' phase and RGBA4 skins. Before the static fill the model was 1.41
times as bright as the desktop's (the panel's multiply), model MAE 20.5. With `slider=1` the view is drawn per eye
(`eyes=2 eye_offset=4.00 focal=100.0`).

Main menus (bottom screen against the desktop's dual-screen bottom half, crop 40, 240, 360, 480, frame 40):
AirStrike 2 0.84 (98.2 % within 16), Gulf Thunder 2.64 (95.4 %; its scan lines); the first game's 4.13 had the
3D banner on the desktop's bottom screen, the sequels have none.

State sequences (Azahar, pad_script only but the touch Skip of the comic and the logo pages' A):

    as2 (logo=1, unlock_heli=1, menu_bot=1, menu_bot_steps=600, end_at=1100 complete): intro1 (logo) -> intro2..5
    (comic pages 1 to 4, A x4 speed, Skip tapped on page 4) -> main (230) -> options (330) -> main (400) -> start
    (460; focus list, difficulty, mode, Back, Next) -> heli (600; Right, Left, Right: player_1, player_2 models) ->
    Start -> "Mission 1: Tutorial" -> dialogue (800) -> playing (976) -> hint (1125, closed by the bot) -> playing ->
    AS3D_BOT_END 600 steps -> ingame (1500) -> Resume -> playing -> end_at -> dialogue (end) -> complete (1783)
    as2 (logo=0, end_at=700 gameover): main -> start -> heli -> dialogue -> playing -> ingame -> playing -> gameover
    -> Quit -> main (score 0: no name entry); with menu_bot=0 the hint stays: hint (670) -> A -> playing
    gulf (logo=0, end_at=300 gameover, end_score=60000): main -> start -> heli -> "Operation 1: Recon" -> playing
    (no start dialogue in mission 1) -> gameover (452) -> Quit -> name (560; D-pad: A, B, Up, A) -> scores (690)
    -> B -> main (780); and main -> options -> main -> start -> heli -> playing -> ingame -> Resume -> playing -> gameover

Mission 1 started from the menus with `menu_bot=1 menu_bot_steps=600` and the helicopter of M5.1 (`unlock_heli=1`,
Right on the selection): AirStrike 2 map_pos 452.0, 103 entities (138 slots, max 142), created 402 / freed 299,
player (708.1, 584.5), 400; Gulf Thunder 614.3, 119 (248, max 257), 733 / 614, score 1200, kills 3 of 5, player
(705.4, 748.2), 400: M5.1's end states exactly. With a fresh save's helicopter 0 (the desktop test's numbers):
AirStrike 2 452.0, 101 entities, 396 / 295; Gulf Thunder 585.9, 79, 944 / 865, score 400.

Found, not fixed (M5.1's): Gulf Thunder mission 1 with `slider=1` stops under Azahar after a few hundred frames, in
`mode=bot level=1` as in the menus (the log ends, the emulator keeps running); mono runs and AirStrike 2 missions 1
and 2 in stereo are fine. Deferred: the first game's name entry keyboard by D-pad (its keys are linear for Up /
Down), the name entry and comics on hardware, the stereo model view checked by eye on the console.


## M5.4 Game selector and memory budgets (apps/3ds/source/ctr_selector.cpp, ctr_install.cpp, ctr_mem.cpp)

**Selector.** `ctr_main.cpp`: with the front end (`mode=menu`, or `mode=play` without `level=`) and no
`game=` in run.ini (or `game=select`; `RunConfig::gameGiven`), `runCtrFrontEnd` (ctr_selector.cpp) runs
instead of `runCtrMenuLoop` alone:

1. `detectInstalls` (ctr_install.cpp, plain file checks): for `as3d`, `as2`, `gulf` under
   `sdmc:/3ds/openairstrike/<key>/`, the paks of the profile, `Settings.xml` and the texts file.
   Complete = none missing. The log gets `selector: <key> installed (N files)`, `selector: <key>
   incomplete, missing: a, b` (WARN) or `selector: <key> not installed`.
2. One complete game and no part-installed one: no selector, that game starts (`selector: one game
   installed (...), skipped`), unless START is held when the app starts (then the selector shows
   with its one card). Otherwise the selector: `ui::GameSelector`, the desktop's own, on the bottom
   screen through the PICA `Renderer2D` at the 320x240 mapping (4:3 mode: everything inside the
   800x600 field, three cards of 94 px; text as small as in the menus), the font and HUD atlases
   from the first game whose paks are all there, the sequels' marquees from their own pictures
   (`gfx\logo\logo.tga`, `clouds`, `glow`, `two3`, mounted from each game's paks for the time the
   selector is up), the first game's banner mesh replaced by its title in text (the mesh renderer is
   not wired here). Input as in the menus (`CtrInput::uiInputs`: D-pad = arrows, A = Enter, B / START =
   Escape = Exit, touch = pointer). The top screen shows the focused game's title, version and the
   keys (or its missing files in red). Cards of a game with files missing show `Missing: <first>`
   and "and N more", and cannot be started (`selector: <key> cannot start, missing: ...`).
3. The choice is written with `writeLauncherChoice` (the desktop's atomic write) to
   `sdmc:/3ds/openairstrike/launcher.bin`; the next start focuses it (`readLauncherChoice`).
4. `runCtrMenuLoop` runs with the selector's graphics context and `CtrInput` (so the pad_script clock
   is continuous; `CtrLoopOptions::context/input`), `FlowConfig::changeGame` set when two or more
   games are complete. "Change game" (the main menu's last item) ends the loop (`front end: Change
   game (profile saved)`), the GameFrame, pica backend and stack are torn down, and the selector
   shows again. Dumps of the later phases are prefixed `<prefix>g<phase>_`; the selector's
   `<prefix>sel_<nn>_<key>_{top,bottom}.ppm` (`dump_screens=1`, 12 frames after each focus change).

With `game=KEY` in run.ini nothing of this runs. Not exercised under Azahar: the one-game skip, START
held at boot and a part-installed card (the single SD card is shared with other runs, so no game
directory was moved away); those paths are the three-line decision in `runCtrFrontEnd` and the
`cardLines` text.

**Azahar (2026-10-08, `mode=menu logo=0 attract=1 menu_bot=1 report_at=600 dump_screens=1 slider=0`, no
`game=`, no launcher.bin; pad_script RIGHT, A, taps on Start Game / Mission 1 / Next / Next, A for the
dialogue, START, tap Quit, tap Change game, RIGHT, A, START+SELECT):**

| step | log |
|---|---|
| boot | `selector: as3d installed (5 files)`, `as2 installed (5 files)`, `gulf installed (6 files)`; `AS3D_SELECTOR focus=as3d`; selector dumped (three cards, as3d focused, top screen "AirStrike 3D, Version 1.70") |
| RIGHT, A | `selector: chose as2 after 70 frames`; `AS3D_MEM ... when=selector` heap 73 KB (selector torn down) |
| as2 front end | attract level 1 loaded, `AS3D_SCREEN name=main` with items 1..5 and `60` (Change game); main menu dumped |
| taps | `start` (frame 229), `heli` (349), `AS3D_LEVEL_LOADED mission=1 "Mission 1: Tutorial"`, `dialogue` (469), `playing` (735) |
| mission 1 | `report_at: mission 1 after 600 bot steps`: **map_pos 452.0, 101 entities, created 396, freed 295** |
| START, Quit | `ingame` (1629), the attract level loaded again, `main` (1689) |
| Change game | `front end: Change game (profile saved)`, `stopped after 1829 frames`, the stack torn down (heap 125 KB), `selector: ... installed` x3, the selector with as2 focused (launcher.bin) |
| RIGHT, A | `selector: chose gulf after 199 frames`, gulf's attract level, `AS3D_SCREEN name=main` (gulf's main menu), START+SELECT: `quit: START+SELECT` |

The as2 mission 1 end state through the menus is map_pos 452.0 as `as3d_sim --game as2 --level 1
--frames 600 --bot`, but 101 entities and 396 / 295 created / freed against 103 and 402 / 299: the
campaign start differs from the sim's (opening dialogue, the helicopter and loadout of the start
screens, the seed); the direct start (`mode=bot level=1`) is identical (table below). The bot's
clock stops during the sequels' opening dialogue (`ctr_menu_loop.cpp`), so that "600 bot steps" means
the same mission frames as the sim; before that fix the end state at 600 loop steps was map_pos 265.8
(the world had been held by the dialogue).

**Memory counters.** `ctr_mem.cpp`: `AS3D_MEM heap_kb= heap_hw_kb= linear_free_kb= linear_tex_kb=
linear_geom_kb= vram_kb= vram_fallbacks= when=start|end|selector|exit game= mission=` (heap = mallinfo
`uordblks`, the high-water sampled every 32 frames and at each line; linear free = `linearSpaceFree`;
linear textures = `pica::textureStats().linearBytes`; geometry = live `GpuMesh` vertex+index bytes plus
the terrain's vertex+index and the water's static+index buffers; VRAM = the texture budget's VRAM
bytes; fallbacks = textures the budget sent to VRAM whose `vramAlloc` failed, cumulative per
process). `start` is logged at the end of the level start (after the precache), `end` when the next
level start or the app's end tears the level down. `PERF_SUMMARY heap_kb` is unchanged.

**The nine missions** (`mode=bot game=G level=N frames=600 slider=0 perf=1`, quality auto = new,
Azahar, mono, 2026-10-08; the sim columns are `as3d_sim --game G --level N --frames 600 --bot`, all
nine identical: map_pos, list entities, created / freed):

| mission | level start | heap start / end / hw (MB) | linear free (MB) | linear tex (MB) | geometry (MB) | VRAM (KB) / fallbacks | end state = sim |
|---|---|---|---|---|---|---|---|
| as3d 1 | 10.8 s | 9.2 / 14.8 / 14.8 | 23.4 | 3.1 | 0.7 | 4193 / 81 | 452.0, 39, 243 / 204 |
| as3d 6 | 12.5 s | 9.5 / 15.7 / 15.7 | 22.1 | 4.2 | 0.9 | 4193 / 122 | 452.0, 67, 241 / 174 |
| as3d 14 | 10.4 s | 9.4 / 15.9 / 15.9 | 22.9 | 3.4 | 0.8 | 4193 / 111 | 452.0, 85, 348 / 263 |
| as2 1 | 15.5 s | 11.9 / 18.2 / 18.2 | 19.9 | 6.0 | 1.3 | 4193 / 129 | 452.0, 103, 402 / 299 |
| as2 6 | 15.9 s | 12.4 / 19.6 / 19.6 | 18.8 | 6.8 | 1.5 | 4215 / 146 | 452.0, 133, 408 / 275 |
| as2 14 | 17.2 s | 12.7 / 20.3 / 20.3 | 16.9 | 8.6 | 1.7 | 4193 / 149 | 452.0, 169, 717 / 548 |
| gulf 1 | 15.2 s | 11.0 / 17.4 / 17.4 | 18.2 | 7.5 | 1.5 | 4192 / 138 | 614.3, 119, 733 / 614 |
| gulf 6 | 15.3 s | 11.4 / 18.4 / 18.4 | 17.8 | 7.7 | 1.6 | 4192 / 125 | 614.3, 197, 1385 / 1188 |
| gulf 14 | 15.0 s | 11.1 / 17.7 / 17.7 | 18.0 | 7.6 | 1.6 | 4193 / 123 | 614.3, 190, 1371 / 1181 |

(level start = beginLevel + UI + precache; heap in MB of 1024 KB; the exact KB values are in the
`AS3D_MEM` lines.)

Budgets of the plan (heap 40 MB, linear textures 14 MB, static geometry 6 MB): **no mission is over
any of them.** The largest values: heap high-water 20.3 MB (as2 14, half the budget), linear textures
8.6 MB (as2 14, 62 %), geometry 1.7 MB (as2 14, 29 %). Linear free never goes below 16.9 MB with the
32 MB linear region (the arenas included). No texture failures (`failures 0`), so no top-10 list is
needed. Flags for M4.4:

- **VRAM fallbacks are not zero in any mission** (81 to 149 textures the budget wanted in VRAM that did
  not fit: VRAM is full at 4.2 MB from the terrain on, they live in linear memory); their cost is
  GPU read speed, not memory. The sequels have more of them and 2 to 5 MB more linear textures than
  the first game.
- **The heap grows 5.6 to 7.6 MB between level start and the 600th step** in every mission
  (as3d 1: 9.2 to 14.8 MB; as2 14: 12.7 to 20.3 MB). Possibly bot mode loads its sounds lazily from the paks
  into the heap (`audio=1`; the precache report says `sounds=0`: the direct mode has no precache of
  sounds), and the world's pools fill; which of the two dominates was not split here. The growth
  stops at the level's end (the growth check below shows the next level starting from the same
  heap), but it is the number M4.4 must budget for the Old 3DS's 40 MB: at as2 14 it is 20 MB, so
  there are about 19 MB of margin.
- The level start is 10 to 17 s (precache 8 to 14 s) under Azahar: M4.3's cache.

**Growth check** (`mode=menu game=as3d logo=0 attract=1 cycle=1,6,1,6,1,6,1,6 cycle_frames=240`;
`ctr_menu_loop.cpp`: mission 1, leave to the attract level, mission 6, leave, four times): the
per-level numbers are identical from the second cycle on, to the KB: mission 1 starts at heap 14026
KB and ends at 13629 KB every time, mission 6 at 14197 KB and 14293 KB, the attract level at
12898 to 13230 KB; linear free 23903 / 22638 / 27198 KB the same each time. **Heap high-water after
the first mission 6: 14923 KB; after the fourth: 14925 KB (+2 KB; the limit was 1 MB): no leak in
level teardown.** The first mission 1 starts 238 KB higher than later ones (first-use caches). The
scripted Change game from as2 to gulf above also returns the heap to 118 to 125 KB between games.
Caveat: the cycle starts missions with `GameFlow::startMission` and leaves with `loadAttract`, the
host calls the menus make, not by tapping the menus.

Verification: `tools/3ds_ci.sh` green (compile-only, no desktop code changed: desktop `ci.sh` not
run). The three games' first three missions at the M3 numbers are the table above (the direct-mode
end states equal the sim).

Deferred (M4.4 and later): heap split by subsystem (sound cache, world pools, pak indexes) for the
Old 3DS's 40 MB; the selector's first-game banner mesh (marquee) and the logos' memory (the
selector holds about 0.5 MB of pictures while it is up); START-held and one-game paths and the
incomplete-install card on Azahar with a scratch SD card; hardware run on the 3DS.

## Hardware tuning, round 1 (M3.6/M3.7, the M1 gate)

The first console runs (drop c9623f7, New 3DS, `mode=bot level=1 frames=3600`): frame avg 39.3 ms
stereo / 36.7 mono, cpu_ms (citro3d's FrameBegin..FrameEnd) 19.4 / 18.0, GPU 13.3 ms for two eyes
(9.7 p95 mono), load 10.2 s. The CPU side was the whole problem.

**Instrumentation** (kept; see apps/3ds/README.md for the columns and keys):
- perf.csv `submit_ms, wait_ms, end_ms, after_ms, sync_ms` and `PERF_SUMMARY split_ms`.
- `AS3D_PASS_MS` every 300 frames (engine/src/render/pica/pass_profile.h): CPU ms, draws and
  vertices per pass and frame, the 2D batch build and conversion apart (ui_batch, ui_copy).
- `AS3D_LOAD phase=...`: session (mount, defs, world, level, pak reads), terrain (base generation,
  texture creation, geometry), precache scripts / world (texture creation split mip / swizzle /
  upload, meshes, shadows, loading frames, pak reads, decode and parse) / UI, level start.
- `AS3D_LATE_SHADOW` names a silhouette the precache missed.
- run.ini switches for A/B console runs: `passes=`, `hud=`, `touch=`, `depthmap=w|z`, `pace=`,
  `gpu_kick=`, `end_flush=`, `pak_cache_kb=`.

**What the console said** (drop 6cad453, mono, `gpu_kick=1 vsync=lazy`): frame 23.9 ms (was 36.7),
submit 17.3 ms of which the touch band 13.4 (buildBatch2D tessellating the 48-segment circles
11.0, conversion 2.6); every world pass 0.1 to 1.5 ms. Stereo: submit 19.1, the second eye about
1.8 ms. Load: session 1.5 s (defs 0.6, level 0.8), terrain 1.0 s (base generation 0.48), script
precache 1.65 s, world precache 5.75 s (texture creation 1.0 of which mip chains 0.76; shadows 1.06;
reads + decode + parse 3.5).

**Changes:**
1. Overlap (`gpu_kick=1`, pica_frame.h kickGpu): citro3d starts its GX queue only at
   C3D_FrameEnd, so the GPU idled while the CPU recorded. After each eye the command list is split
   (flushed) and citro3d's queue is started (its context's first member; libctru gxqueue.c then
   starts every later list as it is added). Every split flushes itself (the clears too) and the
   frame ends with C3D_FrameEnd(GX_CMDLIST_FLUSH): the arena is flushed by range before each split
   (LinearArena::flushPending), every other GPU buffer when written, so the whole-heap flush goes.
   gpu_ms then only times the tail after C3D_FrameEnd: the GPU gate is measured with gpu_kick=0.
2. Frame cadence (drop 3, ctr_loop.h FrameClock): the console runs of drop 2 showed frames with 0
   then 2 simulation steps (mono with normal vsync: 82 / 3248 / 145, 6 cadence changes a second;
   the owner saw stutter): the tick accumulator (1/60 s) beats against the 59.83 Hz panel. Now
   the vblank is the clock: each frame waits for the next tick of citro3d's frame counter
   (C3D_FrameSync, then C3D_FrameBegin without SYNCDRAW) and the next frame's steps are the
   vblanks between the last two of those waits (rounded from the system ticks; at most
   maxCatchUp, the rest dropped). The simulation runs 0.3 % slower than 60 Hz, as the original on
   a 59.83 Hz display. `pace=auto` (default): 60 Hz, and 30 Hz with exactly two steps per frame
   after 3 missed vblanks in 120 frames (stereo on drop 2 missed 6.5 %), back to 60 Hz after 240
   frames whose GPU wait + recording + end + max(GPU tail, one step + prepare) stayed under 14.5
   ms; `PERF_SUMMARY steps_per_frame` and `frames_at_30hz`, `AS3D_PACE` at each change. The
   `vsync=lazy` experiment is gone (it made the cadence worse). Azahar: 597 of 599 frames with one
   step at 60 Hz, 297 of 298 with two at pace=30.
3. 2D vertex cache (renderer2d_pica.cpp): the converted vertices of the last 6 lists of each
   Renderer2D stay in linear buffers of their own; an unchanged list (the touch band, the top list
   on the second eye, a static HUD) is drawn from them without building the batch. Azahar: touch
   24 ms to 0.7 ms.
4. Redundant citro3d state: blend, depth test, cull, alpha test, fog colour and depth map go
   through shadow copies (pica::detail::c3d*), Texture2D::bind skips the texture already bound.
5. Load: texconv swizzle per format with tables (byte-identical, checked on random images in every
   format), mip levels converted in place without a chain of copies; the terrain base blend
   tabulated per height (the same double operations); the TGA decoder writes true-colour rows in
   place; paks read with FSFILE_Read at an offset (timed), an optional block cache
   (`pak_cache_kb=`, off: on Azahar it read four times the bytes for few hits); the closure's
   projected shadows precached at step 0 (mission 1's late ruins_big, lazy_loads 1 -> 0).

Azahar, stereo, drop 2: frame 16.9 ms (vblank bound; was 49.9), submit 7.7, load 7.8 s (was 10.8),
images unchanged (MAE 0.1, the perf text line), sim end state unchanged.

**Load: reads and shadows** (drop 4). The console's drop-2 load (7.3 s) was 3.9 s of read latency:
about 5 to 7 ms per FSFILE_Read whatever its size (precache_scripts 295 reads for 495 KB in 1.5 s;
precache_world 335 reads for 5.3 MB in 2.4 s), then 1.0 s of shadow rasterisation.
- `Vfs::prefetch(paths)` (as3d/vfs.h, pak_source.cpp): the pak entries are sorted by offset and read
  in runs (gaps up to 64 KB read through, runs up to 2 MB), kept until `dropPrefetched`; the data
  is the same as without. DefDatabase reads its obj/wpn/ps files ahead (the session's defs: 106
  reads -> 28 on the first start).
- The read plan (ctr_loop.h ReadPlan, run.ini `prefetch=`): a level start records the paths it
  reads (Vfs::setReadObserver) into `sdmc:/3ds/openairstrike/cache/<game>_m<N>.plan` (names only;
  `_fe` / `_attract` for the front end's starts); the next start of the level prefetches them right
  after the mount (direct modes: GameOptions::afterMount, before the definitions). Mission 1: 519
  files, 7.0 MB, read in 38 runs (8.3 MB with the gaps); the rest of the level start then makes 12
  file reads.
- The silhouette cache (shadow_pica.h, run.ini `shadow_cache=`): `cache/<game>_m<N>.shadows` holds
  each silhouette's alpha texels, bounds and sizes under a 64-bit hash of everything the raster
  depends on (positions, faces, coordinates, the skin's alpha, kind, rotation, quality, sun, the
  rules' shadow fields, the side cap); a hit skips the raster (mission 1: 144 silhouettes, 746 KB).
  Same texels: the Azahar dumps with and without are identical (MAE 0).
Azahar, second start of mission 1: load 5.7 s (was 7.8; Azahar reads at about 5 MB/s, so the 1.7 s
prefetch there says little about the console), shadows 2.4 s -> 0.3 s, script precache 0.35 -> 0.09
s. The first start of a level (no plan, no shadow file yet) costs what it did before.

**Load: left for later** (the owner, 2026-10-08: a loading screen is acceptable, the feel comes
first). Not done:
- The first start of a level (no read plan yet): read the precache set's files ahead before the
  precache, the models first, then their meshes loaded to learn the textures the models name, then
  the textures, particle textures and sounds (a `WorldScene::prefetchPrecacheSet` called at the
  start of the PICA `WorldRenderer::precache`, before the placements' shadows); the scripts
  directory is one contiguous 418 KB region of pak0 (339 files) and could be read ahead whole
  before `World::precacheScripts`. Mission 1's first start read 289 of pak0's 766 models (4.1 MB
  spread over 12.7 MB), so whole-directory reads do not pay for the models.
- The definitions' parsing (about 0.5 s) and the terrain base textures (0.45 s, a candidate for
  the SD cache next to the silhouettes).

**Console numbers of drop 4** (20d61f3, New 3DS, `mode=bot level=1 frames=3600 perf=1 audio=1`):
- Load: the first start 9.1 s (records the read plan; the silhouette cache write took 2.1 s, an
  fsync'd 746 KB file), the next starts **2.9 s** (prefetch of the plan: 541 files, 7.8 MB in 41
  reads, 1.15 s; level start 1.06 s; shadows from the cache 84 ms). The M1 load gate (≤ 3 s) is met
  from the second start of a level.
- Mono, `pace=60` (hw15): 97.3 % of frames carry one step (1=3413, 2=93, 0=0); prepare 1.67 ms,
  recording 4.28, GPU 7.26 avg / 9.06 p95. The 93 double frames came once a second (gaps of 53 to
  59 frames) with no column spiking before them: the log / perf.csv flush to the SD card on the main
  thread. Mono with the default `pace=auto` (hw12) fell to 30 Hz on those misses (and the first
  start's cache writes) and stayed there: 1658 of 1805 frames at 30.
- Stereo, `pace=60 gpu_kick=0` (hw16/hw17): GPU 13.48 avg / 16.46 p95 with RGBA8 targets, 13.39 /
  16.16 with `top_format=rgb565` (no gain: the stereo cost is vertices and texturing, not fill);
  28 % double frames at 60 Hz (1=2041, 2=779). Stereo locks to 30 Hz for now (below).

**Drop 5** (the last of this round):
1. No SD write on the main thread during play (ctr_perf.cpp): the log lines and the perf.csv rows go
   to memory buffers; a writer thread at the lowest priority (0x3F, core 0) writes them about once a
   second while the main thread waits for the vblank or the GPU; `Perf::flush` (suspend, exit) and
   `close` write on the calling thread.
2. The pacer (ctr_loop.cpp updatePacer): one eye starts at 60 Hz, two eyes at 30 Hz (a slider move
   switches); a miss (a frame shown for 2+ vblanks at 60 Hz, 3+ at 30) does not count during the
   300 frames after a start or a level load, nor when the frame waited for a write on the main thread,
   a dump or a late load; 3 counted misses in 120 frames at 60 Hz go to 30 Hz; at 30 Hz, 120 frames
   in a row (doubled, up to 3600, after a fall back within 600 frames) whose CPU path (one step +
   prepare + recording + end) stays under 13 ms and whose GPU time (first command list to the end of
   the queue) under 14 ms go to 60 Hz. `AS3D_MISS` logs the first 40 counted misses with the columns
   of both frames; `PERF_SUMMARY pace` gives changes, misses and excused misses per rate.
   `pace=60` / `pace=30` fix the rate.
3. The bottom screen's 2D lists split (ctr_frame.cpp): the HUD, the touch band and the front end's
   menus each a Renderer2D, so the band stays in the 2D vertex cache while the HUD changes (Gulf on
   Azahar rebuilt 600 to 1600 vertices a frame before).
4. Shadows drawn one draw per silhouette texture (shadow_renderer_pica.cpp): the shadows grouped by
   texture in the order each texture first appears (every blob in one draw, the copies of one
   model's silhouette in one). Shadows write no depth and darken by a factor, so the order changes
   only the 8-bit rounding of the blends. Azahar stereo: as3d mission 1 156 -> 144 draws per frame,
   Gulf mission 1 319 -> 288; dumps at step 600 within MAE 0.16 of drop 4, sim end states equal.

5. The owner's wash in the tutorial (drop 4 play session: while a hint box is up the water covers
   terrain and roads, never the models; no bot run shows it). Suspect: the GPU's depth offset or
   fragment state out of step with what the renderer believes. Two causes made safe:
   applyMaterialState skipped its depth offset when it equalled its own tracked value, while
   setDepthMapping (offset 0) and the terrain's tile overlays (the bias) change the GPU's offset
   behind it, so a polygon offset could be lost or kept (since M1.3); it now always applies it (the
   depth map's shadow copy drops a true repeat) and fillQuad restores the offset the GPU has
   (pica::appliedDepthOffset). And the shadow copies of the fragment state and the texture
   bindings (drop 2) are forgotten at every frameBegin and drawOn, so each frame and target issues
   its state at least once: a mismatch cannot outlive a frame. No state call bypasses the copies
   (every C3D_AlphaBlend / DepthTest / CullFace / AlphaTest / FogColor / DepthMap goes through
   render_state_pica.cpp). Azahar `mode=play level=1` (no input, 750 frames): top and bottom dumps
   equal to drop 4's; no hint box opens in that window in the direct mode, so the wash itself is not
   reproduced: the console run in the tutorial with a hint up is the check.

**What is next:**
- Console: mono with the defaults should now hold 60 Hz (target ≥ 99 % one-step frames); stereo a
  steady 30 Hz (2 steps per frame).
- Stereo at 60 needs the GPU under about 14 ms for two eyes: terrain about 15.5k vertices for both
  eyes (a coarser LOD for the far rows), shadows (silhouettes packed into A8 atlas pages, one draw
  per page: pages aligned to power-of-two cells keep the mip levels apart; about 100 -> 10 draws in
  stereo), opaque + transparent meshes about 12k vertices.
- The sequels' CPU (Gulf mission 1 on Azahar: prepare 8.9 ms, recording 17 ms): profile prepare's
  parts; sort the opaque list by texture and material (A/B for equal-depth ties); merge small
  repeated effect meshes into the arena in submission order; merge a mesh's surfaces that share a
  texture at upload.
- Load: the first start of a level (see "Load: left for later"); the silhouette cache write
  (2.1 s with the fsync) could go to the writer thread.
