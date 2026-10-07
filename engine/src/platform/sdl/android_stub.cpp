// Android: there is no headless backend (no pbuffer-only EGL use on the device); the
// windowed backend (sdl_window.cpp) is the Android backend. This stand-in fails loudly.
//
// Desktop excludes this file from the build (see module.cmake).
#include "backends.h"

#include "as3d/core.h"

namespace as3d {

std::unique_ptr<GraphicsContext> createHeadlessContext(const GraphicsConfig&) {
    AS3D_WARN("createHeadlessContext: no headless backend on Android; use the window backend");
    return nullptr;
}

} // namespace as3d
