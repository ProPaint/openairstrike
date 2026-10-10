# Nintendo 3DS feasibility spike

Not a port: a measuring app (`openairstrike.3dsx`) that answers the three questions of
[docs/3ds-spike.md](../../docs/3ds-spike.md). It compiles the platform-independent engine
modules from `engine/src` with devkitARM, then:

1. mounts the AirStrike 3D paks from `sdmc:/3ds/openairstrike/as3d/`, loads the definitions;
2. runs missions 1, 6 and 14 headless, 600 frames each with the `as3d_sim --bot` pilot,
   printing load time, step time (system tick counter, 268 MHz), heap and entity counts;
3. reloads mission 1 and draws its terrain and the player's helicopter with citro3d on the
   top screen while the world keeps stepping (300 measured frames, then until START).

Output goes to the bottom screen and to `sdmc:/3ds/openairstrike/spike_log.txt`; the
engine's own warnings to `sdmc:/3ds/openairstrike/engine_log.txt`. START quits.

## Build

Needs Docker and the pinned devkitARM image (no native devkitPro). From the repository root:

    tools/3ds_build.sh          # -> build-3ds/apps/3ds/openairstrike.3dsx
    tools/3ds_build.sh clean
    tools/3ds_ci.sh             # compile-only check

It is a CMake build (3ds-cmake's `3DS.cmake` toolchain, `NINTENDO_3DS`): the root
`CMakeLists.txt` adds only `apps/3ds` on that platform, and `engine/CMakeLists.txt` builds the
engine modules with the pica/ndsp/ctr backend defaults (the backend directories are still
empty, so render, ui, audio and platform contribute their neutral files only). Music (M2.4):
the script mounts the libopenmpt source tree that `tools/fetch_third_party.sh openmpt` downloads
once into `${AS3D_DATA_ROOT:-repo}/third_party_local/` (version 0.8.9, pinned SHA-256; the build
fetches it when it is missing, `AS3D_3DS_FETCH=0` never does, `AS3D_3DS_MUSIC=0` builds without
it); without the tree the decoder is the stub and CMake warns. Tremor (pre-rendered Ogg music)
comes from the image's portlibs. `source/scene.v.pica` goes through `ctr_add_shader_library`
and is embedded as `scene_shbin`; `icon.png` is the project's own icon scaled to 48x48.

## Game data

Copy `pak0.apk`, `pak1.apk`, `pak2.apk` of AirStrike 3D (the files under
`third_party_local/original/data/`) to `sdmc:/3ds/openairstrike/as3d/`. They are
copyrighted: never commit them.

## Run in Azahar

The emulated SD card is `~/.var/app/org.azahar_emu.Azahar/data/azahar-emu/sdmc/`. With the
paks in `3ds/openairstrike/as3d/` there and the `.3dsx` copied next to them:

    XA=$(ls /run/user/1000/.mutter-Xwaylandauth.* | head -1)
    flatpak override --user --filesystem="$XA":ro org.azahar_emu.Azahar
    sed -i 's/^async_presentation=true/async_presentation=false/' \
        ~/.var/app/org.azahar_emu.Azahar/config/azahar-emu/qt-config.ini   # for screenshots
    DISPLAY=:0 XAUTHORITY="$XA" flatpak run --env=QT_QPA_PLATFORM=xcb --env=DISPLAY=:0 \
        --env=XAUTHORITY="$XA" org.azahar_emu.Azahar --fullscreen=false \
        ~/.var/app/org.azahar_emu.Azahar/data/azahar-emu/sdmc/3ds/openairstrike/openairstrike.3dsx

`DISPLAY` must also be set on the host side of `flatpak run`, or flatpak does not expose the
X11 socket and Qt aborts with "could not connect to display". Screenshots: `xwd -id <the
"Azahar 2125.1.1" toplevel>` then `ffmpeg -i shot.xwd -update 1 shot.png`; the top screen is
the crop `410x245+434+20` of a 1280x540 window. Emulator timings are not hardware timings
(see the report).

## Run on a real 3DS

Put the paks on the console's SD card as above, open the Homebrew Launcher, press Y
(netload), then from this machine:

    tools/3ds_build.sh netload 192.168.0.151

(equivalently `3dslink -a 192.168.0.151 build-3ds/apps/3ds/openairstrike.3dsx`, port 17491). Or copy
`openairstrike.3dsx` to `sdmc:/3ds/` and start it from the launcher. Afterwards
`sdmc:/3ds/openairstrike/spike_log.txt` holds the real numbers; the questions they settle are
listed under "Open on hardware" in docs/3ds-spike.md.

## GPU probe (M1.0)

`source/probe.cpp` measures what the PICA200 costs for eight texture, target and blend
variants (docs/3ds-port.md, "M1.0 GPU probe"). It is selected by a run config on the SD card,
`sdmc:/3ds/openairstrike/run.ini`:

    mode=probe

`mode=spike` or no file gives the spike. Optional keys, one per line, no trailing comments:
`hold=N` keeps drawing N frames after each variant (for screenshots), `only=CH` runs just
those variants.

The probe runs unattended, prints a table on the bottom screen and appends it to
`sdmc:/3ds/openairstrike/probe_log.txt`. START quits.

## Game loop and run.ini (M1.2)

`sdmc:/3ds/openairstrike/run.ini` chooses what the app does. One `key=value` per line, `#` or `;`
start a comment line, unknown keys are ignored, no file means `mode=spike`:

| key | values | meaning |
|---|---|---|
| `mode` | `spike` (default), `probe`, `sim`, `bot`, `play` | `spike` and `probe` run the old code paths. `sim`: the mission headless at full speed with the bot, no screens, ends after `frames` steps (reproduces `as3d_sim --bot`). `bot`: real-time 60 Hz loop with the bot's input and both screens through the frame composer (`ctr_frame.cpp`: the world on the top screen, in stereo with the 3D slider, the HUD, hint box and touch buttons on the bottom one, a loading screen while the level is precached). `play`: the same loop with the pad (circle pad / D-pad steer, A fire, B missile, X power-up, Y weapon, R next missile, START pause, ZR next power-up on a New 3DS; the bottom-screen buttons pause / next weapon / next missile / missile / power-up / next power-up work by touch; see `ctr_input.h`). |
| `game` | profile key (`as3d`, `as2`, `gulf`) or `select` | the game; its paks are read from `sdmc:/3ds/openairstrike/<key>/`. In the front end (`mode=menu`, or `mode=play` without `level=`) an absent `game=` or `game=select` shows the game selector first (see "Game selector (M5.4)"); the direct modes (`sim`, `bot`, `play` with `level=`) use `as3d` when it is absent |
| `level` | N, default 1 | the mission to start directly (the front end is off: `startLevel=true`, `levelFlow=false`). `mode=play` without `level=` runs the front end instead (see "Front end (M2.1)") |
| `frames` | N, default 0 | simulation steps, then the app quits cleanly; 0 means 600 in `sim` and unlimited otherwise |
| `perf` | `0` (default) or `1` | write `perf.csv` |
| `log_input` | `0` (default) or `1` | log `AS3D_INPUT held=0x... touch=<button\|none>` on every change of the held bits and on every touch-button hit |
| `log_lazy` | `0` (default) or `1` | log `AS3D_LAZY path=... kind=... site=... frame=...` for every load after a level-start precache (a first-use load: what the precache missed; `as3d/load_stats.h`) |
| `bot_weapons` | `0` (default) or `1` | the weapon tour (`as3d/input.h` applyWeaponTour) over the input of `bot`, `play` (pad) and `menu_bot`: every weapon at every upgrade level, every missile type and power-up kind, fire held; a cheat, for the precache checks |
| `bind_fire`, `bind_missile`, `bind_powerup`, `bind_nextweapon`, `bind_nextmissile`, `bind_nextpowerup`, `bind_pause` | `A B X Y L R ZL ZR START SELECT NONE` | remap a pad button (defaults: A, B, X, Y, R, ZR, START); an unknown value is ignored |
| `difficulty` | N | the engine default when absent |
| `dump_at` | N, default 0 (never) | bot/play: the first frame after N simulation steps writes `top_left.ppm`, `top_right.ppm` (only when two eyes were drawn) and `bottom.ppm` (upright 400x240 / 320x240 PPMs read back from the targets) and an `AS3D_DUMP` line (stereo parameters, the player's pixel in each eye, the predicted far disparity, the model records drawn from linear-memory textures) |
| `brightness` | 0..1, default 0.6 | config.ini Brightness for the brightness pass (DST_COLOR, SRC_COLOR) over the top screen; 0.5 is neutral and skips the pass (use it to compare with `as3d_game --render-only`, which has no brightness pass) |
| `slider` | 0..1 | force the 3D slider (the emulator, comparisons); absent: the hardware slider |
| `eye_offset` | world units, default 4 | the eye offset at slider 1 (each eye moves this far along the camera's right axis; `ctr_frame.h` kMaxEyeOffset) |
| `min_focal` | world units, default 40 | the smallest zero-parallax distance (`ctr_frame.h` kMinFocalDistance; the focal distance is the camera-to-player depth) |
| `precache` | `1` (default) or `0` | `0` skips the level-start precache (everything loads on first use) |
| `audio` | `1` (default) or `0` | sound in `bot` and `play` (never in `sim`): the ndsp device, or silence when `ndspInit` fails (no `sdmc:/3ds/dspfirm.cdc`); see "Sound (M2.3)" |
| `hold`, `only` | | probe only (see above) |
| `attract`, `logo`, `menu_bot`, `pad_script`, `dump_every`, `dump_screens`, `dump_prefix` | | the front end, see "Front end (M2.1)" |
| `quality` | `auto` (default), `new`, `old` | the quality profile (M4.1): `auto` picks by APT_CheckNew3DS; `old` = 30 Hz render, particle/sprite caps, blob shadows, no terrain detail |
| `water_tile` | world units, default 80 | the first game's water plane is drawn in tiles of about this size; `0` = the GL's 1280 x 320 quads, whose large triangles lose depth precision on the console (docs/3ds-port.md "Water on hardware") |
| `water` | `1` (default) or `0` | `0` draws no water (hardware A/B) |
| `fog` | `1` (default) or `0` | `0`: the level fog stays off (the terrain, models and sprites keep their unfogged colours; the clear colour is the fog colour as before), for the hardware A/B of the fog table |
| `depthmap` | `w` (default) or `z` | `z`: the world passes use the Z-buffer depth mapping instead of the W-buffer, with the fog table rebuilt for it (hardware A/B) |
| `pace` | `auto` (default), `60`, `30` | the render rate (`ctr_loop.h`): the simulation steps once per vblank (the frame clock counts the vblanks since the last frame, never 0 steps); `auto`: one eye starts at 60 Hz, two eyes at 30 Hz (two steps per frame); 3 counted misses in 120 frames at 60 Hz drop to 30 Hz (misses after a load, an SD write, a dump or a late load do not count); at 30 Hz, 120 frames whose CPU path stays under 13 ms and GPU time under 14 ms return to 60 Hz. `AS3D_PACE`, `AS3D_MISS` and `PERF_SUMMARY pace` in the log |
| `gpu_kick` | `1` (default) or `0` | the GPU starts each eye's command list while the CPU records the next (`pica_frame.h` kickGpu); implies `end_flush=cmdlist`. gpu_ms then only times the tail after C3D_FrameEnd: measure the GPU gate with `gpu_kick=0` |
| `end_flush` | `all` (default) or `cmdlist` | C3D_FrameEnd(0) flushes the whole linear heap; `cmdlist` only the command list (every other GPU buffer is flushed when written) |
| `arena_poison` | `0` (default) or `1` | synchronisation check (docs/3ds-port.md "Frame synchronisation"): each frame arena half and water buffer half is filled with finite garbage and flushed the moment the CPU takes it back, and every deferred block (meshes, textures, 2D caches) when it is freed: a GPU still reading one draws visible garbage. Costs about a 1.5 MB fill per frame |
| `state_check` | `0` (default), `1`, `2` | the render-state self-check (`state_check_pica.h`): `1` compares the terrain's and the water's expected state with the shadow copies and citro3d's context before their draws (`AS3D_STATE_MISMATCH`), reads the GPU's idle status at each frame start (`AS3D_GPU_BUSY`), logs `AS3D_STATE` every 60 frames (water plane, tiles, near tiles, span, its buffer half/offset, near planes, fence, textures created inside frames, command buffer and GX queue use); `2` also reads the GPU registers back right after the terrain's and the water's draws (`AS3D_GPU_MISMATCH`, `AS3D_GPU_REGS`; stalls the CPU twice per eye). With either, **L** marks the moment (the water wash on screen): `AS3D_WASH_MARK`, the screens dumped to `mark<NN>_*.ppm`, the next 3 frames checked in full |
| `passes` | names, default all | only these passes draw, for A/B hardware runs: `clear terrain marks shadows opaque water rest lightning particles sprites brightness top2d model hud hint touch bottom2d`, `world` (the world passes), `all`, `none`; comma separated |
| `hud`, `touch` | `1` (default) or `0` | `0` leaves the HUD / the touch band off the bottom screen |
| `top_format` | `rgba8` (default) or `rgb565` | the top screen's eye targets and framebuffers in RGB565: half the colour bytes the GPU writes and blends (stereo GPU A/B); dumps are read back from 565 (Azahar: MAE 4.6 against RGBA8, the 565 steps under the brightness pass) |
| `prefetch` | `1` (default) or `0` | the read plan (`ctr_loop.h` ReadPlan): each level start records the files it read in `sdmc:/3ds/openairstrike/cache/<game>_m<N>.plan` (file names only) and the next start of that level reads them ahead in a few large reads per pak (`AS3D_LOAD phase=prefetch`, `AS3D_READPLAN`) |
| `shadow_cache` | `1` (default) or `0` | the silhouette cache (`shadow_pica.h`): `cache/<game>_m<N>.shadows` keeps the rasterised silhouettes of the level, keyed by a hash of their inputs, and the next start reuses them (`AS3D_SHADOW_CACHE`) |
| `terrain_lod` | `auto` (default), `0`, `1`, `2` | the terrain's far chunks drawn from a coarse grid (every second vertex, docs/3ds-port.md "Stereo at 60 (round 2, P3)"): `0` never, `1` (= `auto`, the quality profile's default) only while two eyes are drawn, `2` also in mono |
| `terrain_lod_dist` | world units, default 520 | a chunk whose box is farther than this from the camera is coarse |
| `shadow_atlas` | `1` (default) or `0` | the silhouettes packed into 512x512 A8 atlas pages, one draw per page; `0` = one texture per silhouette as before |
| `touch_segments` | 8..96, default 24 | segments of the touch band's circles (48 before: 16k vertices a frame on the bottom screen, 8k now) |
| `fixed_steps` | N (1..8), default 0 (the clock) | N simulation steps per displayed frame whatever the clock: two builds profile and dump (`dump_at`) at the same steps (docs/3ds-port.md "Sequels' CPU (round 2, P4)") |
| `mesh_sort` | `1` (default) or `0` | the opaque mesh records without blending (depth test and write on) in shader / skin / TEV / state order, ties in submission order; `0` = submission order |
| `mesh_merge` | `0` (default) or `1` | runs of tiny mesh records (32 vertices, 48 corners at most) with the same skin and state baked into the frame arena in world space and drawn as one flat draw; off: under Azahar the bake costs as much as the draws saved (a console A/B) |
| `pak_cache_kb` | N, default 0 | a block cache of N KB (64 KB blocks) per pak stream; the paks are read with FSFILE_Read at an offset either way (`AS3D_LOAD ... read_ms`) |

`mode=bot level=1 frames=3600 perf=1` is the M1 acceptance run. START+SELECT held together quits
cleanly in every loop mode. HOME and sleep pause the world, call the save hook (none yet) and flush
the log; the clock restarts on resume (no replay of the time away). On a New 3DS the loop turns the
speed-up on.

Outputs, all in `sdmc:/3ds/openairstrike/`: `log.txt` (every `AS3D_INFO/WARN/ERROR` of the engine and
the app, kept in memory and written about once a second by a low-priority writer thread, never on the main thread during play; replaced at each start), and with `perf=1` `perf.csv`, one row per
displayed frame: `frame,step_ms,prepare_ms,cpu_ms,gpu_ms,eyes,verts,draws,tex_vram_kb,tex_linear_kb,
linear_free_kb,lazy_loads,arena_kb,sim_steps,submit_ms,wait_ms,end_ms,after_ms,sync_ms,render_hz` (prepare_ms: the
composer's CPU prepare; verts/draws: the GPU work of both screens and both eyes; lazy_loads: first-use
loads since the precache; arena_kb: the frame arena bytes the frame used; the frame's CPU in order:
step_ms, prepare_ms, wait_ms in C3D_FrameBegin (vblank and previous GPU frame), submit_ms recording
the command list (drawTop per eye, drawBottom), sync_ms the vblank wait before the frame, end_ms in
C3D_FrameEnd, after_ms the hook's afterFrame: the `dump_at` read-back and PPM writes, about 100 ms
on the emulator and the ~1 s freeze seen on the console at the dump; mode=play never dumps). The
log gets `AS3D_PASS_MS` every 300 frames (per pass: CPU ms / draws / vertices per frame, both eyes;
`ui_batch` and `ui_copy` are the 2D batch build and arena conversion inside the 2D passes),
`AS3D_PREP_MS` with it (the prepare split: `prep` = prepare_ms, `prep_flow`, `prep_world` and its
parts `prep_terrain`, `prep_scene` (`prep_order`, `prep_entities`), `prep_marks`, `prep_water`,
`prep_shadows` (`prep_drape`), `prep_meshes`, `prep_stats`; ms / items per frame), `AS3D_MESH`
(mesh draws, environment draws, texture binds, state changes, merged draws and records per frame),
`AS3D_LOAD phase=...` lines (session: mount, defs, world, level; terrain; precache scripts, world
with the texture create split mip / swizzle / upload, meshes, shadows, loading frames; level start),
`AS3D_LATE_SHADOW` for a silhouette the precache missed, and `PERF_SUMMARY split_ms` (the averages
of the split columns, gpu p95). The log also gets `AS3D_PRECACHE` and `AS3D_TEXTURES` at the level
start and `AS3D_LAZY_LOADS` at the end. At the end `log.txt` gets the end state in the format of
`as3d_sim` and `PERF_SUMMARY` lines (step and frame time avg/p95/max, frames over 17 ms, load time,
heap high-water, linear free, lazy loads, arena). Fetch with ftpd and report:

    tools/3ds_fetch_logs.sh 192.168.1.133         # -> out/3ds/<date>-<commit>/
    python3 tools/3ds_perf_report.py out/3ds/<date>-<commit>

Code: `ctr_main.cpp` (entry), `run_config.*`, `ctr_loop.*` (the loop, `FrameHook`), `ctr_frame.*`
(the frame composer: PICA WorldRenderer, 2D layer, stereo, loading screen; docs/3ds-port.md
"M1.8"), `ctr_perf.*`;
the graphics context is the engine's ctr platform backend (`engine/src/platform/ctr/`, its
private header `ctr_context.h`).

## PICA core test (M1.3)

`mode=picatest` in `run.ini` runs `source/pica_test.cpp` before any game data is read: test
patterns through `engine/src/render/pica` on the top screen, `picatest_log.txt` and
`picatest_checks.txt` next to `run.ini`. Check a screenshot with
`tools/3ds_picatest_check.py shot.png picatest_checks.txt` (docs/3ds-port.md, "M1.3").

`mode=terraintest` (optional `level=N`, `hold=N` frames per phase, default 300) runs
`source/terrain_test.cpp`: the mission's terrain alone through the PICA `TerrainRenderer` after
120 bot frames, in six phases (plain, crater, three lights, a grazing view of the tile overlays,
the same without overlays, the same without depth bias). It writes `terraintest_log.txt` (bytes,
chunk and draw counts, GPU time) and one `terraintest_<phase>.ppm` per phase (the top target,
400x240). Compare phase 1 with the desktop: `as3d_game --headless --level 1 --bot --frames 120
--size 400x240 --screenshot-every 120 --render-only terrain`, then
`tools/3ds_screen_compare.py terraintest_1.png frame_000120.png` (docs/3ds-port.md, "M1.4").

`mode=fxtest` (optional `level=N`, `frames=N` bot frames before drawing, default 300; `hold=N`
frames per phase, default 90) runs `source/fx_test.cpp`: after the bot frames the world is frozen
and the PICA `ParticleRenderer` and `SpriteRenderer` (health bars are sprites) draw it in six
phases (particles, sprites, all, a synthetic 1536-quad stress, synthetic health bars, two stereo
eyes in the screen halves). It writes `fxtest_log.txt` (quads, drops, draws, arena bytes,
textures, GPU time) and `fxtest_<phase>.ppm`. Compare phases 1 to 3 with
`as3d_game --headless --level 1 --bot --frames 1400 --size 400x240 --screenshot-every 1400
--render-only particles` (or `sprites,healthbars`, `particles,sprites,healthbars`) and
`tools/3ds_screen_compare.py fxtest_1.png frame_001400.png --mask` (use `frames=1400`: at frame
300 mission 1 has almost no effects). docs/3ds-port.md, "M1.6".

`mode=meshtest` (optional `hold=N` frames per test, default 300; with `hold` given the app quits
after one cycle) runs `source/mesh_test.cpp`: mission 1 after 120 bot frames, then (a) the
player's helicopter and its attachments through `MeshRenderer`, (b) every model record of the
frame with the world camera through `pica::MeshPass`, (c) b with three extra point lights. It
writes `meshtest_log.txt` (memory, records, draws, vertices, bytes, GPU time),
`meshtest_checks_<t>.txt` (for `tools/3ds_picatest_check.py`) and `meshtest_<t>.ppm`. Compare b
with `as3d_game --headless --level 1 --bot --frames 120 --size 400x240 --screenshot-every 120
--render-only models` and `tools/3ds_meshtest_compare.py frame_000120.png meshtest_b.ppm
side.png` (docs/3ds-port.md, "M1.5").

`mode=uitest` (optional `level=N`, `frames=N` bot frames, default 120; `hold=N` frames per phase,
default 30) runs `source/ui_test.cpp`: the 2D layer through the PICA `Renderer2D` flush, on both
screens (the bottom screen is a 320x240 target without depth, the console is replaced). Phase 1
draws the font (HUD-scale text, the number routine, the ASCII set at scale 1.0 = 12x6 px glyphs,
the alpha font) on the bottom screen and once on the top; phase 2 the mission's HUD
(`HudTarget::Bottom320x240`) and the touch controls band; phase 3 blend, second-texture combine
and scissor quads with expected pixels. It writes `uitest_log.txt` (runs, vertices, arena bytes,
textures), `uitest_<phase>.ppm` (bottom, 320x240), `uitest_top_1.ppm` (400x240) and
`uitest_checks.txt`. Checks: `tools/3ds_picatest_check.py --ppm uitest_3.ppm uitest_checks.txt`.
Compare phase 2 with the bottom half of `as3d_game --dual-screen --headless --level 1 --bot
--frames 120 --screenshot-every 120` (crop `40,240,360,480` of the 400x480 frame) with
`tools/3ds_screen_compare.py uitest_2.ppm bottom.png --direct --mask`. docs/3ds-port.md, "M1.7".

## Sound (M2.3)

The engine's ndsp device (`engine/src/audio/ndsp/`) plays the mixer on one stereo PCM16 channel at
32728 Hz, four 1024-frame buffers, fed by a thread on core 1. `ndspInit` needs the DSP firmware
`sdmc:/3ds/dspfirm.cdc` (dump it once with the DSP1 homebrew); without it the log says
`audio: ndspInit failed (0x...)` once and the game runs silent on the Null device.

In `bot` and `play` (`audio=1`, the default) `source/ctr_audio.cpp` drives an `AudioBridge` as the
desktop game does: the level's sounds are precached at level start, the world's sound requests
are played after every step, the sound pauses with the world (START) and on HOME / sleep, and
stops at exit. `log.txt` gets
`audio: ndsp: channel 0 stereo pcm16 32728 Hz, 4 x 1024 frames ...` at start and three summary lines:

    PERF_SUMMARY audio device=ndsp played=N loops=N missing=N precached=N
    PERF_SUMMARY music decoder=ogg|openmpt|off|none decoded_s=S cpu_share=X% max_decode_ms=X underruns=N
    PERF_SUMMARY audio_feed fills=N (32.0/s) underruns=N max_fill_ms=X feeder_cpu=X% dropped=N core1=1

Music (M2.4; bot, play and the front end): `music=0|1` (default 1) and `music_taps=0|1|2|4|8`
(libopenmpt's interpolation filter, 0 = its default, 8 taps; 2 = linear, about a third cheaper).
At every level start the level's module plays pre-rendered when
`sdmc:/3ds/openairstrike/<game>/music/<module name>.ogg` exists (tremor), else as a module
(libopenmpt), else there is none (one warning). Pre-render on the desktop from your own data and
copy the files over (they are derived from the game data: never commit them):

    cmake -S apps/audio_tool -B build-audio-tool && cmake --build build-audio-tool --target audio_tool
    build-audio-tool/audio_tool render-music --all out/music     # track01.ogg, track01_gameover.ogg, ...

(`--paks <dir>` before `render-music` for a sequel's paks; oggenc or ffmpeg with libvorbis.)
`<name>_gameover.ogg` is the module from pattern order 35, where the game jumps on game over.

`mode=musictest` (optional `frames=N` per pass, default 600 = 10 s; `music_taps=N`;
`music_files=music\a.mo3,music\b.mo3`) runs `source/music_test.cpp`: the menu's and mission 1's
modules (both `track05.mo3` in AirStrike 3D, so the largest module, `track04.mo3`, is the second),
each pre-rendered (when the `.ogg` is on the SD card) and as a module, through `Audio::playMusic`
and the ndsp feeder; on a New 3DS everything again at 268 MHz. Per second: the decoder's share
(its time per second of audio decoded), its longest decode of one 512-frame mixer call, the
feeder's fills, underruns, longest fill and CPU; at the end one `MUSICTEST` line per pass with
`USABLE` or `TOO SLOW` (the plan's rule: at most 25 % of core 1). It writes `musictest_log.txt`.
docs/3ds-port.md, "M2.4".

`mode=audiotest` (optional `frames=N`, default 360 = 6 s) runs `source/audio_test.cpp`: a check of
`std::thread` + `std::mutex`, then `as3d::Audio` on the platform device plays a generated 440 Hz
tone for 1 s, then three sounds of mission 1's precache set (the machine gun every 12 frames for 2 s,
a missile, an explosion panned right). Every second it logs fills (+32 per second expected), underruns, the peak
sample of that second's buffers (about 11000 for the tone), the longest fill and the feeder's CPU
share; it writes `audiotest_log.txt`. docs/3ds-port.md, "M2.3".

`mode=shadowtest` (optional `hold=N` frames per phase, default 60; `precache=0`;
`shadow_rotations=1`) runs `source/shadow_test.cpp`: the PICA `WorldRenderer` started as the
composer does (beginLevel, then the precache with the shadow silhouettes unless `precache=0`),
mission 1 after 120 bot frames, then the world frozen and drawn mono in three phases: `t` terrain
only, `a` terrain and shadows, `b` the full frame. Each phase prepares and draws until no shadow
is a blob any more (with `precache=0` one silhouette is generated per frame, drawn from the next:
the late path, dumped as `shadowtest_a_late0.ppm` and `_late1.ppm`). `shadow_rotations=1` also
precaches the 12 rotations of every projected silhouette. It writes `shadowtest_log.txt`
(silhouette count, bytes, generation time and sizes; per-frame shadows, blobs, late, draped
vertices, draws, arena, prepare cost with and without shadows) and `shadowtest_<phase>.ppm`.
Compare `a` with `as3d_game --headless --level 1 --bot --frames 120 --size 400x240
--screenshot-every 120 --render-only terrain,shadows` (and `--render-only terrain` as the base):
`tools/3ds_screen_compare.py shadowtest_a.ppm frame_000120.png --direct --mask --darken
shadowtest_t.ppm,terrain/frame_000120.png`; `b` with `--render-only
terrain,models,particles,sprites,shadows`. docs/3ds-port.md, "M3.1".

`mode=markstest` (optional `frames=N` bot frames, default 1400; `hold=N`; `precache=0`) runs
`source/mark_test.cpp`: mission 1 after N bot frames, then the world frozen and drawn mono in four
phases: `t` terrain only, `m` terrain, ground marks and skid trails, `s` the same with two synthetic
skid trails, `f` the full frame. It writes `markstest_log.txt` (per frame marks, skid segments,
vertices, draws, arena, texture bytes, build time) and `markstest_<phase>.ppm`. Compare `m` with
`as3d_game --headless --level 1 --bot --frames 1400 --size 400x240 --screenshot-every 1400
--render-only terrain,marks,skid` (and `--render-only terrain` as the base for `--darken`); `f` with
`--render-only terrain,models,particles,sprites,shadows,marks,skid`. docs/3ds-port.md, "M3.2".

## Front end (M2.1)

`mode=menu`, or `mode=play` without `level=`, runs the game's real front end
(`source/ctr_menu_loop.cpp`): `GameStack` with `GameFlow` over the session, the frame composer as
its view. The menus, the intro pages, the HUD, the hint box, mission end, game over and the
high scores are on the bottom screen (the 320x240 mapping, scale 0.4); the top screen shows the
attract level behind the menus and the mission during play, mono or stereo as in `play`, with
the game's title where the desktop's main menu has its 3D banner and PAUSED under the in-game
menu. Missions start from Start Game and end through the menus (Restart, Continue, Quit); every
level start shows the original's loading screen on both screens.

Controls in the menus: D-pad up / down move the focus (held: repeats), left / right change the
focused value (spinners, sliders, the mission list, the helicopter), A = Enter, B and START =
back; touch = the mouse (tap an item; the name entry has its own keyboard). During play the pad
is as in `play`; START or the touch pause button opens the in-game menu. START+SELECT quits;
the main menu's Exit too. HOME / sleep during play open the in-game menu and save the profile.

Files: the paks plus `Settings.xml` (intro pages, version and copyright line) and the texts
imported from the user's executable (`texts_v170.txt` for AirStrike 3D: menu labels and
Information pages) in `sdmc:/3ds/openairstrike/<game>/`, copied from the install like the paks
(both are the game's, never commit them). The profile (settings, progress, high scores) is
`sdmc:/3ds/openairstrike/<game>/profile.bin`, saved through `GameFlow` as on the desktop.
Options offers brightness, the volumes, Show FPS and the camera (`FlowConfig::consoleOptions`:
no Configure controls, Mouse Control, Screen, video or touch rows); Start Game one player.

| key | values | meaning |
|---|---|---|
| `attract` | 0 (default: random), 1..4 | the attract level behind the main menu |
| `logo` | `1` (default) or `0` | the intro pages at boot |
| `menu_bot` | `0` (default) or `1` | the bot flies the missions started from the menus (it also closes the hint boxes); its clock runs with the world's (hint boxes included, not the front end's pauses: the sequels' start dialogue, the in-game menu), so its N-th step is `mode=bot`'s |

| `menu_bot_steps` | N, default 0 | after the N-th running step of a mission (the world's steps but those the front end holds: the bot's steps with `menu_bot=1`), `AS3D_BOT_END` and the end state in the lines of `as3d_sim` (the run goes on) |
| `end_at`, `end_kind` | N; `complete` (default) or `gameover` | at the N-th running step the mission ends as EndLevel or the last life would (scripted runs through Mission Complete, Game Over, the name entry) |
| `end_score` | N | player 1's score set at `end_at` (the fresh high-score table's last entry is 30000: the name entry) |
| `unlock_heli` | N | helicopter N unlocked in the profile at start (a fresh sequel save has entry 0 only, `as3d_sim`'s default is 1; the unlock is saved) |
| `ui_fixed_dt` | `0` (default) or `1` | the menus' clock advances 1/60 s per displayed frame, as `as3d_game --ui-script` (comparisons of animated screens) |
| `pad_script` | `F:NAME,...` | presses injected into the pad at displayed frame F, held 6 frames: `A B X Y L R ZL ZR START SELECT UP DOWN LEFT RIGHT` (several joined by `+`), or `T<x>/<y>` for a touch at bottom-screen pixel (x, y); for unattended emulator runs |
| `dump_screens` | `0` (default) or `1` | 40 frames after each change of screen, `<prefix><nn>_<screen>_top.ppm` and `_bottom.ppm` |
| `dump_every` | N | every N displayed frames, `<prefix>f<frame>_top.ppm` and `_bottom.ppm`; `_right.ppm` too when the world was drawn in stereo |
| `dump_from` | S | with `dump_every=N`: every N simulation steps from step S instead, named `<prefix>s<step>_*.ppm` (mission runs) |
| `dump_prefix` | default `fe_` | the file name prefix of those dumps |
| `report_at` | N | with `menu_bot=1`: the end-state lines (`level N: ... map_pos`, `created ... freed`) once after N bot steps of the mission (the bot's clock stops during the sequels' opening dialogue) |
| `cycle` | `1,6,1,6` | memory growth check (M5.4): from the main menu start each listed mission (`GameFlow::startMission`), leave it after `cycle_frames` displayed frames (`GameFlow::loadAttract`, what the in-game menu's Quit does), then stop the app |
| `cycle_frames` | N, default 240 | see `cycle` |

`frames=N` ends the run after N displayed frames. The log gets `AS3D_SCREEN name=<screen>` on
every change (intro, main, start, options, scores, name, ingame, hint, complete, gameover, playing, ...),
`AS3D_SCREEN name=intro<N>` for the intro pages (the logo pages, then the sequels' comic pages), `AS3D_MENU` with the visible items in the virtual 800x600 screen (for `T` taps: bottom pixel =
virtual x 0.4), `AS3D_LEVEL_LOADED` and, at the end, the end state of the level running.
docs/3ds-port.md, "M2.1".

## Game selector (M5.4)

With `mode=menu` (or `mode=play` without `level=`) and no `game=` in run.ini (or `game=select`):
the app looks for the games under `sdmc:/3ds/openairstrike/{as3d,as2,gulf}/` (each game's paks,
`Settings.xml` and its texts file; missing files are logged as `selector: <key> incomplete,
missing: ...` and shown on its card, which cannot be started), shows the selector on the bottom
screen (D-pad and A, or a tap; B or the Exit button leaves the app; the top screen names the
focused game), writes the choice to `sdmc:/3ds/openairstrike/launcher.bin` (the next start
focuses it) and runs that game's front end. "Change game" on the main menu (offered when two or
more games are complete) comes back to the selector. One complete game and nothing else
installed: no selector, that game starts; hold START while the app starts to see the selector
anyway. docs/3ds-port.md, "M5.4".

`--render-only terrain,models,particles,sprites,shadows,marks,skid,water,lightning` (the default full list now has water). docs/3ds-port.md, "M3.2".

`mode=watertest` (optional `frames=N` bot frames, default 1400; `hold=N`; `precache=0`) runs
`source/water_test.cpp`: mission 1 after N bot frames, then the world frozen and drawn mono: `t` terrain
only, `w` terrain and water, `l` terrain and three synthetic lightning bolts, `f` the full frame, then 30
more bot frames and `w2` (water again, the scroll moved). It writes `watertest_log.txt` (per frame water
quads, drawn, draws, arena, planeZ, scroll offset; bolts) and `watertest_<phase>.ppm`. Compare `w` with
`as3d_game --headless --level 1 --bot --frames 1400 --size 400x240 --screenshot-every 1400 --render-only
terrain,water`, `w2` with the same at `--frames 1430`, `t` with `--render-only terrain`, `f` with
`--render-only terrain,models,particles,sprites,shadows,marks,water,lightning`. docs/3ds-port.md,
"M3.5 Water and M3.3 Lightning".

`mode=sequeltest` (`game=as2|gulf|as3d`, `level=N` default 1, optional `frames=N` bot frames, default 600;
`hold=N`; `precache=0`) runs `source/sequel_test.cpp`: the game's mission from `<dir>/<game>/` (its paks; the
sequels' are not part of the first game's card: copy `pak0.apk`, `pak1.apk`, `pak2.apk` and for Gulf Thunder
`pak4.apk` from the install's `data/`), N bot frames, the end state in the lines of `as3d_sim`, then the world
frozen and drawn mono: `t` terrain only, `w` terrain and water (the sequels' wave grid), `f` the full frame,
then 30 more bot frames and `w2` (water again, the waves moved). It writes `sequeltest_log.txt` (the render
rules, memory: VRAM and linear textures, meshes, terrain, the water grid; per frame the water chunks, animated
vertices, arena bytes and time) and `sequeltest_<game>_<phase>.ppm`. Compare the end state with `as3d_sim --game
G --level N --frames F --bot`, `f` with `as3d_game --game G --headless --level N --bot --frames F --size 400x240
--screenshot-every F --render-only terrain,models,particles,sprites,shadows,marks,skid,water,lightning`, `w`
with `--render-only terrain,water` (tools/3ds_screen_compare.py --direct --mask). docs/3ds-port.md, "M5.1".

`mode=envtest` (optional `frames=N` bot frames, default 120; `hold=N`) runs `source/env_test.cpp`: mission
1 after N bot frames, the frame's model records drawn mono with the world camera through MeshPass: `a` the
player and its attachments (the shield), `b` the environment-mapped records not on the player (the help
items), `c` every record. With `only=f frames=1400` it draws the full frame through the PICA WorldRenderer
instead (`f`). It writes `envtest_log.txt` (the env records, memory with the env normal streams, draws and
env draws per test) and `envtest_<test>.ppm`. Compare `c` with `as3d_game --headless --level 1 --bot
--frames 120 --size 400x240 --screenshot-every 120 --render-only models` (tools/3ds_meshtest_compare.py),
`f` with `--frames 1400 ... --render-only terrain,models,particles,sprites,shadows,marks,water,lightning`
(tools/3ds_screen_compare.py --direct --mask). docs/3ds-port.md, "M3.4 Environment maps".

`mode=savetest` runs `source/ctr_save.cpp` (no game data needed): 50 profile saves read back byte for
byte, the failing-replace, missing-directory and stale-temporary paths, the `fsync` result and the sleep
guard. It writes `savetest_log.txt` (`PASSED: 0 failed checks`) and the files under `savetest/`.

Saves in the app: the profile is written by `as3d::writeFileAtomic` (temporary in the same directory,
`fsync`, rename with the FAT remove fallback) on level end, option changes, HOME/lid and exit; the log
line is `AS3D_SAVE profile bytes=N ms=X ok=1`; `aptSetSleepAllowed(false)` only during a write. Policy and
the hardware checklist: docs/3ds-port.md, "M2.5 Saves".
