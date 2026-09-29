// Screen-space collision (docs/spec/engine-behaviour.md 5, render-pipeline.md 9.3, corrected
// by docs/spec/issues/120): rectangles on the fixed 800x600 viewport, projected with the
// previous frame's matrices; the on-screen bit 0x08 means "touches the window"; the touch
// pass; the two-player push-apart.
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

// Visible when dot(plane, origin) + radius > 0 for all six planes (0x419970).
bool World::sphereInFrustum(const Vec3& c, float r) const {
    for (const Vec4& pl : planes_) {
        if (!(pl.x * c.x + pl.y * c.y + pl.z * c.z + pl.w + r > 0.0f)) return false;
    }
    return true;
}

// 0x419920 (issue 120 rule 4): the rectangle overlaps the window, bounds as the original
// compares them (max >= 0 inclusive, min < size strict).
bool World::rectTouchesViewport(const ScreenRect& r) {
    return r.max[0] >= 0.0f && r.min[0] < kCollisionViewportW && r.max[1] >= 0.0f && r.min[1] < kCollisionViewportH;
}

// 0x4198d0 (issue 120 rule 5): 0 <= p < size.
bool World::pointOnViewport(const float p[3]) {
    return p[0] >= 0.0f && p[0] < kCollisionViewportW && p[1] >= 0.0f && p[1] < kCollisionViewportH;
}

// TYPE_MODEL (render type field 38 = 0) without FL_POINT_COLLISION uses its model box;
// everything else is a point (a swept segment).
bool World::isPointCollider(const Entity& e) const {
    return e.fields[F_RENDER_TYPE] != 0 || (e.flagBits() & FL_POINT_COLLISION);
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
        // R_ProjectEntityBounds (render-pipeline.md 9.3): corners are bbox_scale times the
        // MDL box bounds component-wise (pivot = model origin), transformed by the axis
        // and origin without the entity scale.
        Vec3 fw = e.v3(F_AXIS), lf = e.v3(F_AXIS + 3), up = e.v3(F_AXIS + 6);
        ScreenRect r;
        r.min[0] = r.min[1] = 9999.0f;
        r.max[0] = r.max[1] = -9999.0f;
        for (int i = 0; i < 8; ++i) {
            Vec3 lc{e.bboxScale[0] * ((i & 1) ? e.boundsMax.x : e.boundsMin.x),
                    e.bboxScale[1] * ((i & 2) ? e.boundsMax.y : e.boundsMin.y),
                    e.bboxScale[2] * ((i & 4) ? e.boundsMax.z : e.boundsMin.z)};
            Vec3 w = org + fw * lc.x + lf * lc.y + up * lc.z;
            float p[3];
            if (!projectPoint(w, p)) return; // behind the eye: not on screen
            for (int a = 0; a < 2; ++a) {
                r.min[a] = std::min(r.min[a], p[a]);
                r.max[a] = std::max(r.max[a], p[a]);
            }
        }
        float c[3];
        float depth = projectPoint(org, c) ? c[2] : 0.0f;
        r.min[2] = r.max[2] = depth;
        e.rect = r;
        // Any part of the box on screen: collidable, can fire and be hit (issue 120).
        if (rectTouchesViewport(r)) e.rt |= RT_COLLIDABLE;
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
        if (pointOnViewport(p)) e.rt |= RT_COLLIDABLE;
    }
}

bool World::rectsOverlap(const ScreenRect& a, const ScreenRect& b) {
    return a.min[0] <= b.max[0] && a.max[0] >= b.min[0] && a.min[1] <= b.max[1] && a.max[1] >= b.min[1];
}

bool World::pointInRect(const float p[3], const ScreenRect& r) {
    return p[0] >= r.min[0] && p[0] <= r.max[0] && p[1] >= r.min[1] && p[1] <= r.max[1];
}

// G_SegmentHitsRect (0x40ca10): a segment shorter than sqrt(0.5) pixel tests its end
// point (rcsl-builtins-semantics.md D9; which end is issue 030: we use `b`).
bool World::segmentHitsRect(const float a[3], const float b[3], const ScreenRect& r) {
    float dx = b[0] - a[0], dy = b[1] - a[1];
    if (dx * dx + dy * dy < 0.5f) return pointInRect(b, r);
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
    const bool bits = rules_->touchModeBits;
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
    // The first game: enemies for mode bit 0x1. The sequels (as2/rcsl-vm.delta.md, touch
    // dispatch): every mode but exactly 2 scans the list for (bit 0x1, class 2) or (bit 0x4,
    // class 5) candidates that are not dead.
    if (bits ? tm != TOUCH_BIT_PLAYER : (tm & 1) != 0) {
        for (int c = newest_; c != -1; c = ents_[static_cast<size_t>(c)].older) {
            if (c == idx) continue;
            const Entity& ce = ents_[static_cast<size_t>(c)];
            if (ce.rt & (RT_REMOVED | RT_HEALTH_FROZEN)) continue;
            if (!(ce.rt & RT_COLLIDABLE)) continue;
            if (bits) {
                const float cls = ce.f(F_CLASS);
                const bool accepted = ((tm & TOUCH_BIT_ENEMIES) && cls == kClassEnemy) ||
                                      ((tm & TOUCH_BIT_CIVILIAN) && cls == kClassCivilian);
                if (!accepted || ce.f(F_DEAD) != 0.0f) continue;
            } else if (ce.f(F_CLASS) != kClassEnemy) {
                continue;
            }
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
