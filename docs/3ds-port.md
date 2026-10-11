# Nintendo 3DS port

Working notes of the port (plan: M1.0 onwards). The feasibility spike is in
[3ds-spike.md](3ds-spike.md); its app is [apps/3ds](../apps/3ds/README.md).

## State on 2026-10-11 and how to resume

Round 2 (2026-10-10/11) is merged and was played once on the console (see "Console session of
2026-10-11" at the end: the water wash was caught in the act, the terrain writes no depth for
seconds at a time; mono fell to 30 Hz from a half-second hiccup of the audio thread; the stereo
felt strong). The fixes went into `c314767` (clear=split, the terrain re-issuing its state, the
voice mix outside the mixer lock, comfort defaults 3 / 6 px / 8 px). **Then the owner asked not
to do QA any more, so the console tests itself (round 2, P7; see "Soak mode and the log push" at
the end).** The drop of round 2d was `out/3ds/drop-round2d-5192662/` (after the first console soak, below) (the package's `openairstrike.3dsx`,
`openairstrike.cia`, `run.ini` = `mode=soak report_to=192.168.1.86:7777 perf=1 state_check=1
log_lazy=1`, `run-play.ini` = play with the same diagnostics), its 3dsx and run.ini uploaded to
the card on 2026-10-11 (the card's run.ini is the soak one; the first soak's results are in
"First console soak" at the end: no wash in 30-second stages, mono 60 Hz missed by bursts, the push
failed on SO_ERROR, all three addressed in `5192662`).

**Round 3 (2026-10-11) is merged: R3.1 "Simulation step growth" (dd1024e: the late step 38 to
40% cheaper, bit-identical; the rest of the growth is the original's own muzzle flashes, an owner
decision, see that section) and R3.2 "Stereo CPU path" (664fee3: the per-frame wash detector, the
terrain priming draw, the right eye replaying the left eye's commands, indexed water, one shader
binary, the touch band cached). The second console soak (below) had passed `wash` with 0 register
mismatches in 1534 probes, yet the owner saw the wash during it: brief episodes, lower half of the
top screen only. The periodic probe reads after the terrain's base draws, and since c314767 the
terrain re-issues its state with its second draw, so a lost first state block hits only the first
(nearest, lowest on screen) chunk and the probe never sees it; hence the detector that judges
every frame from an asynchronous depth read-back (`wash_check`, on in soak; `AS3D_WASH` lines per
episode; "Wash detector" in the R3.2 section). The drop of round 3 was `out/3ds/drop-round3-664fee3/` (its build info says `664fee3-dirty`: only a docs edit was uncommitted); **the latest drop is `out/3ds/drop-round3b-d63fbbf/`** (R3.3 merged; `run.ini` = the ten-stage soak list of the R3.3 section, about 25 minutes: four whole missions then six one-minute A/B stages; the CIA must be reinstalled with FBI, the receiver started with `--expect d63fbbf`)
(`run.ini` = `mode=soak report_to=192.168.1.86:7777 perf=1 state_check=1 log_lazy=1
wash_selftest=1`, `run-play.ini` = play with the same diagnostics). What its soak should show:
`AS3D_WASH` episodes with their length (or none), `prime_mismatches` versus `mismatches`
(whether the first state block is still lost), `AS3D_WASH_SELFTEST` 98/98, the step cost per
window against R3.1's table, the stereo submit versus the 10 ms of 5192662, `eye_replay` counters.

**The third console soak (build 664fee3, `out/3ds/2026-10-11-664fee3-dirty/`) and R3.3** (see "Third
console soak ... and round 3 follow-up (R3.3)" at the end): no washed frame in 51792 judged eye-frames,
but 101 register mismatches right after the terrain's priming draw, in bursts, all in mission 2; mono
60 Hz failed (78 / 82 %) mostly from a heap walk every 32 frames; the stereo "GPU +2 ms" was the GPU
tail measure. R3.3 adds the evidence at a mismatch (a canary word read back, a second read, the list
scan, the passes before, marks, the detector's per-band judgement), the terrain's effect block forced
as direct words after the priming draw (`terrain_force_state=1`), the detector's coverage of the nearest
chunk, per-stage run.ini keys in the soak list with the A/B stages of the next drop
(`run-soak-r33.ini`), per-eye counts and `gpu_kick_ms` in perf.csv, the heap walk every 600 frames
instead of 32, `pace_rule=24` (12 before) from the offline replay `tools/3ds_pacer_replay.py`.

**The owner launches from the HOME menu, not the Homebrew Launcher** (every soak log says
`from=none`: argv is empty, which only the installed CIA does). So a new build reaches the console
only when its CIA is reinstalled with FBI (the drop's `openairstrike.cia` is uploaded next to the
3dsx, FBI > SD > 3ds/openairstrike/openairstrike.cia > Install), or when the owner starts the 3dsx
from the Homebrew Launcher instead. The run of 2026-10-11 16:23 (`out/3ds/2026-10-11-5192662-2/`)
was the old CIA again, a second soak of 5192662: same end states, `wash` PASS by the blind probe
again, mono60 29.7%/62.5% with that build's 6-miss pacer, stereo 30 Hz; it tests nothing of round
3. The receiver now takes `--expect <commit>` and prints a warning at the top of its report when
the run's build differs (the agent starts it with the drop's commit).

**The owner's procedure is now:** start OpenAirStrike from the Homebrew Launcher, wait until the
launcher comes back (five to ten minutes), nothing else. The soak runs bot stages of every
installed game (missions 1 and 2, mono and stereo at slider 0.5, the weapon tour, audio on, the
GPU read-back of the terrain every 30 frames so the water wash is caught without L), writes the
verdict to `soak.txt` and pushes every file of the run to the PC. **The agent's side:** before the
owner launches, `tools/3ds_receive.py --port 7777` must be listening in the main checkout (it needs
`build/apps/sim_tool/as3d_sim` and the data at the repo root, or `--sim` / `AS3D_DATA_ROOT`); the
PC's address on the route is `ip route get 192.168.1.133` (`src`, 192.168.1.86 on 2026-10-11). Never
probe the port to check it (a connection is a run: with `--once` the receiver files an empty
`-partial` run and quits; `ss -ltnp | grep 7777` says whether it listens). It
files the run under `out/3ds/<date>-<commit>/`, prints the verdict and the `AS3D_SOAK_RESULT`
lines, runs the perf report per stage (`stages/NN-game-level-eyes/`), the marks' contact sheet
and the end-state comparison with `as3d_sim --bot --bot-weapons`. If the PC was not listening,
the files stay on the card (ftpd + `tools/3ds_fetch_logs.sh`; it fetches `soak.txt` too). A hang
(not a crash) has no watchdog: the launcher never comes back, the owner power-cycles, and the
next launch reports the `soak.running` marker. What the soak cannot judge, and stays optional for
the owner with `run-play.ini`: the stereo feel at the new defaults, the system-keyboard name
entry (P6), the CIA from the HOME menu. Open after the soak's first verdict: whether the wash is
gone on hardware (`PASS wash`), mono at 60 Hz throughout (`mono60`), stereo still at 30 Hz (the
GPU's 11.5 ms tail; "Stereo at 60" lists the next steps), the per-probe stall on hardware
(`probe_ms_avg_max`).

Last console-verified state before round 2 (New 3DS, drop 4, main `098f524` = drop 5 was played
but not measured):

| | mono | stereo |
|---|---|---|
| frames with one simulation step at 60 Hz | 97.3% (the rest = the once-a-second SD write, moved off the main thread in drop 5) | n/a: stereo now locks to a steady 30 Hz (2 steps per frame) because two eyes need 13.5 ms of GPU on average, 16.5 ms p95; 16-bit targets gave no gain |
| GPU p95 | 9.1 ms (gate 11) | 16.5 ms (gate 15) |
| simulation vs desktop | identical over 3600 steps | identical |
| level load, second start | 2.9 s (first start 7-9 s; the owner accepts a loading screen) | |

Drop 5 (not yet console-verified when the project paused): the writer thread for log/perf, the
pacer rules (mono starts at 60 and ignores load-caused misses, stereo starts at 30), the bottom
screen's three Renderer2D lists, shadows grouped by texture, the depth-offset/state-copy fix for
the water wash seen in play (see "Hardware tuning"), and the water near-plane cut.

**The water wash (drop 5 did NOT fix it):** facts from the owner's play sessions: during normal
play it appears for a few seconds at a time and goes away (NOT tied to hint boxes or pauses: the
owner checked), it was on screen at the mission-complete screen of the tutorial, it covers terrain
and roads but never the models, and no bot run or dump ever caught it (the bot's camera and timing
differ from play). The two console-verified causes so far were static (1280-unit quads, fixed by
the tiles) and are not this one. Plan for the next session, console A/B in `mode=play` with the
owner watching (the agent's list): `water=0` (is it the water pass at all?), `water_tile=0` (the
old quads), `gpu_kick=0` (mid-frame submission ordering), `pace=60` vs `pace=30`, `shadows`
and `passes=` toggles; then the first candidate from the code: anything arena-fed drawn from a
half the GPU may still read when frames run long (gpu_kick), a depth-map/offset mismatch between
the terrain and water passes when the near plane or the pacer state changes, or the water pass
inheriting a 2D overlay's depth state. Reproduce on Azahar in `mode=play` with real input
(pad_script flying over water for a minute, dump_every=30) before fixing.

**Drop 5 on the console, the owner's play session (out/3ds/hw21-play-final-098f524/):** at 60 Hz
the cadence is clean (8377 frames, 13 misses = 0.16%), but the pacer fell to 30 Hz for 6466 frames:
32 of the 40 misses are frames whose submit took 18-24 ms instead of 5, most of them with the fire
button held. Cause: late texture loads in play (`AS3D_LAZY_LOADS mission=1 textures=7`, front end
21: the player's weapon effects, items and menu pictures the bot never touches), and a
`Texture2D::create` inside a frame is a synchronous upload that resets the GX queue (M1.3). Two
fixes for the next session: precache every weapon/upgrade/item effect of the player (not only the
current loadout) and the front end's pictures, and queue any remaining late creation to the end
of the frame (never inside the command stream). The same mid-frame upload is a candidate trigger
for the water wash (a corrupted command stream for the frames around it), to test together with
the A/B list above. prepare() is 2.5-7 ms in play against 1.7 in the bot runs (more particles
and projectiles): profile it per pass. **Round 2, P1 (below, "Late loads and mid-frame uploads")
found the long submits to be the touch band's rebuild, not the loads**, and made both safe.

**Round 2, synchronisation (2026-10-10, see "Frame synchronisation" at the end):** the arena halves
and deferLinearFree were safe under gpu_kick (C3D_FrameBegin waits for the whole GX queue, the
kicked lists included); two things were not and are fixed: `Texture2D::destroy` freed memory a frame
in flight could still sample, and a texture created inside a frame went through citro3d's
unflushed command-list split and an asynchronous copy from a freed staging buffer (now linear
memory, no GX copy). The water has its own fenced vertex buffer and, with the terrain, issues its
whole state before its draws. For the wash, the next console session runs `mode=play
state_check=1` and the owner presses L while the wash is on screen (the decision table is in that
section).

**Open on the console:** confirm mono ≥ 99% one-step frames and stereo a clean 30 with drop 5;
the tutorial water wash with a hint box up (hypothesis: the render-state copies, fixed
defensively in drop 5); the menu-font legibility photo (M2.2); a Gulf stereo run for the GPU
command-buffer headroom; the uniform darker dump at step 3500 (a dump read-back artefact, most
likely). **Next engineering:** stereo at 60: P3 did the three cuts and the touch band (see "Stereo at
60 (round 2, P3)": -33 % of the vertices per stereo frame in mission 1, shadows one draw per atlas
page); the console must measure the GPU. The sequels' CPU: P4 (see "Sequels' CPU (round 2,
P4)") cut prepare by two thirds and recording by 7-16 % under Azahar; what is left there is the
per-draw cost of citro3d (a shared mesh vertex pool, a lighter draw path). Then the
first-start load (prefetch of the precache closure), the Old 3DS run (profile exists, no console).

**Round 2, P5 (stereo comfort, see "Stereo comfort and frame polish" at the end):** the eye offset is
now clamped per frame so the nearest content never comes more than 10 px out of the screen and the
far terrain never goes more than 12 px behind it (`max_parallax_px`, `max_far_parallax_px`, eased,
no steps); the attract levels behind the menus were 32-39 px deep and are now within the limits;
2D is on the screen plane (checked identical in both eyes); LOD hysteresis; the touch band is
built during the loading screen. The owner's console check: the stereo feel, and which limit to
lower if it still tires.

**How to resume a console session:** to play or check an installed copy, use the package
(`tools/3ds_package.sh` -> `out/3ds/package/<version>/`: copy its `3ds/` to the card, start
OpenAirStrike from the Homebrew Launcher, or install `openairstrike.cia` with FBI; no `run.ini`
needed: see "Packaging and launch (round 2, P6)"). For a development build: `tools/3ds_build.sh all` builds `build-3ds/apps/3ds/openairstrike.3dsx`;
upload it with `run.ini` over ftpd (port 5000) to `sdmc:/3ds/openairstrike/` (the paks,
`Settings.xml`, `texts_v170.txt`, `music/*.ogg` from `audio_tool render-music --all` and
`sdmc:/3ds/dspfirm.cdc` are already on the owner's card); quit ftpd, Homebrew Launcher, Y, then
`tools/3ds_netload.sh 192.168.1.133 <file.3dsx>` (never `3ds_build.sh netload` for an agent's
binary: it rebuilds and sends the main checkout's); afterwards ftpd again and
`tools/3ds_fetch_logs.sh 192.168.1.133` + `tools/3ds_perf_report.py`. `run.ini` keys are in
[apps/3ds/README.md](../apps/3ds/README.md); `mode=play` is the game, `mode=bot level=1
frames=3600 perf=1` the acceptance run. Azahar (emulator) is for correctness only; it never
showed the two water bugs (large triangles, state copies) that the console did.

## Late loads and mid-frame uploads (engineering round 2, P1, 2026-10-10)

The drop 5 play session (out/3ds/hw21-play-final-098f524/) had 32 of 40 misses on frames whose
submit took 18-24 ms, and `AS3D_LAZY_LOADS mission=1 textures=7 sounds=1 shadows=1`, front end
`textures=21 scripts=4 sounds=2 other=43`. Its perf.csv shows that the long submits are **not** the
loads: lazy_loads does not change on any of them, they come every 9-11 frames during play, and the
log has a missile or fire press just before each. Both problems are dealt with here.

**Finding what is missing.** `LoadStats::setLogLazy` (run.ini `log_lazy=1`, `as3d_game
--log-lazy`): every load counted after a precache's reset logs `AS3D_LAZY path=... kind=...
site=... frame=...`. The precache entry points are `LoadStats::PrecacheScope`s (not logged);
`World::loadLevel` is a `LevelLoadScope` (neither logged nor counted: the level's own load is behind
the loading screen; the front end's `scripts=4 other=43` were the next mission's map and placement
scripts, counted in the attract level's window). Azahar with the main checkout's binary,
`mode=play` from the menus (menu_bot): front end `textures=22 scripts=4 sounds=2 other=43`, mission 1
`textures=7 sounds=1`, as on the console. What they were:

- the menus' pictures: corner, mmenu_1/2, yesno_1, back_1, start_1, starth_0/1/2, topscores_0/1/2,
  optionsh_0/1/2, scroller_1/2, slider, slidebutt_1, icon_border_2, icons_1/2/3; in play the
  in-game menu and the mission complete screen (restart_1, quit_1, continue_1, their _2 focus
  states); the menu sounds menu1, menu2 and the stat counter's type.wav;
- one silhouette: `ruins_big` at rotation 6. A building's script (static_obj_expl_big.scr) creates
  its ruins, and `create` keys the projected shadow by the creator's int(yaw in degrees) mod 12: an
  odd placement rotation gives step 6, which the PICA closure (projected step 0 only) missed.
- the player's weapons, missiles, power-ups and items were already in the closure: with the weapon
  tour below nothing of them loads on first use (desktop, three games, missions 1, 6, 14).

**Precache additions.** `UiAssets::precacheMenus` (the first game's menu pictures for every screen
the main menu reaches and the in-play ones, normal and focused; the sequels' atlas, title logo and
widget pictures; their per-mission comic pages stay on first use) and `UiAssets::menuSounds`, at
every level start of the front end (ctr_frame.cpp) and in `GameView::precacheLevel` (`as3d_game
--precache`: mission 1 textures 113 -> 152, sounds 22 -> 25). `WorldScene::spawnedShadows`: the
projected shadows of the objects a rotated placement's script names, at its create key.

**The weapon tour** (`as3d/input.h applyWeaponTour`, run.ini `bot_weapons=1`, `as3d_game
--headless --bot-weapons`): every 90 frames the next weapon slot with every slot at the next
upgrade level (1..8), 9 of every missile and power-up with the current ones stepping, fire held, a
power-up every 3 s. A cheat for the checks. precache_test runs it on missions 1, 6, 14 of every
game (pause, hint box, mission end, every frame drawn in the 3DS layout with the touch band) and the
first game's front end (every screen, the Information pages, mission 1 from the menus, the in-game
menu, a hint, mission complete): all kinds 0 but the sequels' TerraMorph stamps.

**No GX copy inside a frame, and no stall.** On top of P2's in-frame linear path ("Frame
synchronisation"): `Texture2D::create` inside a frame makes an 8x8 linear placeholder of the
image's average colour and queues the image; `pica::drainLateTextures` (drawFrame, right after
C3D_FrameEnd; at most one texture or 2 ms per frame, texture_budget.h LateBudget) builds the real
texture, VRAM included, into the same handle; the placeholder goes through the fence.
`AS3D_LATE_TEX queued|built path=... frame=...`. P2's in-frame linear creation stays the fallback.
`GpuMesh::upload` inside a frame defers its cache flushes to frameEnd or the next gpu_kick split
(`pica::deferFlush`, `AS3D_LATE_MESH`). The pacer also excuses the frame after a late load (a load
in the submit delays the next sync). `mode=picatest` checks the queue: frame 2 draws the placeholder
(127, 0, 127), frame 4 the real red / blue halves (picatest_late_f2/f4.ppm, Azahar: exact).

**The long submits: the touch band.** `AS3D_SLOW_SUBMIT` (a submit over 15 ms logs its own pass
split, late textures, lazy loads and the front end's three bottom lists: ms / cached / vertices).
Azahar, mode=play with the weapon tour: bottom2d 27 ms every few frames, the touch band rebuilt
whole (16k tessellated vertices) whenever a count or a held highlight on it changes, inside the
frame. `Renderer2D::cacheBreak` makes cache units (PICA only: hits first, then the changed units,
24 slots per list; GL ignores it); `drawTouchControls` breaks after each button's disc and face.
Same Azahar run: submits over 20 ms 117 -> 3 of 4280 frames, p99 37.6 -> 16.7 ms; the band 0.8 ms;
the bottom screen of the bot run at step 600 pixel-identical. This is the most likely cause of the
console's 18-24 ms submits: the console check is the next drop's play session (expect submit p99
near 7 ms and no `AS3D_SLOW_SUBMIT` with a large bottom2d).

**Writes off the level start.** `queueFileWrite` (ctr_perf.h): the writer thread writes the shadow
cache (`saveShadowCache(writer)`) and the read plan (`AS3D_FILE_WRITE ... (writer thread)`), not
the loading screen (the first start's 2.1 s with fsync on the console).

Azahar numbers (frames are not console timings):

| | before (main of 2026-10-10, before P2) | after |
|---|---|---|
| attract level precache | textures 91 sounds 16, 1072 ms | textures 129 sounds 19, 1320 ms |
| mission 1 from the menus, precache | textures 113 sounds 6, 1802 ms | textures 151 sounds 6, 2037 ms |
| front end lazy loads | textures 22 scripts 4 sounds 2 other 43 | 0 |
| mission 1 lazy loads (with the weapon tour, in-game menu, hint, mission complete) | textures 7 sounds 1 (shadows 1 with the tour) | 0 |
| `mode=bot level=1 frames=600` | map_pos 452.0, 39 entities, created 243, freed 204 | the same; top MAE 0.20 (the perf line), bottom identical |

Pace on Azahar: the front end's start screen drops the pacer to 30 Hz under the emulator in both
binaries (gpu_tail about 30 ms, the emulator's GPU) and it never fits again (gpu under 14 ms), so
`pace changes=0 misses60=0` cannot be shown there; `mode=bot level=1` holds 60 Hz with 0 misses.
Left: the console play session; the sequels' comic pages and model views (per mission, first use);
the band's first build (26 ms at the first frame of a level, inside the grace).

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

## Frame synchronisation (engineering round 2, P2)

An audit of when the CPU may write or free memory the GPU reads, under the default `gpu_kick=1`,
against the sources of the pinned image (citro3d 1.7.1 `source/renderqueue.c`, `base.c`,
`texture.c`; libctru 2.7.0 `source/gpu/gxqueue.c`, `gx.c`, `gpu.c`). Then what changed, and the two
diagnostics for the console (`arena_poison=1`, `state_check=1|2`).

### What citro3d and libctru actually do

- **The GX queue** (gxqueue.c) holds the frame's GX commands (command lists, memory fills, texture
  copies, display transfers), at most 32 per frame (C3D_Init; one more is an `svcBreak`). Once
  running it submits up to 3 at a time to GSP and counts completion interrupts (`lastEntry`; VBlank
  and PSC1 ignored); `isRunning` drops when every entry added so far has completed, and the queue's
  callback runs (citro3d's `onQueueFinish`: the buffer swaps once C3D_FrameEnd set them up). A
  queue that is running or merely *active* starts each new entry as it is added.
- **C3D_FrameBegin(0)** (renderqueue.c) skips C3D_FrameSync and calls `C3Di_WaitAndClearQueue(-1)`:
  `gxCmdQueueWait` until `isRunning` is false, then Stop and Clear. So it waits for the completion
  interrupt of **every** entry of the previous frame, the lists kicked in the middle of the frame
  included: a command list's interrupt (P3D) comes at its `GPUREG_FINALIZE`, after its draws and
  the frame-buffer flush citro3d writes before it (`C3Di_SplitFrame`). Nothing the previous frame
  submitted is still executing when C3D_FrameBegin returns; our frame loop's own
  `C3D_FrameSync` before it is only the vblank clock.
- **gpu_kick** (pica_frame.cpp kickGpu): `gxCmdQueueRun` after each eye makes the queue active;
  the later splits (the clears' splits, the next eye's kick, C3D_FrameEnd's last list and the three
  display transfers) start as they are added. When the GPU drains the queue between two kicks the
  callback runs with no swap pending (harmless) and the next add restarts it. C3D_FrameEnd's
  `gxCmdQueueRun` is then a no-op: the queue runs already.
- **C3D_FrameSplit(flags)** submits the list with `GX_ProcessCommandList(.., flags)`: only bit 1
  (`GX_CMDLIST_FLUSH`) makes GSP flush the list from the data cache. citro3d itself splits with
  flags 0 inside a frame in `C3D_SyncTextureCopy`, `C3D_SyncDisplayTransfer` and
  `C3D_SyncMemoryFill`; with `end_flush=cmdlist` (forced by gpu_kick) C3D_FrameEnd no longer
  flushes the linear heap either.
- **C3D_TexLoadImage** to VRAM is `C3D_SyncTextureCopy`: outside a frame synchronous (wait for the
  queue, copy, `gspWaitForPPF`); **inside a frame asynchronous**: an unflushed split plus a
  `GX_TextureCopy` queued behind it, one per mip level.

### Life cycle of the per-frame linear memory

| buffer | written | read by the GPU | fence before the CPU writes or frees it again |
|---|---|---|---|
| arena half H (frame N, H = N mod 2) | from `pica::frameBegin` of N (C3D_FrameBegin of N has returned) to `frameEnd`; nothing allocates from the arena in `prepare()` (checked: every `alloc` site is in a draw) | the lists of N, kicked or ended, until the last completes | C3D_FrameBegin(N+1) waited for all of them; H is written again at N+2: one frame of margin. Under gpu_kick the second eye's kicked list of N cannot still run when prepare(N+1) or frame N+1 writes the other half (H xor 1, last read in N-1) nor when N+2 writes H |
| mid-frame flushes | `LinearArena::flushPending` before every split | the split just submitted | cache lines shared with the next allocations are written back with unchanged bytes: harmless |
| deferLinearFree (meshes, the 2D vertex caches, the water grid) | freed at a later frameBegin | up to the frame the block was deferred in | that frame's lists completed before the next C3D_FrameBegin returned: **safe**, gpu_kick or not (the block can only be referenced by frames up to the one open when it was deferred) |
| terrain retired chunks | freed 2 arena frames later | the frame they were retired in | same fence: safe |
| `Texture2D::destroy` (before: `C3D_TexDelete` at once) | the memory freed immediately | a frame in flight (prepare runs before C3D_FrameBegin's wait) or the current frame | **unsafe**: a texture or mesh allocated next at the same address (linear textures are written in place by the CPU) is read by the GPU instead of the old one: one pass garbled |
| a texture created inside a frame (the late loads of play: weapon effects, items) | staging buffer, `linearFree` right after `C3D_TexLoadImage` | the queued texture copy, later | **unsafe** three ways: the copy reads a freed (reusable) staging buffer; the command list split before it is **not flushed** (the GPU may execute stale command words left in RAM from an earlier frame at that offset of the 1 MB command buffer: random state and draws for the rest of that list); one queue entry per mip level towards the 32-entry limit (an `svcBreak`) |
| C3D_FrameEnd under gpu_kick | | | a narrow race, not fixed: the queue is running while C3D_FrameEnd adds the three transfers and sets the swap flags, so a callback between two adds could swap the top screen before the left eye's transfer (one stale left-eye frame; never a wash). It needs the thread preempted for a whole transfer between two adds |

### What changed

1. **An explicit fence** (pica_frame.h): every `frameBegin` opens a frame serial; it checks that
   the GX queue is idle (`gxCmdQueueWait(.., 0)`; `AS3D_FENCE` and a blocking wait if it ever is
   not) and marks every earlier serial complete; each arena half records the serial that took it
   (`halfHazards` counts a half taken before its last frame was done: never expected). Outside a
   citro3d frame `gpuDone(serial)` polls the queue, `waitGpuIdle()` waits.
2. **Deferred frees on the fence**: `deferLinearFree` tags the block with the current serial and
   frees it once that serial is complete (at a frameBegin or `collectDeferred`), the same as before
   in practice; **`Texture2D::destroy` now defers** its memory the same way (`deferTextureDelete`);
   `Texture2D::create` outside a frame first collects what is done (waiting for the GPU only when a
   texture is pending, so a level's new textures find the old ones' VRAM).
3. **No GX copy inside a frame**: a texture created between frameBegin and frameEnd goes to linear
   memory (written by the CPU, flushed), never through citro3d's unflushed split and asynchronous
   copy; `AS3D_TEX_IN_FRAME` logs the first 20 (P1's precache and frame-end deferral should make
   them rare; this is the safety net).
4. **The water's own vertex buffer** (water_renderer_pica.h `WaterPass::buf_`): the plane's tiles
   and the grid's heights go to a double-buffered linear buffer of their own (2 x 192 KB for the
   plane; the grid's used vertices x 4 bytes), each half taken at the first water draw of a frame
   once `gpuDone` its last frame, each eye's vertices flushed when written; the memory is released
   through deferLinearFree. A full half falls back to the frame arena (counted).
5. **Explicit state for the water and the terrain**: right before the water's draw (plane and
   grid), before the terrain's base draws and again before its tile overlays, the shadow copies and
   texture bindings are forgotten, so the depth map, depth test and mask, blend, cull, alpha test,
   fog and textures are all issued to citro3d and emitted with the pass's first draw, whatever the
   pass before left. This adds a few command words per pass and changes no pixel.

### arena_poison=1

At frameBegin the arena half just taken is filled with pseudo-random finite floats (+-3000, whose
bytes are random colours) and flushed; the same for each water buffer half and for every deferred
block when its fence lets it go. A GPU still reading any of them (a fence failure) or a draw still
using a freed block (a CPU-side use after free) shows garbage triangles at once, on Azahar and on
the console. On Azahar (2026-10-10, the `runs` below) no dump of any poisoned run shows garbage.

### state_check=1|2: the self-check for the wash

The wash: the water covers terrain and roads but never the models. The opaque models are drawn
*before* the water, so the water is not drawn without its depth test (it would cover the models
too); what fits is the terrain's depth missing or wrong where the water is drawn: the terrain drawn
without depth writes or with another depth map than the water's, its depth cleared after it was
drawn, or the GPU running other commands than the ones recorded (an unflushed command list). The
check looks at each of these.

- `state_check=1`, before the terrain's base draws (`pass=terrain`), its overlays
  (`pass=terrain_tiles`) and the water's draw (`pass=water` / `water_grid`): the expected state
  against the shadow copies (`layer=shadow`) and citro3d's context (`layer=c3d`: depth test,
  function, write mask, depth map mode/scale/offset, blend, cull, alpha and stencil test, early
  depth, the bound textures, the target's colour and depth buffers and masks), and the water's depth
  map against the terrain's of the same target (`field=depthmap_vs_terrain`):

      AS3D_STATE_MISMATCH pass=water layer=c3d field=depth_func expected=GEQUAL actual=ALWAYS frame=N target=0x...

  (the first 64 lines, then at most one a second; all counted). At every frameBegin the memory fill
  and display transfer engines must be idle (`AS3D_GPU_BUSY` otherwise). Every 60 frames:

      AS3D_STATE frame=N t=S water=plane planeZ=-67.99 quads=5 tiles=232 near_tiles=0 split=0 max_span=0.52
        min_w=272.4 buf=H/offset/bytes fallbacks=0 near_terrain=4.0 near_water=4.0 arena=H/KB fence=done/serial
        busy=0 hazards=0 deferred=n/max tex_in_frame=created/destroyed cmdbuf=0.17 queue_max=5 checks=..
        probes=.. mismatches=0 gpu_mismatches=0 gpu_busy=0 psc=../.. ppf=.. fields=none

- `state_check=2` adds, right after the terrain's base draws and the water's draw, a submit and
  wait of the command list so far and a read-back of the GPU's registers (GSPGPU_ReadHWRegs:
  depth map enable/scale/offset, cull, depth test and masks, blend, alpha and stencil test, the
  depth and colour buffer addresses and write enables, texture unit 0's address), compared with
  citro3d's context: `AS3D_GPU_MISMATCH pass= field= expected= actual= frame=`; the first read-back
  of each pass prints every register (`AS3D_GPU_REGS`) so the console's read-back can be judged
  (a register that never matches from the first frame does not read back; what matters is a change
  that coincides with the wash). It stalls the CPU twice per eye (the pacer goes to 30 Hz).
- With either level, **L** (unbound in play) marks the moment: `AS3D_WASH_MARK n= frame= step=`,
  the top, right and bottom targets dumped to `mark<NN>_*.ppm` (is the wash in the frame buffer?),
  the left eye's depth buffer to `mark<NN>_depth.ppm` (bytes 3, 2, 1 of each D24S8 word as R, G, B;
  the cleared far depth is black, any colour is a written depth: is the terrain's depth there
  under the wash?), and the next 3
  frames log every check in full (`AS3D_STATE_FULL`) with the register read-back.

Reading the `AS3D_STATE` line: `buf=` is the water buffer's half, the offset of the last eye's
vertices and the frame's bytes (60 is even, so the half printed is always the same one);
`queue_max` is the most GX queue entries a frame had at frameEnd, before C3D_FrameEnd adds its last
list and three transfers (the queue holds 32); `fence=` the completed and the current serial;
`deferred=` the blocks waiting for their fence now and at most; `psc=`/`ppf=` the fill and
transfer engines' control registers at the last frame start (`202` = finished, idle).

### Azahar runs (2026-10-10, main 45b1125 + this round)

| run | frames | result |
|---|---|---|
| mission 1 `mode=bot` mono, defaults | 1959 (2750 steps) | dumps at steps 600 / 1400 / 2700 equal to main's but for the perf text line (MAE 0.007 / 0.198 / 0.016, every difference in rows 230-235); main against a second run of main: MAE 0 |
| Gulf mission 1 `mode=bot slider=1`, defaults | 286 | step 602 left and right eyes: the perf text line only (MAE 0.24) |
| `mode=sim` as3d mission 1 (3600 steps), Gulf mission 1 (1200) | | end states (`level 1: ... map_pos`, `created ... freed`) equal to main's |
| mission 1 bot, `state_check=2 arena_poison=1` | 840 | 0 mismatches over 2484 checks, 0 GPU mismatches over 1656 read-backs, 0 fence waits, 0 hazards, engines idle at every frame start; dumps clean (a step or two off main's: the stalls) |
| Gulf mission 1 bot stereo, `state_check=2 arena_poison=1` | 164 | the same over 984 checks and 656 read-backs (the wave grid: `water_grid`, its heights in the water buffer); step 603 both eyes clean |
| mission 1 `mode=play` mono, `state_check=1 arena_poison=1`, pad_script: forward, fire, missiles, weaving, the hint boxes dismissed, L at frames 300 and 700 | 1203 (4200 steps) | over the river and the bridge while firing: 0 mismatches over 3564 checks, `AS3D_WASH_MARK` and the `mark01/02` top, bottom and depth dumps, `AS3D_STATE_FULL` for 3 frames |
| the same in stereo with `precache=0 state_check=2` (everything loads on first use) | 1052 | one texture created inside a frame (`AS3D_TEX_IN_FRAME frame=477 256x64`, in linear memory), clean; 0 mismatches over 6096 checks and 4064 read-backs |

The register read-back works on Azahar (`AS3D_GPU_REGS pass=terrain ... map_scale=0xbd0000
depth_mask=0x1f71 depth_write=0x3 blend=0x1010000`, the water's `blend=0x76760000`); on the console
it is the first thing to look at in the log. Azahar never showed the wash, so these runs say the
diagnostics are quiet when the state is right, not what the console will show.

### For the console (the next session)

One play session settles it. `run.ini`: `mode=play` (the front end, or `level=1` for the
tutorial), **`state_check=1`**, everything else at its default. Play until the wash shows; press
**L** as soon as it appears and once more while it lasts (each press freezes the game about a
second for the dumps). If time allows, a second session with `state_check=2 arena_poison=1`
(slower: the pacer drops to 30 Hz). Then `tools/3ds_fetch_logs.sh` plus the `mark*_*.ppm` files.
Reading the log around each `AS3D_WASH_MARK`:

| the log at the mark | means | next |
|---|---|---|
| `AS3D_STATE_MISMATCH layer=shadow` | the renderer's own bookkeeping issued another state | the setter named by `field=` |
| `AS3D_STATE_MISMATCH layer=c3d` | citro3d's context differs from what we issued: a C3D call outside render_state_pica.cpp, or the mirror | `AS3D_STATE_FULL` has the context |
| `field=depthmap_vs_terrain` | the water's depth map is not the terrain's | the near planes in `AS3D_STATE` |
| `AS3D_GPU_MISMATCH` (level 2) with no CPU mismatch | the GPU ran other commands than the ones recorded: a corrupted or unflushed command list | `tex_in_frame`, P1's `AS3D_LATE_TEX`, `cmdbuf`, `queue_max` |
| `AS3D_FENCE`, `AS3D_GPU_BUSY`, `hazards>0`, or garbage with `arena_poison=1` | a buffer was rewritten while the GPU read it | the fence |
| none of these, the wash in `markNN_top.ppm` | it is in the frame buffer: look at `markNN_depth.ppm` | black (far) depth under the washed terrain = the terrain's depth is missing (a lost depth write or a late clear) |
| none of these, no wash in the dump | the screen showed another buffer than the one drawn | the display swap (the C3D_FrameEnd race above), `gpu_kick=0` |

The A/B list of the resume section (`water=0`, `water_tile=0`, `gpu_kick=0`, `pace=`) stays the
fallback when the log stays silent.

## Stereo at 60 (round 2, P3)

Goal: two eyes at 60 Hz on the New 3DS. The console (drop 4/5) measured mono GPU 7.3 ms avg / 9.1 ms
p95 and stereo 13.5 / 16.5, so the pacer locks stereo to 30 Hz; RGB565 targets gained nothing (the
cost is vertices and texturing, not fill). Stereo at 60 needs the two eyes under about 14 ms with
margin (target 12 ms avg): a third of the stereo GPU work. Azahar cannot measure GPU time, so this
round cuts what the GPU is given (draws, submitted vertices, texture binds) and shows it with the
pass counters (`AS3D_PASS_MS`, `<pass>=<ms>/<draws>/<verts>` per displayed frame, both eyes) and the
new `AS3D_STEREO60` line, plus dumps against main for the look. Only `engine/src/render/pica/` and
`apps/3ds/` changed: the desktop goldens are untouched (tools/ci.sh OK).

### What changed

1. **Terrain far-chunk LOD** (terrain_renderer_pica.cpp, `RenderQuality::terrainLod`). A chunk (8 rows
   x the level's width) whose box is farther than `terrainLodDistance` (520 world units) from the
   *central* camera (the same choice for both eyes) draws every second vertex in both directions.
   Each chunk keeps a second, "flattened" vertex buffer built once at level start (and again for a
   crater): its odd vertices are moved onto the coarse triangles (height and colour interpolated
   along the coarse row edge, column edge or diagonal they lie on), so
   - the coarse grid keeps the fine diagonal (v00-v11): every fine overlay quad drawn on the
     flattened vertices lies inside one coarse triangle, and the tile overlays (second pass, depth
     bias) keep passing their depth test on coarse chunks (they draw from flattened overlay buffers);
   - two coarse neighbours share their edge row bit for bit (the same float operations);
   - next to a fine chunk, the gap between the fine edge polyline and the straight coarse edge is
     closed with vertical fill triangles (the fine edge's odd vertices are appended to the
     flattened buffer: W more vertices), and the coarse triangles along both edge rows are split
     at the edge midpoints so no T-junction is left. Four index lists per row count (fill above,
     below, both, none), in linear memory;
   - the dynamic-light stream covers the appended vertices (copied from their grid vertex); the
     lights, detail texture, base blocks and fog are unchanged.

   A full coarse chunk is 480 indices (+96 per fill side) against 1536. Memory: a second buffer per
   chunk and its overlays (mission 1: about 250 KB of linear memory). Default: on while two eyes
   are drawn (`pica::frameEyes() == 2`, set by the frame composer before the world's prepare),
   off in mono, so the mono picture does not change.
2. **Shadow atlas** (shadow_renderer_pica.cpp, shadow_pica.h, `RenderQuality::shadowAtlas`). Each
   silhouette goes into a 512x512 A8 page in linear memory instead of a texture of its own; the
   shadows of one page are one draw (the draped uvs are mapped into the cell when the vertices are
   grouped). A cell keeps the texture's power-of-two size and texels and sits at a multiple of 8
   with 8 zero texels to the next cell: its mip levels 1..3 (its own box chain) stay inside it and
   the bilinear edge reads zeros (a silhouette's edge texels are near 0 anyway). Levels 4.. are
   filtered from level 3 of the whole page. The sequels' pages have no mips (their rule). At most
   8 pages; a silhouette that finds no room gets its own texture as before (`full=` counts them).
   The pages belong to the ShadowRenderer and go at the next level's `setRules`; an atlas map has
   no texture of its own (`ShadowMap::valid()` is false), the world renderer keeps its cell
   (`lastShadowAtlasRef()`) with the map. Pages are written outside frames, into cells no draw of
   the frame in flight reads. The silhouette cache file (`cache/*.shadows`) is unchanged (it stores
   the raster's alpha). Memory: mission 1 5 pages = 1707 KB (the separate textures were 982 KB),
   Gulf mission 1 6 pages = 1536 KB. Default: on, mono too (the mono dumps equal main's).
3. **Mesh vertices** (gpu_mesh_pica.cpp). `GpuMesh::upload` merges corners whose PICA vertex
   (position, u8 cube weights, uv, and the env normal of smooth meshes) is byte-identical and drops
   triangles with two corners at one position; the GPU reads the same values, so the picture is
   the same. Mission 1: 13006 -> 12609 vertices (3 %: faceted meshes keep a normal per face); Gulf
   mission 1: 36710 -> 32032 (13 %); indices barely change (27954 -> 27870). Evaluated, not done:
   16-bit uv/position attributes (28 -> 20 bytes a vertex, but a per-mesh scale uniform and
   rounding in the picture, for a fetch cost that is not the bottleneck); reordering the indices
   for the vertex cache (changes the blend order inside transparent meshes); surfaces submitted
   twice for both sides (none: culling is per material). The brief's "12k mesh vertices per eye"
   is not what the counters show: opaque + transparent are about 1.9k unique vertices per eye in
   mission 1 (Gulf about 9k).
4. **The touch band** (ctr_frame.cpp; found by the counters): its circles were tessellated with
   48 segments per ring, **16032 vertices every frame** on the bottom screen, more than the terrain
   of both eyes. 24 segments (`touch_segments=`) keep the edge within a quarter pixel at the band's
   radii: 8256 vertices; bottom dump MAE 0.09 against main (99.4 % of pixels within 8).

Reviewed and left: the fog table is bound once per pass and frame (both eyes share the planes); the
brightness pass is one quad per eye (fill only); the particle (1536) and sprite (768) caps do not
bite in mission 1 or Gulf mission 1 (at most 1.5k particle vertices a frame in Gulf stereo); there
is no second full-screen pass. The water counter under-counts the tiled plane (it counts the
quads, 30 vertices, while about 230 tiles, 1.4k vertices, are drawn per eye): small either way.

### Counters (Azahar)

`mode=bot level=1 frames=1800 perf=1 brightness=0.5 audio=0`; draws / vertices per displayed frame,
the first two 300-frame reports. "off" is this build with `terrain_lod=0 shadow_atlas=0
touch_segments=48` (main 8f15107 plus the mesh merge); "main 34511c4" the binary before P1 and P3.

| run | terrain | shadows | opaque | rest | touch | all passes |
|---|---|---|---|---|---|---|
| as3d m1 stereo, main 34511c4 | 16 / 15342 | 28 / 2394 | 33 / 3048 | 42 / 930 | 9 / 16032 | 154 / 38689 |
| as3d m1 stereo, off | 16 / 15342 | 28 / 2394 | 33 / 2940 | 42 / 902 | 16 / 16032 | 161 / 38553 |
| **as3d m1 stereo, P3** | 16 / **10370** | **4** / 2394 | 33 / 2940 | 42 / 902 | 16 / **8256** | **138 / 25804** |
| as3d m1 mono, main 34511c4 | 7 / 7620 | 12 / 958 | 16 / 1416 | 20 / 374 | 9 / 16032 | 78 / 26830 |
| as3d m1 mono, off | 7 / 7620 | 12 / 958 | 16 / 1343 | 20 / 364 | 16 / 16032 | 86 / 26746 |
| **as3d m1 mono, P3** | 7 / 7620 | **2** / 958 | 16 / 1343 | 20 / 364 | 16 / **8256** | **76 / 18970** |
| Gulf m1 stereo, off | 11 / 16379 | 43 / 9012 | 73 / 10046 | 124 / 8456 | 17 / 16039 | 332 / 68611 |
| **Gulf m1 stereo, P3** | 11 / **10854** | **10** / 8903 | 73 / 10068 | 121 / 8336 | 17 / **8263** | **295 / 55002** |
| Gulf m1 mono (`pace=30`), main 34511c4 | 5 / 8248 | 22 / 4546 | 37 / 6000 | 62 / 4406 | 11 / 16039 | 173 / 43686 |
| **Gulf m1 mono (`pace=30`), P3** | 5 / 8258 | **5** / 4585 | 37 / **5043** | 62 / 4295 | 17 / **8263** | **163 / 34928** |

The touch draws went from 9 to 16 with P1's cache breaks, in both columns. In stereo about half of
the visible chunks are coarse (`coarse_chunks=1400/2938` in mission 1, 1612/3200 in Gulf). Per
stereo frame, mission 1: 161 -> 138 draws, 38.6k -> 25.8k vertices (-33 %); Gulf: 332 -> 295
draws, 68.6k -> 55.0k (-20 %). Of that, the top screen (both eyes, where the 13.5 ms go) loses
5.0k terrain vertices and 24 shadow draws in mission 1 (5.5k and 33 in Gulf), the bottom screen
7.8k vertices once per frame. On the console the gain is to be measured: a third of the vertices
is not a third of the milliseconds if the texture reads dominate.

The log line, every 300 frames after `AS3D_PASS_MS`:

    AS3D_STEREO60 eyes=2 terrain_lod=1 coarse_chunks=1400/2938 (per terrain render) shadow_atlas=1 pages=5 cells=145 full=0 atlas_kb=1706.6 mesh_vertices=13006->12609 mesh_indices=27954->27870

### The look (Azahar dumps)

Matched dumps at step 600 (`pace=60` in stereo so both runs dump the same step), this build
against the same build with the three knobs off, or against main:

| pair | top (each eye) | bottom |
|---|---|---|
| as3d m1 stereo, P3 against off | MAE 1.61, 91.9 % of pixels within 8 | 0.09, 99.4 % |
| Gulf m1 stereo, P3 against off | MAE 0.22, 99.2 % | 0.09, 99.4 % |
| as3d m1 mono, P3 against main 34511c4 | MAE 0.15, 99.8 % (atlas, mesh merge) | 0.09, 99.4 % (touch band) |
| as3d m1 mono, off against main 34511c4 | MAE 0.07, 99.9 % (the perf text line) | 0.00 |
| Gulf m1 mono `pace=30`, P3 against main 34511c4 | MAE 0.16 (an earlier P3 build, same step 602) | 0.09 |

The mission 1 stereo difference is the coarse far third of the screen: hills slightly smoother, the
texture following the moved heights by a pixel or so, the ridge line almost the same; of the 101
pixels that differ by more than 64, 82 are the perf text line and the rest isolated (no crack or
pinhole line at the chunk seams). Gulf mission 1's far rows are mostly flat or water, hence the
small figure. The shadows and the models are the same.

### Run.ini keys and defaults (also in apps/3ds/README.md)

| key | values | New 3DS profile | Old 3DS profile |
|---|---|---|---|
| `terrain_lod` | `auto` (the profile), `0` never, `1` stereo only, `2` always | stereo only | stereo only |
| `terrain_lod_dist` | world units | 520 | 520 |
| `shadow_atlas` | `0`, `1` | on | on (it has no silhouettes: blob shadows) |
| `touch_segments` | 8..96 | 24 | 24 |

`AS3D_QUALITY` logs `terrainLod=stereo terrainLodDist=520 shadowAtlas=1`. The mesh merge has no key
(the GPU reads the same values).

### For the console

1. The GPU gate: `mode=bot level=1 frames=3600 perf=1 pace=60 gpu_kick=0` with the slider up:
   PERF_SUMMARY gpu avg / p95 against drop 4's 13.5 / 16.5 ms. Then the same with `terrain_lod=0`,
   `shadow_atlas=0`, `touch_segments=48` one at a time, to give each cut its milliseconds.
2. The default (`pace=auto`) in stereo: stereo still starts at 30 Hz and goes to 60 after 120
   frames whose GPU time stays under 14 ms (`AS3D_PACE hz=60`); `PERF_SUMMARY pace` and
   `frames_at_30hz` say whether it holds.
3. Look, slider up: the far third of the screen (coarse hills are a little smoother; a chunk
   changes level when it comes within 520 units, about 3 times the player's depth: watch for a pop
   or a crack at that distance, and for tile roads on far hills); the shadows as before; the touch
   band's circles round.

If stereo still falls to 30:
- `terrain_lod_dist=400` (or 320): more coarse chunks, the pop closer.
- Measure per pass with `passes=` (all minus `terrain`, `shadows`, `opaque`, `water`, `rest`,
  `particles` in turn) under `pace=60 gpu_kick=0`: the GPU time each removal saves is that pass's
  cost, so the next cut goes where the milliseconds are.
- The bottom screen only needs 30 Hz: in stereo, drawing it every other frame (no `drawOn` of the
  bottom target, so citro3d neither transfers nor swaps it) saves its GPU share (HUD and touch band,
  about 8.5k vertices) on alternate frames. Not done here: it meets the C3D_FrameEnd swap race of
  "Frame synchronisation".
- The coarse chunks with the one-stage TEV (no detail texture: half their texture reads), if the
  console says texturing is the cost; a second, coarser level (every fourth vertex) beyond twice
  the distance.

## Sequels' CPU (round 2, P4)

The sequels' frame CPU on the main core: prepare (`GameFrame::prepare`, outside the frame) and the
command recording (`submit_ms`: drawTop per eye + drawBottom). Measured on Azahar with
`mode=bot level=N frames=1800 perf=1 fixed_steps=2` (`fixed_steps=1` for AirStrike 3D), so that
two builds profile and dump at the same simulation steps; Azahar's CPU ticks are not the
console's (they behave like an instruction count at the 268 MHz tick rate), their ratios are what
these tables show. Before = main `485ee49` plus the profile commit only; after = this round.

**The prepare split** (`AS3D_PREP_MS`, every 300 frames: `<entry>=<ms>/<items>` per frame;
`prep` = perf.csv prepare_ms; `prep_world` its WorldRenderer::prepare, which is `prep_terrain`
(craters), `prep_scene` (WorldScene::prepare; items = dynamic lights; of it `prep_order` the think
order and `prep_entities` the entity pass, items = entities), `prep_marks`, `prep_water` (water and
lightning), `prep_shadows` (placement, items = requests; of it `prep_drape` the ShadowPass build,
items = shadows draped this frame), `prep_meshes` (the mesh passes' submits, items = records) and
`prep_stats` (the linear-skin statistics); `prep_flow` the HUD state or GameFlow::draw):

| ms per frame | prep | scene (entities) | marks | shadows (drape) | meshes | stats |
|---|---|---|---|---|---|---|
| Gulf 1 before | 10.38 | 2.15 (1.96) | 0.80 | 4.41 (4.17) | 1.88 | 0.91 |
| Gulf 1 after | 3.19 | 1.03 (0.84) | 0.20 | 0.86 (0.60) | 0.68 | 0.18 |
| Gulf 3 before | 16.05 | 3.14 (2.85) | 2.20 | 6.02 (5.79) | 2.99 | 1.46 |
| Gulf 3 after | 5.34 | 1.50 (1.21) | 0.64 | 1.16 (0.88) | 1.30 | 0.51 |
| AirStrike 2 3 before | 12.19 | 3.38 (3.14) | 1.52 | 2.64 (2.32) | 3.06 | 1.35 |
| AirStrike 2 3 after | 4.30 | 1.47 (1.23) | 0.45 | 0.96 (0.55) | 0.98 | 0.19 |

Particles and sprites are built in the recording (the first eye), not in prepare; the water's
prepare is the two sine tables (0.13 ms); the grid is animated per visible chunk in the draw
(M5.1: proportional to the visible grid; the attract level's whole-ocean view is the worst case,
32 chunks, water 4.5 ms under Azahar).

**What changed in prepare** (all with identical output: the dumps below):

- WorldScene keeps each entity slot's last mesh, material and texture lookup with the path or
  skin it came from (the cache's path normalisation, hashing and the material map's string copy
  ran for every entity every frame); a failed texture is not kept (the cache retries it).
- ShadowPass::build no longer copies every cached projected shadow into one list and again into
  texture order: each item points at its vertices and draw() writes the arena once, in group
  order; a planar shadow whose rectangle has not moved since the last build reuses its draped
  geometry. buildTerrainDecal skips the four clips for triangles inside the rectangle or outside
  an edge (the same output).
- MarkPass keeps each mark's cut geometry (entity slot, generation, rectangle) until a crater:
  the sequels fade their marks every frame, which re-cut every mark.
- MeshPass::finish orders indices instead of stable-sorting the 300-byte records (that sort was
  most of `prep_meshes`); records are filled in place.
- The linear-skin statistics (AS3D_DUMP's list) collect definition pointers and join the names
  only when the set changes; the dynamic-light evaluation (models and terrain) has a conservative
  squared-distance early out before the square root.

**Recording** (`AS3D_PASS_MS` sum and PERF_SUMMARY split_ms submit; world draws per frame):

| | prepare before / after | submit before / after | draws | particles | terrain | rest (draws) |
|---|---|---|---|---|---|---|
| Gulf 1 | 10.38 / 3.18 | 10.80 / 9.63 | 176 | 1.11 / 0.68 | 1.70 / 1.28 | 2.10 / 2.00 (66) |
| Gulf 3 | 16.02 / 5.31 | 18.44 / 15.42 | 256 | 4.22 / 2.12 | 3.25 / 2.94 | 1.66 / 1.54 (54) |
| AirStrike 2 1 | 6.40 / 2.29 | 8.13 / 6.93 | 149 | 0.28 / 0.17 | 1.15 / 0.49 | 2.53 / 2.38 (85) |
| AirStrike 2 3 | 12.19 / 4.29 | 12.88 / 11.50 | 248 | 0.95 / 0.54 | 2.00 / 1.63 | 3.81 / 3.45 (129) |
| AirStrike 3D 1 | 2.68 / 1.20 | 7.89 / 6.83 | 90 | 0.42 / 0.26 | 1.68 / 1.01 | 0.76 / 0.73 (23) |

Arena bytes are unchanged (Gulf 3 397 KB per frame, max 621). The recording gains:

- Particles: `visitParticleQuads` (render/particle_quads.h) is buildParticleQuads' walk as a
  template over the vertex sink; the neutral builder writes the same ParticleVertex list, the PICA
  writes FlatVertex straight into the arena (sized by the live particles and the quality cap, the
  colour converted once per quad): half the particle pass.
- Terrain lights: computeLights evaluates only the vertices inside a conservative rectangle around
  the chunk's picked lights (terrainDynamicLightColors' rectangle variant, the same per-vertex
  code; outside it every light is beyond its radius and the stream adds 0).
- MeshPass::draw skips repeated material states and vertex buffers.

**The opaque sort by texture and material** (run.ini `mesh_sort=`, default 1): each run of
opaque-bucket records without blending and with depth test and write (blended or depth-write-off
records keep their place) is ordered by shader, skin, TEV and material state, ties in submission
order. The dumps at step 1500 of all five cases are pixel-identical to before's (outside the perf
text rows 228-239): no equal-depth overlap changed. It saves little: Gulf 1 texture binds 73.8 ->
72.1 per frame (the sequels' opaque models rarely share a skin in a row).

**Merging small meshes** (run.ini `mesh_merge=`, default 0): a run of small records with the same
skin and state in a row is baked into the frame arena in world space (each corner's lit colour as
the mesh shader computes it, or the unlit constant) and drawn with the flat shader in one draw;
the triangles keep their order, so blended effects compose as before. Measured and left off: with
records up to 128 vertices Gulf 1 went from 118 to 86 mesh draws but the `rest` pass from 2.00 to
3.01 ms (the CPU bake costs more than the draws), 90 KB more arena per frame and 135 pixels moved
at triangle edges (max 64); restricted to records of at most 32 vertices / 48 corners, Gulf 3
saves 14 to 28 draws for no time (opaque + rest 4.25 against 4.32 ms; 185 pixels differ by 1).
The per-draw profile says why (a temporary tick split, Azahar, AirStrike 2 mission 3): a mesh draw
is about 35 us, of which C3D_DrawElements 18.5 us with the same vertex buffer and 27 us with a new
one (citro3d's update context, the uniform upload loops over 96 registers for both shader stages,
about twenty command writes); the matrix product, our uniforms, state and TEV together 11 us.
**Merging a mesh's surfaces at upload** needs nothing: a .mdl has one texture and GpuMesh::upload
makes one surface, so every record is one draw already.

**The bottom screen in the sequels**: the direct modes draw the HUD (0.3-0.6 ms) and the touch band
(0.9-1.0 ms, 16-18 draws, cached per unit) as AirStrike 3D does. The front end (`mode=menu
game=gulf menu_bot=1`, Start, the helicopter, Operation 1): `bottom2d` in play 0.88-0.90 ms (23
draws, 8.3k vertices, ui_batch 0.2 ms: the cache units hit); the only `AS3D_SLOW_SUBMIT` with a
large bottom2d is the band's first build at the mission's first frame (15 ms, P1's known one);
the main menu itself rebuilds its list every frame (bottom2d 2.2-3.0 ms, ui_batch 1.3-2.0).

Checks: `tools/ci.sh` OK with the goldens unchanged (the neutral changes: WorldScene, decal
clipping, light early outs, the particle visitor, the terrain light rectangle with a new test);
`mode=sim` end states equal to before's (AirStrike 3D mission 1 3600 steps: map_pos 2551.9,
created 1725, freed 1642; Gulf mission 1 1200 steps: map_pos 1227.2, created 1486, freed 1332);
the bot runs' end states equal too.

New run.ini keys: `fixed_steps=N`, `mesh_sort=0|1`, `mesh_merge=0|1`; new log lines
`AS3D_PREP_MS` and `AS3D_MESH` (draws, environment draws, texture binds, state changes, merged
draws and records, merge arena KB, per frame, every 300 frames).

**Left (targets):** prepare is at or below 4 ms in Gulf 1 and AirStrike 2 1 and 4.3-5.3 ms in the
third missions (the sequels cut by 64 to 69 %, AirStrike 3D by 55 %); recording is cut 7 to 16 %, not by half: below 8 ms only
in AirStrike 2 mission 1. What remains is draw count times citro3d's per-draw cost: a vertex pool
shared by the meshes (indices rebased at upload, one attribute buffer setup, saving the 8.5 us of a
new buffer on 30-60 % of the mesh draws), an atlas for the touch band's icons (17 draws), a lighter
draw path than C3D_DrawElements (our own command words for runs that change only uniforms and the
index buffer; it bypasses citro3d's shadow state and needs the state_check of P2), and the terrain
lights of Gulf mission 3 (2.9 ms: large lights cover whole chunks). The console's own numbers come
with the next play session (prepare and submit columns of perf.csv, `AS3D_PREP_MS`).

## Packaging and launch (round 2, P6)

The port as a thing one installs: a `.3dsx` the Homebrew Launcher lists with its icon, a CIA for
the HOME menu, an SD package with its instructions, the high-score name through the system
keyboard, the first game's 3D banner on the top screen, and the life cycle reviewed.

### The package and the SD layout

    tools/3ds_package.sh [--no-build] [--no-cia]    # AS3D_BUILD_JOBS (default 2), AS3D_VERSION (default 0.3.0-dev)

builds (tools/3ds_build.sh) and writes `out/3ds/package/<version>-<commit>/` and its zip
`out/3ds/package/openairstrike-3ds-<version>-<commit>.zip`:

| file | what |
|---|---|
| `3ds/openairstrike/openairstrike.3dsx` | the app; its SMDH (CMake `ctr_generate_smdh`) has the short title "OpenAirStrike", the long title "OpenAirStrike: open source engine for AirStrike 3D and its sequels", the publisher "ProPaint / openairstrike" and `apps/3ds/icon.png` (the project's own icon) |
| `3ds/openairstrike/run.ini` | every line a comment: the defaults, and the keys an owner may want (game, audio, music, quality, logo, name_entry, perf) |
| `openairstrike.cia` | the HOME menu title (below) |
| `INSTALL.txt` | the SD layout, where each game's files come from (the game's `data/` folder; the texts file from the executable with `tools/extract_exe_texts.py`; the Android import keeps the same files), the music (`audio_tool render-music --all`), `dspfirm.cdc` (DSP1), the keys |

The script refuses to finish if the package holds any other file: no game data can get in. The
SD card:

    sdmc:/3ds/openairstrike/
        openairstrike.3dsx      run.ini (optional)
        as3d/   pak0.apk pak1.apk pak2.apk  Settings.xml  texts_v170.txt  music/*.ogg (optional)
        as2/    pak0.apk pak1.apk pak2.apk  Settings.xml  texts_as2.txt   music/*.ogg
        gulf/   pak0.apk pak1.apk pak2.apk pak4.apk  Settings.xml  texts_gulf.txt  music/*.ogg
        (written by the app: log.txt, launcher.bin, <game>/profile.bin, cache/)
    sdmc:/3ds/dspfirm.cdc

**Where the data is.** Always `kDataDir` = `sdmc:/3ds/openairstrike` (run_config.h), whatever the
launch: `ctr_main.cpp` makes the directory if it is missing (a fresh card: the log and the saves
go there), sets the working directory to it (no relative path can land elsewhere) and only logs
`argv[0]`, as `from=` at the end of the first log line. The Homebrew Launcher passes the `.3dsx`
path, 3dslink `3dslink:/<file>`; Azahar passes nothing for a `.3dsx` and a CIA has no argument:
`from=none` in every Azahar run. So the `.3dsx` may sit anywhere (`sdmc:/3ds/openairstrike.3dsx`
works too); the data does not move. Every engine path was already absolute (`user_data_ctr.cpp`,
the paks, the caches).

**run.ini is optional.** No file, or a file without `mode=`, gives `mode=play` (the front end:
the selector when two or more games are complete, the menus), `audio=1`, `music=1`. `mode=spike`
and any other value (`picatest`, `meshtest`, ...) go to the old path (`main.cpp` reads its test
modes itself), so the test modes work as before. Azahar: the package's `run.ini` (all comments)
plus `pad_script=90:A frames=500`: `mode=play (front end)`, the selector (`as3d`, `as2`, `gulf`
installed), as3d chosen, the main menu; no `run.ini` at all: the selector
(`out` files: scratchpad `p6/defaults_sel/`, `p6/noini/`).

**The hard exit** (`ctr_fatal.{h,cpp}`). When there is nothing to start (no game directory, or
no game with all its paks: the selector would have no font), when `game=` names no game, when
the session cannot start (a pak does not open: `mountGameFiles`' error), when the level start
fails or there is no graphics context, the loops record the reason (`setFatal`, logged as
`fatal: ...`) and return; `main()` then, after every graphics resource is gone, starts the
libctru console: the reason in red on the top screen, the expected SD layout (from the game
profiles) on the bottom one, and waits for A, B, START or the HOME menu's Close before it
returns to the launcher. With `dump_screens=1` the two consoles are written to
`fatal_top.ppm` / `fatal_bottom.ppm`. Azahar (`game=nosuch`): `p6/fatal_dump/fatal_top.png`,
`fatal_bottom.png`. A part-installed game is not fatal: the selector shows its card with
"Missing: ..." (M5.4).

### The CIA

Tools: the pinned devkitPro image has no makerom and no bannertool, so `tools/fetch_third_party.sh
ctrtools` fetches them into `${AS3D_DATA_ROOT:-repo}/third_party_local/`, each download into an
empty directory of its own under `_dl/`, checked against the pinned SHA-256 before anything
reads it:

- makerom 0.18.4, the Linux release binary of 3DSGuy/Project_CTR
  (`makerom-v0.18.4-ubuntu_x86_64.zip`);
- bannertool 1.2.3 of carstene1ns/3ds-bannertool, the maintained fork: Steveice10/bannertool
  answers 404 now, and the fork's Linux binary needs glibc 2.38 (the image has 2.36), so its
  release source tarball is compiled by the script inside the image with its g++ (no network,
  the source mounted read-only).

Both only ever run inside the pinned image with `--network none`, on copies in a scratch
directory. `tools/3ds_package.sh`: `bannertool makebanner -i apps/3ds/banner.png -a silence.wav`
(the banner: `tools/3ds_banner_image.py` draws it from `apps/web/site/icons/icon-512.png` and the
name, committed as a 256x128 PNG; the sound is 0.5 s of silence the script writes), then
`makerom -f cia -target t -exefslogo -rsf apps/3ds/openairstrike.rsf -elf openairstrike.elf
-icon openairstrike.smdh -banner banner.bnr`.

`apps/3ds/openairstrike.rsf` (from the devkitPro CIA template, cut to the app):

| | |
|---|---|
| title ID | **`000400000FA35D00`**: application, unique ID `0xFA35D` from the homebrew range 0xF8000-0xFFFFF; product code `CTR-H-OAS3D` |
| RomFS | none: the data stays on the SD card |
| memory | `SystemMode: 64MB` (Old 3DS), `SystemModeExt: 124MB` (New 3DS); libctru keeps its 32 MB linear heap and gives the rest to the heap |
| New 3DS | `CpuSpeed: 804MHz`, `EnableL2Cache: true` (the speed-up from the start; `osSetSpeedupEnable` still asks for it through `ptm:sysm`) |
| stack | `StackSize: 0x40000` (256 KB for the main thread) |
| file system | `DirectSdmc`, `DirectSdmcWrite`; no save data (`SaveDataSize: 0KB`), no ExtSaveData: the profile is `sdmc:/3ds/openairstrike/<game>/profile.bin` |
| services | `APT:U`, `cfg:u`, `dsp::DSP` (ndsp), `fs:USER`, `gsp::Gpu`, `gsp::Lcd`, `hid:USER`, `ir:rst` (C-stick, ZL/ZR), `ndm:u`, `ptm:sysm` (the speed-up), `ptm:u`; no csnd, soc, http, cameras |
| kernel | the template's syscall list, the DSP I/O mapping, VRAM mapped read-only |

**Azahar (main 56891af + P6):** `azahar --install p6.cia`: "Installed CIA successfully", the
title under `Nintendo 3DS/.../title/00040000/0fa35d00/content/*.app`. Booted from that `.app`:
`mode=bot level=1 frames=300`: mission 1, `map_pos 242.0, 33 entities, created 122, freed 89` =
`as3d_sim --level 1 --frames 300 --bot` (scratchpad `p6/cia_boot/`); `mode=menu` with
`menu_bot=1 menu_bot_steps=300` and the pad script of the name run: the main menu with the
banner, Start Game, mission 1 from the menus, `AS3D_BOT_END` with the same end state
(`p6/cia_menu/`). Azahar logs `ConfigureNew3DSCPU` on `ptm:sysm` as unimplemented: the call
reaches the service (the list is accepted; Azahar does not enforce it, the console does). Its
`memory: APPLICATION 0.0 MB free` line is Azahar's (the 3dsx shows 4068 MB): the console line
is the one to read. The level start's heap from the CIA was 17.4 MB.

**Only the console can confirm:** the HOME menu icon and banner (Azahar does not show a HOME
menu), FBI's install, the 124 MB mode and the 804 MHz/L2 flags taking effect (the log's memory
line and `New 3DS, speed-up on`), the service list against the real kernel (a missing service
shows as an `svcConnectToPort`/`srvGetServiceHandle` failure at start), and that the CIA reads
and writes `sdmc:/3ds/openairstrike/` (DirectSdmc).

### The name entry

The first game asks a name only for the high-score table (Screen::NameEntry, `screens_scores.cpp`:
it has no profile names); AirStrike 2 and Gulf Thunder the same (`SequelScreens::nameEntry`).
All three have the edit field id 2 and commit on Enter or Ok (insert, save, Top Scores). The
desktop feeds text with SDL text input while `Frontend::wantsTextInput()` (an Edit focused,
not in touch mode); touch mode draws its own keyboard, so the 3DS had the touch keys only (the
first game's by D-pad: a line, M5.2's deferral).

Now (`apps/game/name_entry.h`, shared, header-only; `ctr_name_entry.cpp`): when the name entry
opens, the front end loop (between frames) shows the libctru software keyboard applet: normal
type, one line, OK / Cancel, 31 characters at most (the profile's `HighScore::name` limit),
initial text the field's, empty accepted (the original accepts an empty name), and a filter
callback that refuses non-ASCII text and a name the field cannot hold whole (the field's own
rule: a character goes in while the text is narrower than the field less 20 virtual pixels,
`acceptedName`), with a message, keeping the keyboard up. On OK `submitName` puts the name into
the field, focuses it, and the next frame sends Enter: the screen's own commit. Cancel or a
failure leaves the screen's touch keys (taps; A on the field commits); Y opens the keyboard
again. The applet suspends the app: the APT hooks run (`apt: suspend`, the profile saved, the
sound paused, the clock reset on return). Log: `AS3D_NAME keyboard=applet|script result=ok|cancel|failed
code=N name="..." kept="..." ms=N`. run.ini: `name_entry=touch` (no applet), `name_script=TEXT`
(the keyboard's result without the applet, for scripted runs). No D-pad keyboard was added: the
applet covers the pad case.

Desktop tests (game_flow_test.cpp): "the name entry from a system keyboard" for the first game
(touch, console options; game over at 60000, Quit, the field found, the width rule, UTF-8
dropped, the focus moved away and given back, Enter: the entry in the table) and the same for
the sequels (`AS3D_GAME=as2|gulf`).

Azahar: `name_script=P6 Script` (`mode=menu`, Start Game by pad, `menu_bot=1 end_at=200
end_kind=gameover end_score=60000`, Quit by pad): `AS3D_SCREEN name=name`, `AS3D_NAME
keyboard=script result=ok name="P6 Script" kept="P6 Script"`, `AS3D_SAVE ... ok=1`, the Top
Scores screen with the entry (`p6/name_script/p6nm_05_scores_bottom.png`). With the applet:
`apt: suspend` and the profile save are logged when the screen opens, and Azahar opens its
keyboard window, but nothing from this machine reaches that window (XTest and XSendEvent are
dropped under GNOME's Xwayland, and its image cannot be read), so the return through the applet
is not verified on Azahar: it is the first console check below. The emulated SD's as3d profile
now has the "P6 Script" entry.

### The first game's front-end top screen

The desktop's main menu (`GameView::drawFront`, frontend.md 3.3): the attract level, the
letterbox (black bands, the red rule at virtual y 99), the rotating 3D banner `objects\banner.obj`
(additive, unlit) in the virtual rectangle (0, 0, 800, 200) between the menu's two halves, the
items; with `--dual-screen` all of it on the bottom screen. The 3DS had the attract level on top
with the game's title in text where the banner goes ("Deferred" in M2.1 and M3.4).

Now (`ctr_frame.cpp`): at an attract level's start the banner's mesh and material are loaded
into a cache of their own (`AS3D_BANNER model=models/banner/banner.mdl ... load_ms=31` on Azahar;
released with the renderer at every level start, as the model view's), and while the main menu
is up (`Frontend::bannerVisible`) the top screen gets, after the world and its brightness pass,
the letterbox's black band (0..39.6 px) and red rule, then the banner through a `pica::MeshPass`
with `BannerMesh::draw`'s camera and model matrix (the menu clock) in the top 400 x 100 pixels
(the desktop's 4:1 at the full width). Stereo: zero parallax at 40 units (the mesh is 30 to 45
away). The sequels have no banner: their title stays in text. Cost on Azahar: one draw, 526
vertices, `model=0.17 ms` in `AS3D_PASS_MS`. Side by side with `as3d_game --headless --attract 1
--no-logo --size 800x600 --ui-script` (shot at frame 120): scratchpad
`p6/frontend_desktop_vs_3ds.png` (desktop scaled to 400x300 left, the 3DS top and bottom right).

### Life cycle

Read and checked:

- HOME (APT suspend) and the lid (sleep), menu loop: `GameFlow::onBackground` (the in-game menu
  opens during play, so the world stays paused when it comes back), the profile saved, the sound
  suspended, the log flushed; on return the clocks restart (no replay). The direct modes pause
  the world themselves and resume it. Nothing was missing.
- START+SELECT (every loop and the selector), the main menu's Exit and the HOME menu's Close end
  the loop: `saveNow()`, the stack torn down, `Perf::close` joins the writer thread after its
  queued writes (the read plan, the shadow cache: `writeFileAtomic`), so no `*.tmp` is left.
  Azahar (`pad_script=200:START+SELECT`): `quit: START+SELECT`, `AS3D_SAVE ... ok=1`, `when=exit`;
  `find` over the SD tree: no `*.tmp` (`p6/quit/`).
- The hard exit above (missing data, pak, unknown game, level start, graphics).
- `quality=old`, `mode=bot level=1 frames=300` on Azahar: `AS3D_QUALITY profile=old ...
  renderHz=30 ... blobShadowsOnly=1`, 150 frames at 2 steps, the end state = the sim's (`p6/old/`).

Sound is not checked on Azahar: the emulated SD has no `dspfirm.cdc` (`ndspInit failed`, silent).

### For the console

1. Package: `tools/3ds_package.sh`, copy `3ds/` of `out/3ds/package/<version>/` over the card's
   `3ds/` (the paks, texts, music and `dspfirm.cdc` are already there), delete or comment out
   `run.ini`. Homebrew Launcher: OpenAirStrike with its icon and titles; start it: the selector
   or the game, music on; `log.txt` first line ends `from=sdmc:/3ds/openairstrike/openairstrike.3dsx`.
2. CIA: `openairstrike.cia` with FBI; the HOME menu shows the icon and the banner; start it: the
   same game; `log.txt` shows `from=none`, `New 3DS, speed-up on`, the memory line (124 MB mode
   expected on a New 3DS).
3. Name entry: lose with a high score (or `end_at=300 end_kind=gameover end_score=60000` with
   `menu_bot=1`): the keyboard appears, type a name, OK: Top Scores shows it; again with Cancel:
   the touch keys, Y brings the keyboard back; a too long name: the message.
4. HOME and back, lid closed and opened, during play and in the menus: paused, `apt:` lines and
   `AS3D_SAVE`; START+SELECT: back to the launcher; `ls` over ftpd: no `*.tmp`.
5. With `game=as3d` in run.ini, rename `as3d/pak1.apk` for a moment: the screens show "cannot start AirStrike 3D" and the
   layout; A returns to the launcher.
6. An Old 3DS if available: `quality=auto` picks the old profile, mission 1 plays.

## Stereo comfort and frame polish (round 2, P5)

The owner played the tutorial in stereo and turned the slider off during the next mission. Until
this round the eyes were simply `slider x eye_offset` (4 world units at slider 1) apart with zero
parallax at the player's depth (at least `min_focal` 40), whatever the scene. This round makes the
depth comfortable by construction, keeps every 2D element on the screen plane, puts hysteresis on
P3's terrain LOD and takes the menus' and the touch band's 2D rebuilds out of the frames. Only
3DS code changed, plus `Renderer2D::cacheBreak` calls in `MenuSystem::drawItems` (ignored by GL):
the desktop goldens are unchanged and the simulation is untouched (bot end states equal).

### The parallax model (engine/src/render/pica/stereo_comfort.h, desktop-tested)

A point at depth d lands `W A e (1/D - 1/d)` pixels apart in the two eyes (W = 400, A = 1 /
(aspect tan(fovY/2)) = 1.04, e = eye offset, D = focal depth): positive (behind the screen) beyond D,
negative (in front) nearer than D. Per frame, after `WorldScene` prepare:

- **What the camera sees** (`pica::sceneDepth()`, world_pica.h, measured by the PICA
  `WorldRenderer::prepare` only while two eyes are drawn): the nearest point of every opaque and
  transparent model's box inside the central view (`modelNearDepth`), and the nearest and farthest
  terrain or water by rays through a 5 x 6 grid of screen points over the height field
  (`probeTerrainRow`: a ray starts where it comes down to the highest surface, steps 6 % of the
  depth, bisects four times at the first point below the surface, stops at the far plane or the
  fog's end; sky rays count for nothing). One grid row per frame (all six after a level start or
  a mono stretch): a full probe every tenth of a second, about 70 steps a frame.
- **The clamp** (`comfortRatio`): the largest fraction of the slider's eye offset that keeps the
  nearest content within `max_parallax_px x slider` in front of the screen and the farthest terrain
  within `max_far_parallax_px x slider` behind it (defaults 10 and 12 px of the 400).
- **No steps** (`ComfortFilter`): the applied ratio moves towards that target by at most 3.0 per
  second downwards (a close object entering: full depth to none in a third of a second) and 0.4 per
  second upwards (two and a half seconds back); the screen plane (`FocalFilter`) follows the
  player's depth by at most 150 % of itself per second (a respawn or a camera jump eases instead of
  snapping the whole scene's depth). The clamp only lowers the offset, so the cull volume prepared
  for the slider's offset holds both eyes.
- **No player** (the attract levels behind the menus): `min_focal` put the screen plane at 40
  units and the whole landscape 32 to 39 px behind the screen at slider 1. The screen plane is now
  where the last frame's depths let the most depth through within both limits (`balancedFocal`: at
  the nearest content when the far limit allows, else the two limits shared).
- **The banner and the model view**: the first game's main-menu banner (P6: a mesh 19 to 45 units
  away seen with the world's eye offset, up to 15 px of depth, 6 px in front of the letterbox band
  it overlaps) now has its screen plane at its nearest vertex in view (`meshDepthRange`) and its
  depth limited to 4 px behind the screen at slider 1 (`kBannerDepthPx`); the sequels' model view
  has its screen plane at the model's nearest vertex (behind the panel's static fill, 2D drawn over
  it) and its back within `max_far_parallax_px`.

`stereo_comfort=0` restores the old behaviour (no clamp, no easing, min_focal for the attract
levels and the banner). Logs: `AS3D_STEREO_CONFIG` at each start; `AS3D_STEREO` every 300 frames
(effective eye offset min/avg/max against the slider's, the average ratio, the share of frames
clamped and of those limited by the near and by the far content, near and far parallax in px
average and maximum, the nearest depth, the focal depth, the probe's and the model scan's ms); the
`AS3D_DUMP` lines of `dump_at` and of the front end's dumps add `eye_offset_slider`, the effective
`eye_offset`, the target and smoothed ratios, the depths and the parallax they get;
`AS3D_BANNER_DUMP` adds the banner's focal depth.

### 2D on the screen plane

Every top-screen 2D element is drawn with the same orthographic matrix, no depth test and z = 0
in both eyes: the PAUSED banner, the perf line, the title on black, the attract level's title, the
model view's dot grid and static fill, P6's letterbox band and the brightness quad (one per eye,
the same value). Azahar, mission 1 paused (`pad_script=300:START`, `slider=1`,
`passes=clear,top2d,brightness,hud,hint,touch,bottom2d` so that only the 2D and the clear are
drawn): the two eyes are **identical, 0 of 96000 pixels differ**. With the world under them the
PAUSED and perf text match best at shift 0 (the alpha-blended glyph edges differ only by the world
behind them). The front end's dumps now write the right eye whenever two eyes are drawn (also
without a world), for the same check.

### The brightness (M3.6)

The quad is drawn once per eye with the same value after the top 2D layer: `brightness=` of
run.ini in the direct modes, the Settings value (`Frontend::brightness()`, 0.5 during the intro
pages) in the front end; the 2D-only check above includes it. The bottom screen gets no brightness
pass, which is what the desktop's dual-screen layout does (`GameView::drawDual` brightens the top
world rectangle only; the single-screen desktop brightens its whole frame, HUD included, because
the HUD is on it). Decided: the bottom HUD and menus stay unbrightened, as on the desktop's
dual-screen layout.

### Terrain LOD hysteresis (terrain_renderer_pica.cpp)

The chunk levels are decided once per frame (the first eye) and kept between frames: a fine chunk
turns coarse beyond `terrain_lod_dist + terrain_lod_band` (520 + 40), a coarse one fine again under
520 - 40. At most 2 visible chunks change level in a frame, those farthest from the camera's forward
direction first; hidden chunks change freely, and so does everything on the frame the LOD comes
on. `AS3D_STEREO60` counts `lod_switches=N (in M frames)`. The bot runs fly forward and cross each
distance once: 4 switches in 1200 frames of mission 1, 6 in Gulf mission 1, the same with
`terrain_lod_band=0`; the band matters when the camera hovers at the distance (play), where a chunk
can no longer flip from frame to frame. The pictures of the bot runs are those of P3.

### The 2D lists: per-unit cache, consolidation, the touch band at load

- `MenuSystem::drawItems` breaks the cache after each item and around the front widgets, and the
  composer between a screen's background and its items: a pulsing focus or a ticking counter
  rebuilds its own unit only.
- A list split into units that stays unchanged for 30 flushes is **consolidated**: its units'
  cached vertices are copied end to end into one slot and their runs joined where texture and blend
  agree (no conversion), and it is drawn from that slot until it changes. The static AirStrike 3D
  main menu stays at 4 draws; the touch band in play goes from 16 to 9 draws.
- The touch band's first list is built at the end of the level start (a mapper set up as the
  input's, `CtrInput::setUpTouch`, and the loaded session's HUD state) and converted during the last
  loading frame (`AS3D_TOUCH_PREBUILD ms=14.1 vertices=8256 (in the loading screen)`), so the
  mission's first frame finds it cached: direct modes and missions started from the menus. (The
  pacer's grace covered that frame already; the frame itself was the visible hitch.)
- `AS3D_UI_LISTS` (every 300 frames, front end): per list ms, cache units, units rebuilt and draws
  per flush.

### Azahar measurements (CPU ticks are not the console's; their ratios are)

Before = main 56891af (P4), after = this round; `mode=bot level=1 frames=1200 fixed_steps=1|2
perf=1 slider=1|0 pace=60`, `mode=menu logo=0 attract=1 slider=1 ui_fixed_dt=1`.

| | before | after |
|---|---|---|
| mission 1 stereo, first frame's submit | 23.5 ms | 9.6 ms |
| mission 1 mono, first frame's submit | 20.0 ms | 6.2 ms |
| Gulf mission 1 stereo, first frame's submit | 22.3 ms | 8.4 ms |
| touch band in play (mission 1) | 0.88 ms, 16 draws | 0.75 ms, 9 draws |
| touch band in play (Gulf from the menus) | 17 draws | 11 draws |
| Gulf main menu `bottom2d` / `ui_batch` | 2.23 / 1.26-1.30 ms, 16 draws | 0.97-1.04 / 0.18-0.28 ms, 17 draws |
| AirStrike 3D main menu `bottom2d` (static list) | 0.18-0.19 ms, 4 draws | 0.24-0.25 ms, 4 draws (consolidated) |
| prepare, mission 1 stereo (avg) | 1.02 ms | 1.27 ms (probe 0.10, model scan 0.15) |
| prepare, mission 1 mono | 1.015 ms | 1.015 ms (nothing runs in mono) |

Parallax at slider 1 (the right eye's horizontal shift against the left, best match per band of
rows; the effective values from the log):

| scene | before | after |
|---|---|---|
| mission 1 bot, step 600 | top rows +7 px, bottom rows +3 px, the player 0 | identical pictures (both eyes and the bottom screen): near content 2.4 px, far 7.0 px, no clamp; over 1200 frames near at most 4.3 px, far at most 9.0 px |
| mission 1, `max_far_parallax_px=3` (clamp check) | | eye offset 1.72, top rows +3 px (far_px 3.0) |
| Gulf mission 1 bot | | clamped on 31-36 % of the frames: models near the camera reached 39 px in front of the screen, now at most 10; eye offset avg 3.5-3.8, min 0.5; the dump at step 1200 identical to before |
| AirStrike 3D attract level behind the menus | +36 to +39 px everywhere | +2 to +4 px (screen plane at the nearest terrain, about 225 units; eye offset 4) |
| Gulf attract level | +32 to +38 px | +11 to +12 px (eye offset 0.7: rocks 12 units from the camera at 10 px in front, the far hills at 12 behind) |
| AirStrike 3D banner | -6 px (in front of the letterbox band) | +3 px (behind it) |

Mono pictures (mission 1, step 600) and the bottom screens are pixel-identical to before; the
simulation end states are equal (mission 1, 1200 steps: map_pos 872.0, created 508, freed 462; Gulf
mission 1: map_pos 1227.2, created 1486, freed 1332).

### Run.ini keys (also in apps/3ds/README.md)

| key | default | |
|---|---|---|
| `max_parallax_px` | 10 | the nearest content's parallax in front of the screen at slider 1 (scaled by the slider) |
| `max_far_parallax_px` | 12 | the farthest terrain's parallax behind the screen at slider 1 |
| `stereo_comfort` | 1 | 0: the eye offset and screen plane as before P5 |
| `terrain_lod_band` | 40 | the LOD hysteresis, world units either side of `terrain_lod_dist` |

### What the owner should feel on the console

- In missions the usual view keeps its depth (in mission 1's bot flight the clamp never acts: the
  ground under the helicopter is behind the screen, the far hills about 7 px); when something comes
  close to the camera (Gulf: a third of the time) the depth shrinks smoothly instead of the object
  jumping out of the screen, and comes back over two seconds.
- Behind the menus the landscape is no longer 35 to 39 px deep (the eyes had to diverge to fuse
  it); the title banner sits on the screen with little depth.
- PAUSED, the perf line and the letterbox band are flat on the screen.
- If the depth still tires: `max_far_parallax_px=8` (or 6) first, then `max_parallax_px=6`;
  `eye_offset=` now only scales the slider's range below the limits. Report the values that feel
  right.
- No pop or flicker of the far hills when hovering about 500 units from a ridge; if one shows,
  `terrain_lod_band=80`.
- The first frame of a mission no longer stutters on the touch band; the Gulf menus take half the
  bottom-screen time.

## Console session of 2026-10-11 (the round-2 drop played; the water wash caught)

The owner played the tutorial in mono, then again in stereo at about 40 % of the slider, pressing
L five times, four of them with the water wash on screen (`out/3ds/hw23-round2-play/`: log.txt,
perf.csv, `mark01..05_*.ppm`, `marks_sheet.png` = top | right eye | depth | bottom per mark).

**The wash, caught.** Marks 1, 4 and 5 (washed) have a depth buffer that holds only the water
plane's flat gradient and the models: the terrain wrote no depth at all. Mark 3 (no wash) has the
terrain's relief everywhere; mark 2 has it in the near rows only. With no terrain depth the water
plane passes its GEQUAL test over the whole ground and its translucent blend covers the terrain
and the roads (drawn before it), while the models (drawn after, with their own depth) stay clean:
the symptom the owner described since drop 1. The register read-back of the three frames after
each mark (`AS3D_GPU_MISMATCH pass=terrain field=depth_test expected=0x1f71 actual=0x0f10`) says
the same from the GPU's side: after the terrain's base draws the PICA's depth/colour-mask register
held the 2D passes' value (test off, write off) in marks 1, 2, 4 and 5 and the terrain's value in
mark 3, while citro3d's context and our shadow copy held the terrain's value every time, dirty
before the pass and emitted with its first draw (`mismatches=0` on the CPU layers, `dirty=0` after
the draw). The water's own read-back in the same frames was right. So the words citro3d records
for the terrain's first draw are not what the GPU ends up with, for seconds at a time, in mono and
in stereo, at 60 and at 30 Hz; nothing in the CPU-side bookkeeping differs between a washed and a
clean frame. Not explained; two things are in the drop that each remove a way for it to happen:

- `clear=split` (the default now; `clear=joint` is the old behaviour, `clear=quad` draws the
  clear). citro3d clears colour and depth with one GX memory fill, the depth half on the PSC1
  engine, and libctru's GX queue ignores PSC1's completion (gxqueue.c: `irq==GSPGPU_EVENT_PSC1`
  returns), so the queue can start the eye's command list while the depth fill is still running:
  the terrain's depth is then wiped behind its draws (mark 2's near rows written after the fill
  passed them fit this). The split issues the depth fill and the colour fill separately, both on
  PSC0, whose completion is waited for. Cost: the two fills no longer overlap (about 0.2 ms).
- The terrain pass issues its full state a second time with its second draw (a few command words),
  so a lost first state block cannot cost the pass its depth.

If the wash is still there with these, the A/B is `clear=quad` (no fill engine at all), then
`gpu_kick=0`, then `end_flush=all`; the L mark keeps working. If it is gone with the defaults, the
clear was it.

**Mono fell to 30 Hz for half the time, from a periodic hiccup.** Mono held 60 Hz with 0.4 % misses,
but the pacer went to 30 Hz 7 times and stayed there for 5116 of 10857 mono frames: 3 misses in
120 frames each time, and the misses come every 32 frames (0.53 s): frames 3457, 3489, 3521 ...
with the step 1 to 2 ms longer and the submit slightly longer, enough to miss the vblank in a
mission whose frames have only a few ms of slack. The period is the music's: the Ogg decoder
(tremor, on the audio thread, core 1) decodes a page every half second in an 8 ms burst
(`max_decode_ms=7.93`), the audio thread then exceeds the app's share of the system core and is
held off mid-mix while holding the mixer's voice lock, and the game thread's sound starts
(firing!) wait for it. The mixer now mixes the voices on a copy taken under the lock and writes the
positions back under it, so the game thread never waits for a mix in progress; `stopAll` waits
for a mix in flight. The pacer also excuses a frame during which the writer thread wrote the log
(the SD write delays the system core's services), and needs 60 fitting frames to return to 60 Hz
instead of 120. Expect: mono at 60 Hz throughout play, a single judder at most when the writer
writes. Stereo stayed a steady 30 Hz (GPU 11.5 ms avg of the tail alone; the P3 cuts were not
enough for 60): the `run-bot-stereo.ini` run gives the numbers to decide the next cut.

**Stereo "quite strong" at 40 % of the slider.** The log says the parallax was small then (far
2 to 4 px, near 1 to 3 px; the two eyes of mark 5 differ by 1 px), but one 300-frame window had
a near object at 93 px before the clamp caught up (`downPerSecond` 3.0: a third of a second) and
the clamp then acting on a third of the frames, so the depth pumped. Changed: the clamp now
shrinks within two or three frames (`downPerSecond` 24), the near scan ignores props whose box
spans more than 250 units (their box corner, not a visible part, was the near object), and the
defaults are lower: `eye_offset` 3 (was 4), `max_parallax_px` 6 (was 10), `max_far_parallax_px`
8 (was 12). The owner's next feel decides whether to go lower still (`max_far_parallax_px=6`).

**Other numbers of the session:** load 645 ms, lazy loads 0 in the front end and mission 1 (P1
holds), `AS3D_TOUCH_PREBUILD` 10 ms in the loading screen, no `AS3D_FENCE`, no `AS3D_GPU_BUSY`,
no texture created inside a frame, heap 15.5 MB, linear free 16 MB at the lowest, texture VRAM
4193 KB. The three `AS3D_SLOW_SUBMIT` lines are the L mark's own read-backs.

## Soak mode and the log push (round 2, P7)

The owner no longer does QA by hand: one launch runs the checks, writes the verdict, pushes the files
to the PC and returns to the Homebrew Launcher (the owner's "done" signal). Code:
`apps/3ds/source/ctr_soak.cpp` (the stages), `ctr_report.cpp` (the push), `soak_report.{h,cpp}` (the
stage list, the verdict and the framing: platform-neutral, tested on the desktop by
`apps/tests/soak_report_test.cpp`), the probe in `engine/src/render/pica/state_check_pica.cpp`; on the PC
`tools/3ds_receive.py` and `tools/3ds_soak_compare.py`. The keys are in apps/3ds/README.md ("Soak mode
and the log push (P7)").

**The stages.** `mode=soak` runs `runCtrLoop` in `mode=bot` once per stage on the same graphics context
and input (the loop now borrows them, as the front end does from the selector), with a loading screen
between stages: every game whose paks are installed, missions 1 and 2, each in mono (`slider=0`) and in
stereo (`slider=0.5`; the hardware slider is ignored), the whole mission each (`soak_frames=0`, the default since the first console soak: until Mission Complete or Game Over plus 300 steps, 24000 at most; `soak_frames=N` for N steps), the weapon tour,
`perf=1`, `state_check>=1`, `probe_every=30`, audio and music on (the mono hiccup of the last session was
the music thread). `soak=game:level:mono|stereo,...` overrides the list. Before each stage
`soak.running` names it on the card; it is removed at the end, so a launch that died inside a stage is
reported by the next one (`FAIL note the previous launch stopped inside a stage (...)`). The lazy-load
log is disarmed between stages (the previous stage's precache had armed it: the next session start
logged 500 false AS3D_LAZY lines). Each stage appends to the same `log.txt` and `perf.csv` (a new
`stage` column; the frame column restarts).

**The probe.** `probe_every=N`: every N frames the terrain pass's register read-back of
`state_check=2` (`probeGpuState("terrain")`: the command list so far submitted and waited for, one stall
per eye) compares the PICA's registers with citro3d's context, so the wash's signature
(`AS3D_GPU_MISMATCH pass=terrain field=depth_test`) is counted without anyone pressing L. The first 3
GPU mismatches of a level start mark the frame as L does (`AS3D_AUTO_MARK`, `mark<NN>_top/right/
bottom/depth.ppm`; the numbers now run over the whole launch, so stages do not overwrite each other's
marks), and the next 3 frames are checked in full. The pacer excuses a frame that ran a probe and the
next one (the stall delays the next vblank), so the 60 Hz verdict judges the game, not the instrument.
`probe_inject=N` makes the N-th probe of each level start report a made-up mismatch (`field=inject`):
the self-test of the mark, verdict and push path on Azahar, whose GPU never loses its state.

**The verdict** (`soak.txt`, also as `AS3D_SOAK_VERDICT` lines in the log): `wash` (no terrain GPU
mismatch over all stages, at least one probe), `mono60 <stage>` (>= 97 % of the frames after the pacer's
300-frame grace at 60 Hz, the miss rate beside it), `stereo <stage>` (the rate it settled at and the GPU
avg / p95; FAIL only when it could not hold 30 Hz), `lazy_loads` (0; the sequels' TerraMorph stamps,
not precachable by design (as3d/load_stats.h), are counted beside it), `fence`, `gpu_busy`,
`tex_in_frame` (no AS3D_FENCE / AS3D_GPU_BUSY / AS3D_TEX_IN_FRAME), `stages_finished`, then `VERDICT
PASS|FAIL`. Per stage the log has `AS3D_SOAK_STAGE` (game, level, eyes, frames, weapons: what the PC's
comparison needs), the stage's config line (now with `probe_every`, `slider`, `stage` and `build=<commit>`,
the commit tools/3ds_build.sh passes in), the pacer's `AS3D_PACE` lines and `PERF_SUMMARY pace` (now with
`after_grace`, `at60`, `at30`, `probe_excused`, `hz_end`), the pass profile, `AS3D_LAZY_LOADS`, the end
state and `AS3D_SOAK_RESULT` (the counts above plus the probes and their stall, avg / max ms).

**The push.** `report_to=IP:PORT`, in any mode: after the run (before a fatal screen, if any) libctru's
soc service is started (`socInit` with a 1 MB `memalign(0x1000)` buffer, only then), a non-blocking
connect with a 3 s timeout, then the files this launch wrote (noted as they are written: `perf.csv`,
`soak.txt`, the marks; an older `soak.txt` or mark on the card is not sent) and `log.txt` last, in the
framing `AS3D1\n`, per file `<name>\n<size>\n` + bytes, `END\n`. Nothing is read from the socket.
When it cannot connect: one `AS3D_REPORT ... not sent: <why>` line and the files stay on the SD card.
The CIA's RSF lists `soc:U`; the `.3dsx` needs nothing more (the Homebrew Launcher's process has the
service). `tools/3ds_receive.py` listens (one connection at a time, until Ctrl-C), refuses names with
a slash, a backslash or `..` and files over 64 MB, files the run under `out/3ds/<date>-<commit>/`
(the log's `build=`), prints `soak.txt` and the `AS3D_SOAK_RESULT` lines, cuts the log and perf.csv per
stage (`stages/<NN>-<game>-<level>-<eyes>/`) and runs `tools/3ds_perf_report.py` on each (stereo with
`--slider max`), converts the marks to PNG with `marks_sheet.png`, and runs `tools/3ds_soak_compare.py`:
for every stage `as3d_sim --game G --level N --frames F --bot --bot-weapons` (the sim tool got
`--bot-weapons`, the weapon tour on top of the bot as on the console) against the stage's end state
(map_pos, entities, slots, created, freed, refused, script errors).

**Azahar (2026-10-11, no console):**

- `mode=soak soak_frames=300`, the default stages with all three games on the emulated card: 12
  stages, 12 finished, `VERDICT PASS` (`wash` PASS with 105 probes and 0 mismatches; lazy loads 0 with
  16 TerraMorph stamps in the sequels; no fence, busy or in-frame texture; the rate lines are `INFO`:
  300 steps are shorter than the pacer's grace). The push to `127.0.0.1:7777` arrived (Azahar's soc
  reaches the host: 3 files, 418 KB, 42 ms to connect) and the receiver's comparison gave **12 of 12 end
  states equal to the desktop sim** (as3d, as2, gulf, missions 1 and 2, mono and stereo).
- The mark path: `soak=as3d:1:mono,gulf:2:stereo soak_frames=900 probe_inject=5`: two `AS3D_AUTO_MARK`,
  seven mark PPMs pushed (10 files, 2 MB), the contact sheet, `wash` FAIL with the two made-up
  mismatches, both end states equal to the sim.
- The crash marker: a planted `soak.running` gave `FAIL note the previous launch stopped inside a stage
  (...)` and was removed. `report_to` with nothing listening (a plain `mode=bot` run): `AS3D_REPORT
  to=127.0.0.1:7799 not sent: connection refused or unreachable (errno 111)`, the run otherwise intact.
- The probe's cost, mission 1 mono, 1800 steps, the weapon tour, `state_check=1`: the soak stage (37
  probes) against a plain `mode=bot` run: frame 26.78 vs 26.83 ms avg, submit 10.60 vs 10.58 ms, 1125 vs
  1123 frames, end states identical. Azahar's stall per probe is 0.6 ms (its GPU is synchronous); on the
  console it is the GPU time of the terrain draws recorded so far, about 1 to 3 ms, once every 30 frames,
  and the pacer excuses it. `AS3D_SOAK_RESULT probe_ms_avg_max=` gives the console's number.
- Not checked on Azahar: the return to the Homebrew Launcher at the end (the same path as any
  `frames=`-limited bot run, which the console has done since M1) and the console's Wi-Fi.

**On the console, what to expect:** about 30 s per stage plus its load (12 stages with three games:
eight to ten minutes), then the launcher. The first console soak gives, without the owner doing anything
else: whether the wash still happens with `clear=split` (any `pass=terrain` GPU mismatch, with the marks),
mono at 60 Hz with the music thread fixed, the stereo GPU numbers per mission and game, and the end
states of all three games against the desktop.

## First console soak (2026-10-11, build 362f779; out/3ds/hw24-soak1-362f779/)

The owner launched the soak once (four stages: AirStrike 3D missions 1 and 2, mono and stereo, 1800
steps each, the weapon tour, audio on, `probe_every=30`). It ran to the end and came back to the
launcher. `soak.txt`:

- `PASS wash`: 232 terrain probes, 0 GPU mismatches, no marks. The owner then pointed out that the
  wash shows later in the missions, never in the first 30 seconds: the stages are now the whole
  mission (`soak_frames=0`, `AS3D_LEVEL_END`, 300 steps of the Mission Complete screen, where the
  owner once saw the wash too; the desktop bot completes missions 1 and 2 at steps 13660 and 13859
  without dying, verified identical on Azahar).
- `FAIL mono60` mission 1 at 94.6 %, mission 2 at 90.6 % of the frames at 60 Hz after the grace;
  the misses themselves are 0.5 % / 1 %: bursts of three heavy frames in the fights (`submit` 8-9 ms
  plus a GPU tail of 9-11 ms, 23.5k vertices, ~212 draws) tripped the pacer's "3 misses in 120
  frames" rule, and each fall costs 60 fitting frames at 30 Hz. The rule is now 6 misses in 120
  frames. The heavy frames themselves (mono GPU avg 6.6 ms, p95 8.8 ms; the fights' peaks over 10 ms
  with a 9 ms CPU submit) are the next performance item if the soak still fails `mono60`.
- `PASS stereo`: settled at 30 Hz, 0 misses at 30, GPU avg 10.1 / 10.5 ms, p95 13.3 / 14.3 ms.
- `PASS lazy_loads` 0, `fence` 0, `gpu_busy` 0, `tex_in_frame` 0, 4 of 4 stages finished; level
  start 5.7 s (precache 5.0 s, 335 pak reads of 2.3 s).
- The probe's stall on hardware: 2.2 ms average in mono (max 3.1), 3.8-4.0 ms in stereo (max 7.9),
  every 30 frames, excused by the pacer.
- The push: `AS3D_REPORT ... not sent: connection refused or unreachable (errno -26)` while the PC's
  receiver had accepted the connection (0 bytes, then a reset). The console's socket service does not
  answer `SO_ERROR` the Linux way (Azahar's does, which is why the emulator passed). The check is now
  `getpeername` after the poll, with the poll's error bits as the failure; the files were fetched over
  ftpd meanwhile.

## Second console soak (2026-10-11, build 5192662; out/3ds/2026-10-11-5192662/, pushed by the console)

The push worked (3 files, 3.7 MB, connected in 1.8 s). Four whole missions (AirStrike 3D 1 and 2,
mono and stereo), every stage to Mission Complete, every end state identical to the desktop sim.

- `PASS wash`: 1534 terrain probes over the four missions, 0 mismatches, no marks. As far as the bot
  can tell the wash is gone; the owner's play is the only proof left (`run-play.ini`, L if it shows).
- **The simulation step grows 5x through a mission** (per 1200 steps, mono mission 1): 0.9 ms per
  step at the start, 1.9 at 2400, 3.0 at 7200, 4.5 at 10800, 5.2 at 12000; mission 2 the same
  (1.2 to 5.2). Draws and vertices stay flat (150-230 draws, 21-25k vertices); `prep_entities`
  goes from 172 to 1003 items, prepare from 1.1 to 3.4 ms. The desktop sim is bit-identical, so
  this is measured and fixed on the desktop (round 3, R3.1).
- `FAIL mono60`: mission 1 at 60 Hz for 30% of the frames, mission 2 for 66%. The misses at 60 Hz
  are 1.6% and 1.9%, spread over the whole mission (miss frames: step 3.3 ms vs 1.7, submit 7.4 vs
  5.7, GPU 8.5 vs 6.9 ms); the rule of 6 misses in 120 frames still tripped 7 and 10 times and the
  return needs 60 fitting frames (doubling after a quick fall). The rule is now 12 in 120 (10%).
- `PASS stereo` at 30 Hz, but mission 1 missed 9.2% of its 30 Hz frames (13.4% by the perf.csv
  sum), 433 of the last 600 frames: late in the mission two steps cost 10 ms, the submit 10 ms
  (p95 19), the GPU 12 ms: the CPU path alone is over 20 ms. Mission 2: 0.7%. Per frame in stereo
  (`AS3D_PASS_MS`, 2 eyes): terrain 1.7, **water 2.4-2.5 ms for 2 draws of 57 vertices**, opaque
  0.8, rest 0.85, hud 0.75, touch 0.54 (8.4k vertices a frame for the touch band), top2d 0.5,
  particles 0.6, ui_copy 0.3; `AS3D_MESH` 160-190 draws, 100 texture binds, 57 state changes,
  merged 0. Round 3, R3.2.
- Level start 3.2-3.4 s (gate 3.0); lazy loads 0; fence 0; GPU busy 0; textures in frame 0; the
  probe 2.2 ms mono / 4.0 ms stereo (max 10) every 30 frames.

## Simulation step growth (round 3, R3.1)

The second console soak measured the simulation step of AirStrike 3D mission 1 (bot with the
weapon tour) at 0.9 ms per step in the first 1200 steps and 5.2 ms at step 12000, mission 2 1.2
to 5.2 ms. The simulation is bit-identical to the desktop, so the growth was measured and
worked on with `as3d_sim`.

**Method.** `as3d_sim --step-times [N]` (new) prints one `steps:` row per window of N steps
(default 1200): mean and p95 ms per step, the `World::step` phase means, the list entities and
slots at the window's end. `perf` cannot run on this machine (`perf_event_paranoid` 4), so the
profile came from a SIGPROF sampler preloaded into `as3d_sim` (100 us, the leaf PC and 11
frames by `backtrace`, symbols from `nm`, lines from `addr2line`), the late window as the
difference of a 13200-step and a 10800-step run, the early one as 2400 steps minus the load
(4 runs each); rdtsc counters around `think` by entity name for the per-type costs (temporary,
not committed). The before/after tables are the best of 5 to 7 interleaved runs of both
binaries (desktop i7-7700HQ, RelWithDebInfo).

**What grows.** The end state of mission 1 with the tour: 518 list entities, 1038 slots, of
which 440 list entities are missile muzzle flashes (`missile_smoke` 261, `missile_smoke_small`
179) and 445 slots their `PS_GUNSMOKE`/`PS_GUNSMOKE_SMALL` emitter holders. The flash is the
`flash` object of the player's missile weapons (`weapons/player.wpn`), made by `Shoot` step 12
as a linked pool entity on the launcher tag (`tag_rocket1`, `tag_rocket2`, ...). Its definition
has no script (`flag FL_NODRAW`, `attach "PS_GUNSMOKE" "origin"`): nothing ever removes it.
It goes to state 2 (leaving) but stays inside the view frustum with the player, and removing
the player would not remove it either (rcsl-builtins-semantics.md B: linked pool entities stay
in the list; `remove` step 5). The machine gun's `mgun_flash` has a script that removes itself
and does not pile up. So this is the original's behaviour, not a port bug: every missile shot
adds two entities for the rest of the mission. The weapon tour fires 440 missiles in mission 1;
the plain bot (no tour) still leaves 80 flashes by step 10800; a human fires fewer. The other
growth is the mission's own content: uncollected items (`FL_TEMPORARY`, their scripts remove
them only when taken, so they drift behind the camera with their `flare` child for the rest of
the mission; about 12 late in mission 1, 2000 to 3000 cycles per think each) and the busier
second half of the mission. Without the tour the plain bot's step also grows 4.5x (0.017 to
0.076 ms, desktop).

A side effect worth knowing: the flashes' emitter instances never finish (their holder lives),
so the 256-instance particle pool (render-pipeline.md 6.1) is full late in mission 1 with the
tour and later systems are refused (`particles.refused` 1248 at the end). Same in the original
if it behaves as specified; with fewer missiles (human play) the pool does not fill.

**Profile, late window minus early (mission 1, before).** Per 2400 steps the late window took
3.6 times the samples of the early one. Self time, late: `World::think` 14.2 % (early 5.6 %),
`WorldParticles::update` 10.6 % (6.2 %: the step-2 walk of the whole entity tree, every step),
`World::touchEntity` 7.4 % (1.5 %: the touch candidate snapshot rebuilt from the list after
every hit), `ScriptThread::runInvocation` 6.9 % (16.5 %), `setupTransform` 6.2 %, `attachToTag`
5.7 %, `listEntitiesInto` 5.1 % (2.8 %: particles 54 of its 86 samples, `EntityListScope` 26),
`dispatch` 4.6 %, `runEntities` 4.4 %, `freeTree` 3.7 % (0.6 %: a list walk per freed slot to
detach `AttachEntity` children), `freeRemoved` 3.0 %. The script VM is not what grows: its share
falls. rdtsc by entity name, steps 10800 to 13200: the flash thinks (flash plus holder) took
54 % of all think time; a flash think with its holder costs about 250 cycles (rdtsc included),
about 200 when run a second time hot: the cost is instructions more than memory (a cache
prefetch gains 6 %; a table of attach/transform results keyed by their input bits, 71 % hits,
gained nothing and was dropped).

**Changes** (commit 8f447d8; no value changes, see the proof below):
- The active list is also an array, oldest first (`World::listOldest_`: `linkNewest` appends,
  `freeRemoved` compacts after its unlinks, the only ones). The entity pass, the collision
  pass, the touch candidate snapshot, `freeRemoved` and `listEntitiesInto`/`EntityListScope`
  read it instead of chasing the `older` links; `World::listOrder` gives an entity's place.
- `freeRemoved` clears the parent of the pool entities attached to the slots it freed in one
  list walk for all of them (the same links end cleared: nothing between the frees reads the
  parent of a list entity); `freeTree` no longer copies the child array.
- `WorldParticles` step 2: the holders built since the last step (`World::takeNewHolders`) wait
  in a pending list until they get an instance or a refusal (a holder settles for good per
  generation); the step takes the pending holders that have thought in the order of the old
  walk (list order of the root, then the child index at each level down), which itself only
  runs after a reset (`scanAll`).
- `ParticleEmitter::update` skips its particle loop once no particle is live and none was
  spawned since (the gun smoke instances).
- `Entity` members reordered: what the per-step passes read comes first, next to the script
  fields (936 bytes, was 960); the tag cache no longer copies and compares the tag name (every
  change of `tagName` clears it: `buildEntity`, `attachEntity`, `detachEntity`). The entity
  pass prefetches the hot lines of the entity 4 places ahead and the child array of the one 2
  ahead (`World::prefetchAhead`, `__builtin_prefetch`: PLD on the ARM11).
- `think`: no `dispatch` call without a thread, no `think` call for a root that has thought,
  no `computeScreenBounds` call for an emitter holder; `setupTransform` reads the flags only
  for roots; `rootOf` inline.

**Before / after, desktop** (mean / p95 ms per step per 1200 steps, `--bot --bot-weapons`):

| steps to | m1 before | m1 after | m2 before | m2 after | m1 list / slots |
|---|---|---|---|---|---|
| 1200 | 0.0297 / 0.050 | 0.0261 / 0.044 | 0.0387 / 0.059 | 0.0340 / 0.052 | 78 / 166 |
| 2400 | 0.0655 / 0.113 | 0.0535 / 0.089 | 0.0653 / 0.097 | 0.0528 / 0.079 | 206 / 350 |
| 3600 | 0.0641 / 0.090 | 0.0477 / 0.063 | 0.0664 / 0.088 | 0.0516 / 0.070 | 188 / 394 |
| 4800 | 0.0722 / 0.097 | 0.0510 / 0.069 | 0.0756 / 0.100 | 0.0558 / 0.074 | 204 / 410 |
| 6000 | 0.0894 / 0.131 | 0.0636 / 0.098 | 0.0896 / 0.122 | 0.0628 / 0.084 | 346 / 627 |
| 7200 | 0.0955 / 0.130 | 0.0652 / 0.097 | 0.0900 / 0.125 | 0.0596 / 0.081 | 310 / 625 |
| 8400 | 0.1167 / 0.160 | 0.0773 / 0.110 | 0.1096 / 0.147 | 0.0718 / 0.098 | 325 / 667 |
| 9600 | 0.1224 / 0.166 | 0.0768 / 0.103 | 0.1247 / 0.166 | 0.0767 / 0.103 | 443 / 817 |
| 10800 | 0.1419 / 0.195 | 0.0885 / 0.119 | 0.1274 / 0.164 | 0.0816 / 0.105 | 528 / 1089 |
| 12000 | 0.1630 / 0.216 | 0.0982 / 0.127 | 0.1567 / 0.208 | 0.0977 / 0.128 | 479 / 951 |
| 13200 | 0.1564 / 0.211 | 0.0937 / 0.128 | 0.1578 / 0.212 | 0.0984 / 0.134 | 595 / 1111 |
| end | 0.1143 / 0.178 | 0.0683 / 0.102 | 0.1166 / 0.177 | 0.0703 / 0.105 | 518 / 1038 |

Worst late window: mission 1 0.163 to 0.098 ms (-40 %), late/early 5.5 to 3.8; mission 2 0.158
to 0.098 (-38 %), 4.1 to 2.9. The sequels' mission 1 (6000 steps, tour):
as2 0.147-0.200 to 0.119-0.163, gulf 0.098-0.164 to 0.086-0.133 (-15 to -20 % in every window;
little growth to begin with). On the console the same work is about 35 times slower; the late mission 1 step
should go from about 5.2 ms toward 3.2 ms if the ratio carries over (the memory-bound parts, the
list array and the prefetch, may gain more on the ARM11; not measured yet).

**Proof of identity.** `as3d_sim --dump-state` plus the summary, before (0c9c98a) and after,
every mission of the three games (62) after 3600 steps in four configurations (`--bot`;
`--pilot --god --upgrades 3 --items 5`; `--bot --players 2 --seed 7 --difficulty 3`; `--bot
--bot-weapons`): 496 files byte-identical. The whole missions 1 and 2 of AirStrike 3D (13961 and
14160 steps) and the sequels' mission 1 (6000 steps) with the tour: identical end states and
dumps (mission 1: `list entities 518 (max 600), slots 1038 (max 1177), created 8413, freed
7895`). New tests (world_test, world_particles_test): the list array and the links agree
through creates and frees; a pool entity attached to a freed entity or to its definition child
loses its parent; a re-attach to another tag follows the new tag; a full pool refuses holders
in walk order, once each; holders built while paused are picked up after the pause and after a
level restart. ci.sh passes for the three games.

**What remains, and the targets.** The round's target (late/early at most 2.5, the late step
halved) is not reached without changing the simulation: a flash and its holder still cost about
200 to 250 cycles per step, and 440 pairs are about 0.035 ms of the 0.098 ms late step; the
uncollected items and the busier end of the mission make the rest (without any flash the late
step would still be about 2.4 times the first window). What would reach it, as owner decisions
(each changes the simulation, so the goldens and the soak's end states would move):
- Remove a muzzle flash once its emitter has finished (or after a few seconds): a deviation
  from the original, which keeps them; the visible effect is none (the flash is FL_NODRAW and
  its smoke is gone), the particle pool stops filling up.
- Cap the flashes per shooter (the original's pool of 1024 list entities would crash after
  about 900 missiles in one mission anyway, engine-behaviour.md 3.3).
- The weapon tour itself fires missiles far faster than a player; a soak with the plain bot
  grows less (80 flashes by step 10800).
Next measurement: the console soak of this build; `perf.csv` step times per window against the
table above.

## Stereo CPU path (round 3, R3.2)

The second console soak (above) measured, late in mission 1 in stereo at 30 Hz, a submit of 9-10 ms
(p95 19), a GPU of 11-12 ms and a prepare of 3.4 ms, with the water's pass at 2.4 ms of CPU for
"two draws of 57 vertices" and the touch band at 0.54 ms. This round looked for the causes, made
the right eye's world a replay of the left eye's command words, and cut what each pass costs per
draw. It also built the wash detector the owner's soak observation called for (below). Only
`engine/src/render/pica`, `engine/src/ui/pica` and `apps/3ds` changed (plus the shader list in
`engine/src/render/module.cmake` and the new desktop test's source list in `apps/CMakeLists.txt`);
`tools/ci.sh` OK, the desktop goldens untouched.

### Causes found

- **The water was not waiting for its buffer.** Its fenced buffer never stalled (`hazards=0` in
  every `AS3D_STATE` line: each half is taken only once its frame is done). The 2.4 ms were the
  CPU tiling of the plane: per eye about 330 tiles tested and 230 emitted, each with four clip
  transforms, six plane compares per corner, up to eight divisions for its NDC span and six
  vertices built (two more divisions each), then a 33 KB copy into the buffer. The "57 vertices"
  of the counter is the old quad count (it never counted the tiles).
- **citro3d's per-draw cost.** Timed on Azahar, `C3D_DrawElements` alone is about 25 us of a mesh
  draw's 45 (`C3Di_UpdateContext`: two scans of the 96 uniform flags, the dirty blocks, then the
  draw's register writes); our side added a full TEV re-emission at every draw (`applyTev` marked
  the stages dirty each call), a program reload per 2D run (`C3D_BindProgram` reloads the
  program's constants and attribute layout even for the bound program), the vertex buffer set up
  per 2D run, and a vertex shader code upload at every program switch (six separate shader
  binaries: citro3d uploads the code whenever the new program's DVLP differs).
- **Every model record was submitted, on screen or not** (no culling after the entity pass):
  late in mission 1 about 21 records a frame (of 73) are outside both eyes' frusta.
- **The touch band's list was built every frame** (cached vertices, but `drawTouchControls`, the
  unit matching against 24 slots and 15-22 draws ran each frame).
- **Prepare growth:** `prep_entities` walks every live entity (1045 late in mission 1 on Azahar
  too, from 109 at the start); the simulation's own growth is R3.1's. Not changed here (see
  "What remains").

### The second eye: replay (eye_replay_pica.{h,cpp}, run.ini `eye_replay=1` by default)

The two eye projections (`picaEyeProjection`, citro3d's `Mtx_PerspStereoTilt`) differ only in
row 1 (the tilted screen's horizontal axis), columns 2 and 3; with w = -z_view, the right eye's
clip y is the left eye's plus `k1 * w + k2` for every vertex of every world draw. `eyeShiftOf`
derives (k1, k2) from the two matrices each frame and refuses when anything else differs.

- The world shaders (flat, mesh, env, terrain, water) end with `y += eye.x * w + eye.y`, `eye`
  being every program's first uniform (c0 in all six, checked at load: `eye_register=0` in the
  log). eye is (0, 0) except during a replay, and adding zero leaves y exactly as before: mono,
  the left eye and the 2D layers are bit-identical.
- The left eye, after its clear, draws a degenerate triangle (three equal vertices: no fragment)
  that emits its frame buffer and everything dirty, then marks every citro3d state flag dirty
  except the frame buffer (through the state check's mirror of the 1.7.1 context: attribute and
  buffer setup, effect, viewport, scissor, program and its code, texture units and TEV, fog LUT
  and mode) and every uniform except `eye` (`C3Di_DirtyUniforms`). The region it then records,
  up to the end of the sprites, depends on nothing recorded before it.
- The right eye clears its target, sets eye = (k1, k2), primes (its frame buffer, the uniform)
  and appends a copy of the region's words (`GPUCMD_AddRawCommands`: 10-14k words in mission 1,
  17-25k in Gulf mission 1; 0.27-0.62 ms on Azahar). Then all state is marked dirty again (the
  GPU holds the region's last state, citro3d its own), the shadow copies are forgotten and
  eye = (0, 0).
- What the left eye records must serve both eyes: the terrain culls its chunks against both eye
  frusta (`set_union` of the two visible lists), the water plane's tiles and the sequels' wave
  grid chunks are culled for both (a corner is outside a y plane only when outside in both eyes;
  x, z and w are equal), and the meshes were already culled with the widened stereo volume. The
  extra chunks and tiles lie outside the left eye's frustum: its picture is unchanged.
- Off (both eyes recorded as before) in mono, with `eye_replay=0`, in a frame whose state-check
  probes submit the command list mid-pass (`stateCheckSplitsThisFrame`: level 2, the frames after
  a mark or a wash episode, a `probe_every` frame), when the region was split, and when the
  command buffer lacks room (a quarter kept free). `AS3D_EYE_REPLAY` every 300 frames: armed,
  recorded, replayed, failed, words per replay, replay ms.
- The gpu_kick and frame-synchronisation rules are untouched: the region lies inside the left
  eye's kicked list and is only read by the CPU; the copy is new words of the right eye's list;
  no fence, deferred free or texture path changes.

The right eye's picture differs from its own projection's by float rounding only: 100-180 pixels
of 96000 by at most 4 levels in mission 1 (step 600, both dumps), similar in Gulf mission 1.

### Per draw and per pass

- **Water** (water_renderer_pica.cpp): the tiles as an indexed grid. Each corner of a visible
  quad's tile grid is transformed once (clip position, outcode), its NDC and its vertex made once
  when a tile uses it, and a tile adds six 16-bit indices (`drawFlatRunsIndexed`). The corner
  values are the per-tile path's bit for bit and the triangles the same; tiles cut at the near
  plane or split keep the per-tile path (non-indexed, drawn after). Fewer vertices for the GPU as
  well (about 400 per eye instead of 1400, with the post-transform cache of indexed draws).
- **One shader binary** with six entry points (`pica_all.shbin`, `.entry main_<name>` in each
  source): a program switch uploads no code.
- **TEV**: `applyTev` skips a configuration equal to the last one; `c3dInvalidate` (pass starts,
  targets) resets it, so the passes still issue their TEV in full.
- **2D** (renderer2d_pica.cpp): the shader, its matrix and the vertex buffer are issued when they
  change, not per run; a flush of the same list as the list's last flush replays its draw list
  without matching units (`replays` in the stats; a replayed split list still counts towards its
  consolidation).
- **Touch band** (ctr_frame.cpp `TouchKey`): the list is built again only when what it shows
  changes (layout, held buttons, drag target, the HUD fields of its faces, rules, assets).
- **Model records** outside the prepared cull volume (the model box as a sphere through the model
  matrix) are not submitted: `AS3D_MESH culled_records=`.
- `mesh_merge` stays off by default (P4's Azahar measurement: the bake costs what the draws
  save); `merged_draws=0` in the soak is that default, not a failure.

### Azahar numbers (relative: Azahar's CPU ticks are not the console's)

`mode=bot level=1 frames=1800 fixed_steps=2 bot_weapons=1 state_check=1 perf=1 audio=0`, parent
`1c48f7a` against this branch; per displayed frame. The whole-mission rows ran 13800 steps
(Mission Complete at about 13660) and show the last 300-frame window.

| | parent | this round |
|---|---|---|
| mission 1 stereo, submit (PERF_SUMMARY avg) | 15.9 ms | 9.8 ms |
| mission 1 mono, submit | 10.8 ms | 8.8 ms |
| mission 1 stereo, whole mission, submit avg | 17.7 ms | 10.6 ms |
| mission 1 stereo, last window, pass sum | 13.3 ms | 7.2 ms |
| Gulf mission 1 stereo, submit | 19.8 ms | 14.3 ms |
| water, stereo frame / mono | 3.36 / 1.68 ms | 0.89 / 0.86 ms (one eye recorded in stereo) |
| touch band | 1.18-1.20 ms, 20-22 draws | 0.65-0.69 ms (0.48 late, the list changes less) |
| hud | 1.9-2.3 ms | 1.4-1.7 ms |
| `AS3D_MESH` draws, stereo, last window | 145 (both eyes) | 52 (the left eye; the right is a copy), 21 records culled |
| prepare, last window | 4.42 ms | 4.25 ms (prep_meshes 0.54 -> 0.37; prep_entities 1.99 ms for 1045 entities in both) |

Pictures: mono and both bottom screens identical to the parent but for the perf text line (rows
230-234); left eye identical; right eye within rounding (above). End states equal in every run.

### Wash detector (ctr_wash.{h,cpp}, wash_detect.{h,cpp}; run.ini `wash_check`, on in soak)

The owner saw the wash in the second soak "on the lower half of the screen" while the 1534
register probes reported nothing. The detector judges the picture instead, every frame, with no
added wait:

- After each top eye's passes a GX texture copy of seven 8-pixel columns of the eye's D24S8 depth
  buffer is queued behind the eye's command list: the native target is 240 x 400 with 8x8 tiles,
  one "strip" of tiles per 8 screen columns (7680 bytes); strips 3, 10, ..., 45 (screen columns
  28, 84, ..., 364) are 52.5 KB per eye, into a double-buffered linear buffer.
- At the next frame start, after the fence (that frame is complete), 98 samples per eye (7
  columns x 14 rows, 10 rows in the lower half) are judged: the ray through the pixel meets the
  water plane (planeZ; the analytic eye depth there: the W-buffer is linear in the eye depth,
  depth = (far - e) / (far - near)); a height-field march (30-unit steps, bisection) says whether
  the terrain is clearly above the water and in front of the plane (8 units or 3 %). Only those
  samples count; one whose GPU depth is at the plane or behind it (the water's depth, or the
  cleared far depth) is washed. The march is redone for a sixteenth of the samples each frame and
  for any sample before it counts as washed (a stale answer cannot raise a false wash).
- 3 washed samples make a washed frame; an episode (a run of washed frames) is logged when it
  ends: `AS3D_WASH start=<frame> frames=<N> eye=<L|R|both> washed=<max samples> rows=.. cols=..
  episode=<n>`; its first frame logs `AS3D_WASH_START` with the terrain chunks drawn and the first
  one, asks for a mark (the screens and depth dumped as for L, the first 3 episodes of a level)
  and arms the register read-back of every probed pass for this frame and the next.
- `AS3D_WASH_STATS` every 300 frames: eye-frames judged, samples judged per eye, the share whose
  depth agreed with the analytic terrain (calibration: 91-94 % on Azahar, mean error 2 units; the
  rest are models in front, 5-6 %), episodes, judge ms (0.31-0.36 ms per eye on Azahar).
- The soak: `wash_frames`, `wash_episodes`, `wash_evals`, `prime_mismatches` in the `wash` line
  (FAIL when a frame was washed or a terrain probe mismatched), one `FAIL wash <stage>
  first_frame= episodes= frames= longest= max_samples=` per stage that had one, and the fields in
  `AS3D_SOAK_RESULT`.
- Self-tests: `wash_inject=N` (the terrain writes no depth N frames every 600; 1 = 60): Azahar
  catches one 60-frame episode per window in mono, and with `wash_inject=2` two-frame episodes in
  both eyes in stereo (single-frame episodes are caught the same way); `wash_selftest=1`: the
  decoding of the copied tiles against a synchronous display-transfer read-back of the whole
  buffer, 98 of 98 samples equal (`AS3D_WASH_SELFTEST`; the depth is bytes 0-2 of each raw word).
- Without injection: 0 washed frames in mission 1 (1800 steps) mono and stereo, with
  `terrain_prime=1` and with `terrain_prime=0` (Azahar never loses state).
- Cost on the console: 105 KB of texture copy per stereo frame (the colour display transfers are
  about 768 KB), two more GX queue entries per eye, the CPU judging. The sequels' wave grid is not
  judged (no single plane height).

#### Why the probe was blind, and the priming draw

- The probe compares the depth map (0x6D enable, 0x4D scale, 0x4E offset), the depth test and
  masks (0x107), the depth write, the depth and colour buffer addresses, cull, blend and texture
  unit 0: the depth state is in it. But the periodic probe reads the registers **after all the
  terrain's base draws**, and since the console session of 2026-10-11 (c314767) the terrain
  re-issues its state with its second draw. The marks of that session caught the PICA holding the
  2D passes' depth state after the terrain's first state block; if the first block is still lost
  now and then, only the **first chunk** draws without its depth write, and the visible chunks are
  drawn in index order, nearest rows first: the first chunk is the nearest strip of terrain, the
  lower part of the screen. The probe, reading after the re-issued state, sees nothing: it fits
  the owner's "lower half only" with 0 mismatches.
- The shadowing does not hide it: `c3dDepthMap` and the other setters skip a repeat inside a pass
  only, every pass start of the terrain and the water forgets them (`c3dInvalidate`), and
  citro3d re-emits the whole effect block whenever any of it changes. A register lost on the GPU
  behind the CPU's back is not re-sent inside a pass by any layer: only a read-back or the picture
  can see it.
- The water's near tiles: drawn in the same draw and state as the others (the CPU cut only
  changes their vertices; `near_tiles=0` in the soak's bot flight). The mesh passes between the
  terrain and the water do not touch the depth map (`MeshRenderer::end`'s near check is the
  banner and model view's path). The near plane is the same in both eyes: the eye projections
  share rows 2 and 3, which `eyeShiftOf` now checks every stereo frame.
- **terrain_prime** (default on): before the first chunk the terrain draws a degenerate triangle
  that carries its first state block, then issues its state again with the first real chunk (as
  it already did with the second). Every other periodic probe now reads the registers right after
  this priming draw (`pass=terrain_prime`), so the lost first block shows as
  `AS3D_GPU_MISMATCH pass=terrain_prime` (counted as `prime_mismatches`, no mark: the picture is
  not hit) while the detector says whether any wash is left on screen.

### What the next console soak should show

- `AS3D_EYE_REPLAY ... replayed=` close to the frame count in stereo stages (minus the probe
  frames), `failed=0`.
- `AS3D_PASS_MS eyes=2`: the world passes once (terrain about half, opaque/rest/sprites about
  half of the second soak), `water` well under 1 ms, `touch` under 0.4 ms when the band does not
  change, `clear` up by the replay (0.3-0.4 ms); submit in stereo late in mission 1 around
  6 ms (from 9-10); mono submit 0.5-1 ms lower. The GPU time is unchanged by the replay (both
  eyes are still drawn) but should drop a little with the indexed water and no shader uploads.
- The wash: `PASS wash` with `wash_frames=0`, or `AS3D_WASH` episodes with their length and rows;
  `prime_mismatches` says whether the first state block is still being lost (if it is and no
  episode shows, the priming draw is the fix).

### What remains

- **Prepare**: `prep_entities` and `prep_order` walk every live entity (1.99 + 0.73 ms on Azahar
  for 1045 late in mission 1). An entity-level reject before the mesh and material lookups is
  neutral code (`world_scene.cpp`, shared with the desktop) and must keep the lights, health
  bars, shadow requests and marks of off-screen entities: after R3.1's simulation work shows
  whether those entities stay alive. The prepare target (1.5 ms late in stereo) is not reached.
- **Water**: still 0.86 ms per eye on Azahar (about 0.6 on the console by the soak's ratio of
  1.4): the per-tile tests of 230 tiles. A coarser tiling far from the camera changes pixels (and
  the water's depth precision was the wash's first suspect), so it waits for the console's
  verdict on the wash. The 0.3 ms target is not reached.
- **Touch band**: 0.1 ms is not reached while the bot's weapon tour changes the faces several
  times a second; in play the list replays and consolidates (9 draws).
- **Per draw**: citro3d's `C3D_DrawElements` is about half of a mesh draw. A lean draw path
  (our own context update without the geometry-shader uniform scan) or fewer draws (a shared mesh
  vertex pool, `mesh_merge` measured on the console) are the next CPU items; the HUD's 42-57
  draws need a glyph atlas.
- **GPU in stereo (11-12 ms)**, from the counters: about 25k vertices a stereo frame, of which
  8.4k are the touch band on the bottom screen (tessellated circles, every frame), 10k the
  terrain (two TEV stages with the detail texture), 3.5-6k the shadows; RGB565 targets gained
  nothing on the console (drop 4), so the cost is vertices and texturing, not fill. Options:
  bake the touch band into a texture (render it once per change, draw one quad: -8.4k vertices
  a frame), draw the bottom screen every other frame in stereo (it only needs 30 Hz; meets the
  C3D_FrameEnd swap race of "Frame synchronisation"), the coarse terrain chunks with one TEV
  stage, the brightness pass skipped when the brightness is near neutral. The indexed water
  (1400 -> 400 vertices per eye) and the shader-code uploads gone should already show a little.

## Third console soak (2026-10-11, build 664fee3; out/3ds/2026-10-11-664fee3-dirty/) and round 3 follow-up (R3.3)

The owner launched the round 3 drop from the HOME menu (its CIA, `from=none`, build `664fee3-dirty`:
only a docs edit was uncommitted). Four whole missions (AirStrike 3D 1 and 2, mono and stereo), every
stage to Mission Complete, every end state equal to the desktop sim, the push of 3 files (4.3 MB). This
section is what the soak said, what R3.3 changed for it, and how to read the next soak.

### The numbers, against the second soak (5192662)

Per displayed frame, averages over the stage's frames after the first 300 (`tools/3ds_perf_report.py`
and the stage's `perf.csv`); `gpu` is perf.csv's `gpu_ms`, the GPU's tail after `C3D_FrameEnd` (see
"stereo GPU" below); `verts`/`draws` of 664fee3 in stereo count the left eye only (the replayed right eye
counted nothing; fixed by R3.3, B3).

| stage | build | step per step | step per frame | prepare | submit (= cpu) | gpu tail | submit + tail | verts | draws | frames at 60 Hz |
|---|---|---|---|---|---|---|---|---|---|---|
| as3d 1 mono | 5192662 | 3.29 | 5.63 | 1.74 | 6.38 | 7.58 | 13.96 | 22744 | 187 | 29.8 % |
| | 664fee3 | 2.61 | 3.24 | 1.77 | 5.66 | 7.66 | 13.32 | 21899 | 171 | 78.2 % |
| as3d 1 stereo | 5192662 | 3.25 | 6.94 | 2.23 | 9.25 | 11.47 | 20.72 | 32245 | 306 | 0.6 % |
| | 664fee3 | 2.59 | 5.03 | 2.25 | 6.91 | 13.47 | 20.38 | 20058 (left) | 179 (left) | 6.7 % |
| as3d 2 mono | 5192662 | 3.32 | 4.50 | 1.53 | 6.03 | 6.98 | 13.01 | 20974 | 176 | 66.3 % |
| | 664fee3 | 2.59 | 3.11 | 1.54 | 5.34 | 7.22 | 12.56 | 20256 | 161 | 82.0 % |
| as3d 2 stereo | 5192662 | 3.31 | 6.64 | 2.00 | 8.65 | 10.71 | 19.36 | 28522 | 281 | 1.4 % |
| | 664fee3 | 2.59 | 5.13 | 1.96 | 6.43 | 12.65 | 19.08 | 18150 (left) | 165 (left) | 2.3 % |

- **mono60 FAIL** in both missions: 78.2 % and 82.0 % of the frames after the grace at 60 Hz (97 %
  needed), misses 2.43 % / 2.14 %, the pacer's 12-misses-in-120 rule tripped 6 and 3 times (12 / 6
  changes). Every `AS3D_MISS` is `vblanks=2`, mostly `steps=1`.
- **stereo PASS** at 30 Hz (misses 0.03 % / 0.01 %); the soak's GPU avg 13.39 / 12.66 ms (p95 17.35 /
  15.81) against 11.39 / 10.71 for 5192662, the CPU path down from 9.2 to 6.9 ms as R3.2 intended.
- The simulation step fell from 3.3 to 2.6 ms per step (R3.1); per mono frame 3.2 ms.
- Lazy loads 0, fence 0, GPU busy 0, textures in frame 0, probes 1725 (stall 1.4 ms mono, 3.5-3.7 ms
  stereo, max 10.3), the eye replay `replayed=290 of 300` frames (the probe frames record both eyes),
  9-15k words per replay in 0.28-0.38 ms.
- **Load times**, two measures: `mission N loaded in 1806 ms` (log) is the game session's start (pak
  mount 1286, defs, world, level); `PERF_SUMMARY load_ms` = the perf report's `mission load 3222 ms`
  runs from the same start to the end of the renderer's level start (terrain, precache, shadows, the
  loading frames: `AS3D_LOAD phase=level_start ms=1389` and the terrain's 366). The gate (3000 ms) is
  on the second.

### The wash

- **The detector saw no wash**: 51792 eye-frames judged, 0 washed frames, 0 episodes. Its hardware
  self-test passed: `AS3D_WASH_SELFTEST ... equal_to_full_readback=0/98 (bytes 3,2,1 / 2,1,0)` meant 0
  samples equal with the reading of bytes 3, 2, 1 and **98 of 98 with bytes 2, 1, 0** (the reading the
  detector uses: the 24-bit depth is the word's low three bytes). The line now prints
  `equal_to_full_readback_bytes321=` and `equal_to_full_readback_bytes210=` separately. The L/auto
  mark's `markNN_depth.ppm` read bytes 3, 2, 1 (the stencil as red): it now writes bytes 2, 1, 0.
- **The priming draw's probe caught 101 register mismatches, all in mission 2** (stage 3 mono 53,
  stage 4 stereo 48; none in mission 1, none at the `pass=terrain` probes after the base draws, `dirty=0`
  on all, `state_mismatches=0` on the CPU layers): 86 `field=depth_test expected=0x1f71 actual=0x0f10`
  (depth test off, depth write off: the value every 2D pass and every 3D pass with the test off leave)
  and 15 `field=blend expected=0x01010000 actual=0x10110000` (ONE/ONE with alpha ZERO/ONE: the 2D
  layer's additive blend, `renderer2d_pica.cpp` `Blend::Add`). In stereo they mostly come together (the
  whole block stale: the previous frame's last 2D state; twice the blend alone, a few times the depth
  alone); in mono only the depth register was stale and the blend register already held the terrain's
  `0x01010000`. They come in bursts of consecutive prime
  probes (one per 60 frames), at the same point of the mission in mono and stereo (the bot is
  deterministic): mono frame serials 20609-20729 (3 probes in a row), 21449-21869 (8), 22109-23129 (18),
  25169-25529 (7), then runs of 1-3 to 29009, i.e. from about 35 s to 3 minutes into mission 2;
  stereo 31497, 31917-32097 (4), 32277-32697 (8), 33777-34137 (7), 34437-34677 (5), 34797-34917 (3),
  35217, 35877-35997 (3), 36117-36177 (2), 36357. No auto-mark was taken: `gpuMismatch()` excluded the
  prime mismatches from the marks ("the pass re-issues its state after it: the picture is not hit").
- The detector over the same windows: the share of samples agreeing with the analytic terrain is
  88.0 % in the 300-frame windows around a burst against 91.1 % in the others of the mono stage (86.7 /
  86.0 in stereo), "nearer than terrain" (models, effects) 10.7 against 7.3 %; no sample at or behind the
  water plane in any window. Mission 2's scenery (more models in front of the ground) explains that
  difference as well as any GPU state: the picture was not washed.
- What the probe cannot tell: the probe splits the command list, runs the queue and waits for it
  (`gxCmdQueueWait(-1)`), then reads. libctru's GX queue (gxqueue.c) counts **any** completion interrupt
  but PSC1 and the vblanks against its entries and submits up to three entries at once, so an interrupt
  counted for the wrong entry would end the wait before the GPU ran the list: a stale read, the registers
  still holding the previous pass's state, exactly what was read. A state block that the GPU dropped or
  never received reads the same. The owner saw the wash with his own eyes (earlier sessions and the
  second soak: brief, lower half only); the third soak's report carries no observation of the owner.

### What R3.3 changed for the wash (A)

**A1, evidence at the mismatch** (`state_check_pica.{h,cpp}`, `ctr_wash.cpp`, `pass_profile.{h,cpp}`):

- **The canary**: every probe writes a value derived from the probe count into
  `GPUREG_LIGHT0_SPECULAR0` (0x140: lighting is never enabled, the register has no effect) as the last
  word before its split, and reads it back with the registers. Read back = the GPU had executed the
  whole list when the registers were read. `canary=<ok>/<probes>` in every `AS3D_STATE` line,
  `canary_ok=`, `canary_stale=` in `AS3D_SOAK_RESULT` and the `wash` line.
- **A second read**: a mismatching probe waits again, sleeps 1 ms and reads the same registers again
  (`reread_changed=`).
- **`AS3D_GPU_MISMATCH_DETAIL`** (the first 6 per level, then counters): `fields=`, the expected
  depth/blend, `first=` and `second=same|changed` (depth, blend, depth map enable/scale/offset, depth
  write), `canary=<written> first=<read>(ok|stale) second=<read>`, `queue=<entries>/<started>/<done>`
  before the probe's run `-><after the wait>`, `stat=` the GPU status word 0x1EF00034 at both reads,
  `cmdbuf=<words of earlier lists>+<words of this list>/<buffer words>` (citro3d starts the buffer
  afresh at every frame: no wrap), `list=` the scan of the command words the terrain pass recorded up
  to the probe (`ok|cut/words=/cmds=/d107=<last value written to 0x107>,m<byte mask>@<word>/b101=...
  /canary@<word>/fin=<GPUREG_FINALIZE writes>`: whether citro3d put the terrain's depth/mask and blend
  words in the list at all), `forced=` (A2) and `trail=` the last passes that drew, with their frame
  (`f-1:hud f-1:touch f0:clear`: what ran before the terrain).
- **Prime mismatches take auto-marks now** (the first 3 per level, their own budget beside the 3 of the
  other probes and the 3 of the detector): the screens and `markNN_depth.ppm` of that frame, and the
  next 3 frames probe every pass in full (`AS3D_GPU_REGS` each).
- **`AS3D_WASH_TRACE`**: the detector's judgement of the mismatching frame and the next, per row band
  (`rows20-110`, `rows128-158`, `rows168-198`, `rows208-238`, each judged/agree/nearer/washed).
- **`AS3D_STATE ... prime_queue=<min>..<max> prime_words=<min>..<max>`**: at the prime probes of the
  last 60 frames, the GX queue entries before the probe's list and the command words recorded in the
  frame before it. On Azahar mission 2 these stay `2..2` and constant: a burst on the console with
  another value says that something queued or recorded before the terrain differs in those frames.

**A2, the candidate fix, on by default** (`terrain_force_state=1`, `terrain_pica.h`,
`detail::forceEffectState` in `state_check_pica.cpp`): right after the priming draw (before the first
chunk with `terrain_prime=0`) the terrain writes citro3d's whole effect block straight into the command
list with `GPUCMD_AddWrite` and friends, word for word what `C3Di_EffectBind` (citro3d 1.7.1
`source/effect.c`) writes for the current context: depth map enable (0x6D), cull (0x40), depth map
scale/offset (0x4D/0x4E), alpha test, stencil test and op, depth test and colour/depth masks
(0x104-0x107), the gas depth function, blend colour (0x103), blend function (0x101), logic op (0x102),
the colour operation (0x100), the shadow, early depth. It reads the values from citro3d's context
(the state check's mirror of 1.7.1's `internal.h`), whatever its dirty flags say, and leaves the effect
flag dirty afterwards, so citro3d writes the same block once more with the next draw (its context and
the GPU agree either way). The prime probe reads the registers after these words. Reading the next
soak: **prime mismatches 0** with the forced words = a lost (skipped) state update, fixed by them; **prime
mismatches still there with `canary ... (stale)` or `second=changed`** = the probe's read raced the GPU
(the queue's count, see above), not the GPU state; **still there with the canary read back, the second
read the same and `list=` holding `d107=0x00001f71`** = the GPU executed the list and did not keep the
register: a hardware matter no command word fixes; then look at `trail=` and `prime_queue=`.

The audit of command words outside citro3d's tracking: (1) the eye replay's copy
(`GPUCMD_AddRawCommands`) marks every state flag dirty afterwards (attribute and buffer setup, effect,
viewport, scissor, program and its code, texture units and TEV, fog LUT, light environment) and every
uniform (`C3Di_DirtyUniforms`); the region holds no frame buffer write (the prime draw before it emits
the frame buffer), no geometry shader, no procedural texture or gas: nothing it can leave behind
citro3d's back. (2) R3.2's 2D "replay of an unchanged list" replays the draw list through citro3d's own
calls (`C3D_DrawArrays` with the tracked state), not raw words; its own shader/vertex cache lives inside
one flush. (3) The probe's canary (0x140, unused) and A2's forced block (the effect flag left dirty).
Nothing else writes command words (`GPUCMD_` appears in these three places only).

**A3, the detector's coverage of the nearest chunk** (`ctr_wash.cpp`): per judged left-eye frame the
world box of the first chunk the base pass drew (`terrainFirstChunkBounds`) goes through the eye's clip
matrix: the screen rows it covers (to the bottom edge when a corner is behind the camera) and how many
sample rows fall inside. `AS3D_WASH_COVERAGE frames= first_chunk_rows_avg= sample_rows_inside_avg= min=
frames_under_2= sample_rows=` once per level (after 1800 judged frames or at the level's end). Azahar,
the old rows (14 per column, lower half 126..234 step 12): mission 1 mono rows 96..239 on average,
10.2 sample rows inside, fewer than 2 in 2.9 % of the frames (mission 2 stereo 2.3 %). The nearest
chunk then shows only its last rows above the bottom edge, under the lowest sample row. The rows are
now 16 per column, the lower half 128..238 step 10 (112 samples per eye, +14 % judging): 12.0-12.2 rows
inside on average, fewer than 2 in 1.7 % of the frames (missions 1 stereo and 2 mono): the frames
where the nearest chunk's visible part is under 10 pixels tall.

### What R3.3 changed for the stereo GPU question (B)

- **The "regression" is the measure.** perf.csv's `gpu_ms` is `C3D_GetDrawingTime`: from the end of
  `C3D_FrameEnd` to the end of the GX queue (citro3d `renderqueue.c`), i.e. the GPU's tail after the
  CPU has finished recording. Under `gpu_kick` the GPU starts at the end of the left eye; when the CPU
  records the right eye 2.3 ms faster (the replay), the same GPU work shows 2 ms more of it after
  `C3D_FrameEnd`. Submit plus tail went down in all four stages (table above: 20.72 -> 20.38 and 19.36
  -> 19.08 ms in stereo, 13.96 -> 13.32 and 13.01 -> 12.56 in mono). perf.csv now also has
  `gpu_kick_ms`: the same frame's GPU time from its first command list.
- **The both-eye cull does not inflate the terrain**: `AS3D_EYE_CHUNKS` (new, every 300 frames:
  chunks per render that the eye's own frustum sees and that it drew) on Azahar, missions 1 and 2 in
  stereo with the replay: own 4.87-4.90, drawn 4.87-4.90 (left eye); the chunks are strips across the
  whole map, so the eyes' horizontal shift almost never adds one. No change there.
- **Per-eye counts (B3)**: a replayed right eye counts the draws and vertices the left eye recorded for
  it (`stats_` of the world renderer), so perf.csv's `verts`/`draws` hold both eyes again, and the new
  columns `draws_r,verts_r` give the right eye's share (Azahar mission 1 stereo: 61-64 draws, 9254-9270
  vertices of 201-207 / 27592-27624).
- **The detector's copy between the eyes (B4)**: moving both copies after the right eye would not
  remove a GX entry (each eye's clears split the list anyway) and the tail measure explains the rise; it
  stays where it is. The A/B stages below measure its cost directly (`wash_check=0`).
- **Per-stage overrides in the soak list (B1)**: `soak=game:level:eyes[:key=value...]`, applied over the
  file's keys for that stage only (`applyRunConfig`; keys the stage sets itself are refused). The stage
  name in `soak.txt`, `AS3D_SOAK_STAGE ... opts=` and `AS3D_SOAK_RESULT ... opts=` carries them; the
  receiver's directory is `stages/<NN>-<game>-<level>-<eyes>-<key=value>-...`; the end-state comparison
  replays a shortened stage with its own step count. `tools/3ds_receive.py --report <dir>` runs the
  report on a run already on disk. Checked on Azahar end to end (three stages with options, 3 of 3 end
  states equal to the desktop sim).
- **The drop's list (B2, `run-soak-r33.ini`)**: the four whole missions first (the wash evidence), then
  one-minute A/B stages of 3600 steps from the start of mission 1, the same stretch of the mission
  each: stereo baseline, `eye_replay=0`, `wash_check=0`, `terrain_prime=0`, then mono `wash_check=0` and
  mono `music=0` (the controls). About 25 minutes in all.

### What R3.3 changed for mono at 60 Hz (C)

- **Where the misses come from (C3)**: a frame's loop period is the sum of perf.csv's
  `sync,step,prepare,wait,submit,end,after` (it matches the console's own miss count: 238 = misses60 +
  excused60 of mission 2). **59 % (mission 1) and 78 % (mission 2) of the mono misses are frames whose
  number is 1 modulo 32**: a 57-65 % miss rate at that phase against 0.6-1.3 % everywhere else; the
  gaps between misses are 32 frames (90 and 119 times) or 1 (the catch-up). `Perf::record` sampled the
  heap's high-water mark every 32 frames with `mallinfo()`, twice (once more in `heapSample`): newlib's
  `mallinfo` walks every chunk of the 15-18 MB heap on the main thread, and the next frame missed its
  vblank. This is also the "music period" of the console session of 2026-10-11 (misses every 32
  frames), which the audio work did not remove. The heavy submits are not the cause: the frame before a
  miss is 2.2-3.4 ms busier than the average (the heap walk itself is not in the columns), the 7 ms
  submits are the late mission's normal ones. **Fix**: one `mallinfo` every 600 frames
  (`kHeapSampleFrames`, `ctr_perf.h`) feeding both high-water marks, and the next frame excused by the
  pacer like an SD write. Expected: the mono misses down to 1.3 % (mission 1) and 0.6 % (mission 2).
- **The pacer replay (C1, `tools/3ds_pacer_replay.py`)**: from a mono stage's perf.csv, the real misses
  of the frames drawn at 60 Hz (the period over 20.5 ms) and, for the frames drawn at 30 Hz, two models:
  the brief's "sum" (CPU path plus the GPU tail over 16.7 ms), which the tool shows to be far too
  pessimistic on the real 60 Hz frames (mission 1: 228 hits, 3855 false alarms), and a "calibrated" one
  (the miss rate of the 60 Hz frames of the same stage per 1 ms bucket of that sum, a seeded draw).
  Limits: a replay keeps one row per frame (the console steps twice per 30 Hz frame), the probe frames
  and the 300-frame grace are not counted, and a rate per bucket cannot place periodic misses in time.
  Results (calibrated model; drops to 30 Hz, stutters = misses while at 60 Hz):

  | rule (drop / return) | mission 1 at 60 | drops | stutters | mission 2 at 60 | drops | stutters |
  |---|---|---|---|---|---|---|
  | 12/120, 60 fitting frames (the old default) | 69.7 % | 4 | 189 | 75.7 % | 2 | 187 |
  | 24/120, 60 fitting frames (**new default**) | 77.9 % | 3 | 266 (1.41x) | 84.1 % | 1 | 243 (1.30x) |
  | 30/120, 60 fitting frames | 78.8 % | 3 | 286 (1.51x) | 84.4 % | 1 | 250 |
  | 12/120, 30 fitting frames | 84.0 % | 9 | 286 (1.51x) | 90.5 % | 7 | 286 (1.53x) |
  | 3 misses in a row | 50.5 % | 4 | 108 | 80.0 % | 1 | 198 |
  | never drop in mono | 100 % | 0 | 578 (3.1x) | 100 % | 0 | 380 (2.0x) |

  The replay's 12/120 shares are below the console's (78.2 / 82.0 %: the console also excuses SD writes
  and late loads); the comparison between rules is what it is for.
- **The rule (C2)**: the best share at 60 Hz within 1.5x the old rule's stutters in both missions is
  24 misses in 120 frames with the current return (60 fitting frames, doubled after a quick fall back):
  `pace_rule=24`, the default (`apps/3ds/source/pace_rule.h`, desktop-tested); `pace_rule=12` is the old
  rule, `run3` and `never` the others. With the heap walk gone most of the misses the rule saw are gone
  too; the rule matters for what is left.

### Azahar runs (correctness only; Azahar's timings are relative)

| run | result |
|---|---|
| `mode=soak`: mission 1 mono 1500 steps `probe_inject=5`, mission 2 stereo 1200 steps, mission 1 stereo 600 steps `eye_replay=0 wash_inject=2` | the injected mismatch went through the whole evidence path: `AS3D_GPU_MISMATCH_DETAIL` (canary read back `ok`, `second=same`, `queue=2/0/0->3/3/3`, the list scan holding `d107=0x00001f71` and `b101=0x01010000`, `forced=1`, the trail `f-1:rest ... f-1:touch f0:clear`), `AS3D_WASH_TRACE` for that frame and the next, an auto-mark; 0 other mismatches, canary 101 of 101, 0 washed frames; stage names and `opts=` as above |
| `mode=soak`: mission 1 stereo 1500 steps `wash_inject=2`, mission 2 mono 2400, mission 1 mono 400 `terrain_force_state=0 pace_rule=12` | `wash_inject=2` still caught with the replay on (one 2-frame episode, both eyes, 100 samples, rows 20..238), otherwise 0 prime and 0 terrain mismatches, canary 130 of 130, 0 washed frames; self-test 112 of 112 with bytes 2, 1, 0; `pace_rule=12/120` and `terrain_force_state=0` reach their stage; the receiver's `--report` on the run: three option directories, 3 of 3 end states equal to the desktop sim |
| mission 2 mono 4600 steps (`mode=soak`) | over the stretch where the console's bursts began (steps 2100-4600): 0 prime mismatches in 41 prime probes, canary 82 of 82, `prime_queue=2..2 prime_words=422..422` in every window, coverage 12.1 rows (1.6 % under 2); end state equal to the desktop's |
| the drop's list with every stage shortened to 300 steps | 10 of 10 stages, `VERDICT PASS`; each override reached its stage (`AS3D_WASH_CHECK on=0` twice, `terrain_prime=0`, `eye_replay=0`, `music off (run.ini music=0)`); canary 94 of 94, 0 mismatches |

### What the next soak's output must be read for

1. `soak.txt`'s `wash` line: `prime_mismatches=` (0 expected if the forced words were the fix),
   `mismatch_probes=`, `reread_changed=`, `canary_ok=<n>/<probes>` (all of them on a sane read path;
   **if the canary is stale on the clean probes too, the console does not read 0x140 back** and only the
   second read speaks), `canary_stale=`.
2. Each `AS3D_GPU_MISMATCH_DETAIL` with the decision table of A2 above; the `AS3D_WASH_TRACE` lines
   after it (a washed band there would tie the registers to the picture); the auto-marks of the prime
   mismatches (`mark*_depth.ppm`: bytes 2, 1, 0 now); `AS3D_STATE ... prime_queue= prime_words=` around
   the bursts against the quiet windows.
3. `AS3D_WASH_COVERAGE` per level (at least 2 rows in about 98 % of the frames) and `AS3D_WASH` /
   `AS3D_WASH_START` lines if any.
4. mono60: the share at 60 Hz and the misses of the whole missions (expected well above 90 % without
   the heap walk; `tools/3ds_pacer_replay.py stages/<mono stage>` replays the rules on them), the
   `AS3D_MISS` lines (no 32-frame period left), the `music=0` and `wash_check=0` mono controls against
   the first mono stage's first 3600 steps.
5. Stereo: `gpu_kick_ms` and `gpu_ms` per stage, `draws_r`/`verts_r` (the right eye equal to the left
   eye's world under the replay), and the A/B stages against the baseline: `eye_replay=0` (the CPU
   saving and the tail moving back), `wash_check=0` (the detector's GPU cost: two GX copies per frame),
   `terrain_prime=0` (the priming draws' cost); `AS3D_EYE_CHUNKS` per stage.

### What remains

- The cause of the prime mismatches, until the next soak's evidence: a probe race (the GX queue's
  interrupt count), a dropped block, or a hardware latch. If the forced words end them, they stay
  (a few dozen words per eye); if the canary says the read raced, the probe should wait on the GPU's own
  status (`stat=`) rather than the queue's count, and the same race would also make the fence of
  `C3D_FrameBegin` early in those frames (`AS3D_FENCE` would not see it: it reads the same count).
- The wash itself: the owner has seen it; no instrument has caught it since c314767 (clear=split and the
  re-issued terrain state). If the next soak is clean again, `run-play.ini` with L remains the proof.
- Mono at 60 Hz beyond the heap walk: the rest are bursts of heavy frames late in the missions (submit
  7-8 ms plus a 9-10 ms tail); the per-pass CPU items of "Stereo CPU path, What remains".
- Stereo at 60: 19-20 ms of CPU plus GPU tail per frame; the GPU's own time per frame (`gpu_kick_ms`)
  decides the next cut.
