// Windowed backend: an SDL2 window with a GLES 3.0 context. On desktop it requires a
// reachable display (X11/Wayland) and is expected to fail gracefully (return null) in a
// headless CI shell with no DISPLAY -- use the headless EGL backend (egl_headless.cpp)
// for automated testing. On Android it is the only backend: a full-screen window on the
// activity's surface (SDL makes it immersive and keeps the EGL context across pauses).
#include "backends.h"

#include <SDL.h>
#include <GLES3/gl3.h>
#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#endif

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
    void adoptCurrentContext() override {
        SDL_GLContext cur = SDL_GL_GetCurrentContext();
        if (cur) glContext_ = cur;
    }
    void swapBuffers() override { SDL_GL_SwapWindow(window_); }

    int width() const override {
        int w = 0, h = 0;
        drawableSize(w, h);
        return w;
    }
    int height() const override {
        int w = 0, h = 0;
        drawableSize(w, h);
        return h;
    }
    void drawableSize(int& w, int& h) const {
#ifdef __EMSCRIPTEN__
        // SDL's Emscripten port reports the window size here; the page sizes the canvas'
        // drawing buffer itself (device pixel ratio, resizes), so ask the canvas.
        if (emscripten_get_canvas_element_size("#canvas", &w, &h) == EMSCRIPTEN_RESULT_SUCCESS && w > 0 && h > 0) return;
#endif
        SDL_GL_GetDrawableSize(window_, &w, &h);
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
#ifdef __EMSCRIPTEN__
    // An opaque canvas: with alpha the compositor would blend the page behind it wherever
    // drawing leaves alpha below 1.
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
#endif
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI;
#ifdef __ANDROID__
    flags |= SDL_WINDOW_FULLSCREEN;
#else
    if (config.fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (config.resizable) flags |= SDL_WINDOW_RESIZABLE;
#endif
    SDL_Window* window = SDL_CreateWindow(config.title ? config.title : "as3d", SDL_WINDOWPOS_CENTERED,
                                           SDL_WINDOWPOS_CENTERED, config.width, config.height, flags);
    if (!window) {
        AS3D_ERROR("windowed SDL: SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return nullptr;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        // Some mobile GPUs offer no 24-bit depth buffer with this configuration.
        AS3D_WARN("windowed SDL: no context with a 24-bit depth buffer (%s), trying 16", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
        window = SDL_CreateWindow(config.title ? config.title : "as3d", SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED, config.width, config.height, flags);
        glContext = window ? SDL_GL_CreateContext(window) : nullptr;
    }
    if (!glContext) {
        AS3D_ERROR("windowed SDL: SDL_GL_CreateContext failed: %s", SDL_GetError());
        if (window) SDL_DestroyWindow(window);
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
