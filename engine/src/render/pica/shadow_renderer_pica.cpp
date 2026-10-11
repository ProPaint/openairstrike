// Silhouette shadows on the PICA (as3d/shadow_render.h, 3DS only): the CPU silhouettes, the
// blob and the draped draw. The design is in shadow_pica.h; docs/spec/render-pipeline.md 5.
#include "as3d/shadow_render.h"

#include <3ds.h>
#include <citro3d.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <utility>

#include "as3d/fs_util.h"
#include "as3d/material_key.h"
#include "as3d/render_state.h"
#include "as3d/texconv.h"
#include "fx_pica.h"
#include "pica_frame.h"
#include "quality_pica.h"
#include "shader_set.h"
#include "shadow_pica.h"
#include "tev.h"
#include "texture_budget.h"

namespace as3d {

namespace pica {

namespace {

ShadowPicaStats g_stats;
ShadowGenInfo g_lastGen;
std::function<const ShadowSkin*(const Texture2D*)> g_skinSource;
bool g_precacheRotations = false;

double msSince(u64 t0) { return static_cast<double>(svcGetSystemTick() - t0) / (SYSCLOCK_ARM11 / 1000.0); }

// The sequels' multiplied look (DecalLook::ShadowMultiply): grey 1 - texture alpha, alpha 1,
// drawn with the Filter blend (destination x source).
TevConfig tevShadowMultiply() {
    TevConfig c;
    c.count = 1;
    TevStage& s = c.stages[0];
    s.srcRgb[0] = TevSrc::Texture0;
    s.opRgb[0] = TevOpRgb::OneMinusSrcAlpha;
    s.funcRgb = TevFunc::Replace;
    s.srcA[0] = TevSrc::Constant;
    s.funcA = TevFunc::Replace;
    return c;
}

// ---------------------------------------------------------------------------------------
// The atlas (shadow_pica.h)
// ---------------------------------------------------------------------------------------

inline uint32_t morton8(uint32_t x, uint32_t y) {
    return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
}

// The byte of texel (x, y) (engine image coordinates: row 0 at the top) in an A8 level of side
// `side`: texconv's layout (8x8 tiles row-major from the bottom row, Morton inside a tile).
inline size_t a8Offset(int x, int y, int side) {
    const int my = side - 1 - y;
    return static_cast<size_t>((my >> 3) * (side >> 3) + (x >> 3)) * 64 + morton8(static_cast<uint32_t>(x), static_cast<uint32_t>(my));
}

struct AtlasShelf {
    int y = 0, height = 0, x = 0; // height includes the gap
};

struct AtlasPage {
    Texture2D tex;
    bool batchDirty = false; // written inside a placement batch: levels past 3 and the flush owed
    int levels = 1;
    std::vector<AtlasShelf> shelves;
    int top = 0; // the first row no shelf uses
};

struct ShadowAtlas {
    std::vector<std::unique_ptr<AtlasPage>> pages;
    bool mipmaps = true;
    bool broken = false; // a page could not be made as A8: no atlas until the next reset
};

ShadowAtlasRef g_lastAtlasRef;
bool g_fastHit = true;               // run.ini shadow_fast_hit (setShadowFastHit)
bool g_atlasBatch = false;           // shadowAtlasBatchBegin .. End
ShadowAtlas* g_batchAtlas = nullptr; // the atlas written in the batch

// Finds room for a w x h cell (multiples of 8): (page, x, y). False when every page is full and
// no more page may be made.
bool atlasFind(ShadowAtlas& a, int w, int h, bool mipmaps, AtlasPage** outPage, int* outX, int* outY) {
    if (a.broken) return false;
    if (!a.pages.empty() && a.mipmaps != mipmaps) return false; // one rule set per level
    const int need = h + kShadowAtlasGap;
    const int needW = w + kShadowAtlasGap;
    for (auto& pg : a.pages) {
        for (AtlasShelf& sh : pg->shelves) {
            if (sh.height != need || sh.x + w > kShadowAtlasSide) continue;
            *outPage = pg.get();
            *outX = sh.x;
            *outY = sh.y;
            sh.x += needW;
            return true;
        }
        if (pg->top + h <= kShadowAtlasSide) {
            AtlasShelf sh;
            sh.y = pg->top;
            sh.height = need;
            sh.x = needW;
            pg->top += need;
            pg->shelves.push_back(sh);
            *outPage = pg.get();
            *outX = 0;
            *outY = sh.y;
            return true;
        }
    }
    if (static_cast<int>(a.pages.size()) >= kShadowAtlasMaxPages) return false;
    // A new page: zero alpha, A8 in linear memory, the level chain of Texture2D::create.
    const u64 tPage = svcGetSystemTick();
    auto pg = std::make_unique<AtlasPage>();
    TextureOptions opts;
    opts.mipmaps = mipmaps;
    opts.wrapS = opts.wrapT = Wrap::ClampToEdge;
    opts.cls = TextureClass::Shadow;
    opts.res = TextureOptions::Residency::Linear;
    if (g_fastHit) {
        // shadow_fast_hit: the page's A8 bytes zeroed directly (what the grey image of zero alpha
        // converts to at every level), no 1 MB image to fill, convert and filter.
        pg->tex.createZeroed(kShadowAtlasSide, kShadowAtlasSide, PicaFormat::A8, opts);
    } else {
        Image img;
        img.width = img.height = kShadowAtlasSide;
        img.hasAlpha = true;
        img.rgba.assign(static_cast<size_t>(kShadowAtlasSide) * kShadowAtlasSide * 4, 0);
        for (size_t i = 0; i < img.rgba.size(); i += 4) img.rgba[i] = img.rgba[i + 1] = img.rgba[i + 2] = 114;
        pg->tex.create(img, opts);
    }
    PicaFormat fmt = PicaFormat::RGBA8;
    Placement place = Placement::Vram;
    int texW = 0, levels = 0;
    if (!pg->tex.valid() || !textureInfo(pg->tex, &place, &texW, nullptr, &fmt, &levels) || fmt != PicaFormat::A8 ||
        place != Placement::Linear || texW != kShadowAtlasSide) {
        a.broken = true;
        AS3D_WARN("shadow atlas: no A8 page in linear memory, silhouettes keep their own textures");
        return false;
    }
    pg->levels = levels;
    g_stats.atlasPageMs += msSince(tPage);
    a.mipmaps = mipmaps;
    g_stats.atlasBytes += pg->tex.bytes();
    ++g_stats.atlasPages;
    a.pages.push_back(std::move(pg));
    return atlasFind(a, w, h, mipmaps, outPage, outX, outY);
}

// Writes the A8 alpha of `img` (already the cell's power-of-two size) and its box chain into the
// page at (x, y); levels past 3 are rebuilt for the whole page from level 3.
void atlasWrite(AtlasPage& pg, const Image& img, int x, int y) {
    C3D_Tex* tex = const_cast<C3D_Tex*>(c3dTexOf(pg.tex));
    Image cur = img;
    const int cellLevels = std::min(pg.levels, 4);
    for (int level = 0; level < cellLevels; ++level) {
        u32 size = 0;
        auto* dst = static_cast<uint8_t*>(C3D_Tex2DGetImagePtr(tex, level, &size));
        const int side = kShadowAtlasSide >> level;
        const int lx = x >> level, ly = y >> level;
        for (int yy = 0; yy < cur.height; ++yy)
            for (int xx = 0; xx < cur.width; ++xx)
                dst[a8Offset(lx + xx, ly + yy, side)] =
                    cur.rgba[(static_cast<size_t>(yy) * static_cast<size_t>(cur.width) + static_cast<size_t>(xx)) * 4 + 3];
        GSPGPU_FlushDataCache(dst, size);
        if (level + 1 < cellLevels) cur = downsampleBox2x2(cur);
    }
    for (int level = 4; level < pg.levels; ++level) {
        u32 size = 0, prevSize = 0;
        auto* dst = static_cast<uint8_t*>(C3D_Tex2DGetImagePtr(tex, level, &size));
        const auto* src = static_cast<const uint8_t*>(C3D_Tex2DGetImagePtr(tex, level - 1, &prevSize));
        const int side = kShadowAtlasSide >> level;
        for (int yy = 0; yy < side; ++yy)
            for (int xx = 0; xx < side; ++xx) {
                const int s2 = side * 2;
                const int sum = src[a8Offset(2 * xx, 2 * yy, s2)] + src[a8Offset(2 * xx + 1, 2 * yy, s2)] +
                                src[a8Offset(2 * xx, 2 * yy + 1, s2)] + src[a8Offset(2 * xx + 1, 2 * yy + 1, s2)];
                dst[a8Offset(xx, yy, side)] = static_cast<uint8_t>((sum + 2) >> 2);
            }
        GSPGPU_FlushDataCache(dst, size);
    }
}

// One channel of a silhouette (the alpha), row 0 = the image's row 0.
struct AlphaImage {
    int width = 0, height = 0;
    std::vector<u8> a;
};

// texconv's resizeNearest and downsampleBox2x2 on one channel: the same source texels and the
// same rounding, so the bytes equal the RGBA path's alpha.
AlphaImage alphaResizeNearest(const AlphaImage& src, int w, int h) {
    AlphaImage out;
    out.width = w;
    out.height = h;
    out.a.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
    std::vector<int> sxs(static_cast<size_t>(w)); // the source column per column, once
    for (int x = 0; x < w; ++x) sxs[static_cast<size_t>(x)] = static_cast<int>(static_cast<long long>(x) * src.width / w);
    for (int y = 0; y < h; ++y) {
        const int sy = static_cast<int>(static_cast<long long>(y) * src.height / h);
        const u8* row = &src.a[static_cast<size_t>(sy) * static_cast<size_t>(src.width)];
        u8* o = &out.a[static_cast<size_t>(y) * static_cast<size_t>(w)];
        for (int x = 0; x < w; ++x) o[x] = row[sxs[static_cast<size_t>(x)]];
    }
    return out;
}

AlphaImage alphaDownsample(const AlphaImage& src) {
    AlphaImage out;
    out.width = std::max(src.width / 2, 1);
    out.height = std::max(src.height / 2, 1);
    out.a.resize(static_cast<size_t>(out.width) * static_cast<size_t>(out.height));
    const size_t sw = static_cast<size_t>(src.width);
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            const size_t x0 = static_cast<size_t>(std::min(x * 2, src.width - 1));
            const size_t x1 = static_cast<size_t>(std::min(x * 2 + 1, src.width - 1));
            const size_t y0 = static_cast<size_t>(std::min(y * 2, src.height - 1));
            const size_t y1 = static_cast<size_t>(std::min(y * 2 + 1, src.height - 1));
            const int sum = src.a[y0 * sw + x0] + src.a[y0 * sw + x1] + src.a[y1 * sw + x0] + src.a[y1 * sw + x1];
            out.a[static_cast<size_t>(y) * static_cast<size_t>(out.width) + static_cast<size_t>(x)] =
                static_cast<u8>((sum + 2) >> 2);
        }
    return out;
}

// a8Offset(x, y, side) as two table entries, one per axis (the Morton bits of x and of the memory
// row interleave without overlap, so the offset is their sum), for each level's side.
struct A8Tables {
    std::vector<u32> col, row; // col[x] + row[y] = a8Offset(x, y, side)
};
const A8Tables& a8Tables(int level) {
    static A8Tables t[10];
    A8Tables& a = t[level];
    if (a.col.empty()) {
        const int side = kShadowAtlasSide >> level;
        a.col.resize(static_cast<size_t>(side));
        a.row.resize(static_cast<size_t>(side));
        for (int x = 0; x < side; ++x) a.col[static_cast<size_t>(x)] = static_cast<u32>(a8Offset(x, side - 1, side));
        for (int y = 0; y < side; ++y)
            a.row[static_cast<size_t>(y)] = static_cast<u32>(a8Offset(0, y, side));
    }
    return a;
}

// Flushes the memory rows of `level` that a cell's rows [y0, y1) use (8x8 tiles, a tile row is
// side * 8 contiguous bytes), not the whole level.
void flushA8Rows(uint8_t* dst, int side, int y0, int y1) {
    const int my0 = side - y1, my1 = side - 1 - y0; // memory rows, y flipped
    const size_t a = static_cast<size_t>(my0 >> 3) * static_cast<size_t>(side) * 8;
    const size_t b = (static_cast<size_t>(my1 >> 3) + 1) * static_cast<size_t>(side) * 8;
    GSPGPU_FlushDataCache(dst + a, static_cast<u32>(b - a));
}

void rebuildUpperLevels(AtlasPage& pg, bool flushEach);

// atlasWrite from the alpha alone (shadow_fast_hit): the same bytes, the offsets from tables, the
// cache flushed over the rows written.
void atlasWriteAlpha(AtlasPage& pg, const AlphaImage& img, int x, int y) {
    C3D_Tex* tex = const_cast<C3D_Tex*>(c3dTexOf(pg.tex));
    const AlphaImage* cur = &img;
    AlphaImage bufs[2];
    const int cellLevels = std::min(pg.levels, 4);
    for (int level = 0; level < cellLevels; ++level) {
        u32 size = 0;
        auto* dst = static_cast<uint8_t*>(C3D_Tex2DGetImagePtr(tex, level, &size));
        const int side = kShadowAtlasSide >> level;
        const int lx = x >> level, ly = y >> level;
        const A8Tables& t = a8Tables(level);
        const u32* col = t.col.data() + lx;
        const int w = std::min(cur->width, side - lx), h = std::min(cur->height, side - ly);
        for (int yy = 0; yy < h; ++yy) {
            const u8* row = &cur->a[static_cast<size_t>(yy) * static_cast<size_t>(cur->width)];
            uint8_t* d = dst + t.row[static_cast<size_t>(ly + yy)];
            for (int xx = 0; xx < w; ++xx) d[col[xx]] = row[xx];
        }
        if (h > 0 && !g_atlasBatch) flushA8Rows(dst, side, ly, ly + h);
        if (level + 1 < cellLevels) {
            AlphaImage& b = bufs[level & 1];
            b = alphaDownsample(*cur);
            cur = &b;
        }
    }
    if (g_atlasBatch) {
        pg.batchDirty = true;
        return;
    }
    rebuildUpperLevels(pg, true);
}

// The levels past 3 of a page from its level 3 (the whole page), then flushed.
void rebuildUpperLevels(AtlasPage& pg, bool flushEach) {
    C3D_Tex* tex = const_cast<C3D_Tex*>(c3dTexOf(pg.tex));
    for (int level = 4; level < pg.levels; ++level) {
        u32 size = 0, prevSize = 0;
        auto* dst = static_cast<uint8_t*>(C3D_Tex2DGetImagePtr(tex, level, &size));
        const auto* src = static_cast<const uint8_t*>(C3D_Tex2DGetImagePtr(tex, level - 1, &prevSize));
        const int side = kShadowAtlasSide >> level;
        const A8Tables& t = a8Tables(level);
        const A8Tables& s = a8Tables(level - 1);
        for (int yy = 0; yy < side; ++yy) {
            const uint8_t* r0 = src + s.row[static_cast<size_t>(2 * yy)];
            const uint8_t* r1 = src + s.row[static_cast<size_t>(2 * yy + 1)];
            uint8_t* d = dst + t.row[static_cast<size_t>(yy)];
            for (int xx = 0; xx < side; ++xx) {
                const u32 c0 = s.col[static_cast<size_t>(2 * xx)], c1 = s.col[static_cast<size_t>(2 * xx + 1)];
                const int sum = r0[c0] + r0[c1] + r1[c0] + r1[c1];
                d[t.col[static_cast<size_t>(xx)]] = static_cast<uint8_t>((sum + 2) >> 2);
            }
        }
        if (flushEach) GSPGPU_FlushDataCache(dst, size);
    }
}

bool atlasPlaceAlpha(ShadowAtlas& a, const AlphaImage& sil, bool mipmaps, ShadowAtlasRef& ref, size_t* cellBytes) {
    const int w = std::min(roundUpPow2(std::max(sil.width, 8)), 512);
    const int h = std::min(roundUpPow2(std::max(sil.height, 8)), 512);
    if (w > kShadowAtlasSide / 2 || h > kShadowAtlasSide / 2) return false;
    AtlasPage* pg = nullptr;
    int x = 0, y = 0;
    if (!atlasFind(a, w, h, mipmaps, &pg, &x, &y)) {
        ++g_stats.atlasFull;
        return false;
    }
    if (g_atlasBatch) g_batchAtlas = &a;
    if (w != sil.width || h != sil.height) atlasWriteAlpha(*pg, alphaResizeNearest(sil, w, h), x, y);
    else atlasWriteAlpha(*pg, sil, x, y);
    const float P = static_cast<float>(kShadowAtlasSide);
    ref.page = &pg->tex;
    ref.u0 = static_cast<float>(x) / P;
    ref.v0 = static_cast<float>(y) / P;
    ref.su = static_cast<float>(w) / P;
    ref.sv = static_cast<float>(h) / P;
    ++g_stats.atlasCells;
    *cellBytes = byteSize(w, h, PicaFormat::A8, mipmaps ? std::min(pg->levels, 4) : 1);
    return true;
}

// Places a silhouette image (any size: resized as Texture2D::create would) in the atlas.
bool atlasPlace(ShadowAtlas& a, const Image& sil, bool mipmaps, ShadowAtlasRef& ref, size_t* cellBytes) {
    const int w = std::min(roundUpPow2(std::max(sil.width, 8)), 512);
    const int h = std::min(roundUpPow2(std::max(sil.height, 8)), 512);
    if (w > kShadowAtlasSide / 2 || h > kShadowAtlasSide / 2) return false;
    AtlasPage* pg = nullptr;
    int x = 0, y = 0;
    if (!atlasFind(a, w, h, mipmaps, &pg, &x, &y)) {
        ++g_stats.atlasFull;
        return false;
    }
    if (w != sil.width || h != sil.height) atlasWrite(*pg, resizeNearest(sil, w, h), x, y);
    else atlasWrite(*pg, sil, x, y);
    const float P = static_cast<float>(kShadowAtlasSide);
    ref.page = &pg->tex;
    ref.u0 = static_cast<float>(x) / P;
    ref.v0 = static_cast<float>(y) / P;
    ref.su = static_cast<float>(w) / P;
    ref.sv = static_cast<float>(h) / P;
    ++g_stats.atlasCells;
    *cellBytes = byteSize(w, h, PicaFormat::A8, mipmaps ? std::min(pg->levels, 4) : 1);
    return true;
}

} // namespace

ShadowAtlasRef lastShadowAtlasRef() { return g_lastAtlasRef; }

void resetAtlas(ShadowAtlas& a) {
    if (g_batchAtlas == &a) g_batchAtlas = nullptr;
    a.pages.clear(); // Texture2D::destroy defers the memory to the GPU fence
    a.broken = false;
    g_lastAtlasRef = ShadowAtlasRef();
    g_stats.atlasPages = g_stats.atlasCells = g_stats.atlasFull = 0;
    g_stats.atlasBytes = 0;
    g_stats.atlasPageMs = 0.0;
}

const ShadowPicaStats& shadowPicaStats() { return g_stats; }
void resetShadowPicaStats() {
    const size_t blob = g_stats.blobBytes;
    const int pages = g_stats.atlasPages, cells = g_stats.atlasCells, full = g_stats.atlasFull;
    const size_t atlasBytes = g_stats.atlasBytes;
    const double pageMs = g_stats.atlasPageMs;
    g_stats = ShadowPicaStats();
    g_stats.blobBytes = blob;
    g_stats.atlasPages = pages;
    g_stats.atlasCells = cells;
    g_stats.atlasFull = full;
    g_stats.atlasBytes = atlasBytes;
    g_stats.atlasPageMs = pageMs;
}
ShadowGenInfo lastShadowGenInfo() { return g_lastGen; }
void noteShadowFrame(int late, double placeMs) {
    g_stats.late = late;
    g_stats.placeMs = placeMs;
}
void setShadowSkinSource(std::function<const ShadowSkin*(const Texture2D*)> source) { g_skinSource = std::move(source); }
void setShadowPrecacheRotations(bool all) { g_precacheRotations = all; }
bool shadowPrecacheRotations() { return g_precacheRotations; }

size_t ShadowPass::CacheHash::operator()(const CacheKey& k) const {
    auto bits = [](float f) {
        uint32_t u;
        std::memcpy(&u, &f, sizeof u);
        return static_cast<size_t>(u);
    };
    size_t h = std::hash<const void*>()(k.texture);
    for (float f : {k.x, k.y, k.sx, k.sy}) h ^= bits(f) + 0x9e3779b9u + (h << 6) + (h >> 2);
    return h;
}

// ---------------------------------------------------------------------------------------
// ShadowPass
// ---------------------------------------------------------------------------------------

bool ShadowPass::init() {
    if (blob_.valid()) return true;
    // A radial falloff: 0.4 (102) at the centre, (1 - r^2) towards 0 at the inscribed circle;
    // the border texels stay 0 so the clamped edge casts nothing.
    Image img;
    img.width = img.height = kShadowBlobSide;
    img.hasAlpha = true;
    img.rgba.assign(static_cast<size_t>(kShadowBlobSide) * kShadowBlobSide * 4, 114);
    const float c = 0.5f * static_cast<float>(kShadowBlobSide);
    for (int y = 0; y < kShadowBlobSide; ++y) {
        for (int x = 0; x < kShadowBlobSide; ++x) {
            const float dx = (static_cast<float>(x) + 0.5f - c) / (c - 1.0f);
            const float dy = (static_cast<float>(y) + 0.5f - c) / (c - 1.0f);
            const float r2 = dx * dx + dy * dy;
            const float a = r2 < 1.0f ? kShadowMaxAlpha * (1.0f - r2) : 0.0f;
            img.rgba[(static_cast<size_t>(y) * kShadowBlobSide + static_cast<size_t>(x)) * 4 + 3] =
                static_cast<u8>(a * 255.0f + 0.5f);
        }
    }
    TextureOptions opts;
    opts.mipmaps = true;
    opts.wrapS = opts.wrapT = Wrap::ClampToEdge;
    opts.cls = TextureClass::Shadow;
    opts.res = TextureOptions::Residency::Linear;
    blob_.create(img, opts);
    g_stats.blobBytes = blob_.valid() ? blob_.bytes() : 0;
    return blob_.valid();
}

void ShadowPass::begin() {
    items_.clear();
    groups_.clear();
    drawOrder_.clear();
    total_ = 0;
    arenaVerts_ = nullptr;
    arenaFrame_ = ~0u;
    g_stats.drawn = g_stats.blobs = g_stats.vertices = g_stats.draws = 0;
    g_stats.arenaBytes = 0;
}

void ShadowPass::add(const Texture2D* texture, const GroundRect& rect, bool isBlob, bool isStatic,
                     const ShadowAtlasRef* atlas) {
    Item it;
    if (atlas && atlas->page) {
        texture = atlas->page;
        it.u0 = atlas->u0;
        it.v0 = atlas->v0;
        it.su = atlas->su;
        it.sv = atlas->sv;
    }
    if (!texture || !texture->valid()) return;
    it.texture = texture;
    it.rect = rect;
    it.blob = isBlob;
    it.isStatic = isStatic;
    items_.push_back(it);
    if (isBlob) ++g_stats.blobs;
    else ++g_stats.drawn;
}

void ShadowPass::build(const TerrainGridView& grid) {
    const u64 t0 = svcGetSystemTick();
    ++builds_;
    int draped = 0;
    // Each item's draped vertices stay where they are (the static cache, or this build's moving
    // store); draw() copies them into the arena in group order, once per frame.
    const int cur = movingCur_, prev = movingCur_ ^ 1;
    moving_[cur].clear();
    std::vector<DecalVertex>& store = movingVerts_[cur];
    store.clear();
    auto sameRect = [](const GroundRect& a, const GroundRect& b) {
        return a.origin.x == b.origin.x && a.origin.y == b.origin.y && a.axisX.x == b.axisX.x && a.axisX.y == b.axisX.y &&
               a.axisY.x == b.axisY.x && a.axisY.y == b.axisY.y && a.sizeX == b.sizeX && a.sizeY == b.sizeY;
    };
    for (Item& it : items_) {
        it.src = nullptr;
        it.first = it.count = 0;
        if (!it.isStatic) {
            const Moving* old = nullptr;
            for (const Moving& m : moving_[prev])
                if (sameRect(m.rect, it.rect)) {
                    old = &m;
                    break;
                }
            const size_t first = store.size();
            if (old) {
                const auto b = movingVerts_[prev].begin() + static_cast<std::ptrdiff_t>(old->first);
                store.insert(store.end(), b, b + static_cast<std::ptrdiff_t>(old->count));
            } else {
                buildTerrainDecal(grid, it.rect, store);
                ++draped;
            }
            Moving m;
            m.rect = it.rect;
            m.first = first;
            m.count = store.size() - first;
            moving_[cur].push_back(m);
            it.first = first; // in `store` (resolved below: the store may still grow)
            it.count = m.count;
        } else {
            const CacheKey key{it.texture, it.rect.origin.x, it.rect.origin.y, it.rect.sizeX, it.rect.sizeY};
            auto found = cache_.find(key);
            if (found == cache_.end()) {
                Cached c;
                buildTerrainDecal(grid, it.rect, c.verts);
                found = cache_.emplace(key, std::move(c)).first;
                ++draped;
            }
            found->second.lastBuild = builds_;
            it.src = found->second.verts.data(); // unordered_map nodes do not move
            it.count = found->second.verts.size();
        }
    }
    for (Item& it : items_)
        if (!it.isStatic) it.src = store.data() + it.first;
    movingCur_ = prev;
    // Forget the rectangles not seen for 120 builds (destroyed objects, out of view).
    if (builds_ % 60 == 0) {
        for (auto i = cache_.begin(); i != cache_.end();) {
            if (builds_ - i->second.lastBuild > 120) i = cache_.erase(i);
            else ++i;
        }
    }
    // Grouped by texture (first-appearance order, stable inside a group): one draw per group.
    groups_.clear();
    drawOrder_.clear();
    groupOf_.assign(items_.size(), -1);
    for (size_t i = 0; i < items_.size(); ++i) {
        const Item& it = items_[i];
        if (it.count == 0) continue;
        int g = -1;
        for (size_t k = 0; k < groups_.size(); ++k)
            if (groups_[k].texture == it.texture) g = static_cast<int>(k);
        if (g < 0) {
            g = static_cast<int>(groups_.size());
            groups_.push_back(Group{it.texture, 0, 0});
        }
        groupOf_[i] = g;
        groups_[static_cast<size_t>(g)].count += it.count;
    }
    size_t at = 0;
    for (size_t g = 0; g < groups_.size(); ++g) {
        groups_[g].first = at;
        at += groups_[g].count;
        for (size_t i = 0; i < items_.size(); ++i)
            if (groupOf_[i] == static_cast<int>(g)) drawOrder_.push_back(static_cast<int>(i));
    }
    total_ = at;
    g_stats.vertices = static_cast<int>(total_);
    g_stats.drapedNow = draped;
    g_stats.buildMs = msSince(t0);
}

void ShadowPass::draw(const Mat4& viewProj) {
    if (total_ == 0 || !backendReady()) return;
    LinearArena& arena = frameArena();
    if (!arenaVerts_ || arenaFrame_ != arena.frames()) {
        // First eye of this arena frame: the draped vertices into the arena in group order, white.
        auto* v = arena.allocArray<FlatVertex>(total_);
        arenaVerts_ = v;
        arenaFrame_ = arena.frames();
        if (!v) return;
        FlatVertex* o = v;
        for (int index : drawOrder_) {
            const Item& it = items_[static_cast<size_t>(index)];
            // An atlas cell: the draped [0, 1] coordinates into the page.
            const bool cell = it.su != 1.0f || it.sv != 1.0f || it.u0 != 0.0f || it.v0 != 0.0f;
            for (size_t k = 0; k < it.count; ++k, ++o) {
                const DecalVertex& d = it.src[k];
                o->pos[0] = d.x;
                o->pos[1] = d.y;
                o->pos[2] = d.z;
                o->uv[0] = cell ? it.u0 + d.u * it.su : d.u;
                o->uv[1] = cell ? it.v0 + d.v * it.sv : d.v;
                o->rgba[0] = o->rgba[1] = o->rgba[2] = o->rgba[3] = 255;
            }
        }
        g_stats.arenaBytes += total_ * sizeof(FlatVertex);
    }
    ShaderSet& sh = shaders();
    sh.bind(Shader::Flat);
    sh.setMvp(viewProj);
    sh.setVertices(arenaVerts_);
    applyMaterialState(decalMaterialKey(multiply_ ? DecalBlend::Filter : DecalBlend::Alpha));
    applyTev(multiply_ ? tevShadowMultiply() : tevShadow(255));
    for (const Group& g : groups_) {
        if (g.count == 0) continue;
        g.texture->bind(0);
        C3D_DrawArrays(GPU_TRIANGLES, static_cast<int>(g.first), static_cast<int>(g.count));
        ++g_stats.draws;
    }
    MaterialKey plain; // blend off, depth test + write, cull back, no offset, level fog colour
    applyMaterialState(plain);
}

} // namespace pica

// ---------------------------------------------------------------------------------------
// ShadowRenderer (as3d/shadow_render.h)
// ---------------------------------------------------------------------------------------

struct ShadowRenderer::Impl {
    RenderRules rules = renderRules(GameId::AirStrike3D);
    pica::ShadowPass pass;
    pica::ShadowAtlas atlas; // the pages of this renderer's silhouettes (shadow_pica.h)
};


ShadowRenderer::ShadowRenderer() : impl_(new Impl) {}
ShadowRenderer::~ShadowRenderer() = default;

// The world renderer's beginLevel: a new level's rules, so the atlas pages of the last level go
// (the world renderer drops every silhouette of that level right after).
void ShadowRenderer::setRules(const RenderRules& rules) {
    pica::resetAtlas(impl_->atlas);
    impl_->rules = rules;
    impl_->pass.setMultiply(rules.shadowMultiply);
}

namespace pica {
namespace {

constexpr u32 kShadowCacheMagic = 0x31574853; // "SHW1"
constexpr u32 kShadowCacheVersion = 1;

struct CachedSilhouette {
    ShadowBounds bounds;
    int width = 0, height = 0;  // the GL-side sizes (ShadowMap::width / height)
    int texW = 0, texH = 0;     // the rasterised image
    std::vector<u8> alpha;      // texW * texH, row 0 = the image's row 0
};

struct ShadowCache {
    std::string path;
    std::unordered_map<u64, CachedSilhouette> entries;
    bool dirty = false;
    ShadowCacheStats stats;
};

ShadowCache& cache() {
    static ShadowCache c;
    return c;
}

struct Fnv {
    u64 h = 1469598103934665603ull;
    void bytes(const void* p, size_t n) {
        const u8* b = static_cast<const u8*>(p);
        for (size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 1099511628211ull;
        }
    }
    template <class T> void value(const T& v) { bytes(&v, sizeof v); }
};


// The FNV state after the version and the model's geometry, per model since the cache file was
// set (shadow_fast_hit): the same bytes in the same order as the full key, so the same keys. A
// model is known by its address and its arrays' (the resource cache keeps a level's models).
struct GeometryMemo {
    const void* positions = nullptr;
    const void* uvs = nullptr;
    const void* faces = nullptr;
    size_t np = 0, nu = 0, nf = 0;
    u64 h = 0;
};
std::unordered_map<const ModelData*, GeometryMemo> g_geometryMemo;

void hashGeometry(Fnv& f, const ModelData& model) {
    f.value(kShadowCacheVersion);
    f.value(static_cast<u32>(model.positions.size()));
    for (const Vec3& v : model.positions) {
        f.value(v.x);
        f.value(v.y);
        f.value(v.z);
    }
    f.value(static_cast<u32>(model.uvs.size()));
    for (const Vec2& v : model.uvs) {
        f.value(v.x);
        f.value(v.y);
    }
    f.value(static_cast<u32>(model.faces.size()));
    for (const Face& fc : model.faces) {
        for (int k = 0; k < 3; ++k) {
            f.value(fc.v[k]);
            f.value(fc.uv[k]);
        }
    }
}

u64 silhouetteKey(const ModelData& model, const ShadowSkin* skin, ShadowKind kind, const Vec3& towardsSun,
                  int rotationSteps, ShadowQuality quality, const RenderRules& rules, ShadowCacheStats* stats) {
    Fnv f;
    GeometryMemo* memo = nullptr;
    bool reused = false;
    if (g_fastHit) {
        GeometryMemo& m = g_geometryMemo[&model];
        if (m.h != 0 && m.positions == model.positions.data() && m.uvs == model.uvs.data() &&
            m.faces == model.faces.data() && m.np == model.positions.size() && m.nu == model.uvs.size() &&
            m.nf == model.faces.size()) {
            f.h = m.h;
            reused = true;
        } else {
            memo = &m;
        }
    }
    if (reused) {
        if (stats) ++stats->keyReuses;
    } else {
        hashGeometry(f, model);
        if (memo) {
            memo->positions = model.positions.data();
            memo->uvs = model.uvs.data();
            memo->faces = model.faces.data();
            memo->np = model.positions.size();
            memo->nu = model.uvs.size();
            memo->nf = model.faces.size();
            memo->h = f.h;
        }
        if (stats) ++stats->keyModels;
    }
    f.value(skin ? skin->contentHash() : u64(0));
    f.value(static_cast<int>(kind));
    f.value(rotationSteps);
    f.value(static_cast<int>(quality));
    f.value(towardsSun.x);
    f.value(towardsSun.y);
    f.value(towardsSun.z);
    f.value(rules.shadowHeightExpCap);
    f.value(rules.shadowMaxSide);
    f.value(rules.shadowEraseBelowGround);
    f.value(rules.shadowMipmaps);
    f.value(kMaxShadowSide);
    return f.h;
}

template <class T> bool readValue(const u8*& p, const u8* end, T& v) {
    if (static_cast<size_t>(end - p) < sizeof v) return false;
    std::memcpy(&v, p, sizeof v);
    p += sizeof v;
    return true;
}

template <class T> void writeValue(std::vector<u8>& out, const T& v) {
    const u8* b = reinterpret_cast<const u8*>(&v);
    out.insert(out.end(), b, b + sizeof v);
}

} // namespace

void setShadowFastHit(bool on) { g_fastHit = on; }

void shadowAtlasBatchBegin() {
    g_atlasBatch = g_fastHit;
    g_batchAtlas = nullptr;
}

void shadowAtlasBatchEnd() {
    const bool was = g_atlasBatch;
    g_atlasBatch = false;
    if (!was || !g_batchAtlas) return;
    const u64 t0 = svcGetSystemTick();
    for (auto& pg : g_batchAtlas->pages) {
        if (!pg || !pg->batchDirty) continue;
        rebuildUpperLevels(*pg, false);
        if (C3D_Tex* tex = const_cast<C3D_Tex*>(c3dTexOf(pg->tex))) C3D_TexFlush(tex); // every level, one flush
        pg->batchDirty = false;
    }
    g_batchAtlas = nullptr;
    cache().stats.placeMs += msSince(t0); // the batch's share of the placement
}

void setShadowCacheFile(const std::string& path) {
    ShadowCache& c = cache();
    g_geometryMemo.clear(); // a new level start: new models may sit at old addresses
    c.path = path;
    c.entries.clear();
    c.dirty = false;
    c.stats = ShadowCacheStats();
    if (path.empty()) return;
    const u64 t0 = svcGetSystemTick();
    std::vector<u8> data;
    if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
        if (std::fseek(f, 0, SEEK_END) == 0) {
            const long n = std::ftell(f);
            if (n > 0 && n < (32 << 20) && std::fseek(f, 0, SEEK_SET) == 0) {
                data.resize(static_cast<size_t>(n));
                if (std::fread(data.data(), 1, data.size(), f) != data.size()) data.clear();
            }
        }
        std::fclose(f);
    }
    c.stats.fileBytes = data.size();
    const u8* p = data.data();
    const u8* end = p + data.size();
    u32 magic = 0, version = 0, count = 0;
    if (readValue(p, end, magic) && readValue(p, end, version) && readValue(p, end, count) &&
        magic == kShadowCacheMagic && version == kShadowCacheVersion) {
        for (u32 i = 0; i < count; ++i) {
            u64 key = 0;
            CachedSilhouette e;
            if (!readValue(p, end, key) || !readValue(p, end, e.bounds.xmin) || !readValue(p, end, e.bounds.ymin) ||
                !readValue(p, end, e.bounds.xmax) || !readValue(p, end, e.bounds.ymax) || !readValue(p, end, e.width) ||
                !readValue(p, end, e.height) || !readValue(p, end, e.texW) || !readValue(p, end, e.texH))
                break;
            if (e.texW <= 0 || e.texH <= 0 || e.texW > 4096 || e.texH > 4096) break;
            const size_t n = static_cast<size_t>(e.texW) * static_cast<size_t>(e.texH);
            if (static_cast<size_t>(end - p) < n) break;
            e.alpha.assign(p, p + n);
            p += n;
            c.entries.emplace(key, std::move(e));
        }
    }
    c.stats.loaded = static_cast<int>(c.entries.size());
    c.stats.loadMs = msSince(t0);
}

const ShadowCacheStats& shadowCacheStats() { return cache().stats; }

int saveShadowCache(const ShadowCacheWriter& write) {
    ShadowCache& c = cache();
    if (c.path.empty() || !c.dirty) return 0;
    const u64 t0 = svcGetSystemTick();
    std::vector<u8> out;
    writeValue(out, kShadowCacheMagic);
    writeValue(out, kShadowCacheVersion);
    writeValue(out, static_cast<u32>(c.entries.size()));
    for (const auto& kv : c.entries) {
        const CachedSilhouette& e = kv.second;
        writeValue(out, kv.first);
        writeValue(out, e.bounds.xmin);
        writeValue(out, e.bounds.ymin);
        writeValue(out, e.bounds.xmax);
        writeValue(out, e.bounds.ymax);
        writeValue(out, e.width);
        writeValue(out, e.height);
        writeValue(out, e.texW);
        writeValue(out, e.texH);
        out.insert(out.end(), e.alpha.begin(), e.alpha.end());
    }
    const size_t slash = c.path.find_last_of('/');
    if (slash != std::string::npos) makeDirectories(c.path.substr(0, slash));
    const int entries = static_cast<int>(c.entries.size());
    if (write && write(c.path, std::move(out))) {
        c.dirty = false;
        c.stats.saveMs = msSince(t0); // the serialisation only: the writer thread writes the file
        return entries;
    }
    const bool ok = writeFileAtomic(c.path, out.data(), out.size());
    c.dirty = !ok;
    c.stats.saveMs = msSince(t0);
    return ok ? static_cast<int>(c.entries.size()) : 0;
}

} // namespace pica

bool ShadowRenderer::init(std::string* error) {
    if (impl_->pass.init()) return true;
    if (error) *error = "ShadowRenderer: cannot create the blob texture";
    return false;
}

// The GL side renders this in an FBO (spec 5.2); here the CPU rasteriser gives the same
// silhouette (at most kMaxShadowSide a side: `out.width` and `out.height` are the texture's). No CPU copy is kept (`out.image` stays empty, as on the web): 591 silhouettes
// of mission 1 would hold several MB of RGBA.
bool ShadowRenderer::generate(const ModelData& model, const Texture2D* skin, ShadowKind kind, const Vec3& towardsSun,
                              int rotationSteps, ShadowQuality quality, ShadowMap& out) {
    pica::ShadowPicaStats& st = pica::g_stats;
    if (model.faces.empty() || model.positions.empty()) {
        ++st.failed;
        return false;
    }
    if (kind == ShadowKind::Planar) rotationSteps = 0;
    const u64 t0 = svcGetSystemTick();
    const ShadowSkin* cpuSkin = skin && pica::g_skinSource ? pica::g_skinSource(skin) : nullptr;
    if (cpuSkin && (!cpuSkin->valid() || cpuSkin->opaque())) cpuSkin = nullptr;
    // The GL side's size, then rasterised at most kMaxShadowSide a side over the same bounds.
    int glW = 0, glH = 0;
    shadowTextureSize(computeShadowBounds(model, kind, towardsSun, rotationSteps), quality, glW, glH, impl_->rules);
    const bool capped = glW > pica::kMaxShadowSide || glH > pica::kMaxShadowSide;
    ShadowSilhouette sil;
    pica::ShadowCache& sc = pica::cache();
    u64 key = 0;
    const pica::CachedSilhouette* hit = nullptr;
    if (!sc.path.empty()) {
        const u64 k0 = svcGetSystemTick();
        key = pica::silhouetteKey(model, cpuSkin, kind, towardsSun, rotationSteps, quality, impl_->rules, &sc.stats);
        sc.stats.keyMs += pica::msSince(k0);
        auto it = sc.entries.find(key);
        if (it != sc.entries.end()) hit = &it->second;
    }
    // shadow_fast_hit: a hit goes into the atlas from its alpha, no RGBA image.
    if (hit && pica::g_fastHit && pica::renderQuality().shadowAtlas) {
        const u64 p0 = svcGetSystemTick();
        pica::AlphaImage alpha;
        alpha.width = hit->texW;
        alpha.height = hit->texH;
        alpha.a = hit->alpha; // a copy: the entry stays for the file
        pica::g_lastAtlasRef = pica::ShadowAtlasRef();
        size_t cellBytes = 0;
        const bool placed =
            pica::atlasPlaceAlpha(impl_->atlas, alpha, impl_->rules.shadowMipmaps, pica::g_lastAtlasRef, &cellBytes);
        sc.stats.placeMs += pica::msSince(p0);
        if (placed) {
            ++sc.stats.hits;
            out.texture = Texture2D();
            out.image = Image();
            out.bounds = hit->bounds;
            out.kind = kind;
            out.width = hit->width;
            out.height = hit->height;
            pica::ShadowGenInfo& gi = pica::g_lastGen;
            gi.width = glW;
            gi.height = glH;
            gi.texWidth = hit->texW;
            gi.texHeight = hit->texH;
            gi.bytes = cellBytes;
            gi.ms = pica::msSince(t0);
            gi.skinAlpha = cpuSkin != nullptr;
            ++st.generated;
            if (capped) ++st.capped;
            int k = 0;
            while (k < 7 && (8 << k) < std::max(glW, glH)) ++k;
            ++st.bySide[k];
            st.textureBytes += gi.bytes;
            st.generateMs += gi.ms;
            st.maxGenerateMs = std::max(st.maxGenerateMs, gi.ms);
            return true;
        }
        // The atlas is full: the RGBA path below (a texture of its own), as before.
    }
    if (hit) {
        // The cached texels: the alpha of the raster's image (its colour is the constant grey).
        ++sc.stats.hits;
        sil.bounds = hit->bounds;
        sil.kind = kind;
        sil.width = hit->width;
        sil.height = hit->height;
        sil.image.width = hit->texW;
        sil.image.height = hit->texH;
        sil.image.hasAlpha = true;
        sil.image.rgba.resize(hit->alpha.size() * 4);
        for (size_t i = 0; i < hit->alpha.size(); ++i) {
            u8* o = &sil.image.rgba[i * 4];
            o[0] = o[1] = o[2] = 114;
            o[3] = hit->alpha[i];
        }
    } else {
        if (!rasterizeShadowSilhouette(model, cpuSkin, kind, towardsSun, rotationSteps, quality, impl_->rules, sil,
                                       pica::kMaxShadowSide)) {
            ++st.failed;
            return false;
        }
        if (!sc.path.empty()) {
            ++sc.stats.misses;
            pica::CachedSilhouette e;
            e.bounds = sil.bounds;
            e.width = sil.width;
            e.height = sil.height;
            e.texW = sil.image.width;
            e.texH = sil.image.height;
            e.alpha.resize(static_cast<size_t>(e.texW) * static_cast<size_t>(e.texH));
            for (size_t i = 0; i < e.alpha.size(); ++i) e.alpha[i] = sil.image.rgba[i * 4 + 3];
            sc.entries[key] = std::move(e);
            sc.dirty = true;
        }
    }
    Image img = std::move(sil.image);
    pica::g_lastAtlasRef = pica::ShadowAtlasRef();
    size_t cellBytes = 0;
    out.texture = Texture2D();
    const u64 p0 = svcGetSystemTick();
    bool placed = false;
    if (pica::renderQuality().shadowAtlas) {
        if (pica::g_fastHit) { // the alpha path for a rasterised one too (the same bytes)
            pica::AlphaImage alpha;
            alpha.width = img.width;
            alpha.height = img.height;
            alpha.a.resize(static_cast<size_t>(img.width) * static_cast<size_t>(img.height));
            for (size_t i = 0; i < alpha.a.size(); ++i) alpha.a[i] = img.rgba[i * 4 + 3];
            placed = pica::atlasPlaceAlpha(impl_->atlas, alpha, impl_->rules.shadowMipmaps, pica::g_lastAtlasRef, &cellBytes);
        } else {
            placed = pica::atlasPlace(impl_->atlas, img, impl_->rules.shadowMipmaps, pica::g_lastAtlasRef, &cellBytes);
        }
    }
    sc.stats.placeMs += pica::msSince(p0);
    if (!placed) {
        TextureOptions opts;
        opts.mipmaps = impl_->rules.shadowMipmaps;
        opts.wrapS = opts.wrapT = Wrap::ClampToEdge;
        opts.cls = TextureClass::Shadow;
        opts.res = TextureOptions::Residency::Linear;
        out.texture.create(img, opts);
    }
    const bool inAtlas = pica::g_lastAtlasRef.page != nullptr;
    out.image = Image();
    out.bounds = sil.bounds;
    out.kind = kind;
    out.width = sil.width;
    out.height = sil.height;
    pica::ShadowGenInfo& gi = pica::g_lastGen;
    gi.width = glW;
    gi.height = glH;
    gi.texWidth = img.width;
    gi.texHeight = img.height;
    gi.bytes = inAtlas ? cellBytes : out.texture.bytes();
    gi.ms = pica::msSince(t0);
    gi.skinAlpha = cpuSkin != nullptr;
    if (!inAtlas && !out.texture.valid()) {
        ++st.failed;
        return false;
    }
    ++st.generated;
    if (capped) ++st.capped;
    {
        int k = 0;
        while (k < 7 && (8 << k) < std::max(glW, glH)) ++k;
        ++st.bySide[k];
    }
    st.textureBytes += gi.bytes;
    st.generateMs += gi.ms;
    st.maxGenerateMs = std::max(st.maxGenerateMs, gi.ms);
    return true;
}

void ShadowRenderer::draw(const Terrain& terrain, const ShadowInstance* instances, size_t count,
                          const DecalViewParams& params) {
    draw(TerrainGridView::of(terrain), instances, count, params);
}

// One mono draw of `instances` (the world renderer uses pica::ShadowPass per eye instead).
// `params.projection` is a PICA projection (picaEyeProjection), as for the other PICA renderers.
void ShadowRenderer::draw(const TerrainGridView& grid, const ShadowInstance* instances, size_t count,
                          const DecalViewParams& params) {
    pica::ShadowPass& pass = impl_->pass;
    pass.begin();
    for (size_t i = 0; i < count; ++i) {
        const ShadowInstance& inst = instances[i];
        if (!inst.map || !inst.map->valid()) continue;
        pass.add(&inst.map->texture, shadowRect(*inst.map, inst.origin, inst.yawDegrees), false, false);
    }
    pass.build(grid);
    lastTriangles_ = static_cast<size_t>(pica::shadowPicaStats().vertices) / 3;
    float nearPlane, farPlane;
    pica::detail::planesOfProjection(params.projection.m, &nearPlane, &farPlane);
    pica::setDepthMapping(FogDepthMap::WBuffer, nearPlane);
    pica::setLevelFogColor(params.fogColour);
    if (params.fogStart < 1.0e7f && params.fogEnd > params.fogStart)
        pica::setFog(nearPlane, farPlane, params.fogStart, params.fogEnd);
    else
        pica::disableFog();
    pass.draw(params.projection * params.view);
}

} // namespace as3d
