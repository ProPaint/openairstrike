// Stereo comfort (3DS, docs/3ds-port.md "Stereo comfort and frame polish (round 2, P5)"), as plain
// math: header only, platform neutral, desktop-tested in apps/tests/pica_core_test.cpp.
//
// The parallax model. The eyes sit at -e and +e along the camera's right axis and converge at
// the focal depth D (pica_space.h). A point at depth d lands W A e (1/D - 1/d) pixels apart in
// the two eyes (W = 400, A = 1 / (aspect tan(fovY / 2)) = 1.04): positive (behind the screen)
// beyond D, negative (in front, "popping out") nearer than D. The frame composer puts D at the
// player's helicopter, so
//   - the nearest visible content (the terrain at the bottom of the screen, a hill or a building
//     between the camera and the player) has the largest negative parallax, W A e (1/dn - 1/D);
//   - the farthest visible terrain (the hills under the fog) the largest positive one,
//     W A e (1/D - 1/df), at most W A e / D.
// comfortRatio() is the largest fraction of the slider's eye offset that keeps both within their
// limits (max_parallax_px, max_far_parallax_px, scaled by the slider); ComfortFilter moves the
// eye offset towards it a little per frame, so a close object entering the view never changes the
// depth in one step.
//
// probeTerrainDepth() finds the nearest and farthest terrain the central camera sees by marching
// rays through a 5 x 6 grid of screen points over the height field; modelNearDepth() the nearest
// point of a model's box inside the view.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "as3d/math.h"

namespace as3d::pica {

// Horizontal parallax, in pixels per world unit of eye offset, of a point at depth d for the focal
// depth D on a screen `width` pixels wide with A = `a`: > 0 behind the screen, < 0 in front.
inline float parallaxPxPerUnit(float d, float focal, float a, float width) {
    if (!(d > 0.0f) || !(focal > 0.0f)) return 0.0f;
    return width * a * (1.0f / focal - 1.0f / d);
}

// A = 1 / (aspect tan(fovY / 2)) of a projection (pica_space.h).
inline float stereoA(float fovYDegrees, float aspect) {
    return 1.0f / (std::tan(degToRad(fovYDegrees) * 0.5f) * aspect);
}

struct ComfortLimits {
    float maxNearPx = 6.0f; // negative parallax of the nearest content at slider 1 (run.ini max_parallax_px)
    float maxFarPx = 8.0f;  // positive parallax of the farthest terrain at slider 1 (max_far_parallax_px)
};

// The fraction (0..1] of the slider's eye offset `fullOffset` (= slider x eye_offset) that keeps
// the nearest content (depth `nearDepth`, 0 = none) within slider x maxNearPx in front of the
// screen and the farthest (depth `farDepth`, 0 = none) within slider x maxFarPx behind it. The
// slider cancels out: the limits scale with it, as the offset does.
inline float comfortRatio(const ComfortLimits& lim, float nearDepth, float farDepth, float focal, float a, float width,
                          float maxEyeOffset) {
    float r = 1.0f;
    if (!(maxEyeOffset > 0.0f)) return r;
    const float kn = -parallaxPxPerUnit(nearDepth, focal, a, width); // > 0 when nearer than the focal plane
    if (kn > 0.0f) r = std::min(r, lim.maxNearPx / (kn * maxEyeOffset));
    const float kf = parallaxPxPerUnit(farDepth, focal, a, width);
    if (kf > 0.0f) r = std::min(r, lim.maxFarPx / (kf * maxEyeOffset));
    return std::max(r, 0.0f);
}

// The focal depth for a view without a player to anchor the screen plane (the attract levels behind
// the menus): the one that lets the largest eye offset (at most `maxEyeOffset`) keep the nearest
// content within lim.maxNearPx in front and the farthest within lim.maxFarPx behind, as near to the
// nearest content as the far limit allows (the scene behind the screen when it can be). 0 without
// a nearest depth.
inline float balancedFocal(const ComfortLimits& lim, float nearDepth, float farDepth, float a, float width,
                           float maxEyeOffset) {
    if (!(nearDepth > 0.0f) || !(maxEyeOffset > 0.0f)) return 0.0f;
    if (!(farDepth > nearDepth)) return nearDepth;
    const float k = width * a;
    const float span = 1.0f / nearDepth - 1.0f / farDepth;
    const float e = std::min(maxEyeOffset, (lim.maxNearPx + lim.maxFarPx) / (k * span));
    const float invD = std::min(1.0f / nearDepth, 1.0f / farDepth + lim.maxFarPx / (k * e));
    return 1.0f / invD;
}

// The eye offset's comfort ratio over time: towards the target by at most `downPerSecond` (when
// the depth must shrink) or `upPerSecond` (when it may grow back) times the elapsed time. No step
// changes: a close object entering the view takes the depth down over a third of a second, its
// leaving brings it back over two and a half.
struct ComfortFilter {
    float ratio = 1.0f;
    bool primed = false;
    float downPerSecond = 24.0f; // a close object takes the depth down within two or three frames
    float upPerSecond = 0.4f;

    void reset() { primed = false; ratio = 1.0f; }
    // `dt` the seconds since the last update (the simulation steps / 60). The first update after a
    // reset takes the target at once (a level start, the slider coming up from 0).
    float update(float target, float dt) {
        target = std::clamp(target, 0.0f, 1.0f);
        if (!primed) {
            primed = true;
            ratio = target;
            return ratio;
        }
        const float dtc = std::clamp(dt, 0.0f, 0.25f);
        const float d = target - ratio;
        if (d < 0.0f) ratio = std::max(target, ratio - downPerSecond * dtc);
        else ratio = std::min(target, ratio + upPerSecond * dtc);
        return ratio;
    }
};

// The focal depth over time: the player's depth, moving by at most `relPerSecond` of itself per
// second (a respawn or a camera jump eases the screen plane over instead of snapping the whole
// scene's depth). The first update after a reset takes it at once.
struct FocalFilter {
    float focal = 0.0f;
    bool primed = false;
    float relPerSecond = 1.5f;

    void reset() { primed = false; }
    float update(float target, float dt) {
        if (!primed || !(focal > 0.0f)) {
            primed = true;
            focal = target;
            return focal;
        }
        const float maxStep = focal * relPerSecond * std::clamp(dt, 0.0f, 0.25f);
        focal += std::clamp(target - focal, -maxStep, maxStep);
        return focal;
    }
};

// The nearest and farthest terrain (or water) the central camera sees, by rays through a 5 x 6 grid
// of screen points (the corners and edges included), one grid row (kProbeRows, bottom to top) per
// call of probeTerrainRow so that the frame composer spreads a probe over several frames.
// `height(x, y)` is the surface height at world (x, y), or -1e30 outside the map; `topZ` is at
// least the highest surface (a ray starts where it comes down to it; one that never does is sky).
// Each ray marches in steps of 6 % of the depth (bisected four times at the first point below the
// surface) up to `farLimit` (the far plane, or the fog's end). Rays that hit nothing (the sky)
// count for neither.
struct TerrainDepth {
    float nearest = 0.0f, farthest = 0.0f;
    int hits = 0, steps = 0;
    // Adds another probe's result (another row).
    void merge(const TerrainDepth& o) {
        if (o.hits > 0) {
            if (hits == 0 || o.nearest < nearest) nearest = o.nearest;
            if (hits == 0 || o.farthest > farthest) farthest = o.farthest;
        }
        hits += o.hits;
        steps += o.steps;
    }
};

constexpr int kProbeRows = 6, kProbeCols = 5;

template <class HeightFn>
TerrainDepth probeTerrainRow(const Mat4& view, float fovYDegrees, float aspect, float nearPlane, float farLimit,
                             float topZ, const HeightFn& height, int row) {
    TerrainDepth out;
    const float ty = std::tan(degToRad(fovYDegrees) * 0.5f), tx = ty * aspect;
    // The camera (view is rigid: eye = R world + t, R(row, col) = view.at(col, row)).
    const Vec3 t{view.at(3, 0), view.at(3, 1), view.at(3, 2)};
    auto toWorld = [&](const Vec3& e) {
        const Vec3 d{e.x - t.x, e.y - t.y, e.z - t.z};
        return Vec3{view.at(0, 0) * d.x + view.at(0, 1) * d.y + view.at(0, 2) * d.z,
                    view.at(1, 0) * d.x + view.at(1, 1) * d.y + view.at(1, 2) * d.z,
                    view.at(2, 0) * d.x + view.at(2, 1) * d.y + view.at(2, 2) * d.z};
    };
    const Vec3 cam = toWorld(Vec3{0, 0, 0});
    static constexpr float kU[kProbeCols] = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};
    static constexpr float kV[kProbeRows] = {-1.0f, -0.6f, -0.2f, 0.2f, 0.6f, 1.0f};
    if (row < 0 || row >= kProbeRows) return out;
    const float v = kV[row];
    const float d0 = std::max(nearPlane, 0.5f);
    for (float u : kU) {
        const Vec3 p1 = toWorld(Vec3{u * tx, v * ty, -1.0f});
        const Vec3 dir{p1.x - cam.x, p1.y - cam.y, p1.z - cam.z}; // per unit of depth
        auto below = [&](float d) {
            const Vec3 p{cam.x + dir.x * d, cam.y + dir.y * d, cam.z + dir.z * d};
            return p.z <= height(p.x, p.y);
        };
        // Above every surface the ray cannot hit: start where it comes down to topZ.
        float start = d0;
        if (cam.z > topZ) {
            if (!(dir.z < 0.0f)) continue; // level or rising above the highest surface: sky
            start = std::max(d0, (cam.z - topZ) / -dir.z);
        }
        float prev = start, d = start;
        bool hit = false;
        while (d <= farLimit) {
            ++out.steps;
            if (below(d)) {
                hit = true;
                break;
            }
            prev = d;
            d = d * 1.06f + 0.25f;
        }
        if (!hit) continue;
        if (d > prev) {
            float lo = prev, hi = d;
            for (int k = 0; k < 4; ++k) {
                const float mid = 0.5f * (lo + hi);
                if (below(mid)) hi = mid;
                else lo = mid;
            }
            d = hi;
        }
        TerrainDepth one;
        one.nearest = one.farthest = d;
        one.hits = 1;
        out.merge(one);
    }
    return out;
}

// Every row at once (the tests, a first frame).
template <class HeightFn>
bool probeTerrainDepth(const Mat4& view, float fovYDegrees, float aspect, float nearPlane, float farLimit, float topZ,
                       const HeightFn& height, TerrainDepth& out) {
    out = TerrainDepth();
    for (int r = 0; r < kProbeRows; ++r)
        out.merge(probeTerrainRow(view, fovYDegrees, aspect, nearPlane, farLimit, topZ, height, r));
    return out.hits > 0;
}

// The nearest depth (clamped to the near plane) of a model's box (model-space bounds, eye-space
// matrix `modelView`) when the box can be inside the central camera's view (a conservative test on
// the box's eye-space bounds against the frustum's four side planes and its near / far planes);
// 0 when it cannot.
inline float modelNearDepth(const Mat4& modelView, const Vec3& bmin, const Vec3& bmax, float fovYDegrees, float aspect,
                            float nearPlane, float farPlane) {
    float xmin = 1e30f, xmax = -1e30f, ymin = 1e30f, ymax = -1e30f, dmin = 1e30f, dmax = -1e30f;
    for (int c = 0; c < 8; ++c) {
        const Vec3 p{(c & 1) ? bmax.x : bmin.x, (c & 2) ? bmax.y : bmin.y, (c & 4) ? bmax.z : bmin.z};
        const Vec4 e = modelView * Vec4{p.x, p.y, p.z, 1.0f};
        xmin = std::min(xmin, e.x);
        xmax = std::max(xmax, e.x);
        ymin = std::min(ymin, e.y);
        ymax = std::max(ymax, e.y);
        dmin = std::min(dmin, -e.z);
        dmax = std::max(dmax, -e.z);
    }
    if (dmax < nearPlane || dmin > farPlane) return 0.0f;
    const float ty = std::tan(degToRad(fovYDegrees) * 0.5f), tx = ty * aspect;
    // At its farthest depth the view is widest: a box outside there is outside everywhere.
    if (xmin > tx * dmax || xmax < -tx * dmax || ymin > ty * dmax || ymax < -ty * dmax) return 0.0f;
    return std::max(dmin, nearPlane);
}

// The nearest depth (clamped to the near plane) of a mesh's vertices inside the view (eye-space
// matrix `modelView`), or 0 when none is: exact where modelNearDepth's box is conservative (the
// banner's and the model view's screen plane: a long, turned mesh's box reaches far nearer than
// the mesh).
// meshDepthRange also gives the farthest such vertex (false when no vertex is in the view).
inline bool meshDepthRange(const Mat4& modelView, const std::vector<Vec3>& positions, float fovYDegrees, float aspect,
                           float nearPlane, float farPlane, float& nearest, float& farthest) {
    const float ty = std::tan(degToRad(fovYDegrees) * 0.5f), tx = ty * aspect;
    nearest = farthest = 0.0f;
    for (const Vec3& p : positions) {
        const Vec4 e = modelView * Vec4{p.x, p.y, p.z, 1.0f};
        const float d = -e.z;
        if (d < nearPlane * 0.5f || d > farPlane) continue;
        if (std::fabs(e.x) > tx * d || std::fabs(e.y) > ty * d) continue;
        if (nearest == 0.0f || d < nearest) nearest = d;
        if (d > farthest) farthest = d;
    }
    if (!(nearest > 0.0f)) return false;
    nearest = std::max(nearest, nearPlane);
    farthest = std::max(farthest, nearest);
    return true;
}

inline float meshNearDepth(const Mat4& modelView, const std::vector<Vec3>& positions, float fovYDegrees, float aspect,
                           float nearPlane, float farPlane) {
    float n = 0.0f, f = 0.0f;
    return meshDepthRange(modelView, positions, fovYDegrees, aspect, nearPlane, farPlane, n, f) ? n : 0.0f;
}

// The eye offset that keeps an object whose depth runs from `focal` (its front, on the screen plane)
// to `farDepth` within `maxPx` pixels of positive parallax on a viewport `width` pixels wide with
// A = `a`: `eyeOffset` when it already does.
inline float limitEyeOffsetForDepth(float eyeOffset, float focal, float farDepth, float a, float width, float maxPx) {
    const float k = parallaxPxPerUnit(farDepth, focal, a, width);
    if (k > 0.0f && k * eyeOffset > maxPx) return std::max(0.0f, maxPx / k);
    return eyeOffset;
}

} // namespace as3d::pica
