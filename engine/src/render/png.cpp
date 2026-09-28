// PNG encoding for as3d::writePng, backed by stb_image_write (third_party/stb).
// as3d::Image is already top-down (row 0 = top), which is exactly the row order a PNG
// file stores, so no flip is needed here (unlike RenderTarget::readPixels, which flips
// GL's bottom-up readback into that same top-down convention before this ever runs).
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "as3d/gfx.h"

namespace as3d {

bool writePng(const char* path, const Image& image) {
    if (!path || image.width <= 0 || image.height <= 0) return false;
    if (image.rgba.size() != static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4) {
        AS3D_ERROR("writePng: image buffer size does not match %dx%d RGBA8", image.width, image.height);
        return false;
    }
    int ok = stbi_write_png(path, image.width, image.height, 4, image.rgba.data(), image.width * 4);
    if (!ok) {
        AS3D_ERROR("writePng: failed to write '%s'", path);
        return false;
    }
    return true;
}

} // namespace as3d
