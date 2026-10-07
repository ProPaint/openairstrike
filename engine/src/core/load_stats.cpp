// as3d/load_stats.h: process-wide atomic counters.
#include "as3d/load_stats.h"

#include <atomic>
#include <cstdio>

namespace as3d {

namespace {

std::atomic<long long> gCounts[kLoadKindCount];
std::atomic<long long> gBytes{0};
thread_local int tSiteDepth = 0;

} // namespace

const char* loadKindName(LoadKind kind) {
    switch (kind) {
        case LoadKind::Texture: return "textures";
        case LoadKind::Model: return "models";
        case LoadKind::Script: return "scripts";
        case LoadKind::Sound: return "sounds";
        case LoadKind::Shadow: return "shadows";
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

void LoadStats::count(LoadKind kind) {
    const int k = static_cast<int>(kind);
    if (k >= 0 && k < kLoadKindCount) gCounts[k].fetch_add(1, std::memory_order_relaxed);
}

void LoadStats::fileRead(size_t bytes) {
    gBytes.fetch_add(static_cast<long long>(bytes), std::memory_order_relaxed);
    if (tSiteDepth == 0) count(LoadKind::Other);
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
}

LoadStats::Site::Site() { ++tSiteDepth; }
LoadStats::Site::~Site() { --tSiteDepth; }

} // namespace as3d
