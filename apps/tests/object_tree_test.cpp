// Tests for as3d::ObjectTree (as3d/object_tree.h). See docs/spec/obj.md ("attach") and
// docs/spec/mdl.md ("The attach position/orientation formula").
#include "doctest.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>

#include "as3d/core.h"
#include "as3d/defs.h"
#include "as3d/math.h"
#include "as3d/object_tree.h"
#include "as3d/vfs.h"
#include "test_data.h"

using namespace as3d;

namespace {

bool mountOriginalPaks(Vfs& vfs) {
    std::string dataDir = testdata::installDir() + "/data";
    for (const char* name : {"pak0.apk", "pak1.apk", "pak2.apk"}) {
        auto src = makePakSource(openFileStream(dataDir + "/" + name));
        if (!src) return false;
        vfs.mount(std::move(src));
    }
    return true;
}

// Independent re-implementation of the rotation-matrix-from-angles construction used by
// engine/src/game/object_tree.cpp, so the transform tests below check the *formula*
// against a from-scratch computation, not against the implementation's own helper.
Mat3 rotFromAnglesRef(const Vec3& a) {
    // docs/spec/engine-behaviour.md 4.3, written independently of the implementation.
    double x = a.x * kPi / 180.0, y = a.y * kPi / 180.0, z = a.z * kPi / 180.0;
    double sx = std::sin(x), cx = std::cos(x), sy = std::sin(y), cy = std::cos(y);
    double sz = std::sin(z), cz = std::cos(z);
    double f[3] = {cy * cz, cy * sz, sy};
    double l[3] = {sx * sy * cz - cx * sz, sx * sy * sz + cx * cz, -sx * cy};
    double u[3] = {-sx * sz - cx * sy * cz, sx * cz - cx * sy * sz, cx * cy};
    Mat3 m;
    for (int r = 0; r < 3; r++) {
        m.at(0, r) = static_cast<float>(f[r]);
        m.at(1, r) = static_cast<float>(l[r]);
        m.at(2, r) = static_cast<float>(u[r]);
    }
    return m;
}

void checkMat3Approx(const Mat3& a, const Mat3& b, double eps = 0.0005) {
    for (int c = 0; c < 3; c++) {
        for (int r = 0; r < 3; r++) {
            CHECK(a.at(c, r) == doctest::Approx(b.at(c, r)).epsilon(eps));
        }
    }
}

void checkVec3Approx(const Vec3& a, const Vec3& b, double eps = 0.0005) {
    CHECK(a.x == doctest::Approx(b.x).epsilon(eps));
    CHECK(a.y == doctest::Approx(b.y).epsilon(eps));
    CHECK(a.z == doctest::Approx(b.z).epsilon(eps));
}

bool startsWithKnownWarning(const std::string& w) {
    static const char* kPrefixes[] = {
        "object '",  // "attach target ... does not resolve", "attachment cycle detected",
                     // "max attachment depth"
        "Tag '",     // "Tag 'X' not found in model 'Y'."
    };
    for (const char* p : kPrefixes) {
        if (w.rfind(p, 0) == 0) return true;
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------------
// Synthetic transform tests (no game data needed): exercise the general attachment
// formula from docs/spec/mdl.md directly, independent of any real .obj/.mdl content.
// ---------------------------------------------------------------------------------

TEST_CASE("ObjectTree::updateTransforms: root transform") {
    ObjectTree tree;
    ObjectNode root;
    root.localOffset = Vec3{10.0f, 20.0f, 5.0f};
    root.angles = Vec3{0.0f, 90.0f, 0.0f};
    root.scale = 2.0f;
    tree.nodes().push_back(root);
    tree.updateTransforms();

    const ObjectNode& r = tree.nodes()[0];
    checkVec3Approx(r.worldPos, root.localOffset);
    checkMat3Approx(r.worldRot, rotFromAnglesRef(root.angles));
}

TEST_CASE("ObjectTree::updateTransforms: abs attachment is rigid (position AND orientation "
          "composed with the parent)") {
    ObjectTree tree;

    ObjectNode root;
    root.localOffset = Vec3{10.0f, 20.0f, 5.0f};
    root.angles = Vec3{0.0f, 90.0f, 0.0f};
    root.scale = 2.0f;
    tree.nodes().push_back(root);

    ObjectNode child;
    child.parent = 0;
    child.absolute = true;
    child.tagPosition = Vec3{1.0f, 0.0f, 0.0f};
    child.localOffset = Vec3{0.0f, 0.0f, 3.0f};
    child.angles = Vec3{0.0f, 45.0f, 0.0f}; // child's own additional yaw
    child.scale = 1.0f;
    tree.nodes().push_back(child);
    tree.nodes()[0].children.push_back(1);

    tree.updateTransforms();

    const ObjectNode& r = tree.nodes()[0];
    const ObjectNode& c = tree.nodes()[1];

    Mat3 rootRot = rotFromAnglesRef(root.angles);
    Vec3 local = (child.tagPosition + child.localOffset) * root.scale; // abs: offset folded in before the parent rotation, then scaled
    Vec3 expectedPos = r.worldPos + rootRot * local;
    Mat3 expectedRot = rootRot * rotFromAnglesRef(child.angles); // abs: composed with the parent's rotation

    checkVec3Approx(c.worldPos, expectedPos);
    checkMat3Approx(c.worldRot, expectedRot);
}

TEST_CASE("ObjectTree::updateTransforms: non-abs attachment keeps independent orientation "
          "and adds its own offset unrotated, in world space") {
    ObjectTree tree;

    ObjectNode root;
    root.localOffset = Vec3{10.0f, 20.0f, 5.0f};
    root.angles = Vec3{0.0f, 90.0f, 0.0f};
    root.scale = 2.0f;
    tree.nodes().push_back(root);

    ObjectNode child;
    child.parent = 0;
    child.absolute = false;
    child.tagPosition = Vec3{0.0f, 5.0f, 0.0f};
    child.localOffset = Vec3{1.0f, 1.0f, 1.0f};
    child.angles = Vec3{0.0f, 0.0f, 0.0f}; // no own rotation either, to isolate the position rule
    child.scale = 1.0f;
    tree.nodes().push_back(child);
    tree.nodes()[0].children.push_back(1);

    tree.updateTransforms();

    const ObjectNode& r = tree.nodes()[0];
    const ObjectNode& c = tree.nodes()[1];

    Mat3 rootRot = rotFromAnglesRef(root.angles);
    // non-abs: local = tagPosition only (no localOffset folded in), scaled by the
    // parent, THEN localOffset is added post-hoc in world space, unrotated.
    Vec3 local = child.tagPosition * root.scale;
    Vec3 expectedPos = r.worldPos + rootRot * local + child.localOffset;
    Mat3 expectedRot = rotFromAnglesRef(child.angles); // non-abs: parent's rotation is ignored entirely

    checkVec3Approx(c.worldPos, expectedPos);
    checkMat3Approx(c.worldRot, expectedRot);
}

TEST_CASE("ObjectTree::updateTransforms: parent scale near zero is treated as unset (1.0)") {
    ObjectTree tree;

    ObjectNode root;
    root.scale = 0.0f; // "never used in shipped data" case, per docs/spec/obj.md
    tree.nodes().push_back(root);

    ObjectNode child;
    child.parent = 0;
    child.absolute = true;
    child.tagPosition = Vec3{3.0f, 4.0f, 0.0f};
    tree.nodes().push_back(child);
    tree.nodes()[0].children.push_back(1);

    tree.updateTransforms();

    // scale == 0 must behave exactly like scale == 1 (the "parent.scale > 0.01" guard
    // in docs/spec/mdl.md's formula), not multiply everything to the origin.
    checkVec3Approx(tree.nodes()[1].worldPos, Vec3{3.0f, 4.0f, 0.0f});
}

TEST_CASE("ObjectTree::updateTransforms: yaw is angle field z, 0 faces +X, identity at zero") {
    ObjectTree tree;
    ObjectNode root;
    root.angles = Vec3{0, 0, 90};
    tree.nodes().push_back(root);
    ObjectNode child;
    child.parent = 0;
    child.tagPosition = Vec3{1, 0, 0};
    tree.nodes().push_back(child);
    tree.nodes()[0].children.push_back(1);
    tree.updateTransforms();
    checkVec3Approx(tree.nodes()[1].worldPos, Vec3{0, 1, 0}, 0.001);
}

TEST_CASE("ObjectTree: default-constructed tree is empty") {
    ObjectTree tree;
    CHECK(tree.nodes().empty());
    CHECK(tree.warnings().empty());
}

// ---------------------------------------------------------------------------------
// Real-data tests.
// ---------------------------------------------------------------------------------

TEST_CASE("ObjectTree: unknown object name yields an empty tree with one warning, no crash") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    Vfs vfs;
    REQUIRE(mountOriginalPaks(vfs));
    DefDatabase db;
    REQUIRE(db.load(vfs));

    ObjectTree tree = ObjectTree::build(db, vfs, "this_object_does_not_exist_xyz");
    CHECK(tree.nodes().empty());
    REQUIRE(tree.warnings().size() == 1);
    CHECK(tree.warnings()[0].find("not found") != std::string::npos);
}

TEST_CASE("ObjectTree: spot check tank_small_green (turret at tag_turret, guns at tag_guns)") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    Vfs vfs;
    REQUIRE(mountOriginalPaks(vfs));
    DefDatabase db;
    REQUIRE(db.load(vfs));

    ObjectTree tree = ObjectTree::build(db, vfs, "tank_small_green");
    REQUIRE(!tree.nodes().empty());
    const ObjectNode& root = tree.nodes()[0];
    CHECK(root.name == "tank_small_green");
    CHECK(root.parent == -1);

    // Find the turret child (attached at tag_turret).
    int turretIdx = -1;
    for (int childIdx : root.children) {
        const ObjectNode& n = tree.nodes()[static_cast<size_t>(childIdx)];
        if (n.tagName == "tag_turret") turretIdx = childIdx;
    }
    REQUIRE(turretIdx >= 0);
    const ObjectNode& turret = tree.nodes()[static_cast<size_t>(turretIdx)];
    CHECK(turret.kind == NodeKind::Object);
    CHECK(turret.name == "tank_small_green_cannon");
    CHECK(turret.absolute);

    // The turret's own child: the guns, at tag_guns.
    bool foundGuns = false;
    for (int gcIdx : turret.children) {
        const ObjectNode& g = tree.nodes()[static_cast<size_t>(gcIdx)];
        if (g.tagName == "tag_guns") foundGuns = true;
    }
    CHECK(foundGuns);
}

TEST_CASE("ObjectTree: a helicopter object has rotor ('tag_vint*') children") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    Vfs vfs;
    REQUIRE(mountOriginalPaks(vfs));
    DefDatabase db;
    REQUIRE(db.load(vfs));

    // Find any object that declares an attachment at a "tag_vint*" tag (mdl.md's tag
    // inventory: "tag_vint* (49) -- Russian vint, 'rotor/propeller'") -- rather than
    // hardcoding one object name, since the exact roster of helicopter object names
    // isn't spec'd verbatim.
    const ObjectDef* helicopter = nullptr;
    for (const ObjectDef& o : db.objects()) {
        for (const AttachDef& at : o.attachments) {
            if (at.tagName.find("tag_vint") != std::string::npos) {
                helicopter = &o;
                break;
            }
        }
        if (helicopter) break;
    }
    REQUIRE(helicopter != nullptr);

    ObjectTree tree = ObjectTree::build(db, vfs, helicopter->name);
    bool foundRotor = false;
    for (const ObjectNode& n : tree.nodes()) {
        if (n.tagName.find("tag_vint") != std::string::npos) foundRotor = true;
    }
    CHECK(foundRotor);
}

TEST_CASE("ObjectTree: every one of the 864 object definitions builds without crashing, "
          "terminates, and only produces documented warning kinds") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    Vfs vfs;
    REQUIRE(mountOriginalPaks(vfs));
    DefDatabase db;
    REQUIRE(db.load(vfs));
    REQUIRE(db.objects().size() == 864);

    int totalNodes = 0, maxDepthOverall = 0, objectsWithAttachments = 0;
    int totalMissingTags = 0, totalMissingTargets = 0, totalCycles = 0;
    int unexpectedWarnings = 0;
    std::set<std::string> distinctRootsBuilt;

    for (const ObjectDef& o : db.objects()) {
        if (o.name.empty()) continue; // malformed block; nothing to build
        if (!distinctRootsBuilt.insert(o.name).second) continue; // first-wins, matches findObject

        ObjectTree tree = ObjectTree::build(db, vfs, o.name);
        const ObjectTreeStats& s = tree.stats();
        totalNodes += s.nodeCount;
        maxDepthOverall = std::max(maxDepthOverall, s.maxDepth);
        objectsWithAttachments += s.objectsWithAttachments;
        totalMissingTags += s.missingTags;
        totalMissingTargets += s.missingTargets;
        totalCycles += s.cyclesDetected;

        for (const std::string& w : tree.warnings()) {
            if (!startsWithKnownWarning(w)) unexpectedWarnings++;
        }
    }

    CHECK(unexpectedWarnings == 0);
    CHECK(totalNodes > 0);
    // The deepest documented chain is tank -> turret -> guns (depth 2); a generous
    // upper bound catches a real regression without being a tautology.
    CHECK(maxDepthOverall <= 10);

    std::fprintf(stderr,
                 "ObjectTree corpus totals: %d distinct roots built, %d total nodes, max depth %d, "
                 "%d objects-with-attachments (root-level, summed per build), %d missing tags, "
                 "%d missing targets, %d cycles\n",
                 static_cast<int>(distinctRootsBuilt.size()), totalNodes, maxDepthOverall,
                 objectsWithAttachments, totalMissingTags, totalMissingTargets, totalCycles);
}
