// Ground marks and skid trails on the PICA (ground_marks_pica.h).
#include "ground_marks_pica.h"

#include <3ds.h>
#include <citro3d.h>

#include <algorithm>
#include <cstring>

#include "as3d/material_key.h"
#include "as3d/render_state.h"
#include "fx_draw_pica.h"
#include "pica_frame.h"

namespace as3d::pica {

namespace {

MarkPicaStats g_stats;

double msSince(u64 t0) { return static_cast<double>(svcGetSystemTick() - t0) / (SYSCLOCK_ARM11 / 1000.0); }

uint8_t byteOf(float f) { return static_cast<uint8_t>(std::min(std::max(f, 0.0f), 1.0f) * 255.0f + 0.5f); }

bool sameMark(const std::pair<GroundMarkDesc, std::uint64_t>& a, const std::pair<GroundMarkDesc, std::uint64_t>& b) {
    const GroundMarkDesc& x = a.first;
    const GroundMarkDesc& y = b.first;
    return a.second == b.second && x.texture == y.texture && x.alpha == y.alpha && x.colour.x == y.colour.x &&
           x.colour.y == y.colour.y && x.colour.z == y.colour.z && x.blend == y.blend;
}

} // namespace

const MarkPicaStats& markPicaStats() { return g_stats; }

namespace {
const std::vector<SkidTrail>* g_testSkids = nullptr;
}
void setTestSkidTrails(const std::vector<SkidTrail>* trails) { g_testSkids = trails; }
const std::vector<SkidTrail>* testSkidTrails() { return g_testSkids; }

void MarkPass::reset() {
    lastMarks_.clear();
    markVerts_.clear();
    markRuns_.clear();
    skidVerts_.clear();
    skidRuns_.clear();
    runs_.clear();
    dirty_ = true;
    markBatchCount_ = markDropped_ = 0;
    arena_ = nullptr;
    arenaFrame_ = ~0u;
    g_stats = MarkPicaStats();
}

void MarkPass::beginFrame() {
    const int rebuilds = g_stats.rebuilds;
    g_stats = MarkPicaStats();
    g_stats.rebuilds = rebuilds;
    arena_ = nullptr;
    arenaFrame_ = ~0u;
}

void MarkPass::setMarks(const MarkList& marks, const TerrainGridView& grid) {
    const u64 t0 = svcGetSystemTick();
    bool same = !dirty_ && marks.size() == lastMarks_.size();
    for (size_t i = 0; same && i < marks.size(); ++i) same = sameMark(marks[i], lastMarks_[i]);
    if (!same && grid.valid()) {
        dirty_ = false;
        lastMarks_ = marks;
        markVerts_.clear();
        markRuns_.clear();
        ++g_stats.rebuilds;
        g_stats.rebuilt = true;
        std::vector<DecalBatch> batches;
        std::vector<DecalVertex> tmp;
        int dropped = 0;
        for (const auto& m : marks) {
            const GroundMarkDesc& d = m.first;
            if (!d.texture || !d.texture->valid()) continue;
            tmp.clear();
            buildTerrainDecal(grid, GroundRect::axisAligned(d.origin.x + d.minX, d.origin.y + d.minY,
                                                            d.origin.x + d.maxX, d.origin.y + d.maxY),
                              tmp);
            if (tmp.empty() || markVerts_.size() + tmp.size() > kMaxMarkVertices) {
                ++dropped;
                continue;
            }
            DecalBatch b;
            b.first = markVerts_.size();
            b.count = tmp.size();
            b.texture = d.texture;
            b.blend = d.blend;
            b.colour = d.colour;
            b.alpha = d.alpha;
            batches.push_back(b);
            const uint8_t r = byteOf(d.colour.x), g = byteOf(d.colour.y), bl = byteOf(d.colour.z), a = byteOf(d.alpha);
            for (const DecalVertex& v : tmp) {
                FlatVertex f;
                f.pos[0] = v.x;
                f.pos[1] = v.y;
                f.pos[2] = v.z;
                f.uv[0] = v.u;
                f.uv[1] = d.unflippedTexture ? 1.0f - v.v : v.v;
                f.rgba[0] = r;
                f.rgba[1] = g;
                f.rgba[2] = bl;
                f.rgba[3] = a;
                markVerts_.push_back(f);
            }
        }
        buildDecalRuns(batches, markRuns_);
        markBatchCount_ = static_cast<int>(batches.size());
        markDropped_ = dropped;
    }
    g_stats.marks = markBatchCount_;
    g_stats.marksDropped = markDropped_;
    g_stats.markVertices = static_cast<int>(markVerts_.size());
    g_stats.buildMs += msSince(t0);
}

void MarkPass::setSkids(const SkidTrail* trails, size_t count, const TerrainGridView& grid,
                        const SkidTrailParams& params, const SkidTextureResolver& resolve) {
    const u64 t0 = svcGetSystemTick();
    skidSrc_.clear();
    skidRuns_.clear();
    skidVerts_.clear();
    if (count > 0 && grid.valid()) {
        g_stats.skidTrails = buildSkidTrails(trails, count, grid, params, resolve, skidSrc_, skidRuns_);
        skidVerts_.resize(skidSrc_.size());
        for (size_t i = 0; i < skidSrc_.size(); ++i) {
            const SkidVertex& s = skidSrc_[i];
            FlatVertex& f = skidVerts_[i];
            f.pos[0] = s.x;
            f.pos[1] = s.y;
            f.pos[2] = s.z;
            f.uv[0] = s.u;
            f.uv[1] = s.v;
            f.rgba[0] = f.rgba[1] = f.rgba[2] = 255;
            f.rgba[3] = byteOf(s.alpha);
        }
        for (const DrawRun& r : skidRuns_) g_stats.skidSegments += std::max(r.count / 2 - 1, 0);
        g_stats.skidVertices = static_cast<int>(skidVerts_.size());
    }
    runs_ = markRuns_;
    for (DrawRun r : skidRuns_) {
        r.first += static_cast<int>(markVerts_.size());
        runs_.push_back(r);
    }
    noteTextures();
    g_stats.buildMs += msSince(t0);
}

void MarkPass::noteTextures() {
    std::vector<const Texture2D*> seen;
    for (const DrawRun& r : runs_) {
        if (!r.texture || std::find(seen.begin(), seen.end(), r.texture) != seen.end()) continue;
        seen.push_back(r.texture);
        g_stats.textureBytes += r.texture->bytes();
    }
    g_stats.textures = static_cast<int>(seen.size());
}

int MarkPass::draw(const Mat4& view, const Mat4& projection, const Vec3& fogColour, float fogStart, float fogEnd) {
    const size_t total = markVerts_.size() + skidVerts_.size();
    if (runs_.empty() || total == 0 || !backendReady()) return 0;
    LinearArena& arena = frameArena();
    if (!arena_ || arenaFrame_ != arena.frames()) {
        auto* v = arena.allocArray<FlatVertex>(total);
        arenaFrame_ = arena.frames();
        arena_ = v;
        if (!v) return 0;
        if (!markVerts_.empty()) std::memcpy(v, markVerts_.data(), markVerts_.size() * sizeof(FlatVertex));
        if (!skidVerts_.empty())
            std::memcpy(v + markVerts_.size(), skidVerts_.data(), skidVerts_.size() * sizeof(FlatVertex));
        g_stats.arenaBytes = total * sizeof(FlatVertex);
    }
    if (!arena_) return 0;
    FxEyeParams eye;
    eye.view = view;
    eye.projection = projection;
    eye.fogColour = fogColour;
    eye.fogStart = fogStart;
    eye.fogEnd = fogEnd;
    g_stats.draws = drawFlatRuns(arena_, runs_, eye);
    MaterialKey plain; // blend off, depth test + write, cull back, no offset, level fog colour
    applyMaterialState(plain);
    return g_stats.draws;
}

} // namespace as3d::pica
