// as3d/load_stats.h: process-wide atomic counters.
#include "as3d/load_stats.h"

#include <atomic>
#include <cstdio>
#include <string>

#include "as3d/core.h"

namespace as3d {

namespace {

std::atomic<long long> gCounts[kLoadKindCount];
std::atomic<long long> gBytes{0};
thread_local int tSiteDepth = 0;
thread_local std::string tLastPath;
std::atomic<bool> gLogLazy{false};
std::atomic<bool> gArmed{false};     // a reset() happened since the log was turned on
std::atomic<int> gPrecacheDepth{0};
std::atomic<long> gFrame{0};

void logLoad(LoadKind kind, const char* path, const char* site) {
    if (!gLogLazy.load(std::memory_order_relaxed) || !gArmed.load(std::memory_order_relaxed) ||
        gPrecacheDepth.load(std::memory_order_relaxed) > 0)
        return;
    AS3D_INFO("AS3D_LAZY path=%s kind=%s site=%s frame=%ld", path && *path ? path : "?", loadKindName(kind),
              site && *site ? site : "?", gFrame.load(std::memory_order_relaxed));
}

} // namespace

const char* loadKindName(LoadKind kind) {
    switch (kind) {
        case LoadKind::Texture: return "textures";
        case LoadKind::Model: return "models";
        case LoadKind::Script: return "scripts";
        case LoadKind::Sound: return "sounds";
        case LoadKind::Shadow: return "shadows";
        case LoadKind::Stamp: return "stamps";
        case LoadKind::Other: return "other";
    }
    return "?";
}

long long LoadCounts::total() const {
    long long t = 0;
    for (long long c : count) t += c;
    return t;
}

std::string LoadCounts::str() const {
    std::string s;
    char buf[64];
    for (int k = 0; k < kLoadKindCount; ++k) {
        std::snprintf(buf, sizeof buf, "%s=%lld ", loadKindName(static_cast<LoadKind>(k)), count[k]);
        s += buf;
    }
    std::snprintf(buf, sizeof buf, "bytes=%lld", bytes);
    return s + buf;
}

LoadCounts LoadCounts::operator-(const LoadCounts& since) const {
    LoadCounts d;
    for (int k = 0; k < kLoadKindCount; ++k) d.count[k] = count[k] - since.count[k];
    d.bytes = bytes - since.bytes;
    return d;
}

void LoadStats::count(LoadKind kind, const char* path, const char* site) {
    const int k = static_cast<int>(kind);
    if (k >= 0 && k < kLoadKindCount) gCounts[k].fetch_add(1, std::memory_order_relaxed);
    if (path) tLastPath = path;
    logLoad(kind, path, site);
}

void LoadStats::fileRead(size_t bytes, const char* path) {
    gBytes.fetch_add(static_cast<long long>(bytes), std::memory_order_relaxed);
    if (tSiteDepth == 0) count(LoadKind::Other, path, "vfs");
}

LoadCounts LoadStats::lazyLoads() {
    LoadCounts c;
    for (int k = 0; k < kLoadKindCount; ++k) c.count[k] = gCounts[k].load(std::memory_order_relaxed);
    c.bytes = gBytes.load(std::memory_order_relaxed);
    return c;
}

void LoadStats::reset() {
    for (auto& c : gCounts) c.store(0, std::memory_order_relaxed);
    gBytes.store(0, std::memory_order_relaxed);
    if (gLogLazy.load(std::memory_order_relaxed)) gArmed.store(true, std::memory_order_relaxed);
}

void LoadStats::setLogLazy(bool on) {
    gLogLazy.store(on, std::memory_order_relaxed);
    if (!on) gArmed.store(false, std::memory_order_relaxed);
}
bool LoadStats::logLazy() { return gLogLazy.load(std::memory_order_relaxed); }
void LoadStats::setFrame(long frame) { gFrame.store(frame, std::memory_order_relaxed); }
long LoadStats::frame() { return gFrame.load(std::memory_order_relaxed); }
const char* LoadStats::lastPath() { return tLastPath.c_str(); }

LoadStats::PrecacheScope::PrecacheScope() { gPrecacheDepth.fetch_add(1, std::memory_order_relaxed); }
LoadStats::PrecacheScope::~PrecacheScope() { gPrecacheDepth.fetch_sub(1, std::memory_order_relaxed); }

LoadStats::LevelLoadScope::LevelLoadScope() : saved_(lazyLoads()) {}
LoadStats::LevelLoadScope::~LevelLoadScope() {
    for (int k = 0; k < kLoadKindCount; ++k) gCounts[k].store(saved_.count[k], std::memory_order_relaxed);
    gBytes.store(saved_.bytes, std::memory_order_relaxed);
}

LoadStats::Site::Site() { ++tSiteDepth; }
LoadStats::Site::~Site() { --tSiteDepth; }

} // namespace as3d
