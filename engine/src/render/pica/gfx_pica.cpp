// PICA implementation of as3d::Texture2D (as3d/texture.h) and the texture budget
// (texture_budget.h).
//
// create(): format from TextureOptions::cls (texconv chooseFormat), power-of-two sides of at
// least 8 and at most kMaxTextureSide (nearest resize), the mip chain when asked (texconv
// buildMipChain, levels down to 8 texels), texconv's tiled + v-flipped layout, so engine UVs
// are used unchanged. Placement by the budget: VRAM textures are uploaded from a linear
// staging buffer with C3D_TexLoadImage (a GX texture copy: the CPU must never write VRAM on
// hardware, it data-aborts); linear textures are written in place and flushed.
// width()/height() report the source image size, as the GL backend; bytes() the GPU bytes.
//
// create() and the destructor must run outside C3D_FrameBegin/C3D_FrameEnd: the VRAM copy is
// synchronous and resets citro3d's GX queue (load time / precache only).
#include <3ds.h>
#include <citro3d.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "as3d/texconv.h"
#include "as3d/texture.h"
#include "pass_profile.h"
#include "texture_budget.h"

namespace as3d {
namespace pica {

namespace {

constexpr int kMaxTextureSide = 512; // the PICA takes 1024; the game's art is <= 256

struct PicaTexture {
    C3D_Tex tex{};
    Placement placement = Placement::Linear;
    size_t bytes = 0;
    PicaFormat format = PicaFormat::RGBA8;
};

GPU_TEXCOLOR toGpu(PicaFormat f) {
    switch (f) {
    case PicaFormat::RGBA8: return GPU_RGBA8;
    case PicaFormat::RGB8: return GPU_RGB8;
    case PicaFormat::RGBA5551: return GPU_RGBA5551;
    case PicaFormat::RGB565: return GPU_RGB565;
    case PicaFormat::RGBA4: return GPU_RGBA4;
    case PicaFormat::LA8: return GPU_LA8;
    case PicaFormat::L8: return GPU_L8;
    case PicaFormat::A8: return GPU_A8;
    case PicaFormat::LA4: return GPU_LA4;
    case PicaFormat::L4: return GPU_L4;
    case PicaFormat::A4: return GPU_A4;
    }
    return GPU_RGBA8;
}

GPU_TEXTURE_FILTER_PARAM toGpu(Filter f) { return f == Filter::Nearest ? GPU_NEAREST : GPU_LINEAR; }

GPU_TEXTURE_WRAP_PARAM toGpu(Wrap w) {
    switch (w) {
    case Wrap::Repeat: return GPU_REPEAT;
    case Wrap::ClampToEdge: return GPU_CLAMP_TO_EDGE;
    case Wrap::MirroredRepeat: return GPU_MIRRORED_REPEAT;
    }
    return GPU_REPEAT;
}

int textureSide(int v) { return std::min(roundUpPow2(std::max(v, 8)), kMaxTextureSide); }

PicaTexture* picaTex(std::uintptr_t handle) { return reinterpret_cast<PicaTexture*>(handle); }

// The texture bound to each unit by Texture2D::bind (C3D_TexBind marks the unit dirty, which
// re-emits its whole configuration with the next draw).
const C3D_Tex* g_bound[3] = {nullptr, nullptr, nullptr};

} // namespace

namespace detail {
void forgetTextureBindings() { g_bound[0] = g_bound[1] = g_bound[2] = nullptr; }
} // namespace detail

TextureBudget& textureBudget() {
    static TextureBudget budget;
    return budget;
}

TextureStats textureStats() { return textureBudget().stats(); }

// Test hook (apps/3ds/pica_test.cpp): what a Texture2D became.
bool textureInfo(const Texture2D& t, Placement* placement, int* texWidth, int* texHeight, PicaFormat* format,
                 int* levels) {
    if (!t.valid()) return false;
    const PicaTexture* p = picaTex(static_cast<std::uintptr_t>(t.id()));
    if (placement) *placement = p->placement;
    if (texWidth) *texWidth = p->tex.width;
    if (texHeight) *texHeight = p->tex.height;
    if (format) *format = p->format;
    if (levels) *levels = p->tex.maxLevel + 1;
    return true;
}

} // namespace pica

using pica::PicaTexture;
using pica::Placement;

Texture2D::~Texture2D() { destroy(); }

Texture2D::Texture2D(Texture2D&& other) noexcept
    : handle_(other.handle_), width_(other.width_), height_(other.height_), bytes_(other.bytes_) {
    other.handle_ = 0;
    other.width_ = other.height_ = 0;
    other.bytes_ = 0;
}

Texture2D& Texture2D::operator=(Texture2D&& other) noexcept {
    if (this != &other) {
        destroy();
        handle_ = other.handle_;
        width_ = other.width_;
        height_ = other.height_;
        bytes_ = other.bytes_;
        other.handle_ = 0;
        other.width_ = other.height_ = 0;
        other.bytes_ = 0;
    }
    return *this;
}

void Texture2D::destroy() {
    if (handle_) {
        PicaTexture* p = pica::picaTex(handle_);
        for (const C3D_Tex*& b : pica::g_bound)
            if (b == &p->tex) b = nullptr; // a new texture may reuse the address
        C3D_TexDelete(&p->tex); // vramFree or linearFree, by address
        pica::textureBudget().remove(p->placement, p->bytes);
        delete p;
        handle_ = 0;
    }
    width_ = height_ = 0;
    bytes_ = 0;
}

void Texture2D::create(const Image& image, const TextureOptions& options) {
    destroy();
    pica::TextureBudget& budget = pica::textureBudget();
    if (image.width <= 0 || image.height <= 0 ||
        image.rgba.size() < static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4) {
        budget.noteFailure();
        return;
    }
    pica::LoadProfile& lp = pica::loadProfile();
    const u64 t0 = svcGetSystemTick();
    const PicaFormat fmt = chooseFormat(image, options.cls);
    const int w = pica::textureSide(image.width), h = pica::textureSide(image.height);
    // The levels of buildMipChain (texconv.h): halved while both sides stay >= 8.
    int levelCount = 1;
    if (options.mipmaps)
        for (int lw = w, lh = h; lw / 2 >= 8 && lh / 2 >= 8; lw /= 2, lh /= 2) ++levelCount;
    u64 mipTicks = 0, swizzleTicks = 0;
    const size_t bytes = byteSize(w, h, fmt, levelCount);

    auto* p = new PicaTexture;
    p->format = fmt;
    p->placement = budget.choose(bytes, options.res);
    C3D_TexInitParams params{static_cast<u16>(w), static_cast<u16>(h), static_cast<u8>(levelCount - 1), pica::toGpu(fmt),
                             GPU_TEX_2D, p->placement == Placement::Vram};
    bool ok = C3D_TexInitWithParams(&p->tex, nullptr, params);
    if (!ok && p->placement == Placement::Vram) {
        budget.noteVramFallback();
        p->placement = Placement::Linear;
        params.onVram = false;
        ok = C3D_TexInitWithParams(&p->tex, nullptr, params);
    }
    if (!ok) {
        delete p;
        budget.noteFailure();
        return;
    }
    // VRAM: the chain goes through one linear staging buffer (levels at their offsets in the
    // texture), one cache flush, then a GX copy per level. Linear memory: written in place.
    uint8_t* staging = nullptr;
    if (p->placement == Placement::Vram) {
        staging = static_cast<uint8_t*>(linearAlloc(bytes));
        if (!staging) {
            C3D_TexDelete(&p->tex);
            delete p;
            budget.noteFailure();
            return;
        }
    }

    // Each level is converted straight into its place (texconv swizzleInto), the next one
    // downsampled from it (no chain of copies): level 0 is the image itself when its size is
    // already the texture's.
    Image bufA, bufB;
    const Image* cur = &image;
    if (w != image.width || h != image.height) {
        const u64 m0 = svcGetSystemTick();
        bufA = resizeNearest(image, w, h);
        mipTicks += svcGetSystemTick() - m0;
        cur = &bufA;
    }
    size_t offset = 0;
    std::vector<size_t> offsets;
    for (int level = 0; level < levelCount; ++level) {
        const size_t levelBytes = byteSize(cur->width, cur->height, fmt, 1);
        uint8_t* dst;
        size_t room;
        if (staging) {
            dst = staging + offset;
            room = bytes - offset;
        } else {
            u32 size = 0;
            dst = static_cast<uint8_t*>(C3D_Tex2DGetImagePtr(&p->tex, level, &size));
            room = size;
        }
        const u64 s0 = svcGetSystemTick();
        swizzleInto(*cur, fmt, dst, room);
        swizzleTicks += svcGetSystemTick() - s0;
        offsets.push_back(offset);
        offset += levelBytes;
        if (level + 1 < levelCount) {
            const u64 m0 = svcGetSystemTick();
            Image& next = cur == &bufA ? bufB : bufA;
            next = downsampleBox2x2(*cur);
            cur = &next;
            mipTicks += svcGetSystemTick() - m0;
        }
    }
    if (staging) {
        GSPGPU_FlushDataCache(staging, bytes);
        for (int level = 0; level < levelCount; ++level)
            C3D_TexLoadImage(&p->tex, staging + offsets[static_cast<size_t>(level)], GPU_TEXFACE_2D, level);
        linearFree(staging);
    } else {
        C3D_TexFlush(&p->tex);
    }
    const u64 tMip = t0 + mipTicks; // the profile's split (mip, swizzle, the rest = upload)
    lp.texMipTicks += mipTicks;

    {
        const u64 t1 = svcGetSystemTick();
        lp.texSwizzleTicks += swizzleTicks;
        lp.texUploadTicks += (t1 - tMip) - swizzleTicks; // t1 - t0 - mip - swizzle
        lp.texTicks += t1 - t0;
        ++lp.texCount;
        lp.texLevels += levelCount;
        lp.texBytes += bytes;
    }
    C3D_TexSetFilter(&p->tex, pica::toGpu(options.magFilter), pica::toGpu(options.minFilter));
    if (levelCount > 1) C3D_TexSetFilterMipmap(&p->tex, pica::toGpu(options.minFilter));
    C3D_TexSetWrap(&p->tex, pica::toGpu(options.wrapS), pica::toGpu(options.wrapT));

    p->bytes = bytes;
    budget.add(p->placement, bytes);
    handle_ = reinterpret_cast<std::uintptr_t>(p);
    width_ = image.width;
    height_ = image.height;
    bytes_ = bytes;
}

void Texture2D::bind(unsigned unit) const {
    if (!handle_ || unit > 2) return;
    C3D_Tex* t = &pica::picaTex(handle_)->tex;
    if (pica::g_bound[unit] == t) return;
    pica::g_bound[unit] = t;
    C3D_TexBind(static_cast<int>(unit), t);
}

} // namespace as3d
