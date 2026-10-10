// sdmc:/3ds/openairstrike/run.ini of the 3DS app: what to run. One "key=value" per line, '#'
// or ';' start a comment line, unknown keys are ignored (probe.cpp reads its own keys, hold= and
// only=, from the same file). A missing file, or a file without mode=, gives the defaults: mode=play
// (the front end: the game selector, the menus, audio and music on), what an installed copy runs.
// Documented in apps/3ds/README.md.
#pragma once

#include <string>
#include <vector>

#include "as3d/console_pad.h"

namespace ctrapp {

constexpr const char* kDataDir = "sdmc:/3ds/openairstrike"; // no trailing slash

enum class QualityMode { Auto, New, Old }; // run.ini quality=

enum class Mode {
    Spike, // the feasibility spike (main.cpp); also every other mode= value (the test modes main.cpp reads)
    Probe, // the M1.0 GPU probe (probe.cpp)
    Sim,   // the mission headless at full speed with the bot, no screens (as3d_sim --bot)
    Bot,   // real-time 60 Hz loop, bot input, screens through the frame hook
    Play,  // real-time loop with the pad; without level= the front end (as Menu); the default
    Menu,  // the front end (M2.1): menus on the bottom screen, missions started from them
};

struct RunConfig {
    Mode mode = Mode::Play;
    std::string launchedFrom;  // not a key: argv[0] of the launch (HBL: the .3dsx path; 3dslink: 3dslink:/...; a CIA: none)
    std::string game = "as3d"; // game=KEY: a profile key of as3d/game_data.h; its paks live in <dir>/<key>/
    bool gameGiven = false;    // game=KEY is in the file (not absent, not game=select): no game selector
    int level = 1;             // level=N: the mission to start (1..the game's mission count)
    bool levelGiven = false;   // level= is in the file (mode=play without it runs the front end)
    int frames = 0;            // frames=N: simulation steps, then quit; 0 = 600 in sim, unlimited otherwise
    bool perf = false;         // perf=1: write perf.csv
    bool logInput = false;     // log_input=1: an AS3D_INPUT line per change of held bits / touch button
    bool logLazy = false;      // log_lazy=1: an AS3D_LAZY line per first-use load after a precache (as3d/load_stats.h)
    bool botWeapons = false;   // bot_weapons=1: the weapon tour (as3d/input.h applyWeaponTour) over the input
    as3d::PadBindings bindings = as3d::defaultBindings(); // bind_fire=A ... (see console_pad.h)
    int difficulty = -1;       // difficulty=N: -1 = the engine's default
    // The frame composer (ctr_frame.h):
    int dumpAt = 0;            // dump_at=N: at displayed frame N write top_left/top_right/bottom PPMs; 0 = never
    float brightness = 0.6f;   // brightness=F: config.ini Brightness (0.5 = neutral: no pass), default 0.6
    float slider = -1.0f;      // slider=F: force the 3D slider to F in [0, 1]; -1 = the hardware slider
    float eyeOffset = 0.0f;    // eye_offset=F: world units at slider 1; 0 = ctr_frame.h kMaxEyeOffset
    float minFocal = 0.0f;     // min_focal=F: smallest focal distance; 0 = ctr_frame.h kMinFocalDistance
    // Stereo comfort (docs/3ds-port.md "Stereo comfort and frame polish (round 2, P5)"):
    float maxParallaxPx = 10.0f;    // max_parallax_px=F: the nearest content's negative parallax at slider 1 (px of 400)
    float maxFarParallaxPx = 12.0f; // max_far_parallax_px=F: the farthest terrain's positive parallax at slider 1
    bool stereoComfort = true;      // stereo_comfort=0: no clamp and no focal easing (the eye offset as before)
    bool precache = true;      // precache=0: skip the level-start precache (first-use loads, for comparison)
    bool audio = true;         // audio=0|1: sound in bot and play (ctr_audio.h); never in sim
    bool music = true;         // music=0|1: the level and menu music (M2.4; needs a libopenmpt build)
    int musicTaps = 0;         // music_taps=N: libopenmpt's interpolation (1, 2, 4, 8 taps); 0 = its default, 8
    // The front end (mode=menu, or mode=play without level=; ctr_loop.cpp):
    int attract = 0;           // attract=N: the attract level 1..4 behind the main menu; 0 = random
    bool logo = true;          // logo=0: no intro pages
    bool menuBot = false;      // menu_bot=1: during a mission started from the menus the bot flies
    // cycle=1,6,1,6: the memory growth check of M5.4 (front end only). From the main menu the
    // loop starts each listed mission through GameFlow::startMission, lets it run cycle_frames
    // displayed frames (default 240), leaves it through GameFlow::loadAttract (what "Quit" in the
    // in-game menu does), waits 60 frames, takes the next; then the app stops. Level start and
    // level end log AS3D_MEM as always.
    int reportAt = 0;          // report_at=N: with menu_bot=1: log the mission's end-state lines once after N bot steps (front end)
    std::vector<int> cycle;
    int cycleFrames = 240;
    QualityMode quality = QualityMode::Auto; // quality=auto/new/old: the quality profile (quality_profile.h)
    // menu_bot_steps=N: after the N-th running step of a mission (the world's steps but those the
    // front end holds: the start dialogue, the menus) the end state is logged as in mode=bot (the run
    // goes on); 0 = never.
    int menuBotSteps = 0;
    // end_at=N, end_kind=complete|gameover: at the N-th running step the mission ends as EndLevel
    // or the last life would (for scripted runs through Mission Complete and Game Over); 0 = never.
    int endAt = 0;
    int endScore = -1;         // end_score=N: player 1's score set at end_at (the name entry needs 30000)
    bool endGameOver = false;
    // unlock_heli=N: the profile has helicopter N unlocked at start (a fresh sequel save has entry
    // 0 only; as3d_sim's default is 1). For comparisons with mode=bot; the unlock is saved.
    int unlockHeli = -1;
    bool uiFixedDt = false;   // ui_fixed_dt=1: the menus' clock advances 1/60 s per displayed frame (comparisons)
    // pad_script=F:NAME[,F:NAME...]: presses injected into the pad at displayed frame F (held 6
    // frames): A B X Y L R ZL ZR START SELECT UP DOWN LEFT RIGHT, or T<x>/<y> for a touch at
    // bottom-screen pixel (x, y). Frames must not decrease. For scripted runs (the emulator).
    std::string padScript;
    // The name entry (ctr_name_entry.h): name_entry=applet (default) shows the software keyboard
    // when the screen opens (Y again after Cancel); name_entry=touch leaves only the screen's own
    // touch keyboard. name_script=TEXT: the keyboard's result without the applet (scripted runs:
    // the emulator's keyboard dialog needs a hand); the field's rules apply as to a typed name.
    bool nameApplet = true;
    std::string nameScript;
    int dumpEvery = 0;         // dump_every=N: every N displayed frames, <prefix>f<frame>_{top,bottom}.ppm
    int dumpFrom = 0;          // dump_from=S: dump_every counts simulation steps from step S (<prefix>s<step>_*.ppm)
    bool dumpScreens = false;  // dump_screens=1: 40 frames after each front-end screen change,
                               // <prefix><nn>_<screen>_{top,bottom}.ppm
    std::string dumpPrefix = "fe_"; // dump_prefix=: the file name prefix of dump_every / dump_screens
    // Hardware A/B of the world's depth and the water (docs/3ds-port.md "Water on hardware"):
    bool fog = true;           // fog=0: the level fog stays off (hardware A/B of the fog table)
    bool depthMapZ = false;    // depthmap=z|w: the world passes with the Z-buffer mapping instead of the W-buffer
    bool water = true;         // water=0: no water pass
    int fixedSteps = 0;        // fixed_steps=N (1..8): N simulation steps per displayed frame, the clock ignored
    bool meshMerge = false;    // mesh_merge=1: runs of tiny mesh records as one draw (mesh_renderer_pica.h)
    bool meshSort = true;      // mesh_sort=0: the opaque meshes in submission order (mesh_renderer_pica.h)
    float waterTile = -1.0f;   // water_tile=N: the plane's tile size in world units (0 = the GL's 320-unit quads); -1 = the default
    // Hardware tuning (A/B runs, docs/3ds-port.md "Hardware tuning"):
    std::string passes;        // passes=a,b,..: only these passes draw (pass_profile.h names, world, all); empty = all
    bool hud = true;           // hud=0: no HUD on the bottom screen
    bool touch = true;         // touch=0: no touch band on the bottom screen
    bool top565 = false;       // top_format=rgb565: RGB565 top targets and screen (ctr_context.h setTopColor565)
    bool prefetch = true;      // prefetch=0: no read plan (ctr_loop.h ReadPlan)
    bool shadowCache = true;   // shadow_cache=0: every silhouette rasterised (no sdmc:/.../cache/*.shadows)
    // Stereo at 60 (docs/3ds-port.md "Stereo at 60 (round 2, P3)"); -1 = the quality profile's default.
    int terrainLod = -1;              // terrain_lod=auto|0|1|2: far chunks coarse: 0 never, 1 in stereo, 2 always
    float terrainLodDistance = -1.0f; // terrain_lod_dist=N: world units from the camera to a coarse chunk
    float terrainLodBand = -1.0f;     // terrain_lod_band=N: hysteresis, coarse beyond dist + N, fine again under dist - N
    int shadowAtlas = -1;             // shadow_atlas=0|1: silhouettes in A8 atlas pages, one draw per page
    int touchSegments = 24;           // touch_segments=N: segments of the touch band's circles (48 before)
    int pakCacheKb = 0;        // pak_cache_kb=N: a block cache of N KB per pak stream (rw_stream_ctr.cpp); 0 = none (default)
    // Frame pacing (ctr_loop.h): pace=auto (0, the default: 60 Hz, 30 Hz while frames do not fit),
    // pace=60, pace=30. The simulation always steps once per vblank.
    int pace = 0;
    bool gpuKick = true;       // gpu_kick=0: no mid-frame start of the GPU (pica_frame.h kickGpu); default on
    bool cmdlistFlush = false; // end_flush=cmdlist: C3D_FrameEnd(GX_CMDLIST_FLUSH), no whole-heap flush (forced by gpu_kick=1)
    // Synchronisation diagnostics (docs/3ds-port.md "Frame synchronisation"):
    bool arenaPoison = false;  // arena_poison=1: garbage in every arena half / water half / freed block the CPU takes back
    int stateCheck = 0;        // state_check=1|2: the render-state self-check (state_check_pica.h); L marks the wash

    // The real-time front end instead of a direct mission start.
    bool menu() const { return mode == Mode::Menu || (mode == Mode::Play && !levelGiven); }
    bool usesLoop() const { return mode == Mode::Sim || mode == Mode::Bot || mode == Mode::Play || mode == Mode::Menu; }
    int effectiveFrames() const { return frames > 0 ? frames : (mode == Mode::Sim ? 600 : 0); }
};

const char* modeName(Mode m);

// Parses the file text; keys it does not know are skipped, bad values keep the default.
RunConfig parseRunConfig(const std::string& text);
// Reads `path`; an absent file gives the defaults.
RunConfig readRunConfig(const std::string& path);

} // namespace ctrapp
