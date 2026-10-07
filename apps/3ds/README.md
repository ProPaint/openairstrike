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
empty, so render, ui, audio and platform contribute their neutral files only; music is the
stub, `AS3D_WITH_OPENMPT=OFF`). `source/scene.v.pica` goes through `ctr_add_shader_library`
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
| `mode` | `spike` (default), `probe`, `sim`, `bot`, `play` | `spike` and `probe` run the old code paths. `sim`: the mission headless at full speed with the bot, no screens, ends after `frames` steps (reproduces `as3d_sim --bot`). `bot`: real-time 60 Hz loop with the bot's input and the screens through the frame hook (for now cleared to a colour that cycles with the frame count). `play`: the same loop with the pad (circle pad / D-pad steer, A fire, B missile, X power-up, Y weapon, R next missile, START pause; a stub until M1.9). |
| `game` | profile key, default `as3d` | the game; its paks are read from `sdmc:/3ds/openairstrike/<key>/` |
| `level` | N, default 1 | the mission to start (the front end is off: `startLevel=true`, `levelFlow=false`) |
| `frames` | N, default 0 | simulation steps, then the app quits cleanly; 0 means 600 in `sim` and unlimited otherwise |
| `perf` | `0` (default) or `1` | write `perf.csv` |
| `difficulty` | N | the engine default when absent |
| `hold`, `only` | | probe only (see above) |

`mode=bot level=1 frames=3600 perf=1` is the M1 acceptance run. START+SELECT held together quits
cleanly in every loop mode. HOME and sleep pause the world, call the save hook (none yet) and flush
the log; the clock restarts on resume (no replay of the time away). On a New 3DS the loop turns the
speed-up on.

Outputs, all in `sdmc:/3ds/openairstrike/`: `log.txt` (every `AS3D_INFO/WARN/ERROR` of the engine and
the app, written once a second; replaced at each start), and with `perf=1` `perf.csv`, one row per
displayed frame: `frame,step_ms,prepare_ms,cpu_ms,gpu_ms,eyes,verts,draws,tex_vram_kb,tex_linear_kb,
linear_free_kb,lazy_loads`. The renderer columns are 0 until the PICA backend and the frame composer
fill them, `lazy_loads` until M1.10. At the end `log.txt` gets the end state in the format of
`as3d_sim` and `PERF_SUMMARY` lines (step and frame time avg/p95/max, frames over 17 ms, load time,
heap high-water, linear free). Fetch with ftpd and report:

    tools/3ds_fetch_logs.sh 192.168.1.133         # -> out/3ds/<date>-<commit>/
    python3 tools/3ds_perf_report.py out/3ds/<date>-<commit>

Code: `ctr_main.cpp` (entry), `run_config.*`, `ctr_loop.*` (the loop, `FrameHook`), `ctr_perf.*`;
the graphics context is the engine's ctr platform backend (`engine/src/platform/ctr/`, its
private header `ctr_context.h`).
