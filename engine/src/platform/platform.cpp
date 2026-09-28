// Dispatches to the headless or windowed backend. Always compiled, on every target
// (desktop and Android): it touches neither EGL nor SDL itself.
#include "as3d/platform.h"

#include "backends.h"

namespace as3d {

std::unique_ptr<GraphicsContext> createGraphicsContext(const GraphicsConfig& config) {
    return config.headless ? createHeadlessContext(config) : createWindowContext(config);
}

} // namespace as3d
