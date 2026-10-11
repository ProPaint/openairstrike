// The 3DS app's log and performance record: sdmc:/3ds/openairstrike/log.txt (every AS3D_INFO,
// WARN and ERROR of the engine goes there through as3d::setLogSink, buffered and flushed once a
// second) and, with perf=1, perf.csv with one row per displayed frame:
//
//   frame,step_ms,prepare_ms,cpu_ms,gpu_ms,eyes,verts,draws,tex_vram_kb,tex_linear_kb,
//   linear_free_kb,lazy_loads,arena_kb,sim_steps,submit_ms,wait_ms,end_ms,after_ms,sync_ms,render_hz,stage
//
// (stage: the soak's stage number, 0 outside mode=soak; a soak's stages append to one perf.csv,
// the frame column restarting at each stage.)
// The frame's CPU, in order (ctr_loop.cpp): step_ms (the simulation steps), prepare_ms (the
// composer's prepare: scene lists, mesh passes, outside the frame), wait_ms (C3D_FrameBegin:
// the vblank sync and the wait for the GPU to finish the previous frame), submit_ms (drawTop per
// eye and drawBottom: the command list, 2D lists built inside the frame), sync_ms (the vblank
// wait before the frame, C3D_FrameSync), end_ms (C3D_FrameEnd:
// the cache flush and the queue start), after_ms (afterFrame: screen dumps). cpu_ms is citro3d's
// processing time (FrameBegin to FrameEnd, about submit_ms); gpu_ms the GPU time from
// FrameEnd to the end of the queue.
//
// The log and the rows are kept in memory and written by a low-priority writer thread (about once
// a second, while the main thread waits): no SD write on the main thread during play.
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
    uint64_t gpuStartTick = 0; // when the GPU got the frame's first command list (gpu_kick), 0: at C3D_FrameEnd
};

struct FrameSample {
    unsigned frame = 0;
    float stepMs = 0.0f;   // all simulation steps of this frame
    int simSteps = 1;      // how many simulation steps ran before this frame was drawn (2 at 30 Hz render)
    float cpuMs = 0.0f;    // C3D_GetProcessingTime
    float gpuMs = 0.0f;    // C3D_GetDrawingTime
    float periodMs = 0.0f; // time since the previous frame (0 for the first)
    float waitMs = 0.0f;   // in C3D_FrameBegin
    float submitMs = 0.0f; // between C3D_FrameBegin and C3D_FrameEnd
    float syncMs = 0.0f;   // the vblank wait before the frame (C3D_FrameSync)
    int renderHz = 60;     // the pacing's rate when the frame was drawn (pace=auto)
    uint64_t endTick = 0;  // when C3D_FrameEnd was called
    float endMs = 0.0f;    // in C3D_FrameEnd
    float afterMs = 0.0f;  // the hook's afterFrame (dumps)
    int lateTextures = 0;  // late textures built after C3D_FrameEnd (pica drainLateTextures)
    float lateMs = 0.0f;   // their time
    FrameStats stats;
};

// True once after Perf wrote log.txt / perf.csv to the SD card (the pacer excuses the frame
// that waited for it); reading clears it.
bool takePerfIoFlag();
// The writer thread finished a write at or after `tick` (minus one vblank): the pacer excuses
// that frame's miss.
bool perfWriterWroteSince(uint64_t tick);

// A file the writer thread writes (as3d::writeFileAtomic: temporary file, fsync, rename) instead
// of the caller: the level start's caches (the shadow silhouettes, the read plan), whose first
// write cost 2.1 s on the console. Taken in order; false when there is no writer thread (Perf is
// not open: the caller writes the file itself). Perf::close (and flush) write what is queued.
bool queueFileWrite(const std::string& path, std::vector<uint8_t>&& bytes);
// Files queued and not written yet (tests, logs).
size_t pendingFileWrites();

// The files of the data directory this launch wrote that the log push sends besides log.txt,
// perf.csv and soak.txt (the mark dumps; ctr_report.h). Names, no directory.
void noteRunFile(const std::string& name);
std::vector<std::string> runFiles();

// Wall clock of the app: the ARM11 system tick (268.111856 MHz).
uint64_t nowTicks();
double ticksToMs(uint64_t ticks);

class Perf {
public:
    ~Perf();
    // Opens log.txt (truncated) and, with `csv`, perf.csv; routes the engine log into log.txt.
    // A later open in the same launch appends to both files (perf.csv's header is written once).
    void open(bool csv);
    void setStage(int stage) { stage_ = stage; } // perf.csv's stage column (mode=soak)
    // The GPU time of the frames recorded so far (gpu_ms): average and 95th percentile.
    void gpuStats(double* avg, double* p95);
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
    int stage_ = 0;
    std::FILE* csv_ = nullptr;
    std::vector<float> steps_, periods_;
    // Frame CPU split (frames with a period only): prepare, wait, submit, end, after; cpu, gpu.
    double splitSum_[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    double afterMax_ = 0;
    unsigned stepHist_[4] = {0, 0, 0, 0}; // frames with 0, 1, 2, 3+ simulation steps
    unsigned framesAt30_ = 0;
    unsigned splitCount_ = 0;
    std::vector<float> gpus_;
    double stepSum_ = 0, periodSum_ = 0, stepMax_ = 0, periodMax_ = 0, loadMs_ = 0;
    unsigned over_ = 0, periodCount_ = 0, lazyMax_ = 0, arenaMax_ = 0;
    size_t heapHigh_ = 0, heapArenaHigh_ = 0, linearFreeMin_ = SIZE_MAX;
    uint64_t lastFlush_ = 0;
};

// A frame period longer than this counts as a missed vblank (16.7 ms plus the hardware's
// 0.04 ms jitter and measurement noise).
constexpr double kOverMs = 17.0;

} // namespace ctrapp
