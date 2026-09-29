// GLES 3.0 context creation. This header has no SDL or EGL types in it: it is safe to
// include from anywhere, including the platform-independent `render` module (which
// only needs *a* current context, not how it was made).
//
// Two backends exist behind createGraphicsContext(): a headless EGL context (renders
// into a pbuffer surface; the actual pixels are read back through as3d::RenderTarget,
// see as3d/gfx.h) and an SDL2 window (desktop only for now). Which one you get is
// selected by GraphicsConfig::headless. On Android only the window backend exists (a
// full-screen SDL window). Both, when creation succeeds, leave a current
// OpenGL ES 3.0 context ready for the `render` module to use.
#pragma once

#include <memory>
#include <string>

#include "as3d/vfs.h"

namespace as3d {

struct GraphicsConfig {
    int width = 640;
    int height = 480;
    // true: EGL pbuffer context, no window system, no visible output.
    // false: an on-screen SDL2 window (on Android always full screen, immersive).
    bool headless = true;
    bool fullscreen = false;   // desktop: full-screen desktop mode
    bool resizable = false;
    bool vsync = true;
    const char* title = "as3d";
};

// Owns a GLES 3.0 context (and, for the windowed backend, the window it belongs to).
class GraphicsContext {
public:
    virtual ~GraphicsContext() = default;

    // Makes this context current on the calling thread. Safe to call again (e.g. after
    // another context was made current on the same thread).
    virtual bool makeCurrent() = 0;

    // Presents the default framebuffer. A no-op (but harmless) for the headless
    // backend, whose "default framebuffer" is a pbuffer nothing ever looks at --
    // render into an as3d::RenderTarget and read it back instead.
    virtual void swapBuffers() = 0;

    // Size of the default framebuffer (the pbuffer, or the window's drawable size).
    virtual int width() const = 0;
    virtual int height() const = 0;

    // "<backend>: vendor=... renderer=... version=..." -- human-readable, logged by
    // `as3d_viewer info` and useful in bug reports to know which GL implementation
    // actually ran a test.
    virtual std::string description() const = 0;

    // Processes window-system events. Returns false once the user asked to close the
    // window (e.g. clicked the close button); the headless backend has no events and
    // always returns true. Callers that only render headless never need to call this.
    virtual bool pumpEvents() { return true; }

    // The windowed backend on Android: after SDL_RENDER_DEVICE_RESET (SDL replaced a lost
    // EGL context with a new, current one), take over the new context. Every GL object of
    // the old context is gone and must be recreated by the caller.
    virtual void adoptCurrentContext() {}
};

// Creates a context per `config`. Returns null and logs the reason (AS3D_ERROR) on
// failure -- e.g. no EGL device available, or no display for the windowed backend.
std::unique_ptr<GraphicsContext> createGraphicsContext(const GraphicsConfig& config);

// A read-only byte stream through SDL_RWops (engine/src/platform/rw_stream.cpp). On desktop
// `path` is a file path. On Android a relative path names an APK asset, read in place
// through the asset manager (no copy to storage); keep such assets stored uncompressed so
// seeks are cheap. Returns null and logs the reason on failure. Reads are serialised.
std::unique_ptr<IStream> openPlatformStream(const std::string& path);

// Reads a whole file through the same mechanism (a file path, or an APK asset on Android).
// Returns false without logging when it does not exist (for optional files). At most 64 MB.
bool readPlatformFile(const std::string& path, Blob& out);

// Directory for per-user data (the profile), created if missing, with a trailing '/'.
// Desktop: $XDG_DATA_HOME/airstrike3d, else ~/.local/share/airstrike3d (the current directory
// when neither can be used). Android: the app's internal files directory. Web (Emscripten):
// /persist, which the page backs with browser storage. Empty on failure.
std::string userDataDir();

} // namespace as3d
