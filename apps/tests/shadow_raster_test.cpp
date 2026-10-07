// The CPU silhouette rasteriser (as3d/shadow_raster.h) against ShadowRenderer::generate() on
// the headless GLES context: every shadow mission 1 precaches at level load (the keys
// WorldRenderer::beginLevel generates), compared texel by texel. Also times the CPU path for
// the 3DS budget (M0.4d of the 3DS port plan). The synthetic cases need no game data.
#include "doctest.h"

#include "as3d/defs.h"
#include "as3d/level.h"
#include "as3d/platform.h"
#include "as3d/scene.h"
#include "as3d/shadow_raster.h"
#include "as3d/shadow_render.h"
#include "as3d/vfs.h"
#include "as3d/world.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "test_data.h"

using namespace as3d;

namespace {

GraphicsContext* rasterGlContext() {
    static std::unique_ptr<GraphicsContext> ctx = [] {
        GraphicsConfig cfg;
        cfg.width = 320;
        cfg.height = 240;
        cfg.headless = true;
        cfg.title = "as3d_tests";
        return createGraphicsContext(cfg);
    }();
    if (ctx) ctx->makeCurrent();
    return ctx.get();
}

// Peak shadow alpha is 102 (0.4): a texel counts as covered from half of that.
constexpr int kCoveredAlpha = 51;

struct Diff {
    double meanAbs = 0.0; // over the alpha channel, in 1/255
    double iou = 1.0;     // of the covered texels
    int maxAbs = 0;
};

Diff compare(const Image& gl, const Image& cpu) {
    Diff d;
    const size_t n = static_cast<size_t>(gl.width) * static_cast<size_t>(gl.height);
    long sum = 0;
    long inter = 0, uni = 0;
    for (size_t i = 0; i < n; i++) {
        int a = gl.rgba[i * 4 + 3], b = cpu.rgba[i * 4 + 3];
        sum += std::abs(a - b);
        d.maxAbs = std::max(d.maxAbs, std::abs(a - b));
        bool ca = a >= kCoveredAlpha, cb = b >= kCoveredAlpha;
        inter += ca && cb;
        uni += ca || cb;
    }
    d.meanAbs = n ? static_cast<double>(sum) / static_cast<double>(n) : 0.0;
    d.iou = uni ? static_cast<double>(inter) / static_cast<double>(uni) : 1.0;
    return d;
}

ModelData makeBox(float hx, float hy, float z0, float z1) {
    ModelData m;
    for (int i = 0; i < 8; i++)
        m.positions.push_back({(i & 1) ? hx : -hx, (i & 2) ? hy : -hy, (i & 4) ? z1 : z0});
    m.uvs = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    const int quads[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
    for (auto& q : quads) {
        Face a, b;
        a.v[0] = static_cast<u16>(q[0]); a.v[1] = static_cast<u16>(q[1]); a.v[2] = static_cast<u16>(q[2]);
        b.v[0] = static_cast<u16>(q[0]); b.v[1] = static_cast<u16>(q[2]); b.v[2] = static_cast<u16>(q[3]);
        a.uv[0] = 0; a.uv[1] = 1; a.uv[2] = 2;
        b.uv[0] = 0; b.uv[1] = 2; b.uv[2] = 3;
        m.faces.push_back(a);
        m.faces.push_back(b);
    }
    return m;
}

struct ShadowSpecT {
    bool any = false;
    ShadowKind kind = ShadowKind::Planar;
    ShadowQuality quality = ShadowQuality::Normal;
};

// As world_render.cpp's shadowSpecOf.
ShadowSpecT specOf(ShadowMode m) {
    switch (m) {
        case ShadowMode::Projected: return {true, ShadowKind::Projected, ShadowQuality::Normal};
        case ShadowMode::ProjectedLow: return {true, ShadowKind::Projected, ShadowQuality::Low};
        case ShadowMode::ProjectedHigh: return {true, ShadowKind::Projected, ShadowQuality::High};
        case ShadowMode::Planar: return {true, ShadowKind::Planar, ShadowQuality::Normal};
        case ShadowMode::PlanarLow: return {true, ShadowKind::Planar, ShadowQuality::Low};
        case ShadowMode::PlanarHigh: return {true, ShadowKind::Planar, ShadowQuality::High};
        default: return {};
    }
}

struct ShadowKey {
    std::string key;
    const ObjectDef* def = nullptr;
    ShadowSpecT spec;
    int steps = 0;
};

// WorldRenderer::Impl::precacheShadows: the placed object and its attachments.
void collectKeys(const DefDatabase& db, ResourceCache& cache, const ObjectDef* def, int steps, bool night, int depth,
                 std::set<std::string>& seen, std::vector<ShadowKey>& out) {
    if (!def || depth > kMaxAttachDepth) return;
    ShadowSpecT ss = specOf(def->shadow);
    if (ss.any && def->type == ObjectType::Model && !def->model.empty() && def->sort != SortMode::Effect &&
        cache.mesh(def->model).valid) {
        int s = ss.kind == ShadowKind::Planar ? 0 : steps;
        std::string key = normalizePath(def->model) + "||" + std::to_string(static_cast<int>(ss.kind)) + "|" +
                          std::to_string(static_cast<int>(ss.quality)) + "|" + std::to_string(s);
        if (seen.insert(key).second) out.push_back({key, def, ss, s});
    }
    for (const AttachDef& at : def->attachments) {
        if (at.nightOnly && !night) continue;
        if (db.findParticleSystem(at.targetName)) continue;
        collectKeys(db, cache, db.findObject(at.targetName), steps, night, depth + 1, seen, out);
    }
}

} // namespace

TEST_CASE("shadow raster: synthetic boxes match the GL silhouette") {
    GraphicsContext* ctx = rasterGlContext();
    if (!ctx) {
        std::fprintf(stderr, "SKIPPED (no headless GLES context available): %s\n", __FILE__);
        return;
    }
    ShadowRenderer sr;
    std::string err;
    REQUIRE_MESSAGE(sr.init(&err), err);
    const RenderRules rules = renderRules(GameId::AirStrike3D);

    // A skin whose left half is transparent and right half half-transparent.
    Image skinImg;
    skinImg.width = skinImg.height = 16;
    skinImg.hasAlpha = true;
    skinImg.rgba.assign(16 * 16 * 4, 200);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) skinImg.rgba[(static_cast<size_t>(y) * 16 + x) * 4 + 3] = x < 8 ? 0 : 128;
    Texture2D skinTex;
    TextureOptions opts;
    opts.mipmaps = true;
    skinTex.create(skinImg, opts);
    ShadowSkin skin(skinImg);
    CHECK_FALSE(skin.opaque());

    struct Case { const char* name; ModelData model; ShadowKind kind; Vec3 sun; int steps; bool textured; };
    const Vec3 sun = normalize(Vec3{0.6f, -0.3f, 1.0f});
    const Case cases[] = {
        {"planar box", makeBox(12.0f, 7.0f, 0.0f, 10.0f), ShadowKind::Planar, sun, 0, false},
        {"projected box, rotated", makeBox(12.0f, 7.0f, 0.0f, 10.0f), ShadowKind::Projected, sun, 2, false},
        {"projected box, half sunk", makeBox(9.0f, 9.0f, -6.0f, 6.0f), ShadowKind::Projected, sun, 5, false},
        {"projected box, sunk", makeBox(9.0f, 9.0f, -6.0f, 0.0f), ShadowKind::Projected, sun, 0, false},
        {"planar box, skin alpha", makeBox(12.0f, 7.0f, 0.0f, 10.0f), ShadowKind::Planar, sun, 0, true},
        {"projected box, skin alpha", makeBox(12.0f, 7.0f, 1.0f, 10.0f), ShadowKind::Projected, sun, 1, true},
    };
    for (const Case& c : cases) {
        ShadowMap gl;
        REQUIRE(sr.generate(c.model, c.textured ? &skinTex : nullptr, c.kind, c.sun, c.steps, ShadowQuality::Normal, gl));
        ShadowSilhouette cpu;
        REQUIRE(rasterizeShadowSilhouette(c.model, c.textured ? &skin : nullptr, c.kind, c.sun, c.steps,
                                          ShadowQuality::Normal, rules, cpu));
        REQUIRE(cpu.width == gl.width);
        REQUIRE(cpu.height == gl.height);
        CHECK(cpu.bounds.xmin == gl.bounds.xmin);
        CHECK(cpu.bounds.ymax == gl.bounds.ymax);
        Diff d = compare(gl.image, cpu.image);
        std::printf("shadow raster: %-28s %3dx%-3d mean |da| %.3f/255 max %3d IoU %.4f\n", c.name, gl.width,
                    gl.height, d.meanAbs, d.maxAbs, d.iou);
        INFO(c.name);
        CHECK(d.meanAbs <= 1.0);
        CHECK(d.iou >= 0.99);
        for (size_t i = 0; i < cpu.image.rgba.size(); i += 4) {
            CHECK(cpu.image.rgba[i] == 114);
            CHECK(cpu.image.rgba[i + 3] <= 102);
        }
    }
}

TEST_CASE("shadow raster: mission 1 silhouettes match the GL ones") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    AS3D_REQUIRE_FIRST_GAME("mission 1 objects");
    GraphicsContext* ctx = rasterGlContext();
    if (!ctx) {
        std::fprintf(stderr, "SKIPPED (no headless GLES context available): %s\n", __FILE__);
        return;
    }
    Vfs vfs;
    vfs.mount(makeDirSource(testdata::extractedDir()));
    DefDatabase db;
    REQUIRE(db.load(vfs));
    World w;
    w.init(vfs, db, WorldConfig());
    std::string err;
    REQUIRE_MESSAGE(w.loadLevel("1", &err), err);
    const Terrain* t = w.terrain();
    REQUIRE(t);
    REQUIRE(t->level());
    const TerrainStyle& st = t->style();
    // WorldRenderer::beginLevel's sun and precache walk.
    Vec3 sun{st.sun[3], st.sun[4], st.sun[5]};
    const Vec3 towardsSun = length(sun) > 1e-6f ? normalize(sun) : Vec3{0.0f, 0.0f, 1.0f};
    ResourceCache cache(vfs);
    std::set<std::string> seen;
    std::vector<ShadowKey> keys;
    const LevelData& level = *t->level();
    for (const Placement& p : level.placements)
        collectKeys(db, cache, db.findObject(level.typeName(p)), p.rotationSteps, st.night, 0, seen, keys);
    std::printf("shadow raster: mission 1 precaches %zu silhouettes\n", keys.size());
    REQUIRE(keys.size() >= 20);

    ShadowRenderer sr;
    REQUIRE_MESSAGE(sr.init(&err), err);
    const RenderRules rules = renderRules(GameId::AirStrike3D);
    sr.setRules(rules);

    // Skins as Material::fromObjectDef picks them, uploaded like ResourceCache (mipmapped).
    struct Skin {
        bool ok = false;
        Texture2D tex;
        std::unique_ptr<ShadowSkin> cpu;
    };
    std::map<std::string, Skin> skins;
    double cpuSeconds = 0.0, skinSeconds = 0.0;
    int compared = 0, alphaSkins = 0;
    double worstMean = 0.0, worstIou = 1.0;
    for (const ShadowKey& k : keys) {
        const GpuMesh& mesh = cache.mesh(k.def->model);
        std::string skinPath = k.def->skin.empty() ? mesh.model.sourceTexturePath : k.def->skin;
        Skin& sk = skins[normalizePath(skinPath)];
        if (!sk.cpu) {
            Blob blob;
            Image img;
            sk.ok = !skinPath.empty() && vfs.read(skinPath, blob) && decodeTga(blob.data(), blob.size(), img);
            auto t0 = std::chrono::steady_clock::now();
            sk.cpu.reset(sk.ok ? new ShadowSkin(img) : new ShadowSkin());
            skinSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (sk.ok) {
                TextureOptions opts;
                opts.mipmaps = true;
                sk.tex.create(img, opts);
            }
        }
        // A missing skin is the opaque placeholder in the game: no skin on both sides.
        const Texture2D* glSkin = sk.ok ? &sk.tex : nullptr;
        const ShadowSkin* cpuSkin = sk.ok ? sk.cpu.get() : nullptr;
        if (cpuSkin && !cpuSkin->opaque()) alphaSkins++;

        ShadowMap gl;
        REQUIRE(sr.generate(mesh.model, glSkin, k.spec.kind, towardsSun, k.steps, k.spec.quality, gl));
        ShadowSilhouette cpu;
        auto t0 = std::chrono::steady_clock::now();
        REQUIRE(rasterizeShadowSilhouette(mesh.model, cpuSkin, k.spec.kind, towardsSun, k.steps, k.spec.quality, rules,
                                          cpu));
        double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        cpuSeconds += dt;
        REQUIRE(cpu.width == gl.width);
        REQUIRE(cpu.height == gl.height);
        Diff d = compare(gl.image, cpu.image);
        std::printf("shadow raster: %-48s %4zu tris %3dx%-3d %s mean |da| %.3f/255 max %3d IoU %.4f cpu %.2f ms\n",
                    k.key.c_str(), mesh.model.faces.size(), gl.width, gl.height,
                    cpuSkin && !cpuSkin->opaque() ? "alpha" : "     ", d.meanAbs, d.maxAbs, d.iou, dt * 1e3);
        INFO(k.key);
        CHECK(d.meanAbs <= 4.0);
        CHECK(d.iou >= 0.97);
        worstMean = std::max(worstMean, d.meanAbs);
        worstIou = std::min(worstIou, d.iou);
        compared++;
    }
    std::printf("shadow raster: %d silhouettes (%d with skin alpha), worst mean |da| %.3f/255, worst IoU %.4f\n",
                compared, alphaSkins, worstMean, worstIou);
    std::printf("shadow raster: CPU %.1f ms for the level (%.2f ms per silhouette) + %.1f ms of skin mipmaps;"
                " x4 for the 3DS: %.2f s against the 1.0 s budget\n",
                cpuSeconds * 1e3, cpuSeconds * 1e3 / compared, skinSeconds * 1e3, 4.0 * (cpuSeconds + skinSeconds));
}

