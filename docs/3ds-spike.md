# Nintendo 3DS feasibility spike (2026-10-07)

Question: can this engine be ported to the Nintendo 3DS? A bounded spike, not a port. It
answers three questions with measurements; the app is in [apps/3ds](../apps/3ds/README.md)
(build, Azahar and hardware instructions there).

**Verdict: feasible.** The simulation compiles and links for the 3DS without changing a single
engine file. It runs missions 1, 6 and 14 with exactly the desktop results, in about 9 MB of
memory. citro3d draws the mission's terrain and the helicopter mesh, textured, with the
engine's camera. What stays open is CPU speed on real hardware. The emulator cannot measure
it, and the estimate below says the Old 3DS is tight for busy missions and the New 3DS is
comfortable. One run on the owner's New 3DS settles it (command at the end).

## Setup

- Toolchain: devkitARM r68 (arm-none-eabi-gcc 16.1.0), libctru 2.7.0, citro3d 1.7.1,
  picasso 2.7.2, all from the pinned Docker image
  `devkitpro/devkitarm@sha256:116afba8…0bbf5` (no native devkitPro on this machine).
  Flags: `-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -O2 -std=gnu++17
  -fno-exceptions -fno-rtti -ffp-contract=off`, linked with `--gc-sections`.
- Emulator: Azahar 2125.1.1 (flatpak), New 3DS mode and Old 3DS mode, JIT, 100 % clock.
- Data: the AirStrike 3D paks (pak0/1/2.apk, 24 MB) on the emulated SD card, mounted
  through the engine's own `openFileStream` + `makePakSource` (stdio reads, nothing preloaded).
- Desktop reference: `as3d_sim --level N --frames 600 --bot` from the same commit.

## Question 1: does the platform-independent core compile and link?

**Yes, with no source changes.** `core`, `vfs`, `formats`, `script` and `game` (52 files) build
from `engine/src` as they are. `input_actions.cpp` (the test pilot) builds too, through a
one-line include wrapper, because the rest of the input module uses SDL scancodes.

| What needed changing | Result |
|---|---|
| `std::filesystem` (game_data.cpp) | Nothing. devkitARM's libstdc++ has it over libctru's devoptab. `identifyPaks()` and `detectGames()` run on the SD card: the paks identify as `as3d` by content. |
| Exceptions, RTTI | The engine already builds with `-fno-exceptions -fno-rtti` |
| Threads | None used by these modules |
| 64-bit math, alignment | No problem seen. The `ByteReader` reads are bytewise. |
| Floating point determinism | Same results as x86-64 for all three missions (see question 2), with `-ffp-contract=off` as on desktop |
| `<3ds.h>` name clashes | None: only the spike's own files include `<3ds.h>`. libctru's global `u8`/`u32` typedefs coexist with `as3d::u8`/`as3d::u32`. |

Warnings: 47, all `-Wformat`, in 7 files (37 in `world_dump.cpp`). On arm-none-eabi,
`uint32_t` is `long unsigned int`, so `%u` with an `as3d::u32` warns. This is harmless on ARM
(int and long are both 32-bit), and a real port would cast or use `PRIu32`. No other warning.

Sizes (spike with the citro3d scene; engine code not referenced by the spike is dropped by
`--gc-sections`):

| | bytes |
|---|---|
| `.3dsx` | 506 536 |
| `.text` | 430 880 |
| `.rodata` | 29 792 |
| `.data` | 7 612 |
| `.bss` | 20 284 |
| `.tbss` | 3 080 |

A clean build takes 47 s on 8 cores inside Docker.

Further modules (compile check only, `-fsyntax-only` with devkitARM): `ui` is 24 of 25 files
portable. Only `renderer2d.cpp` includes GLES. `render` is 11 of 24 files portable (camera,
material, scene lighting, render rules, resource cache, envmap, health bar, png, the shader
sources). The 13 others call GLES directly.

## Question 2: does a mission run at speed within memory?

Times below are emulated system ticks (`svcGetSystemTick`, 268 111 856 Hz), **not hardware
times**. Azahar's JIT advances the tick counter by roughly one tick per executed ARM
instruction, without cache, memory or VFP latencies. So a tick here is closer to "one
instruction" than to "one cycle": treat these figures as an instruction count. Old and New
3DS modes gave the same numbers to within 0.1 ms, because the emulator does not model the
clock difference. Memory figures are real: they are the engine's own allocations.

| | mission 1 | mission 6 | mission 14 |
|---|---|---|---|
| level load (emulated) | 914 ms | 911 ms | 908 ms |
| step, average (emulated) | 2.57 ms | 3.03 ms | 4.57 ms |
| step, worst (frame 0: the level's first spawns) | 50.2 ms | 134.3 ms | 72.3 ms |
| steps over 16.7 ms / over 33.3 ms | 3 / 1 | 6 / 1 | 24 / 6 |
| list entities, peak | 44 | 70 | 91 |
| slots, peak | 81 | 135 | 232 |
| heap in use after load | 6.52 MB | 6.54 MB | 6.52 MB |
| heap in use, peak during the run | 6.73 MB | 6.81 MB | 6.98 MB |
| matches `as3d_sim --bot` (map_pos, entities, created/freed, score, kills, player position, health) | yes | yes | yes |

Fixed costs: mounting the three paks takes 58 ms and 0.12 MB (pak directories). Loading the
definitions takes 590 ms and 1.5 MB. A mission adds 4.9 MB, and freeing the world returns the
heap to 1.65 MB. The malloc arena's high-water mark over the whole run is 7.74 MB.

The determinism result is worth noting. After 600 frames on all three missions, the ARM11
build (hard-float VFP, newlib libm) ends in the same state as the x86-64 desktop build:
map_pos 452.0, entities 39/67/85 (max 44/70/91), created 243/241/348, freed 204/174/263,
scores 0/100/400, kills 0/1/2, player (644.5, 534.8), health 400/400/374. Recorded input
scripts and the golden traces should carry over.

Memory budget: **the spike fits in 64 MB with a wide margin.** The peak is about 7 MB heap,
plus 0.7 MB linear memory for the drawn scene, plus 0.5 MB of code. Azahar gave the app a
91.5 MB heap in New 3DS mode and 63.5 MB in Old 3DS mode, with a 32 MB linear heap in both.
Even a full renderer with all of a mission's textures in linear memory should stay well
under the Old 3DS's 64 MB (see "What a real port needs"). `osGetMemRegionUsed/Free` returned
nonsense under Azahar (used 124 MB of a 96 MB region), so the heap figures come from
`mallinfo()`.

Speed, estimated for hardware (not measured):

- On the desktop (x86-64, RelWithDebInfo), mission 14 steps in 0.73 ms on average.
- The emulator's 4.57 ms on mission 14 is about 1.2 M instructions per step. On the ARM11
  MPCore (in-order, 16 KB L1, slow FCRAM, no L2 on the Old 3DS), code like this (std::string
  and map lookups, script VM dispatch, VFP floats) probably runs at 2 to 3 cycles per
  instruction.
- That gives about 9 to 14 ms per step on the Old 3DS (268 MHz) for a busy mission.
  Mission 1 would be about 5 to 8 ms.
- On the New 3DS at 804 MHz with its L2 cache (`osSetSpeedupEnable(true)`), that is about
  3 to 5 ms.
- The step budget at 60 Hz is 16.7 ms and must also cover the renderer's CPU work. So: the
  New 3DS has comfortable headroom, and the Old 3DS is plausible for light missions but
  tight for heavy ones.
- The step spikes (first frame, first spawn of a type mid-mission) come from loading on
  first use (scripts, models through the pak reader). On hardware they would be worse,
  because SD reads are slow. A port must precache at level load.

## Question 3: can the data be drawn with citro3d?

**Yes.** On the top screen (400x240, RGBA8 colour + D24S8 depth), the spike draws:

- **The terrain** of mission 1 (32x256 cells) as triangles. It is split into 16 chunks of
  16 rows so that 16-bit indices suffice, giving 8 976 vertices and 306 KB. The colour is the
  terrain's static lighting (`Terrain::colors()`), modulated by one terrain texture
  (`texture1.tga` of the level's texture set) repeated every 4 cells. Chunks outside the
  camera's y range are skipped: 3 of 16 are drawn.
- **The player's helicopter**: `models\comanche\comanche_impala.mdl` (507 vertices,
  372 triangles after `buildRenderMesh`) with its 256x128 skin. It is placed with the
  engine's entity matrix.

The TGA textures are decoded by the engine (`decodeTga`) and converted at runtime: power-of-two
sides, 8x8 Morton-swizzled tiles, ABGR byte order, uploaded with `C3D_TexFlush`. The camera is
the engine's own (`worldViewOf`: camera fields 14 to 20). The GL perspective matrix is
replaced by `Mtx_PerspTilt(fovY, 400/240, 4, far)`. A logged check confirms the mapping:
view-space (1,0,-10) lands at 0.104 on the 400-pixel axis and (0,1,-10) at 0.173 on the
240-pixel axis, as expected for a 60 degree vertical field of view. The vertex shader is a
10-instruction picasso program (one MVP matrix, colour scaled from bytes). The texture
combiner is one stage: texture times vertex colour. As asked, there is no water, particles,
shadows, sprites, lighting or HUD. The river shows as its dark, unlit, unwatered bed.

Measured over 298 frames (the world stepping each frame, `C3D_FRAME_SYNCDRAW`):

| | value |
|---|---|
| world step | 2.50 ms (emulated) |
| `C3D_GetProcessingTime` (CPU from FrameBegin to FrameEnd) | 0.09 ms, worst 0.11 (emulated) |
| `C3D_GetDrawingTime` (GPU) | 0.25 ms (Azahar's GPU timing is not modelled: meaningless) |
| frame period | 16.77 ms, worst 33.43 (vsync-locked; one missed vblank) |
| vertices per frame | 10 332 (indexed), command buffer 1 % used |
| VRAM | 6 MB. Free after the render target: 5.27 MB (target colour + depth = 0.73 MB). Textures and vertex buffers are in linear memory, so VRAM is untouched by the scene. |
| linear memory used by the scene | 0.69 MB (terrain 306 KB, two textures 384 KB, helicopter mesh) |

Screenshots contain game art, so they are not committed. They are in the main checkout's
`out/3ds-spike/`: `azahar_render1.png` (both screens with the log),
`azahar_new3ds_grid.png` (four top-screen frames one second apart) and `top2.png`. The logs of
both emulator runs are next to them (`spike_log_new3ds.txt`, `spike_log_old3ds.txt`).

The GPU side of the question (fill rate and vertex throughput of the PICA200 at 268 MHz) can
only be measured on hardware. 10 k vertices per frame is far below what 3DS games push, but a
full scene adds water, alpha-blended particles and sprites, shadows and a second texture pass
on the terrain.

## Open on hardware

The emulator answered correctness and memory. It cannot answer speed. With the paks in
`sdmc:/3ds/openairstrike/as3d/`, the Homebrew Launcher waiting (press Y):

    apps/3ds/build.sh netload 192.168.0.151

Then read `sdmc:/3ds/openairstrike/spike_log.txt`. It gives the real step time per mission,
the real level load time (SD reads), and the real `C3D_GetDrawingTime`. If missions on the
New 3DS step over about 8 ms, add `osSetSpeedupEnable(true)` (804 MHz + L2) at the top of
`main()` and compare.

## What a real port needs

- **Platform layer** (`apps/3ds` + a small 3DS backend): app loop with `aptMainLoop`, file
  paths under `sdmc:/3ds/openairstrike/<game>/` (or RomFS), profile and high-score saving,
  `osSetSpeedupEnable` on the New 3DS, sleep/home-menu handling, logging to the SD card.
- **Renderer**: a citro3d backend for the 13 GLES files of `render` and `renderer2d.cpp`. The
  portable half (materials, render rules, camera, resource cache logic) stays. The work:
  - Terrain: the generated base textures, the detail pass and the tile overlays. Two
    texture units plus 6 TEV stages cover it, as a multitexture pass instead of
    multi-pass.
  - Meshes: blend modes, alpha test and envmap (PICA has sphere-map-like texgen only through
    the shader; do it in the vertex shader).
  - Fog: the PICA fog LUT.
  - Water: alpha-blended scrolling texture.
  - Particles and sprites: billboards built in the vertex shader or on the CPU.
  - Skid and ground marks.
  - Lightning.
  - Shadows: projected shadow textures are possible (render-to-texture works). Blob
    shadows are the cheap fallback.
  - Dynamic lights: the PICA has 8 hardware fragment lights, or do vertex colour.
  - Textures: shipping RGBA8 everywhere costs 4 bytes per texel. Converting at load to
    RGB565, RGBA4 or ETC1 halves or quarters it. All textures must be power of two and at
    most 1024.
  - Stereoscopic 3D comes almost free with `Mtx_PerspStereoTilt` (a second render pass).
- **HUD and menus**: the 800x600 layouts must be redone for 400x240 plus the 320x240 touch
  screen. Natural split: the play field on top, score, weapons, lives and the hint boxes
  below. The fonts need checking for legibility at that size. The front-end screens
  (`ui`, 24 portable files) mostly need a 2D backend and new layout numbers.
- **Audio**: `audio` uses SDL2 and libopenmpt (C++ with exceptions and RTTI, compiled
  separately as today). Output through `ndsp`. libopenmpt decoding of the MO3 music may cost
  real CPU on the Old 3DS. Options are decoding on the second core (the syscore, limited on
  the Old 3DS) or pre-rendering the music to Opus/Vorbis (portlibs have libopusfile and
  tremor). WAV effects map directly to ndsp channels.
- **Input**: `hidKeysHeld` to `FrameInput`: circle pad and D-pad to steer, A fire, B missile,
  X power-up, L/R to cycle, START pause. The touch screen can hold the weapon pickers. Two
  players need local wireless: out of scope.
- **Memory and streaming**: not needed for the simulation (7 MB). For the renderer, keep
  textures in linear memory, drop RGBA8, and precache a level's models, scripts and textures
  at load, which also removes the first-spawn stalls.
- **Performance work if the Old 3DS is a target**: profile on hardware, then the usual
  suspects. Per-frame `std::string` building and map lookups in the script host, the
  collision viewport projection, and per-entity allocations.
- **The sequels**: AirStrike 2 and Gulf Thunder have larger paks (47 MB and 43 MB) but the
  same engine. The memory picture should be similar, but this was not measured.

## Estimate

For one developer who knows this engine, to a playable AirStrike 3D on the New 3DS (full
renderer at reduced fidelity, bottom-screen HUD, music and effects, controls, saving):

- platform layer and input: 1 week;
- renderer backend: 4 to 6 weeks;
- HUD, menus and 2D: 2 to 3 weeks;
- audio: 1 to 2 weeks;
- hardware tuning: 1 to 2 weeks.

That totals **2 to 3 months**. Supporting the Old 3DS at 60 Hz on the heavy missions adds an
unknown amount of optimisation work, which the hardware run above would size. The sequels
would follow for a few weeks more each, mostly front-end screens.
