// Shared helpers for the headless 2D render tests (front end): a GLES context, the game paks,
// and pixel statistics. Tests using them skip loudly without game data or a GLES context.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "as3d/gfx.h"
#include "as3d/image.h"
#include "as3d/platform.h"
#include "as3d/vfs.h"
#include "test_data.h"

namespace uitest {

inline as3d::GraphicsContext* context() {
    static std::unique_ptr<as3d::GraphicsContext> ctx = [] {
        as3d::GraphicsConfig cfg;
        cfg.width = 64;
        cfg.height = 64;
        cfg.headless = true;
        cfg.title = "as3d_tests_frontend";
        return as3d::createGraphicsContext(cfg);
    }();
    if (ctx) ctx->makeCurrent();
    return ctx.get();
}

inline bool mountPaks(as3d::Vfs& vfs) {
    const std::string dir = testdata::installDir() + "/data";
    bool any = false;
    for (const char* n : {"pak0.apk", "pak1.apk", "pak2.apk"}) {
        auto src = as3d::makePakSource(as3d::openFileStream(dir + "/" + n));
        if (src) { vfs.mount(std::move(src)); any = true; }
    }
    return any;
}

// Pixels differing from `bg` by more than a small threshold, inside [x0,x1) x [y0,y1).
inline long litIn(const as3d::Image& img, const as3d::u8 bg[3], int x0, int y0, int x1, int y1) {
    long n = 0;
    for (int y = std::max(y0, 0); y < std::min(y1, img.height); y++)
        for (int x = std::max(x0, 0); x < std::min(x1, img.width); x++) {
            const as3d::u8* p = &img.rgba[(static_cast<size_t>(y) * img.width + x) * 4];
            n += std::abs(p[0] - bg[0]) + std::abs(p[1] - bg[1]) + std::abs(p[2] - bg[2]) > 24;
        }
    return n;
}

} // namespace uitest

// Put at the top of a render test after AS3D_REQUIRE_DATA().
#define AS3D_REQUIRE_GLES()                                                                   \
    if (!uitest::context()) {                                                                 \
        std::fprintf(stderr, "SKIPPED (no headless GLES context available): %s\n", __FILE__); \
        return;                                                                               \
    }
