// sdmc:/3ds/openairstrike/run.ini of the 3DS app: what to run. One "key=value" per line, '#'
// or ';' start a comment line, unknown keys are ignored (probe.cpp reads its own keys, hold= and
// only=, from the same file). A missing file gives the defaults: mode=spike, the feasibility
// spike. Documented in apps/3ds/README.md.
#pragma once

#include <string>
#include <vector>

#include "as3d/console_pad.h"

namespace ctrapp {

constexpr const char* kDataDir = "sdmc:/3ds/openairstrike"; // no trailing slash

enum class QualityMode { Auto, New, Old }; // run.ini quality=

enum class Mode {
    Spike, // the feasibility spike (main.cpp), the default
    Probe, // the M1.0 GPU probe (probe.cpp)
    Sim,   // the mission headless at full speed with the bot, no screens (as3d_sim --bot)
    Bot,   // real-time 60 Hz loop, bot input, screens through the frame hook
    Play,  // real-time loop with the pad; without level= the front end (as Menu)
    Menu,  // the front end (M2.1): menus on the bottom screen, missions started from them
};

struct RunConfig {
    Mode mode = Mode::Spike;
    std::string game = "as3d"; // game=KEY: a profile key of as3d/game_data.h; its paks live in <dir>/<key>/
    bool gameGiven = false;    // game=KEY is in the file (not absent, not game=select): no game selector
    int level = 1;             // level=N: the mission to start (1..the game's mission count)
    bool levelGiven = false;   // level= is in the file (mode=play without it runs the front end)
    int frames = 0;            // frames=N: simulation steps, then quit; 0 = 600 in sim, unlimited otherwise
    bool perf = false;         // perf=1: write perf.csv
    bool logInput = false;     // log_input=1: an AS3D_INPUT line per change of held bits / touch button
    as3d::PadBindings bindings = as3d::defaultBindings(); // bind_fire=A ... (see console_pad.h)
    int difficulty = -1;       // difficulty=N: -1 = the engine's default
    // The frame composer (ctr_frame.h):
    int dumpAt = 0;            // dump_at=N: at displayed frame N write top_left/top_right/bottom PPMs; 0 = never
    float brightness = 0.6f;   // brightness=F: config.ini Brightness (0.5 = neutral: no pass), default 0.6
    float slider = -1.0f;      // slider=F: force the 3D slider to F in [0, 1]; -1 = the hardware slider
    float eyeOffset = 0.0f;    // eye_offset=F: world units at slider 1; 0 = ctr_frame.h kMaxEyeOffset
    float minFocal = 0.0f;     // min_focal=F: smallest focal distance; 0 = ctr_frame.h kMinFocalDistance
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
    int dumpEvery = 0;         // dump_every=N: every N displayed frames, <prefix>f<frame>_{top,bottom}.ppm
    bool dumpScreens = false;  // dump_screens=1: 40 frames after each front-end screen change,
                               // <prefix><nn>_<screen>_{top,bottom}.ppm
    std::string dumpPrefix = "fe_"; // dump_prefix=: the file name prefix of dump_every / dump_screens

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
