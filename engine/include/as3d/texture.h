// Backend-neutral 2D texture handle. The GLES implementation lives in
// engine/src/render/gles/gfx.cpp; the PICA backend provides its own Texture2D .cpp.
//
// Texture orientation convention: as3d::Image stores row 0 as the TOP row, textures
// keep that order, and v = 0 is the TOP of a texture in this engine (v = 1 the bottom).
#pragma once

#include <cstddef>
#include <cstdint>

#include "as3d/image.h"
#include "as3d/texconv.h"

namespace as3d {

enum class Filter { Nearest, Linear };
enum class Wrap { Repeat, ClampToEdge, MirroredRepeat };

struct TextureOptions {
    bool mipmaps = false;
    Filter minFilter = Filter::Linear;
    Filter magFilter = Filter::Linear;
    Wrap wrapS = Wrap::Repeat;
    Wrap wrapT = Wrap::Repeat;
    // Hints for the PICA backend (format choice, memory placement). The GL backend
    // ignores both.
    TextureClass cls = TextureClass::Auto;
    enum class Residency { Auto, Vram, Linear } res = Residency::Auto;
};

class Texture2D {
public:
    Texture2D() = default;
    ~Texture2D();
    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;
    Texture2D(Texture2D&& other) noexcept;
    Texture2D& operator=(Texture2D&& other) noexcept;

    // Uploads `image` as-is (rows in Image order, no flip). The GL backend always
    // stores RGBA8; `image.hasAlpha` does not change the internal format.
    void create(const Image& image, const TextureOptions& options = {});
#if defined(__3DS__)
    // PICA only (gfx_pica.cpp): a w x h texture of `format` in linear memory whose every byte is
    // zero (the shadow atlas's empty pages), without an image to convert.
    void createZeroed(int w, int h, PicaFormat format, const TextureOptions& options = {});
    // PICA only: a w x h texture (power-of-two sides of at least 8) of `format` with `levels` mip
    // levels from bytes already converted (texconv's layout, the levels one after the other, as
    // Texture2D::create makes them), placed as create would place it; outside frames only (false
    // inside one, or when the bytes do not match the size). The terrain's base texture cache.
    bool createConverted(int w, int h, PicaFormat format, int levels, const uint8_t* data, size_t bytes,
                         const TextureOptions& options = {});
#endif
    void bind(unsigned unit = 0) const;

    bool valid() const { return handle_ != 0; }
    int width() const { return width_; }
    int height() const { return height_; }
    // Approximate GPU memory held by this texture (including mip chain).
    size_t bytes() const { return bytes_; }
    // GL-only: the GL texture name. Do not use outside GL code.
    unsigned id() const { return static_cast<unsigned>(handle_); }

private:
    std::uintptr_t handle_ = 0;
    int width_ = 0;
    int height_ = 0;
    size_t bytes_ = 0;
    void destroy();
};

} // namespace as3d
