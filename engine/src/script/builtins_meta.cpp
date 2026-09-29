// Static metadata for the engine builtins of each game: the 85 of the first game, transcribed
// from docs/spec/rcsl-builtins-table.md ("Table"), and the 101 of the sequels. Only what the VM needs to size and
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

// AirStrike 2's 101 builtins in its table order (docs/spec/as2/rcsl-builtins-table.delta.md,
// transcribed from testdata/golden/as2/rcsl_builtins.json). Gulf Thunder has the same table
// (the delta's call-site check runs its scripts against it).
constexpr BuiltinMeta kTableV251[] = {
    {"debug", 0, RK::None, DK::None},
    {"RespawnPlayer", 0, RK::None, DK::None},
    {"EndLevel", 0, RK::None, DK::None},
    {"GameOver", 0, RK::None, DK::None},
    {"random", 0, RK::Float, DK::None},
    {"crandom", 0, RK::Float, DK::None},
    {"sin", 1, RK::Float, DK::None},
    {"cos", 1, RK::Float, DK::None},
    {"tan", 1, RK::Float, DK::None},
    {"atan", 1, RK::Float, DK::None},
    {"atan2", 2, RK::Float, DK::None},
    {"abs", 1, RK::Float, DK::None},
    {"min", 2, RK::Float, DK::None},
    {"max", 2, RK::Float, DK::None},
    {"lerp", 3, RK::Float, DK::None},
    {"copysign", 2, RK::Float, DK::None},
    {"floor", 1, RK::Float, DK::None},
    {"floor2", 2, RK::Float, DK::None},
    {"fmod", 2, RK::Float, DK::None},
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
    {"DetachEntity", 1, RK::None, DK::None},
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
    {"RadialDamagePlayer", 3, RK::None, DK::None},
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
    {"WaterHeight", 2, RK::Float, DK::None},
    {"PlaceLight", 3, RK::None, DK::None},
    {"CameraQuake", 1, RK::None, DK::None},
    {"LockTarget", 0, RK::Entity, DK::None},
    {"IsValidTarget", 1, RK::IntAsFloat, DK::None},
    {"StartSound", 1, RK::None, DK::None},
    {"StartLoopingSound", 1, RK::None, DK::None},
    {"StopLoopingSound", 0, RK::None, DK::None},
    {"TraceLine", 3, RK::Entity, DK::None},
    {"TraceLineDamage", 3, RK::None, DK::None},
    {"Lightning", 1, RK::None, DK::None},
    {"PushPlayer", 0, RK::None, DK::None},
    {"G_AddPowerUp", 2, RK::None, DK::None},
    {"G_UsePowerUp", 0, RK::IntAsFloat, DK::None},
    {"G_GetPowerUp", 0, RK::IntAsFloat, DK::None},
    {"G_SetPowerUpCount", 2, RK::None, DK::None},
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
    {"TerraMorph", 2, RK::None, DK::None},
    {"IsMultiplayer", 0, RK::IntAsFloat, DK::None},
    {"IsPlayerInGame", 1, RK::IntAsFloat, DK::None},
    {"GetMapPosOfs", 0, RK::Float, DK::None},
    {"GetPlayerAccel", 1, RK::None, DK::None},
    {"GetPlayersDistance", 0, RK::Float, DK::None},
};

constexpr size_t kTableV251Count = sizeof(kTableV251) / sizeof(kTableV251[0]);
static_assert(kTableV251Count == 101, "as2 builtin table");

} // namespace

BuiltinMetaTable builtinMetaTable(const char* builtinSet) {
    if (builtinSet && (std::strcmp(builtinSet, "v251") == 0 || std::strcmp(builtinSet, "v271") == 0)) {
        return {kTableV251, kTableV251Count};
    }
    return {kTable, kTableCount};
}

const BuiltinMeta* findBuiltinMeta(const char* builtinSet, const char* name) {
    if (!name) return nullptr;
    BuiltinMetaTable t = builtinMetaTable(builtinSet);
    for (size_t i = 0; i < t.count; ++i) {
        if (std::strcmp(t.rows[i].name, name) == 0) return &t.rows[i];
    }
    return nullptr;
}

const BuiltinMeta* findBuiltinMeta(const char* name) { return findBuiltinMeta("v170", name); }

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
