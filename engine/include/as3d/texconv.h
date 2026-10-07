// Texture conversion for the 3DS GPU (PICA200): format choice, power-of-two resize, mip
// chains and the GPU's tiled memory layout. Platform neutral (no 3DS headers); the
// 3DS backend uploads the produced bytes unchanged. See engine/src/render/texconv.cpp.
//
// Layout conventions (3dbrew "GPU/Textures", citro3d tex3ds):
//  * The texture is stored as 8x8 tiles, tiles in row-major order, texels inside a tile in
//    Morton (Z) order: offset = x0 | y0<<1 | x1<<2 | y1<<3 | x2<<4 | y2<<5.
//  * The first memory row is the BOTTOM of the picture (PICA t=1 is the first stored row).
//    swizzle() therefore flips the image vertically: engine row 0 (top, v=0) is written to
//    the last memory row. With this, engine UVs (v=0 is the top row) are used unchanged at
//    draw time; no v = 1 - v flip is needed. Each mip level is flipped on its own.
//  * Texels are little-endian; see PicaFormat for the byte layouts.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "as3d/image.h"

namespace as3d {

enum class PicaFormat { RGBA8, RGB8, RGBA5551, RGB565, RGBA4, LA8, L8, A8, LA4, L4, A4 };

// Texel byte layouts (GPU_TEXCOLOR in citro3d, little endian):
//  RGBA8    u32 = R<<24 | G<<16 | B<<8 | A          (bytes A,B,G,R)
//  RGB8     bytes B,G,R
//  RGBA5551 u16 = R<<11 | G<<6 | B<<1 | A
//  RGB565   u16 = R<<11 | G<<5 | B
//  RGBA4    u16 = R<<12 | G<<8 | B<<4 | A
//  LA8      u16 = L<<8 | A                          (bytes A,L)
//  L8, A8   one byte
//  LA4      byte = L<<4 | A
//  L4, A4   nibbles; the even texel (in Morton stream order) is the LOW nibble
// Luminance formats sample as R=G=B=L, A=1; A formats sample as RGB=0 (unswizzle() mirrors this).
int bitsPerTexel(PicaFormat f);
// Bytes of a mip chain: levels w x h, w/2 x h/2, ... (`mipLevels` levels, at least 1), stored
// back to back.
size_t byteSize(int w, int h, PicaFormat f, int mipLevels);

enum class TextureClass { Auto, Opaque, Cutout, Alpha, Font, Lightmap, Shadow };
// Auto: alpha all 255 -> RGB565; only 0/255 -> RGBA5551; otherwise RGBA4.
// Opaque RGB565, Cutout RGBA5551, Alpha RGBA4, Font LA8, Lightmap RGB565, Shadow A8.
PicaFormat chooseFormat(const Image& img, TextureClass cls);

int roundUpPow2(int v);                                // 1 -> 1, 3 -> 4, 256 -> 256
Image resizeNearest(const Image& src, int w, int h);   // nearest, sy = y*sh/h
Image downsampleBox2x2(const Image& src);              // averages 2x2 blocks (rounded)
// Level 0 is `img` resized (nearest) to power-of-two dimensions of at least 8; each next level
// is the box downsample, while both dimensions stay >= 8.
std::vector<Image> buildMipChain(const Image& img);

// Converts to `fmt` and writes the tiled, vertically flipped layout (see the header comment).
// Width and height must be multiples of 8, otherwise the result is empty.
std::vector<uint8_t> swizzle(const Image& img, PicaFormat fmt);
// Inverse of swizzle (rows un-flipped, texels expanded to 8 bits per channel).
Image unswizzle(const uint8_t* data, int w, int h, PicaFormat fmt);

} // namespace as3d
