#include "ctr_perf.h"

#include <3ds.h>
#include <malloc.h>

#include <algorithm>
#include <mutex>

#include "as3d/core.h"
#include "run_config.h"

namespace ctrapp {
namespace {

// The log file and its line buffer. Shared with the engine's log sink, which may be called from
// any thread.
std::mutex g_logMutex;
std::FILE* g_logFile = nullptr;
std::string g_logBuf;

void flushLogLocked() {
    if (g_logFile && !g_logBuf.empty()) {
        std::fwrite(g_logBuf.data(), 1, g_logBuf.size(), g_logFile);
        std::fflush(g_logFile);
        g_logBuf.clear();
    }
}

void logSink(as3d::LogLevel level, const char* line) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_logBuf += line;
    if (level >= as3d::LogLevel::Error || g_logBuf.size() > 16384) flushLogLocked();
}

double percentile(std::vector<float>& v, double p) {
    if (v.empty()) return 0.0;
    size_t k = static_cast<size_t>(p * (v.size() - 1));
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return v[k];
}

constexpr size_t kMaxSamples = 36000; // ten minutes at 60 Hz; the statistics beyond keep avg and max only

} // namespace

uint64_t nowTicks() { return svcGetSystemTick(); }
double ticksToMs(uint64_t ticks) { return static_cast<double>(ticks) * 1000.0 / SYSCLOCK_ARM11; }

Perf::~Perf() { close(); }

void Perf::open(bool csv) {
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        if (!g_logFile) g_logFile = std::fopen((std::string(kDataDir) + "/log.txt").c_str(), "w");
    }
    as3d::setLogSink(logSink);
    as3d::setLogLevel(as3d::LogLevel::Info);
    if (csv) {
        csv_ = std::fopen((std::string(kDataDir) + "/perf.csv").c_str(), "w");
        if (csv_) {
            std::fputs("frame,step_ms,prepare_ms,cpu_ms,gpu_ms,eyes,verts,draws,tex_vram_kb,tex_linear_kb,"
                       "linear_free_kb,lazy_loads\n",
                       csv_);
        } else {
            AS3D_WARN("cannot open perf.csv");
        }
    }
    steps_.reserve(4096);
    periods_.reserve(4096);
    lastFlush_ = nowTicks();
}

void Perf::record(const FrameSample& s) {
    const size_t linearFree = linearSpaceFree();
    linearFreeMin_ = std::min(linearFreeMin_, linearFree);
    if ((s.frame & 31) == 0) {
        struct mallinfo mi = mallinfo();
        heapHigh_ = std::max(heapHigh_, static_cast<size_t>(mi.uordblks));
        heapArenaHigh_ = std::max(heapArenaHigh_, static_cast<size_t>(mi.arena));
    }
    stepSum_ += s.stepMs;
    stepMax_ = std::max<double>(stepMax_, s.stepMs);
    if (steps_.size() < kMaxSamples) steps_.push_back(s.stepMs);
    if (s.periodMs > 0.0f) {
        periodSum_ += s.periodMs;
        periodMax_ = std::max<double>(periodMax_, s.periodMs);
        ++periodCount_;
        if (s.periodMs > kOverMs) ++over_;
        if (periods_.size() < kMaxSamples) periods_.push_back(s.periodMs);
    }
    lazyMax_ = std::max(lazyMax_, s.stats.lazyLoads);
    if (csv_) {
        std::fprintf(csv_, "%u,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%u,%u,%u,%u\n", s.frame, s.stepMs, s.stats.prepareMs,
                     s.cpuMs, s.gpuMs, s.stats.eyes, s.stats.verts, s.stats.draws, s.stats.texVramKb,
                     s.stats.texLinearKb, static_cast<unsigned>(linearFree / 1024), s.stats.lazyLoads);
    }
    service();
}

void Perf::service() {
    const uint64_t now = nowTicks();
    if (now - lastFlush_ < SYSCLOCK_ARM11) return;
    flush();
}

void Perf::flush() {
    lastFlush_ = nowTicks();
    if (csv_) std::fflush(csv_);
    std::lock_guard<std::mutex> lock(g_logMutex);
    flushLogLocked();
}

void Perf::writeSummary(unsigned frames, unsigned steps, unsigned dropped) {
    const double n = frames ? static_cast<double>(frames) : 1.0;
    const double stepP95 = percentile(steps_, 0.95);
    const double periodP95 = percentile(periods_, 0.95);
    const double periodAvg = periodCount_ ? periodSum_ / periodCount_ : 0.0;
    struct mallinfo mi = mallinfo();
    heapHigh_ = std::max(heapHigh_, static_cast<size_t>(mi.uordblks));
    heapArenaHigh_ = std::max(heapArenaHigh_, static_cast<size_t>(mi.arena));
    AS3D_INFO("PERF_SUMMARY frames=%u steps=%u dropped=%u", frames, steps, dropped);
    AS3D_INFO("PERF_SUMMARY step_ms avg=%.3f p95=%.3f max=%.3f", stepSum_ / n, stepP95, stepMax_);
    AS3D_INFO("PERF_SUMMARY frame_ms avg=%.3f p95=%.3f max=%.3f", periodAvg, periodP95, periodMax_);
    AS3D_INFO("PERF_SUMMARY over_ms=%.1f over=%u of=%u", kOverMs, over_, periodCount_);
    AS3D_INFO("PERF_SUMMARY load_ms=%.0f", loadMs_);
    AS3D_INFO("PERF_SUMMARY heap_kb high=%zu arena=%zu", heapHigh_ / 1024, heapArenaHigh_ / 1024);
    AS3D_INFO("PERF_SUMMARY linear_free_kb now=%zu min=%zu", static_cast<size_t>(linearSpaceFree()) / 1024,
              (linearFreeMin_ == SIZE_MAX ? 0 : linearFreeMin_) / 1024);
    AS3D_INFO("PERF_SUMMARY lazy_loads max=%u", lazyMax_);
    flush();
}

void Perf::close() {
    flush();
    if (csv_) {
        std::fclose(csv_);
        csv_ = nullptr;
    }
    as3d::setLogSink(nullptr);
    std::lock_guard<std::mutex> lock(g_logMutex);
    flushLogLocked();
    if (g_logFile) {
        std::fclose(g_logFile);
        g_logFile = nullptr;
    }
}

} // namespace ctrapp
