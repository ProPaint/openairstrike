# Nintendo 3DS feasibility spike

Not a port: a measuring app (`as3d_spike.3dsx`) that answers the three questions of
[docs/3ds-spike.md](../../docs/3ds-spike.md). It compiles the platform-independent engine
modules (`core`, `vfs`, `formats`, `script`, `game`, plus `input_actions.cpp` for the test
pilot) straight from `engine/src` with devkitARM, then:

1. mounts the AirStrike 3D paks from `sdmc:/3ds/openairstrike/as3d/`, loads the definitions;
2. runs missions 1, 6 and 14 headless, 600 frames each with the `as3d_sim --bot` pilot,
   printing load time, step time (system tick counter, 268 MHz), heap and entity counts;
3. reloads mission 1 and draws its terrain and the player's helicopter with citro3d on the
   top screen while the world keeps stepping (300 measured frames, then until START).

Output goes to the bottom screen and to `sdmc:/3ds/openairstrike/spike_log.txt`; the
engine's own warnings to `sdmc:/3ds/openairstrike/engine_log.txt`. START quits.

## Build

Needs Docker and the pinned devkitARM image (no native devkitPro):

    apps/3ds/build.sh          # -> apps/3ds/as3d_spike.3dsx (about 45 s from clean, 8 cores)
    apps/3ds/build.sh clean

The Makefile is devkitPro's 3DS application template with `SOURCES` pointing into
`engine/src`. `source/scene.v.pica` is the vertex shader (picasso, built by the template rules).

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
        ~/.var/app/org.azahar_emu.Azahar/data/azahar-emu/sdmc/3ds/openairstrike/as3d_spike.3dsx

`DISPLAY` must also be set on the host side of `flatpak run`, or flatpak does not expose the
X11 socket and Qt aborts with "could not connect to display". Screenshots: `xwd -id <the
"Azahar 2125.1.1" toplevel>` then `ffmpeg -i shot.xwd -update 1 shot.png`; the top screen is
the crop `410x245+434+20` of a 1280x540 window. Emulator timings are not hardware timings
(see the report).

## Run on a real 3DS

Put the paks on the console's SD card as above, open the Homebrew Launcher, press Y
(netload), then from this machine:

    apps/3ds/build.sh netload 192.168.0.151

(equivalently `3dslink -a 192.168.0.151 apps/3ds/as3d_spike.3dsx`, port 17491). Or copy
`as3d_spike.3dsx` to `sdmc:/3ds/` and start it from the launcher. Afterwards
`sdmc:/3ds/openairstrike/spike_log.txt` holds the real numbers; the questions they settle are
listed under "Open on hardware" in docs/3ds-spike.md.

## GPU probe (M1.0)

`source/probe.cpp` measures what the PICA200 costs for eight texture, target and blend
variants (docs/3ds-port.md, "M1.0 GPU probe"). It is selected by a run config on the SD card,
`sdmc:/3ds/openairstrike/run.ini`:

    mode=probe        # or mode=spike; no file = spike
    hold=0            # optional: frames to keep drawing after each variant (screenshots)
    only=             # optional: run just these variants, e.g. only=CH

The probe runs unattended, prints a table on the bottom screen and appends it to
`sdmc:/3ds/openairstrike/probe_log.txt`. START quits.
