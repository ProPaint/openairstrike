// Where a silhouette shadow lies on the ground (docs/spec/render-pipeline.md 5.3, 5.4), backend
// neutral: the rectangle of as3d/shadow_render.h's shadowRect. The draped geometry itself is
// buildTerrainDecal (decal_build.cpp), shared with the ground marks.
#include "as3d/shadow_render.h"

namespace as3d {

GroundRect shadowRect(const ShadowBounds& b, ShadowKind kind, const Vec3& origin, float yawDegrees) {
    if (kind == ShadowKind::Projected) {
        return GroundRect::axisAligned(origin.x + static_cast<float>(b.xmin), origin.y + static_cast<float>(b.ymin),
                                       origin.x + static_cast<float>(b.xmax), origin.y + static_cast<float>(b.ymax));
    }
    return GroundRect::rotated({origin.x, origin.y}, yawDegrees, static_cast<float>(b.xmin), static_cast<float>(b.ymin),
                               static_cast<float>(b.xmax), static_cast<float>(b.ymax));
}

GroundRect shadowRect(const ShadowMap& map, const Vec3& origin, float yawDegrees) {
    return shadowRect(map.bounds, map.kind, origin, yawDegrees);
}

} // namespace as3d
