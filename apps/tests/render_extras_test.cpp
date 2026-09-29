// WP-35 render passes: shadows, ground marks, sprites, dynamic lights, environment maps.
// The geometry, formula and coordinate tests need no GL context and no game data; the
// headless render tests need both and skip loudly without them.
#include "doctest.h"

#include "as3d/dynamic_lights.h"
#include "as3d/envmap.h"
#include "as3d/ground_marks.h"
#include "as3d/level.h"
#include "as3d/platform.h"
#include "as3d/scene.h"
#include "as3d/shadow_render.h"
#include "as3d/sprite_render.h"
#include "as3d/terrain.h"

#include "as3d/defs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "../viewer/level_render.h"
#include "test_data.h"

using namespace as3d;

namespace {

// A W x H cell level whose vertex heights are `heightOf(col)` (0..255, mapped 1:1 to z).
struct SyntheticTerrain {
    LevelData level;
    Terrain terrain;
    SyntheticTerrain(int w, int h, int (*heightOf)(int col)) {
        level.width = static_cast<u32>(w);
        level.height = static_cast<u32>(h);
        level.cells.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
        for (int r = 0; r < h; r++)
            for (int c = 0; c < w; c++) level.cells[static_cast<size_t>(r) * w + c].height = static_cast<u8>(heightOf(c));
        TerrainStyle style;
        style.hmin = 0.0f;
        style.hmax = 255.0f;
        REQUIRE(terrain.build(level, style));
    }
};

float triangleArea(const DecalVertex* v) {
    return 0.5f * std::fabs((v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[2].x - v[0].x) * (v[1].y - v[0].y));
}

float totalArea(const std::vector<DecalVertex>& verts) {
    float a = 0.0f;
    for (size_t i = 0; i + 2 < verts.size(); i += 3) a += triangleArea(&verts[i]);
    return a;
}

// A closed box [-hx, hx] x [-hy, hy] x [z0, z1] as ModelData (12 triangles, one UV).
ModelData makeBox(float hx, float hy, float z0, float z1) {
    ModelData m;
    for (int i = 0; i < 8; i++)
        m.positions.push_back({(i & 1) ? hx : -hx, (i & 2) ? hy : -hy, (i & 4) ? z1 : z0});
    m.uvs.push_back({0.5f, 0.5f});
    const int quads[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
    for (auto& q : quads) {
        Face a, b;
        a.v[0] = static_cast<u16>(q[0]); a.v[1] = static_cast<u16>(q[1]); a.v[2] = static_cast<u16>(q[2]);
        b.v[0] = static_cast<u16>(q[0]); b.v[1] = static_cast<u16>(q[2]); b.v[2] = static_cast<u16>(q[3]);
        m.faces.push_back(a);
        m.faces.push_back(b);
    }
    return m;
}

GraphicsContext* glContext() {
    static std::unique_ptr<GraphicsContext> ctx = [] {
        GraphicsConfig cfg;
        cfg.width = 320;
        cfg.height = 240;
        cfg.headless = true;
        cfg.title = "as3d_tests";
        return createGraphicsContext(cfg);
    }();
    if (ctx) ctx->makeCurrent();
    return ctx.get();
}

#define REQUIRE_GL(ctxVar)                                                                     \
    GraphicsContext* ctxVar = glContext();                                                     \
    if (!ctxVar) {                                                                             \
        std::fprintf(stderr, "SKIPPED (no headless GLES context available): %s\n", __FILE__);  \
        return;                                                                                \
    }

} // namespace

// ---------------------------------------------------------------------------
// Ground decal geometry
// ---------------------------------------------------------------------------

TEST_CASE("decal geometry: flat terrain, axis aligned rectangle") {
    SyntheticTerrain st(8, 8, [](int) { return 77; });
    // Spans four cells in each direction, partly cutting cells.
    GroundRect rect = GroundRect::axisAligned(50.0f, 70.0f, 170.0f, 150.0f);
    std::vector<DecalVertex> v;
    buildTerrainDecal(st.terrain, rect, v);
    REQUIRE(!v.empty());
    CHECK(v.size() % 3 == 0);
    CHECK(totalArea(v) == doctest::Approx(120.0f * 80.0f).epsilon(1e-4));
    for (const DecalVertex& p : v) {
        CHECK(p.x >= 50.0f - 1e-3f);
        CHECK(p.x <= 170.0f + 1e-3f);
        CHECK(p.y >= 70.0f - 1e-3f);
        CHECK(p.y <= 150.0f + 1e-3f);
        CHECK(p.z == doctest::Approx(77.0f));
        // u = s, v = 1 - t
        CHECK(p.u == doctest::Approx((p.x - 50.0f) / 120.0f).epsilon(1e-3));
        CHECK(p.v == doctest::Approx(1.0f - (p.y - 70.0f) / 80.0f).epsilon(1e-3));
    }
}

TEST_CASE("decal geometry: heights follow a slope") {
    SyntheticTerrain st(8, 8, [](int col) { return col * 20; });
    GroundRect rect = GroundRect::axisAligned(90.0f, 90.0f, 190.0f, 130.0f);
    std::vector<DecalVertex> v;
    buildTerrainDecal(st.terrain, rect, v);
    REQUIRE(!v.empty());
    CHECK(totalArea(v) == doctest::Approx(100.0f * 40.0f).epsilon(1e-4));
    for (const DecalVertex& p : v) CHECK(p.z == doctest::Approx(st.terrain.heightAt(p.x, p.y)).epsilon(0.02));
}

TEST_CASE("decal geometry: rotated rectangle keeps its area and stays inside") {
    SyntheticTerrain st(10, 10, [](int) { return 10; });
    GroundRect rect = GroundRect::rotated({200.0f, 200.0f}, 30.0f, -30.0f, -20.0f, 50.0f, 20.0f);
    std::vector<DecalVertex> v;
    buildTerrainDecal(st.terrain, rect, v);
    REQUIRE(!v.empty());
    CHECK(totalArea(v) == doctest::Approx(80.0f * 40.0f).epsilon(1e-3));
    for (const DecalVertex& p : v) {
        Vec2 st2 = rect.coords(p.x, p.y);
        CHECK(st2.x >= -1e-3f);
        CHECK(st2.x <= 1.0f + 1e-3f);
        CHECK(st2.y >= -1e-3f);
        CHECK(st2.y <= 1.0f + 1e-3f);
        CHECK(p.u == doctest::Approx(st2.x).epsilon(1e-3));
        CHECK(p.v == doctest::Approx(1.0f - st2.y).epsilon(1e-3));
    }
}

TEST_CASE("decal geometry: rectangle off the map or degenerate yields nothing") {
    SyntheticTerrain st(4, 4, [](int) { return 0; });
    std::vector<DecalVertex> v;
    buildTerrainDecal(st.terrain, GroundRect::axisAligned(500.0f, 500.0f, 600.0f, 600.0f), v);
    CHECK(v.empty());
    buildTerrainDecal(st.terrain, GroundRect::axisAligned(10.0f, 10.0f, 10.0f, 50.0f), v);
    CHECK(v.empty());
}

// ---------------------------------------------------------------------------
// Shadows: bounds, texture size, silhouette
// ---------------------------------------------------------------------------

TEST_CASE("shadow bounds and texture sizes follow the spec") {
    ModelData box = makeBox(10.0f, 10.0f, 0.0f, 10.0f);
    ShadowBounds down = computeShadowBounds(box, ShadowKind::Planar, {1.0f, 0.0f, 1.0f}, 3);
    // Planar ignores both the sun and the rotation.
    CHECK(down.xmin == -10);
    CHECK(down.xmax == 10);
    CHECK(down.ymin == -10);
    CHECK(down.ymax == 10);

    Vec3 sun = normalize(Vec3{1.0f, 0.0f, 1.0f}); // 45 degrees: the shadow of the top shifts by -z
    ShadowBounds proj = computeShadowBounds(box, ShadowKind::Projected, sun, 0);
    CHECK(proj.xmin == -20);
    CHECK(proj.xmax == 10);
    CHECK(proj.ymin == -10);
    CHECK(proj.ymax == 10);

    // A quarter turn of 3 steps (90 degrees) swaps the extents of a 2:1 box.
    ModelData slab = makeBox(20.0f, 5.0f, 0.0f, 1.0f);
    ShadowBounds rot = computeShadowBounds(slab, ShadowKind::Projected, {0.0f, 0.0f, 1.0f}, 3);
    CHECK(rot.xmax - rot.xmin == 10);
    CHECK(rot.ymax - rot.ymin == 40);

    int w = 0, h = 0;
    ShadowBounds b;
    b.xmin = -20; b.xmax = 20; b.ymin = -5; b.ymax = 5; // 40 x 10
    shadowTextureSize(b, ShadowQuality::Normal, w, h);
    CHECK(w == 32); // int(log2(40) + 0.5) = 5
    CHECK(h == 8);  // int(log2(10) + 0.5) = 3
    shadowTextureSize(b, ShadowQuality::Low, w, h);
    CHECK(w == 16);
    CHECK(h == 4);
    shadowTextureSize(b, ShadowQuality::High, w, h);
    CHECK(w == 64);
    CHECK(h == 16);
    b.xmin = -400; b.xmax = 400; b.ymin = -300; b.ymax = 300; // capped at 256 x 128
    shadowTextureSize(b, ShadowQuality::Normal, w, h);
    CHECK(w == 256);
    CHECK(h == 128);
}

TEST_CASE("shadow rectangles: projected is static, planar turns with the yaw") {
    ShadowMap m;
    m.bounds.xmin = -10; m.bounds.xmax = 30; m.bounds.ymin = -5; m.bounds.ymax = 5;
    m.kind = ShadowKind::Projected;
    GroundRect a = shadowRect(m, {100.0f, 200.0f, 50.0f}, 77.0f); // yaw and altitude ignored
    CHECK(a.origin.x == doctest::Approx(90.0f));
    CHECK(a.origin.y == doctest::Approx(195.0f));
    CHECK(a.sizeX == doctest::Approx(40.0f));
    CHECK(a.axisX.x == doctest::Approx(1.0f));

    m.kind = ShadowKind::Planar;
    GroundRect b = shadowRect(m, {100.0f, 200.0f, 999.0f}, 90.0f);
    CHECK(b.axisX.x == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(b.axisX.y == doctest::Approx(1.0f));
    // Local (xmin, ymin) = (-10, -5) turned by 90 degrees: (5, -10) from the origin.
    CHECK(b.origin.x == doctest::Approx(105.0f));
    CHECK(b.origin.y == doctest::Approx(190.0f));
}

TEST_CASE("shadow silhouette texture darkens by at most 40 percent") {
    REQUIRE_GL(ctx);
    ShadowRenderer sr;
    std::string err;
    REQUIRE_MESSAGE(sr.init(&err), err);
    ModelData box = makeBox(12.0f, 12.0f, 0.0f, 10.0f);
    // A stray vertex enlarges the bounds so that part of the texture stays uncovered.
    box.positions.push_back({40.0f, 40.0f, 0.0f});
    ShadowMap m;
    REQUIRE(sr.generate(box, nullptr, ShadowKind::Planar, {0.0f, 0.0f, 1.0f}, 0, ShadowQuality::Normal, m));
    REQUIRE(m.image.width == m.width);
    int maxA = 0, covered = 0, clear = 0;
    for (int i = 0; i < m.width * m.height; i++) {
        int a = m.image.rgba[static_cast<size_t>(i) * 4 + 3];
        maxA = std::max(maxA, a);
        if (a >= 100) covered++;
        if (a == 0) clear++;
        CHECK(m.image.rgba[static_cast<size_t>(i) * 4] == 114);
    }
    CHECK(maxA <= 102);            // 0.4 * 255
    CHECK(maxA >= 100);
    CHECK(covered > 0);
    CHECK(clear > covered);        // the box covers only part of the rectangle
    // The box lies in x, y in [-12, 12] of bounds [-12, 40]: the texture's left part is covered.
    // Row 0 is the largest y, so the covered block is in the lower left.
    int px = m.width * 12 / 100 + 1;
    CHECK(m.image.rgba[(static_cast<size_t>(m.height - 1 - (m.height * 12 / 100)) * m.width + px) * 4 + 3] >= 100);
    CHECK(m.image.rgba[(0 * static_cast<size_t>(m.width) + (m.width - 1)) * 4 + 3] == 0);
}

TEST_CASE("projected shadow erases geometry at or below the ground") {
    REQUIRE_GL(ctx);
    ShadowRenderer sr;
    std::string err;
    REQUIRE_MESSAGE(sr.init(&err), err);
    ModelData sunk = makeBox(10.0f, 10.0f, -8.0f, 0.0f); // entirely below z = 0
    ShadowMap m;
    REQUIRE(sr.generate(sunk, nullptr, ShadowKind::Projected, {0.0f, 0.0f, 1.0f}, 0, ShadowQuality::Normal, m));
    int maxA = 0;
    for (int i = 0; i < m.width * m.height; i++) maxA = std::max<int>(maxA, m.image.rgba[static_cast<size_t>(i) * 4 + 3]);
    CHECK(maxA == 0);
    ModelData standing = makeBox(10.0f, 10.0f, -8.0f, 6.0f);
    ShadowMap m2;
    REQUIRE(sr.generate(standing, nullptr, ShadowKind::Projected, {0.0f, 0.0f, 1.0f}, 0, ShadowQuality::Normal, m2));
    maxA = 0;
    for (int i = 0; i < m2.width * m2.height; i++) maxA = std::max<int>(maxA, m2.image.rgba[static_cast<size_t>(i) * 4 + 3]);
    CHECK(maxA >= 100);
}

// ---------------------------------------------------------------------------
// Dynamic lights
// ---------------------------------------------------------------------------

TEST_CASE("light list keeps at most 32 lights") {
    DynamicLightList list;
    for (int i = 0; i < 40; i++) {
        bool ok = list.add(DynamicLight::point({0, 0, 0}, {1, 1, 1}, 10.0f));
        CHECK(ok == (i < 32));
    }
    CHECK(list.size() == 32);
    list.clear();
    CHECK(list.size() == 0);
}

TEST_CASE("model light attenuation matches the hand computed ambient cube values") {
    const Vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    // Light 50 units along +X of a model at the origin, radius 100: att = 0.5.
    DynamicLight l = DynamicLight::point({50, 0, 0}, {2.0f, 1.0f, 0.5f}, 100.0f);
    Vec3 cube[6] = {};
    accumulateModelLights(&l, 1, {0, 0, 0}, axes, cube);
    // Axis 0: d = 1, k = (0.7 + 0.3) * 0.5 = 0.5 on the + slot.
    CHECK(cube[0].x == doctest::Approx(1.0f));
    CHECK(cube[0].y == doctest::Approx(0.5f));
    CHECK(cube[0].z == doctest::Approx(0.25f));
    CHECK(cube[3].x == 0.0f);
    // Axes 1 and 2: d = 0, the wrap term only: k = 0.3 * 0.5 = 0.15 on the + slots.
    CHECK(cube[1].x == doctest::Approx(0.3f));
    CHECK(cube[2].x == doctest::Approx(0.3f));
    CHECK(cube[4].x == 0.0f);
    CHECK(cube[5].x == 0.0f);

    // From the other side the -X slot gets it: light at -X.
    DynamicLight l2 = DynamicLight::point({-50, 0, 0}, {1, 1, 1}, 100.0f);
    Vec3 cube2[6] = {};
    accumulateModelLights(&l2, 1, {0, 0, 0}, axes, cube2);
    CHECK(cube2[3].x == doctest::Approx(0.5f));
    CHECK(cube2[0].x == 0.0f);

    // Outside the radius nothing is added; exactly at the radius the attenuation is zero.
    DynamicLight far = DynamicLight::point({120, 0, 0}, {1, 1, 1}, 100.0f);
    Vec3 cube3[6] = {};
    accumulateModelLights(&far, 1, {0, 0, 0}, axes, cube3);
    for (const Vec3& c : cube3) CHECK(c.x == 0.0f);

    // A diagonal light at distance 60 of radius 120: att = 0.5, d = (0.8, 0.6, 0).
    DynamicLight diag = DynamicLight::point({48, 36, 0}, {1, 1, 1}, 120.0f);
    Vec3 cube4[6] = {};
    accumulateModelLights(&diag, 1, {0, 0, 0}, axes, cube4);
    CHECK(cube4[0].x == doctest::Approx((0.7f * 0.8f + 0.3f) * 0.5f));
    CHECK(cube4[1].x == doctest::Approx((0.7f * 0.6f + 0.3f) * 0.5f));
    CHECK(cube4[2].x == doctest::Approx(0.3f * 0.5f));
}

TEST_CASE("spot light cone: outer cosine cuts, inner cosine ramps") {
    // 60 degree cone: outer cos(30), inner cos(24); pointing down from 50 above the origin.
    DynamicLight s = DynamicLight::spotLight({0, 0, 50}, {1, 1, 1}, 100.0f, {0, 0, -1}, 60.0f);
    CHECK(s.cosOuter == doctest::Approx(std::cos(degToRad(30.0f))));
    CHECK(s.cosInner == doctest::Approx(std::cos(degToRad(24.0f))));
    Vec3 up{0, 0, 1};
    // On the axis: full strength, att = 0.5, normal facing the light.
    CHECK(terrainLightFactor(s, {0, 0, 0}, up) == doctest::Approx(0.5f));
    // 27 degrees off the axis (between the cosines): spot = (c - outer) / (inner - outer).
    float ang = degToRad(27.0f);
    Vec3 target{50.0f * std::tan(ang), 0, 0};
    float dist = 50.0f / std::cos(ang);
    float c = std::cos(ang);
    float spot = (c - s.cosOuter) / (s.cosInner - s.cosOuter);
    float nd = 50.0f / dist; // dot(up, toLight)
    float expect = (0.7f * nd + 0.3f) * ((100.0f - dist) / 100.0f) * spot;
    CHECK(terrainLightFactor(s, target, up) == doctest::Approx(expect).epsilon(1e-4));
    // 40 degrees off the axis: outside the cone.
    Vec3 out{50.0f * std::tan(degToRad(40.0f)), 0, 0};
    CHECK(terrainLightFactor(s, out, up) == 0.0f);
}

TEST_CASE("terrain light: hand computed factors and clamping") {
    DynamicLight l = DynamicLight::point({0, 0, 50}, {1.0f, 0.5f, 0.0f}, 100.0f);
    // Normal up, light straight above at 50: (0.7 * 1 + 0.3) * 0.5.
    CHECK(terrainLightFactor(l, {0, 0, 0}, {0, 0, 1}) == doctest::Approx(0.5f));
    // Facing away: nothing.
    CHECK(terrainLightFactor(l, {0, 0, 0}, {0, 0, -1}) == 0.0f);
    // Tilted normal: dot = cos 45.
    Vec3 tilt = normalize(Vec3{1, 0, 1});
    Vec3 target{-50.0f, 0.0f, 0.0f};
    float dist = std::sqrt(50.0f * 50.0f + 50.0f * 50.0f);
    float nd = dot(tilt, normalize(l.position - target));
    CHECK(terrainLightFactor(l, target, tilt) == doctest::Approx((0.7f * nd + 0.3f) * (100.0f - dist) / 100.0f));
    // The vertex colour is baked + colour * factor, clamped to 1.
    Vec3 c = lightTerrainVertex(&l, 1, {0, 0, 0}, {0, 0, 1}, {0.4f, 0.9f, 0.2f});
    CHECK(c.x == doctest::Approx(0.9f));
    CHECK(c.y == doctest::Approx(1.0f)); // 0.9 + 0.25 clamped
    CHECK(c.z == doctest::Approx(0.2f));
}

// ---------------------------------------------------------------------------
// Environment maps
// ---------------------------------------------------------------------------

TEST_CASE("sphere map coordinates against hand computed values") {
    // A vertex straight ahead with a normal facing the eye maps to the centre.
    Vec2 c = sphereMapUv({0, 0, -10}, {0, 0, 1});
    CHECK(c.x == doctest::Approx(0.5f));
    CHECK(c.y == doctest::Approx(0.5f));
    // n = (0.6, 0, 0.8): r = (0.96, 0, 0.28), m = 3.2, s = 0.96 / 3.2 + 0.5 = 0.8.
    Vec2 a = sphereMapUv({0, 0, -10}, {0.6f, 0.0f, 0.8f});
    CHECK(a.x == doctest::Approx(0.8f));
    CHECK(a.y == doctest::Approx(0.5f));
    Vec2 b = sphereMapUv({0, 0, -10}, {0.0f, -0.6f, 0.8f});
    CHECK(b.x == doctest::Approx(0.5f));
    CHECK(b.y == doctest::Approx(0.2f));
    // The unit vector to the vertex matters, not its distance.
    Vec2 d = sphereMapUv({0, 0, -400}, {0.6f, 0.0f, 0.8f});
    CHECK(d.x == doctest::Approx(a.x));
}

TEST_CASE("environment map helpers: folded normals, quad rotation, mode table") {
    Vec3 f = foldEnvNormal({-0.5f, 0.25f, -1.0f});
    CHECK(f.x == 0.5f);
    CHECK(f.y == 0.25f);
    CHECK(f.z == 1.0f);

    Vec2 r0 = rotateQuadEnvUv({1.0f, 0.0f}, 0.0f);
    CHECK(r0.x == doctest::Approx(1.0f));
    Vec2 r3 = rotateQuadEnvUv({1.0f, 0.0f}, 3.0f); // 90 degrees after 3 seconds
    CHECK(r3.x == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(r3.y == doctest::Approx(1.0f));
    Vec2 r12 = rotateQuadEnvUv({1.0f, 0.0f}, 12.0f); // 360 degrees
    CHECK(r12.x == doctest::Approx(1.0f).epsilon(1e-4));

    CHECK(envMapModeFromInt(0) == EnvMapMode::None);
    CHECK(envMapModeFromInt(1) == EnvMapMode::Glitter);
    CHECK(envMapModeFromInt(2) == EnvMapMode::Chrome);
    CHECK(envMapModeFromInt(3) == EnvMapMode::Quad);
    CHECK(envMapModeFromInt(9) == EnvMapMode::None);

    // Identity model, view moving the eye back: the normal matrix leaves the folded normal alone.
    Mat3 nm = envNormalMatrix(translation({0, 0, -5}), Mat4::identity());
    Vec3 n = nm * Vec3{0.0f, 0.0f, 1.0f};
    CHECK(n.z == doctest::Approx(1.0f));
    // A uniform model scale s gives normals of length 1/s (not renormalised).
    Mat3 ns = envNormalMatrix(Mat4::identity(), scale({2, 2, 2}));
    CHECK(length(ns * Vec3{0, 0, 1}) == doctest::Approx(0.5f));
}

TEST_CASE("environment combine modes") {
    Vec4 skin{0.5f, 0.25f, 0.125f, 0.5f};
    Vec4 env{0.25f, 0.25f, 0.5f, 1.0f};
    Vec4 col{1.0f, 0.5f, 1.0f, 1.0f};
    Vec4 g = combineEnvMap(EnvMapMode::Glitter, skin, env, col);
    CHECK(g.x == doctest::Approx(0.5f + 0.25f));
    CHECK(g.y == doctest::Approx(0.125f + 0.25f));
    CHECK(g.z == doctest::Approx(0.125f + 0.5f));
    CHECK(g.w == doctest::Approx(0.5f));
    Vec4 q = combineEnvMap(EnvMapMode::Quad, skin, env, col);
    CHECK(q.x == doctest::Approx(0.25f));
    CHECK(q.y == doctest::Approx(0.125f));
    CHECK(q.z == doctest::Approx(0.5f));
    Vec4 ch = combineEnvMap(EnvMapMode::Chrome, skin, env, col);
    CHECK(ch.x == doctest::Approx(0.5f * 0.5f + 0.25f * 1.0f * 0.5f));
    Vec4 none = combineEnvMap(EnvMapMode::None, skin, env, col);
    CHECK(none.x == doctest::Approx(0.5f));
}

// ---------------------------------------------------------------------------
// Sprites
// ---------------------------------------------------------------------------

TEST_CASE("sprite frame grid and corner order") {
    SpriteInstance s;
    s.frameCols = 10;
    s.frameRows = 1;
    s.frame = 3;
    Vec2 uv[4];
    spriteUvs(s, uv);
    // s0 = 0.3, s1 = 0.4; the single row spans t = 0..1, so v = 1 - t goes 1 (bottom) to 0 (top).
    CHECK(uv[0].x == doctest::Approx(0.3f));
    CHECK(uv[1].x == doctest::Approx(0.4f));
    CHECK(uv[0].y == doctest::Approx(1.0f));
    CHECK(uv[2].y == doctest::Approx(0.0f));

    // 3 x 4 grid, frame 5: column 2, row 1 from the top: t0 = 1 - 1/4 - 1/4 = 0.5.
    s.frameCols = 3;
    s.frameRows = 4;
    s.frame = 5;
    spriteUvs(s, uv);
    CHECK(uv[0].x == doctest::Approx(2.0f / 3.0f));
    CHECK(uv[0].y == doctest::Approx(1.0f - 0.5f));
    CHECK(uv[2].y == doctest::Approx(1.0f - 0.75f));

    // Without a grid the object's own s/t are used: t = 0 at the bottom -> v = 1.
    SpriteInstance p;
    p.minS = 0.0f; p.minT = 0.0f; p.maxS = 0.5f; p.maxT = 1.0f;
    spriteUvs(p, uv);
    CHECK(uv[1].x == doctest::Approx(0.5f));
    CHECK(uv[0].y == doctest::Approx(1.0f));
    CHECK(uv[3].y == doctest::Approx(0.0f));
}

TEST_CASE("sprite placement: billboard, horizontal and vertical") {
    SpriteInstance s;
    s.origin = {10, 20, 30};
    s.minX = -2; s.minY = -1; s.maxX = 2; s.maxY = 1;
    Vec3 right{1, 0, 0}, up{0, 0, 1};
    SpriteQuad q = buildSpriteQuad(s, right, up);
    CHECK(q.pos[0].x == doctest::Approx(8.0f));
    CHECK(q.pos[0].z == doctest::Approx(29.0f));
    CHECK(q.pos[2].x == doctest::Approx(12.0f));
    CHECK(q.pos[2].z == doctest::Approx(31.0f));
    // A 90 degree view-plane rotation turns (2, 1) into (-1, 2).
    s.yawDegrees = 90.0f;
    q = buildSpriteQuad(s, right, up);
    CHECK(q.pos[2].x == doctest::Approx(10.0f - 1.0f));
    CHECK(q.pos[2].z == doctest::Approx(30.0f + 2.0f));
    // Scale applies to billboards only.
    s.yawDegrees = 0.0f;
    s.scale = 2.0f;
    q = buildSpriteQuad(s, right, up);
    CHECK(q.pos[2].x == doctest::Approx(14.0f));
    s.kind = SpriteKind::Horizontal;
    q = buildSpriteQuad(s, right, up);
    CHECK(q.pos[2].x == doctest::Approx(12.0f));
    CHECK(q.pos[2].y == doctest::Approx(21.0f));
    CHECK(q.pos[2].z == doctest::Approx(30.0f));
    s.kind = SpriteKind::Vertical;
    q = buildSpriteQuad(s, right, up);
    CHECK(q.pos[2].x == doctest::Approx(12.0f));
    CHECK(q.pos[2].y == doctest::Approx(20.0f));
    CHECK(q.pos[2].z == doctest::Approx(31.0f)); // local y is height

    // Billboard axes are rows 0 and 1 of the view rotation.
    Mat4 view = lookAt({0, -100, 100}, {0, 0, 0}, {0, 0, 1});
    Vec3 r, u;
    spriteBillboardAxes(view, r, u);
    CHECK(r.x == doctest::Approx(1.0f));
    CHECK(u.z > 0.5f);
}

// ---------------------------------------------------------------------------
// Headless renders of a real level
// ---------------------------------------------------------------------------

namespace {

struct RenderEnv {
    Vfs vfs;
    DefDatabase db;
    std::unique_ptr<ResourceCache> cache;
    MeshRenderer renderer;
    bool ok = false;
};

RenderEnv* renderEnv() {
    static RenderEnv env;
    if (!env.ok) {
        for (const char* name : {"pak0.apk", "pak1.apk", "pak2.apk"}) {
            auto src = makePakSource(openFileStream(testdata::installDir() + "/data/" + name));
            if (!src) return nullptr;
            env.vfs.mount(std::move(src));
        }
        if (!env.db.load(env.vfs)) return nullptr;
        env.cache.reset(new ResourceCache(env.vfs));
        std::string error;
        if (!env.renderer.init(&error)) return nullptr;
        env.ok = true;
    }
    return &env;
}

viewer::LevelRenderOptions plainOptions() {
    viewer::LevelRenderOptions o;
    o.width = 320;
    o.height = 240;
    o.scroll = 1500.0f;
    o.msaa = 0;
    o.shadows = o.sprites = o.marks = o.dataLights = o.envmaps = false;
    return o;
}

Image renderPlain(RenderEnv& e, const char* level, const viewer::LevelRenderOptions& o, viewer::LevelRenderStats* st = nullptr) {
    Image img;
    viewer::LevelRenderStats local;
    std::string error;
    bool ok = viewer::renderLevel(e.vfs, e.db, *e.cache, e.renderer, level, o, img, st ? st : &local, error);
    REQUIRE_MESSAGE(ok, error);
    return img;
}

int luma(const Image& img, int i) {
    return (img.rgba[static_cast<size_t>(i) * 4] * 30 + img.rgba[static_cast<size_t>(i) * 4 + 1] * 59 +
            img.rgba[static_cast<size_t>(i) * 4 + 2] * 11) / 100;
}

} // namespace

#define REQUIRE_RENDER(envVar)                                                                  \
    AS3D_REQUIRE_DATA();                                                                        \
    AS3D_REQUIRE_PLAYABLE();                                                                    \
    REQUIRE_GL(glCtx);                                                                          \
    RenderEnv* envVar = renderEnv();                                                            \
    REQUIRE(envVar != nullptr)

TEST_CASE("render: a dynamic light brightens its surroundings and nothing else") {
    REQUIRE_RENDER(e);
    viewer::LevelRenderOptions o = plainOptions();
    Image base = renderPlain(*e, "1", o);
    o.extraLights.push_back(DynamicLight::point({640.0f, 1650.0f, 120.0f}, {2.0f, 2.0f, 2.0f}, 300.0f));
    Image lit = renderPlain(*e, "1", o);
    REQUIRE(lit.width == base.width);
    int n = base.width * base.height;
    long brighter = 0, darker = 0, unchanged = 0;
    double sumBase = 0, sumLit = 0;
    for (int i = 0; i < n; i++) {
        int d = luma(lit, i) - luma(base, i);
        if (d > 2) brighter++;
        else if (d < -2) darker++;
        else unchanged++;
        sumBase += luma(base, i);
        sumLit += luma(lit, i);
    }
    CHECK(brighter > n / 20);        // a visible pool
    CHECK(darker == 0);              // lights only add
    CHECK(unchanged > n / 5);        // and it is local
    CHECK(sumLit > sumBase * 1.02);
}

TEST_CASE("render: shadows darken the ground by at most 40 percent") {
    REQUIRE_RENDER(e);
    viewer::LevelRenderOptions o = plainOptions();
    Image base = renderPlain(*e, "1", o);
    int n = base.width * base.height;
    // One shadow at a time: dst * (1 - a) with a <= 0.4 (plus a fog tint of at most a * fog
    // colour), so no channel falls below 60 percent. Overlapping shadows multiply.
    viewer::LevelRenderStats count;
    renderPlain(*e, "1", [&] { auto q = o; q.shadows = true; return q; }(), &count);
    REQUIRE(count.shadowsDrawn > 10);
    long visibleShadows = 0;
    for (int first = 0; first < count.shadowsDrawn; first += 5) {
        o.shadows = true;
        o.shadowFirst = first;
        o.shadowLimit = 1;
        Image sh = renderPlain(*e, "1", o);
        long darker = 0;
        for (int i = 0; i < n; i++) {
            for (int k = 0; k < 3; k++) {
                int b = base.rgba[static_cast<size_t>(i) * 4 + k], s = sh.rgba[static_cast<size_t>(i) * 4 + k];
                CHECK(s >= static_cast<int>(static_cast<float>(b) * 0.6f) - 3);
            }
            if (luma(sh, i) < luma(base, i) - 3) darker++;
        }
        if (darker > 20) visibleShadows++;
    }
    CHECK(visibleShadows >= 2);
    o.shadowFirst = 0;
    o.shadowLimit = -1;
    viewer::LevelRenderStats all;
    Image sh = renderPlain(*e, "1", o, &all);
    CHECK(all.shadowsDrawn > 10);
    long darker = 0;
    for (int i = 0; i < n; i++)
        if (luma(sh, i) < luma(base, i) - 4) darker++;
    CHECK(darker > n / 100);
}

TEST_CASE("render: ground marks multiply the terrain darker") {
    REQUIRE_RENDER(e);
    viewer::LevelRenderOptions o = plainOptions();
    Image base = renderPlain(*e, "1", o);
    o.marks = true;
    o.extraMarks = "demo";
    viewer::LevelRenderStats st;
    Image marked = renderPlain(*e, "1", o, &st);
    CHECK(st.marksDrawn == 7);
    int n = base.width * base.height;
    long darker = 0, brighter = 0;
    for (int i = 0; i < n; i++) {
        int d = luma(marked, i) - luma(base, i);
        if (d < -8) darker++;
        if (d > 2) brighter++;
    }
    CHECK(darker > 50);
    CHECK(brighter == 0); // BLEND_FILTER only multiplies
}

TEST_CASE("render: night level gets lights, sprites and environment maps") {
    REQUIRE_RENDER(e);
    viewer::LevelRenderOptions o = plainOptions();
    Image base = renderPlain(*e, "20", o);
    o.sprites = o.dataLights = o.envmaps = true;
    viewer::LevelRenderStats st;
    Image full = renderPlain(*e, "20", o, &st);
    CHECK(st.lightsUsed > 0);
    CHECK(st.lightsUsed <= kMaxDynamicLights);
    CHECK(st.spritesDrawn > 0);
    int n = base.width * base.height;
    long brighter = 0, darker = 0;
    for (int i = 0; i < n; i++) {
        int d = luma(full, i) - luma(base, i);
        if (d > 6) brighter++;
        if (d < -20) darker++;
    }
    CHECK(brighter > n / 50);
    CHECK(darker < n / 100);

    // Environment maps alone change the pixels of the mapped models.
    viewer::LevelRenderOptions oe = plainOptions();
    oe.envmaps = true;
    Image env = renderPlain(*e, "20", oe, &st);
    CHECK(st.envParts > 0);
    long changed = 0;
    for (int i = 0; i < n; i++)
        if (std::abs(luma(env, i) - luma(base, i)) > 2) changed++;
    CHECK(changed > 20);
}
