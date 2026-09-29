// Implements as3d::Camera's factory functions (as3d/scene.h). No GL, no defs.h -- just
// as3d/math.h.
#include "as3d/scene.h"

#include <algorithm>
#include <cmath>

namespace as3d {

Camera Camera::framing(const Vec3& boundsMin, const Vec3& boundsMax, float yawDeg, float pitchDeg,
                        float distFactor, float aspect) {
    Camera cam;
    cam.aspect = aspect;
    Vec3 center = (boundsMin + boundsMax) * 0.5f;
    Vec3 extent = boundsMax - boundsMin;
    float radius = 0.5f * length(extent);
    if (radius < 0.001f) radius = 1.0f;
    cam.target = center;

    // Distance so the bounding sphere fits inside the vertical field of view with a
    // ~25% margin, then scaled by distFactor (the viewer's own "--dist" multiplier).
    float distance = (radius / std::sin(degToRad(cam.fovYDegrees * 0.5f))) * 1.25f * std::max(0.01f, distFactor);
    float yaw = degToRad(yawDeg);
    float pitch = degToRad(pitchDeg);
    Vec3 dir{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
    cam.eye = center + dir * distance;
    cam.nearPlane = std::max(0.01f, distance * 0.01f);
    cam.farPlane = distance * 10.0f + radius * 4.0f;
    return cam;
}

Camera Camera::topDown(const Vec3& target, float height, float tiltDeg, float aspect) {
    Camera cam;
    cam.aspect = aspect;
    cam.target = target;
    float t = degToRad(tiltDeg);
    // Above and behind (-Y) the target, tilted forward off straight-down so the view
    // also looks slightly "ahead" along +Y -- the direction the player's helicopter
    // flies (docs/graphics.md, "Top-down camera").
    Vec3 offset{0.0f, -height * std::sin(t), height * std::cos(t)};
    cam.eye = target + offset;
    cam.nearPlane = std::max(0.1f, height * 0.02f);
    cam.farPlane = height * 20.0f;
    return cam;
}

} // namespace as3d
