// Private to the platform module: the two concrete backends behind
// as3d::createGraphicsContext(). Not part of the public API.
#pragma once

#include <memory>

#include "as3d/platform.h"

namespace as3d {

// EGL pbuffer context, no window system. Implemented in egl_headless.cpp on desktop;
// android_stub.cpp provides a null-returning stand-in on Android until WP-19's
// counterpart there lands.
std::unique_ptr<GraphicsContext> createHeadlessContext(const GraphicsConfig& config);

// SDL2 window + GLES3 context. Implemented in sdl_window.cpp on desktop;
// android_stub.cpp provides a null-returning stand-in on Android.
std::unique_ptr<GraphicsContext> createWindowContext(const GraphicsConfig& config);

} // namespace as3d
