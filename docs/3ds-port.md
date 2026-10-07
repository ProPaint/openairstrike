# Nintendo 3DS port

Working notes of the port (plan: M1.0 onwards). The feasibility spike is in
[3ds-spike.md](3ds-spike.md); its app is [apps/3ds](../apps/3ds/README.md).

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
3. Start the Homebrew Launcher, press Y, and from this machine `apps/3ds/build.sh netload
   192.168.1.133` (builds, then 3dslink). Or copy `as3d_spike.3dsx` to `sdmc:/3ds/` and start it.
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

### Results on hardware

New 3DS, `osSetSpeedupEnable(true)`: hardware: pending (headless step before/after: pending).
`vramSpaceFree` before anything: hardware: pending.

All times in ms.

| variant | GPU avg | GPU p95 | CPU avg | CPU p95 | frame avg | frame p95 | VRAM free MB | linear free MB | indices/frame |
|---|---|---|---|---|---|---|---|---|---|
| A | hardware: pending | | | | | | | | |
| B | hardware: pending | | | | | | | | |
| C | hardware: pending | | | | | | | | |
| D | hardware: pending | | | | | | | | |
| E | hardware: pending | | | | | | | | |
| F | hardware: pending | | | | | | | | |
| G | hardware: pending | | | | | | | | |
| H | hardware: pending | | | | | | | | |

Decisions the numbers feed (to fill in after the run): VRAM or linear textures; RGB565 or
RGBA8; whether stereo 3D is affordable (D minus C); the vertex budget (E); the cost of one
full-screen blended layer (F per quad); whether mipmaps pay (G minus C); whether the bottom
screen may be drawn with citro3d every frame (H minus C).
