// GamePath: the per-placement waypoint path used by MoveToNextWP and friends.
#include "world_path.h"

#include <algorithm>
#include <cmath>

#include "as3d/world.h"
#include "world_internal.h"

namespace as3d {

namespace {
constexpr float kSampleSpacing = 8.0f;
constexpr int kStepsPerSegment = 1000;
} // namespace

float headingOfDirection(float dx, float dy) {
    if (dx == 0.0f && dy == 0.0f) return -90.0f;
    float yaw = std::atan2(dy, dx) * kRadToDeg;
    if (yaw < 0.0f) yaw += 360.0f;
    return yaw - 90.0f;
}

Vec2 GamePath::point(int seg, float t) const {
    const Segment& s = segs_[static_cast<size_t>(seg)];
    float u = 1.0f - t;
    float a = u * u * u, b = 3.0f * u * u * t, c = 3.0f * u * t * t, d = t * t * t;
    return {a * s.p0.x + b * s.p1.x + c * s.p2.x + d * s.p3.x, a * s.p0.y + b * s.p1.y + c * s.p2.y + d * s.p3.y};
}

Vec2 GamePath::tangent(int seg, float t) const {
    const Segment& s = segs_[static_cast<size_t>(seg)];
    float u = 1.0f - t;
    float a = 3.0f * u * u, b = 6.0f * u * t, c = 3.0f * t * t;
    Vec2 d{a * (s.p1.x - s.p0.x) + b * (s.p2.x - s.p1.x) + c * (s.p3.x - s.p2.x),
           a * (s.p1.y - s.p0.y) + b * (s.p2.y - s.p1.y) + c * (s.p3.y - s.p2.y)};
    if (std::fabs(d.x) + std::fabs(d.y) < 1e-6f) d = {s.p3.x - s.p0.x, s.p3.y - s.p0.y};
    return d;
}

bool GamePath::build(const Placement& pl) {
    segs_.clear();
    points_.clear();
    delays_.clear();
    param_.clear();
    heading_.clear();
    turn_.clear();
    size_t n = pl.waypoints.size();
    if (n < 2) return false;
    loops_ = pl.loopingPath();
    for (const Waypoint& w : pl.waypoints) {
        points_.push_back(w.worldPoint());
        delays_.push_back(w.delay);
    }
    size_t segs = loops_ ? n : n - 1;
    for (size_t i = 0; i < segs; ++i) {
        const Waypoint& a = pl.waypoints[i];
        const Waypoint& b = pl.waypoints[(i + 1) % n];
        segs_.push_back({a.worldPoint(), a.worldOutCtrl(), b.worldInCtrl(), b.worldPoint()});
    }
    float dist = 0.0f;
    Vec2 prev = point(0, 0.0f);
    auto record = [&](int seg, float t) {
        Vec2 d = tangent(seg, t);
        param_.push_back(static_cast<float>(seg) + t);
        heading_.push_back(headingOfDirection(d.x, d.y));
    };
    record(0, 0.0f);
    for (int s = 0; s < static_cast<int>(segs_.size()); ++s) {
        for (int k = 1; k <= kStepsPerSegment; ++k) {
            float t = static_cast<float>(k) / kStepsPerSegment;
            Vec2 p = point(s, t);
            float dx = p.x - prev.x, dy = p.y - prev.y;
            dist += std::sqrt(dx * dx + dy * dy);
            prev = p;
            if (dist >= kSampleSpacing * static_cast<float>(param_.size())) record(s, t);
            if (param_.size() > 200000) break; // bound: a path cannot be absurdly long
        }
    }
    turn_.resize(heading_.size(), 0.0f);
    for (size_t k = 0; k + 1 < heading_.size(); ++k) turn_[k] = wrap180(heading_[k + 1] - heading_[k]);
    return true;
}

float GamePath::paramAt(float s, int& k, float& f) const {
    int N = sampleCount();
    float n = s * 0.125f;
    if (!(n > 0.0f)) n = 0.0f;
    k = static_cast<int>(std::floor(n));
    f = n - static_cast<float>(k);
    if (k >= N - 1) {
        k = std::max(N - 2, 0);
        f = N >= 2 ? 1.0f : 0.0f;
    }
    if (N < 2) return param_.empty() ? 0.0f : param_[0];
    return param_[static_cast<size_t>(k)] + f * (param_[static_cast<size_t>(k) + 1] - param_[static_cast<size_t>(k)]);
}

int GamePath::segmentAt(float s) const {
    int k;
    float f;
    float p = paramAt(s, k, f);
    int seg = static_cast<int>(std::floor(p));
    return std::min(std::max(seg, 0), static_cast<int>(segs_.size()) - 1);
}

GamePath::Eval GamePath::evaluate(float s) const {
    Eval ev;
    if (segs_.empty() || param_.empty()) return ev;
    int k;
    float f;
    float p = paramAt(s, k, f);
    int seg = static_cast<int>(std::floor(p));
    float t = p - static_cast<float>(seg);
    int nseg = static_cast<int>(segs_.size());
    if (seg >= nseg) {
        seg = nseg - 1;
        t = 1.0f;
    }
    if (seg < 0) {
        seg = 0;
        t = 0.0f;
    }
    ev.pos = point(seg, t);
    ev.segment = seg;
    size_t k0 = static_cast<size_t>(k), k1 = std::min(k0 + 1, heading_.size() - 1);
    ev.heading = heading_[k0] + f * wrap180(heading_[k1] - heading_[k0]);
    ev.bank = turn_[k0] + f * (turn_[k1] - turn_[k0]);
    return ev;
}

float GamePath::delay(int i) const {
    if (delays_.empty()) return 0.0f;
    int k = static_cast<int>(delays_.size());
    return delays_[static_cast<size_t>(((i % k) + k) % k)];
}

Vec2 GamePath::waypointPos(int i) const {
    if (points_.empty()) return {};
    int k = static_cast<int>(points_.size());
    return points_[static_cast<size_t>(((i % k) + k) % k)];
}

const GamePath* World::pathOfPlacement(size_t i) const {
    return i < gamePaths_.size() ? gamePaths_[i].get() : nullptr;
}

} // namespace as3d
