// Lightning bolt geometry (backend neutral), docs/spec/render-pipeline.md 7.1.
#include "as3d/lightning_render.h"

#include <cmath>

namespace as3d {

namespace {

constexpr float kHalfWidth = 8.0f;
constexpr float kUnitsPerRepeat = 96.0f;
constexpr float kScrollPerSecond = 3.0f;

} // namespace

Vec3 perpendicularVector(const Vec3& d) {
    const float a[3] = {std::fabs(d.x), std::fabs(d.y), std::fabs(d.z)};
    int k = 0;
    if (a[1] < a[k]) k = 1;
    if (a[2] < a[k]) k = 2;
    Vec3 axis{k == 0 ? 1.0f : 0.0f, k == 1 ? 1.0f : 0.0f, k == 2 ? 1.0f : 0.0f};
    Vec3 p = axis - d * dot(axis, d);
    float len = length(p);
    return len > 1e-12f ? p * (1.0f / len) : axis;
}

bool buildLightningQuads(const Vec3& start, const Vec3& end, float time, LightningVertex out[8]) {
    Vec3 d = end - start;
    float len = length(d);
    if (!(len > 1e-6f) || !std::isfinite(len)) return false;
    Vec3 dn = d * (1.0f / len);
    Vec3 p1 = perpendicularVector(dn);
    Vec3 p2 = cross(dn, p1);
    float s0 = std::fmod(time * kScrollPerSecond, 1.0f); // repeat wrap: only the fraction matters
    float s1 = s0 + len / kUnitsPerRepeat;
    const Vec3 ps[2] = {p1, p2};
    for (int q = 0; q < 2; ++q) {
        Vec3 o = ps[q] * kHalfWidth;
        LightningVertex* v = out + 4 * q;
        v[0] = {start - o, {s0, 1.0f}};
        v[1] = {end - o, {s1, 1.0f}};
        v[2] = {end + o, {s1, 0.0f}};
        v[3] = {start + o, {s0, 0.0f}};
    }
    return true;
}


int buildLightningBolts(const Vec3* starts, const Vec3* ends, size_t count, float time, const Texture2D* texture,
                        std::vector<LightningVertex>& verts, std::vector<DrawRun>& runs) {
    if (count == 0 || !texture) return 0;
    const size_t first = verts.size();
    int bolts = 0;
    for (size_t i = 0; i < count; ++i) {
        LightningVertex q[8];
        if (!buildLightningQuads(starts[i], ends[i], time, q)) continue;
        const int order[6] = {0, 1, 2, 0, 2, 3};
        for (int k = 0; k < 2; ++k)
            for (int j : order) verts.push_back(q[4 * k + j]);
        ++bolts;
    }
    if (verts.size() > first) {
        DrawRun r;
        r.key = blendKey(RenderBlend::Add, false, false, CullMode::Off);
        r.texture = texture;
        r.first = static_cast<int>(first);
        r.count = static_cast<int>(verts.size() - first);
        appendDrawRun(runs, r);
    }
    return bolts;
}

} // namespace as3d
