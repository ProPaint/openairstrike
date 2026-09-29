// Skid-mark trails as the simulation keeps them (AirStrike 2 and later): the pool, the nodes
// and their ages. Rules: docs/spec/as2/engine-behaviour.delta.md 3.1.2 (G_InitObject,
// G_UpdateSkidTrails, G_FreeEntity), render-pipeline.delta.md 7.7, issues as2/210 and
// as2/221; numbers from GameRules::skid*. Plain data, no GL: World::liveSkidTrail hands the
// trails out read-only and WorldRenderer turns them into as3d/skid_render.h's SkidTrail.
//
// Choices (issue as2/210): a node started when the timer runs out gets age 0 (the original
// keeps the stale age of the reused slot); a full trail drops its oldest node when a new one
// is started (same visible result as the original's write one slot past the end); ages grow
// also while the game is paused, as in the original (G_UpdateSkidTrails runs every frame).
#pragma once

#include <array>
#include <string>

#include "as3d/core.h"
#include "as3d/vec.h"

namespace as3d {

constexpr int kMaxSkidNodes = 32;       // node capacity of a trail (GameRules::skidMaxNodes, 23, is clamped to it)
constexpr int kMaxSkidTrailPool = 256;  // bound of GameRules::skidTrailPool (64)
constexpr int kMaxEntitySkidTrails = 8; // skid_mark records a definition holds (as2 3.1.2)

// One cross-section of a trail.
struct WorldSkidNode {
    Vec2 position;             // centre of the mark: B.xy + y·Ŷ + x·X̂ (skid_mark x y w)
    Vec2 direction{0.0f, 1.0f}; // (−X̂.y, X̂.x): the section runs along X̂ = (d.y, −d.x)
    float age = 0.0f;          // seconds since the node was started
    float distance = 0.0f;     // the trail's length when the node was last written (texture v · w)
};

struct WorldSkidTrail {
    bool inUse = false;
    // The entity laying it (slot and generation); -1 once the owner was freed.
    int owner = -1;
    u32 ownerGeneration = 0;
    // The definition's record: lateral offset x (along axis row 0), forward offset y (row 1),
    // width w, texture (points into the definition database, which outlives the world).
    float x = 0.0f, y = 0.0f, width = 0.0f;
    const std::string* texture = nullptr;
    float timer = 0.0f;  // seconds since the last node was fixed
    float length = 0.0f; // distance the owner's base origin travelled (3D)
    bool hasPrevious = false;
    Vec3 previous;       // the owner's base origin at the last update
    int nodeCount = 0;   // oldest first
    std::array<WorldSkidNode, kMaxSkidNodes> nodes{};
};

} // namespace as3d
