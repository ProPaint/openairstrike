// Screen mapping and the 2D quad batcher. See as3d/ui.h.
#include <algorithm>
#include <cmath>

#include "as3d/ui.h"

namespace as3d::ui {

Color pulse(float f, float phi, float mt) {
    const float v = 0.5f + 0.5f * std::sin(f * 3.14159265f * mt - phi);
    return {v, v, v, 1.0f};
}

Mapping computeMapping(int fbWidth, int fbHeight) {
    Mapping m;
    m.fbWidth = std::max(fbWidth, 1);
    m.fbHeight = std::max(fbHeight, 1);
    const float w = static_cast<float>(m.fbWidth), h = static_cast<float>(m.fbHeight);
    const float aspect = w / h;
    if (aspect >= 4.0f / 3.0f - 1e-4f) {
        m.scaleX = m.scaleY = h / kVirtualHeight;
        m.offsetX = (w - kVirtualWidth * m.scaleX) * 0.5f;
    } else if (aspect >= 1.25f) {
        m.scaleX = w / kVirtualWidth;
        m.scaleY = h / kVirtualHeight;
    } else {
        m.scaleX = m.scaleY = w / kVirtualWidth;
        m.offsetY = (h - kVirtualHeight * m.scaleY) * 0.5f;
    }
    return m;
}

void Mapping::fieldPixels(int& x, int& y, int& w, int& h) const {
    const int x0 = std::max(0, static_cast<int>(std::lround(toFbX(0))));
    const int y0 = std::max(0, static_cast<int>(std::lround(toFbY(0))));
    const int x1 = std::min(fbWidth, static_cast<int>(std::lround(toFbX(kVirtualWidth))));
    const int y1 = std::min(fbHeight, static_cast<int>(std::lround(toFbY(kVirtualHeight))));
    x = x0;
    y = y0;
    w = std::max(x1 - x0, 0);
    h = std::max(y1 - y0, 0);
}

void Renderer2D::begin(int fbWidth, int fbHeight) {
    mapping_ = computeMapping(fbWidth, fbHeight);
    quads_.clear();
    dropped_ = 0;
}

bool Renderer2D::add(const Quad& q) {
    if (static_cast<int>(quads_.size()) >= kMaxQuads) {
        dropped_++;
        return false;
    }
    quads_.push_back(q);
    return true;
}

bool Renderer2D::quad(float x, float y, float w, float h, float s0, float t0, float s1, float t1,
                      const Texture2D* tex, Color c, Blend blend) {
    Quad q;
    q.x = x; q.y = y; q.w = w; q.h = h;
    q.s0 = s0; q.t0 = t0; q.s1 = s1; q.t1 = t1;
    q.texture = tex;
    q.color = c;
    q.blend = blend;
    return add(q);
}

bool Renderer2D::quadSpec(float x, float y, float w, float h, float s0, float t0, float s1, float t1,
                          const Texture2D* tex, Color c, Blend blend) {
    // Spec convention: top-left samples (s0, t1) with t = 1 at the top of the image; ours has
    // v = 0 at the top, so v = 1 - t.
    return quad(x, y, w, h, s0, 1.0f - t1, s1, 1.0f - t0, tex, c, blend);
}

bool Renderer2D::pic(float x, float y, const Texture2D& tex, Color c, Blend blend) {
    return quad(x, y, static_cast<float>(tex.width()), static_cast<float>(tex.height()), 0, 0, 1, 1, &tex, c, blend);
}

bool Renderer2D::rect(float x, float y, float w, float h, Color c, Blend blend) {
    return quad(x, y, w, h, 0, 0, 1, 1, nullptr, c, blend);
}

bool Renderer2D::outline(float x, float y, float w, float h, Color c, Blend blend) {
    // One framebuffer pixel thick, in virtual units.
    const float tx = 1.0f / mapping_.scaleX, ty = 1.0f / mapping_.scaleY;
    bool ok = rect(x, y, w, ty, c, blend);
    ok &= rect(x, y + h - ty, w, ty, c, blend);
    ok &= rect(x, y + ty, tx, h - 2 * ty, c, blend);
    ok &= rect(x + w - tx, y + ty, tx, h - 2 * ty, c, blend);
    return ok;
}

bool Renderer2D::line(float x0, float y0, float x1, float y1, Color c, Blend blend) {
    Quad q;
    q.x = x0; q.y = y0; q.w = x1 - x0; q.h = y1 - y0;
    q.color = c;
    q.blend = blend;
    q.line = true;
    return add(q);
}

bool Renderer2D::circle(float cx, float cy, float radius, Color c, Blend blend, float feather) {
    return ring(cx, cy, radius, radius, c, blend, feather);
}

bool Renderer2D::ring(float cx, float cy, float radius, float thickness, Color c, Blend blend, float feather) {
    if (!(radius > 0)) return false;
    // Round on screen: the horizontal radius in virtual pixels follows the mapping's aspect.
    const float rx = radius * mapping_.scaleY / mapping_.scaleX;
    Quad q;
    q.x = cx - rx;
    q.y = cy - radius;
    q.w = 2 * rx;
    q.h = 2 * radius;
    q.color = c;
    q.blend = blend;
    q.shape = QuadShape::Ellipse;
    q.inner = thickness >= radius ? 0.0f : std::max(0.0f, (radius - thickness) / radius);
    q.feather = std::max(feather, 0.0f) / radius;
    return add(q);
}

bool Renderer2D::fullscreen(Color c, Blend blend) {
    return rect(mapping_.left(), mapping_.top(), mapping_.right() - mapping_.left(),
                mapping_.bottom() - mapping_.top(), c, blend);
}

void Renderer2D::quadCorners(const Quad& q, float px[4], float py[4]) const {
    const Mapping& m = mapping_;
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
    if (halfPixelShift_) {
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

} // namespace as3d::ui
