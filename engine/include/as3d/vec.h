// Plain vector structs, for headers that need the types without the math
// library. Operations on them live in as3d/math.h. Owned by the orchestrator.
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
