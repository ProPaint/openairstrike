// Web spike, Stage A (docs/web-spike.md): renders level 1 of the original game in the page's
// canvas, the view of `as3d_viewer level 1 --scroll 500`, then scrolls slowly so that every
// frame differs. Each frame runs the viewer's renderLevel (apps/viewer/level_render.cpp, which
// rebuilds the level every call and renders into an offscreen 4x MSAA target) and blits the
// result to the canvas. Arguments (from the page's query string): --scroll Y, --hold. The paks come from the file packager's /data directory.
//
// Log lines (the browser console):
//   AS3D_WEB_GL <renderer description>
//   AS3D_WEB_FIRST_FRAME page_ms=T render_ms=R    T from navigation start
//   AS3D_WEB_LEVEL_PERF frames=N avg_ms=A max_ms=M scroll=S    every 5 s
#include <GLES3/gl3.h>
#include <emscripten.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "as3d/core.h"
#include "as3d/defs.h"
#include "as3d/image.h"
#include "as3d/platform.h"
#include "as3d/scene.h"
#include "as3d/vfs.h"
#include "level_render.h"

using namespace as3d;

namespace {

struct App {
    std::unique_ptr<GraphicsContext> gl;
    Vfs vfs;
    DefDatabase db;
    std::unique_ptr<ResourceCache> cache;
    MeshRenderer renderer;
    viewer::LevelRenderOptions opts;
    unsigned tex = 0, fbo = 0;
    long frames = 0;
    bool hold = false; // --hold: stay at the start scroll (screenshot comparisons)
    double windowStart = -1, sum = 0, max = 0;
    int windowFrames = 0;
};

void frame(void* arg) {
    App& a = *static_cast<App*>(arg);
    a.gl->pumpEvents();
    const double t0 = emscripten_get_now();
    Image img;
    std::string err;
    if (!viewer::renderLevel(a.vfs, a.db, *a.cache, a.renderer, "1", a.opts, img, nullptr, err)) {
        AS3D_ERROR("FATAL: renderLevel: %s", err.c_str());
        emscripten_cancel_main_loop();
        return;
    }
    // The image is top-down; the blit flips it into the canvas's bottom-up rows.
    glBindTexture(GL_TEXTURE_2D, a.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, a.fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, a.tex, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    const int w = a.gl->width(), h = a.gl->height();
    glViewport(0, 0, w, h);
    glBlitFramebuffer(0, 0, img.width, img.height, 0, h, w, 0, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    a.gl->swapBuffers();
    const double t1 = emscripten_get_now();
    const double ms = t1 - t0;
    if (a.frames == 0) {
        AS3D_INFO("AS3D_WEB_FIRST_FRAME page_ms=%.0f render_ms=%.1f size=%dx%d", t1, ms, w, h);
        a.windowStart = t1;
    } else {
        a.sum += ms;
        a.max = std::max(a.max, ms);
        ++a.windowFrames;
        if (t1 - a.windowStart >= 5000.0) {
            AS3D_INFO("AS3D_WEB_LEVEL_PERF frames=%d avg_ms=%.1f max_ms=%.1f fps=%.1f scroll=%.1f", a.windowFrames,
                      a.sum / a.windowFrames, a.max, 1000.0 * a.windowFrames / (t1 - a.windowStart),
                      static_cast<double>(a.opts.scroll));
            a.windowStart = t1;
            a.sum = a.max = 0;
            a.windowFrames = 0;
        }
    }
    ++a.frames;
    if (!a.hold) a.opts.scroll += 0.5f; // the game scrolls about 0.5 world units per 60 Hz step
}

} // namespace

int main(int argc, char** argv) {
    App* app = new App(); // lives for the page's lifetime: the browser owns the loop
    App& a = *app;
    a.opts.scroll = 500.0f;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--hold") a.hold = true;
        else if (std::string(argv[i]) == "--scroll" && i + 1 < argc) a.opts.scroll = static_cast<float>(std::atof(argv[++i]));
    }
    GraphicsConfig gc;
    gc.headless = false;
    gc.width = a.opts.width;
    gc.height = a.opts.height;
    gc.title = "AirStrike 3D level (web spike)";
    a.gl = createGraphicsContext(gc);
    if (!a.gl) {
        AS3D_ERROR("FATAL: no WebGL 2 context");
        return 3;
    }
    a.gl->makeCurrent();
    AS3D_INFO("AS3D_WEB_GL %s", a.gl->description().c_str());
    bool any = false;
    for (const char* name : {"/data/pak0.apk", "/data/pak1.apk", "/data/pak2.apk"}) {
        std::unique_ptr<IStream> s = openPlatformStream(name);
        std::unique_ptr<IFileSource> src = s ? makePakSource(std::move(s)) : nullptr;
        if (!src) continue;
        a.vfs.mount(std::move(src));
        any = true;
    }
    if (!any || !a.db.load(a.vfs)) {
        AS3D_ERROR("FATAL: no game data under /data");
        return 1;
    }
    std::string err;
    if (!a.renderer.init(&err)) {
        AS3D_ERROR("FATAL: renderer: %s", err.c_str());
        return 1;
    }
    a.cache.reset(new ResourceCache(a.vfs));
    glGenTextures(1, &a.tex);
    glBindTexture(GL_TEXTURE_2D, a.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenFramebuffers(1, &a.fbo);
    emscripten_set_main_loop_arg(frame, app, 0, true);
    return 0;
}
