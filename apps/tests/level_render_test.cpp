// Headless render of real levels through the viewer's level renderer (apps/viewer/
// level_render.cpp, compiled into this test by including it): checks that terrain, water and
// objects put non-trivial, correctly-coloured pixels on screen. Skips loudly without game
// data or without a headless GLES context.
#include "doctest.h"

#include "as3d/platform.h"
#include "as3d/scene.h"

#include "as3d/defs.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <set>

#include "../viewer/level_render.cpp"
#include "test_data.h"

using namespace as3d;

namespace {

struct Stats {
    int total = 0, magenta = 0, background = 0;
    std::set<unsigned> colours;
};

Stats analyse(const Image& img, const unsigned char* bg) {
    Stats s;
    s.total = img.width * img.height;
    for (int i = 0; i < s.total; i++) {
        int r = img.rgba[i * 4], g = img.rgba[i * 4 + 1], b = img.rgba[i * 4 + 2];
        if (r > 225 && g < 40 && b > 225) s.magenta++;
        if (std::abs(r - bg[0]) < 3 && std::abs(g - bg[1]) < 3 && std::abs(b - bg[2]) < 3) s.background++;
        s.colours.insert(static_cast<unsigned>((r >> 3) << 10 | (g >> 3) << 5 | (b >> 3)));
    }
    return s;
}

} // namespace

TEST_CASE("level renderer draws terrain, water and objects") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    static std::unique_ptr<GraphicsContext> ctx = [] {
        GraphicsConfig cfg;
        cfg.width = 320;
        cfg.height = 240;
        cfg.headless = true;
        return createGraphicsContext(cfg);
    }();
    if (!ctx) {
        std::fprintf(stderr, "SKIPPED (no headless GLES context available): %s\n", __FILE__);
        return;
    }
    ctx->makeCurrent();

    Vfs vfs;
    for (const char* name : {"pak0.apk", "pak1.apk", "pak2.apk"}) {
        auto src = makePakSource(openFileStream(testdata::installDir() + "/data/" + name));
        REQUIRE(src);
        vfs.mount(std::move(src));
    }
    DefDatabase db;
    REQUIRE(db.load(vfs));
    ResourceCache cache(vfs);
    MeshRenderer renderer;
    std::string error;
    REQUIRE(renderer.init(&error));

    struct Case { const char* level; float scroll; bool overview; bool expectWater; };
    const Case cases[] = {{"1", 2000.0f, false, true}, {"12", 2500.0f, false, false}, {"1", 0.0f, true, true}};
    for (const Case& c : cases) {
        INFO("level ", c.level, " scroll ", c.scroll, c.overview ? " overview" : "");
        viewer::LevelRenderOptions o;
        o.width = c.overview ? 96 : 320;
        o.height = c.overview ? 768 : 240;
        o.scroll = c.scroll;
        o.overview = c.overview;
        o.msaa = 0;
        Image img;
        viewer::LevelRenderStats st;
        REQUIRE_MESSAGE(viewer::renderLevel(vfs, db, cache, renderer, c.level, o, img, &st, error), error);
        CHECK(st.missingTextures == 0);
        CHECK(st.hasWater == c.expectWater);
        CHECK(st.drawnObjects > 10);
        REQUIRE(img.width == o.width);
        // Clear colour is the fog colour (game view) or the fixed overview colour: terrain
        // must cover nearly the whole frame, so few pixels may equal it.
        const unsigned char bgOverview[3] = {13, 13, 20};
        Stats s = analyse(img, bgOverview);
        if (c.overview) CHECK(s.background < s.total / 20);
        CHECK(s.colours.size() > 200);
        CHECK(s.magenta == 0);
    }
}
