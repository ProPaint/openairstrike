// The sequels' model view (the helicopter selection's rotating preview, ui::ModelView), the part
// every host draws the same way: the object's parts with their materials and local matrices, and
// the entity matrix of the view's angles and origin. GameView (GL) and the 3DS frame composer
// (PICA, apps/3ds/source/ctr_frame.cpp) draw them with the camera of the view: at the origin,
// looking down -z with y up (identity view), the view's field of view and planes, the default
// SceneLighting. Header only: the 3DS app compiles no GameView.
#pragma once

#include <cmath>
#include <string>
#include <vector>

#include "as3d/defs.h"
#include "as3d/math.h"
#include "as3d/object_tree.h"
#include "as3d/scene.h"

namespace as3d_game {

// AnglesToAxis (engine-behaviour.md 4.3) with the angles as fields 14..16, in degrees, and the
// origin: the entity's model matrix (as the banner's).
inline as3d::Mat4 entityMatrix(const float angles[3], const float origin[3]) {
    using as3d::Vec3;
    const float kDeg = 3.14159265f / 180.0f;
    const float ax = angles[0] * kDeg, ay = angles[1] * kDeg, az = angles[2] * kDeg;
    const float sa = std::sin(ax), ca = std::cos(ax), sb = std::sin(ay), cb = std::cos(ay), sc = std::sin(az),
                cc = std::cos(az);
    const Vec3 fwd{cb * cc, cb * sc, sb};
    const Vec3 left{sa * sb * cc - ca * sc, sa * sb * sc + ca * cc, -sa * cb};
    const Vec3 up{-sa * sc - ca * sb * cc, sa * cc - ca * sb * sc, ca * cb};
    as3d::Mat4 model;
    model.at(0, 0) = fwd.x; model.at(0, 1) = fwd.y; model.at(0, 2) = fwd.z; model.at(0, 3) = 0;
    model.at(1, 0) = left.x; model.at(1, 1) = left.y; model.at(1, 2) = left.z; model.at(1, 3) = 0;
    model.at(2, 0) = up.x; model.at(2, 1) = up.y; model.at(2, 2) = up.z; model.at(2, 3) = 0;
    model.at(3, 0) = origin[0]; model.at(3, 1) = origin[1]; model.at(3, 2) = origin[2]; model.at(3, 3) = 1;
    return model;
}

struct ModelViewPart {
    const as3d::GpuMesh* gpu = nullptr;
    as3d::Material material;
    as3d::Mat4 local;
    as3d::Vec4 colour{1, 1, 1, 1};
};

// The drawn parts of `object` (objects\*.obj): its object tree's active, visible nodes with a
// model (a hidden or inactive node hides its subtree; FL_NODRAW is skipped), meshes and skins
// from `cache`.
inline std::vector<ModelViewPart> modelViewParts(const as3d::DefDatabase& db, as3d::Vfs& vfs,
                                                 as3d::ResourceCache& cache, const std::string& object) {
    using namespace as3d;
    std::vector<ModelViewPart> parts;
    ObjectTree tree = ObjectTree::build(db, vfs, object);
    const auto& nodes = tree.nodes();
    std::vector<char> hidden(nodes.size(), 0);
    for (size_t i = 0; i < nodes.size(); ++i) {
        const ObjectNode& n = nodes[i];
        if (!n.active || (n.parent >= 0 && hidden[static_cast<size_t>(n.parent)])) hidden[i] = 1;
        if (hidden[i] || n.kind != NodeKind::Object || !n.def || n.def->model.empty() || (n.def->flags & FL_NODRAW)) continue;
        const GpuMesh& gpu = cache.mesh(n.def->model);
        if (!gpu.valid) continue;
        ModelViewPart part;
        part.gpu = &gpu;
        part.material = Material::fromObjectDef(*n.def, cache);
        part.local = n.world;
        part.colour = n.colour;
        parts.push_back(part);
    }
    return parts;
}

} // namespace as3d_game
