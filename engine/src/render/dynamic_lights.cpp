// Dynamic light list and formulas, docs/spec/render-pipeline.md 2.4.
#include "as3d/dynamic_lights.h"

#include <algorithm>
#include <cmath>

namespace as3d {

DynamicLight DynamicLight::point(const Vec3& position, const Vec3& colour, float radius) {
    DynamicLight l;
    l.position = position;
    l.colour = colour;
    l.radius = radius;
    return l;
}

DynamicLight DynamicLight::spotLight(const Vec3& position, const Vec3& colour, float radius, const Vec3& direction,
                                     float angleDegrees) {
    DynamicLight l = point(position, colour, radius);
    l.spot = true;
    l.direction = normalize(direction);
    l.cosOuter = std::cos(degToRad(0.5f * angleDegrees));
    l.cosInner = std::cos(degToRad(0.4f * angleDegrees));
    return l;
}

bool DynamicLightList::add(const DynamicLight& light) {
    if (lights_.size() >= static_cast<size_t>(kMaxDynamicLights)) return false;
    lights_.push_back(light);
    return true;
}

bool evaluateLight(const DynamicLight& light, const Vec3& target, Vec3& toLight, float& att, float& spot) {
    if (light.radius <= 0.0f) return false;
    Vec3 v = light.position - target;
    float dist = length(v);
    if (dist > light.radius) return false;
    if (dist != 0.0f) v = v * (1.0f / dist);
    spot = 1.0f;
    if (light.spot) {
        float c = dot(-v, light.direction);
        if (c < light.cosOuter) return false;
        if (c < light.cosInner) spot = (c - light.cosOuter) / (light.cosInner - light.cosOuter);
    }
    att = (light.radius - dist) / light.radius;
    toLight = v;
    return true;
}

void accumulateModelLights(const DynamicLight* lights, size_t count, const Vec3& origin, const Vec3 axes[3],
                           Vec3 cube[6]) {
    for (size_t n = 0; n < count; n++) {
        Vec3 v;
        float att = 0.0f, spot = 1.0f;
        if (!evaluateLight(lights[n], origin, v, att, spot)) continue;
        for (int i = 0; i < 3; i++) {
            float d = dot(axes[i], v);
            float k = (0.7f * std::fabs(d) + 0.3f) * att * spot;
            Vec3& slot = d >= 0.0f ? cube[i] : cube[i + 3];
            slot = slot + lights[n].colour * k;
        }
    }
}

float terrainLightFactor(const DynamicLight& light, const Vec3& position, const Vec3& normal) {
    Vec3 v;
    float att = 0.0f, spot = 1.0f;
    if (!evaluateLight(light, position, v, att, spot)) return 0.0f;
    float nd = dot(normal, v);
    if (nd <= 0.0f) return 0.0f;
    return (0.7f * nd + 0.3f) * att * spot;
}

Vec3 lightTerrainVertex(const DynamicLight* lights, size_t count, const Vec3& position, const Vec3& normal,
                        const Vec3& baked) {
    Vec3 c = baked;
    for (size_t n = 0; n < count; n++) c = c + lights[n].colour * terrainLightFactor(lights[n], position, normal);
    return {std::min(c.x, 1.0f), std::min(c.y, 1.0f), std::min(c.z, 1.0f)};
}

bool lightMayBeVisible(const DynamicLight& light, const Mat4& viewProj) {
    // Gribb-Hartmann planes from the rows of the clip matrix (column-major storage).
    float rows[4][4];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) rows[r][c] = viewProj.at(c, r);
    for (int a = 0; a < 3; a++) {
        for (int sign = -1; sign <= 1; sign += 2) {
            float p[4];
            for (int i = 0; i < 4; i++) p[i] = rows[3][i] + static_cast<float>(sign) * rows[a][i];
            float len = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
            if (len < 1e-9f) continue;
            float d = (p[0] * light.position.x + p[1] * light.position.y + p[2] * light.position.z + p[3]) / len;
            if (d < -light.radius) return false;
        }
    }
    return true;
}

} // namespace as3d
