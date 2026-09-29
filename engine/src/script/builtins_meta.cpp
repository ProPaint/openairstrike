// Static metadata for the 85 engine builtins, transcribed from
// docs/spec/rcsl-builtins-table.md ("Table"). Only what the VM needs to size and
// classify an auto-generated stub for a builtin a host does not implement: name, arity,
// return kind and done-flag kind. See as3d::script::BuiltinMeta in script.h.
#include "as3d/script.h"

#include <cstring>

namespace as3d::script {

namespace {

using RK = ReturnKind;
using DK = DoneFlagKind;

constexpr BuiltinMeta kTable[] = {
    {"debug", 0, RK::None, DK::None},
    {"RespawnPlayer", 0, RK::None, DK::None},
    {"EndLevel", 0, RK::None, DK::None},
    {"random", 0, RK::Float, DK::None},
    {"crandom", 0, RK::Float, DK::None},
    {"sin", 1, RK::Float, DK::None},
    {"cos", 1, RK::Float, DK::None},
    {"tan", 1, RK::Float, DK::None},
    {"atan", 1, RK::Float, DK::None},
    {"abs", 1, RK::Float, DK::None},
    {"min", 2, RK::Float, DK::None},
    {"max", 2, RK::Float, DK::None},
    {"lerp", 3, RK::Float, DK::None},
    {"vec_copy", 2, RK::None, DK::None},
    {"vec_add", 3, RK::None, DK::None},
    {"vec_sub", 3, RK::None, DK::None},
    {"vec_ma", 4, RK::None, DK::None},
    {"vec_length", 1, RK::Float, DK::None},
    {"vec_norm", 1, RK::Float, DK::None},
    {"vec_scale", 2, RK::None, DK::None},
    {"vec_setlen", 2, RK::None, DK::None},
    {"vec_toyaw", 1, RK::Float, DK::None},
    {"vec_toangles", 2, RK::None, DK::None},
    {"ClearAxis", 1, RK::None, DK::None},
    {"AnglesToAxis", 2, RK::None, DK::None},
    {"create", 2, RK::Entity, DK::None},
    {"remove", 1, RK::None, DK::None},
    {"activate", 1, RK::None, DK::None},
    {"deactivate", 1, RK::None, DK::None},
    {"getentity", 1, RK::Entity, DK::None},
    {"setskin", 2, RK::None, DK::None},
    {"callback", 4, RK::None, DK::None},
    {"AttachEntity", 4, RK::None, DK::None},
    {"AttachActivate", 1, RK::None, DK::None},
    {"AttachDeactivate", 1, RK::None, DK::None},
    {"AttachCallback", 4, RK::None, DK::None},
    {"ParentCallback", 3, RK::None, DK::None},
    {"move", 1, RK::None, DK::None},
    {"movex", 1, RK::None, DK::None},
    {"movey", 1, RK::None, DK::None},
    {"movez", 1, RK::None, DK::None},
    {"rotate", 1, RK::None, DK::None},
    {"rotatex", 1, RK::None, DK::None},
    {"rotatey", 1, RK::None, DK::None},
    {"rotatez", 1, RK::None, DK::None},
    {"sleep", 0, RK::None, DK::Zero},
    {"Shoot", 3, RK::None, DK::None},
    {"Damage", 2, RK::None, DK::None},
    {"RadialDamage", 3, RK::None, DK::None},
    {"RotateTo", 2, RK::None, DK::Computed},
    {"lRotateTo", 2, RK::None, DK::Computed},
    {"RotateToClamp", 4, RK::None, DK::Computed},
    {"lRotateToClamp", 4, RK::None, DK::Computed},
    {"MoveToNextWP", 1, RK::None, DK::Computed},
    {"lMoveToNextWP", 1, RK::None, DK::Computed},
    {"RotateToNextWP", 0, RK::None, DK::Computed},
    {"lRotateToNextWP", 0, RK::None, DK::Computed},
    {"GetWaypointDelay", 1, RK::Float, DK::None},
    {"TerrainHeight", 2, RK::Float, DK::None},
    {"PlaceLight", 3, RK::None, DK::None},
    {"CameraQuake", 1, RK::None, DK::None},
    {"LockTarget", 0, RK::Entity, DK::None},
    {"IsValidTarget", 1, RK::IntAsFloat, DK::None},
    {"StartSound", 1, RK::None, DK::None},
    {"StartLoopingSound", 1, RK::None, DK::None},
    {"StopLoopingSound", 0, RK::None, DK::None},
    {"TraceLine", 3, RK::Entity, DK::None},
    {"TraceLineDamage", 3, RK::None, DK::None},
    {"Lightning", 0, RK::None, DK::None},
    {"PushPlayer", 0, RK::None, DK::None},
    {"G_AddPowerUp", 2, RK::None, DK::None},
    {"G_UsePowerUp", 0, RK::IntAsFloat, DK::None},
    {"G_GetPowerUp", 0, RK::IntAsFloat, DK::None},
    {"G_AddMissiles", 2, RK::None, DK::None},
    {"G_UseMissile", 0, RK::IntAsFloat, DK::None},
    {"G_GetMissiles", 0, RK::IntAsFloat, DK::None},
    {"G_GetUpgrade", 1, RK::IntAsFloat, DK::None},
    {"G_SetUpgrade", 2, RK::None, DK::None},
    {"ShowTutorialHint", 1, RK::None, DK::None},
    {"PlayerFreezeHealth", 1, RK::None, DK::None},
    {"PlayerDisableAction", 1, RK::None, DK::None},
    {"FreezeHealth", 2, RK::None, DK::None},
    {"SetFlag", 2, RK::IntAsFloat, DK::None},
    {"ClearFlag", 2, RK::IntAsFloat, DK::None},
    {"SetModel", 1, RK::None, DK::None},
};

constexpr size_t kTableCount = sizeof(kTable) / sizeof(kTable[0]);

} // namespace

const BuiltinMeta* findBuiltinMeta(const char* name) {
    if (!name) return nullptr;
    for (const BuiltinMeta& m : kTable) {
        if (std::strcmp(m.name, name) == 0) return &m;
    }
    return nullptr;
}

size_t builtinMetaCount() { return kTableCount; }

const BuiltinMeta& builtinMetaAt(size_t index) { return kTable[index]; }

// -- BuiltinArgs convenience methods --

float BuiltinArgs::f32(int index) const {
    u32 b = bits(index);
    float f;
    std::memcpy(&f, &b, sizeof f);
    return f;
}

void BuiltinArgs::setReturnFloat(float v) {
    u32 b;
    std::memcpy(&b, &v, sizeof b);
    setReturnBits(b);
}

// -- BuiltinReport --

void BuiltinReport::noteCall(const char* name, BuiltinStatus status) {
    for (auto& kv : entries_) {
        if (kv.first == name) {
            kv.second.calls++;
            kv.second.status = status;
            return;
        }
    }
    // First time this name is ever recorded by this report.
    entries_.push_back({std::string(name), Entry{1, status}});
    if (status == BuiltinStatus::Stub) {
        AS3D_WARN("script: builtin '%s' is not implemented by the host; using a stub", name);
    }
}

std::vector<BuiltinReport::Row> BuiltinReport::rows() const {
    std::vector<Row> out;
    out.reserve(entries_.size());
    for (const auto& kv : entries_) out.push_back({kv.first, kv.second.calls, kv.second.status});
    return out;
}

} // namespace as3d::script
