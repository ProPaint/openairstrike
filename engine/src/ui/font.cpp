// Font metrics, text measurement and drawing (render-pipeline.md 8.3).
#include <algorithm>

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
    if (!font.metrics || !font.texture || !font.texture->valid()) return 0;
    const FontMetrics& m = *font.metrics;
    const float s = style.scale;
    if (style.align != Align::Left) {
        float w = measureText(m, text, s, style.markup);
        x -= style.align == Align::Center ? w * 0.5f : w;
    }
    Color base = style.color;
    Color col = base;
    int n = 0;
    for (char ch : text) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (style.markup && c == '{') { col = {1, 1, 1, base.a}; continue; }
        if (style.markup && c == '}') { col = base; continue; }
        if (glyphDrawable(m, c)) {
            GlyphUv uv = glyphUv(m, c);
            if (r.quad(x, y, m.glyphW * s, m.glyphH * s, uv.s0, uv.t0, uv.s1, uv.t1, font.texture, col, Blend::Add))
                n++;
        }
        x += static_cast<float>(m.advance[c]) * s;
    }
    return n;
}

std::vector<std::string> wrapText(const FontMetrics& m, std::string_view text, float maxWidth, float scale) {
    std::vector<std::string> lines;
    std::string cur;
    size_t i = 0;
    while (i < text.size()) {
        size_t j = text.find(' ', i);
        if (j == std::string_view::npos) j = text.size();
        std::string word(text.substr(i, j - i));
        // A word wider than a whole line is broken between characters.
        while (measureText(m, word, scale) > maxWidth && word.size() > 1) {
            size_t k = 1;
            while (k < word.size() && measureText(m, word.substr(0, k + 1), scale) <= maxWidth) k++;
            if (!cur.empty()) { lines.push_back(cur); cur.clear(); }
            lines.push_back(word.substr(0, k));
            word.erase(0, k);
        }
        std::string trial = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && measureText(m, trial, scale) > maxWidth) {
            lines.push_back(cur);
            cur = word;
        } else {
            cur = trial;
        }
        i = j + 1;
    }
    lines.push_back(cur);
    return lines;
}

} // namespace as3d::ui
