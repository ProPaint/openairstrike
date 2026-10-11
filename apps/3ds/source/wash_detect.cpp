// The water-wash detector's platform-neutral part (wash_detect.h).
#include "wash_detect.h"

#include <algorithm>
#include <cmath>

namespace ctrapp {

using as3d::Mat4;
using as3d::Vec3;
using as3d::Vec4;

namespace {

// The general 4x4 inverse (cofactors); false when singular. Storage-order agnostic.
bool invert(const float* m, float* out) {
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
             m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
             m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
             m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
              m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
             m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
             m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
             m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
              m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
             m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
              m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
              m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
             m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
              m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (!(std::fabs(det) > 1e-30f)) return false;
    const float r = 1.0f / det;
    for (int i = 0; i < 16; ++i) out[i] = inv[i] * r;
    return true;
}

Vec3 unproject(const Mat4& inv, float xn, float yn, float zn) {
    const Vec4 p = inv * Vec4{xn, yn, zn, 1.0f};
    const float w = std::fabs(p.w) > 1e-20f ? p.w : 1e-20f;
    return Vec3{p.x / w, p.y / w, p.z / w};
}

float eyeDepth(const Mat4& view, const Vec3& p) { return -(view * Vec4{p.x, p.y, p.z, 1.0f}).z; }

constexpr float kStep = 30.0f;      // the march step (three quarters of a height-map cell)
constexpr int kMaxSteps = 200;
constexpr float kAboveWater = 2.0f; // the terrain hit must be this far above the plane

float frontMargin(float waterDepth) { return std::max(8.0f, 0.03f * waterDepth); }
float waterTolerance(float waterDepth) { return std::max(2.0f, 0.01f * waterDepth); }

} // namespace

std::vector<std::pair<int, int>> washSamplePixels(int width, int height) {
    std::vector<std::pair<int, int>> out;
    const float sx = static_cast<float>(width) / 400.0f, sy = static_cast<float>(height) / 240.0f;
    auto row = [&](int y) {
        for (int x = 20; x <= 380; x += 40)
            out.emplace_back(static_cast<int>(static_cast<float>(x) * sx), static_cast<int>(static_cast<float>(y) * sy));
    };
    for (int y = 20; y <= 120; y += 20) row(y);
    for (int y = 130; y <= 230; y += 10) row(y);
    return out;
}

bool washInverse(const WashView& v, Mat4* inverse) { return invert(v.clip.m, inverse->m); }

bool washRay(const WashView& v, const Mat4& inv, const WashScene& s, int x, int y, WashRay* out) {
    const float xn = (static_cast<float>(x) + 0.5f) * 2.0f / static_cast<float>(v.width) - 1.0f;
    const float yn = 1.0f - (static_cast<float>(y) + 0.5f) * 2.0f / static_cast<float>(v.height);
    const Vec3 a = unproject(inv, xn, yn, -1.0f), b = unproject(inv, xn, yn, 1.0f);
    if (!(a.z > s.planeZ) || !(b.z < s.planeZ)) return false; // the ray never comes down to the water
    const float tw = (a.z - s.planeZ) / (a.z - b.z);
    const Vec3 w{a.x + (b.x - a.x) * tw, a.y + (b.y - a.y) * tw, s.planeZ};
    if (w.x < 0.0f || w.y < 0.0f || w.x > s.mapW || w.y > s.mapH) return false;
    out->a = a;
    out->b = b;
    out->tWater = tw;
    out->waterDepth = eyeDepth(v.view, w);
    return true;
}

bool washTerrainFront(const WashView& v, const WashScene& s, const WashRay& r, float* terrainDepth) {
    if (!s.height) return false;
    const Vec3& a = r.a;
    const Vec3& b = r.b;
    const float tw = r.tWater;
    const float top = std::max(s.topZ, s.planeZ) + 1.0f;
    // March from where the ray comes down to the highest terrain to the water point.
    const float t0 = a.z > top ? (a.z - top) / (a.z - b.z) : 0.0f;
    if (t0 >= tw) return false;
    const Vec3 d{b.x - a.x, b.y - a.y, b.z - a.z};
    const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z) * (tw - t0);
    const int steps = std::min(kMaxSteps, std::max(1, static_cast<int>(std::ceil(len / kStep))));
    auto at = [&](float t) { return Vec3{a.x + d.x * t, a.y + d.y * t, a.z + d.z * t}; };
    auto below = [&](const Vec3& p) {
        if (p.x < 0.0f || p.y < 0.0f || p.x > s.mapW || p.y > s.mapH) return false;
        return p.z <= s.height(p.x, p.y);
    };
    float prev = t0, hit = -1.0f;
    for (int i = 1; i <= steps; ++i) {
        const float t = t0 + (tw - t0) * static_cast<float>(i) / static_cast<float>(steps);
        if (below(at(t))) {
            float lo = prev, hi = t;
            for (int k = 0; k < 5; ++k) {
                const float mid = 0.5f * (lo + hi);
                if (below(at(mid))) hi = mid;
                else lo = mid;
            }
            hit = hi;
            break;
        }
        prev = t;
    }
    if (hit < 0.0f) return false;
    const Vec3 h = at(hit);
    if (h.z < s.planeZ + kAboveWater) return false;
    const float td = eyeDepth(v.view, h);
    if (!(td > v.nearPlane) || td >= r.waterDepth - frontMargin(r.waterDepth)) return false;
    *terrainDepth = td;
    return true;
}

std::vector<WashProbe> washProbes(const WashView& v, const WashScene& s, const std::vector<std::pair<int, int>>& pixels) {
    std::vector<WashProbe> out;
    Mat4 inv;
    if (!s.height || !washInverse(v, &inv)) return out;
    for (const auto& px : pixels) {
        WashRay r;
        float td = 0.0f;
        if (!washRay(v, inv, s, px.first, px.second, &r) || !washTerrainFront(v, s, r, &td)) continue;
        WashProbe p;
        p.x = px.first;
        p.y = px.second;
        p.terrainDepth = td;
        p.waterDepth = r.waterDepth;
        out.push_back(p);
    }
    return out;
}

float wBufferEyeDepth(uint32_t depth24, float nearPlane, float farPlane) {
    const float d = static_cast<float>(depth24 & 0xFFFFFFu) / 16777215.0f;
    return farPlane - d * (farPlane - nearPlane);
}

bool washedSample(const WashProbe& p, float gpuEyeDepth) {
    return gpuEyeDepth >= p.waterDepth - waterTolerance(p.waterDepth);
}

bool terrainSample(const WashProbe& p, float gpuEyeDepth) {
    return std::fabs(gpuEyeDepth - p.terrainDepth) <= std::max(4.0f, 0.03f * p.terrainDepth);
}

void WashResult::add(const WashProbe& p, float gpuEyeDepth) {
    ++judged;
    if (washedSample(p, gpuEyeDepth)) {
        if (washed == 0) {
            rowMin = rowMax = p.y;
            colMin = colMax = p.x;
        } else {
            rowMin = std::min(rowMin, p.y);
            rowMax = std::max(rowMax, p.y);
            colMin = std::min(colMin, p.x);
            colMax = std::max(colMax, p.x);
        }
        ++washed;
    } else if (terrainSample(p, gpuEyeDepth)) {
        ++agree;
        terrainErr += std::fabs(gpuEyeDepth - p.terrainDepth);
    } else if (gpuEyeDepth < p.terrainDepth) {
        ++nearer;
    }
}

} // namespace ctrapp
