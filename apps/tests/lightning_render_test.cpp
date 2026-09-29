// Lightning bolts (render-pipeline.md 7.1): geometry of the two crossed quads and a draw into
// an offscreen target. Draw tests skip loudly without a headless GLES context or game data.
#include "doctest.h"

#include <cmath>
#include <cstdio>
#include <memory>

#include "as3d/gfx.h"
#include "as3d/lightning_render.h"
#include "as3d/platform.h"
#include "as3d/scene.h"
#include "as3d/vfs.h"
#include "test_data.h"

using namespace as3d;

namespace {

GraphicsContext* lightningContext() {
    static std::unique_ptr<GraphicsContext> ctx = [] {
        GraphicsConfig cfg;
        cfg.width = 64;
        cfg.height = 64;
        cfg.headless = true;
        return createGraphicsContext(cfg);
    }();
    if (ctx) ctx->makeCurrent();
    return ctx.get();
}

} // namespace

TEST_CASE("lightning: perpendicular vector") {
    const Vec3 dirs[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, normalize(Vec3{1, 2, 3}), normalize(Vec3{-5, 0.1f, 0.2f})};
    for (const Vec3& d : dirs) {
        Vec3 p = perpendicularVector(d);
        CHECK(std::fabs(dot(p, d)) < 1e-5f);
        CHECK(length(p) == doctest::Approx(1.0f));
    }
    // The axis of the smallest component: for (0, 1, 0) the first smallest is x.
    Vec3 p = perpendicularVector({0, 1, 0});
    CHECK(p.x == doctest::Approx(1.0f));
}

TEST_CASE("lightning: two crossed quads 16 units wide, texture along the bolt") {
    LightningVertex v[8];
    CHECK_FALSE(buildLightningQuads({1, 2, 3}, {1, 2, 3}, 0.0f, v));
    const Vec3 a{100, 200, 0}, b{100, 392, 0}; // 192 units: the texture repeats twice
    REQUIRE(buildLightningQuads(a, b, 0.0f, v));
    for (int q = 0; q < 2; ++q) {
        const LightningVertex* k = v + 4 * q;
        CHECK(length(k[0].pos - k[3].pos) == doctest::Approx(16.0f));
        CHECK(length(k[1].pos - k[2].pos) == doctest::Approx(16.0f));
        CHECK(length((k[0].pos + k[3].pos) * 0.5f - a) < 1e-4f);
        CHECK(length((k[1].pos + k[2].pos) * 0.5f - b) < 1e-4f);
        CHECK(k[0].uv.x == doctest::Approx(0.0f));
        CHECK(k[1].uv.x == doctest::Approx(2.0f));
        CHECK(k[0].uv.y != k[3].uv.y);
    }
    // The two quads are crossed: their width directions are orthogonal to each other.
    Vec3 w0 = v[3].pos - v[0].pos, w1 = v[7].pos - v[4].pos;
    CHECK(std::fabs(dot(w0, w1)) < 1e-3f);
    // The texture scrolls by three repeats per second.
    LightningVertex s[8];
    REQUIRE(buildLightningQuads(a, b, 0.1f, s));
    CHECK(s[0].uv.x == doctest::Approx(0.3f));
    CHECK(s[1].uv.x - s[0].uv.x == doctest::Approx(2.0f));
}

TEST_CASE("lightning: a bolt is drawn additively over the scene") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    GraphicsContext* ctx = lightningContext();
    if (!ctx) {
        std::fprintf(stderr, "SKIPPED (no headless GLES context available): %s\n", __FILE__);
        return;
    }
    Vfs vfs;
    vfs.mount(makeDirSource(testdata::extractedDir()));
    ResourceCache cache(vfs);
    LightningRenderer lr;
    std::string err;
    REQUIRE_MESSAGE(lr.init(&err), err);
    RenderTarget target;
    REQUIRE(target.create(128, 128, 0));
    target.bind();
    setViewport(0, 0, 128, 128);
    clear({0, 0, 0, 1}, true);
    LightningViewParams p;
    // Looking down -z from z = 200 at the origin.
    p.view = translation(Vec3{0, 0, -200});
    p.projection = perspective(60.0f, 1.0f, 4.0f, 1000.0f);
    const Vec3 st{-80, 0, 0}, en{80, 0, 0};
    lr.draw(&st, &en, 1, cache.texture("gfx\\lightning2.tga").texture, p);
    CHECK(lr.lastBoltCount() == 1);
    Image img;
    REQUIRE(target.readPixels(img));
    int litMiddle = 0, litFar = 0;
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            const u8* px = &img.rgba[(static_cast<size_t>(y) * img.width + x) * 4];
            bool lit = px[0] + px[1] + px[2] > 60;
            if (std::abs(y - img.height / 2) <= 3) litMiddle += lit ? 1 : 0;
            if (std::abs(y - img.height / 2) > 20) litFar += lit ? 1 : 0;
        }
    }
    CHECK(litMiddle > 100);
    CHECK(litFar == 0);
}
