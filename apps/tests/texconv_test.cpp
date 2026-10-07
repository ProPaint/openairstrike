// Tests for texconv (3DS texture conversion). Platform neutral.
#include "doctest.h"

#include "as3d/texconv.h"

#include <cstdlib>

using namespace as3d;

namespace {
Image gradient(int w, int h) {
    Image img;
    img.width = w;
    img.height = h;
    img.hasAlpha = true;
    img.rgba.resize(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t* p = &img.rgba[(static_cast<size_t>(y) * w + x) * 4];
            p[0] = static_cast<uint8_t>(x * 255 / (w - 1));
            p[1] = static_cast<uint8_t>(y * 255 / (h - 1));
            p[2] = static_cast<uint8_t>((x * 7 + y * 31) & 255);
            p[3] = static_cast<uint8_t>(255 - x * 13 - y * 5);
        }
    return img;
}
Image grey(const Image& s) { // R=G=B=L from the red channel
    Image g = s;
    for (size_t i = 0; i < g.rgba.size(); i += 4) g.rgba[i + 1] = g.rgba[i + 2] = g.rgba[i];
    return g;
}
Image filled(int w, int h, uint8_t a) {
    Image img = gradient(w, h);
    for (size_t i = 3; i < img.rgba.size(); i += 4) img.rgba[i] = a;
    return img;
}
Image rt(const Image& src, PicaFormat f) {
    auto b = swizzle(src, f);
    return unswizzle(b.data(), src.width, src.height, f);
}
} // namespace

TEST_CASE("texconv: lossless round trips on a 16x8 image") {
    Image g = gradient(16, 8);
    CHECK(rt(g, PicaFormat::RGBA8).rgba == g.rgba);
    Image gr = grey(g);
    CHECK(rt(gr, PicaFormat::LA8).rgba == gr.rgba);
    Image gl = gr;
    for (size_t i = 3; i < gl.rgba.size(); i += 4) gl.rgba[i] = 255;
    CHECK(rt(gl, PicaFormat::L8).rgba == gl.rgba);
    Image a = rt(g, PicaFormat::A8);
    for (size_t i = 0; i < a.rgba.size(); i += 4) CHECK(a.rgba[i + 3] == g.rgba[i + 3]);
    Image rgb = rt(g, PicaFormat::RGB8);
    for (size_t i = 0; i < rgb.rgba.size(); i += 4)
        for (int k = 0; k < 3; ++k) CHECK(rgb.rgba[i + k] == g.rgba[i + k]);
}

TEST_CASE("texconv: lossy formats stay within quantisation") {
    Image g = gradient(16, 8);
    struct C {
        PicaFormat f;
        int tol;
    } cases[] = {{PicaFormat::RGB565, 8}, {PicaFormat::RGBA5551, 8}, {PicaFormat::RGBA4, 9}};
    for (auto c : cases) {
        auto b = swizzle(g, c.f);
        REQUIRE(b.size() == 16 * 8 * 2);
        Image r = unswizzle(b.data(), 16, 8, c.f);
        for (size_t i = 0; i < g.rgba.size(); i += 4) {
            for (int k = 0; k < 3; ++k) CHECK(std::abs(r.rgba[i + k] - g.rgba[i + k]) <= c.tol);
            if (c.f == PicaFormat::RGBA4) CHECK(std::abs(r.rgba[i + 3] - g.rgba[i + 3]) <= c.tol);
        }
    }
    Image gr = grey(g);
    for (size_t i = 3; i < gr.rgba.size(); i += 4) gr.rgba[i] = 255;
    auto b = swizzle(gr, PicaFormat::L4);
    REQUIRE(b.size() == 16 * 8 / 2);
    Image r = unswizzle(b.data(), 16, 8, PicaFormat::L4);
    for (size_t i = 0; i < gr.rgba.size(); i += 4) CHECK(std::abs(r.rgba[i] - gr.rgba[i]) <= 9);
}

TEST_CASE("texconv: tile order, Morton order and the vertical flip") {
    Image img;
    img.width = 8;
    img.height = 8;
    img.hasAlpha = true;
    img.rgba.assign(8 * 8 * 4, 0);
    uint8_t* p = &img.rgba[(5 * 8 + 3) * 4]; // x=3, y=5 (engine rows, row 0 = top)
    p[0] = 0x11;
    p[1] = 0x22;
    p[2] = 0x33;
    p[3] = 0x44;
    auto b = swizzle(img, PicaFormat::RGBA8);
    REQUIRE(b.size() == 256);
    // flipped: memory row 2, x=3 -> x=011, y=010 -> offset 1 + 4 + 8 = 13
    CHECK(b[13 * 4 + 0] == 0x44); // A
    CHECK(b[13 * 4 + 1] == 0x33); // B
    CHECK(b[13 * 4 + 2] == 0x22); // G
    CHECK(b[13 * 4 + 3] == 0x11); // R
    for (size_t i = 0; i < 64; ++i)
        if (i != 13) CHECK(b[i * 4 + 3] == 0);

    // Tiles are row-major: in a 16x16 image the engine pixel (9, 1) is at memory row 14, so tile
    // row 1, tile column 1 = tile 3; inside it x=1, y=6 -> 1 | 0<<1 | 0<<2 | 1<<3 | 0<<4 | 1<<5 = 41.
    Image big;
    big.width = 16;
    big.height = 16;
    big.rgba.assign(16 * 16 * 4, 0);
    big.rgba[(1 * 16 + 9) * 4 + 3] = 200;
    auto bb = swizzle(big, PicaFormat::A8);
    CHECK(bb[3 * 64 + 41] == 200);
}

TEST_CASE("texconv: 16-bit texel bit layouts") {
    Image one;
    one.width = 8;
    one.height = 8;
    one.rgba.assign(8 * 8 * 4, 0);
    for (size_t i = 0; i < one.rgba.size(); i += 4) {
        one.rgba[i] = 255;
        one.rgba[i + 3] = 255;
    }
    // all-red opaque: RGB565 0xF800, RGBA5551 0xF801, RGBA4 0xF00F (little endian)
    auto a = swizzle(one, PicaFormat::RGB565);
    CHECK(a[0] == 0x00);
    CHECK(a[1] == 0xF8);
    auto b = swizzle(one, PicaFormat::RGBA5551);
    CHECK(b[0] == 0x01);
    CHECK(b[1] == 0xF8);
    auto c = swizzle(one, PicaFormat::RGBA4);
    CHECK(c[0] == 0x0F);
    CHECK(c[1] == 0xF0);
}

TEST_CASE("texconv: formats and sizes") {
    Image a = filled(8, 8, 255);
    CHECK(chooseFormat(a, TextureClass::Auto) == PicaFormat::RGB565);
    Image c = filled(8, 8, 255);
    c.rgba[3] = 0;
    CHECK(chooseFormat(c, TextureClass::Auto) == PicaFormat::RGBA5551);
    c.rgba[7] = 100;
    CHECK(chooseFormat(c, TextureClass::Auto) == PicaFormat::RGBA4);
    CHECK(chooseFormat(a, TextureClass::Font) == PicaFormat::LA8);
    CHECK(chooseFormat(a, TextureClass::Shadow) == PicaFormat::A8);
    CHECK(chooseFormat(c, TextureClass::Lightmap) == PicaFormat::RGB565);
    CHECK(chooseFormat(c, TextureClass::Opaque) == PicaFormat::RGB565);
    CHECK(chooseFormat(a, TextureClass::Cutout) == PicaFormat::RGBA5551);
    CHECK(chooseFormat(a, TextureClass::Alpha) == PicaFormat::RGBA4);

    CHECK(bitsPerTexel(PicaFormat::A4) == 4);
    CHECK(bitsPerTexel(PicaFormat::RGB8) == 24);
    CHECK(byteSize(256, 256, PicaFormat::RGBA8, 1) == 262144u);
    // 256 -> 8 is 6 levels: 65536+16384+4096+1024+256+64 = 87360 texels
    CHECK(byteSize(256, 256, PicaFormat::RGBA8, 6) == 87360u * 4);
}

TEST_CASE("texconv: resize and mip chain") {
    CHECK(roundUpPow2(1) == 1);
    CHECK(roundUpPow2(100) == 128);
    CHECK(roundUpPow2(256) == 256);
    Image g = gradient(20, 12);
    Image r = resizeNearest(g, 32, 16);
    CHECK(r.width == 32);
    CHECK(r.rgba[(0 * 32 + 31) * 4] == g.rgba[(0 * 20 + 19) * 4]);
    auto chain = buildMipChain(gradient(100, 30));
    REQUIRE(chain.size() == 3); // 128x32, 64x16, 32x8
    CHECK(chain[0].width == 128);
    CHECK(chain[0].height == 32);
    CHECK(chain.back().width == 32);
    CHECK(chain.back().height == 8);
    Image flat = filled(16, 16, 77);
    CHECK(downsampleBox2x2(flat).rgba[3] == 77);
    auto tiny = buildMipChain(gradient(3, 3));
    REQUIRE(tiny.size() == 1);
    CHECK(tiny[0].width == 8);
}
