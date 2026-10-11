// The per-pass GPU timing (gpu_passes_pica.h).
#include "gpu_passes_pica.h"

#include <3ds.h>
#include <citro3d.h>

#include <cstdio>

#include "pica_frame.h"
#include "state_check_pica.h"

// citro3d's context: its first member is the GX queue (pica_frame.cpp).
extern "C" gxCmdQueue_s __C3D_Context;

namespace as3d::pica {
namespace {

// A register no pass uses (lighting is off; the state check's canary register): the one word of the
// sync list.
constexpr u32 kSyncReg = 0x0140; // GPUREG_LIGHT0_SPECULAR0

struct Timing {
    int every = 0;
    unsigned frames = 0;      // frameBegin calls since setGpuPassEvery
    bool timed = false;       // this frame
    bool lastTimed = false;   // the frame before (its tail is read at this frameBegin)
    int screen = 0;
    unsigned timedFrames = 0; // since the last report
    unsigned tails = 0;
    u64 ticks[kGpuScreens][kGpuSegs] = {};
    unsigned marks[kGpuScreens][kGpuSegs] = {};
    u64 syncTicks = 0;
    unsigned syncs = 0;
    double tailMs = 0.0;
};

Timing& tm() {
    static Timing t;
    return t;
}

// The commands recorded since the last boundary to the GPU, and the wait for the queue to drain:
// the ticks from the submission to the drain (0 when nothing was recorded).
u64 submitAndWait() {
    u32* buf = nullptr;
    u32 size = 0, offset = 0;
    GPUCMD_GetBuffer(&buf, &size, &offset);
    if (!buf) return 0; // outside a citro3d frame
    // The arena's new vertices flushed first (CPU work outside the timing; splitCommandList then
    // has only the late uploads' flushes, rare, and the split itself).
    frameArena().flushPending();
    const u64 t0 = svcGetSystemTick();
    splitCommandList();
    gxCmdQueueRun(&__C3D_Context);
    gxCmdQueueWait(&__C3D_Context, -1);
    const u64 ticks = svcGetSystemTick() - t0;
    // Every entry so far completed: the queue's entries are counted again from 0, so a timed frame's
    // extra lists never reach the queue's 32 entries per frame (one more is an svcBreak; C3D_Init).
    gxCmdQueueClear(&__C3D_Context);
    return ticks;
}

const char* segName(int s) {
    if (s <= static_cast<int>(Pass::Bottom2d)) return passName(static_cast<Pass>(s));
    switch (static_cast<GpuExtra>(s - static_cast<int>(Pass::Bottom2d) - 1)) {
    case GpuExtra::Wash: return "wash";
    case GpuExtra::Other: return "other";
    default: return "?";
    }
}

void mark(int seg) {
    Timing& t = tm();
    if (!t.timed || seg < 0 || seg >= kGpuSegs) return;
    const u64 ticks = submitAndWait();
    t.ticks[t.screen][seg] += ticks;
    ++t.marks[t.screen][seg];
}

} // namespace

void setGpuPassEvery(int frames) {
    Timing& t = tm();
    t = Timing();
    t.every = frames > 0 ? frames : 0;
}

int gpuPassEvery() { return tm().every; }

void gpuPassFrameBegin() {
    Timing& t = tm();
    if (t.lastTimed) {
        // C3D_FrameBegin waited for the last frame's queue: its drawing time is the tail after
        // C3D_FrameEnd, i.e. the display transfers (every pass was waited for before it).
        t.tailMs += static_cast<double>(C3D_GetDrawingTime());
        ++t.tails;
    }
    t.lastTimed = false;
    t.timed = false;
    t.screen = 0;
    if (t.every <= 0) return;
    ++t.frames;
    // Every N-th frame, from the N/2-th (away from the state check's probe frames, multiples of its
    // period), and never a frame whose probes split the command list themselves.
    if ((t.frames + static_cast<unsigned>(t.every / 2)) % static_cast<unsigned>(t.every) != 0) return;
    if (stateCheckSplitsThisFrame()) return;
    t.timed = true;
    t.lastTimed = true;
    ++t.timedFrames;
    // The fixed cost of a boundary: a list of one word, submitted and waited for.
    GPUCMD_AddWrite(kSyncReg, 0);
    t.syncTicks += submitAndWait();
    ++t.syncs;
}

bool gpuPassTimedFrame() { return tm().timed; }

void gpuPassScreen(int screen) {
    Timing& t = tm();
    if (screen >= 0 && screen < kGpuScreens) t.screen = screen;
}

void gpuPassMark(Pass p) {
    const int s = static_cast<int>(p);
    if (s <= static_cast<int>(Pass::Bottom2d)) mark(s);
}

void gpuPassMark(GpuExtra e) { mark(static_cast<int>(Pass::Bottom2d) + 1 + static_cast<int>(e)); }

std::string gpuPassReport() {
    Timing& t = tm();
    if (t.timedFrames == 0) return std::string();
    const double k = static_cast<double>(SYSCLOCK_ARM11) / 1000.0;
    const double n = static_cast<double>(t.timedFrames);
    const double sync = t.syncs ? static_cast<double>(t.syncTicks) / k / t.syncs : 0.0;
    char buf[64];
    std::snprintf(buf, sizeof buf, "timed=%u every=%d sync=%.3f", t.timedFrames, t.every, sync);
    std::string out = buf;
    double sum = 0.0;
    const char* screenName[kGpuScreens] = {"L:", "R:", "B:"};
    for (int sc = 0; sc < kGpuScreens; ++sc) {
        std::string part;
        double screenSum = 0.0;
        for (int s = 0; s < kGpuSegs; ++s) {
            if (!t.marks[sc][s]) continue;
            // Per timed frame, less the fixed cost of each boundary.
            double ms = (static_cast<double>(t.ticks[sc][s]) / k - sync * t.marks[sc][s]) / n;
            if (ms < 0.0) ms = 0.0;
            screenSum += ms;
            std::snprintf(buf, sizeof buf, " %s=%.2f", segName(s), ms);
            part += buf;
        }
        if (part.empty()) continue;
        sum += screenSum;
        std::snprintf(buf, sizeof buf, " (%.2f)", screenSum);
        out += std::string(" ") + screenName[sc] + part + buf;
    }
    const double tail = t.tails ? t.tailMs / t.tails : 0.0;
    sum += tail;
    std::snprintf(buf, sizeof buf, " tail=%.2f sum=%.2f", tail, sum);
    out += buf;
    const int every = t.every;
    const unsigned frames = t.frames;
    const bool lastTimed = t.lastTimed, timed = t.timed;
    const int screen = t.screen;
    t = Timing();
    t.every = every;
    t.frames = frames;
    t.lastTimed = lastTimed;
    t.timed = timed;
    t.screen = screen;
    return out;
}

} // namespace as3d::pica
