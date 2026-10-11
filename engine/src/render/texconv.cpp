#include "as3d/texconv.h"

#include <algorithm>
#include <cstring>

namespace as3d {
namespace {

inline uint32_t morton8(uint32_t x, uint32_t y) {
    return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
}
inline int q(int v, int max) { return (v * max + 127) / 255; } // 8 bits -> n bits, rounded
inline int e5(int v) { return (v << 3) | (v >> 2); }
inline int e6(int v) { return (v << 2) | (v >> 4); }
inline int e4(int v) { return v * 17; }
inline int lum(const uint8_t* p) { return (p[0] * 77 + p[1] * 150 + p[2] * 29 + 128) >> 8; }

bool g_reference = false; // setTexconvReference

} // namespace

void setTexconvReference(bool reference) { g_reference = reference; }
bool texconvReference() { return g_reference; }

int bitsPerTexel(PicaFormat f) {
    switch (f) {
    case PicaFormat::RGBA8: return 32;
    case PicaFormat::RGB8: return 24;
    case PicaFormat::RGBA5551:
    case PicaFormat::RGB565:
    case PicaFormat::RGBA4:
    case PicaFormat::LA8: return 16;
    case PicaFormat::L8:
    case PicaFormat::A8:
    case PicaFormat::LA4: return 8;
    case PicaFormat::L4:
    case PicaFormat::A4: return 4;
    }
    return 32;
}

size_t byteSize(int w, int h, PicaFormat f, int mipLevels) {
    size_t total = 0;
    for (int i = 0; i < std::max(mipLevels, 1); ++i) {
        size_t lw = static_cast<size_t>(std::max(w >> i, 1)), lh = static_cast<size_t>(std::max(h >> i, 1));
        total += lw * lh * static_cast<size_t>(bitsPerTexel(f)) / 8;
    }
    return total;
}

PicaFormat chooseFormat(const Image& img, TextureClass cls) {
    switch (cls) {
    case TextureClass::Opaque:
    case TextureClass::Lightmap: return PicaFormat::RGB565;
    case TextureClass::Cutout: return PicaFormat::RGBA5551;
    case TextureClass::Alpha: return PicaFormat::RGBA4;
    case TextureClass::Font: return PicaFormat::LA8;
    case TextureClass::Shadow: return PicaFormat::A8;
    case TextureClass::TrueColor: return PicaFormat::RGBA8;
    case TextureClass::Auto: break;
    }
    bool binary = true, opaque = true;
    for (size_t i = 3; i < img.rgba.size(); i += 4) {
        uint8_t a = img.rgba[i];
        if (a != 255) opaque = false;
        if (a != 0 && a != 255) {
            binary = false;
            break;
        }
    }
    if (opaque) return PicaFormat::RGB565;
    return binary ? PicaFormat::RGBA5551 : PicaFormat::RGBA4;
}

int roundUpPow2(int v) {
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

Image resizeNearest(const Image& src, int w, int h) {
    Image out;
    out.width = w;
    out.height = h;
    out.hasAlpha = src.hasAlpha;
    if (w <= 0 || h <= 0 || src.width <= 0 || src.height <= 0) return out;
    if (w == src.width && h == src.height) { // the same size: a plain copy
        out.rgba.assign(src.rgba.begin(), src.rgba.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(w) * h * 4));
        return out;
    }
    out.rgba.resize(static_cast<size_t>(w) * h * 4);
    std::vector<int> sxs(static_cast<size_t>(w));
    for (int x = 0; x < w; ++x) sxs[static_cast<size_t>(x)] = static_cast<int>(static_cast<long long>(x) * src.width / w);
    for (int y = 0; y < h; ++y) {
        int sy = static_cast<int>(static_cast<long long>(y) * src.height / h);
        const uint8_t* row = &src.rgba[static_cast<size_t>(sy) * src.width * 4];
        uint8_t* o = &out.rgba[static_cast<size_t>(y) * w * 4];
        for (int x = 0; x < w; ++x, o += 4) std::memcpy(o, row + static_cast<size_t>(sxs[static_cast<size_t>(x)]) * 4, 4);
    }
    return out;
}

Image downsampleBox2x2(const Image& src) {
    Image out;
    out.width = std::max(src.width / 2, 1);
    out.height = std::max(src.height / 2, 1);
    out.hasAlpha = src.hasAlpha;
    out.rgba.resize(static_cast<size_t>(out.width) * out.height * 4);
    if (!g_reference && src.width >= 2 && src.height >= 2 && (src.width & 1) == 0 && (src.height & 1) == 0) {
        // Even sides, four channels per word: the bytes of a texel as 16-bit lanes (R and B in
        // `lo`, G and A in `hi`), the four texels summed lane by lane (at most 4 x 255 + 2, no
        // carry between lanes), then (sum + 2) >> 2 per lane: the channel loop's result.
        const size_t sw = static_cast<size_t>(src.width) * 4;
        for (int y = 0; y < out.height; ++y) {
            const uint8_t* r0 = &src.rgba[static_cast<size_t>(y) * 2 * sw];
            const uint8_t* r1 = r0 + sw;
            uint8_t* o = &out.rgba[static_cast<size_t>(y) * out.width * 4];
            for (int x = 0; x < out.width; ++x, r0 += 8, r1 += 8, o += 4) {
                uint32_t p[4];
                std::memcpy(&p[0], r0, 4);
                std::memcpy(&p[1], r0 + 4, 4);
                std::memcpy(&p[2], r1, 4);
                std::memcpy(&p[3], r1 + 4, 4);
                const uint32_t m = 0x00FF00FFu;
                const uint32_t lo = (p[0] & m) + (p[1] & m) + (p[2] & m) + (p[3] & m) + 0x00020002u;
                const uint32_t hi = ((p[0] >> 8) & m) + ((p[1] >> 8) & m) + ((p[2] >> 8) & m) + ((p[3] >> 8) & m) + 0x00020002u;
                const uint32_t v = ((lo >> 2) & m) | (((hi >> 2) & m) << 8);
                std::memcpy(o, &v, 4);
            }
        }
        return out;
    }
    if (src.width >= 2 && src.height >= 2 && (src.width & 1) == 0 && (src.height & 1) == 0) {
        // Even sides (every power of two >= 2): no edge clamping, row pointers.
        const size_t sw = static_cast<size_t>(src.width) * 4;
        for (int y = 0; y < out.height; ++y) {
            const uint8_t* r0 = &src.rgba[static_cast<size_t>(y) * 2 * sw];
            const uint8_t* r1 = r0 + sw;
            uint8_t* o = &out.rgba[static_cast<size_t>(y) * out.width * 4];
            for (int x = 0; x < out.width; ++x, r0 += 8, r1 += 8, o += 4)
                for (int c = 0; c < 4; ++c) o[c] = static_cast<uint8_t>((r0[c] + r0[c + 4] + r1[c] + r1[c + 4] + 2) >> 2);
        }
        return out;
    }
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            int x0 = std::min(x * 2, src.width - 1), x1 = std::min(x * 2 + 1, src.width - 1);
            int y0 = std::min(y * 2, src.height - 1), y1 = std::min(y * 2 + 1, src.height - 1);
            for (int c = 0; c < 4; ++c) {
                int s = src.rgba[(static_cast<size_t>(y0) * src.width + x0) * 4 + c] +
                        src.rgba[(static_cast<size_t>(y0) * src.width + x1) * 4 + c] +
                        src.rgba[(static_cast<size_t>(y1) * src.width + x0) * 4 + c] +
                        src.rgba[(static_cast<size_t>(y1) * src.width + x1) * 4 + c];
                out.rgba[(static_cast<size_t>(y) * out.width + x) * 4 + c] = static_cast<uint8_t>((s + 2) >> 2);
            }
        }
    return out;
}

std::vector<Image> buildMipChain(const Image& img) {
    std::vector<Image> chain;
    int w = std::max(roundUpPow2(img.width), 8), h = std::max(roundUpPow2(img.height), 8);
    chain.push_back(resizeNearest(img, w, h));
    while (chain.back().width / 2 >= 8 && chain.back().height / 2 >= 8)
        chain.push_back(downsampleBox2x2(chain.back()));
    return chain;
}

namespace {

// The 64 texels of an 8x8 tile in memory (Morton) order: their x and y inside the tile.
struct TileOrder {
    uint8_t x[64], y[64];
    TileOrder() {
        for (uint32_t yy = 0; yy < 8; ++yy)
            for (uint32_t xx = 0; xx < 8; ++xx) {
                const uint32_t m = morton8(xx, yy);
                x[m] = static_cast<uint8_t>(xx);
                y[m] = static_cast<uint8_t>(yy);
            }
    }
};

// q(v, max) for every byte value.
struct QuantTables {
    uint8_t q31[256], q63[256], q15[256];
    QuantTables() {
        for (int v = 0; v < 256; ++v) {
            q31[v] = static_cast<uint8_t>(q(v, 31));
            q63[v] = static_cast<uint8_t>(q(v, 63));
            q15[v] = static_cast<uint8_t>(q(v, 15));
        }
    }
};

const TileOrder& tileOrder() {
    static const TileOrder t;
    return t;
}

const QuantTables& quant() {
    static const QuantTables t;
    return t;
}

// Walks the texels in memory order (tiles row-major from the bottom of the picture, Morton
// inside a tile) and calls put(index, source pixel) for each.
template <class Put> void forEachTexel(const Image& img, Put put) {
    const int w = img.width, h = img.height;
    const TileOrder& to = tileOrder();
    const uint8_t* rgba = img.rgba.data();
    size_t idx = 0;
    if (!g_reference) {
        // The tile's eight source rows once per tile, the texel's offset in its row per texel.
        uint8_t xo[64];
        for (int i = 0; i < 64; ++i) xo[i] = static_cast<uint8_t>(to.x[i] * 4);
        const size_t stride = static_cast<size_t>(w) * 4;
        for (int ty = 0; ty < h; ty += 8) {
            const uint8_t* rows[8];
            for (int j = 0; j < 8; ++j) rows[j] = rgba + static_cast<size_t>(h - 1 - (ty + j)) * stride;
            for (int tx = 0; tx < w; tx += 8) {
                const size_t xb = static_cast<size_t>(tx) * 4;
                for (int i = 0; i < 64; ++i, ++idx) put(idx, rows[to.y[i]] + xb + xo[i]);
            }
        }
        return;
    }
    for (int ty = 0; ty < h; ty += 8) {     // memory tile rows: ty = my of the tile's first row
        for (int tx = 0; tx < w; tx += 8) {
            for (int i = 0; i < 64; ++i, ++idx) {
                const int x = tx + to.x[i], my = ty + to.y[i];
                const int y = h - 1 - my; // memory row my is picture row h - 1 - my
                put(idx, rgba + (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4);
            }
        }
    }
}

} // namespace

bool swizzleInto(const Image& img, PicaFormat fmt, uint8_t* out, size_t outBytes) {
    const int w = img.width, h = img.height;
    if (w <= 0 || h <= 0 || (w & 7) || (h & 7) || img.rgba.size() < static_cast<size_t>(w) * h * 4) return false;
    const size_t bytes = static_cast<size_t>(w) * h * static_cast<size_t>(bitsPerTexel(fmt)) / 8;
    if (!out || outBytes < bytes) return false;
    const QuantTables& qt = quant();
    switch (fmt) {
    case PicaFormat::RGBA8:
        forEachTexel(img, [out](size_t i, const uint8_t* p) {
            uint8_t* d = out + i * 4;
            d[0] = p[3];
            d[1] = p[2];
            d[2] = p[1];
            d[3] = p[0];
        });
        break;
    case PicaFormat::RGB8:
        forEachTexel(img, [out](size_t i, const uint8_t* p) {
            uint8_t* d = out + i * 3;
            d[0] = p[2];
            d[1] = p[1];
            d[2] = p[0];
        });
        break;
    case PicaFormat::RGBA5551:
        forEachTexel(img, [out, &qt](size_t i, const uint8_t* p) {
            const unsigned v = static_cast<unsigned>(qt.q31[p[0]] << 11 | qt.q31[p[1]] << 6 | qt.q31[p[2]] << 1 | (p[3] >= 128));
            out[i * 2] = static_cast<uint8_t>(v);
            out[i * 2 + 1] = static_cast<uint8_t>(v >> 8);
        });
        break;
    case PicaFormat::RGB565:
        forEachTexel(img, [out, &qt](size_t i, const uint8_t* p) {
            const unsigned v = static_cast<unsigned>(qt.q31[p[0]] << 11 | qt.q63[p[1]] << 5 | qt.q31[p[2]]);
            out[i * 2] = static_cast<uint8_t>(v);
            out[i * 2 + 1] = static_cast<uint8_t>(v >> 8);
        });
        break;
    case PicaFormat::RGBA4:
        forEachTexel(img, [out, &qt](size_t i, const uint8_t* p) {
            const unsigned v = static_cast<unsigned>(qt.q15[p[0]] << 12 | qt.q15[p[1]] << 8 | qt.q15[p[2]] << 4 | qt.q15[p[3]]);
            out[i * 2] = static_cast<uint8_t>(v);
            out[i * 2 + 1] = static_cast<uint8_t>(v >> 8);
        });
        break;
    case PicaFormat::LA8:
        forEachTexel(img, [out](size_t i, const uint8_t* p) {
            out[i * 2] = p[3];
            out[i * 2 + 1] = static_cast<uint8_t>(lum(p));
        });
        break;
    case PicaFormat::L8:
        forEachTexel(img, [out](size_t i, const uint8_t* p) { out[i] = static_cast<uint8_t>(lum(p)); });
        break;
    case PicaFormat::A8:
        forEachTexel(img, [out](size_t i, const uint8_t* p) { out[i] = p[3]; });
        break;
    case PicaFormat::LA4:
        forEachTexel(img, [out, &qt](size_t i, const uint8_t* p) {
            out[i] = static_cast<uint8_t>(qt.q15[lum(p)] << 4 | qt.q15[p[3]]);
        });
        break;
    case PicaFormat::L4:
    case PicaFormat::A4: {
        std::memset(out, 0, bytes);
        const bool l = fmt == PicaFormat::L4;
        forEachTexel(img, [out, &qt, l](size_t i, const uint8_t* p) {
            const unsigned n = qt.q15[l ? lum(p) : p[3]];
            out[i / 2] = static_cast<uint8_t>(out[i / 2] | ((i & 1) ? n << 4 : n));
        });
        break;
    }
    }
    return true;
}

std::vector<uint8_t> swizzle(const Image& img, PicaFormat fmt) {
    const int w = img.width, h = img.height;
    if (w <= 0 || h <= 0 || (w & 7) || (h & 7) || img.rgba.size() < static_cast<size_t>(w) * h * 4) return {};
    std::vector<uint8_t> out(static_cast<size_t>(w) * h * static_cast<size_t>(bitsPerTexel(fmt)) / 8, 0);
    swizzleInto(img, fmt, out.data(), out.size());
    return out;
}

Image unswizzle(const uint8_t* data, int w, int h, PicaFormat fmt) {
    Image img;
    img.width = w;
    img.height = h;
    img.hasAlpha = true;
    if (!data || w <= 0 || h <= 0 || (w & 7) || (h & 7)) return img;
    size_t bits = static_cast<size_t>(bitsPerTexel(fmt));
    img.rgba.resize(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        int my = h - 1 - y;
        for (int x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>((my >> 3) * (w >> 3) + (x >> 3)) * 64 + morton8(static_cast<uint32_t>(x), static_cast<uint32_t>(my));
            const uint8_t* d = data + idx * bits / 8;
            int r = 0, g = 0, b = 0, a = 255;
            int v = d[0] | (bits == 16 ? d[1] << 8 : 0);
            switch (fmt) {
            case PicaFormat::RGBA8: a = d[0]; b = d[1]; g = d[2]; r = d[3]; break;
            case PicaFormat::RGB8: b = d[0]; g = d[1]; r = d[2]; break;
            case PicaFormat::RGBA5551:
                r = e5(v >> 11); g = e5((v >> 6) & 31); b = e5((v >> 1) & 31); a = (v & 1) ? 255 : 0; break;
            case PicaFormat::RGB565: r = e5(v >> 11); g = e6((v >> 5) & 63); b = e5(v & 31); break;
            case PicaFormat::RGBA4:
                r = e4(v >> 12); g = e4((v >> 8) & 15); b = e4((v >> 4) & 15); a = e4(v & 15); break;
            case PicaFormat::LA8: r = g = b = v >> 8; a = v & 255; break;
            case PicaFormat::L8: r = g = b = d[0]; break;
            case PicaFormat::A8: a = d[0]; break;
            case PicaFormat::LA4: r = g = b = e4(d[0] >> 4); a = e4(d[0] & 15); break;
            case PicaFormat::L4:
            case PicaFormat::A4: {
                int n = (idx & 1) ? d[0] >> 4 : d[0] & 15;
                if (fmt == PicaFormat::L4) r = g = b = e4(n);
                else a = e4(n);
                break;
            }
            }
            uint8_t* o = &img.rgba[(static_cast<size_t>(y) * w + x) * 4];
            o[0] = static_cast<uint8_t>(r);
            o[1] = static_cast<uint8_t>(g);
            o[2] = static_cast<uint8_t>(b);
            o[3] = static_cast<uint8_t>(a);
        }
    }
    return img;
}

} // namespace as3d
