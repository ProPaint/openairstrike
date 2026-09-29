// Waypoint path sampling (docs/spec/hmap.md "Waypoint paths").
#include <algorithm>
#include <cmath>

#include "as3d/math.h"
#include "as3d/terrain.h"

namespace as3d {

namespace {
constexpr float kSampleSpacing = 8.0f; // world units of arc length between table samples
constexpr int kFineSteps = 1000;       // parameter steps of 0.001 per segment

Vec2 bez(const Vec2& p0, const Vec2& p1, const Vec2& p2, const Vec2& p3, float t) {
    float u = 1 - t;
    float a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
    return {a * p0.x + b * p1.x + c * p2.x + d * p3.x, a * p0.y + b * p1.y + c * p2.y + d * p3.y};
}
Vec2 bezTangent(const Vec2& p0, const Vec2& p1, const Vec2& p2, const Vec2& p3, float t) {
    float u = 1 - t;
    float a = 3 * u * u, b = 6 * u * t, c = 3 * t * t;
    Vec2 d{a * (p1.x - p0.x) + b * (p2.x - p1.x) + c * (p3.x - p2.x),
           a * (p1.y - p0.y) + b * (p2.y - p1.y) + c * (p3.y - p2.y)};
    if (std::fabs(d.x) + std::fabs(d.y) < 1e-6f) d = {p3.x - p0.x, p3.y - p0.y};
    return d;
}
// Heading convention (internally consistent; the spec does not pin the absolute
// yaw frame): degrees, 0 = travelling along +y, 90 = along +x.
float headingOf(const Vec2& d) { return radToDeg(std::atan2(d.x, d.y)); }
float lerpAngle(float a, float b, float t) {
    float d = std::fmod(b - a, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    return a + d * t;
}
} // namespace

bool WaypointPath::build(const Placement& pl) {
    samples_.clear();
    waypointArc_.clear();
    waypointDelay_.clear();
    loops_ = false;
    waypointCount_ = pl.waypoints.size();
    if (pl.waypoints.size() < 2) return false;
    size_t n = pl.waypoints.size();
    loops_ = pl.loopingPath();
    size_t segs = loops_ ? n : n - 1;
    for (const Waypoint& w : pl.waypoints) waypointDelay_.push_back(w.delay);

    float dist = 0.0f, sinceSample = 0.0f;
    Vec2 prev{};
    for (size_t s = 0; s < segs; s++) {
        const Waypoint& a = pl.waypoints[s];
        const Waypoint& b = pl.waypoints[(s + 1) % n];
        Vec2 p0 = a.worldPoint(), p1 = a.worldOutCtrl(), p2 = b.worldInCtrl(), p3 = b.worldPoint();
        waypointArc_.push_back(dist);
        for (int k = 0; k <= kFineSteps; k++) {
            if (s > 0 && k == 0) continue; // same point as the end of the previous segment
            float t = static_cast<float>(k) / kFineSteps;
            Vec2 p = bez(p0, p1, p2, p3, t);
            bool first = (s == 0 && k == 0);
            if (!first) {
                float dx = p.x - prev.x, dy = p.y - prev.y;
                float l = std::sqrt(dx * dx + dy * dy);
                dist += l;
                sinceSample += l;
            }
            prev = p;
            bool last = (s == segs - 1 && k == kFineSteps);
            if (first || last || sinceSample >= kSampleSpacing) {
                DenseSample ds;
                ds.distance = dist;
                ds.position = p;
                ds.headingDegrees = headingOf(bezTangent(p0, p1, p2, p3, t));
                samples_.push_back(ds);
                sinceSample = 0.0f;
            }
        }
    }
    return true;
}

WaypointPath::Sample WaypointPath::sampleAtDistance(float s) const {
    Sample out;
    if (samples_.empty()) return out;
    float len = length();
    if (loops_ && len > 0) {
        s = std::fmod(s, len);
        if (s < 0) s += len;
    } else {
        s = std::min(std::max(s, 0.0f), len);
    }
    auto it = std::upper_bound(samples_.begin(), samples_.end(), s,
                               [](float v, const DenseSample& d) { return v < d.distance; });
    if (it == samples_.begin()) {
        out.position = samples_.front().position;
        out.headingDegrees = samples_.front().headingDegrees;
        return out;
    }
    if (it == samples_.end()) {
        out.position = samples_.back().position;
        out.headingDegrees = samples_.back().headingDegrees;
        return out;
    }
    const DenseSample& b = *it;
    const DenseSample& a = *(it - 1);
    float span = b.distance - a.distance;
    float t = span > 1e-6f ? (s - a.distance) / span : 0.0f;
    out.position = {a.position.x + (b.position.x - a.position.x) * t, a.position.y + (b.position.y - a.position.y) * t};
    out.headingDegrees = lerpAngle(a.headingDegrees, b.headingDegrees, t);
    return out;
}

float WaypointPath::waypointArcLength(int i) const {
    if (waypointArc_.empty() || i < 0) return 0.0f;
    if (static_cast<size_t>(i) < waypointArc_.size()) return waypointArc_[static_cast<size_t>(i)];
    return length(); // last waypoint of an open path
}

float WaypointPath::waypointDelay(int i) const {
    if (waypointDelay_.empty()) return 0.0f;
    long k = static_cast<long>(waypointDelay_.size());
    long m = ((i % k) + k) % k;
    return waypointDelay_[static_cast<size_t>(m)];
}

WaypointPath::AdvanceResult WaypointPath::advance(float& cursor, float distance) const {
    AdvanceResult r;
    if (samples_.empty()) return r;
    float len = length();
    float old = cursor, now = cursor + distance;
    int n = waypointCount();
    if (loops_) {
        // boundaries crossed: waypoints 1..K-1, then the wrap to waypoint 0 at `len`
        for (int i = 1; i <= n && !r.waypointPassed; i++) {
            float arc = (i < n) ? waypointArc_[static_cast<size_t>(i)] : len;
            if (arc > old && arc <= now) {
                r.waypointPassed = true;
                r.passedWaypointIndex = i % n;
            }
        }
        if (len > 0 && now >= len) now = std::fmod(now, len);
    } else {
        for (int i = 1; i < n && !r.waypointPassed; i++) {
            float arc = (i < n - 1) ? waypointArc_[static_cast<size_t>(i)] : len;
            if (arc > old && arc <= now) {
                r.waypointPassed = true;
                r.passedWaypointIndex = i;
            }
        }
        if (now >= len) {
            now = len;
            if (old < len) {
                r.finished = true;
                if (!r.waypointPassed) {
                    r.waypointPassed = true;
                    r.passedWaypointIndex = n - 1;
                }
            }
        }
    }
    cursor = now;
    Sample s = sampleAtDistance(now);
    r.position = s.position;
    r.headingDegrees = s.headingDegrees;
    return r;
}

} // namespace as3d
