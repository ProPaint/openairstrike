// CPU silhouette rasteriser and the shared shadow geometry (as3d/shadow_raster.h,
// docs/spec/render-pipeline.md 5.1 and 5.2). Mirrors ShadowRenderer::generate()
// (render/gles/shadow_renderer.cpp) step by step; keep the two in step.
#include "as3d/shadow_raster.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "as3d/math.h"

namespace as3d {

Vec3 shadowLightDirection(ShadowKind kind, const Vec3& towardsSun) {
    if (kind == ShadowKind::Planar) return {0.0f, 0.0f, 1.0f}; // straight down
    Vec3 L = normalize(towardsSun);
    if (L.z < 0.05f) L.z = 0.05f; // every shipped level has L.z > 0; keep the projection finite
    return L;
}

Vec3 projectShadowVertex(const Vec3& v, float cosR, float sinR, const Vec3& L) {
    float x = v.x * cosR - v.y * sinR;
    float y = v.x * sinR + v.y * cosR;
    return {x - v.z * L.x / L.z, y - v.z * L.y / L.z, v.z};
}

ShadowBounds computeShadowBounds(const ModelData& model, ShadowKind kind, const Vec3& towardsSun, int rotationSteps) {
    Vec3 L = shadowLightDirection(kind, towardsSun);
    float a = kind == ShadowKind::Planar ? 0.0f : degToRad(static_cast<float>(rotationSteps) * kShadowRotationStepDegrees);
    float c = std::cos(a), s = std::sin(a);
    float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    for (const Vec3& v : model.positions) {
        Vec3 p = projectShadowVertex(v, c, s, L);
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
    }
    ShadowBounds b;
    if (model.positions.empty()) return b;
    // A hair of tolerance so that float noise (cos of a quarter turn) does not widen the box.
    const float eps = 1e-3f;
    b.xmin = static_cast<int>(std::floor(minX + eps));
    b.ymin = static_cast<int>(std::floor(minY + eps));
    b.xmax = static_cast<int>(std::ceil(maxX - eps));
    b.ymax = static_cast<int>(std::ceil(maxY - eps));
    if (b.xmax <= b.xmin) b.xmax = b.xmin + 1;
    if (b.ymax <= b.ymin) b.ymax = b.ymin + 1;
    return b;
}

void shadowTextureSize(const ShadowBounds& bounds, ShadowQuality quality, int& width, int& height) {
    shadowTextureSize(bounds, quality, width, height, renderRules(GameId::AirStrike3D));
}

void shadowTextureSize(const ShadowBounds& bounds, ShadowQuality quality, int& width, int& height,
                       const RenderRules& rules) {
    float w = static_cast<float>(bounds.xmax - bounds.xmin);
    float h = static_cast<float>(bounds.ymax - bounds.ymin);
    const int capY = rules.shadowHeightExpCap;
    const float limitY = static_cast<float>(1 << capY);
    float ex = w < 256.0f ? std::log2(std::max(w, 1.0f)) : 8.0f;
    float ey = h < limitY ? std::log2(std::max(h, 1.0f)) : static_cast<float>(capY);
    int W = 1 << static_cast<int>(ex + 0.5f);
    int H = 1 << static_cast<int>(ey + 0.5f);
    if (quality == ShadowQuality::Low) { W /= 2; H /= 2; }
    else if (quality == ShadowQuality::High) { W *= 2; H *= 2; }
    if (rules.shadowMaxSide > 0) { // as2 delta 5.2 step 1: halved until 2·side <= 2·max
        while (W > rules.shadowMaxSide) W /= 2;
        while (H > rules.shadowMaxSide) H /= 2;
    }
    width = std::max(W, 1);
    height = std::max(H, 1);
}

// ---------------------------------------------------------------------------
// ShadowSkin
// ---------------------------------------------------------------------------

ShadowSkin::ShadowSkin(const Image& skin) {
    if (skin.width <= 0 || skin.height <= 0 ||
        skin.rgba.size() < static_cast<size_t>(skin.width) * static_cast<size_t>(skin.height) * 4)
        return;
    Level base;
    base.w = skin.width;
    base.h = skin.height;
    base.a.resize(static_cast<size_t>(base.w) * static_cast<size_t>(base.h));
    for (size_t i = 0; i < base.a.size(); i++) {
        base.a[i] = skin.rgba[i * 4 + 3];
        if (base.a[i] != 255) opaque_ = false;
    }
    levels_.push_back(std::move(base));
    if (opaque_) return; // every level would be 255: sample() answers 1 without them
    // glGenerateMipmap: each level halves (floor, at least 1) by a 2x2 box, rounded.
    while (levels_.back().w > 1 || levels_.back().h > 1) {
        const Level& p = levels_.back();
        Level n;
        n.w = std::max(p.w / 2, 1);
        n.h = std::max(p.h / 2, 1);
        n.a.resize(static_cast<size_t>(n.w) * static_cast<size_t>(n.h));
        for (int y = 0; y < n.h; y++) {
            const int y0 = std::min(2 * y, p.h - 1), y1 = std::min(2 * y + 1, p.h - 1);
            for (int x = 0; x < n.w; x++) {
                const int x0 = std::min(2 * x, p.w - 1), x1 = std::min(2 * x + 1, p.w - 1);
                int sum = p.a[static_cast<size_t>(y0) * p.w + x0] + p.a[static_cast<size_t>(y0) * p.w + x1] +
                          p.a[static_cast<size_t>(y1) * p.w + x0] + p.a[static_cast<size_t>(y1) * p.w + x1];
                n.a[static_cast<size_t>(y) * n.w + x] = static_cast<u8>((sum + 2) / 4);
            }
        }
        levels_.push_back(std::move(n));
    }
}

float ShadowSkin::bilinear(const Level& l, float u, float v) const {
    // GL_LINEAR with GL_REPEAT: texel centres at (i + 0.5) / size, v = 0 at image row 0.
    // (sample() keeps |u|, |v| below 1e6, so the floors fit an int.)
    float x = u * static_cast<float>(l.w) - 0.5f;
    float y = v * static_cast<float>(l.h) - 0.5f;
    int fx = static_cast<int>(x), fy = static_cast<int>(y);
    if (static_cast<float>(fx) > x) --fx; // floor
    if (static_cast<float>(fy) > y) --fy;
    float ax = x - static_cast<float>(fx), ay = y - static_cast<float>(fy);
    int ix = fx % l.w;
    int iy = fy % l.h;
    if (ix < 0) ix += l.w;
    if (iy < 0) iy += l.h;
    int ix1 = ix + 1 >= l.w ? 0 : ix + 1;
    int iy1 = iy + 1 >= l.h ? 0 : iy + 1;
    const u8* r0 = &l.a[static_cast<size_t>(iy) * l.w];
    const u8* r1 = &l.a[static_cast<size_t>(iy1) * l.w];
    float top = r0[ix] + (r0[ix1] - r0[ix]) * ax;
    float bot = r1[ix] + (r1[ix1] - r1[ix]) * ax;
    return (top + (bot - top) * ay) * (1.0f / 255.0f);
}

float ShadowSkin::sample(float u, float v, float lod) const {
    if (levels_.empty() || opaque_) return 1.0f;
    if (!(u > -1e6f && u < 1e6f && v > -1e6f && v < 1e6f)) return 1.0f;
    // GL_LINEAR_MIPMAP_LINEAR (GLES 3.0 3.8.10.4): magnification at lod <= 0, else the two
    // nearest levels blended, the last level alone beyond it.
    const int last = static_cast<int>(levels_.size()) - 1;
    if (!(lod > 0.0f)) return bilinear(levels_[0], u, v);
    if (lod >= static_cast<float>(last)) return bilinear(levels_[static_cast<size_t>(last)], u, v);
    int d = static_cast<int>(lod);
    float f = lod - static_cast<float>(d);
    float a0 = bilinear(levels_[static_cast<size_t>(d)], u, v);
    float a1 = bilinear(levels_[static_cast<size_t>(d) + 1], u, v);
    return a0 + (a1 - a0) * f;
}

// ---------------------------------------------------------------------------
// Silhouette
// ---------------------------------------------------------------------------

namespace {

// Vertices are snapped to 1/256 pixel (8 sub-pixel bits, as Mesa's and most GPUs'
// rasterisers), and coverage is decided exactly in that fixed point.
constexpr int kSubBits = 8;
constexpr std::int64_t kOne = std::int64_t(1) << kSubBits;
constexpr std::int64_t kHalf = kOne / 2;
constexpr double kSub = static_cast<double>(kOne);

struct Corner {
    float x, y, z, u, v;
};

// First and last pixel whose centre (i + 0.5) lies at or after / at or before `m` (fixed point).
int firstCentreFrom(std::int64_t m) {
    std::int64_t v = m - kHalf;
    return static_cast<int>(v >= 0 ? (v + kOne - 1) / kOne : -((-v) / kOne));
}
int lastCentreUpTo(std::int64_t m) {
    std::int64_t v = m - kHalf;
    return static_cast<int>(v >= 0 ? v / kOne : -((-v + kOne - 1) / kOne));
}

} // namespace

bool rasterizeShadowSilhouette(const ModelData& model, const ShadowSkin* skin, ShadowKind kind,
                               const Vec3& towardsSun, int rotationSteps, ShadowQuality quality,
                               const RenderRules& rules, ShadowSilhouette& out, int maxSide) {
    if (model.faces.empty() || model.positions.empty()) return false;
    if (kind == ShadowKind::Planar) rotationSteps = 0;
    const Vec3 L = shadowLightDirection(kind, towardsSun);
    const ShadowBounds bounds = computeShadowBounds(model, kind, towardsSun, rotationSteps);
    int W, H;
    shadowTextureSize(bounds, quality, W, H, rules);
    if (maxSide > 0) {
        W = std::min(W, maxSide);
        H = std::min(H, maxSide);
    }
    const int rw = 2 * W, rh = 2 * H; // supersampled grid, as generate()'s FBO
    const float sx = static_cast<float>(rw) / static_cast<float>(bounds.xmax - bounds.xmin);
    const float sy = static_cast<float>(rh) / static_cast<float>(bounds.ymax - bounds.ymin);

    // The corner list of generate(): a corner whose position index is out of range is
    // skipped, which shifts the following triangles exactly as glDrawArrays sees them.
    const float a = degToRad(static_cast<float>(rotationSteps) * kShadowRotationStepDegrees);
    const float c = std::cos(a), s = std::sin(a);
    std::vector<Corner> corners;
    corners.reserve(model.faces.size() * 3);
    float zmin = 1e30f, zmax = -1e30f;
    for (const Face& f : model.faces) {
        for (int k = 0; k < 3; k++) {
            if (f.v[k] >= model.positions.size()) continue;
            Vec3 p = projectShadowVertex(model.positions[f.v[k]], c, s, L);
            Vec2 uv = f.uv[k] < model.uvs.size() ? model.uvs[f.uv[k]] : Vec2{};
            corners.push_back({(p.x - static_cast<float>(bounds.xmin)) * sx, (p.y - static_cast<float>(bounds.ymin)) * sy,
                               p.z, uv.x, uv.y});
            zmin = std::min(zmin, p.z);
            zmax = std::max(zmax, p.z);
        }
    }
    if (corners.size() < 3) return false;
    zmin = std::min(zmin, 0.0f);
    zmax = std::max(zmax, 0.0f);
    // Depth ties: GL compares 24-bit depths of the range [zmin - 1, zmax + 1] with LEQUAL, so
    // two fragments within about one depth step tie and both pass.
    const float depthEps = (zmax - zmin + 2.0f) / static_cast<float>(1 << 24) * 2.0f;

    const bool textured = skin && skin->valid() && !skin->opaque();
    const double texW = textured ? static_cast<double>(skin->width()) : 1.0;
    const double texH = textured ? static_cast<double>(skin->height()) : 1.0;

    // Sample grid in GL window order (row 0 = the bottom, y = 0); flipped at the end.
    const size_t n = static_cast<size_t>(rw) * static_cast<size_t>(rh);
    std::vector<u8> colour(n, 255);                                       // clear to white
    std::vector<float> depth(n, -std::numeric_limits<float>::infinity()); // clear to far

    const size_t triCount = corners.size() / 3;
    for (size_t t = 0; t < triCount; t++) {
        const Corner* q[3] = {&corners[t * 3], &corners[t * 3 + 1], &corners[t * 3 + 2]};
        std::int64_t X[3], Y[3];
        for (int k = 0; k < 3; k++) {
            X[k] = static_cast<std::int64_t>(std::llround(static_cast<double>(q[k]->x) * kSub));
            Y[k] = static_cast<std::int64_t>(std::llround(static_cast<double>(q[k]->y) * kSub));
        }
        std::int64_t area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
        if (area == 0) continue;
        if (area < 0) { // counter-clockwise from here on; there is no culling
            std::swap(X[1], X[2]);
            std::swap(Y[1], Y[2]);
            std::swap(q[1], q[2]);
            area = -area;
        }
        const int x0 = std::max(firstCentreFrom(std::min({X[0], X[1], X[2]})), 0);
        const int x1 = std::min(lastCentreUpTo(std::max({X[0], X[1], X[2]})), rw - 1);
        const int y0 = std::max(firstCentreFrom(std::min({Y[0], Y[1], Y[2]})), 0);
        const int y1 = std::min(lastCentreUpTo(std::max({Y[0], Y[1], Y[2]})), rh - 1);
        if (x0 > x1 || y0 > y1) continue;

        // Edge e runs from corner e to corner e + 1 and its function is positive inside. The
        // top-left rule: an edge going down (dy < 0), or horizontal going left, owns the
        // samples exactly on it, so a shared edge covers each sample once. The others are
        // biased by -1 so that "inside" is simply ">= 0".
        std::int64_t A[3], B[3], E0[3];
        const std::int64_t px0 = static_cast<std::int64_t>(x0) * kOne + kHalf;
        const std::int64_t py0 = static_cast<std::int64_t>(y0) * kOne + kHalf;
        for (int e = 0; e < 3; e++) {
            const int f = (e + 1) % 3;
            const std::int64_t dx = X[f] - X[e], dy = Y[f] - Y[e];
            A[e] = -dy * kOne; // one pixel to the right
            B[e] = dx * kOne;  // one pixel up
            E0[e] = dx * (py0 - Y[e]) - dy * (px0 - X[e]);
            const bool owns = dy < 0 || (dy == 0 && dx < 0);
            if (!owns) E0[e] -= 1;
        }
        // The function of edge e is the weight of the opposite corner (e + 2) % 3, times area.
        const double inv = 1.0 / static_cast<double>(area);
        const Corner& c0 = *q[0];
        const Corner& c1 = *q[1];
        const Corner& c2 = *q[2];
        // z, u and v are affine in the sample position: their value at the first sample of the
        // box and their steps per sample right and up, from the edge functions (exact
        // barycentrics in double once per triangle, then float steps: the 3DS's ARM11 converts
        // 64-bit integers and rounds through library calls, too slow per sample).
        auto plane = [&](float a0, float a1, float a2, float& at0, float& dx, float& dy) {
            // value = (e1 * a0 + e2 * a1 + e0 * a2) / area
            const double v0 = (static_cast<double>(E0[1]) * a0 + static_cast<double>(E0[2]) * a1 +
                               static_cast<double>(E0[0]) * a2) * inv;
            const double vx = (static_cast<double>(A[1]) * a0 + static_cast<double>(A[2]) * a1 +
                               static_cast<double>(A[0]) * a2) * inv;
            const double vy = (static_cast<double>(B[1]) * a0 + static_cast<double>(B[2]) * a1 +
                               static_cast<double>(B[0]) * a2) * inv;
            at0 = static_cast<float>(v0);
            dx = static_cast<float>(vx);
            dy = static_cast<float>(vy);
        };
        float z0, zdx, zdy, u0 = 0, udx = 0, udy = 0, v0 = 0, vdx = 0, vdy = 0;
        plane(c0.z, c1.z, c2.z, z0, zdx, zdy);

        float lod = 0.0f;
        if (textured) {
            // Constant UV gradient (orthographic, so affine). The level of detail is log2 of the
            // footprint's major axis (the larger singular value of the texel-space Jacobian),
            // not GLES 3.0 3.8.10's max of the two screen-axis lengths: that is what the GL
            // reference does (measured on the desktop GPU; equal for isotropic footprints,
            // larger for anisotropic ones such as palm leaves seen edge-on).
            const double fx1 = static_cast<double>(X[1] - X[0]) / kSub, fy1 = static_cast<double>(Y[1] - Y[0]) / kSub;
            const double fx2 = static_cast<double>(X[2] - X[0]) / kSub, fy2 = static_cast<double>(Y[2] - Y[0]) / kSub;
            const double ar = static_cast<double>(area) / (kSub * kSub);
            const double du1 = c1.u - c0.u, du2 = c2.u - c0.u, dv1 = c1.v - c0.v, dv2 = c2.v - c0.v;
            const double sdx = (du1 * fy2 - du2 * fy1) / ar * texW, sdy = (du2 * fx1 - du1 * fx2) / ar * texW;
            const double tdx = (dv1 * fy2 - dv2 * fy1) / ar * texH, tdy = (dv2 * fx1 - dv1 * fx2) / ar * texH;
            const double p = sdx * sdx + tdx * tdx, q = sdy * sdy + tdy * tdy, r = sdx * sdy + tdx * tdy;
            const double major2 = 0.5 * (p + q + std::sqrt((p - q) * (p - q) + 4.0 * r * r));
            lod = major2 > 0.0 ? static_cast<float>(0.5 * std::log2(major2)) : -100.0f;
            plane(c0.u, c1.u, c2.u, u0, udx, udy);
            plane(c0.v, c1.v, c2.v, v0, vdx, vdy);
        }

        for (int y = y0; y <= y1; y++) {
            const int64_t dyRows = y - y0;
            std::int64_t e0 = E0[0] + B[0] * dyRows;
            std::int64_t e1 = E0[1] + B[1] * dyRows;
            std::int64_t e2 = E0[2] + B[2] * dyRows;
            // The covered run of this row: the samples where every edge function is >= 0
            // (each is linear in x: a lower bound where it rises, an upper one where it falls).
            int xa = x0, xb = x1;
            const std::int64_t span = x1 - x0 + 1;
            const std::int64_t er[3] = {e0, e1, e2};
            for (int e = 0; e < 3 && xa <= xb; e++) {
                const std::int64_t a = A[e], ev = er[e];
                if (a == 0) {
                    if (ev < 0) xa = xb + 1;
                } else if (a > 0) {
                    if (ev < 0) xa = std::max(xa, x0 + static_cast<int>(std::min<std::int64_t>((-ev + a - 1) / a, span)));
                } else if (ev < 0) {
                    xa = xb + 1;
                } else {
                    xb = std::min(xb, x0 + static_cast<int>(std::min<std::int64_t>(ev / -a, span)));
                }
            }
            if (xa > xb) continue;
            u8* crow = &colour[static_cast<size_t>(y) * rw];
            float* drow = &depth[static_cast<size_t>(y) * rw];
            const float fy = static_cast<float>(dyRows);
            const float zr = z0 + zdy * fy, ur = u0 + udy * fy, vr = v0 + vdy * fy;
            for (int x = xa; x <= xb; x++) {
                const float k = static_cast<float>(x - x0);
                const float z = zr + zdx * k;
                float& d = drow[x];
                if (z < d - depthEps) continue; // GL_LEQUAL: the highest z wins, ties pass
                d = z;
                if (!textured) {
                    crow[x] = 0; // black at alpha 1
                    continue;
                }
                const float alpha = skin->sample(ur + udx * k, vr + vdx * k, lod);
                // Black blended by alpha into the 8-bit target: dst * (1 - alpha), rounded.
                crow[x] = static_cast<u8>(static_cast<float>(crow[x]) * (1.0f - alpha) + 0.5f);
            }
        }
    }

    // Step 4: the white quad at z = 0 (depth test LEQUAL, no blend) on projected shadows;
    // step 5: + 0.6 (153), saturating.
    const bool erase = kind == ShadowKind::Projected && rules.shadowEraseBelowGround;
    for (size_t i = 0; i < n; i++) {
        int r = colour[i];
        if (erase && depth[i] <= depthEps) r = 255;
        colour[i] = static_cast<u8>(std::min(255, r + 153));
    }

    // Step 6: 2x2 box filter to W x H, RGB (114, 114, 114), alpha = 255 - red. Image row y
    // is window rows rh - 1 - 2y and rh - 2 - 2y (row 0 = the top).
    Image img;
    img.width = W;
    img.height = H;
    img.hasAlpha = true;
    img.rgba.resize(static_cast<size_t>(W) * static_cast<size_t>(H) * 4);
    for (int y = 0; y < H; y++) {
        const u8* r0 = &colour[static_cast<size_t>(rh - 1 - 2 * y) * rw];
        const u8* r1 = &colour[static_cast<size_t>(rh - 2 - 2 * y) * rw];
        for (int x = 0; x < W; x++) {
            int sum = r0[2 * x] + r0[2 * x + 1] + r1[2 * x] + r1[2 * x + 1];
            u8* px = &img.rgba[(static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x)) * 4];
            px[0] = px[1] = px[2] = 114;
            px[3] = static_cast<u8>(255 - sum / 4);
        }
    }
    out.image = std::move(img);
    out.bounds = bounds;
    out.kind = kind;
    out.width = W;
    out.height = H;
    return true;
}

} // namespace as3d
