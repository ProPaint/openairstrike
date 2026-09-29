// `as3d_viewer level <level number | levels.txt id | maps\x.hsc> --out file.png [--scroll y]
// [--size WxH] [--overview [--span y]] [--camera 0..3] [--camx x] [--no-objects]`: renders a
// level of the original game headless, as the game would show it at scroll position `y`
// (g_map_pos), or, with --overview, from high above.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "as3d/gfx.h"
#include "level_render.h"
#include "registry.h"
#include "viewer_scene.h"

namespace {

int run(int argc, char** argv) {
    viewer::LevelRenderOptions o;
    std::string out, level;
    bool sizeGiven = false;
    for (int i = 0; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&](const char* name) -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "error: %s needs a value\n", name); return nullptr; }
            return argv[++i];
        };
        const char* v = nullptr;
        if (a == "--out") { if (!(v = val("--out"))) return 1; out = v; }
        else if (a == "--scroll") { if (!(v = val("--scroll"))) return 1; o.scroll = static_cast<float>(std::atof(v)); }
        else if (a == "--span") { if (!(v = val("--span"))) return 1; o.span = static_cast<float>(std::atof(v)); }
        else if (a == "--camera") { if (!(v = val("--camera"))) return 1; o.cameraMode = std::atoi(v); }
        else if (a == "--camx") { if (!(v = val("--camx"))) return 1; o.cameraX = static_cast<float>(std::atof(v)); }
        else if (a == "--size") {
            if (!(v = val("--size"))) return 1;
            const char* x = std::strpbrk(v, "xX");
            if (!x || std::atoi(v) <= 0 || std::atoi(x + 1) <= 0) { std::fprintf(stderr, "error: bad --size '%s'\n", v); return 1; }
            o.width = std::atoi(v);
            o.height = std::atoi(x + 1);
            sizeGiven = true;
        }
        else if (a == "--overview") o.overview = true;
        else if (a == "--no-objects") o.objects = false;
        else if (level.empty()) level = a;
        else { std::fprintf(stderr, "error: unexpected argument '%s'\n", a.c_str()); return 1; }
    }
    if (level.empty() || out.empty()) {
        std::fprintf(stderr, "usage: as3d_viewer level <level number|id|maps\\x.hsc> --out file.png [--scroll y] [--size WxH] "
                             "[--overview [--span y]] [--camera 0..3] [--camx x] [--no-objects]\n");
        return 1;
    }
    if (o.overview && !sizeGiven) {
        // The map is 1280 wide and up to 10240 long: default to a tall image of the same shape.
        o.width = 400;
        o.height = 3200;
        if (o.span > 0.0f) o.height = std::min(4096, std::max(300, static_cast<int>(400.0f * o.span / 1280.0f)));
    }
    viewer::ViewerContext ctx;
    std::string err;
    if (!ctx.init(o.width, o.height, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    as3d::Image img;
    viewer::LevelRenderStats stats;
    if (!viewer::renderLevel(ctx.vfs, ctx.db, *ctx.cache, ctx.renderer, level, o, img, &stats, err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    if (!as3d::writePng(out.c_str(), img)) { std::fprintf(stderr, "error: cannot write %s\n", out.c_str()); return 1; }
    for (const std::string& w : ctx.cache->warnings()) std::fprintf(stderr, "warning: %s\n", w.c_str());
    std::printf("wrote %s (%dx%d): level %s, %d placements, %d object parts, %d terrain chunks, water %s, %d missing terrain textures\n",
                out.c_str(), img.width, img.height, stats.levelId.c_str(), stats.placements, stats.drawnObjects,
                stats.visibleChunks, stats.hasWater ? "yes" : "no", stats.missingTextures);
    return 0;
}

} // namespace

AS3D_VIEWER_COMMAND("level", "render a level (terrain, water, objects) from the game camera or from above", run);
