// The PICA backend's per-pass CPU profile and pass switches (hardware tuning, docs/3ds-port.md
// "Hardware tuning"). Platform neutral: the clock is passed in (svcGetSystemTick on the 3DS),
// so the desktop tests can exercise the bookkeeping.
//
// Profile: each pass of a frame (world passes per eye, the 2D lists, the brightness pass) adds
// its CPU ticks (the C3D calls and whatever vertex building the pass does inside the frame),
// draw calls and vertices. report() gives the per-frame averages since the last report as one
// line:  "frames=300 <name>=<ms>/<draws>/<verts> ..." (per displayed frame, both eyes summed).
// The 2D list internals (batch build, arena conversion) are their own entries, included in the
// time of the 2D pass that flushed them.
//
// Switches (run.ini passes= / hud= / touch=, for A/B hardware runs): a pass that is off is
// skipped entirely (no CPU, no GPU work).
#pragma once

#include <cstdint>
#include <string>

namespace as3d::pica {

enum class Pass : int {
    Clear,      // the world's pass 0 (clear) and the state set-up of the frame
    Terrain,    // pass 2
    Marks,      // pass 4 (ground marks, skid trails)
    Shadows,    // pass 5
    Opaque,     // pass 6 (opaque mesh list)
    Water,      // pass 7
    Rest,       // passes 8, 9 (transparent and effect mesh lists)
    Lightning,  // after the effect list
    Particles,  // pass 10
    Sprites,    // pass 12 (health bars included)
    Brightness, // pass 14
    Top2d,      // the top screen's 2D list (title, PAUSED, perf line, model view backdrop)
    Model,      // the sequels' model view (top screen)
    Hud,        // the bottom screen's HUD (quads built and flushed)
    Hint,       // the hint box over the HUD
    Touch,      // the touch band (quads built and flushed)
    Bottom2d,   // the front end's bottom screen list
    Ui2dBatch,  // of the 2D passes: buildBatch2D + pixel snapping (CPU)
    Ui2dCopy,   // of the 2D passes: Vertex2D -> FlatVertex conversion into the arena
    Count
};

constexpr int kPassCount = static_cast<int>(Pass::Count);

const char* passName(Pass p);

// Parses run.ini passes=: a comma (or space) separated list of pass names (passName; "world" =
// every world pass, "all", "none"). Returns the mask of enabled passes; unknown names are
// appended to `unknown` (comma separated) when given.
uint32_t parsePassList(const std::string& list, std::string* unknown = nullptr);
constexpr uint32_t kAllPasses = 0xFFFFFFFFu;

void setPassMask(uint32_t mask);
uint32_t passMask();
inline bool passBit(uint32_t mask, Pass p) { return (mask >> static_cast<int>(p)) & 1u; }
bool passEnabled(Pass p);

struct PassTotals {
    uint64_t ticks = 0;
    int64_t draws = 0;
    int64_t verts = 0;
};

class PassProfile {
public:
    void add(Pass p, uint64_t ticks, int draws, int verts) {
        PassTotals& t = totals_[static_cast<int>(p)];
        t.ticks += ticks;
        t.draws += draws;
        t.verts += verts;
    }
    void endFrame() { ++frames_; }
    unsigned frames() const { return frames_; }
    const PassTotals& totals(Pass p) const { return totals_[static_cast<int>(p)]; }
    // The per-frame averages since the last reset (passes never used are left out), then reset.
    // `ticksPerMs`: the clock's rate.
    std::string report(double ticksPerMs, bool reset = true);
    void reset();

private:
    PassTotals totals_[kPassCount];
    unsigned frames_ = 0;
};

// The backend's instance.
PassProfile& passProfile();

// The load profile (level start, docs/3ds-port.md "Hardware tuning"): where Texture2D::create
// and the mesh uploads spend their time. Ticks of the caller's clock; cumulative, the caller
// takes differences around a phase.
struct LoadProfile {
    uint64_t texTicks = 0;     // the whole Texture2D::create
    uint64_t texMipTicks = 0;  // of which: power-of-two resize and the mip chain
    uint64_t texSwizzleTicks = 0; // of which: texconv swizzle (format conversion + tiling)
    uint64_t texUploadTicks = 0;  // of which: copies into linear memory, cache flushes, GX copies to VRAM
    int texCount = 0;
    int texLevels = 0;
    uint64_t texBytes = 0;
    uint64_t meshTicks = 0; // GpuMesh::upload (linear buffers, copies, flushes)
    int meshCount = 0;
};
LoadProfile& loadProfile();

} // namespace as3d::pica
