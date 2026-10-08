// The per-pass profile and pass switches (pass_profile.h).
#include "pass_profile.h"

#include <cstdio>

namespace as3d::pica {
namespace {

constexpr const char* kNames[kPassCount] = {
    "clear", "terrain", "marks",  "shadows", "opaque", "water", "rest",     "lightning", "particles", "sprites",
    "brightness", "top2d", "model", "hud", "hint", "touch", "bottom2d", "ui_batch", "ui_copy",
};

uint32_t g_mask = kAllPasses;

constexpr uint32_t bit(Pass p) { return 1u << static_cast<int>(p); }

constexpr uint32_t kWorldPasses = bit(Pass::Clear) | bit(Pass::Terrain) | bit(Pass::Marks) | bit(Pass::Shadows) |
                                  bit(Pass::Opaque) | bit(Pass::Water) | bit(Pass::Rest) | bit(Pass::Lightning) |
                                  bit(Pass::Particles) | bit(Pass::Sprites);

} // namespace

const char* passName(Pass p) {
    const int i = static_cast<int>(p);
    return i >= 0 && i < kPassCount ? kNames[i] : "?";
}

uint32_t parsePassList(const std::string& list, std::string* unknown) {
    uint32_t mask = 0;
    size_t pos = 0;
    while (pos < list.size()) {
        size_t end = list.find_first_of(", ", pos);
        if (end == std::string::npos) end = list.size();
        const std::string name = list.substr(pos, end - pos);
        pos = end + 1;
        if (name.empty()) continue;
        if (name == "all") {
            mask = kAllPasses;
            continue;
        }
        if (name == "none") continue;
        if (name == "world") {
            mask |= kWorldPasses;
            continue;
        }
        bool found = false;
        for (int i = 0; i < kPassCount; ++i) {
            if (name == kNames[i]) {
                mask |= 1u << i;
                found = true;
            }
        }
        if (!found && unknown) {
            if (!unknown->empty()) *unknown += ",";
            *unknown += name;
        }
    }
    // The profile's own entries are not passes: always "on".
    return mask | bit(Pass::Ui2dBatch) | bit(Pass::Ui2dCopy);
}

void setPassMask(uint32_t mask) { g_mask = mask; }
uint32_t passMask() { return g_mask; }
bool passEnabled(Pass p) { return passBit(g_mask, p); }

std::string PassProfile::report(double ticksPerMs, bool doReset) {
    std::string out;
    char buf[96];
    const double n = frames_ ? static_cast<double>(frames_) : 1.0;
    std::snprintf(buf, sizeof buf, "frames=%u", frames_);
    out = buf;
    double sum = 0.0;
    for (int i = 0; i < kPassCount; ++i) {
        const PassTotals& t = totals_[i];
        if (t.ticks == 0 && t.draws == 0) continue;
        const double ms = static_cast<double>(t.ticks) / ticksPerMs / n;
        if (i != static_cast<int>(Pass::Ui2dBatch) && i != static_cast<int>(Pass::Ui2dCopy)) sum += ms;
        std::snprintf(buf, sizeof buf, " %s=%.2f/%.0f/%.0f", kNames[i], ms, static_cast<double>(t.draws) / n,
                      static_cast<double>(t.verts) / n);
        out += buf;
    }
    std::snprintf(buf, sizeof buf, " sum=%.2f", sum);
    out += buf;
    if (doReset) reset();
    return out;
}

void PassProfile::reset() {
    for (PassTotals& t : totals_) t = PassTotals();
    frames_ = 0;
}

LoadProfile& loadProfile() {
    static LoadProfile p;
    return p;
}

PassProfile& passProfile() {
    static PassProfile p;
    return p;
}

} // namespace as3d::pica
