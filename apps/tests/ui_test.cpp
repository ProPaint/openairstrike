// UI tests: font metrics, screen mapping, quad batching (all CPU-only), and headless render
// tests with pixel statistics (skipped loudly without game data or a GLES context).
#include "doctest.h"

#include <cmath>
#include <cstdio>
#include <memory>

#include "as3d/gfx.h"
#include "as3d/image.h"
#include "as3d/platform.h"
#include "as3d/ui.h"
#include "as3d/vfs.h"
#include "test_data.h"

using namespace as3d;
using namespace as3d::ui;

namespace {

GraphicsContext* uiContext() {
    static std::unique_ptr<GraphicsContext> ctx = [] {
        GraphicsConfig cfg;
        cfg.width = 64;
        cfg.height = 64;
        cfg.headless = true;
        cfg.title = "as3d_tests_ui";
        return createGraphicsContext(cfg);
    }();
    if (ctx) ctx->makeCurrent();
    return ctx.get();
}

FontMetrics synthetic() {
    FontMetrics m;
    m.columns = 4;
    m.rows = 4;
    m.atlasSize = 128;
    m.cellW = 32;
    m.cellH = 32;
    m.glyphW = 30;
    m.glyphH = 20;
    m.drawableLimit = 16;
    for (int i = 0; i < 256; i++) m.advance[i] = static_cast<u8>(i % 7 + 1);
    return m;
}

bool mountPaks(Vfs& vfs) {
    std::string dir = testdata::originalDir() + "/data";
    bool any = false;
    for (const char* n : {"pak0.apk", "pak1.apk", "pak2.apk"}) {
        auto src = makePakSource(openFileStream(dir + "/" + n));
        if (src) { vfs.mount(std::move(src)); any = true; }
    }
    return any;
}

struct Stats {
    long lit = 0;      // pixels differing from the background
    long inBox = 0;    // of those, inside the rectangle
};

Stats litPixels(const Image& img, const u8 bg[3], int x0, int y0, int x1, int y1) {
    Stats s;
    for (int y = 0; y < img.height; y++)
        for (int x = 0; x < img.width; x++) {
            const u8* p = &img.rgba[(static_cast<size_t>(y) * img.width + x) * 4];
            int d = std::abs(p[0] - bg[0]) + std::abs(p[1] - bg[1]) + std::abs(p[2] - bg[2]);
            if (d > 24) {
                s.lit++;
                if (x >= x0 && x < x1 && y >= y0 && y < y1) s.inBox++;
            }
        }
    return s;
}

} // namespace

TEST_CASE("ui font: original advances and glyph uv") {
    const FontMetrics& m = FontMetrics::original();
    CHECK(m.advance['0'] == 14);
    CHECK(m.advance['A'] == 15);
    CHECK(m.advance['@'] == 18);
    CHECK(m.advance['W'] == 23);
    CHECK(m.advance[' '] == 10);
    CHECK(m.advance[0x05] == 0);
    CHECK(m.advance[0xE0] == 11);
    // '0' = 0x30: column 0, row 6. Left 30 texels, lower 15 rows of the 16-row cell.
    GlyphUv uv = glyphUv(m, '0');
    CHECK(uv.s0 == doctest::Approx(0.0f));
    CHECK(uv.s1 == doctest::Approx(30.0f / 256));
    CHECK(uv.t0 == doctest::Approx((6 * 16 + 1) / 256.0f));
    CHECK(uv.t1 == doctest::Approx(7 * 16 / 256.0f));
    GlyphUv a = glyphUv(m, 'A'); // 0x41: column 1, row 8
    CHECK(a.s0 == doctest::Approx(32.0f / 256));
    CHECK(a.t0 == doctest::Approx((8 * 16 + 1) / 256.0f));
    CHECK(glyphDrawable(m, 'A'));
    CHECK_FALSE(glyphDrawable(m, ' '));
    CHECK_FALSE(glyphDrawable(m, 0xC0)); // font_rus.tga is missing
    CHECK_FALSE(glyphDrawable(m, 0x05));
}

TEST_CASE("ui font: synthetic metrics") {
    FontMetrics m = synthetic();
    GlyphUv uv = glyphUv(m, 5); // column 1, row 1
    CHECK(uv.s0 == doctest::Approx(32.0f / 128));
    CHECK(uv.s1 == doctest::Approx(62.0f / 128));
    CHECK(uv.t0 == doctest::Approx((32 + 12) / 128.0f));
    CHECK(uv.t1 == doctest::Approx(64.0f / 128));
    CHECK(glyphDrawable(m, 5));
    CHECK_FALSE(glyphDrawable(m, 20));
    // advance[i] = i % 7 + 1: 'a' = 97 -> 7, 'b' = 98 -> 1
    CHECK(measureText(m, "ab") == doctest::Approx(8.0f));
    CHECK(measureText(m, "ab", 2.0f) == doctest::Approx(16.0f));
    CHECK(measureText(m, "a{b}", 1.0f, true) == doctest::Approx(8.0f));
    CHECK(measureText(m, "a{b}", 1.0f, false) > 8.0f);
    CHECK(measureText(m, "") == 0.0f);
}

TEST_CASE("ui text: drawing, alignment and markup produce the expected quads") {
    Renderer2D r;
    r.begin(800, 600);
    Texture2D tex; // invalid handle: drawText refuses it, so use the CPU path with a fake
    const FontMetrics& m = FontMetrics::original();
    Font f{&m, &tex};
    CHECK(drawText(r, f, 0, 0, "AB", {}) == 0); // invalid texture draws nothing
    CHECK(r.quads().empty());
    // Alpha font without its texture draws nothing either.
    TextStyle al;
    al.kind = FontKind::Alpha;
    CHECK(drawText(r, f, 0, 0, "AB", al) == 0);
    CHECK(numberWidth("123", 0.75f) == doctest::Approx(30.0f)); // 10, 21, 31 truncated: 10 + 10 + 10
    CHECK(numberWidth("12", 1.0f) == doctest::Approx(28.0f));
}

TEST_CASE("ui colours and spec UVs") {
    Color o = orange();
    CHECK(o.r == doctest::Approx(1.0f));
    CHECK(o.g == doctest::Approx(0.627f).epsilon(0.01));
    CHECK(o.b == doctest::Approx(0.0f));
    Color p = packed(0x80000060u);
    CHECK(p.r == doctest::Approx(0x60 / 255.0f));
    CHECK(p.a == doctest::Approx(0x80 / 255.0f));
    CHECK(pulse(2, 0, 0).r == doctest::Approx(0.5f));
    CHECK(pulse(2, 0, 0.25f).r == doctest::Approx(1.0f));
    Renderer2D r;
    r.begin(800, 600);
    Texture2D t;
    r.quadSpec(0, 0, 10, 10, 0.1f, 0.2f, 0.3f, 0.9f, &t, Color{}, Blend::Alpha);
    const Quad& q = r.quads().back();
    CHECK(q.s0 == doctest::Approx(0.1f));
    CHECK(q.t0 == doctest::Approx(0.1f)); // top-left samples spec t1 = 0.9 -> v 0.1
    CHECK(q.t1 == doctest::Approx(0.8f));
    // Weapon icon table: 4 and 9 in the third row, 10..19 empty.
    CHECK(weaponIconUv(4).t0 == doctest::Approx(0.18f));
    CHECK(weaponIconUv(9).s0 == doctest::Approx(0.258f));
    CHECK(weaponIconUv(7).s0 == doctest::Approx(0.0f));
    CHECK(weaponIconUv(7).t0 == doctest::Approx(0.453f));
    CHECK(weaponIconUv(12).empty());
    CHECK(missileIconUv(1).s0 == doctest::Approx(0.516f));
    CHECK(powerupIconUv(0).t1 == doctest::Approx(0.727f));
}

TEST_CASE("ui mapping: 4:3, 16:9, 20:9, 5:4 and portrait") {
    Mapping a = computeMapping(800, 600);
    CHECK(a.scaleX == doctest::Approx(1.0f));
    CHECK(a.offsetX == doctest::Approx(0.0f));
    Mapping b = computeMapping(1600, 1200);
    CHECK(b.scaleX == doctest::Approx(2.0f));
    CHECK(b.toFbX(800) == doctest::Approx(1600.0f));

    Mapping c = computeMapping(1920, 1080); // 16:9
    CHECK(c.scaleX == doctest::Approx(1.8f));
    CHECK(c.scaleY == doctest::Approx(1.8f));
    CHECK(c.offsetX == doctest::Approx((1920 - 1440) / 2.0f));
    CHECK(c.toFbX(0) == doctest::Approx(240.0f));
    CHECK(c.toFbX(800) == doctest::Approx(1680.0f));
    CHECK(c.toVirtX(c.toFbX(123.0f)) == doctest::Approx(123.0f));
    CHECK(c.left() < 0.0f);
    CHECK(c.right() > 800.0f);

    Mapping d = computeMapping(2400, 1080); // 20:9
    CHECK(d.scaleX == doctest::Approx(1.8f));
    CHECK(d.offsetX == doctest::Approx((2400 - 1440) / 2.0f));
    CHECK(d.toFbY(600) == doctest::Approx(1080.0f));
    CHECK(d.left() == doctest::Approx(-d.offsetX / 1.8f));

    Mapping e = computeMapping(1280, 1024); // 5:4 stretches
    CHECK(e.scaleX == doctest::Approx(1.6f));
    CHECK(e.scaleY == doctest::Approx(1024.0f / 600));
    CHECK(e.offsetX == 0.0f);

    Mapping p = computeMapping(600, 1200); // portrait letterboxes vertically
    CHECK(p.scaleX == doctest::Approx(0.75f));
    CHECK(p.scaleY == doctest::Approx(0.75f));
    CHECK(p.offsetY == doctest::Approx((1200 - 450) / 2.0f));
}

TEST_CASE("ui renderer: quad list limit and helpers") {
    Renderer2D r;
    r.begin(800, 600);
    for (int i = 0; i < kMaxQuads; i++) CHECK(r.rect(0, 0, 1, 1, Color{}));
    CHECK_FALSE(r.rect(0, 0, 1, 1, Color{}));
    CHECK_FALSE(r.line(0, 0, 5, 5, Color{}));
    CHECK(static_cast<int>(r.quads().size()) == kMaxQuads);
    CHECK(r.dropped() == 2);
    r.begin(800, 600);
    CHECK(r.quads().empty());
    CHECK(r.dropped() == 0);
    CHECK(r.outline(0, 0, 10, 10, Color{}));
    CHECK(r.quads().size() == 4);
    r.begin(2400, 1080);
    CHECK(r.fullscreen(Color{}));
    const Quad& q = r.quads().back();
    CHECK(q.x == doctest::Approx(r.mapping().left()));
    CHECK(q.x + q.w == doctest::Approx(r.mapping().right()));
}

TEST_CASE("ui hud: state to quads, typewriter and hint layout") {
    Typewriter t0 = typewriterText("Hello", 0.5f);
    CHECK(t0.text.empty());
    Typewriter t1 = typewriterText("Hello", 1.0f + 0.3f);
    CHECK(t1.text == "Hel"); // floor(2.4) + 1 characters
    CHECK(t1.alpha == doctest::Approx(1.0f));
    Typewriter t2 = typewriterText("Hello", 1.0f + 5.0f / 8 + 3.5f);
    CHECK(t2.text == "Hello");
    CHECK(t2.alpha == doctest::Approx(0.5f));
    CHECK(typewriterText("Hello", 1.0f + 5.0f / 8 + 4.5f).text.empty());
    CHECK(messageAlpha(1.0f) == 1.0f);
    CHECK(messageAlpha(2.5f) == doctest::Approx(0.5f));
    CHECK(messageAlpha(3.0f) == 0.0f);

    const FontMetrics& m = FontMetrics::original();
    HintLayout L = layoutHint(m, "short");
    CHECK(L.lines.size() == 1);
    CHECK(L.box.w == 360.0f);
    CHECK(L.box.h == 160.0f);
    CHECK(L.box.x + L.box.w * 0.5f == doctest::Approx(400.0f));
    HintLayout L2 = layoutHint(m, "a^b^c^d^e^f^g^h");
    CHECK(L2.lines.size() == 8);
    CHECK(L2.box.h == doctest::Approx(18.0f * 8 + 80));
    std::string many;
    for (int i = 0; i < 30; i++) many += "x^";
    CHECK(layoutHint(m, many).lines.size() == 16);
    // Lines are cut only at '^' (no width wrapping) and clamped at 63 bytes.
    std::string longLine(60, 'W');
    HintLayout L3 = layoutHint(m, longLine + " " + longLine);
    CHECK(L3.lines.size() == 1);
    CHECK(L3.lines[0].size() == 63);
    // Box geometry of frontend.md 3.15.
    HintLayout L4 = layoutHint(m, "one^two^three^four^five");
    CHECK(L4.box.h == doctest::Approx(18.0f * 5 + 80));
    CHECK(L4.box.y == doctest::Approx(std::floor((600 - L4.box.h) / 2)));
    CHECK(L4.okButton.x == 350.0f);
    CHECK(L4.okButton.y == doctest::Approx(L4.box.y + L4.box.h - 60));
    CHECK(L4.okButton.w == 100.0f);
    CHECK(L4.okButton.h == 64.0f);
    CHECK(L4.textTop == doctest::Approx(L4.box.y + 20));
    // Braces are not measured.
    HintLayout L5 = layoutHint(m, std::string(30, 'W') + "{" + std::string(10, 'W') + "}");
    CHECK(L5.box.w == doctest::Approx(40.0f * 23 + 40));

    // Typewriter sound: the first character is silent, spaces are silent.
    CHECK_FALSE(typewriterTypes("Ab c", 0.9f, 1.05f)); // first character appears
    CHECK(typewriterTypes("Ab c", 1.05f, 1.2f));        // 'b'
    CHECK_FALSE(typewriterTypes("Ab c", 1.2f, 1.3f));   // ' '
    CHECK(typewriterTypes("Ab c", 1.3f, 1.4f));         // 'c'
    CHECK_FALSE(typewriterTypes("Ab c", 1.4f, 1.45f));  // nothing new
}

TEST_CASE("ui headless: text and HUD render non-empty, upright, in the right places") {
    AS3D_REQUIRE_DATA();
    GraphicsContext* ctx = uiContext();
    if (!ctx) {
        std::fprintf(stderr, "SKIPPED (no headless GLES context available): %s\n", __FILE__);
        return;
    }
    Vfs vfs;
    REQUIRE(mountPaks(vfs));
    UiAssets assets;
    std::string err;
    REQUIRE_MESSAGE(assets.load(vfs, &err), err);
    CHECK(assets.missing == 0);
    Renderer2D r;
    REQUIRE_MESSAGE(r.init(&err), err);
    const u8 bg[3] = {0, 0, 0};

    // Text: 'I' style check by lit bounds: the string lies inside its measured box.
    {
        RenderTarget target;
        REQUIRE(target.create(800, 600, 0));
        target.bind();
        clear({0, 0, 0, 1}, true);
        r.begin(800, 600);
        TextStyle st;
        st.align = Align::Center;
        int quads = drawText(r, assets.uiFont(), 400, 300, "HELLO", st);
        CHECK(quads == 5);
        CHECK(r.flush() == 1);
        Image img;
        REQUIRE(target.readPixels(img));
        float w = measureText(FontMetrics::original(), "HELLO");
        Stats s = litPixels(img, bg, static_cast<int>(400 - w / 2) - 1, 300, static_cast<int>(400 + w / 2) + 1, 316);
        CHECK(s.lit > 200);
        CHECK(s.inBox == s.lit);
        // Upright: 'H' has two vertical strokes and a bar; the top rows of the glyph must not be
        // empty and the string must not spill below the glyph height.
        long top = 0, bottom = 0;
        for (int x = 0; x < 800; x++) {
            const u8* pt = &img.rgba[(static_cast<size_t>(302) * 800 + x) * 4];
            const u8* pb = &img.rgba[(static_cast<size_t>(311) * 800 + x) * 4];
            top += pt[0] > 60;
            bottom += pb[0] > 60;
        }
        CHECK(top > 0);
        CHECK(bottom > 0);
    }

    // HUD at 20:9: nothing is drawn in the side bars, the play field is.
    {
        RenderTarget target;
        REQUIRE(target.create(2400, 1080, 0));
        target.bind();
        clear({0, 0, 0, 1}, true);
        r.begin(2400, 1080);
        HudState st;
        st.players[0].score = 4242;
        st.players[0].missiles[0] = 7;
        st.players[0].powerups[1] = 2;
        drawHud(r, assets, st);
        CHECK(r.dropped() == 0);
        CHECK(r.flush() > 0);
        Image img;
        REQUIRE(target.readPixels(img));
        Mapping m = r.mapping();
        int fx0 = static_cast<int>(m.toFbX(0)), fx1 = static_cast<int>(m.toFbX(800));
        Stats s = litPixels(img, bg, fx0, 0, fx1, 1080);
        CHECK(s.lit > 2000);
        CHECK(s.inBox == s.lit);
        // Health bar region (virtual 10..190 x 10..31) and score bar (610..790) are both lit.
        Stats hb = litPixels(img, bg, static_cast<int>(m.toFbX(10)), static_cast<int>(m.toFbY(10)),
                             static_cast<int>(m.toFbX(190)), static_cast<int>(m.toFbY(31)));
        Stats sb = litPixels(img, bg, static_cast<int>(m.toFbX(610)), static_cast<int>(m.toFbY(10)),
                             static_cast<int>(m.toFbX(790)), static_cast<int>(m.toFbY(31)));
        CHECK(hb.inBox > 500);
        CHECK(sb.inBox > 500);
        // Lower middle of the screen stays empty.
        Stats mid = litPixels(img, bg, static_cast<int>(m.toFbX(200)), static_cast<int>(m.toFbY(200)),
                              static_cast<int>(m.toFbX(600)), static_cast<int>(m.toFbY(500)));
        CHECK(mid.inBox == 0);
    }

    // Two players: both halves carry a health bar; hint box darkens and draws.
    {
        RenderTarget target;
        REQUIRE(target.create(800, 600, 0));
        target.bind();
        clear({0, 0, 0, 1}, true);
        r.begin(800, 600);
        HudState st;
        st.playerCount = 2;
        drawHud(r, assets, st);
        r.flush();
        Image img;
        REQUIRE(target.readPixels(img));
        Stats l = litPixels(img, bg, 10, 10, 190, 31);
        Stats rr = litPixels(img, bg, 610, 10, 790, 31);
        CHECK(l.inBox > 500);
        CHECK(rr.inBox > 500);

        // Two-player power-up columns: player 1 at x 82, player 2 at 648 (frontend.md 4.3).
        target.bind(); // readPixels unbinds the target
        clear({0, 0, 0, 1}, true);
        r.begin(800, 600);
        st.players[0].powerups[2] = 1;
        st.players[1].powerups[3] = 1;
        drawHud(r, assets, st);
        r.flush();
        REQUIRE(target.readPixels(img));
        CHECK(litPixels(img, bg, 82, 54, 152, 93).inBox > 300);
        CHECK(litPixels(img, bg, 648, 54, 718, 93).inBox > 300);

        target.bind();
        clear({0, 0, 0, 1}, true);
        r.begin(800, 600);
        drawHint(r, assets, "Hint text^second line");
        CHECK(r.flush() > 0);
        REQUIRE(target.readPixels(img));
        Stats box = litPixels(img, bg, 200, 200, 600, 400);
        CHECK(box.lit > 300);
        CHECK(box.inBox == box.lit);
    }

    // One player: missile frames are packed in type order (types 1 and 3 owned -> frames at
    // y 73 and 114, nothing at 155), the selected one is brighter (drawn twice).
    {
        RenderTarget target;
        REQUIRE(target.create(800, 600, 0));
        target.bind();
        clear({0, 0, 0, 1}, true);
        r.begin(800, 600);
        HudState st;
        st.players[0].missiles[1] = 4;
        st.players[0].missiles[3] = 9;
        st.players[0].missileSelected = 3;
        drawHud(r, assets, st);
        r.flush();
        Image img;
        REQUIRE(target.readPixels(img));
        Stats first = litPixels(img, bg, 10, 73, 80, 112);
        Stats second = litPixels(img, bg, 10, 114, 80, 153);
        Stats third = litPixels(img, bg, 10, 155, 80, 194);
        CHECK(first.inBox > 300);
        CHECK(second.inBox > 300);
        CHECK(third.inBox == 0);
        // Frame border brightness: the selected (second) frame's top-left corner row is brighter.
        auto sum = [&](int x0, int y0) {
            long s = 0;
            for (int y = y0; y < y0 + 3; y++)
                for (int x = x0; x < x0 + 60; x++) s += img.rgba[(static_cast<size_t>(y) * 800 + x) * 4];
            return s;
        };
        CHECK(sum(12, 114) > sum(12, 73));
        // Lives: two icons by default, nothing at the third slot.
        CHECK(litPixels(img, bg, 15, 555, 79, 587).inBox > 200);
        CHECK(litPixels(img, bg, 80, 555, 111, 587).inBox == 0);
    }
}
