// Keyword -> enum-value tables shared by the objects/weapons/particles/level
// loaders. Mirrors tools/ref/defs.py exactly. Private to as3d_game.
// See docs/spec/obj.md, docs/spec/ps.md.
#pragma once

#include <cstring>

#include "as3d/defs.h"

namespace as3d {
namespace defs_detail {

inline bool eq(const std::string& s, const char* lit) { return s == lit; }

inline ObjectType parseObjectType(const std::string& s) {
    if (eq(s, "TYPE_MODEL")) return ObjectType::Model;
    if (eq(s, "TYPE_SPRITE")) return ObjectType::Sprite;
    if (eq(s, "TYPE_MARK")) return ObjectType::Mark;
    if (eq(s, "TYPE_HSPRITE")) return ObjectType::HSprite;
    if (eq(s, "TYPE_VSPRITE")) return ObjectType::VSprite;
    return ObjectType::Model;
}

inline BlendMode parseBlendMode(const std::string& s) {
    if (eq(s, "BLEND_ALPHA")) return BlendMode::Alpha;
    if (eq(s, "BLEND_ADD")) return BlendMode::Add;
    if (eq(s, "BLEND_FILTER")) return BlendMode::Filter;
    return BlendMode::None;
}

inline EnvMode parseEnvMode(const std::string& s) {
    if (eq(s, "ENV_GLITTER")) return EnvMode::Glitter;
    if (eq(s, "ENV_CHROME")) return EnvMode::Chrome;
    if (eq(s, "ENV_QUAD")) return EnvMode::Quad;
    return EnvMode::None;
}

inline ShadowMode parseShadowMode(const std::string& s) {
    if (eq(s, "SHADOW_PROJECTED")) return ShadowMode::Projected;
    if (eq(s, "SHADOW_PROJECTED_LOW")) return ShadowMode::ProjectedLow;
    if (eq(s, "SHADOW_PROJECTED_HIGH")) return ShadowMode::ProjectedHigh;
    if (eq(s, "SHADOW_PLANAR")) return ShadowMode::Planar;
    if (eq(s, "SHADOW_PLANAR_LOW")) return ShadowMode::PlanarLow;
    if (eq(s, "SHADOW_PLANAR_HIGH")) return ShadowMode::PlanarHigh;
    if (eq(s, "SHADOW_PLANAR_PROJECTED")) return ShadowMode::PlanarProjected;
    if (eq(s, "SHADOW_PLANAR_PROJECTED_LOW")) return ShadowMode::PlanarProjectedLow;
    if (eq(s, "SHADOW_PLANAR_PROJECTED_HIGH")) return ShadowMode::PlanarProjectedHigh;
    return ShadowMode::None;
}

inline SortMode parseSortMode(const std::string& s) {
    if (eq(s, "SORT_OPAQUE")) return SortMode::Opaque;
    if (eq(s, "SORT_TRANS")) return SortMode::Trans;
    if (eq(s, "SORT_EFFECT")) return SortMode::Effect;
    return SortMode::Opaque;
}

inline TouchMode parseTouchMode(const std::string& s) {
    if (eq(s, "TOUCH_ENEMIES")) return TouchMode::Enemies;
    if (eq(s, "TOUCH_PLAYER")) return TouchMode::Player;
    if (eq(s, "TOUCH_ALL")) return TouchMode::All;
    return TouchMode::None;
}

inline u32 parseObjRFlag(const std::string& s) {
    if (eq(s, "RF_NOLIGHTING")) return RF_NOLIGHTING;
    if (eq(s, "RF_NODLIGHT")) return RF_NODLIGHT;
    if (eq(s, "RF_NOCULLING")) return RF_NOCULLING;
    if (eq(s, "RF_NODEPTHTEST")) return RF_NODEPTHTEST;
    if (eq(s, "RF_NODEPTHWRITE")) return RF_NODEPTHWRITE;
    if (eq(s, "RF_BANNER")) return RF_BANNER;
    return 0;
}

inline u32 parsePsRFlag(const std::string& s) {
    if (eq(s, "RF_NOLIGHTING")) return RF_NOLIGHTING;
    if (eq(s, "RF_NOCULLING")) return RF_NOCULLING;
    if (eq(s, "RF_NODEPTHTEST")) return RF_NODEPTHTEST;
    if (eq(s, "RF_NODEPTHWRITE")) return RF_NODEPTHWRITE;
    return 0;
}

inline u32 parseFlag(const std::string& s) {
    if (eq(s, "FL_ONGROUND")) return FL_ONGROUND;
    if (eq(s, "FL_ONGROUND_NORMAL")) return FL_ONGROUND | FL_ONGROUND_NORMAL_EXTRA;
    if (eq(s, "FL_ONWATER")) return FL_ONWATER;
    if (eq(s, "FL_NODRAW")) return FL_NODRAW;
    if (eq(s, "FL_TEMPORARY")) return FL_TEMPORARY;
    if (eq(s, "FL_NONTARGET")) return FL_NONTARGET;
    if (eq(s, "FL_POINT_COLLISION")) return FL_POINT_COLLISION;
    return 0;
}

// Sequel-only "flag" and "touch" values (see SequelFlagBits, SequelTouchBits in as3d/defs.h):
// 0 for every other spelling.
inline u32 parseSequelFlag(const std::string& s) {
    if (eq(s, "FL_ONWATER_NORMAL")) return SEQ_FL_ONWATER_NORMAL;
    if (eq(s, "FL_ONWATER_FLAT")) return SEQ_FL_ONWATER_FLAT;
    return 0;
}

inline u32 parseSequelTouch(const std::string& s) {
    if (eq(s, "TOUCH_CIVILIAN")) return SEQ_TOUCH_CIVILIAN;
    return 0;
}

inline CoordMode parseCoordMode(const std::string& s) {
    if (eq(s, "COORD_DECART")) return CoordMode::Decart;
    if (eq(s, "COORD_CILINDER")) return CoordMode::Cilinder;
    if (eq(s, "COORD_SPHERE")) return CoordMode::Sphere;
    return CoordMode::Decart;
}

inline DrawMode parseDrawMode(const std::string& s) {
    if (eq(s, "DRAW_VERT")) return DrawMode::Vert;
    if (eq(s, "DRAW_HORIZ")) return DrawMode::Horiz;
    return DrawMode::None;
}

inline EmitMode parseEmitMode(const std::string& s) {
    if (eq(s, "EMIT_ONCE")) return EmitMode::Once;
    if (eq(s, "EMIT_DURATION")) return EmitMode::Duration;
    return EmitMode::None;
}

inline AnimMode parseAnimMode(const std::string& s) {
    if (eq(s, "ANIM_LINEAR")) return AnimMode::Linear;
    if (eq(s, "ANIM_NORMAL")) return AnimMode::Normal;
    if (eq(s, "ANIM_LOOP")) return AnimMode::Loop;
    return AnimMode::None;
}

inline FadeMode parseFadeMode(const std::string& s) {
    if (eq(s, "FADE_LINEAR")) return FadeMode::Linear;
    if (eq(s, "FADE_EXP")) return FadeMode::Exp;
    return FadeMode::None;
}

} // namespace defs_detail
} // namespace as3d
