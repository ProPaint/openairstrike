// Silhouette shadows on the PICA (as3d/shadow_render.h, 3DS only): the CPU silhouettes, the
// blob and the draped draw. The design is in shadow_pica.h; docs/spec/render-pipeline.md 5.
#include "as3d/shadow_render.h"

#include <3ds.h>
#include <citro3d.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include "as3d/material_key.h"
#include "as3d/render_state.h"
#include "fx_pica.h"
#include "pica_frame.h"
#include "shader_set.h"
#include "shadow_pica.h"
#include "tev.h"

namespace as3d {

namespace pica {

namespace {

ShadowPicaStats g_stats;
ShadowGenInfo g_lastGen;
std::function<const ShadowSkin*(const Texture2D*)> g_skinSource;
bool g_precacheRotations = false;

double msSince(u64 t0) { return static_cast<double>(svcGetSystemTick() - t0) / (SYSCLOCK_ARM11 / 1000.0); }

// The sequels' multiplied look (DecalLook::ShadowMultiply): grey 1 - texture alpha, alpha 1,
// drawn with the Filter blend (destination x source).
TevConfig tevShadowMultiply() {
    TevConfig c;
    c.count = 1;
    TevStage& s = c.stages[0];
    s.srcRgb[0] = TevSrc::Texture0;
    s.opRgb[0] = TevOpRgb::OneMinusSrcAlpha;
    s.funcRgb = TevFunc::Replace;
    s.srcA[0] = TevSrc::Constant;
    s.funcA = TevFunc::Replace;
    return c;
}

} // namespace

const ShadowPicaStats& shadowPicaStats() { return g_stats; }
void resetShadowPicaStats() {
    const size_t blob = g_stats.blobBytes;
    g_stats = ShadowPicaStats();
    g_stats.blobBytes = blob;
}
ShadowGenInfo lastShadowGenInfo() { return g_lastGen; }
void noteShadowFrame(int late, double placeMs) {
    g_stats.late = late;
    g_stats.placeMs = placeMs;
}
void setShadowSkinSource(std::function<const ShadowSkin*(const Texture2D*)> source) { g_skinSource = std::move(source); }
void setShadowPrecacheRotations(bool all) { g_precacheRotations = all; }
bool shadowPrecacheRotations() { return g_precacheRotations; }

size_t ShadowPass::CacheHash::operator()(const CacheKey& k) const {
    auto bits = [](float f) {
        uint32_t u;
        std::memcpy(&u, &f, sizeof u);
        return static_cast<size_t>(u);
    };
    size_t h = std::hash<const void*>()(k.texture);
    for (float f : {k.x, k.y, k.sx, k.sy}) h ^= bits(f) + 0x9e3779b9u + (h << 6) + (h >> 2);
    return h;
}

// ---------------------------------------------------------------------------------------
// ShadowPass
// ---------------------------------------------------------------------------------------

bool ShadowPass::init() {
    if (blob_.valid()) return true;
    // A radial falloff: 0.4 (102) at the centre, (1 - r^2) towards 0 at the inscribed circle;
    // the border texels stay 0 so the clamped edge casts nothing.
    Image img;
    img.width = img.height = kShadowBlobSide;
    img.hasAlpha = true;
    img.rgba.assign(static_cast<size_t>(kShadowBlobSide) * kShadowBlobSide * 4, 114);
    const float c = 0.5f * static_cast<float>(kShadowBlobSide);
    for (int y = 0; y < kShadowBlobSide; ++y) {
        for (int x = 0; x < kShadowBlobSide; ++x) {
            const float dx = (static_cast<float>(x) + 0.5f - c) / (c - 1.0f);
            const float dy = (static_cast<float>(y) + 0.5f - c) / (c - 1.0f);
            const float r2 = dx * dx + dy * dy;
            const float a = r2 < 1.0f ? kShadowMaxAlpha * (1.0f - r2) : 0.0f;
            img.rgba[(static_cast<size_t>(y) * kShadowBlobSide + static_cast<size_t>(x)) * 4 + 3] =
                static_cast<u8>(a * 255.0f + 0.5f);
        }
    }
    TextureOptions opts;
    opts.mipmaps = true;
    opts.wrapS = opts.wrapT = Wrap::ClampToEdge;
    opts.cls = TextureClass::Shadow;
    opts.res = TextureOptions::Residency::Linear;
    blob_.create(img, opts);
    g_stats.blobBytes = blob_.valid() ? blob_.bytes() : 0;
    return blob_.valid();
}

void ShadowPass::begin() {
    items_.clear();
    verts_.clear();
    arenaVerts_ = nullptr;
    arenaFrame_ = ~0u;
    g_stats.drawn = g_stats.blobs = g_stats.vertices = g_stats.draws = 0;
    g_stats.arenaBytes = 0;
}

void ShadowPass::add(const Texture2D* texture, const GroundRect& rect, bool isBlob, bool isStatic) {
    if (!texture || !texture->valid()) return;
    Item it;
    it.texture = texture;
    it.rect = rect;
    it.blob = isBlob;
    it.isStatic = isStatic;
    items_.push_back(it);
    if (isBlob) ++g_stats.blobs;
    else ++g_stats.drawn;
}

void ShadowPass::build(const TerrainGridView& grid) {
    const u64 t0 = svcGetSystemTick();
    ++builds_;
    verts_.clear();
    int draped = 0;
    for (Item& it : items_) {
        it.first = verts_.size();
        if (!it.isStatic) {
            buildTerrainDecal(grid, it.rect, verts_);
            ++draped;
        } else {
            const CacheKey key{it.texture, it.rect.origin.x, it.rect.origin.y, it.rect.sizeX, it.rect.sizeY};
            auto found = cache_.find(key);
            if (found == cache_.end()) {
                Cached c;
                buildTerrainDecal(grid, it.rect, c.verts);
                found = cache_.emplace(key, std::move(c)).first;
                ++draped;
            }
            found->second.lastBuild = builds_;
            verts_.insert(verts_.end(), found->second.verts.begin(), found->second.verts.end());
        }
        it.count = verts_.size() - it.first;
    }
    // Forget the rectangles not seen for 120 builds (destroyed objects, out of view).
    if (builds_ % 60 == 0) {
        for (auto i = cache_.begin(); i != cache_.end();) {
            if (builds_ - i->second.lastBuild > 120) i = cache_.erase(i);
            else ++i;
        }
    }
    g_stats.vertices = static_cast<int>(verts_.size());
    g_stats.drapedNow = draped;
    g_stats.buildMs = msSince(t0);
}

void ShadowPass::draw(const Mat4& viewProj) {
    if (verts_.empty() || !backendReady()) return;
    LinearArena& arena = frameArena();
    if (!arenaVerts_ || arenaFrame_ != arena.frames()) {
        // First eye of this arena frame: the draped vertices into the arena, white.
        auto* v = arena.allocArray<FlatVertex>(verts_.size());
        arenaVerts_ = v;
        arenaFrame_ = arena.frames();
        if (!v) return;
        for (size_t i = 0; i < verts_.size(); ++i) {
            const DecalVertex& d = verts_[i];
            v[i].pos[0] = d.x;
            v[i].pos[1] = d.y;
            v[i].pos[2] = d.z;
            v[i].uv[0] = d.u;
            v[i].uv[1] = d.v;
            v[i].rgba[0] = v[i].rgba[1] = v[i].rgba[2] = v[i].rgba[3] = 255;
        }
        g_stats.arenaBytes += verts_.size() * sizeof(FlatVertex);
    }
    ShaderSet& sh = shaders();
    sh.bind(Shader::Flat);
    sh.setMvp(viewProj);
    sh.setVertices(arenaVerts_);
    applyMaterialState(decalMaterialKey(multiply_ ? DecalBlend::Filter : DecalBlend::Alpha));
    applyTev(multiply_ ? tevShadowMultiply() : tevShadow(255));
    const Texture2D* bound = nullptr;
    for (const Item& it : items_) {
        if (it.count == 0) continue;
        if (it.texture != bound) {
            it.texture->bind(0);
            bound = it.texture;
        }
        C3D_DrawArrays(GPU_TRIANGLES, static_cast<int>(it.first), static_cast<int>(it.count));
        ++g_stats.draws;
    }
    MaterialKey plain; // blend off, depth test + write, cull back, no offset, level fog colour
    applyMaterialState(plain);
}

} // namespace pica

// ---------------------------------------------------------------------------------------
// ShadowRenderer (as3d/shadow_render.h)
// ---------------------------------------------------------------------------------------

struct ShadowRenderer::Impl {
    RenderRules rules = renderRules(GameId::AirStrike3D);
    pica::ShadowPass pass;
};

ShadowRenderer::ShadowRenderer() : impl_(new Impl) {}
ShadowRenderer::~ShadowRenderer() = default;

void ShadowRenderer::setRules(const RenderRules& rules) {
    impl_->rules = rules;
    impl_->pass.setMultiply(rules.shadowMultiply);
}

bool ShadowRenderer::init(std::string* error) {
    if (impl_->pass.init()) return true;
    if (error) *error = "ShadowRenderer: cannot create the blob texture";
    return false;
}

// The GL side renders this in an FBO (spec 5.2); here the CPU rasteriser gives the same
// silhouette (at most kMaxShadowSide a side: `out.width` and `out.height` are the texture's). No CPU copy is kept (`out.image` stays empty, as on the web): 591 silhouettes
// of mission 1 would hold several MB of RGBA.
bool ShadowRenderer::generate(const ModelData& model, const Texture2D* skin, ShadowKind kind, const Vec3& towardsSun,
                              int rotationSteps, ShadowQuality quality, ShadowMap& out) {
    pica::ShadowPicaStats& st = pica::g_stats;
    if (model.faces.empty() || model.positions.empty()) {
        ++st.failed;
        return false;
    }
    if (kind == ShadowKind::Planar) rotationSteps = 0;
    const u64 t0 = svcGetSystemTick();
    const ShadowSkin* cpuSkin = skin && pica::g_skinSource ? pica::g_skinSource(skin) : nullptr;
    if (cpuSkin && (!cpuSkin->valid() || cpuSkin->opaque())) cpuSkin = nullptr;
    // The GL side's size, then rasterised at most kMaxShadowSide a side over the same bounds.
    int glW = 0, glH = 0;
    shadowTextureSize(computeShadowBounds(model, kind, towardsSun, rotationSteps), quality, glW, glH, impl_->rules);
    const bool capped = glW > pica::kMaxShadowSide || glH > pica::kMaxShadowSide;
    ShadowSilhouette sil;
    if (!rasterizeShadowSilhouette(model, cpuSkin, kind, towardsSun, rotationSteps, quality, impl_->rules, sil,
                                   pica::kMaxShadowSide)) {
        ++st.failed;
        return false;
    }
    Image img = std::move(sil.image);
    TextureOptions opts;
    opts.mipmaps = impl_->rules.shadowMipmaps;
    opts.wrapS = opts.wrapT = Wrap::ClampToEdge;
    opts.cls = TextureClass::Shadow;
    opts.res = TextureOptions::Residency::Linear;
    out.texture.create(img, opts);
    out.image = Image();
    out.bounds = sil.bounds;
    out.kind = kind;
    out.width = sil.width;
    out.height = sil.height;
    pica::ShadowGenInfo& gi = pica::g_lastGen;
    gi.width = glW;
    gi.height = glH;
    gi.texWidth = img.width;
    gi.texHeight = img.height;
    gi.bytes = out.texture.bytes();
    gi.ms = pica::msSince(t0);
    gi.skinAlpha = cpuSkin != nullptr;
    if (!out.texture.valid()) {
        ++st.failed;
        return false;
    }
    ++st.generated;
    if (capped) ++st.capped;
    {
        int k = 0;
        while (k < 7 && (8 << k) < std::max(glW, glH)) ++k;
        ++st.bySide[k];
    }
    st.textureBytes += gi.bytes;
    st.generateMs += gi.ms;
    st.maxGenerateMs = std::max(st.maxGenerateMs, gi.ms);
    return true;
}

void ShadowRenderer::draw(const Terrain& terrain, const ShadowInstance* instances, size_t count,
                          const DecalViewParams& params) {
    draw(TerrainGridView::of(terrain), instances, count, params);
}

// One mono draw of `instances` (the world renderer uses pica::ShadowPass per eye instead).
// `params.projection` is a PICA projection (picaEyeProjection), as for the other PICA renderers.
void ShadowRenderer::draw(const TerrainGridView& grid, const ShadowInstance* instances, size_t count,
                          const DecalViewParams& params) {
    pica::ShadowPass& pass = impl_->pass;
    pass.begin();
    for (size_t i = 0; i < count; ++i) {
        const ShadowInstance& inst = instances[i];
        if (!inst.map || !inst.map->valid()) continue;
        pass.add(&inst.map->texture, shadowRect(*inst.map, inst.origin, inst.yawDegrees), false, false);
    }
    pass.build(grid);
    lastTriangles_ = static_cast<size_t>(pica::shadowPicaStats().vertices) / 3;
    float nearPlane, farPlane;
    pica::detail::planesOfProjection(params.projection.m, &nearPlane, &farPlane);
    pica::setDepthMapping(FogDepthMap::WBuffer, nearPlane);
    pica::setLevelFogColor(params.fogColour);
    if (params.fogStart < 1.0e7f && params.fogEnd > params.fogStart)
        pica::setFog(nearPlane, farPlane, params.fogStart, params.fogEnd);
    else
        pica::disableFog();
    pass.draw(params.projection * params.view);
}

} // namespace as3d
