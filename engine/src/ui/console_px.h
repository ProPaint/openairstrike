// Drawing in console pixels through any Renderer2D mapping (the console menu view and the
// console HUD, docs/3ds-ui-design.md R4.5 and R4.7): rectangles, frames, text at the console sizes,
// half-size pictures. Internal to engine/src/ui.
#pragma once

#include <algorithm>
#include <cmath>
#include <string_view>

#include "as3d/ui.h"

namespace as3d::ui::consolepx {

inline int capOffset(FontSize s) { return static_cast<int>(std::lround(2.0f * fontSizeScale(s))); }
// The top of the glyph boxes that centres the cap height in a band (y, h).
inline int textTopIn(float y, float h, FontSize s) {
    return static_cast<int>(std::floor(y + (h - static_cast<float>(fontCapPx(s))) * 0.5f)) - capOffset(s);
}

struct Px {
    Renderer2D& r;
    const UiAssets& a;
    Font font;
    const Mapping& m;
    Px(Renderer2D& rr, const UiAssets& aa) : r(rr), a(aa), font(aa.uiFont()), m(rr.mapping()) {}
    float vx(float x) const { return m.toVirtX(x); }
    float vy(float y) const { return m.toVirtY(y); }
    float vw(float w) const { return w / m.scaleX; }
    float vh(float h) const { return h / m.scaleY; }
    void rect(const RectF& p, Color c, Blend b = Blend::Alpha) const { r.rect(vx(p.x), vy(p.y), vw(p.w), vh(p.h), c, b); }
    void rect(float x, float y, float w, float h, Color c, Blend b = Blend::Alpha) const { rect(RectF{x, y, w, h}, c, b); }
    void frame(const RectF& p, Color c, int t = 1) const {
        const float ft = static_cast<float>(t);
        rect(p.x, p.y, p.w, ft, c);
        rect(p.x, p.y + p.h - ft, p.w, ft, c);
        rect(p.x, p.y + ft, ft, p.h - 2 * ft, c);
        rect(p.x + p.w - ft, p.y + ft, ft, p.h - 2 * ft, c);
    }
    // Text with its glyph boxes' top at y.
    int text(float x, float y, std::string_view s, FontSize size, Color c, Align al = Align::Left, bool markup = false,
             TextEffect fx = TextEffect::None) const {
        if (!font.metrics || s.empty()) return 0;
        TextStyle st;
        st.scale = fontSizeScale(size) / m.scaleY;
        st.size = size;
        st.color = c;
        st.align = al;
        st.markup = markup;
        st.effect = fx;
        return drawText(r, font, vx(std::floor(x)), vy(static_cast<float>(y)), s, st);
    }
    // Text centred (vertically on its cap height) in a band.
    void textIn(const RectF& b, std::string_view s, FontSize size, Color c, Align al = Align::Center, float pad = 8) const {
        const float x = al == Align::Center ? b.x + b.w * 0.5f : al == Align::Left ? b.x + pad : b.x + b.w - pad;
        text(x, static_cast<float>(textTopIn(b.y, b.h, size)), s, size, c, al);
    }
    // A sub-rectangle (spec UVs: t = 1 the top row) of a half-size picture, centred on (cx, cy)
    // and cropped to maxW x maxH pixels around its centre. Returns its drawn width (0: no picture).
    int halfPicture(std::string_view path, SpecUv uv, float cx, float cy, float maxW, float maxH, Color c, Blend b) const {
        const HalfPicture p = a.textureHalf(path);
        if (!p.valid()) return 0;
        float w = std::floor((uv.s1 - uv.s0) * static_cast<float>(p.width));
        float h = std::floor((uv.t1 - uv.t0) * static_cast<float>(p.height));
        float s0 = uv.s0, s1 = uv.s1, t0 = uv.t0, t1 = uv.t1; // spec: t1 the top
        if (w > maxW) {
            const float cut = (w - maxW) * 0.5f / static_cast<float>(p.width);
            s0 += cut;
            s1 -= cut;
            w = maxW;
        }
        if (h > maxH) {
            const float cut = (h - maxH) * 0.5f / static_cast<float>(p.height);
            t0 += cut;
            t1 -= cut;
            h = maxH;
        }
        const float x = std::floor(cx - w * 0.5f), y = std::floor(cy - h * 0.5f);
        r.quad(vx(x), vy(y), vw(w), vh(h), s0 * p.s1, (1.0f - t1) * p.t1, s1 * p.s1, (1.0f - t0) * p.t1, p.texture, c, b);
        return static_cast<int>(w);
    }
    // A filled triangle pointing left or right inside the box (one-pixel columns).
    void triangle(const RectF& box, bool right, Color c) const {
        const int h = 14, w = 8;
        const float x0 = std::floor(box.x + (box.w - static_cast<float>(w)) * 0.5f);
        const float yc = std::floor(box.y + box.h * 0.5f);
        for (int i = 0; i < w; ++i) {
            const int col = right ? i : w - 1 - i;
            const float half = static_cast<float>(h) * 0.5f * (1.0f - static_cast<float>(i) / static_cast<float>(w));
            rect(x0 + static_cast<float>(col), yc - std::floor(half), 1, std::max(1.0f, 2 * std::floor(half)), c);
        }
    }
    void triangleUpDown(const RectF& box, bool down, Color c) const {
        const int w = 14, h = 8;
        const float y0 = std::floor(box.y + (box.h - static_cast<float>(h)) * 0.5f);
        const float xc = std::floor(box.x + box.w * 0.5f);
        for (int i = 0; i < h; ++i) {
            const int row = down ? i : h - 1 - i;
            const float half = static_cast<float>(w) * 0.5f * (1.0f - static_cast<float>(i) / static_cast<float>(h));
            rect(xc - std::floor(half), y0 + static_cast<float>(row), std::max(1.0f, 2 * std::floor(half)), 1, c);
        }
    }
};

} // namespace as3d::ui::consolepx
