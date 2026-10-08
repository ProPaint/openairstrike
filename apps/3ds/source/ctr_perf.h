// The 3DS app's log and performance record: sdmc:/3ds/openairstrike/log.txt (every AS3D_INFO,
// WARN and ERROR of the engine goes there through as3d::setLogSink, buffered and flushed once a
// second) and, with perf=1, perf.csv with one row per displayed frame:
//
//   frame,step_ms,prepare_ms,cpu_ms,gpu_ms,eyes,verts,draws,tex_vram_kb,tex_linear_kb,
//   linear_free_kb,lazy_loads,arena_kb,sim_steps
//
// The frame composer (ctr_frame.h) fills the renderer columns through FrameStats: prepare_ms the
// CPU time of its prepare(), verts/draws the GPU work of both screens, the texture bytes of
// pica::textureStats, lazy_loads the first-use loads since the precache (LoadStats), arena_kb the
// frame arena bytes this frame used. The summary block written at
// the end (PERF_SUMMARY lines in log.txt) is read by tools/3ds_perf_report.py.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ctrapp {

// What a frame hook reports about its last frame (all zero for the colour-cycling hook).
struct FrameStats {
    float prepareMs = 0.0f;
    int eyes = 0, verts = 0, draws = 0;
    unsigned texVramKb = 0, texLinearKb = 0;
    unsigned lazyLoads = 0;
    unsigned arenaKb = 0;  // frame arena bytes used by this frame
};

struct FrameSample {
    unsigned frame = 0;
    float stepMs = 0.0f;   // all simulation steps of this frame
    int simSteps = 1;      // how many simulation steps ran before this frame was drawn (2 at 30 Hz render)
    float cpuMs = 0.0f;    // C3D_GetProcessingTime
    float gpuMs = 0.0f;    // C3D_GetDrawingTime
    float periodMs = 0.0f; // time since the previous frame (0 for the first)
    FrameStats stats;
};

// Wall clock of the app: the ARM11 system tick (268.111856 MHz).
uint64_t nowTicks();
double ticksToMs(uint64_t ticks);

class Perf {
public:
    ~Perf();
    // Opens log.txt (truncated) and, with `csv`, perf.csv; routes the engine log into log.txt.
    void open(bool csv);
    // Appends the row and keeps the statistics. The log buffer is flushed once a second.
    void record(const FrameSample& s);
    void service(); // flush the log if a second has passed
    void flush();   // flush now (suspend, exit)
    void setLoadMs(double ms) { loadMs_ = ms; }
    // The summary block (log.txt) and a last flush.
    void writeSummary(unsigned frames, unsigned steps, unsigned dropped);
    void close();

private:
    bool csvOpen_ = false;
    std::FILE* csv_ = nullptr;
    std::vector<float> steps_, periods_;
    double stepSum_ = 0, periodSum_ = 0, stepMax_ = 0, periodMax_ = 0, loadMs_ = 0;
    unsigned over_ = 0, periodCount_ = 0, lazyMax_ = 0, arenaMax_ = 0;
    size_t heapHigh_ = 0, heapArenaHigh_ = 0, linearFreeMin_ = SIZE_MAX;
    uint64_t lastFlush_ = 0;
};

// A frame period longer than this counts as a missed vblank (16.7 ms plus the hardware's
// 0.04 ms jitter and measurement noise).
constexpr double kOverMs = 17.0;

} // namespace ctrapp
