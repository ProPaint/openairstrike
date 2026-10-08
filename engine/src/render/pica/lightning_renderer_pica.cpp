// Lightning bolts on the PICA (lightning_renderer_pica.h).
#include "lightning_renderer_pica.h"

#include <citro3d.h>

#include <cstring>

#include "as3d/lightning_render.h"
#include "as3d/material_key.h"
#include "as3d/render_state.h"
#include "fx_draw_pica.h"
#include "pica_frame.h"

namespace as3d::pica {

namespace {
LightningPicaStats g_stats;
} // namespace

const LightningPicaStats& lightningPicaStats() { return g_stats; }

namespace {
const TestBolts* g_test = nullptr;
}
void setTestBolts(const TestBolts* bolts) { g_test = bolts; }
const TestBolts* testBolts() { return g_test; }

void LightningPass::prepare(const Vec3* starts, const Vec3* ends, size_t count, float time, const Texture2D* texture) {
    g_stats = LightningPicaStats();
    g_stats.time = time;
    verts_.clear();
    runs_.clear();
    arena_ = nullptr;
    arenaFrame_ = ~0u;
    if (count == 0 || !texture || !texture->valid()) return;
    size_t use = count;
    if (use > static_cast<size_t>(kMaxLightningBoltsPica)) {
        g_stats.boltsDropped = static_cast<int>(use) - kMaxLightningBoltsPica;
        use = kMaxLightningBoltsPica;
    }
    std::vector<LightningVertex> src;
    g_stats.bolts = buildLightningBolts(starts, ends, use, time, texture, src, runs_);
    verts_.resize(src.size());
    for (size_t i = 0; i < src.size(); ++i) {
        FlatVertex& f = verts_[i];
        f.pos[0] = src[i].pos.x;
        f.pos[1] = src[i].pos.y;
        f.pos[2] = src[i].pos.z;
        f.uv[0] = src[i].uv.x;
        f.uv[1] = src[i].uv.y;
        f.rgba[0] = f.rgba[1] = f.rgba[2] = f.rgba[3] = 255;
    }
    g_stats.vertices = static_cast<int>(verts_.size());
}

int LightningPass::draw(const Mat4& view, const Mat4& projection, const Vec3& fogColour, float fogStart, float fogEnd) {
    if (runs_.empty() || verts_.empty() || !backendReady()) return 0;
    LinearArena& arena = frameArena();
    if (!arena_ || arenaFrame_ != arena.frames()) {
        auto* v = arena.allocArray<FlatVertex>(verts_.size());
        arenaFrame_ = arena.frames();
        arena_ = v;
        if (!v) return 0;
        std::memcpy(v, verts_.data(), verts_.size() * sizeof(FlatVertex));
        g_stats.arenaBytes = verts_.size() * sizeof(FlatVertex);
    }
    if (!arena_) return 0;
    FxEyeParams eye;
    eye.view = view;
    eye.projection = projection;
    eye.fogColour = fogColour;
    eye.fogStart = fogStart;
    eye.fogEnd = fogEnd;
    g_stats.draws = drawFlatRuns(arena_, runs_, eye);
    MaterialKey plain;
    applyMaterialState(plain);
    return g_stats.draws;
}

} // namespace as3d::pica
