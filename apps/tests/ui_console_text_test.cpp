// The console text sizes and the 2D foundation of the 3DS UI (docs/3ds-ui-design.md "UI
// foundation and the screen gallery"): glyph advances per size, the S atlas built from a synthetic
// font (checksum), the outline pass on a synthetic glyph, the pixel mapping, half-size pictures and
// the key hints. Synthetic data only (no game font).
#include "doctest.h"

#include <cmath>
#include <cstdio>
#include <memory>

#include "as3d/gfx.h"
#include "as3d/image.h"
#include "as3d/platform.h"
#include "as3d/ui.h"

using namespace as3d;
using namespace as3d::ui;

namespace {

GraphicsContext* consoleContext() {
    static std::unique_ptr<GraphicsContext> ctx = [] {
        GraphicsConfig cfg;
        cfg.width = 64;
        cfg.height = 64;
        cfg.headless = true;
        cfg.title = "as3d_tests_console_text";
        return createGraphicsContext(cfg);
    }();
    if (ctx) ctx->makeCurrent();
    return ctx.get();
}

// A 256 x 256 font in font.tga's layout (8 x 16 cells of 32 x 16, the glyph the left 30 and lower
// 15 texels): per cell a vertical one-texel stroke, a horizontal bar and one half-intensity texel,
// at positions that depend on the cell. `alphaFont`: the intensity in alpha (font_alpha.tga),
// else in RGB with alpha 255 (font.tga).
Image syntheticFont(bool alphaFont) {
    Image img;
    img.width = img.height = 256;
    img.hasAlpha = true;
    img.rgba.assign(256 * 256 * 4, 0);
    auto set = [&](int x, int y, uint8_t v) {
        uint8_t* p = &img.rgba[(static_cast<size_t>(y) * 256 + static_cast<size_t>(x)) * 4];
        if (alphaFont) {
            p[0] = p[1] = p[2] = 255;
            p[3] = v;
        } else {
            p[0] = p[1] = p[2] = v;
        }
    };
    if (!alphaFont)
        for (size_t i = 3; i < img.rgba.size(); i += 4) img.rgba[i] = 255;
    for (int idx = 0; idx < 128; ++idx) {
        const int x0 = (idx % 8) * 32, y0 = (idx / 8) * 16;
        const int sx = idx % 20 + 2;
        for (int y = 3; y <= 12; ++y) set(x0 + sx, y0 + y, 255); // the stroke, cap rows 3..12
        const int by = idx % 9 + 3, bw = idx % 13 + 3;
        for (int x = 1; x < 1 + bw; ++x) set(x0 + x, y0 + by, 255);
        set(x0 + 25, y0 + 8, 128);
    }
    return img;
}

uint32_t fnv1a(const std::vector<uint8_t>& v) {
    uint32_t h = 2166136261u;
    for (uint8_t b : v) {
        h ^= b;
        h *= 16777619u;
    }
    return h;
}

uint8_t texel(const Image& img, int x, int y, int c) {
    return img.rgba[(static_cast<size_t>(y) * static_cast<size_t>(img.width) + static_cast<size_t>(x)) * 4 + static_cast<size_t>(c)];
}

// The three font textures of a console Font from the synthetic font (needs a GL context).
struct ConsoleFont {
    Texture2D font, alpha, small;
    FontMetrics smallMetrics = smallFontMetrics(FontMetrics::original());
    bool make() {
        TextureOptions o;
        o.minFilter = o.magFilter = Filter::Nearest;
        o.wrapS = o.wrapT = Wrap::ClampToEdge;
        font.create(syntheticFont(false), o);
        alpha.create(syntheticFont(true), o);
        small.create(smallFontAtlas(syntheticFont(false), FontMetrics::original()), o);
        return font.valid() && alpha.valid() && small.valid();
    }
    Font get() const {
        Font f;
        f.metrics = &FontMetrics::original();
        f.texture = &font;
        f.alphaTexture = &alpha;
        f.consoleSizes = true;
        f.smallTexture = &small;
        f.smallMetrics = &smallMetrics;
        return f;
    }
};

} // namespace

TEST_CASE("console text: sizes, integer advances per size") {
    const FontMetrics& m = FontMetrics::original();
    CHECK(fontSizeScale(FontSize::S) == doctest::Approx(0.8f));
    CHECK(fontSizeScale(FontSize::L) == doctest::Approx(1.0f));
    CHECK(fontSizeScale(FontSize::T) == doctest::Approx(2.0f));
    CHECK(fontCapPx(FontSize::S) == 8);
    CHECK(fontCapPx(FontSize::L) == 10);
    CHECK(fontCapPx(FontSize::T) == 20);
    // Today's 3DS scales on screen: body text 0.4, HUD counts 0.6, items 0.8, PAUSED 1.2.
    CHECK(consoleSizeFor(0.4f) == FontSize::S);
    CHECK(consoleSizeFor(0.6f) == FontSize::S);
    CHECK(consoleSizeFor(0.8f) == FontSize::L);
    CHECK(consoleSizeFor(1.0f) == FontSize::L);
    CHECK(consoleSizeFor(1.2f) == FontSize::T);
    // 'H' 14, ' ' 10, '!' 3, 'W' 23, 'i' 3.
    CHECK(glyphAdvancePx(m, FontSize::L, 'H') == 14);
    CHECK(glyphAdvancePx(m, FontSize::T, 'H') == 28);
    CHECK(glyphAdvancePx(m, FontSize::S, 'H') == 11);
    CHECK(glyphAdvancePx(m, FontSize::S, ' ') == 8);
    CHECK(glyphAdvancePx(m, FontSize::S, '!') == 2);
    CHECK(glyphAdvancePx(m, FontSize::S, 'W') == 18);
    CHECK(glyphAdvancePx(m, FontSize::S, 0x7F) == 0); // no advance stays none
    for (int c = 0; c < 256; ++c) {
        const unsigned char u = static_cast<unsigned char>(c);
        CHECK(glyphAdvancePx(m, FontSize::T, u) == 2 * m.advance[c]);
        if (m.advance[c] > 0) CHECK(glyphAdvancePx(m, FontSize::S, u) >= 1);
    }
    CHECK(measureTextPx(m, "HH", FontSize::S) == 22);
    CHECK(measureTextPx(m, "{H}H", FontSize::L, true) == 28);
    CHECK(numberAdvancePx(FontSize::S) == 11);
    CHECK(numberAdvancePx(FontSize::L) == 14);
    CHECK(numberAdvancePx(FontSize::T) == 28);
    const FontMetrics s = smallFontMetrics(m);
    CHECK(s.glyphW == doctest::Approx(24.0f));
    CHECK(s.glyphH == doctest::Approx(12.0f));
    CHECK(s.advance[static_cast<unsigned char>('H')] == 11);
    const GlyphUv uv = glyphUv(s, 'H'); // cell (0, 9): the lower 12 rows, the left 24 texels
    CHECK(uv.s0 == doctest::Approx(0.0f));
    CHECK(uv.s1 == doctest::Approx(24.0f / 256.0f));
    CHECK(uv.t0 == doctest::Approx((9 * 16 + 4) / 256.0f));
    CHECK(uv.t1 == doctest::Approx((9 * 16 + 16) / 256.0f));
}

TEST_CASE("console text: the S atlas from a synthetic font") {
    const FontMetrics& m = FontMetrics::original();
    const Image font = syntheticFont(false);
    const Image s = smallFontAtlas(font, m);
    REQUIRE(s.width == 256);
    REQUIRE(s.height == 256);
    REQUIRE(s.rgba.size() == 256u * 256u * 4u);
    // RGB and alpha hold the same value (one LA8 texture for both kinds).
    bool same = true;
    for (size_t i = 0; i < s.rgba.size(); i += 4)
        same = same && s.rgba[i] == s.rgba[i + 1] && s.rgba[i] == s.rgba[i + 2] && s.rgba[i] == s.rgba[i + 3];
    CHECK(same);
    // Nothing outside the glyphs' 24 x 12 areas.
    long outside = 0;
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x) {
            const bool in = (x % 32) < 24 && (y % 16) >= 4;
            if (!in && texel(s, x, y, 0)) ++outside;
        }
    CHECK(outside == 0);
    // Cell 3: the stroke at source column 5 lies wholly inside output column 4 ([5, 6.25)): full
    // intensity there after the contrast, over the 8 rows of the 10-texel cap.
    {
        const int x0 = 3 * 32, y0 = 0;
        int full = 0;
        for (int y = y0 + 4; y < y0 + 16; ++y) full += texel(s, x0 + 4, y, 0) == 255;
        CHECK(full >= 7);
        CHECK(texel(s, x0 + 3, y0 + 10, 0) == 0);
    }
    // The same atlas from the alpha twin (the intensity in alpha).
    CHECK(smallFontAtlas(syntheticFont(true), m).rgba == s.rgba);
    // The algorithm's checksum (a change of the resample shows here; update with the reason).
    CHECK(fnv1a(s.rgba) == 2138097201u);
    // A too-small source gives an empty (all zero) atlas, no crash.
    Image tiny;
    tiny.width = tiny.height = 8;
    tiny.rgba.assign(8 * 8 * 4, 255);
    const Image e = smallFontAtlas(tiny, m);
    CHECK(e.width == 256);
    long lit = 0;
    for (uint8_t v : e.rgba) lit += v != 0;
    CHECK(lit == 0);
}

TEST_CASE("console text: pixel mapping and half-size pictures") {
    const Mapping p = pixelMapping(320, 240);
    CHECK(p.scaleX == 1.0f);
    CHECK(p.scaleY == 1.0f);
    CHECK(p.offsetX == 0.0f);
    CHECK(p.toFbX(17.0f) == 17.0f);
    Renderer2D r;
    r.beginPixels(400, 240);
    CHECK(r.mapping().fbWidth == 400);
    CHECK(r.mapping().scaleY == 1.0f);
    r.begin(400, 240); // the scaled mapping again
    CHECK(r.mapping().scaleY == doctest::Approx(0.4f));

    // 10 x 6: 5 x 3 picture in an 8 x 8 texture. Block (0, 0): two opaque red and two transparent
    // texels: red at alpha 128 (alpha-weighted, no darkening).
    Image src;
    src.width = 10;
    src.height = 6;
    src.hasAlpha = true;
    src.rgba.assign(10 * 6 * 4, 0);
    auto put = [&](int x, int y, uint8_t r8, uint8_t g8, uint8_t b8, uint8_t a8) {
        uint8_t* q = &src.rgba[(static_cast<size_t>(y) * 10 + static_cast<size_t>(x)) * 4];
        q[0] = r8;
        q[1] = g8;
        q[2] = b8;
        q[3] = a8;
    };
    put(0, 0, 255, 0, 0, 255);
    put(1, 0, 255, 0, 0, 255);
    for (int y = 2; y < 4; ++y)
        for (int x = 2; x < 4; ++x) put(x, y, 0, 200, 100, 255); // block (1, 1): opaque
    int w = 0, h = 0;
    const Image half = halfSizePicture(src, &w, &h);
    CHECK(w == 5);
    CHECK(h == 3);
    CHECK(half.width == 8);
    CHECK(half.height == 8);
    CHECK(texel(half, 0, 0, 0) == 255);
    CHECK(texel(half, 0, 0, 1) == 0);
    CHECK(texel(half, 0, 0, 3) == 128);
    CHECK(texel(half, 1, 1, 1) == 200);
    CHECK(texel(half, 1, 1, 3) == 255);
    CHECK(texel(half, 6, 0, 3) == 0); // padding
    CHECK(texel(half, 0, 5, 3) == 0);
    const Image none = halfSizePicture(Image{}, &w, &h);
    CHECK(none.width == 0);
}

TEST_CASE("console text: glyph quads on whole pixels, sizes, the outline pass on a synthetic glyph") {
    GraphicsContext* ctx = consoleContext();
    if (!ctx) {
        std::fprintf(stderr, "SKIPPED (no headless GLES context available): %s\n", __FILE__);
        return;
    }
    ConsoleFont cf;
    REQUIRE(cf.make());
    const Font f = cf.get();
    Renderer2D r;
    std::string err;
    REQUIRE_MESSAGE(r.init(&err), err);

    // The 3DS bottom screen's scaled mapping (0.4): body text at scale 1 becomes S, its quads on
    // whole pixels, advancing by the S advances, the left edge kept.
    {
        r.begin(320, 240);
        TextStyle st; // scale 1: 0.4 px per texel on screen
        st.effect = TextEffect::None;
        REQUIRE(drawText(r, f, 100, 300, "HH", st) == 2);
        float px[4], py[4];
        r.quadCorners(r.quads()[0], px, py);
        CHECK(px[0] == doctest::Approx(40.0f).epsilon(1e-4));
        CHECK(px[2] - px[0] == doctest::Approx(24.0f).epsilon(1e-4)); // S glyph 24 px wide
        CHECK(py[1] - py[0] == doctest::Approx(12.0f).epsilon(1e-4));
        CHECK(std::fabs(py[0] - std::round(py[0])) < 1e-3f);
        float qx[4], qy[4];
        r.quadCorners(r.quads()[1], qx, qy);
        CHECK(qx[0] - px[0] == doctest::Approx(11.0f).epsilon(1e-4));
        CHECK(r.quads()[0].texture == &cf.small);
        // Scale 2 (0.8 on screen): L from font.tga; scale 3 (1.2): T with its outline (8 + 1 quads).
        r.begin(320, 240);
        st.scale = 2.0f;
        REQUIRE(drawText(r, f, 100, 300, "H", st) == 1);
        CHECK(r.quads()[0].texture == &cf.font);
        r.quadCorners(r.quads()[0], px, py);
        CHECK(px[2] - px[0] == doctest::Approx(30.0f).epsilon(1e-4));
        r.begin(320, 240);
        TextStyle big;
        big.scale = 3.0f;
        REQUIRE(drawText(r, f, 100, 300, "H", big) == 9);
        CHECK(r.quads()[0].texture == &cf.alpha);
        CHECK(r.quads()[8].texture == &cf.font);
        r.quadCorners(r.quads()[8], px, py);
        CHECK(px[2] - px[0] == doctest::Approx(60.0f).epsilon(1e-4));
        // A T line too wide for the screen from its anchor steps down to L, then S.
        r.begin(320, 240);
        REQUIRE(drawText(r, f, 400, 300, "HHHHHHHHHHHHHHHHHHHHHHHH", TextStyle{3.0f, Color{}, Align::Center}) > 0);
        CHECK(r.quads().back().texture == &cf.small);
        // Desktop fonts (consoleSizes off) keep the scaled routine.
        Font plain = f;
        plain.consoleSizes = false;
        r.begin(320, 240);
        REQUIRE(drawText(r, plain, 100, 300, "H", TextStyle{}) == 1);
        r.quadCorners(r.quads()[0], px, py);
        CHECK(px[2] - px[0] == doctest::Approx(12.0f).epsilon(1e-4)); // 30 texels at 0.4
        // The number routine: L at scale 2, 14 px apart.
        r.begin(320, 240);
        REQUIRE(drawNumber(r, f, 50, 50, "12", 2.0f, Color{}) == 2);
        r.quadCorners(r.quads()[0], px, py);
        r.quadCorners(r.quads()[1], qx, qy);
        CHECK(qx[0] - px[0] == doctest::Approx(14.0f).epsilon(1e-4));
    }

    // The outline pass rendered: 'H' of the synthetic font (cell 72: stroke at column 14) in L,
    // white additive over a mid grey, with its outline: the stroke white, the columns either side
    // black, two columns away grey.
    {
        RenderTarget target;
        REQUIRE(target.create(64, 32, 0));
        target.bind();
        clear({0.5f, 0.5f, 0.5f, 1}, true);
        r.beginPixels(64, 32);
        TextStyle st;
        st.size = FontSize::L;
        st.effect = TextEffect::Outline;
        REQUIRE(drawText(r, f, 10, 4, "H", st) == 9);
        r.flush();
        Image img;
        REQUIRE(target.readPixels(img));
        const int sx = 10 + (72 % 20 + 2); // the stroke's column on screen
        // Row: glyph top 4 + (cap row 6 of the cell - 1 empty row above the glyph).
        const int y = 4 + 5;
        CHECK(texel(img, sx, y, 0) > 240);
        CHECK(texel(img, sx - 1, y, 0) < 16);
        CHECK(texel(img, sx + 1, y, 0) < 16);
        CHECK(std::abs(texel(img, sx + 3, y, 0) - 128) < 8);
    }

    // Key hints without an atlas: the key's name framed, then the label; with one, its rectangle.
    {
        r.beginPixels(320, 240);
        KeyHintStyle ks;
        const int w = drawKeyHint(r, f, nullptr, 10, 200, HintKey::A, "Select", ks);
        const FontMetrics& m = FontMetrics::original();
        CHECK(w == measureTextPx(m, "A", FontSize::S) + 4 + ks.gap + measureTextPx(m, "Select", FontSize::S));
        KeyGlyphAtlas atlas;
        atlas.texture = &cf.font;
        atlas.rects[static_cast<int>(HintKey::B)] = {0, 0, 12, 12};
        r.beginPixels(320, 240);
        const int w2 = drawKeyHint(r, f, &atlas, 10, 200, HintKey::B, "", ks);
        CHECK(w2 == 12);
        REQUIRE(r.quads().size() == 1);
        CHECK(r.quads()[0].texture == &cf.font);
        CHECK(r.quads()[0].w == doctest::Approx(12.0f));
    }
}
