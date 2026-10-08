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

#include "as3d/input.h"
#include "as3d/platform.h"
#include "ctr_perf.h"
#include "game_session.h"
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
};

// Runs until the mission's `frames` steps are done, the user quits (START+SELECT) or the system
// closes the app. Returns the process exit code (0, or 1 when the game data cannot be loaded).
// With run.menu() it runs the front end instead (runCtrMenuLoop, below).
int runCtrLoop(const CtrLoopOptions& options);

// The front end (M2.1): GameStack with GameFlow over the session, the frame composer as its
// view (menus on the bottom screen, the attract level or the mission on the top one), the pad
// and the touch screen as the front end's input. Missions start and end through the menus;
// `frames` (when not 0) ends the run after that many displayed frames.
int runCtrMenuLoop(const CtrLoopOptions& options); // ctr_menu_loop.cpp

// The end-state lines of as3d_sim (map_pos, entities, created / freed, ...) for `level`.
void logEndState(const as3d_game::GameSession& session, int level, unsigned steps);

} // namespace ctrapp
