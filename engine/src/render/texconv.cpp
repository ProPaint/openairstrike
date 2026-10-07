#include "as3d/texconv.h"

#include <algorithm>

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

} // namespace

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
    out.rgba.resize(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        int sy = static_cast<int>(static_cast<long long>(y) * src.height / h);
        for (int x = 0; x < w; ++x) {
            int sx = static_cast<int>(static_cast<long long>(x) * src.width / w);
            const uint8_t* p = &src.rgba[(static_cast<size_t>(sy) * src.width + sx) * 4];
            std::copy(p, p + 4, &out.rgba[(static_cast<size_t>(y) * w + x) * 4]);
        }
    }
    return out;
}

Image downsampleBox2x2(const Image& src) {
    Image out;
    out.width = std::max(src.width / 2, 1);
    out.height = std::max(src.height / 2, 1);
    out.hasAlpha = src.hasAlpha;
    out.rgba.resize(static_cast<size_t>(out.width) * out.height * 4);
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

std::vector<uint8_t> swizzle(const Image& img, PicaFormat fmt) {
    int w = img.width, h = img.height;
    if (w <= 0 || h <= 0 || (w & 7) || (h & 7) || img.rgba.size() < static_cast<size_t>(w) * h * 4) return {};
    size_t bits = static_cast<size_t>(bitsPerTexel(fmt));
    std::vector<uint8_t> out(static_cast<size_t>(w) * h * bits / 8, 0);
    for (int y = 0; y < h; ++y) {
        int my = h - 1 - y; // memory row: bottom of the picture first
        for (int x = 0; x < w; ++x) {
            const uint8_t* p = &img.rgba[(static_cast<size_t>(y) * w + x) * 4];
            size_t idx = static_cast<size_t>((my >> 3) * (w >> 3) + (x >> 3)) * 64 + morton8(static_cast<uint32_t>(x), static_cast<uint32_t>(my));
            int r = p[0], g = p[1], b = p[2], a = p[3];
            uint8_t* d = &out[idx * bits / 8];
            switch (fmt) {
            case PicaFormat::RGBA8: d[0] = static_cast<uint8_t>(a); d[1] = static_cast<uint8_t>(b); d[2] = static_cast<uint8_t>(g); d[3] = static_cast<uint8_t>(r); break;
            case PicaFormat::RGB8: d[0] = static_cast<uint8_t>(b); d[1] = static_cast<uint8_t>(g); d[2] = static_cast<uint8_t>(r); break;
            case PicaFormat::RGBA5551:
            case PicaFormat::RGB565:
            case PicaFormat::RGBA4:
            case PicaFormat::LA8: {
                unsigned v;
                if (fmt == PicaFormat::RGBA5551) v = static_cast<unsigned>(q(r, 31) << 11 | q(g, 31) << 6 | q(b, 31) << 1 | (a >= 128));
                else if (fmt == PicaFormat::RGB565) v = static_cast<unsigned>(q(r, 31) << 11 | q(g, 63) << 5 | q(b, 31));
                else if (fmt == PicaFormat::RGBA4) v = static_cast<unsigned>(q(r, 15) << 12 | q(g, 15) << 8 | q(b, 15) << 4 | q(a, 15));
                else v = static_cast<unsigned>(lum(p) << 8 | a);
                d[0] = static_cast<uint8_t>(v);
                d[1] = static_cast<uint8_t>(v >> 8);
                break;
            }
            case PicaFormat::L8: d[0] = static_cast<uint8_t>(lum(p)); break;
            case PicaFormat::A8: d[0] = static_cast<uint8_t>(a); break;
            case PicaFormat::LA4: d[0] = static_cast<uint8_t>(q(lum(p), 15) << 4 | q(a, 15)); break;
            case PicaFormat::L4:
            case PicaFormat::A4: {
                unsigned n = static_cast<unsigned>(q(fmt == PicaFormat::L4 ? lum(p) : a, 15));
                *d = static_cast<uint8_t>(*d | ((idx & 1) ? n << 4 : n));
                break;
            }
            }
        }
    }
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
