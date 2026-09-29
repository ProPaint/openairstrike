// Implements engine/include/as3d/object_tree.h. See docs/spec/obj.md ("attach") and
// docs/spec/mdl.md ("The attach position/orientation formula", "The trailing 12 bytes").
#include "as3d/object_tree.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>

#include "as3d/core.h"
#include "as3d/defs.h"
#include "as3d/model.h"
#include "as3d/vfs.h"

namespace as3d {

namespace {

std::string lowerAscii(const std::string& s) {
    std::string r = s;
    for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

// translation(pos) * mat4(rot) -- a plain rotation+translation, no separate helper exists
// in as3d/math.h for combining a Mat3 and a Vec3 this way.
Mat4 rigidTransform(const Mat3& rot, const Vec3& pos) {
    Mat4 m = Mat4::identity();
    for (int c = 0; c < 3; c++)
        for (int r = 0; r < 3; r++) m.at(c, r) = rot.at(c, r);
    m.at(3, 0) = pos.x;
    m.at(3, 1) = pos.y;
    m.at(3, 2) = pos.z;
    return m;
}

// A node's own orientation matrix from its `angles`, using the engine's own
// AnglesToAxis formula (docs/spec/engine-behaviour.md section 4.3), NOT as3d::anglesToAxis:
// the angle fields are (x, y, z) = (roll, pitch, yaw) in degrees, z is yaw about +Z
// (0 = facing +X), and
//   forward = (cy*cz, cy*sz, sy)
//   left    = (sx*sy*cz - cx*sz, sx*sy*sz + cx*cz, -sx*cy)
//   up      = (-sx*sz - cx*sy*cz, sx*cz - cx*sy*sz, cx*cy)
// The result has these three vectors as its columns, so `rot * v` maps a local vector
// (x along forward, y along left, z along up) into the parent/world frame. It is the
// identity at zero angles and always a proper right-handed rotation. It differs from
// as3d::anglesToAxis (math.h), which takes (pitch, yaw, roll), negates pitch in forward
// and returns a mirrored `right`; see the WP-30/33 report.
Mat3 rotFromAngles(const Vec3& angles) {
    float x = degToRad(angles.x), y = degToRad(angles.y), z = degToRad(angles.z);
    float sx = std::sin(x), cx = std::cos(x), sy = std::sin(y), cy = std::cos(y);
    float sz = std::sin(z), cz = std::cos(z);
    Vec3 forward{cy * cz, cy * sz, sy};
    Vec3 left{sx * sy * cz - cx * sz, sx * sy * sz + cx * cz, -sx * cy};
    Vec3 up{-sx * sz - cx * sy * cz, sx * cz - cx * sy * sz, cx * cy};
    Mat3 m;
    m.at(0, 0) = forward.x; m.at(0, 1) = forward.y; m.at(0, 2) = forward.z;
    m.at(1, 0) = left.x;    m.at(1, 1) = left.y;    m.at(1, 2) = left.z;
    m.at(2, 0) = up.x;      m.at(2, 1) = up.y;      m.at(2, 2) = up.z;
    return m;
}

// Everything the builder needs that must not leak into object_tree.h (which stays
// defs.h-free -- see the header comment).
struct Builder {
    const DefDatabase& db;
    Vfs& vfs;
    ObjectTree::BuildOptions opts;
    std::vector<ObjectNode>& nodes;
    std::vector<std::string>& warnings;
    ObjectTreeStats& stats;
    std::unordered_map<std::string, ModelData> modelCache; // key: normalizePath(model path)
    std::vector<std::string> ancestry;                      // lowercased object names, open frames

    const ModelData* loadModelCached(const std::string& path) {
        if (path.empty()) return nullptr;
        std::string key = normalizePath(path);
        auto it = modelCache.find(key);
        if (it != modelCache.end()) return &it->second;
        Blob blob;
        ModelData data; // stays default/empty if the read or parse fails -- findTag() on an
                         // empty ModelData simply never finds anything, which is the
                         // correct "missing tag" outcome for a broken/missing model.
        if (vfs.read(path, blob)) {
            std::string err;
            loadModel(blob.data(), blob.size(), data, &err);
        }
        auto res = modelCache.emplace(key, std::move(data));
        return &res.first->second;
    }

    // "origin" (case-insensitive) is always found, per docs/spec/obj.md, without ever
    // touching the parent's model.
    Vec3 resolveTagPosition(const ObjectDef* parentDef, const std::string& tagName, bool& found) {
        found = false;
        if (lowerAscii(tagName) == "origin") {
            found = true;
            return Vec3{0, 0, 0};
        }
        const ModelData* model = parentDef ? loadModelCached(parentDef->model) : nullptr;
        if (model) {
            const ModelTag* tag = model->findTag(tagName.c_str());
            if (tag) {
                found = true;
                return tag->position;
            }
        }
        return Vec3{0, 0, 0};
    }

    // Appends one node, links it to its parent, updates stats, and (for an Object-kind
    // node whose subtree should still be expanded) recurses into its own attachments.
    int addNode(int parentIdx, NodeKind kind, const ObjectDef* def, const ParticleSystemDef* ps,
                const std::string& name, const std::string& tagName, const std::string& attachId,
                bool absolute, bool nightOnly, const Vec3& tagPos, int depth, bool expand) {
        int idx = static_cast<int>(nodes.size());
        nodes.emplace_back();
        ObjectNode& n = nodes.back();
        n.kind = kind;
        n.def = def;
        n.particleDef = ps;
        n.name = name;
        n.parent = parentIdx;
        n.tagName = tagName;
        n.attachId = attachId;
        n.absolute = absolute;
        n.nightOnly = nightOnly;
        n.tagPosition = tagPos;
        if (def) {
            // ObjectDef::scale defaults to a raw 0.0 when the "scale" statement is
            // absent (never used in the shipped data, see docs/spec/obj.md); this
            // engine treats that as "no override", matching the attach formula's own
            // "parent.scale > 0.01" unset-check.
            n.scale = std::fabs(def->scale) > 0.0001f ? def->scale : 1.0f;
        }
        if (parentIdx >= 0) nodes[static_cast<size_t>(parentIdx)].children.push_back(idx);
        stats.nodeCount++;
        stats.maxDepth = std::max(stats.maxDepth, depth);

        if (expand && kind == NodeKind::Object && def) expandAttachments(idx, def, depth);
        return idx;
    }

    void expandAttachments(int nodeIdx, const ObjectDef* def, int depth) {
        if (!def->attachments.empty()) stats.objectsWithAttachments++;
        if (depth >= opts.maxDepth) {
            if (!def->attachments.empty()) {
                warnings.push_back("object '" + def->name + "': max attachment depth (" +
                                    std::to_string(opts.maxDepth) + ") reached, stopping recursion");
            }
            return;
        }
        for (const AttachDef& at : def->attachments) {
            if (at.nightOnly && !opts.night) continue;

            // VERIFIED-CODE resolution order (docs/spec/obj.md, G_InitObject
            // @0x00409ba0): particle system first, then object.
            const ParticleSystemDef* ps = db.findParticleSystem(at.targetName);
            const ObjectDef* childDef = ps ? nullptr : db.findObject(at.targetName);
            if (!ps && !childDef) {
                stats.missingTargets++;
                warnings.push_back("object '" + def->name + "': attach target '" + at.targetName +
                                    "' does not resolve to any object or particle system");
                continue;
            }

            bool tagFound = false;
            Vec3 tagPos = resolveTagPosition(def, at.tagName, tagFound);
            if (!tagFound) {
                stats.missingTags++;
                // Matches the original's own log message verbatim (docs/spec/mdl.md,
                // "The trailing 12 bytes"): "Tag '%s' not found in model '%s'."
                warnings.push_back("Tag '" + at.tagName + "' not found in model '" + def->model + "'.");
            }

            if (ps) {
                addNode(nodeIdx, NodeKind::ParticleSystem, nullptr, ps, ps->name, at.tagName,
                        at.idName, at.absolute, at.nightOnly, tagPos, depth + 1, /*expand=*/false);
                continue;
            }

            std::string lname = lowerAscii(childDef->name);
            bool isCycle = std::find(ancestry.begin(), ancestry.end(), lname) != ancestry.end();
            if (isCycle) {
                stats.cyclesDetected++;
                warnings.push_back("object '" + def->name + "': attachment cycle detected via '" +
                                    childDef->name + "', not expanded further");
                // Still record the node (visible in --list) but never recurse into it.
                addNode(nodeIdx, NodeKind::Object, childDef, nullptr, childDef->name, at.tagName,
                        at.idName, at.absolute, at.nightOnly, tagPos, depth + 1, /*expand=*/false);
                continue;
            }

            ancestry.push_back(lname);
            addNode(nodeIdx, NodeKind::Object, childDef, nullptr, childDef->name, at.tagName,
                    at.idName, at.absolute, at.nightOnly, tagPos, depth + 1, /*expand=*/true);
            ancestry.pop_back();
        }
    }
};

} // namespace

ObjectTree ObjectTree::build(const DefDatabase& db, Vfs& vfs, const std::string& objectName,
                              const BuildOptions& options) {
    ObjectTree tree;
    const ObjectDef* root = db.findObject(objectName);
    if (!root) {
        tree.warnings_.push_back("object '" + objectName + "' not found");
        return tree;
    }

    Builder b{db, vfs, options, tree.nodes_, tree.warnings_, tree.stats_, {}, {}};
    b.ancestry.push_back(lowerAscii(root->name));
    b.addNode(-1, NodeKind::Object, root, nullptr, root->name, "", "", false, false, Vec3{0, 0, 0}, 0,
              /*expand=*/true);
    tree.updateTransforms();
    return tree;
}

void ObjectTree::updateTransforms() {
    // nodes_ is always stored parent-before-child (a child's index is only ever
    // appended after its parent already exists), so a single forward pass is enough.
    for (size_t i = 0; i < nodes_.size(); i++) {
        ObjectNode& n = nodes_[i];
        Mat3 ownRot = rotFromAngles(n.angles);
        if (n.parent < 0) {
            n.worldRot = ownRot;
            n.worldPos = n.localOffset;
        } else {
            ObjectNode& p = nodes_[static_cast<size_t>(n.parent)];
            float pScale = std::fabs(p.scale) > 0.01f ? p.scale : 1.0f;
            Vec3 local = n.absolute ? (n.tagPosition + n.localOffset) : n.tagPosition;
            local = local * pScale;
            n.worldPos = p.worldPos + p.worldRot * local;
            if (!n.absolute) n.worldPos = n.worldPos + n.localOffset;
            n.worldRot = n.absolute ? (p.worldRot * ownRot) : ownRot;
        }
        float s = std::fabs(n.scale) > 0.0001f ? n.scale : 1.0f;
        n.world = rigidTransform(n.worldRot, n.worldPos) * as3d::scale(Vec3{s, s, s});
    }
}

} // namespace as3d
