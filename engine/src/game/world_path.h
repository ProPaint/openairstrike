// Waypoint paths as the entity movement builtins use them (hmap.md "Waypoint paths";
// rcsl-builtins-semantics.md MoveToNextWP / RotateToNextWP / GetWaypointDelay).
//
// A path of K >= 2 waypoints is a chain of cubic Bezier segments (K - 1 open, K looping).
// Preprocessing walks each segment in parameter steps of 0.001 and records a sample every
// 8 world units of arc length: the curve parameter (segment + t), the heading, and the
// heading change to the next sample (the "bank" value). Headings use the field-16
// convention (rcsl-builtins-semantics.md D6/D7: atan2 in degrees, +360 when negative,
// minus 90, so a heading points the model's nose along the travel direction).
#pragma once

#include <vector>

#include "as3d/level.h"
#include "as3d/vec.h"

namespace as3d {

class GamePath {
public:
    bool build(const Placement& placement);

    int sampleCount() const { return static_cast<int>(param_.size()); }
    int waypointCount() const { return static_cast<int>(points_.size()); }
    bool loops() const { return loops_; }

    struct Eval {
        Vec2 pos;
        float heading = 0.0f;
        float bank = 0.0f;
        int segment = 0;
    };
    // Evaluates at arc length s (clamped to the sample table).
    Eval evaluate(float s) const;
    // Segment index (waypoint that starts the current segment) at arc length s.
    int segmentAt(float s) const;
    // Delay / world position of waypoint i mod K (negative i wrapped into [0, K)).
    float delay(int i) const;
    Vec2 waypointPos(int i) const;
    float headingAtStart() const { return heading_.empty() ? 0.0f : heading_[0]; }

private:
    Vec2 point(int seg, float t) const;
    Vec2 tangent(int seg, float t) const;
    float paramAt(float s, int& k, float& f) const;

    struct Segment {
        Vec2 p0, p1, p2, p3;
    };
    std::vector<Segment> segs_;
    std::vector<Vec2> points_;
    std::vector<float> delays_;
    std::vector<float> param_;   // T[k]
    std::vector<float> heading_; // H[k]
    std::vector<float> turn_;    // H[k+1] - H[k], wrapped; 0 for the last sample
    bool loops_ = false;
};

// Field-16 heading of a direction (D6 yaw): atan2 in degrees, +360 if negative, - 90.
float headingOfDirection(float dx, float dy);

} // namespace as3d
