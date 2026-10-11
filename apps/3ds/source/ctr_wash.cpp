// The water-wash detector on the console (ctr_wash.h).
#include "ctr_wash.h"

#include <3ds.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "as3d/core.h"
#include "pica_frame.h"
#include "state_check_pica.h"
#include "terrain_pica.h"

namespace ctrapp {

namespace {

// The native top target is 240 wide (the screen's vertical axis, from the bottom) and 400 tall
// (the screen's horizontal axis); its D24S8 depth is stored in 8x8 tiles, one row of tiles (a
// "strip") per 8 native rows = 8 screen columns: strip s holds screen columns 8s..8s+7, its tiles
// running up the screen from the bottom (native x), 7680 bytes. The copy takes strips 3, 10, ...,
// 45 (one in seven): screen columns 28, 84, ..., 364 are sampled.
constexpr int kStripBytes = 240 * 8 * 4;
constexpr int kFirstStrip = 3, kStripStep = 7, kStrips = 7;
constexpr size_t kCopyBytes = static_cast<size_t>(kStrips) * kStripBytes;
constexpr unsigned kMarchPhases = 16; // each sample's terrain march is redone every 16 judged frames

// The byte offset of native pixel (nx, local row r of its strip) inside a strip: 8x8 tiles along
// nx, Morton order inside a tile (x bits even, y bits odd).
int stripOffset(int nx, int r) {
    unsigned i = static_cast<unsigned>(nx & 7) | (static_cast<unsigned>(r & 7) << 8);
    i = (i ^ (i << 2)) & 0x1313u;
    i = (i ^ (i << 1)) & 0x1515u;
    i = (i | (i >> 7)) & 0x3Fu;
    return static_cast<int>((i + static_cast<unsigned>(nx & ~7) * 8u) * 4u);
}

} // namespace

WashDetector::~WashDetector() {
    for (auto& h : buf_)
        for (uint8_t*& b : h) {
            if (b) as3d::pica::deferLinearFree(b);
            b = nullptr;
        }
}

void WashDetector::setEnabled(bool on) {
    enabled_ = on;
    if (pixels_.empty()) {
        for (int k = 0; k < kStrips; ++k) {
            const int x = (kFirstStrip + k * kStripStep) * 8 + 4;
            for (int y = 20; y <= 110; y += 30) pixels_.emplace_back(x, y);  // upper half: 4 rows
            for (int y = 126; y <= 234; y += 12) pixels_.emplace_back(x, y); // lower half: 10 rows
        }
    }
}

bool WashDetector::alloc() {
    if (buf_[0][0]) return true;
    for (auto& h : buf_)
        for (uint8_t*& b : h) {
            b = static_cast<uint8_t*>(linearMemAlign(kCopyBytes, 0x80));
            if (!b) {
                AS3D_WARN("wash detector: no linear memory for its read-back (4 x %u KB): off",
                          static_cast<unsigned>(kCopyBytes / 1024));
                enabled_ = false;
                return false;
            }
        }
    return true;
}

void WashDetector::reset() {
    flush();
    for (auto& h : pending_)
        for (Pending& p : h) p.valid = false;
    levelEpisodes_ = 0;
    cells_[0].clear();
    cells_[1].clear();
}

bool WashDetector::queue(C3D_RenderTarget* target, int eye, const WashView& view, const WashScene& scene,
                         const as3d::Terrain* terrain, unsigned displayedFrame) {
    if (!enabled_ || eye < 0 || eye > 1 || !target || !target->frameBuf.depthBuf ||
        target->frameBuf.depthFmt != GPU_RB_DEPTH24_STENCIL8 || target->frameBuf.width != 240 ||
        target->frameBuf.height != 400 || !alloc())
        return false;
    const uint32_t serial = as3d::pica::frameSerial();
    const int h = static_cast<int>(serial & 1u);
    Pending& p = pending_[h][eye];
    // The eye's command list first (flushed), then the copy behind it in citro3d's GX queue: the
    // queue starts an entry once the one before completed.
    as3d::pica::splitCommandList();
    auto* src = static_cast<uint8_t*>(target->frameBuf.depthBuf) + kFirstStrip * kStripBytes;
    GX_TextureCopy(reinterpret_cast<u32*>(src), GX_BUFFER_DIM(kStripBytes / 16, (kStripStep - 1) * kStripBytes / 16),
                   reinterpret_cast<u32*>(buf_[h][eye]), GX_BUFFER_DIM(kStripBytes / 16, 0), kCopyBytes,
                   GX_TRANSFER_RAW_COPY(1));
    p.valid = true;
    p.serial = serial;
    p.frame = displayedFrame;
    p.view = view;
    p.scene = scene;
    p.terrain = terrain;
    ++queued_;
    return true;
}

float WashDetector::sampleEyeDepth(const uint8_t* buf, int x, int y, float nearPlane, float farPlane) const {
    const int strip = x >> 3;
    const int k = (strip - kFirstStrip) / kStripStep;
    const int nx = 239 - y; // native x: the screen's vertical axis from the bottom
    const uint8_t* px = buf + static_cast<size_t>(k) * kStripBytes + stripOffset(nx, x & 7);
    // D24S8: the depth in the low 24 bits (little-endian), the stencil in the top byte.
    const uint32_t d = static_cast<uint32_t>(px[0]) | (static_cast<uint32_t>(px[1]) << 8) | (static_cast<uint32_t>(px[2]) << 16);
    return wBufferEyeDepth(d, nearPlane, farPlane);
}

void WashDetector::closeEpisode() {
    if (!inEpisode_) return;
    inEpisode_ = false;
    AS3D_INFO("AS3D_WASH start=%u frames=%u eye=%s washed=%d rows=%d..%d cols=%d..%d episode=%d", epStart_, epFrames_,
              epEyes_ == 3 ? "both" : (epEyes_ == 2 ? "R" : "L"), epMax_, epRowMin_, epRowMax_, epColMin_, epColMax_,
              levelEpisodes_);
    as3d::pica::stateCheckNoteWashEpisode(epStart_, epFrames_, epMax_);
}

void WashDetector::flush() { closeEpisode(); }

bool WashDetector::judge(const as3d::Terrain* terrain, unsigned displayedFrame) {
    (void)displayedFrame;
    if (!enabled_) return false;
    // The last frame that queued copies: the other half's (this frame's serial is one more).
    const uint32_t serial = as3d::pica::frameSerial();
    const int h = static_cast<int>((serial - 1u) & 1u);
    bool any = false, washedFrame = false;
    int eyes = 0, maxWashed = 0;
    unsigned frame = 0;
    int rowMin = 0, rowMax = 0, colMin = 0, colMax = 0;
    for (int eye = 0; eye < 2; ++eye) {
        Pending& p = pending_[h][eye];
        if (!p.valid) continue;
        p.valid = false;
        if (p.serial + 1u != serial || p.terrain != terrain || !as3d::pica::gpuDone(p.serial)) {
            ++dropped_;
            continue;
        }
        const u64 t0 = svcGetSystemTick();
        GSPGPU_InvalidateDataCache(buf_[h][eye], kCopyBytes);
        WashResult r;
        as3d::Mat4 inv;
        std::vector<Cell>& cells = cells_[eye];
        if (cells.size() != pixels_.size()) cells.assign(pixels_.size(), Cell());
        if (washInverse(p.view, &inv)) {
            for (size_t i = 0; i < pixels_.size(); ++i) {
                WashRay ray;
                Cell& c = cells[i];
                if (!washRay(p.view, inv, p.scene, pixels_[i].first, pixels_[i].second, &ray)) {
                    c.marched = false;
                    continue;
                }
                const bool due = !c.marched || (i % kMarchPhases) == (phase_ % kMarchPhases);
                if (due) {
                    c.marched = true;
                    c.front = washTerrainFront(p.view, p.scene, ray, &c.terrainDepth);
                    ++marches_;
                }
                if (!c.front) continue;
                WashProbe q;
                q.x = pixels_[i].first;
                q.y = pixels_[i].second;
                q.terrainDepth = c.terrainDepth;
                q.waterDepth = ray.waterDepth;
                const float gpu = sampleEyeDepth(buf_[h][eye], q.x, q.y, p.view.nearPlane, p.view.farPlane);
                if (!due && washedSample(q, gpu)) {
                    // A cached answer is a few frames old: march again before the sample counts.
                    c.front = washTerrainFront(p.view, p.scene, ray, &c.terrainDepth);
                    ++marches_;
                    if (!c.front) continue;
                    q.terrainDepth = c.terrainDepth;
                }
                r.add(q, gpu);
            }
        }
        const double ms = static_cast<double>(svcGetSystemTick() - t0) * 1000.0 / SYSCLOCK_ARM11;
        as3d::pica::stateCheckNoteWashEval(ms, r.judged);
        any = true;
        frame = p.frame;
        ++evals_;
        judged_ += static_cast<unsigned>(r.judged);
        agree_ += static_cast<unsigned>(r.agree);
        nearer_ += static_cast<unsigned>(r.nearer);
        err_ += r.terrainErr;
        ms_ += ms;
        msMax_ = std::max(msMax_, ms);
        if (logEvals)
            AS3D_INFO("AS3D_WASH_EVAL frame=%u eye=%d judged=%d washed=%d agree=%d nearer=%d ms=%.3f", p.frame, eye, r.judged,
                      r.washed, r.agree, r.nearer, ms);
        if (r.washed >= kWashMinSamples) {
            if (!washedFrame || r.rowMin < rowMin) rowMin = r.rowMin;
            if (!washedFrame || r.rowMax > rowMax) rowMax = r.rowMax;
            if (!washedFrame || r.colMin < colMin) colMin = r.colMin;
            if (!washedFrame || r.colMax > colMax) colMax = r.colMax;
            washedFrame = true;
            eyes |= 1 << eye;
            maxWashed = std::max(maxWashed, r.washed);
        }
    }
    if (!any) return false;
    ++phase_;
    if (!washedFrame) {
        closeEpisode();
        return false;
    }
    ++washedFrames_;
    if (inEpisode_) {
        ++epFrames_;
        epEyes_ |= eyes;
        epMax_ = std::max(epMax_, maxWashed);
        epRowMin_ = std::min(epRowMin_, rowMin);
        epRowMax_ = std::max(epRowMax_, rowMax);
        epColMin_ = std::min(epColMin_, colMin);
        epColMax_ = std::max(epColMax_, colMax);
        return false;
    }
    inEpisode_ = true;
    ++levelEpisodes_;
    epStart_ = frame;
    epFrames_ = 1;
    epEyes_ = eyes;
    epMax_ = maxWashed;
    epRowMin_ = rowMin;
    epRowMax_ = rowMax;
    epColMin_ = colMin;
    epColMax_ = colMax;
    // The chunks the terrain drew that frame (the culling or the depth?).
    const std::vector<int>& drawn = as3d::pica::terrainDrawnChunks(0);
    AS3D_INFO("AS3D_WASH_START frame=%u eye=%s washed=%d rows=%d..%d cols=%d..%d drawn_chunks=%zu first_chunk=%d episode=%d",
              frame, eyes == 3 ? "both" : (eyes == 2 ? "R" : "L"), maxWashed, rowMin, rowMax, colMin, colMax, drawn.size(),
              drawn.empty() ? -1 : drawn.front(), levelEpisodes_);
    // The register read-back of every probed pass, this frame and the next.
    as3d::pica::stateCheckArmProbe(2);
    return levelEpisodes_ <= as3d::pica::kMaxAutoMarks && as3d::pica::stateCheckWashMarkDue();
}

void WashDetector::selfTest(C3D_RenderTarget* target, const WashView& view, const WashScene& scene) {
    if (selfTested_ || !target || !target->frameBuf.depthBuf || !alloc()) return;
    selfTested_ = true;
    // The L mark's path: the whole depth buffer through the display transfer (untiled, 32-bit words,
    // the depth in bytes 3, 2, 1), against the strips copied raw and decoded here.
    const size_t bytes = static_cast<size_t>(240) * 400 * 4;
    auto* lin = static_cast<uint8_t*>(linearAlloc(bytes));
    if (!lin) return;
    as3d::pica::waitGpuIdle();
    C3D_SyncDisplayTransfer(static_cast<u32*>(target->frameBuf.depthBuf), GX_BUFFER_DIM(240, 400),
                            reinterpret_cast<u32*>(lin), GX_BUFFER_DIM(240, 400),
                            GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
                                GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                                GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    GSPGPU_InvalidateDataCache(lin, bytes);
    auto* src = static_cast<uint8_t*>(target->frameBuf.depthBuf) + kFirstStrip * kStripBytes;
    C3D_SyncTextureCopy(reinterpret_cast<u32*>(src), GX_BUFFER_DIM(kStripBytes / 16, (kStripStep - 1) * kStripBytes / 16),
                        reinterpret_cast<u32*>(buf_[0][0]), GX_BUFFER_DIM(kStripBytes / 16, 0), kCopyBytes,
                        GX_TRANSFER_RAW_COPY(1));
    GSPGPU_InvalidateDataCache(buf_[0][0], kCopyBytes);
    int same = 0, sameB = 0, total = 0, nonzero = 0;
    double sum = 0.0;
    for (const auto& px : pixels_) {
        const uint8_t* f = lin + (static_cast<size_t>(px.first) * 240 + static_cast<size_t>(239 - px.second)) * 4;
        // The display transfer's word order is not the raw one: both readings.
        const uint32_t fullA = (static_cast<uint32_t>(f[3]) << 16) | (static_cast<uint32_t>(f[2]) << 8) | f[1];
        const uint32_t fullB = (static_cast<uint32_t>(f[2]) << 16) | (static_cast<uint32_t>(f[1]) << 8) | f[0];
        const float e2 = sampleEyeDepth(buf_[0][0], px.first, px.second, view.nearPlane, view.farPlane);
        const float eA = wBufferEyeDepth(fullA, view.nearPlane, view.farPlane);
        const float eB = wBufferEyeDepth(fullB, view.nearPlane, view.farPlane);
        ++total;
        if (fullA || fullB) ++nonzero;
        if (std::fabs(eA - e2) < 0.01f) ++same;
        if (std::fabs(eB - e2) < 0.01f) ++sameB;
        sum += std::fabs(std::fabs(eA - e2) < std::fabs(eB - e2) ? eA - e2 : eB - e2);
    }
    const std::vector<WashProbe> probes = washProbes(view, scene, pixels_);
    WashResult r;
    for (const WashProbe& q : probes) r.add(q, sampleEyeDepth(buf_[0][0], q.x, q.y, view.nearPlane, view.farPlane));
    AS3D_INFO("AS3D_WASH_SELFTEST samples=%d equal_to_full_readback=%d/%d (bytes 3,2,1 / 2,1,0) nonzero=%d mean_diff=%.3f "
              "judged=%d agree=%d nearer=%d washed=%d mean_terrain_err=%.2f",
              total, same, sameB, nonzero, total ? sum / total : 0.0, r.judged, r.agree, r.nearer, r.washed,
              r.agree ? r.terrainErr / r.agree : 0.0);
    linearFree(lin);
}

std::string WashDetector::statsLine() {
    char b[320];
    std::snprintf(b, sizeof b,
                  "AS3D_WASH_STATS evals=%u queued=%u dropped=%u judged_per_eye=%.1f agree=%.1f%% nearer=%.1f%% "
                  "mean_terrain_err=%.2f washed_frames=%u episodes=%d open=%d judge_ms=%.3f max=%.3f marches_per_eval=%.1f",
                  evals_, queued_, dropped_, evals_ ? static_cast<double>(judged_) / evals_ : 0.0,
                  judged_ ? 100.0 * agree_ / judged_ : 0.0, judged_ ? 100.0 * nearer_ / judged_ : 0.0,
                  agree_ ? err_ / agree_ : 0.0, washedFrames_, levelEpisodes_, inEpisode_ ? 1 : 0,
                  evals_ ? ms_ / evals_ : 0.0, msMax_, evals_ ? static_cast<double>(marches_) / evals_ : 0.0);
    marches_ = 0;
    evals_ = judged_ = agree_ = nearer_ = washedFrames_ = queued_ = dropped_ = 0;
    err_ = ms_ = msMax_ = 0.0;
    return b;
}

} // namespace ctrapp
