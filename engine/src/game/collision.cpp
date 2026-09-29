// Screen-space collision (docs/spec/engine-behaviour.md 5): rectangles on the fixed
// 800x600 viewport, projected with the previous frame's matrices; the touch pass; the
// two-player push-apart.
#include <algorithm>
#include <cmath>

#include "as3d/defs.h"
#include "as3d/world.h"
#include "world_internal.h"

namespace as3d {

bool World::projectPoint(const Vec3& p, float out[3]) const {
    Vec4 c = prevViewProj_ * Vec4{p.x, p.y, p.z, 1.0f};
    if (!(c.w > 1e-6f)) return false;
    float inv = 1.0f / c.w;
    out[0] = (c.x * inv * 0.5f + 0.5f) * kCollisionViewportW;
    out[1] = (c.y * inv * 0.5f + 0.5f) * kCollisionViewportH;
    out[2] = c.z * inv * 0.5f + 0.5f;
    return true;
}

bool World::sphereInFrustum(const Vec3& c, float r) const {
    for (const Vec4& pl : planes_) {
        if (pl.x * c.x + pl.y * c.y + pl.z * c.z + pl.w < -r) return false;
    }
    return true;
}

bool World::isPointCollider(const Entity& e) const {
    bool model = e.def && e.def->type == ObjectType::Model;
    return !model || (e.flagBits() & FL_POINT_COLLISION);
}

void World::computeScreenBounds(int idx) {
    Entity& e = ents_[static_cast<size_t>(idx)];
    if (e.emitter) return;
    // Scenery (class 0, no touch filter): collidable, no rectangle.
    if (e.f(F_CLASS) == 0.0f && e.touchMode == 0) {
        e.rt |= RT_COLLIDABLE;
        return;
    }
    e.rt &= ~RT_COLLIDABLE;
    Vec3 org = e.v3(F_BASE_ORIGIN);
    if (!sphereInFrustum(org, e.radius)) return;
    if (!isPointCollider(e)) {
        // Model box scaled by bbox_scale about its centre (GUESS for the pivot, spec 5.1),
        // placed with the entity transform.
        float s = e.f(F_SCALE);
        float k = s > 0.01f ? s : 1.0f;
        Vec3 c = (e.boundsMin + e.boundsMax) * 0.5f;
        Vec3 h = (e.boundsMax - e.boundsMin) * 0.5f;
        h = {h.x * e.bboxScale[0], h.y * e.bboxScale[1], h.z * e.bboxScale[2]};
        Vec3 fw = e.v3(F_AXIS), lf = e.v3(F_AXIS + 3), up = e.v3(F_AXIS + 6);
        ScreenRect r;
        for (int i = 0; i < 8; ++i) {
            Vec3 lc{c.x + ((i & 1) ? h.x : -h.x), c.y + ((i & 2) ? h.y : -h.y), c.z + ((i & 4) ? h.z : -h.z)};
            lc = lc * k;
            Vec3 w = org + fw * lc.x + lf * lc.y + up * lc.z;
            float p[3];
            if (!projectPoint(w, p)) return;
            for (int a = 0; a < 3; ++a) {
                if (i == 0 || p[a] < r.min[a]) r.min[a] = p[a];
                if (i == 0 || p[a] > r.max[a]) r.max[a] = p[a];
            }
        }
        e.rect = r;
        if (r.min[0] >= 0.0f && r.min[1] >= 0.0f && r.max[0] < kCollisionViewportW && r.max[1] < kCollisionViewportH) {
            e.rt |= RT_COLLIDABLE;
        }
    } else {
        // Point colliders: a segment from this frame's projected origin (min) to the
        // previous frame's (max).
        float p[3];
        if (!projectPoint(org, p)) return;
        for (int a = 0; a < 3; ++a) {
            e.rect.min[a] = p[a];
            e.rect.max[a] = e.hasPrevPoint ? e.prevPoint[a] : p[a];
            e.prevPoint[a] = p[a];
        }
        e.hasPrevPoint = true;
        if (p[0] >= 0.0f && p[1] >= 0.0f && p[0] < kCollisionViewportW && p[1] < kCollisionViewportH) {
            e.rt |= RT_COLLIDABLE;
        }
    }
}

bool World::rectsOverlap(const ScreenRect& a, const ScreenRect& b) {
    return a.min[0] <= b.max[0] && a.max[0] >= b.min[0] && a.min[1] <= b.max[1] && a.max[1] >= b.min[1];
}

bool World::pointInRect(const float p[3], const ScreenRect& r) {
    return p[0] >= r.min[0] && p[0] <= r.max[0] && p[1] >= r.min[1] && p[1] <= r.max[1];
}

bool World::segmentHitsRect(const float a[3], const float b[3], const ScreenRect& r) {
    float dx = b[0] - a[0], dy = b[1] - a[1];
    if (std::sqrt(dx * dx + dy * dy) < 0.7f) {
        float m[3] = {(a[0] + b[0]) * 0.5f, (a[1] + b[1]) * 0.5f, 0.0f};
        return pointInRect(m, r);
    }
    // Liang-Barsky clip of the 2D segment against the rectangle.
    float t0 = 0.0f, t1 = 1.0f;
    const float p[4] = {-dx, dx, -dy, dy};
    const float q[4] = {a[0] - r.min[0], r.max[0] - a[0], a[1] - r.min[1], r.max[1] - a[1]};
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0.0f) {
            if (q[i] < 0.0f) return false;
            continue;
        }
        float t = q[i] / p[i];
        if (p[i] < 0.0f) {
            if (t > t1) return false;
            if (t > t0) t0 = t;
        } else {
            if (t < t0) return false;
            if (t < t1) t1 = t;
        }
    }
    return t0 <= t1;
}

void World::touchEntity(int idx) {
    Entity& t = ents_[static_cast<size_t>(idx)];
    int tm = t.touchMode;
    bool tPoint = isPointCollider(t);
    if (tm & 2) { // TOUCH_PLAYER or TOUCH_ALL
        for (int p = 0; p < config_.players; ++p) {
            int pi = playerEntityIndex(p);
            if (pi < 0 || pi == idx) continue;
            const Entity& pe = ents_[static_cast<size_t>(pi)];
            if (!(pe.f(F_HEALTH) > 0.0f)) continue;
            bool hit = tPoint ? pointInRect(t.rect.min, pe.rect) : rectsOverlap(t.rect, pe.rect);
            if (hit) {
                t.playerIndex = p;
                runTouch(idx, pi);
                return; // one player per frame; the enemy scan is skipped
            }
        }
    }
    if (tm & 1) { // TOUCH_ENEMIES or TOUCH_ALL
        for (int c = newest_; c != -1; c = ents_[static_cast<size_t>(c)].older) {
            if (c == idx) continue;
            const Entity& ce = ents_[static_cast<size_t>(c)];
            if (ce.rt & (RT_REMOVED | RT_HEALTH_FROZEN)) continue;
            if (!(ce.rt & RT_COLLIDABLE) || ce.f(F_CLASS) != kClassEnemy) continue;
            bool cPoint = isPointCollider(ce);
            bool hit;
            if (!tPoint && !cPoint) hit = rectsOverlap(t.rect, ce.rect);
            else if (tPoint && !cPoint) hit = segmentHitsRect(t.rect.min, t.rect.max, ce.rect);
            else if (!tPoint && cPoint) hit = true;
            else hit = false;
            if (!hit) continue;
            runTouch(idx, c);
            if (t.rt & RT_REMOVED) break;
        }
    }
}

void World::runCollisions() {
    for (int i = newest_; i != -1;) {
        Entity& e = ents_[static_cast<size_t>(i)];
        int older = e.older;
        if (e.touchMode != 0 && (e.rt & RT_COLLIDABLE) && !(e.rt & RT_REMOVED)) touchEntity(i);
        i = older;
    }
    // Two-player push-apart (5.4).
    if (config_.players == 2) {
        int a = playerEntityIndex(0), b = playerEntityIndex(1);
        if (a >= 0 && b >= 0) {
            Entity& pa = ents_[static_cast<size_t>(a)];
            Entity& pb = ents_[static_cast<size_t>(b)];
            if (pa.f(F_HEALTH) > 0.0f && pb.f(F_HEALTH) > 0.0f && rectsOverlap(pa.rect, pb.rect)) {
                float dx = pa.f(F_ORIGIN) - pb.f(F_ORIGIN), dy = pa.f(F_ORIGIN + 1) - pb.f(F_ORIGIN + 1);
                float len = std::sqrt(dx * dx + dy * dy);
                if (len > 0.0f) {
                    dx /= len;
                    dy /= len;
                }
                float k = 2000.0f * frametime_;
                pa.setF(F_VELOCITY, pa.f(F_VELOCITY) + dx * k);
                pa.setF(F_VELOCITY + 1, pa.f(F_VELOCITY + 1) + dy * k);
                pb.setF(F_VELOCITY, pb.f(F_VELOCITY) - dx * k);
                pb.setF(F_VELOCITY + 1, pb.f(F_VELOCITY + 1) - dy * k);
            }
        }
    }
}

} // namespace as3d
