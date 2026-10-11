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
// Outside a frame the VRAM copy is synchronous (citro3d waits for the GX queue, copies, waits for
// the copy). Inside a frame (a late load; docs/3ds-port.md "Frame synchronisation") citro3d would
// split the command list WITHOUT a cache flush (C3D_FrameSplit(0): with gpu_kick's
// end_flush=cmdlist the GPU may run stale command words) and queue an asynchronous copy from a
// staging buffer freed at once, one queue entry per mip level (32 per frame): so a texture
// created inside a frame goes to linear memory instead (no GX command at all). destroy() hands
// the memory to the GPU fence (pica::deferTextureDelete): a frame in flight may still sample it.
//
// The late queue on top of that (3DS round 2, P1): a texture created inside a frame (a load the
// precache missed) is first an 8x8 linear-memory placeholder of the image's average colour (CPU
// written, one cache flush), its image queued; drainLateTextures() (the composer, after
// C3D_FrameEnd) builds the real texture outside the frame, VRAM included, and swaps it into the
// same handle, the placeholder going through the fence. AS3D_LATE_TEX logs each (the path of the
// load that asked, the frame). If the placeholder cannot be made, the in-frame linear path above
// stays the fallback.
#include <3ds.h>
#include <citro3d.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/load_stats.h"
#include "as3d/texconv.h"
#include "as3d/texture.h"
#include "pass_profile.h"
#include "state_check_pica.h"
#include "texture_budget.h"

namespace as3d {
namespace pica {

// pica_frame.h (not included here: its world headers bring as3d::Placement of terrain.h).
bool backendReady();
bool inFrame();
uint32_t frameSerial();
void deferTextureDelete(const C3D_Tex& tex);
void collectDeferred(bool waitIfTextures);

namespace {

constexpr int kMaxTextureSide = 512; // the PICA takes 1024; the game's art is <= 256

struct PicaTexture {
    C3D_Tex tex{};
    Placement placement = Placement::Linear;
    size_t bytes = 0;
    PicaFormat format = PicaFormat::RGBA8;
    bool late = false; // a placeholder whose real texture is queued (drainLateTextures)
};

// A texture created inside a frame: its image, built by drainLateTextures.
struct LateTexture {
    PicaTexture* tex = nullptr;
    Image image;
    TextureOptions options;
    std::string path;
    long frame = 0;
};
std::vector<LateTexture> g_late;
LateTextureStats g_lateStats;

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

TextureInFrameStats g_texInFrame;
const TextureInFrameStats& textureInFrameStats() { return g_texInFrame; }

const C3D_Tex* boundTexture(unsigned unit) { return unit < 3 ? g_bound[unit] : nullptr; }
const C3D_Tex* c3dTexOf(const Texture2D& t) {
    return t.valid() ? &picaTex(static_cast<std::uintptr_t>(t.id()))->tex : nullptr;
}

namespace detail {
void forgetTextureBindings() { g_bound[0] = g_bound[1] = g_bound[2] = nullptr; }
void unbindTexture(unsigned unit) {
    if (unit > 2) return;
    g_bound[unit] = nullptr;
    // The unit is disabled with the next draw. Unit 0 may be bound to null directly; for units 1
    // and 2 citro3d 1.7.1 reads the texture's type first (a data abort on the console), so the
    // slot is cleared through the context mirror (state_check_pica.cpp).
    if (unit == 0) C3D_TexBind(0, nullptr);
    else c3dSetTextureSlotNull(unit);
}
} // namespace detail

SkinHalfStats g_skinHalf;
const SkinHalfStats& skinHalfStats() { return g_skinHalf; }

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
        if (p->late) {
            for (auto it = pica::g_late.begin(); it != pica::g_late.end(); ++it)
                if (it->tex == p) {
                    pica::g_late.erase(it);
                    break;
                }
        }
        if (pica::backendReady()) {
            pica::deferTextureDelete(p->tex); // vramFree or linearFree once the GPU is done with it
            if (pica::inFrame()) ++pica::g_texInFrame.destroys;
        } else {
            C3D_TexDelete(&p->tex);
        }
        pica::textureBudget().remove(p->placement, p->bytes);
        delete p;
        handle_ = 0;
    }
    width_ = height_ = 0;
    bytes_ = 0;
}

namespace pica {
namespace {

// The texture of `image` into `p` (fresh: no C3D texture yet). False on failure (the budget
// noted it). Inside a frame: linear memory, no GX copy (the header).
bool buildTexture(PicaTexture* p, const Image& image, const TextureOptions& options) {
    pica::TextureBudget& budget = pica::textureBudget();
    pica::LoadProfile& lp = pica::loadProfile();
    const u64 t0 = svcGetSystemTick();
    const PicaFormat fmt = chooseFormat(image, options.cls);
    const int fullW = pica::textureSide(image.width), fullH = pica::textureSide(image.height);
    // R4.11 (run.ini skin_half): the texture starts `baseLevel` levels down the full chain, so a
    // 256 x 256 skin is made from the chain's 128 x 128 level (the same bytes the full chain
    // would have sampled when minified); never below 8 a side.
    int dropped = 0;
    int w = fullW, h = fullH;
    while (dropped < options.baseLevel && w / 2 >= 8 && h / 2 >= 8) {
        w /= 2;
        h /= 2;
        ++dropped;
    }
    // The levels of buildMipChain (texconv.h): halved while both sides stay >= 8.
    int levelCount = 1;
    if (options.mipmaps)
        for (int lw = w, lh = h; lw / 2 >= 8 && lh / 2 >= 8; lw /= 2, lh /= 2) ++levelCount;
    u64 mipTicks = 0, swizzleTicks = 0;
    const size_t bytes = byteSize(w, h, fmt, levelCount);

    // The VRAM a destroyed texture held is free once the GPU is done with it.
    if (!pica::inFrame()) pica::collectDeferred(true);
    p->format = fmt;
    p->placement = budget.choose(bytes, options.res);
    if (pica::inFrame()) { // no GX copy inside a frame (the header)
        p->placement = Placement::Linear;
        pica::TextureInFrameStats& tf = pica::g_texInFrame;
        if (++tf.creates <= 20)
            AS3D_INFO("AS3D_TEX_IN_FRAME frame=%u %dx%d %.1f KB: linear memory (no GX copy inside a frame)",
                      static_cast<unsigned>(pica::frameSerial()), w, h, static_cast<double>(bytes) / 1024.0);
    }
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
        budget.noteFailure();
        return false;
    }
    // VRAM: the chain goes through one linear staging buffer (levels at their offsets in the
    // texture), one cache flush, then a GX copy per level. Linear memory: written in place.
    uint8_t* staging = nullptr;
    if (p->placement == Placement::Vram) {
        staging = static_cast<uint8_t*>(linearAlloc(bytes));
        if (!staging) {
            C3D_TexDelete(&p->tex);
            budget.noteFailure();
            return false;
        }
    }

    // Each level is converted straight into its place (texconv swizzleInto), the next one
    // downsampled from it (no chain of copies): level 0 is the image itself when its size is
    // already the texture's.
    Image bufA, bufB;
    const Image* cur = &image;
    if (fullW != image.width || fullH != image.height) {
        const u64 m0 = svcGetSystemTick();
        bufA = resizeNearest(image, fullW, fullH);
        mipTicks += svcGetSystemTick() - m0;
        cur = &bufA;
    }
    for (int k = 0; k < dropped; ++k) { // the dropped levels: the chain's own box filter
        const u64 m0 = svcGetSystemTick();
        Image& next = cur == &bufA ? bufB : bufA;
        next = downsampleBox2x2(*cur);
        cur = &next;
        mipTicks += svcGetSystemTick() - m0;
        ++pica::g_skinHalf.dropped;
    }
    if (dropped) ++pica::g_skinHalf.textures;
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
    return true;
}

// The placeholder of a texture created inside a frame: 8x8 RGBA8 in linear memory, every texel
// the image's average colour (a sampled average: at most 32 x 32 texels read).
bool buildPlaceholder(PicaTexture* p, const Image& image, const TextureOptions& options) {
    unsigned sum[4] = {0, 0, 0, 0}, n = 0;
    const int sx = std::max(1, image.width / 32), sy = std::max(1, image.height / 32);
    for (int y = 0; y < image.height; y += sy)
        for (int x = 0; x < image.width; x += sx) {
            const uint8_t* px =
                &image.rgba[(static_cast<size_t>(y) * static_cast<size_t>(image.width) + static_cast<size_t>(x)) * 4];
            for (int c = 0; c < 4; ++c) sum[c] += px[c];
            ++n;
        }
    Image avg;
    avg.width = avg.height = 8;
    avg.hasAlpha = image.hasAlpha;
    avg.rgba.resize(8 * 8 * 4);
    for (size_t i = 0; i < avg.rgba.size(); ++i) avg.rgba[i] = static_cast<uint8_t>(n ? sum[i % 4] / n : 255);
    C3D_TexInitParams params{8, 8, 0, GPU_RGBA8, GPU_TEX_2D, false};
    if (!C3D_TexInitWithParams(&p->tex, nullptr, params)) return false;
    u32 size = 0;
    auto* dst = static_cast<uint8_t*>(C3D_Tex2DGetImagePtr(&p->tex, 0, &size));
    swizzleInto(avg, PicaFormat::RGBA8, dst, size);
    C3D_TexFlush(&p->tex); // a data cache flush: no GX command, safe inside the frame
    C3D_TexSetFilter(&p->tex, pica::toGpu(options.magFilter), pica::toGpu(options.minFilter));
    C3D_TexSetWrap(&p->tex, pica::toGpu(options.wrapS), pica::toGpu(options.wrapT));
    p->placement = Placement::Linear;
    p->format = PicaFormat::RGBA8;
    p->bytes = byteSize(8, 8, PicaFormat::RGBA8, 1);
    pica::textureBudget().add(Placement::Linear, p->bytes);
    return true;
}

} // namespace

const LateTextureStats& lateTextureStats() { return g_lateStats; }
size_t lateTexturesPending() { return g_late.size(); }

int drainLateTextures(const LateBudget& budget) {
    if (g_late.empty() || inFrame()) return 0;
    const u64 t0 = svcGetSystemTick();
    int done = 0;
    while (!g_late.empty()) {
        const double ms = static_cast<double>(svcGetSystemTick() - t0) * 1000.0 / SYSCLOCK_ARM11;
        if (!lateDrainContinue(done, ms, budget)) break;
        LateTexture lt = std::move(g_late.front());
        g_late.erase(g_late.begin());
        PicaTexture* p = lt.tex;
        const u64 b0 = svcGetSystemTick();
        collectDeferred(true); // the VRAM of destroyed textures, as create() outside a frame
        auto* real = new PicaTexture;
        const bool ok = buildTexture(real, lt.image, lt.options);
        if (ok) {
            // The placeholder: through the fence (the frame just submitted may still sample it).
            deferTextureDelete(p->tex);
            textureBudget().remove(p->placement, p->bytes);
            for (const C3D_Tex*& b : g_bound)
                if (b == &p->tex) b = nullptr; // bound again with the new contents
            p->tex = real->tex;
            p->placement = real->placement;
            p->bytes = real->bytes;
            p->format = real->format;
        }
        p->late = false;
        delete real;
        ++done;
        ++g_lateStats.built;
        const double buildMs = static_cast<double>(svcGetSystemTick() - b0) * 1000.0 / SYSCLOCK_ARM11;
        g_lateStats.buildMs += buildMs;
        AS3D_INFO("AS3D_LATE_TEX built path=%s frame=%ld now=%ld %dx%d %s ms=%.1f queued=%zu", lt.path.c_str(), lt.frame,
                  LoadStats::frame(), lt.image.width, lt.image.height,
                  ok ? (p->placement == Placement::Vram ? "vram" : "linear") : "failed", buildMs, g_late.size());
    }
    return done;
}

} // namespace pica

void Texture2D::create(const Image& image, const TextureOptions& options) {
    destroy();
    pica::TextureBudget& budget = pica::textureBudget();
    if (image.width <= 0 || image.height <= 0 ||
        image.rgba.size() < static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4) {
        budget.noteFailure();
        return;
    }
    auto* p = new PicaTexture;
    if (pica::inFrame() && pica::buildPlaceholder(p, image, options)) {
        // Inside a frame: the placeholder now, the real texture after the frame (the header).
        p->late = true;
        ++pica::g_texInFrame.creates;
        pica::LateTexture lt;
        lt.tex = p;
        lt.image = image;
        lt.options = options;
        lt.path = LoadStats::lastPath();
        lt.frame = LoadStats::frame();
        ++pica::g_lateStats.queued;
        AS3D_INFO("AS3D_LATE_TEX queued path=%s frame=%ld %dx%d", lt.path.c_str(), lt.frame, image.width, image.height);
        pica::g_late.push_back(std::move(lt));
    } else if (!pica::buildTexture(p, image, options)) {
        delete p;
        return;
    }
    handle_ = reinterpret_cast<std::uintptr_t>(p);
    width_ = image.width;
    height_ = image.height;
    bytes_ = p->bytes;
}

void Texture2D::createZeroed(int w, int h, PicaFormat format, const TextureOptions& options) {
    destroy();
    pica::TextureBudget& budget = pica::textureBudget();
    w = pica::textureSide(w);
    h = pica::textureSide(h);
    int levelCount = 1;
    if (options.mipmaps)
        for (int lw = w, lh = h; lw / 2 >= 8 && lh / 2 >= 8; lw /= 2, lh /= 2) ++levelCount;
    auto* p = new PicaTexture;
    p->format = format;
    p->placement = pica::Placement::Linear;
    const C3D_TexInitParams params{static_cast<u16>(w), static_cast<u16>(h), static_cast<u8>(levelCount - 1),
                                   pica::toGpu(format), GPU_TEX_2D, false};
    if (!C3D_TexInitWithParams(&p->tex, nullptr, params)) {
        budget.noteFailure();
        delete p;
        return;
    }
    const size_t bytes = byteSize(w, h, format, levelCount);
    std::memset(p->tex.data, 0, bytes);
    C3D_TexFlush(&p->tex);
    C3D_TexSetFilter(&p->tex, pica::toGpu(options.magFilter), pica::toGpu(options.minFilter));
    if (levelCount > 1) C3D_TexSetFilterMipmap(&p->tex, pica::toGpu(options.minFilter));
    C3D_TexSetWrap(&p->tex, pica::toGpu(options.wrapS), pica::toGpu(options.wrapT));
    p->bytes = bytes;
    budget.add(p->placement, bytes);
    handle_ = reinterpret_cast<std::uintptr_t>(p);
    width_ = w;
    height_ = h;
    bytes_ = bytes;
}

bool Texture2D::createConverted(int w, int h, PicaFormat format, int levels, const uint8_t* data, size_t bytes,
                                const TextureOptions& options) {
    destroy();
    if (!data || pica::inFrame() || w < 8 || h < 8 || pica::textureSide(w) != w || pica::textureSide(h) != h || levels < 1 ||
        byteSize(w, h, format, levels) != bytes)
        return false;
    pica::TextureBudget& budget = pica::textureBudget();
    pica::LoadProfile& lp = pica::loadProfile();
    const u64 t0 = svcGetSystemTick();
    pica::collectDeferred(true); // the VRAM of destroyed textures, as create() outside a frame
    auto* p = new PicaTexture;
    p->format = format;
    p->placement = budget.choose(bytes, options.res);
    C3D_TexInitParams params{static_cast<u16>(w), static_cast<u16>(h), static_cast<u8>(levels - 1), pica::toGpu(format),
                             GPU_TEX_2D, p->placement == pica::Placement::Vram};
    bool ok = C3D_TexInitWithParams(&p->tex, nullptr, params);
    if (!ok && p->placement == pica::Placement::Vram) {
        budget.noteVramFallback();
        p->placement = pica::Placement::Linear;
        params.onVram = false;
        ok = C3D_TexInitWithParams(&p->tex, nullptr, params);
    }
    if (!ok) {
        budget.noteFailure();
        delete p;
        return false;
    }
    size_t offset = 0;
    if (p->placement == pica::Placement::Vram) {
        auto* staging = static_cast<uint8_t*>(linearAlloc(bytes));
        if (!staging) {
            C3D_TexDelete(&p->tex);
            budget.noteFailure();
            delete p;
            return false;
        }
        std::memcpy(staging, data, bytes);
        GSPGPU_FlushDataCache(staging, bytes);
        for (int level = 0; level < levels; ++level) {
            C3D_TexLoadImage(&p->tex, staging + offset, GPU_TEXFACE_2D, level);
            offset += byteSize(w >> level, h >> level, format, 1);
        }
        linearFree(staging);
    } else {
        for (int level = 0; level < levels; ++level) {
            u32 size = 0;
            auto* dst = static_cast<uint8_t*>(C3D_Tex2DGetImagePtr(&p->tex, level, &size));
            const size_t lb = byteSize(w >> level, h >> level, format, 1);
            if (dst && size >= lb) std::memcpy(dst, data + offset, lb);
            offset += lb;
        }
        C3D_TexFlush(&p->tex);
    }
    C3D_TexSetFilter(&p->tex, pica::toGpu(options.magFilter), pica::toGpu(options.minFilter));
    if (levels > 1) C3D_TexSetFilterMipmap(&p->tex, pica::toGpu(options.minFilter));
    C3D_TexSetWrap(&p->tex, pica::toGpu(options.wrapS), pica::toGpu(options.wrapT));
    p->bytes = bytes;
    budget.add(p->placement, bytes);
    const u64 t1 = svcGetSystemTick();
    lp.texUploadTicks += t1 - t0;
    lp.texTicks += t1 - t0;
    ++lp.texCount;
    lp.texLevels += levels;
    lp.texBytes += bytes;
    handle_ = reinterpret_cast<std::uintptr_t>(p);
    width_ = w;
    height_ = h;
    bytes_ = bytes;
    return true;
}

void Texture2D::bind(unsigned unit) const {
    if (!handle_ || unit > 2) return;
    C3D_Tex* t = &pica::picaTex(handle_)->tex;
    if (pica::g_bound[unit] == t) return;
    pica::g_bound[unit] = t;
    C3D_TexBind(static_cast<int>(unit), t);
}

} // namespace as3d
