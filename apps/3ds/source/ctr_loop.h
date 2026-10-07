// The 3DS main loop (M1.2): aptMainLoop, the fixed 60 Hz simulation step with at most two
// catch-up steps per displayed frame, APT hooks (HOME / sleep: pause, save, resume), the pad, the
// quit chord START+SELECT, the performance record (ctr_perf.h) and the frame hook the frame
// composer (M1.8) fills.
//
// One iteration, in real-time modes (bot, play):
//   aptMainLoop -> hidScanInput -> quit chord? -> clock: acc += min(0.25 s, elapsed)
//   -> up to 2 steps of GameSession::step (bot input or pad) -> hook.prepare
//   -> ctx.beginFrame (waits for the vblank) -> hook.drawTop(eye) per eye -> hook.drawBottom
//   -> ctx.endFrame -> perf row.
// Mode sim runs the same steps back to back with no screens and no clock.
#pragma once

#include <functional>

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

// What the hook is told about the frame.
struct FrameInfo {
    unsigned frame = 0;       // displayed frames so far
    unsigned step = 0;        // simulation steps so far
    int eyes = 1;             // top-screen eyes to draw: 2 with the 3D slider up, else 1
    float slider = 0.0f;      // osGet3DSliderState
    bool paused = false;      // the world is paused (suspend, in-game pause)
    as3d_game::GameSession* session = nullptr;
};

// The frame composer's slot. The loop brackets the frame (beginFrame / endFrame) and calls these
// in order; the hook binds the targets itself (C3D_FrameDrawOn on ctx.topTarget(eye) and
// ctx.bottomTarget()). Without a hook the loop clears the screens to a colour that changes with
// the frame number.
class FrameHook {
public:
    virtual ~FrameHook() = default;
    // Once per displayed frame, after the steps and before the frame opens (CPU work: culling,
    // entity lists).
    virtual void prepare(const FrameInfo&) {}
    virtual void drawTop(as3d::ctr::CtrContext& ctx, int eye, const FrameInfo&) = 0;
    virtual void drawBottom(as3d::ctr::CtrContext& ctx, const FrameInfo&) = 0;
    // The renderer columns of perf.csv for the frame just drawn.
    virtual void stats(FrameStats&) {}
};

struct CtrLoopOptions {
    RunConfig run;
    FrameHook* hook = nullptr;          // null: the colour-cycling clear
    // Called when the app is suspended (HOME, sleep) and once at exit, with the world paused:
    // the flow's profile save (GameFlow::saveNow) once the front end runs on the 3DS (M2).
    std::function<void()> saveHook;
};

// Runs until the mission's `frames` steps are done, the user quits (START+SELECT) or the system
// closes the app. Returns the process exit code (0, or 1 when the game data cannot be loaded).
int runCtrLoop(const CtrLoopOptions& options);

} // namespace ctrapp
