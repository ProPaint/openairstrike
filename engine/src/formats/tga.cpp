// TGA (Targa) image decoder. See docs/spec/tga.md for the format subset covered here
// and the reasoning behind the choices below (in particular how `hasAlpha` is derived
// and the always-top-down orientation). Reference implementation: tools/ref/tga.py,
// which every shipped .tga is cross-checked against (see apps/tests/tga_test.cpp).
//
// No exceptions, no RTTI (this module is built with -fno-exceptions -fno-rtti):
// malformed or unsupported input is reported by returning false, never by throwing or
// asserting, and every buffer access is bounds-checked before it happens.
#include "as3d/image.h"

#include <cstring>
#include <vector>

namespace as3d {

namespace {

constexpr int kMaxDimension = 8192;

struct Rgba {
    u8 r = 0, g = 0, b = 0, a = 0;
};

struct Header {
    u8 idLength = 0;
    u8 colourMapType = 0;
    u8 imageType = 0;
    u16 cmapFirst = 0;
    u16 cmapLength = 0;
    u8 cmapDepth = 0;
    u16 width = 0;
    u16 height = 0;
    u8 pixelDepth = 0;
    u8 descriptor = 0;
};

bool readHeader(ByteReader& r, Header& h) {
    h.idLength = r.readU8();
    h.colourMapType = r.readU8();
    h.imageType = r.readU8();
    h.cmapFirst = r.readU16();
    h.cmapLength = r.readU16();
    h.cmapDepth = r.readU8();
    r.readU16(); // x origin: not meaningful for a decoder that always outputs top-down.
    r.readU16(); // y origin: ditto.
    h.width = r.readU16();
    h.height = r.readU16();
    h.pixelDepth = r.readU8();
    h.descriptor = r.readU8();
    return !r.failed();
}

// What a pixel byte (or colour map entry) means for a given image type.
enum class TypeClass { Index, TrueColor, Grey, Unsupported };

TypeClass classifyType(u8 imageType, bool& isRle) {
    isRle = (imageType == 9 || imageType == 10 || imageType == 11);
    switch (imageType) {
        case 1:
        case 9:
            return TypeClass::Index;
        case 2:
        case 10:
            return TypeClass::TrueColor;
        case 3:
        case 11:
            return TypeClass::Grey;
        default:
            return TypeClass::Unsupported;
    }
}

// Byte size of one colour entry (a pixel, or a colour map entry) at the given bit
// depth, for the depths this decoder understands. 0 means "unsupported depth".
int colourEntryBytes(int depth) {
    switch (depth) {
        case 15:
        case 16:
            return 2;
        case 24:
            return 3;
        case 32:
            return 4;
        default:
            return 0;
    }
}

inline u8 expand5(u8 v) { return static_cast<u8>((v << 3) | (v >> 2)); }

// Reads one BGR(A)-ish colour entry of `depth` bits at `p`. Caller guarantees
// `p + colourEntryBytes(depth)` is in bounds and `colourEntryBytes(depth) != 0`.
//
// Alpha is only ever taken from real data at depth 32; every other depth (including
// 16-bit true colour's spare high bit) yields a=255 -- see docs/spec/tga.md, "Alpha
// bits and hasAlpha".
Rgba readColourEntry(const u8* p, int depth) {
    Rgba c;
    if (depth == 32) {
        c.b = p[0];
        c.g = p[1];
        c.r = p[2];
        c.a = p[3];
    } else if (depth == 24) {
        c.b = p[0];
        c.g = p[1];
        c.r = p[2];
        c.a = 255;
    } else { // 15 or 16
        u16 v = static_cast<u16>(p[0] | (p[1] << 8));
        c.r = expand5(static_cast<u8>(v & 0x1F));
        c.g = expand5(static_cast<u8>((v >> 5) & 0x1F));
        c.b = expand5(static_cast<u8>((v >> 10) & 0x1F));
        c.a = 255;
    }
    return c;
}

} // namespace

bool decodeTga(const u8* data, size_t size, Image& out) {
    out = Image{};
    if (!data || size < 18) return false;

    ByteReader r(data, size);
    Header h;
    if (!readHeader(r, h)) return false;

    if (h.width == 0 || h.height == 0) return false;
    if (h.width > kMaxDimension || h.height > kMaxDimension) return false;

    r.skip(h.idLength);
    if (r.failed()) return false;

    bool isRle = false;
    TypeClass typeClass = classifyType(h.imageType, isRle);
    if (typeClass == TypeClass::Unsupported) return false;

    // A colour map, if present (colourMapType == 1), always occupies space between the
    // ID field and the pixel data -- even for a true-colour or greyscale image that
    // doesn't use it -- so it must always be skipped to find the right pixel offset,
    // not just when TypeClass::Index actually reads it.
    if (h.colourMapType != 0 && h.colourMapType != 1) return false;
    size_t cmapEntryBytes = 0;
    if (h.colourMapType == 1) {
        int eb = colourEntryBytes(h.cmapDepth);
        if (eb == 0) return false;
        cmapEntryBytes = static_cast<size_t>(eb);
    }
    size_t cmapTotalBytes = cmapEntryBytes * h.cmapLength;
    if (cmapTotalBytes > r.remaining()) return false;
    const u8* cmapBase = data + r.pos();

    std::vector<Rgba> cmap;
    int pixelBytes = 0;
    bool hasAlpha = false;

    if (typeClass == TypeClass::Index) {
        if (h.colourMapType != 1) return false; // colour-mapped image needs a colour map
        cmap.resize(h.cmapLength);
        for (u32 i = 0; i < h.cmapLength; ++i) {
            cmap[i] = readColourEntry(cmapBase + static_cast<size_t>(i) * cmapEntryBytes, h.cmapDepth);
        }
        if (h.pixelDepth != 8) return false; // only 8-bit colour map indices are supported
        pixelBytes = 1;
        hasAlpha = (h.cmapDepth == 32);
    } else if (typeClass == TypeClass::TrueColor) {
        pixelBytes = colourEntryBytes(h.pixelDepth);
        if (pixelBytes == 0) return false;
        hasAlpha = (h.pixelDepth == 32);
    } else { // Grey
        if (h.pixelDepth != 8) return false; // only 8-bit grey values are supported
        pixelBytes = 1;
        hasAlpha = false;
    }

    r.skip(cmapTotalBytes);
    if (r.failed()) return false;

    // Decodes one source pixel at `p` (which must have `pixelBytes` valid bytes) into
    // `pixel`. Returns false only for a colour-mapped index that falls outside the
    // supplied colour map -- the only per-pixel failure mode possible once the depths
    // above have already been validated.
    auto decodePixel = [&](const u8* p, Rgba& pixel) -> bool {
        switch (typeClass) {
            case TypeClass::Index: {
                int rel = static_cast<int>(p[0]) - static_cast<int>(h.cmapFirst);
                if (rel < 0 || rel >= static_cast<int>(h.cmapLength)) return false;
                pixel = cmap[static_cast<size_t>(rel)];
                return true;
            }
            case TypeClass::TrueColor:
                pixel = readColourEntry(p, h.pixelDepth);
                return true;
            case TypeClass::Grey:
                pixel = Rgba{p[0], p[0], p[0], 255};
                return true;
            default:
                return false;
        }
    };

    // width/height are each <= kMaxDimension (8192), so every product below fits
    // comfortably in size_t with wide margin (8192*8192*4 == 256 MiB).
    size_t width = h.width;
    size_t height = h.height;
    size_t totalPixels = width * height;
    size_t rgbaBytes = totalPixels * 4;

    // For the uncompressed formats the exact pixel-data size is known up front: check
    // it fits before allocating the (up to 256 MiB, at the 8192 cap) output buffer, so
    // a header claiming a huge image but backed by a short buffer fails cheaply. RLE
    // streams can't be size-checked this way (their encoded size isn't implied by the
    // header), so they're bounds-checked packet by packet below instead.
    if (!isRle) {
        size_t needed = totalPixels * static_cast<size_t>(pixelBytes);
        if (needed > r.remaining()) return false;
    }

    std::vector<u8> src(rgbaBytes, 0); // decoded pixels, still in file row order

    auto store = [&](size_t index, const Rgba& c) {
        u8* o = &src[index * 4];
        o[0] = c.r;
        o[1] = c.g;
        o[2] = c.b;
        o[3] = c.a;
    };

    if (!isRle) {
        const u8* p = data + r.pos();
        for (size_t i = 0; i < totalPixels; ++i) {
            Rgba c;
            if (!decodePixel(p + i * pixelBytes, c)) return false;
            store(i, c);
        }
    } else {
        size_t i = 0;
        size_t pos = r.pos();
        while (i < totalPixels) {
            if (pos >= size) return false; // truncated: no room for a packet header
            u8 packetHeader = data[pos++];
            size_t count = static_cast<size_t>(packetHeader & 0x7F) + 1;
            if (i + count > totalPixels) return false; // packet would overrun the image
            if (packetHeader & 0x80) {
                // Run-length packet: one pixel, repeated `count` times.
                if (pos + static_cast<size_t>(pixelBytes) > size) return false;
                Rgba c;
                if (!decodePixel(data + pos, c)) return false;
                pos += pixelBytes;
                for (size_t k = 0; k < count; ++k) store(i++, c);
            } else {
                // Raw packet: `count` distinct pixels.
                if (pos + count * static_cast<size_t>(pixelBytes) > size) return false;
                for (size_t k = 0; k < count; ++k) {
                    Rgba c;
                    if (!decodePixel(data + pos, c)) return false;
                    pos += pixelBytes;
                    store(i++, c);
                }
            }
        }
    }

    // Image descriptor byte (offset 17): bit 5 selects vertical origin (0 = bottom,
    // matching every shipped file, which this flips; 1 = top, left as-is), bit 4
    // selects horizontal origin. See docs/spec/tga.md, "Image descriptor byte".
    bool topDown = (h.descriptor >> 5) & 1;
    bool rightToLeft = (h.descriptor >> 4) & 1;

    out.width = static_cast<int>(width);
    out.height = static_cast<int>(height);
    out.hasAlpha = hasAlpha;
    out.rgba.assign(rgbaBytes, 0);

    if (!rightToLeft) {
        size_t rowBytes = width * 4;
        for (size_t y = 0; y < height; ++y) {
            size_t dstRow = topDown ? y : (height - 1 - y);
            std::memcpy(&out.rgba[dstRow * rowBytes], &src[y * rowBytes], rowBytes);
        }
    } else {
        for (size_t y = 0; y < height; ++y) {
            size_t dstRow = topDown ? y : (height - 1 - y);
            for (size_t x = 0; x < width; ++x) {
                size_t srcX = width - 1 - x;
                std::memcpy(&out.rgba[(dstRow * width + x) * 4], &src[(y * width + srcX) * 4], 4);
            }
        }
    }

    return true;
}

} // namespace as3d
