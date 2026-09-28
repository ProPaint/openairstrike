// `as3d_viewer info`: prints the context description (vendor/renderer/version, and
// for the headless backend which EGL platform was used) for both backends, so a bug
// report can say exactly what ran.
#include <cstdio>

#include "as3d/platform.h"
#include "registry.h"

namespace {

int run(int, char**) {
    {
        as3d::GraphicsConfig cfg;
        cfg.headless = true;
        cfg.title = "as3d_viewer info (headless)";
        auto ctx = as3d::createGraphicsContext(cfg);
        if (ctx) {
            ctx->makeCurrent();
            std::printf("headless: %s\n", ctx->description().c_str());
        } else {
            std::printf("headless: unavailable\n");
        }
    }
    {
        as3d::GraphicsConfig cfg;
        cfg.headless = false;
        cfg.title = "as3d_viewer info (window)";
        auto ctx = as3d::createGraphicsContext(cfg);
        if (ctx) {
            ctx->makeCurrent();
            std::printf("window: %s\n", ctx->description().c_str());
        } else {
            std::printf("window: unavailable (no display, or SDL init failed)\n");
        }
    }
    return 0;
}

} // namespace

AS3D_VIEWER_COMMAND("info", "print headless/window context descriptions", run);
