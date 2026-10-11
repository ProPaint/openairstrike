// The 3DS main loop (M1.2): aptMainLoop, the fixed 60 Hz simulation step with at most two
// catch-up steps per displayed frame, APT hooks (HOME / sleep: pause, save, resume), the pad, the
// quit chord START+SELECT, the performance record (ctr_perf.h) and the frame hook the frame
// composer (M1.8) fills.
//
// One iteration, in real-time modes (bot, play):
//   aptMainLoop -> hidScanInput -> quit chord? -> clock: acc += min(0.25 s, elapsed)
//   -> up to 2 steps of GameSession::step (bot input or pad) -> hook.prepare
//   -> ctx.beginFrame (waits for the vblank) -> hook.drawTop(eye) per eye -> hook.drawBottom
//   -> ctx.endFrame -> hook.stats -> hook.afterFrame -> perf row.
// Before the loop hook.begin (the level start: renderer, precache, loading screen), after it
// hook.end (while the graphics context still exists).
// Mode sim runs the same steps back to back with no screens and no clock.
#pragma once

#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include "as3d/input.h"
#include "as3d/platform.h"
#include "ctr_perf.h"
#include "game_session.h"
#include "catch_up.h"
#include "pace_rule.h"
#include "run_config.h"

namespace as3d {
namespace ctr {
class CtrContext;
}
} // namespace as3d

namespace ctrapp {

class GameFrame; // ctr_frame.h
class CtrInput;  // ctr_input.h

// What the hook is told about the frame.
struct FrameInfo {
    unsigned frame = 0;       // displayed frames so far
    unsigned step = 0;        // simulation steps so far
    int eyes = 1;             // top-screen eyes to draw: 2 with the 3D slider up, else 1
    float slider = 0.0f;      // osGet3DSliderState, or run.ini slider= when given
    bool paused = false;      // the world is paused (suspend, in-game pause)
    as3d_game::GameSession* session = nullptr;
    const as3d::TouchMapper* touch = nullptr; // the bottom-screen buttons (ctr_input.h), for drawing
    float lastCpuMs = 0.0f, lastGpuMs = 0.0f; // of the previous frame (the perf line)
    // The front end (menu mode): the screen shown (AS3D_SCREEN names: intro, a menu's name,
    // playing, paused; empty in the direct modes) and whether the gameplay controls are live
    // (GameFlow::playing: the touch buttons are drawn).
    const char* screen = "";
    bool playing = true;
    uint32_t padDown = 0; // pad:: bits pressed this frame (run.ini state_check: L marks the water wash)
};

// The frame composer's slot (ctr_frame.h). The loop brackets the frame (beginFrame / endFrame)
// and calls these in order; the hook binds the targets itself (pica::drawOn on ctx.topTarget(eye)
// and ctx.bottomTarget()). Required in the real-time modes (bot, play).
class FrameHook {
public:
    virtual ~FrameHook() = default;
    // Once, after the mission loaded and before the first step: the renderer's level start
    // (precache, loading screen). False: the app stops.
    virtual bool begin(as3d::ctr::CtrContext&, as3d_game::GameSession&) { return true; }
    // Once per displayed frame, after the steps and before the frame opens (CPU work: culling,
    // entity lists).
    virtual void prepare(const FrameInfo&) {}
    virtual void drawTop(as3d::ctr::CtrContext& ctx, int eye, const FrameInfo&) = 0;
    virtual void drawBottom(as3d::ctr::CtrContext& ctx, const FrameInfo&) = 0;
    // The renderer columns of perf.csv for the frame just drawn.
    virtual void stats(FrameStats&) {}
    // After ctx.endFrame (the frame is submitted): read-backs (run.ini dump_at).
    virtual void afterFrame(as3d::ctr::CtrContext&, const FrameInfo&) {}
    // Once at the end, before the session and the graphics context go: release GPU resources.
    virtual void end() {}
};

// What runCtrLoop measured, for mode=soak (ctr_soak.h). The pacer's counts are those of
// paceStats() at the end.
struct PaceStats {
    int hz = 60;              // the rate at the end
    unsigned changes = 0;
    unsigned frames60 = 0, misses60 = 0, excused60 = 0, frames30 = 0, misses30 = 0;
    unsigned graceFrames = 0; // frames after the pacer's grace (300 after a start or a load, 60 after an eye change)
    unsigned graceAt60 = 0, graceAt30 = 0; // of those, drawn at 60 / 30 Hz
    unsigned probeExcused = 0; // misses excused because a probe_every GPU read-back stalled the frame
    // R4.2: frames at 60 Hz after the grace whose sync was on time but whose GPU (wait + submit +
    // end + the GPU tail) ended after the next vblank, so the picture came a vblank late; and the
    // pictures that stayed on screen longer than the rate (a late sync or a late GPU: what the eye
    // sees as a stutter; a run of late GPU frames is one stutter, then a steady delay).
    unsigned gpuLate60 = 0, hitches60 = 0;
    // The catch-up (catch_up.h): frames that ended with steps owed, owed steps run because the
    // frame had room / because the lag bound was reached, steps dropped, the most owed.
    unsigned behindFrames = 0, repaid = 0, forced = 0, dropped = 0;
    int lagMax = 0;
};
PaceStats paceStats();

struct LoopReport {
    bool started = false;  // the mission loaded and the loop ran
    bool finished = false; // it reached its `frames` steps
    bool levelEnded = false;  // run.stopAtLevelEnd: Mission Complete or Game Over was reached (then the margin)
    bool gameOver = false;    // ... and it was Game Over
    unsigned levelEndStep = 0; // the step the level ended at
    bool quit = false;     // START+SELECT, or the system closed the app
    unsigned frames = 0, steps = 0, dropped = 0;
    PaceStats pace;
    double gpuAvg = 0.0, gpuP95 = 0.0;
    long long lazyLoads = 0;  // first-use loads after the precache, TerraMorph stamps excepted
    long long lazyStamps = 0; // the sequels' TerraMorph stamps (not precachable, as3d/load_stats.h)
};

struct CtrLoopOptions {
    RunConfig run;
    FrameHook* hook = nullptr;          // required in bot and play (ctr_frame.h GameFrame)
    // Called when the app is suspended (HOME, sleep) and once at exit, with the world paused.
    // The menu mode sets it to the flow's profile save itself (GameFlow::onBackground on
    // suspend: the in-game menu opens during play, then GameFlow::saveNow; saveNow at exit).
    std::function<void()> saveHook;
    // The menu mode's view (ctr_frame.h GameFrame::makeFlowView, setFlow, loadingScreen).
    GameFrame* frame = nullptr;
    // The game selector's front end (ctr_selector.cpp): the graphics context and the input it
    // already made (borrowed; null = the loop makes its own), whether the main menu offers
    // "Change game", and, as an output, whether the loop ended because it was chosen.
    as3d::GraphicsContext* context = nullptr;
    CtrInput* input = nullptr;
    bool changeGameOffered = false;
    mutable bool changeGameRequested = false;
    // runCtrLoop (bot, play, sim) fills it at the end when set (mode=soak).
    LoopReport* report = nullptr;
};

// Runs until the mission's `frames` steps are done, the user quits (START+SELECT) or the system
// closes the app. Borrows options.context and options.input when set (mode=soak runs one loop per
// stage on the same screens). Returns the process exit code (0, or 1 when the game data cannot be loaded).
// With run.menu() it runs the front end instead (runCtrMenuLoop, below).
int runCtrLoop(const CtrLoopOptions& options);

// The front end (M2.1): GameStack with GameFlow over the session, the frame composer as its
// view (menus on the bottom screen, the attract level or the mission on the top one), the pad
// and the touch screen as the front end's input. Missions start and end through the menus;
// `frames` (when not 0) ends the run after that many displayed frames.
int runCtrMenuLoop(const CtrLoopOptions& options); // ctr_menu_loop.cpp

// Frame pacing from run.ini (pace=, gpu_kick=, end_flush=, pak_cache_kb=); both loops call it
// once, then startPacing with the quality profile's render rate once the screens exist.
//
// The clock is the vblank (docs/3ds-port.md "Frame cadence"): drawFrame waits for the next tick of
// citro3d's frame counter (C3D_FrameSync: every vblank at 60 Hz, every other one at 30 Hz) and
// takes its timestamp; FrameClock::due() turns the time between the last two of them into whole
// vblanks, the simulation steps due (60 Hz steps, one per vblank), and the catch-up rule
// (catch_up.h, run.ini catchup=: deferred by default since R4.2) decides how many of them the
// next frame runs: its own (1 at 60 Hz, 2 at 30) and an owed one only when the frame has room,
// at most catchup_lag behind, at most maxCatchUp per frame (catchup=immediate: all of them up to
// maxCatchUp, the rest dropped). So a frame never carries 0 steps and a steady rate never
// mixes 0- and 2-step frames (the tick accumulator did against the 59.83 Hz panel).
// pace=auto (default with the New 3DS profile): 60 Hz, dropping to 30 Hz (two steps per frame)
// after 3 misses in 120 frames and back after 240 frames that would fit (AS3D_PACE lines);
// pace=60 / pace=30 fix the rate.
struct FramePacing {
    bool gpuKick = true;
    bool cmdlistFlush = true;
    int pace = 0; // 0 auto, 60, 30
    PaceRule rule; // pace=auto's drop rule (run.ini pace_rule=, pace_rule.h)
    CatchUpRule catchUp; // the steps owed after a late frame (run.ini catchup=, catch_up.h)
};
void setFramePacing(const RunConfig& run);
const FramePacing& framePacing();
void startPacing(int profileHz);
int renderHz();
int maxCatchUp();
unsigned paceChanges();
void logPaceSummary(); // PERF_SUMMARY pace: changes, misses at 60 / 30 Hz, excused ones

class FrameClock {
public:
    // The next two frames carry one tick of steps (start, a level load, resume from HOME).
    void reset();
    // The simulation steps the next frame runs (>= 1, the catch-up rule of catch_up.h); steps
    // never run (over maxCatchUp() with catchup=immediate, beyond the lag bound with deferred) are
    // added to *dropped.
    int due(unsigned* dropped);
    int lastInterval() const; // vblanks between the last two frames, uncapped
};

// The read plan of a level start (docs/3ds-port.md "Load: reads"): on the console each read of
// the SD card costs about 5 ms whatever its size, and a level start reads 600 files. begin()
// loads sdmc:/3ds/openairstrike/cache/<key>.plan (the paths the same level start read last time,
// file names only) and prefetches them (as3d::Vfs::prefetch: a few large reads per pak), then
// records every path read until end(), which frees what was not used and stores the plan when it
// changed. The data read is the same with or without a plan. run.ini prefetch=0 turns it off.
class ReadPlan {
public:
    void begin(as3d::Vfs& vfs, const std::string& key, bool enabled);
    void end();
    bool active() const { return vfs_ != nullptr; }
    double prefetchMs() const { return prefetchMs_; } // the last begin()'s prefetch (AS3D_LEVEL_START)

private:
    double prefetchMs_ = 0.0;
    as3d::Vfs* vfs_ = nullptr;
    std::string key_;
    std::vector<std::string> loaded_, recorded_;
    std::unordered_set<std::string> seen_, planned_;
    uint64_t start_ = 0;
};

// One displayed frame through the hook: prepare, the frame (beginFrame, drawTop per eye,
// drawBottom, endFrame), stats, afterFrame; fills the sample's cpu/gpu and split columns
// (wait, submit, end, after) and its stats. Shared by both loops.
void drawFrame(as3d::ctr::CtrContext& ctx, FrameHook& hook, const FrameInfo& info, FrameSample& sample);

// The end-state lines of as3d_sim (map_pos, entities, created / freed, ...) for `level`.
void logEndState(const as3d_game::GameSession& session, int level, unsigned steps);

} // namespace ctrapp
