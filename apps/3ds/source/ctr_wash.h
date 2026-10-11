// The water-wash detector on the console (run.ini wash_check; docs/3ds-port.md "Wash detector"):
// every frame, after each top eye's passes, a GX texture copy of seven 8-pixel columns of the eye's
// D24S8 depth buffer (the native buffer's tile rows: 7 x 7.5 KB = 52.5 KB per eye) is queued
// behind the eye's command list into a double-buffered linear buffer; at the start of the next
// frame, once the fence says that frame is complete (no wait is added), the CPU judges 112
// samples per eye (7 columns x 16 rows, 12 of them in the lower half, the last at row 238: R3.3) with wash_detect.h: the analytic water-plane depth against the GPU's depth where
// the terrain must be in front. An episode is a run of consecutive washed frames; it is logged
// when it ends (AS3D_WASH start= frames= eye= washed= rows= cols= ...), and the first frame of the
// first kMaxAutoMarks episodes of a level asks for a mark (the screens dumped as for L) and arms
// the register read-back for the next frames.
#pragma once

#include <citro3d.h>

#include <cstdint>
#include <string>
#include <vector>

#include "as3d/math.h"
#include "wash_detect.h"

namespace as3d {
class Terrain;
}

namespace ctrapp {

class WashDetector {
public:
    bool logEvals = false; // run.ini wash_log=1: one AS3D_WASH_EVAL line per judged eye-frame
    ~WashDetector();
    void setEnabled(bool on);
    bool enabled() const { return enabled_; }
    // A new level (or none): pending copies and the open episode are dropped / closed.
    void reset();
    // Inside the frame, after `eye`'s passes on `target` (before the kick): queue the copy. `scene`
    // and the eye's view are kept to judge it next frame. False when nothing was queued.
    bool queue(C3D_RenderTarget* target, int eye, const WashView& view, const WashScene& scene,
               const as3d::Terrain* terrain, unsigned displayedFrame);
    // At the start of a frame, after pica::frameBegin: judge the copies of the last frame that queued
    // any (complete by the fence). `terrain` is the level's now (a copy from another level is
    // dropped). True when a washed episode just started and may take a mark (the caller dumps the
    // screens after this frame, as for L).
    bool judge(const as3d::Terrain* terrain, unsigned displayedFrame);
    // The level ends: the open episode is logged.
    void flush();
    // run.ini wash_selftest=1: the sample decoding against a synchronous full read-back of `target`
    // (the L mark's path), once; logs AS3D_WASH_SELFTEST.
    void selfTest(C3D_RenderTarget* target, const WashView& view, const WashScene& scene);
    // The periodic line (AS3D_WASH_STATS, with the AS3D_PASS_MS report).
    std::string statsLine();
    // The rows of the sample grid inside the nearest terrain the frame drew (the first chunk of the
    // base pass, terrain_pica.h terrainFirstChunkBounds), over the level so far: logged once per
    // level as AS3D_WASH_COVERAGE (R3.3), after kCoverageFrames judged left-eye frames or at the
    // level's end.
    static constexpr unsigned kCoverageFrames = 1800;

private:
    struct Pending {
        bool valid = false;
        uint32_t serial = 0;    // the frame serial that queued it
        unsigned frame = 0;     // displayed frame
        WashView view;
        WashScene scene;
        const as3d::Terrain* terrain = nullptr;
        bool firstValid = false; // the first chunk the eye's base pass drew (world box)
        float firstMin[3] = {0, 0, 0}, firstMax[3] = {0, 0, 0};
    };
    void noteCoverage(const Pending& p);
    void logCoverage();
    struct Coverage {
        unsigned frames = 0, framesUnder2 = 0, rowsInSum = 0, rowsInMin = ~0u;
        double topSum = 0.0, bottomSum = 0.0;
        bool logged = false;
    } coverage_;
    std::vector<int> sampleRows_; // the distinct sample rows
    bool alloc();
    float sampleEyeDepth(const uint8_t* buf, int x, int y, float nearPlane, float farPlane) const;
    void closeEpisode();

    bool enabled_ = false;
    uint8_t* buf_[2][2] = {{nullptr, nullptr}, {nullptr, nullptr}}; // [half][eye]
    Pending pending_[2][2];
    int half_ = 0;
    std::vector<std::pair<int, int>> pixels_;
    // The open episode.
    bool inEpisode_ = false;
    unsigned epStart_ = 0, epFrames_ = 0;
    int epMax_ = 0, epEyes_ = 0, epRowMin_ = 0, epRowMax_ = 0, epColMin_ = 0, epColMax_ = 0;
    int levelEpisodes_ = 0;
    // Per eye and sample: the terrain march's last answer (a 1/kMarchPhases of the samples is
    // marched again each frame; a sample judged washed is marched again before it counts).
    struct Cell {
        bool marched = false, front = false;
        float terrainDepth = 0.0f;
    };
    std::vector<Cell> cells_[2];
    unsigned phase_ = 0;
    unsigned marches_ = 0;
    // Since the last stats line.
    unsigned evals_ = 0, judged_ = 0, agree_ = 0, nearer_ = 0, washedFrames_ = 0, queued_ = 0, dropped_ = 0;
    double err_ = 0.0, ms_ = 0.0, msMax_ = 0.0;
    bool selfTested_ = false;
};

} // namespace ctrapp
