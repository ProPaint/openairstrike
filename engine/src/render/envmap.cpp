// Environment map formulas, docs/spec/render-pipeline.md 4.3.
#include "as3d/envmap.h"

#include <cmath>

namespace as3d {

EnvMapMode envMapModeFromInt(int value) {
    switch (value) {
        case 1: return EnvMapMode::Glitter;
        case 2: return EnvMapMode::Chrome;
        case 3: return EnvMapMode::Quad;
        default: return EnvMapMode::None;
    }
}

Vec3 foldEnvNormal(const Vec3& n) { return {std::fabs(n.x), std::fabs(n.y), std::fabs(n.z)}; }

Mat3 envNormalMatrix(const Mat4& view, const Mat4& model) {
    Mat4 mv = view * model;
    return transpose(inverse(mat3FromMat4(mv)));
}

Vec2 sphereMapUv(const Vec3& eyePosition, const Vec3& eyeNormal) {
    Vec3 u = normalize(eyePosition);
    Vec3 r = u - eyeNormal * (2.0f * dot(eyeNormal, u));
    float m = 2.0f * std::sqrt(r.x * r.x + r.y * r.y + (r.z + 1.0f) * (r.z + 1.0f));
    if (m < 1e-9f) return {0.5f, 0.5f};
    return {r.x / m + 0.5f, r.y / m + 0.5f};
}

Vec2 rotateQuadEnvUv(const Vec2& st, float timeSeconds) {
    float a = degToRad(timeSeconds * kEnvQuadDegreesPerSecond);
    float c = std::cos(a), s = std::sin(a);
    return {c * st.x - s * st.y, s * st.x + c * st.y};
}

Vec4 combineEnvMap(EnvMapMode mode, const Vec4& skin, const Vec4& env, const Vec4& c) {
    switch (mode) {
        case EnvMapMode::Glitter:
            return {skin.x * c.x + env.x, skin.y * c.y + env.y, skin.z * c.z + env.z, skin.w * c.w * env.w};
        case EnvMapMode::Chrome: {
            float k = 1.0f - skin.w;
            return {skin.x * skin.w + env.x * c.x * k, skin.y * skin.w + env.y * c.y * k,
                    skin.z * skin.w + env.z * c.z * k, env.w * c.w};
        }
        case EnvMapMode::Quad:
            return {env.x * c.x, env.y * c.y, env.z * c.z, env.w * c.w};
        case EnvMapMode::None:
            break;
    }
    return {skin.x * c.x, skin.y * c.y, skin.z * c.z, skin.w * c.w};
}

} // namespace as3d
