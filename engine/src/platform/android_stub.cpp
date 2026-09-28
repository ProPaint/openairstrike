// Android placeholder: WP-19 only implements the desktop backends (headless EGL and
// SDL2 window). The Android context is being prototyped separately in
// apps/android_boot and will be unified with this module later. Until then, both
// factories simply fail loudly instead of crashing.
//
// Desktop only excludes this file from the build (see module.cmake); on Android it is
// the only translation unit in this directory that gets compiled.
#include "backends.h"

#include "as3d/core.h"

namespace as3d {

std::unique_ptr<GraphicsContext> createHeadlessContext(const GraphicsConfig&) {
    AS3D_WARN("createHeadlessContext: no Android backend yet (WP-19 covers desktop only; "
              "see apps/android_boot for the in-progress Android context prototype)");
    return nullptr;
}

std::unique_ptr<GraphicsContext> createWindowContext(const GraphicsConfig&) {
    AS3D_WARN("createWindowContext: no Android backend yet (WP-19 covers desktop only; "
              "see apps/android_boot for the in-progress Android context prototype)");
    return nullptr;
}

} // namespace as3d
