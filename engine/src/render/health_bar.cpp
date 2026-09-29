// Enemy health bars, docs/spec/engine-behaviour.md 6.5 (choices in docs/spec/issues/111).
#include "as3d/health_bar.h"

#include <algorithm>
#include <cmath>

namespace as3d {

namespace {
constexpr int kFlagOnGround = 0x01; // FL_ONGROUND (FL_ONGROUND_NORMAL includes it)

SpriteInstance spriteOf(const HealthBarSpriteDef& d, const Vec3& pos) {
    SpriteInstance s;
    s.kind = SpriteKind::Billboard;
    s.origin = pos;
    s.colour = {1.0f, 1.0f, 1.0f, 1.0f};
    s.minX = d.minX;
    s.minY = d.minY;
    s.minS = d.minS;
    s.minT = d.minT;
    s.maxX = d.maxX;
    s.maxY = d.maxY;
    s.maxS = d.maxS;
    s.maxT = d.maxT;
    s.texture = d.texture;
    s.blend = d.blend;
    s.noDepthTest = d.noDepthTest;
    s.noDepthWrite = d.noDepthWrite;
    return s;
}
} // namespace

bool healthBarVisible(const Entity& e) {
    return e.maxHealth > kHealthBarMinMaxHealth && e.f(F_CLASS) == kClassEnemy &&
           e.sinceDamage < kHealthBarShowSeconds && e.f(F_DEAD) == 0.0f;
}

Vec3 healthBarPosition(const Entity& e) {
    Vec3 o = e.v3(F_BASE_ORIGIN);
    if (e.flagBits() & kFlagOnGround) return {o.x, o.y - 0.6f * e.radius, o.z + 1.2f * e.boundsMin.z};
    return {o.x, o.y + 0.6f * e.radius, o.z + 1.2f * e.boundsMax.z};
}

float healthBarFraction(const Entity& e) {
    if (!(e.maxHealth > 0.0f)) return 0.0f;
    float f = e.f(F_HEALTH) / e.maxHealth;
    if (!std::isfinite(f)) return 0.0f;
    return std::min(std::max(f, 0.0f), 1.0f);
}

int buildHealthBar(const Entity& e, const HealthBarSpriteDef& empty, const HealthBarSpriteDef& full,
                   SpriteInstance out[2]) {
    if (!healthBarVisible(e)) return 0;
    const Vec3 pos = healthBarPosition(e);
    out[0] = spriteOf(empty, pos);
    const float f = healthBarFraction(e);
    if (!(f > 0.0f)) return 1;
    SpriteInstance s = spriteOf(full, pos);
    s.maxX = s.minX + (s.maxX - s.minX) * f;
    s.maxS = s.minS + (s.maxS - s.minS) * f;
    out[1] = s;
    return 2;
}

} // namespace as3d
