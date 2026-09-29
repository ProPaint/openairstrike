// Static object attachment hierarchy (WP-30/33). See docs/spec/obj.md ("attach") and
// docs/spec/mdl.md ("The attach position/orientation formula") for the rules this file
// implements.
//
// Deliberately GL-free and, more importantly, defs.h-free at the *header* level: only
// forward declarations of as3d::ObjectDef / as3d::ParticleSystemDef / as3d::DefDatabase
// are used here, so this header never needs to see the full as3d::BlendMode enum from
// as3d/defs.h. That matters because as3d/defs.h and as3d/gfx.h both declare an unrelated
// `enum class as3d::BlendMode` with different enumerators -- the two cannot both be
// #included in the same translation unit without a redefinition error. Callers that need
// this header's types *and* defs.h/gfx.h at once (i.e. every viewer command) must resolve
// that collision themselves (see apps/viewer/cmd_model.cpp for the pattern); this header
// only needs the `const ObjectDef*`/`const ParticleSystemDef*` pointer types and a
// `const DefDatabase&` parameter, none of which require the full definitions.
//
// This package does not run scripts: every node's own `angles`/`localOffset` start at
// zero and `active` starts true (see docs/spec/rcsl-vm.md's entity field map for the
// fields scripts will eventually drive: origin/angles/scale/color). updateTransforms()
// still implements the *general* attachment formula (non-zero angles/offset/scale, both
// `abs` and non-`abs`), so synthetic tests can exercise it, and so it is ready to be
// driven by a script runtime later without changes.
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "as3d/math.h"

namespace as3d {

struct ObjectDef;
struct ParticleSystemDef;
class DefDatabase;
class Vfs;

enum class NodeKind { Object, ParticleSystem };

struct ObjectNode {
    NodeKind kind = NodeKind::Object;
    const ObjectDef* def = nullptr;                   // set when kind == Object
    const ParticleSystemDef* particleDef = nullptr;    // set when kind == ParticleSystem
    std::string name;                                  // resolved target name

    int parent = -1;             // index into ObjectTree::nodes(), -1 for the root
    std::vector<int> children;   // indices into ObjectTree::nodes()

    // How this node is attached to its parent (meaningless for the root).
    std::string tagName;     // a model tag name on the PARENT's model, or "origin"
    std::string attachId;    // "id NAME" modifier, empty if absent
    bool absolute = false;   // "abs" modifier: rigid (position+orientation) attachment
    bool nightOnly = false;  // "night" modifier: only instantiated when night == true
    // Per docs/spec/obj.md, an attachment's `id` lets scripts later address and
    // deactivate/reactivate it; the documented initial state is active. This package
    // never changes it, but callers (a renderer, a future script runtime) should skip
    // an inactive node and its whole subtree.
    bool active = true;

    // Fixed at build time: the position of `tagName` inside the PARENT's own model
    // space ((0,0,0) for "origin" or when the parent has no model/the tag is missing).
    Vec3 tagPosition{0.0f, 0.0f, 0.0f};

    // Mutable per-node state a script runtime will drive; zero-initialized here per
    // "this package does not run scripts" above. Exposed so tests (and a future script
    // runtime) can set them directly and call updateTransforms().
    Vec3 localOffset{0.0f, 0.0f, 0.0f}; // child's own declared offset, added per the abs rule
    Vec3 angles{0.0f, 0.0f, 0.0f};      // own orientation, degrees: x = roll, y = pitch, z = yaw (0 = +X)
    float scale = 1.0f;                 // 0 or a near-zero value is treated as "unset" -> 1.0
    Vec4 colour{1.0f, 1.0f, 1.0f, 1.0f};

    // Computed by ObjectTree::updateTransforms().
    Vec3 worldPos{0.0f, 0.0f, 0.0f};
    Mat3 worldRot = Mat3::identity();
    Mat4 world = Mat4::identity(); // translation(worldPos) * mat4(worldRot) * scale(scale)
};

struct ObjectTreeStats {
    int nodeCount = 0;
    int maxDepth = 0;               // root is depth 0
    int objectsWithAttachments = 0; // Object-kind nodes whose def declares >= 1 attach statement
    int missingTags = 0;
    int missingTargets = 0;
    int cyclesDetected = 0;
};

// Deliberately a free struct, not nested in ObjectTree: GCC rejects a default function
// argument of `= {}` that refers to a nested class's own default member initializers
// from within the same enclosing class (the nested type isn't "complete enough" yet at
// that point) -- see ObjectTree::build below.
struct ObjectTreeBuildOptions {
    // Honours the "night" attach modifier (docs/spec/obj.md) / LevelDef::night
    // (docs/spec/levels-txt.md): night-only attachments are only instantiated when this
    // is true.
    bool night = false;
    // Guards against cycles and pathologically deep trees. The shipped data's deepest
    // chain is 2 (tank -> turret -> guns); this is a generous safety net, not a
    // realistic limit.
    int maxDepth = 48;
};

class ObjectTree {
public:
    using BuildOptions = ObjectTreeBuildOptions;

    // Builds the tree rooted at `objectName` (looked up in `db`, case-insensitively, as
    // DefDatabase::findObject does). `vfs` is used only to load the .mdl files of every
    // object actually reached, purely to resolve tag positions -- no GPU/texture work
    // happens here (that is ResourceCache's job, see as3d/scene.h). Never crashes; on
    // failure (object not found) the tree is empty (nodes().empty()) and a warning is
    // recorded.
    static ObjectTree build(const DefDatabase& db, Vfs& vfs, const std::string& objectName,
                             const BuildOptions& options = {});

    const std::vector<ObjectNode>& nodes() const { return nodes_; }
    std::vector<ObjectNode>& nodes() { return nodes_; }
    const std::vector<std::string>& warnings() const { return warnings_; }
    const ObjectTreeStats& stats() const { return stats_; }

    // Recomputes world/worldPos/worldRot for every node from its current
    // angles/localOffset/scale/tagPosition and its parent's already-updated transform,
    // implementing exactly the "attach position/orientation formula" of
    // docs/spec/mdl.md v1.1:
    //
    //   local = abs ? (tagPosition + localOffset) : tagPosition
    //   local *= (parent.scale > 0.01 ? parent.scale : 1.0)
    //   worldPos = parent.worldPos + parent.worldRot * local
    //   if (!abs) worldPos += localOffset            // added in world space, unrotated
    //   worldRot = abs ? (parent.worldRot * ownRot) : ownRot
    //   ownRot = engine AnglesToAxis(angles), angles = (roll, pitch, yaw); see engine-behaviour.md 4.3
    //
    // The root has no parent: worldPos = localOffset, worldRot = ownRot. Safe to call
    // repeatedly (e.g. once per frame once a script runtime exists); nodes_ is always
    // stored parent-before-child, so a single forward pass suffices. Cheap: no
    // allocation, no I/O, no GL.
    void updateTransforms();

private:
    std::vector<ObjectNode> nodes_;
    std::vector<std::string> warnings_;
    ObjectTreeStats stats_;
};

} // namespace as3d
