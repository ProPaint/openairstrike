// The eyes of the world camera (as3d/world_render.h: EyeView, eyeViewMatrix, eyeProjection,
// eyeCullFrustum): plain math on WorldView, no GL, shared by every render backend. The
// formulas are documented with the declarations.
#include "as3d/world_render.h"

#include <cmath>

namespace as3d {

Mat4 eyeViewMatrix(const WorldView& view, const EyeView& eye) {
    if (eye.eyeOffset == 0.0f) return view.view;
    // T(-e, 0, 0) * view: the translation adds -e to the x row's translation term only.
    Mat4 v = view.view;
    v.at(3, 0) -= eye.eyeOffset;
    return v;
}

Vec3 eyePosition(const WorldView& view, const EyeView& eye) {
    if (eye.eyeOffset == 0.0f) return view.eye;
    const Vec3 right{view.view.at(0, 0), view.view.at(1, 0), view.view.at(2, 0)};
    return view.eye + right * eye.eyeOffset;
}

Mat4 eyeProjection(const WorldView& view, const EyeView& eye, float aspect) {
    // The symmetric case is worldViewOf's own call.
    Mat4 p = perspective(view.fovY, aspect, view.nearPlane, view.farPlane);
    if (eye.eyeOffset != 0.0f && eye.focalDistance > 0.0f) p.at(2, 0) = -p.at(0, 0) * eye.eyeOffset / eye.focalDistance;
    return p;
}

bool CullFrustum::sphereVisible(const Vec3& c, float r) const {
    for (const Vec4& p : planes) {
        if (p.x * c.x + p.y * c.y + p.z * c.z + p.w < -r) return false;
    }
    return true;
}

CullFrustum eyeCullFrustum(const WorldView& view, float aspect, float maxEyeOffset, float minFocalDistance) {
    const float tanY = std::tan(degToRad(view.fovY) * 0.5f);
    const float m = std::fabs(maxEyeOffset);
    const float t = tanY * aspect + (minFocalDistance > 0.0f ? m / minFocalDistance : 0.0f);
    // Eye-space planes (n . p + w >= 0 inside), camera looking down -z.
    const Vec4 eyePlanes[6] = {
        {1.0f, 0.0f, -t, m},                // left:   x + m + t d >= 0
        {-1.0f, 0.0f, -t, m},               // right: -x + m + t d >= 0
        {0.0f, 1.0f, -tanY, 0.0f},          // bottom
        {0.0f, -1.0f, -tanY, 0.0f},         // top
        {0.0f, 0.0f, -1.0f, -view.nearPlane}, // near: d - near >= 0
        {0.0f, 0.0f, 1.0f, view.farPlane},  // far:  far - d >= 0
    };
    // World space: p_eye = R p + T, so n . p_eye + w = (R^T n) . p + (n . T + w).
    const Mat4& v = view.view;
    CullFrustum f;
    for (int i = 0; i < 6; ++i) {
        const Vec4& e = eyePlanes[i];
        Vec3 n{v.at(0, 0) * e.x + v.at(0, 1) * e.y + v.at(0, 2) * e.z,
               v.at(1, 0) * e.x + v.at(1, 1) * e.y + v.at(1, 2) * e.z,
               v.at(2, 0) * e.x + v.at(2, 1) * e.y + v.at(2, 2) * e.z};
        float w = e.x * v.at(3, 0) + e.y * v.at(3, 1) + e.z * v.at(3, 2) + e.w;
        const float len = std::sqrt(e.x * e.x + e.y * e.y + e.z * e.z);
        f.planes[i] = Vec4{n.x / len, n.y / len, n.z / len, w / len};
    }
    return f;
}

} // namespace as3d
