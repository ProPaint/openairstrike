// Windowed backend: an SDL2 window with a GLES 3.0 context. Desktop only; requires a
// reachable display (X11/Wayland). This is expected to fail gracefully (return null)
// in a headless CI shell with no DISPLAY -- that is normal, not a bug: use the
// headless EGL backend (egl_headless.cpp) for automated testing.
//
// Desktop only. On Android this file is excluded from the build (see module.cmake);
// android_stub.cpp stands in for it there.
#include "backends.h"

#include <SDL.h>
#include <GLES3/gl3.h>

#include <cstdio>
#include <memory>
#include <string>

#include "as3d/core.h"

namespace as3d {
namespace {

class SdlWindowContext final : public GraphicsContext {
public:
    ~SdlWindowContext() override {
        if (glContext_) SDL_GL_DeleteContext(glContext_);
        if (window_) SDL_DestroyWindow(window_);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    bool makeCurrent() override { return SDL_GL_MakeCurrent(window_, glContext_) == 0; }
    void swapBuffers() override { SDL_GL_SwapWindow(window_); }

    int width() const override {
        int w = 0, h = 0;
        SDL_GL_GetDrawableSize(window_, &w, &h);
        return w;
    }
    int height() const override {
        int w = 0, h = 0;
        SDL_GL_GetDrawableSize(window_, &w, &h);
        return h;
    }

    std::string description() const override { return description_; }

    bool pumpEvents() override {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return false;
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) return false;
        }
        return true;
    }

    SDL_Window* window_ = nullptr;
    SDL_GLContext glContext_ = nullptr;
    std::string description_;
};

} // namespace

std::unique_ptr<GraphicsContext> createWindowContext(const GraphicsConfig& config) {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        AS3D_ERROR("windowed SDL: SDL_InitSubSystem(SDL_INIT_VIDEO) failed: %s", SDL_GetError());
        return nullptr;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI;
    SDL_Window* window = SDL_CreateWindow(config.title ? config.title : "as3d", SDL_WINDOWPOS_CENTERED,
                                           SDL_WINDOWPOS_CENTERED, config.width, config.height, flags);
    if (!window) {
        AS3D_ERROR("windowed SDL: SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return nullptr;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        AS3D_ERROR("windowed SDL: SDL_GL_CreateContext failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return nullptr;
    }

    SDL_GL_MakeCurrent(window, glContext);
    SDL_GL_SetSwapInterval(config.vsync ? 1 : 0);

    auto ctx = std::make_unique<SdlWindowContext>();
    ctx->window_ = window;
    ctx->glContext_ = glContext;

    const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    char buf[512];
    std::snprintf(buf, sizeof buf, "sdl-window: vendor=%s renderer=%s version=%s", vendor ? vendor : "?",
                  renderer ? renderer : "?", version ? version : "?");
    ctx->description_ = buf;
    AS3D_INFO("%s", ctx->description_.c_str());
    return ctx;
}

} // namespace as3d
