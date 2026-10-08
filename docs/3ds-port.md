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
