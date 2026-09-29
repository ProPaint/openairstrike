// Enemy and boss health bars (docs/spec/engine-behaviour.md 6.5, frontend.md 4.2): two
// billboard sprites, `hbar_empty` and `hbar_full` of objects\misc.obj, queued in the entity
// pass right after the entity's own records. There is no 2D boss bar in the original: bosses
// and big enemies use the same 3D bar. GL free; the sprites are drawn by SpriteRenderer.
//
// The split of the bar between the two sprites is not in the specs (docs/spec/issues/111):
// the empty bar is drawn whole, the full bar on top of it cut to the health fraction, both
// in position and in texture s.
#pragma once

#include "as3d/sprite_render.h"
#include "as3d/world.h"

namespace as3d {

constexpr float kHealthBarMinMaxHealth = 150.0f; // shown only when maximum health > 150
constexpr float kHealthBarShowSeconds = 1.0f;    // for 1 s after the last damage

// The rectangle and texture of one bar sprite (the object's `min` / `max`, `skin`, `rflag`).
struct HealthBarSpriteDef {
    float minX = -16.0f, minY = -1.3f, minS = 0.0f, minT = 0.0f;
    float maxX = 16.0f, maxY = 1.3f, maxS = 1.0f, maxT = 1.0f;
    const Texture2D* texture = nullptr;
    DecalBlend blend = DecalBlend::None;
    bool noDepthTest = true;
    bool noDepthWrite = false;
};

// Maximum health > 150, class 2 (enemy), damaged less than 1 s ago, not dead.
bool healthBarVisible(const Entity& e);
// Ground units (FL_ONGROUND): (x, y - 0.6 r, z + 1.2 model min z); others
// (x, y + 0.6 r, z + 1.2 model max z), from the entity's world position.
Vec3 healthBarPosition(const Entity& e);
// Health / maximum health, clamped to [0, 1].
float healthBarFraction(const Entity& e);
// The two sprites (empty, then full cut to the fraction); returns how many were written:
// 0 when the bar is not visible, 1 when the fill is empty.
int buildHealthBar(const Entity& e, const HealthBarSpriteDef& empty, const HealthBarSpriteDef& full,
                   SpriteInstance out[2]);

} // namespace as3d
