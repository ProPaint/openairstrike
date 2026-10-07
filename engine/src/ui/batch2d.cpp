// Neutral vertex generation of the 2D layer. See as3d/batch2d.h.
#include "as3d/batch2d.h"

#include <algorithm>
#include <cmath>

namespace as3d::ui {

namespace {

uint8_t to8(float v) {
    return static_cast<uint8_t>(std::lround(std::min(std::max(v, 0.0f), 1.0f) * 255.0f));
}

// Colour scaled by the coverage: alpha, and rgb too for additive quads (as the shader does).
void setCoverageColor(Vertex2D& v, const Color& c, float cov, bool scaleRgb) {
    const float k = scaleRgb ? cov : 1.0f;
    v.color[0] = c.r * k;
    v.color[1] = c.g * k;
    v.color[2] = c.b * k;
    v.color[3] = c.a * (scaleRgb ? 1.0f : cov);
    for (int i = 0; i < 4; i++) v.rgba[i] = to8(v.color[i]);
}

float smooth01(float e0, float e1, float x) {
    const float t = std::min(std::max((x - e0) / (e1 - e0), 0.0f), 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

CombineMode combineOf(const Quad& q) {
    if (!q.texture2 || q.combine2 == 0) return CombineMode::None;
    return (q.combine2 >= 1 && q.combine2 <= 3) ? static_cast<CombineMode>(q.combine2) : CombineMode::Replace;
}

void tessellate(const Quad& q, const float px[4], const float py[4], const Color& c, const Batch2DOptions& o,
                std::vector<Vertex2D>& out) {
    const float cx = (px[0] + px[2]) * 0.5f, cy = (py[0] + py[2]) * 0.5f;
    const float ax = (px[3] - px[0]) * 0.5f, ay = (py[3] - py[0]) * 0.5f; // half x axis
    const float bx = (px[1] - px[0]) * 0.5f, by = (py[1] - py[0]) * 0.5f; // half y axis
    const float rpx = std::sqrt(std::max(std::sqrt(ax * ax + ay * ay) * std::sqrt(bx * bx + by * by), 1e-6f));
    const float aa = 1.27f / rpx + q.feather;
    const float inner = q.inner;
    const bool add = q.blend == Blend::Add;

    std::vector<float> knots = {1.0f, std::max(0.0f, 1.0f - aa)};
    if (q.inner <= 0.0f) knots.push_back(0.0f); // solid centre: a fan
    if (inner > 0.0f) {
        knots.push_back(inner);
        knots.push_back(std::max(0.0f, inner - aa));
    }
    std::sort(knots.begin(), knots.end());
    knots.erase(std::unique(knots.begin(), knots.end(), [](float a, float b) { return b - a < 1e-4f; }), knots.end());

    const int n = std::max(o.circleSegments, 8);
    std::vector<float> cs(n + 1), sn(n + 1);
    for (int k = 0; k <= n; k++) {
        const float a = 6.28318531f * static_cast<float>(k % n) / static_cast<float>(n);
        cs[k] = std::cos(a);
        sn[k] = std::sin(a);
    }
    auto cov = [&](float r) {
        float v = 1.0f - smooth01(1.0f - aa, 1.0f, r);
        if (inner > 0.0f) v *= smooth01(inner - aa, inner, r);
        return v;
    };
    auto make = [&](float r, int k) {
        Vertex2D v;
        const float lx = r * cs[k], ly = r * sn[k];
        v.lx = lx;
        v.ly = ly;
        v.x = cx + lx * ax + ly * bx;
        v.y = cy + lx * ay + ly * by;
        const float fs = (lx + 1) * 0.5f, ft = (ly + 1) * 0.5f;
        v.u = q.s0 + (q.s1 - q.s0) * fs;
        v.v = q.t0 + (q.t1 - q.t0) * ft;
        v.u2 = q.s0b + (q.s1b - q.s0b) * fs;
        v.v2 = q.t0b + (q.t1b - q.t0b) * ft;
        v.inner = q.inner;
        v.feather = q.feather;
        setCoverageColor(v, c, cov(r), add);
        return v;
    };
    size_t i0 = 0;
    if (knots[0] <= 0.0f && knots.size() > 1) { // fan from the centre to the first ring
        for (int k = 0; k < n; k++) {
            out.push_back(make(0.0f, 0));
            out.push_back(make(knots[1], k));
            out.push_back(make(knots[1], k + 1));
        }
        i0 = 1;
    }
    for (size_t i = i0; i + 1 < knots.size(); i++) {
        for (int k = 0; k < n; k++) {
            const Vertex2D a = make(knots[i], k), b = make(knots[i], k + 1);
            const Vertex2D d = make(knots[i + 1], k), e = make(knots[i + 1], k + 1);
            out.push_back(a); out.push_back(d); out.push_back(e);
            out.push_back(a); out.push_back(e); out.push_back(b);
        }
    }
}

} // namespace

Color effectiveColor(const Quad& q) {
    Color c = q.color;
    if (q.blend == Blend::Add) {
        c.r *= c.a; c.g *= c.a; c.b *= c.a; c.a = 1.0f;
    } else if (q.blend == Blend::Opaque) {
        c.a = 1.0f;
    } else if (q.blend == Blend::Filter) {
        c.r = 1.0f + (c.r - 1.0f) * c.a;
        c.g = 1.0f + (c.g - 1.0f) * c.a;
        c.b = 1.0f + (c.b - 1.0f) * c.a;
        c.a = 1.0f;
    }
    return c;
}

void quadCornersFb(const Quad& q, const Mapping& m, bool halfPixelShift, float px[4], float py[4]) {
    // Virtual corners: top-left, bottom-left, bottom-right, top-right (a line: its two ends
    // in slots 0 and 2).
    float vx[4] = {q.x, q.x, q.x + q.w, q.x + q.w};
    float vy[4] = {q.y, q.y + q.h, q.y + q.h, q.y};
    if (!q.line && q.rotation != 0.0f) {
        const float a = q.rotation * 3.14159265f / 180.0f;
        const float ca = std::cos(a), sa = std::sin(a);
        const float cx = q.x + q.w * 0.5f, cy = q.y + q.h * 0.5f;
        for (int i = 0; i < 4; i++) {
            const float dx = vx[i] - cx, dy = vy[i] - cy;
            vx[i] = cx + dx * ca - dy * sa;
            vy[i] = cy + dx * sa + dy * ca;
        }
    }
    if (halfPixelShift) {
        for (int i = 0; i < 4; i++) {
            vx[i] -= 0.5f;
            vy[i] -= 0.5f;
        }
    }
    if (q.line) {
        float x0 = m.toFbX(vx[0]), y0 = m.toFbY(vy[0]), x1 = m.toFbX(vx[2]), y1 = m.toFbY(vy[2]);
        float dx = x1 - x0, dy = y1 - y0;
        float len = std::sqrt(dx * dx + dy * dy);
        float nx = 0, ny = 0;
        if (len > 0) { nx = -dy / len * 0.5f; ny = dx / len * 0.5f; }
        px[0] = x0 + nx; py[0] = y0 + ny;
        px[1] = x0 - nx; py[1] = y0 - ny;
        px[2] = x1 - nx; py[2] = y1 - ny;
        px[3] = x1 + nx; py[3] = y1 + ny;
        return;
    }
    for (int i = 0; i < 4; i++) {
        px[i] = m.toFbX(vx[i]);
        py[i] = m.toFbY(vy[i]);
    }
}

void buildBatch2D(const std::vector<Quad>& quads, const Mapping& m, std::vector<Vertex2D>& verts,
                  std::vector<Run2D>& runs, const Batch2DOptions& opts) {
    verts.clear();
    runs.clear();
    verts.reserve(quads.size() * 6);
    static const float kLx[4] = {-1, -1, 1, 1}, kLy[4] = {-1, 1, 1, -1};
    static const int order[6] = {0, 1, 2, 0, 2, 3};
    for (const Quad& q : quads) {
        const Color c = effectiveColor(q);
        float px[4], py[4], ps[4], pt[4];
        quadCornersFb(q, m, opts.halfPixelShift, px, py);
        const bool shaped = q.shape == QuadShape::Ellipse;
        const bool tess = shaped && opts.tessellateShapes;

        Run2D want;
        want.blend = q.blend;
        want.tex = q.texture;
        want.combine2 = combineOf(q);
        want.tex2 = want.combine2 == CombineMode::None ? nullptr : q.texture2;
        want.shape = shaped && !tess;
        if (runs.empty() || runs.back().blend != want.blend || runs.back().tex != want.tex ||
            runs.back().tex2 != want.tex2 || runs.back().combine2 != want.combine2 ||
            runs.back().shape != want.shape) {
            want.first = static_cast<int>(verts.size());
            runs.push_back(want);
        }
        if (tess) {
            tessellate(q, px, py, c, opts, verts);
        } else {
            if (q.line) {
                for (int i = 0; i < 4; i++) ps[i] = pt[i] = 0.5f;
            } else {
                ps[0] = q.s0; pt[0] = q.t0;
                ps[1] = q.s0; pt[1] = q.t1;
                ps[2] = q.s1; pt[2] = q.t1;
                ps[3] = q.s1; pt[3] = q.t0;
            }
            const float ps2[4] = {q.s0b, q.s0b, q.s1b, q.s1b}, pt2[4] = {q.t0b, q.t1b, q.t1b, q.t0b};
            for (int k : order) {
                Vertex2D v;
                v.x = px[k]; v.y = py[k];
                v.u = ps[k]; v.v = pt[k];
                v.u2 = ps2[k]; v.v2 = pt2[k];
                v.lx = kLx[k]; v.ly = kLy[k];
                v.inner = q.inner; v.feather = q.feather;
                v.color[0] = c.r; v.color[1] = c.g; v.color[2] = c.b; v.color[3] = c.a;
                for (int i = 0; i < 4; i++) v.rgba[i] = to8(v.color[i]);
                verts.push_back(v);
            }
        }
        runs.back().count = static_cast<int>(verts.size()) - runs.back().first;
    }
}

} // namespace as3d::ui
