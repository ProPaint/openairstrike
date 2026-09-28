// Minimal plain vector structs. Owned temporarily by this work package: a parallel
// worktree is building the real engine/include/as3d/math.h; when it merges, math.h
// is expected to build on these same two structs rather than duplicate them.
#pragma once

namespace as3d {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

} // namespace as3d
