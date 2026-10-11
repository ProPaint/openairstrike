// Screen mapping and the 2D quad batcher. See as3d/ui.h.
#include <algorithm>
#include <cmath>

#include "as3d/batch2d.h"
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

Mapping pixelMapping(int fbWidth, int fbHeight) {
    Mapping m;
    m.fbWidth = std::max(fbWidth, 1);
    m.fbHeight = std::max(fbHeight, 1);
    return m; // scale 1, offset 0
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

void Renderer2D::begin(int fbWidth, int fbHeight, int originX, int originY) {
    mapping_ = computeMapping(fbWidth, fbHeight);
    originX_ = originX;
    originY_ = originY;
    quads_.clear();
    breaks_.clear();
    dropped_ = 0;
}

void Renderer2D::beginPixels(int fbWidth, int fbHeight, int originX, int originY) {
    begin(fbWidth, fbHeight, originX, originY);
    mapping_ = pixelMapping(fbWidth, fbHeight);
}

void Renderer2D::beginMapped(int fbWidth, int fbHeight, const Mapping& m) {
    begin(fbWidth, fbHeight);
    mapping_ = m;
    mapping_.fbWidth = fbWidth;
    mapping_.fbHeight = fbHeight;
}

void Renderer2D::appendMapped(const Renderer2D& src) {
    const Mapping& a = src.mapping_;
    const Mapping& b = mapping_;
    const float kx = a.scaleX / b.scaleX, ky = a.scaleY / b.scaleY;
    for (const Quad& q0 : src.quads_) {
        Quad q = q0;
        q.x = b.toVirtX(a.toFbX(q0.x));
        q.y = b.toVirtY(a.toFbY(q0.y));
        q.w = q0.w * kx;
        q.h = q0.h * ky;
        add(q);
    }
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
    quadCornersFb(q, mapping_, halfPixelShift_, px, py);
}

} // namespace as3d::ui
