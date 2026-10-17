// Font metrics, text measurement and the three text routines (render-pipeline.md 8.3,
// frontend.md 2.8).
#include <algorithm>
#include <cmath>
#include <vector>

#include "as3d/ui.h"

namespace as3d::ui {

namespace {

FontMetrics makeOriginal() {
    FontMetrics m;
    // Advance widths by 16-byte row; rows not listed are 0 (table at 0x456598 of the original).
    struct Row { int base; u8 v[16]; };
    static const Row rows[] = {
        {0x10, {8, 17, 17, 12, 14, 14, 11, 19, 19, 9, 9, 20, 21, 21, 12, 0}},
        {0x20, {10, 3, 7, 12, 14, 19, 16, 4, 5, 5, 9, 12, 3, 7, 4, 15}},
        {0x30, {14, 8, 12, 13, 14, 13, 13, 13, 13, 13, 3, 3, 11, 11, 11, 11}},
        {0x40, {18, 15, 13, 13, 14, 11, 11, 14, 14, 3, 11, 13, 11, 17, 15, 13}},
        {0x50, {13, 15, 13, 13, 13, 13, 15, 23, 15, 14, 13, 5, 8, 5, 11, 12}},
        {0x60, {5, 11, 11, 11, 11, 11, 7, 11, 10, 3, 4, 10, 3, 18, 10, 11}},
        {0x70, {11, 11, 9, 11, 9, 10, 11, 16, 10, 10, 10, 6, 3, 6, 11, 0}},
        {0xA0, {0, 0, 0, 0, 0, 0, 0, 0, 12, 0, 0, 0, 0, 0, 0, 0}},
        {0xB0, {0, 0, 0, 0, 0, 0, 0, 0, 12, 0, 0, 0, 0, 0, 0, 0}},
        {0xC0, {15, 13, 12, 14, 13, 12, 14, 12, 14, 14, 13, 14, 15, 13, 13, 13}},
        {0xD0, {11, 13, 12, 13, 12, 12, 12, 12, 15, 15, 15, 14, 12, 12, 15, 14}},
        {0xE0, {11, 12, 11, 11, 13, 11, 14, 11, 13, 13, 13, 13, 15, 13, 12, 13}},
        {0xF0, {13, 12, 12, 14, 12, 12, 13, 13, 13, 14, 14, 14, 11, 11, 14, 11}},
    };
    for (const Row& r : rows)
        for (int i = 0; i < 16; i++) m.advance[r.base + i] = r.v[i];
    return m;
}

} // namespace

// ---------------------------------------------------------------------------
// Console sizes (FontSize, docs/3ds-ui-design.md "UI foundation and the screen gallery")
// ---------------------------------------------------------------------------

float fontSizeScale(FontSize size) {
    switch (size) {
    case FontSize::S: return 0.8f;
    case FontSize::T: return 2.0f;
    default: return 1.0f;
    }
}

int fontCapPx(FontSize size) {
    switch (size) {
    case FontSize::S: return 8;
    case FontSize::T: return 20;
    default: return 10;
    }
}

int fontLinePitchPx(FontSize size) {
    switch (size) {
    case FontSize::S: return 13;
    case FontSize::T: return 32;
    default: return 16;
    }
}

FontSize consoleSizeFor(float pxPerTexel) {
    if (pxPerTexel < 0.7f) return FontSize::S;
    if (pxPerTexel < 1.1f) return FontSize::L;
    return FontSize::T;
}

int glyphAdvancePx(const FontMetrics& m, FontSize size, unsigned char c) {
    const int a = m.advance[c];
    if (a <= 0) return 0;
    switch (size) {
    case FontSize::S: return std::max(1, static_cast<int>(std::lround(static_cast<double>(a) * 0.8)));
    case FontSize::T: return 2 * a;
    default: return a;
    }
}

int measureTextPx(const FontMetrics& m, std::string_view text, FontSize size, bool markup) {
    int w = 0;
    for (char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (markup && (c == '{' || c == '}')) continue;
        w += glyphAdvancePx(m, size, c);
    }
    return w;
}

FontMetrics smallFontMetrics(const FontMetrics& m) {
    FontMetrics s = m;
    s.glyphW = std::round(m.glyphW * 0.8f);
    s.glyphH = std::round(m.glyphH * 0.8f);
    for (int c = 0; c < 256; ++c) s.advance[c] = static_cast<u8>(glyphAdvancePx(m, FontSize::S, static_cast<unsigned char>(c)));
    return s;
}

Image smallFontAtlas(const Image& font, const FontMetrics& m) {
    const int size = static_cast<int>(m.atlasSize);
    Image out;
    out.width = out.height = size;
    out.hasAlpha = true;
    out.rgba.assign(static_cast<size_t>(size) * static_cast<size_t>(size) * 4, 0);
    const int gw = static_cast<int>(m.glyphW), gh = static_cast<int>(m.glyphH); // 30 x 15
    const int ow = static_cast<int>(std::round(m.glyphW * 0.8f)), oh = static_cast<int>(std::round(m.glyphH * 0.8f));
    const int cw = static_cast<int>(m.cellW), ch = static_cast<int>(m.cellH);
    if (font.width < m.columns * cw || font.height < m.rows * ch || ow <= 0 || oh <= 0 ||
        font.rgba.size() < static_cast<size_t>(font.width) * static_cast<size_t>(font.height) * 4)
        return out;
    // The source texel's glyph intensity (RGB carries it in font.tga; alpha scales it).
    auto src = [&](int x, int y) -> float {
        const uint8_t* p = &font.rgba[(static_cast<size_t>(y) * static_cast<size_t>(font.width) + static_cast<size_t>(x)) * 4];
        const int v = std::max({p[0], p[1], p[2]});
        return static_cast<float>(v) * static_cast<float>(p[3]) / 255.0f;
    };
    // Separable coverage weights: output texel o covers source [o * step, (o + 1) * step), each
    // source texel weighted by the part of it inside, normalised by the span.
    auto weights = [](int n, int outN) {
        const double step = static_cast<double>(n) / static_cast<double>(outN);
        std::vector<std::vector<std::pair<int, float>>> w(static_cast<size_t>(outN));
        for (int o = 0; o < outN; ++o) {
            const double a = o * step, b = (o + 1) * step;
            for (int i = static_cast<int>(std::floor(a)); i < n && i < b; ++i) {
                const double ov = std::min(b, i + 1.0) - std::max(a, static_cast<double>(i));
                if (ov > 1e-9) w[static_cast<size_t>(o)].push_back({i, static_cast<float>(ov / step)});
            }
        }
        return w;
    };
    const auto wx = weights(gw, ow), wy = weights(gh, oh);
    constexpr float kContrast = 1.25f; // a one-texel stroke covers 0.8 of an output texel
    const int cells = std::min(m.columns * m.rows, m.drawableLimit);
    for (int idx = 0; idx < cells; ++idx) {
        const int col = idx % m.columns, row = idx / m.columns;
        const int sx0 = col * cw, sy0 = row * ch + (ch - gh); // the glyph: left gw, lower gh texels
        const int dx0 = col * cw, dy0 = row * ch + (ch - oh);
        for (int oy = 0; oy < oh; ++oy)
            for (int ox = 0; ox < ow; ++ox) {
                float v = 0;
                for (const auto& py : wy[static_cast<size_t>(oy)])
                    for (const auto& px : wx[static_cast<size_t>(ox)]) v += px.second * py.second * src(sx0 + px.first, sy0 + py.first);
                const int q = std::clamp(static_cast<int>(std::lround(v * kContrast)), 0, 255);
                uint8_t* o =
                    &out.rgba[(static_cast<size_t>(dy0 + oy) * static_cast<size_t>(size) + static_cast<size_t>(dx0 + ox)) * 4];
                o[0] = o[1] = o[2] = o[3] = static_cast<uint8_t>(q);
            }
    }
    return out;
}

namespace {

// One glyph of a console run: the byte, its colour, its pen position (pixels from the run's left).
struct RunGlyph {
    unsigned char c;
    Color color;
    int pen;
};

// The texture and the glyph rectangle of a size: S from the S atlas (both kinds), L and T from
// font.tga / font_alpha.tga; k = screen pixels per atlas texel.
struct SizeSource {
    const Texture2D* tex = nullptr;
    const FontMetrics* m = nullptr;
    int k = 1;
};
SizeSource sizeSource(const Font& f, FontSize size, bool alphaFont) {
    SizeSource s;
    if (size == FontSize::S) {
        s.tex = f.smallTexture;
        s.m = f.smallMetrics;
    } else {
        s.tex = alphaFont ? f.alphaTexture : f.texture;
        s.m = f.metrics;
        s.k = size == FontSize::T ? 2 : 1;
    }
    if (s.tex && !s.tex->valid()) s.tex = nullptr;
    return s;
}

// The glyphs of a run at (leftPx, topPx) (whole pixels of the mapping), the effect pass first.
int drawConsoleRun(Renderer2D& r, const Font& f, FontSize size, bool alphaFont, int leftPx, int topPx,
                   const std::vector<RunGlyph>& glyphs, TextEffect effect, float alpha) {
    const SizeSource src = sizeSource(f, size, alphaFont);
    if (!src.m || !src.tex) return 0;
    const Mapping& mp = r.mapping();
    const float gwPx = src.m->glyphW * static_cast<float>(src.k), ghPx = src.m->glyphH * static_cast<float>(src.k);
    const float fbW = static_cast<float>(mp.fbWidth);
    auto put = [&](float px, float py, const GlyphUv& uv, const Texture2D* tex, Color c, Blend b) {
        return r.quad(mp.toVirtX(px), mp.toVirtY(py), gwPx / mp.scaleX, ghPx / mp.scaleY, uv.s0, uv.t0, uv.s1, uv.t1, tex, c, b);
    };
    int n = 0;
    // The effect: the alpha font (the S atlas's alpha, or font_alpha.tga) in black, under the glyphs.
    static const int kOutline[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
    static const int kShadow[1][2] = {{1, 1}};
    const int(*offs)[2] = nullptr;
    int nOffs = 0;
    if (effect == TextEffect::Outline) {
        offs = kOutline;
        nOffs = 8;
    } else if (effect == TextEffect::Shadow) {
        offs = kShadow;
        nOffs = 1;
    }
    if (nOffs) {
        const SizeSource back = sizeSource(f, size, true);
        if (back.tex && back.m) {
            const Color black{0, 0, 0, alpha};
            for (int o = 0; o < nOffs; ++o)
                for (const RunGlyph& g : glyphs) {
                    if (!glyphDrawable(*back.m, g.c)) continue;
                    const float px = static_cast<float>(leftPx + g.pen + offs[o][0]);
                    if (px + gwPx <= 0 || px >= fbW) continue;
                    if (put(px, static_cast<float>(topPx + offs[o][1]), glyphUv(*back.m, g.c), back.tex, black, Blend::Alpha)) n++;
                }
        }
    }
    const Blend blend = alphaFont ? Blend::Alpha : Blend::Add;
    for (const RunGlyph& g : glyphs) {
        const float px = static_cast<float>(leftPx + g.pen);
        if (px + gwPx <= 0 || px >= fbW) continue;
        if (glyphDrawable(*src.m, g.c)) {
            if (put(px, static_cast<float>(topPx), glyphUv(*src.m, g.c), src.tex, g.color, blend)) n++;
        } else if (g.c >= src.m->drawableLimit && !alphaFont) {
            // The missing font_rus.tga: a filled rectangle, as the scaled routine.
            if (r.rect(mp.toVirtX(px), mp.toVirtY(static_cast<float>(topPx)), gwPx / mp.scaleX, ghPx / mp.scaleY, g.color,
                       Blend::Add))
                n++;
        }
    }
    return n;
}

TextEffect effectiveEffect(const TextStyle& style, FontSize size) {
    if (style.effect != TextEffect::Auto) return style.effect;
    return size == FontSize::T && style.kind == FontKind::Additive ? TextEffect::Outline : TextEffect::None;
}

int drawTextConsole(Renderer2D& r, const Font& font, float x, float y, std::string_view text, const TextStyle& style) {
    if (!font.metrics) return 0;
    const FontMetrics& m = *font.metrics;
    const bool alphaFont = style.kind == FontKind::Alpha;
    const bool markup = style.markup && !alphaFont;
    const Mapping& mp = r.mapping();
    const float onScreen = style.scale * mp.scaleY; // pixels per texel of the scaled text
    FontSize size = style.size == FontSize::Auto ? consoleSizeFor(onScreen) : style.size;
    // The anchor and the glyph box's vertical centre of the scaled text, in pixels.
    const float ax = mp.toFbX(x);
    const float centreY = mp.toFbY(y) + m.glyphH * onScreen * 0.5f;
    auto leftFor = [&](FontSize s) {
        const float w = static_cast<float>(measureTextPx(m, text, s, markup));
        const float l = style.align == Align::Left ? ax : (style.align == Align::Center ? ax - w * 0.5f : ax - w);
        return static_cast<int>(std::floor(l + 0.5f));
    };
    int left = leftFor(size);
    if (style.size == FontSize::Auto) {
        while (size != FontSize::S) {
            const int w = measureTextPx(m, text, size, markup);
            if (left >= 0 && left + w <= mp.fbWidth) break;
            size = size == FontSize::T ? FontSize::L : FontSize::S;
            left = leftFor(size);
        }
    }
    const SizeSource src = sizeSource(font, size, alphaFont);
    if (!src.m || !src.tex) return 0;
    const float ghPx = src.m->glyphH * static_cast<float>(src.k);
    const int top = static_cast<int>(std::floor(centreY - ghPx * 0.5f + 0.5f));
    std::vector<RunGlyph> glyphs;
    glyphs.reserve(text.size());
    const Color base = style.color;
    Color col = base;
    int pen = 0;
    for (char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (markup && c == '{') {
            col = {1, 1, 1, base.a};
            continue;
        }
        if (markup && c == '}') {
            col = base;
            continue;
        }
        glyphs.push_back({c, col, pen});
        pen += glyphAdvancePx(m, size, c);
    }
    return drawConsoleRun(r, font, size, alphaFont, left, top, glyphs, effectiveEffect(style, size), base.a);
}

int drawNumberConsole(Renderer2D& r, const Font& font, float x, float y, std::string_view digits, float scale, Color c) {
    if (!font.metrics) return 0;
    const FontMetrics& m = *font.metrics;
    const Mapping& mp = r.mapping();
    const float onScreen = scale * mp.scaleY;
    const FontSize size = consoleSizeFor(onScreen);
    // The routine draws whole cells; the glyph is their lower glyphH rows.
    const float glyphTop = mp.toFbY(y) + (m.cellH - m.glyphH) * onScreen;
    const float centreY = glyphTop + m.glyphH * onScreen * 0.5f;
    const SizeSource src = sizeSource(font, size, false);
    if (!src.m || !src.tex) return 0;
    const float ghPx = src.m->glyphH * static_cast<float>(src.k);
    const int top = static_cast<int>(std::floor(centreY - ghPx * 0.5f + 0.5f));
    const int left = static_cast<int>(std::floor(mp.toFbX(x) + 0.5f));
    const int advance = numberAdvancePx(size);
    std::vector<RunGlyph> glyphs;
    int pen = 0;
    for (char ch : digits) {
        glyphs.push_back({static_cast<unsigned char>(ch), c, pen});
        pen += advance;
    }
    TextStyle st;
    return drawConsoleRun(r, font, size, false, left, top, glyphs, effectiveEffect(st, size), c.a);
}

} // namespace

int numberAdvancePx(FontSize size) { return size == FontSize::S ? 11 : (size == FontSize::T ? 28 : 14); }

const FontMetrics& FontMetrics::original() {
    static const FontMetrics m = makeOriginal();
    return m;
}

GlyphUv glyphUv(const FontMetrics& m, unsigned char c) {
    int idx = c >= m.drawableLimit ? c - m.drawableLimit : c;
    int col = idx % m.columns, row = idx / m.columns;
    // Original (v up): t0 = 15/16 - row/16 is the bottom edge of the cell and the glyph spans
    // the lower glyphH rows. With v = 0 at the top, that is the cell's bottom-aligned rows.
    float cellTop = static_cast<float>(row) * m.cellH;
    float top = cellTop + (m.cellH - m.glyphH);
    GlyphUv uv;
    uv.s0 = static_cast<float>(col) * m.cellW / m.atlasSize;
    uv.s1 = uv.s0 + m.glyphW / m.atlasSize;
    uv.t0 = top / m.atlasSize;
    uv.t1 = (top + m.glyphH) / m.atlasSize;
    return uv;
}

bool glyphDrawable(const FontMetrics& m, unsigned char c) {
    return c < m.drawableLimit && c != ' ' && m.advance[c] > 0;
}

float measureText(const FontMetrics& m, std::string_view text, float scale, bool markup) {
    float w = 0;
    for (char ch : text) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (markup && (c == '{' || c == '}')) continue;
        w += static_cast<float>(m.advance[c]) * scale;
    }
    return w;
}

int drawText(Renderer2D& r, const Font& font, float x, float y, std::string_view text, const TextStyle& style) {
    if (font.consoleSizes) return drawTextConsole(r, font, x, y, text, style);
    const bool alphaFont = style.kind == FontKind::Alpha;
    const Texture2D* tex = alphaFont ? font.alphaTexture : font.texture;
    if (!font.metrics || !tex || !tex->valid()) return 0;
    const FontMetrics& m = *font.metrics;
    const float s = style.scale;
    const bool markup = style.markup && !alphaFont;
    if (style.align != Align::Left) {
        float w = measureText(m, text, s, markup);
        x -= style.align == Align::Center ? w * 0.5f : w;
    }
    const Blend blend = alphaFont ? Blend::Alpha : Blend::Add;
    Color base = style.color;
    Color col = base;
    int n = 0;
    for (char ch : text) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (markup && c == '{') { col = {1, 1, 1, base.a}; continue; }
        if (markup && c == '}') { col = base; continue; }
        if (glyphDrawable(m, c)) {
            GlyphUv uv = glyphUv(m, c);
            if (r.quad(x, y, m.glyphW * s, m.glyphH * s, uv.s0, uv.t0, uv.s1, uv.t1, tex, col, blend))
                n++;
        } else if (c >= m.drawableLimit && !alphaFont) {
            // The original registers the missing font_rus.tga as handle 0, which draws an
            // untextured filled rectangle (frontend.md 4.9 point 10).
            if (r.rect(x, y, m.glyphW * s, m.glyphH * s, col, Blend::Add)) n++;
        }
        x += static_cast<float>(m.advance[c]) * s;
    }
    return n;
}

int drawTextShadowed(Renderer2D& r, const Font& font, float x, float y, std::string_view text,
                     const TextStyle& style, float shadowAlpha) {
    TextStyle sh = style;
    sh.kind = FontKind::Alpha;
    sh.markup = false;
    sh.color = {0, 0, 0, shadowAlpha};
    std::string plain;
    std::string_view shadowText = text;
    if (style.markup) {
        // Braces are not drawn by the main text; keep the shadow aligned with it.
        for (char ch : text)
            if (ch != '{' && ch != '}') plain += ch;
        shadowText = plain;
    }
    int n = drawText(r, font, x + 2, y + 2, shadowText, sh);
    return n + drawText(r, font, x, y, text, style);
}

int drawNumber(Renderer2D& r, const Font& font, float x, float y, std::string_view digits, float scale, Color c) {
    if (font.consoleSizes) return drawNumberConsole(r, font, x, y, digits, scale, c);
    if (!font.metrics || !font.texture || !font.texture->valid()) return 0;
    const FontMetrics& m = *font.metrics;
    int n = 0;
    for (char ch : digits) {
        unsigned char code = static_cast<unsigned char>(ch);
        if (code < m.drawableLimit && code != ' ') {
            int col = code % m.columns, row = code / m.columns;
            float s0 = static_cast<float>(col) * m.cellW / m.atlasSize;
            float t0 = static_cast<float>(row) * m.cellH / m.atlasSize;
            if (r.quad(x, y, m.cellW * scale, m.cellH * scale, s0, t0, s0 + m.cellW / m.atlasSize,
                       t0 + m.cellH / m.atlasSize, font.texture, c, Blend::Add))
                n++;
        }
        x = std::trunc(x + 14.0f * scale);
    }
    return n;
}

float numberWidth(std::string_view digits, float scale) {
    float x = 0;
    for (size_t i = 0; i < digits.size(); i++) x = std::trunc(x + 14.0f * scale);
    return x;
}

} // namespace as3d::ui

namespace as3d::ui {

const char* hintKeyName(HintKey k) {
    static const char* const kNames[] = {"A", "B", "X", "Y", "L", "R", "ZL", "ZR", "START", "SELECT", "+", "PAD", "TOUCH"};
    const int i = static_cast<int>(k);
    return i >= 0 && i < static_cast<int>(HintKey::Count) ? kNames[i] : "?";
}

int drawKeyHint(Renderer2D& r, const Font& font, const KeyGlyphAtlas* atlas, int x, int y, HintKey key,
                std::string_view label, const KeyHintStyle& style) {
    if (!font.metrics) return 0;
    const Mapping& mp = r.mapping();
    const FontSize size = style.size == FontSize::Auto ? FontSize::S : style.size;
    const float k = fontSizeScale(size);
    const int pitch = fontLinePitchPx(size), cap = fontCapPx(size);
    TextStyle st;
    st.scale = k / mp.scaleY; // the glyph box at the size, one atlas texel per pixel
    st.size = size;
    st.effect = TextEffect::None;
    int w = 0;
    const KeyGlyphAtlas::Rect* pic = atlas && atlas->texture && atlas->texture->valid() ? &atlas->rect(key) : nullptr;
    if (pic && !pic->empty()) {
        const float tw = static_cast<float>(atlas->texture->width()), th = static_cast<float>(atlas->texture->height());
        const int top = y + (pitch - pic->h) / 2;
        r.quad(mp.toVirtX(static_cast<float>(x)), mp.toVirtY(static_cast<float>(top)), static_cast<float>(pic->w) / mp.scaleX,
               static_cast<float>(pic->h) / mp.scaleY, static_cast<float>(pic->x) / tw, static_cast<float>(pic->y) / th,
               static_cast<float>(pic->x + pic->w) / tw, static_cast<float>(pic->y + pic->h) / th, atlas->texture, Color{},
               Blend::Alpha);
        w = pic->w;
    } else {
        // The key's name in a one-pixel frame around its cap height.
        const char* name = hintKeyName(key);
        const int tw = measureTextPx(*font.metrics, name, size);
        const int capTop = y + static_cast<int>(std::lround(2.0f * k));
        const float bx = static_cast<float>(x), by = static_cast<float>(capTop - 2);
        const float bw = static_cast<float>(tw + 4), bh = static_cast<float>(cap + 4);
        r.outline(mp.toVirtX(bx), mp.toVirtY(by), bw / mp.scaleX, bh / mp.scaleY, style.keyColor);
        st.color = style.keyColor;
        drawText(r, font, mp.toVirtX(static_cast<float>(x + 2)), mp.toVirtY(static_cast<float>(y)), name, st);
        w = tw + 4;
    }
    if (!label.empty()) {
        st.color = style.labelColor;
        const int lx = x + w + style.gap;
        drawText(r, font, mp.toVirtX(static_cast<float>(lx)), mp.toVirtY(static_cast<float>(y)), label, st);
        w += style.gap + measureTextPx(*font.metrics, label, size);
    }
    return w;
}

} // namespace as3d::ui
