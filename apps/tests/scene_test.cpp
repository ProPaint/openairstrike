// Tests for as3d::MeshRenderer / Material / ResourceCache / Camera (as3d/scene.h). Like
// apps/tests/gfx_test.cpp, these create a real headless GLES 3.0 context; on a machine
// where none is available every TEST_CASE prints a loud SKIPPED message and passes, but
// on this development machine (and in CI) a context is always created and these tests
// actually run.
#include "doctest.h"

#include "as3d/scene.h"

#include "as3d/defs.h"

#include <GLES3/gl3.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "as3d/image.h"
#include "as3d/platform.h"
#include "as3d/vfs.h"
#include "test_data.h"

using namespace as3d;

namespace {

GraphicsContext* headlessContext() {
    static std::unique_ptr<GraphicsContext> ctx = [] {
        GraphicsConfig cfg;
        cfg.width = 256;
        cfg.height = 256;
        cfg.headless = true;
        cfg.title = "as3d_tests (scene)";
        return createGraphicsContext(cfg);
    }();
    if (ctx) ctx->makeCurrent();
    return ctx.get();
}

#define AS3D_REQUIRE_HEADLESS_GL(ctxVar)                                                     \
    GraphicsContext* ctxVar = headlessContext();                                             \
    if (!ctxVar) {                                                                           \
        std::fprintf(stderr, "SKIPPED (no headless GLES context available): %s\n", __FILE__); \
        return;                                                                              \
    }

struct Px {
    int r, g, b, a;
};

Px pixelAt(const Image& img, int x, int y) {
    size_t idx = (static_cast<size_t>(y) * img.width + x) * 4;
    return {img.rgba[idx + 0], img.rgba[idx + 1], img.rgba[idx + 2], img.rgba[idx + 3]};
}

bool approxColor(Px p, int r, int g, int b, int tol) {
    return std::abs(p.r - r) <= tol && std::abs(p.g - g) <= tol && std::abs(p.b - b) <= tol;
}

// One 1x1 white texel: modulating by it is a no-op, isolating lighting/blend math from
// any real texture content.
Texture2D makeWhiteTexture() {
    Image img;
    img.width = 1;
    img.height = 1;
    img.hasAlpha = false;
    img.rgba = {255, 255, 255, 255};
    Texture2D tex;
    TextureOptions opts;
    opts.minFilter = Filter::Nearest;
    opts.magFilter = Filter::Nearest;
    tex.create(img, opts);
    return tex;
}

// A single flat quad (2 triangles) centred at `center`, lying in the plane
// perpendicular to `normal`, `halfExtent` wide along the other two axes. Built directly
// (not through ModelData/buildRenderMesh) so these tests control geometry exactly.
GpuMesh makeQuad(const Vec3& center, const Vec3& normal, const Vec3& axisU, const Vec3& axisV,
                  float halfExtent) {
    GpuMesh mesh;
    RenderVertex v[4];
    Vec3 corners[4] = {center - axisU * halfExtent - axisV * halfExtent,
                        center + axisU * halfExtent - axisV * halfExtent,
                        center + axisU * halfExtent + axisV * halfExtent,
                        center - axisU * halfExtent + axisV * halfExtent};
    Vec2 uvs[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (int i = 0; i < 4; i++) {
        v[i].position = corners[i];
        v[i].normal = normal;
        v[i].uv = uvs[i];
    }
    u16 indices[6] = {0, 1, 2, 0, 2, 3};
    mesh.vbo.upload(v, sizeof v);
    mesh.ibo.upload(indices, sizeof indices, IndexType::U16);
    VertexLayout layout;
    layout.strideBytes = sizeof(RenderVertex);
    layout.attribs = {
        {0, 3, offsetof(RenderVertex, position), false},
        {1, 3, offsetof(RenderVertex, normal), false},
        {2, 2, offsetof(RenderVertex, uv), false},
    };
    mesh.vao.create(mesh.vbo, layout, &mesh.ibo);
    mesh.indexCount = 6;
    mesh.valid = true;
    return mesh;
}

Material makeMaterial(const Texture2D& tex, MaterialBlend blend, const Vec4& tint, bool noLighting,
                       SortBucket bucket) {
    Material mat;
    mat.texture = &tex;
    mat.blend = blend;
    mat.tint = tint;
    mat.noLighting = noLighting;
    mat.sortBucket = bucket;
    return mat;
}

RenderTarget makeTarget(int size = 128) {
    RenderTarget target;
    target.create(size, size, 0);
    return target;
}

Image renderOneFrame(RenderTarget& target, const Vec4& background, MeshRenderer& renderer, const Camera& cam,
                      const SceneLighting& lighting,
                      const std::vector<std::tuple<const GpuMesh*, const Material*, Mat4>>& items) {
    target.bind();
    setViewport(0, 0, target.width(), target.height());
    clear(background, /*depth=*/true);
    renderer.begin(cam, lighting);
    for (const auto& it : items) renderer.submit(*std::get<0>(it), *std::get<1>(it), std::get<2>(it));
    renderer.end();
    Image img;
    target.readPixels(img);
    return img;
}

} // namespace

TEST_CASE("Material::fromObjectDef maps blend/rflag/sort per docs/spec/obj.md") {
    AS3D_REQUIRE_HEADLESS_GL(ctx);
    Vfs vfs; // empty: no game data needed, def.skin is empty -> placeholder texture
    ResourceCache cache(vfs);

    ObjectDef def;
    def.blend = BlendMode::Add;
    def.rflag = RF_NOLIGHTING | RF_NOCULLING | RF_NODEPTHTEST | RF_NODEPTHWRITE;
    def.sort = SortMode::Trans;

    Material mat = Material::fromObjectDef(def, cache);
    CHECK(mat.blend == MaterialBlend::Add);
    CHECK(mat.noLighting);
    CHECK(mat.noCulling);
    CHECK(mat.noDepthTest);
    CHECK(mat.noDepthWrite);
    CHECK(mat.sortBucket == SortBucket::Trans);
    CHECK(mat.texture != nullptr); // placeholder, never null
}

TEST_CASE("MeshRenderer: a face toward the sun is brighter than a face away from it") {
    AS3D_REQUIRE_HEADLESS_GL(ctx);
    RenderTarget target = makeTarget();
    Texture2D white = makeWhiteTexture();

    MeshRenderer renderer;
    std::string error;
    REQUIRE(renderer.init(&error));

    // Camera at (0,-5,0) looking toward +Y, up = +Z: screen-right = +X, screen-up = +Z.
    Camera cam;
    cam.eye = Vec3{0, -5, 0};
    cam.target = Vec3{0, 0, 0};
    cam.worldUp = Vec3{0, 0, 1};
    cam.aspect = 1.0f;

    SceneLighting lighting;
    lighting.sunDirection = Vec3{0, -1, 0}; // "toward the sun" points back at the camera
    lighting.sunColor = Vec3{1, 1, 1};
    lighting.ambientColor = Vec3{0.05f, 0.05f, 0.05f};
    lighting.fogStart = 100000.0f;
    lighting.fogEnd = 200000.0f; // effectively no fog at this scale

    // Two flat quads, both perpendicular to Y (facing the camera axis), one normal
    // toward the sun (-Y) and one away (+Y); both disable culling so a normal pointing
    // "away" from the camera still renders (this test is about lighting, not culling).
    GpuMesh towardSun = makeQuad(Vec3{-1.5f, 0, 0}, Vec3{0, -1, 0}, Vec3{1, 0, 0}, Vec3{0, 0, 1}, 0.8f);
    GpuMesh awayFromSun = makeQuad(Vec3{1.5f, 0, 0}, Vec3{0, 1, 0}, Vec3{1, 0, 0}, Vec3{0, 0, 1}, 0.8f);

    Material mat = makeMaterial(white, MaterialBlend::Opaque, Vec4{1, 1, 1, 1}, /*noLighting=*/false,
                                 SortBucket::Opaque);
    mat.noCulling = true;

    Image img = renderOneFrame(target, Vec4{0, 0, 0, 1}, renderer, cam, lighting,
                                {{&towardSun, &mat, Mat4::identity()}, {&awayFromSun, &mat, Mat4::identity()}});

    Px brightPx = pixelAt(img, img.width / 4, img.height / 2);      // over the -1.5 quad
    Px darkPx = pixelAt(img, (img.width * 3) / 4, img.height / 2);  // over the +1.5 quad
    CHECK(brightPx.r > darkPx.r + 40);
    CHECK(brightPx.g > darkPx.g + 40);
    CHECK(brightPx.b > darkPx.b + 40);
}

TEST_CASE("MeshRenderer: additive blending brightens the background") {
    AS3D_REQUIRE_HEADLESS_GL(ctx);
    RenderTarget target = makeTarget();
    Texture2D white = makeWhiteTexture();
    MeshRenderer renderer;
    std::string error;
    REQUIRE(renderer.init(&error));

    Camera cam;
    cam.eye = Vec3{0, -5, 0};
    cam.target = Vec3{0, 0, 0};
    cam.worldUp = Vec3{0, 0, 1};
    cam.aspect = 1.0f;
    SceneLighting lighting;
    lighting.fogStart = 100000.0f;
    lighting.fogEnd = 200000.0f;

    Vec4 background{0.2f, 0.2f, 0.2f, 1.0f};
    GpuMesh backQuad = makeQuad(Vec3{0, 0.1f, 0}, Vec3{0, -1, 0}, Vec3{1, 0, 0}, Vec3{0, 0, 1}, 1.0f);
    GpuMesh frontQuad = makeQuad(Vec3{0, -0.1f, 0}, Vec3{0, -1, 0}, Vec3{1, 0, 0}, Vec3{0, 0, 1}, 1.0f);

    Material bgMat = makeMaterial(white, MaterialBlend::Opaque, Vec4{background.x, background.y, background.z, 1},
                                   /*noLighting=*/true, SortBucket::Opaque);
    Material addMat =
        makeMaterial(white, MaterialBlend::Add, Vec4{0.3f, 0.3f, 0.3f, 1.0f}, /*noLighting=*/true, SortBucket::Effect);

    // Submitted transparent-before-opaque on purpose: end() must still draw opaque
    // first (see the sort-order test below for a direct check of this).
    Image img = renderOneFrame(target, background, renderer, cam, lighting,
                                {{&frontQuad, &addMat, Mat4::identity()}, {&backQuad, &bgMat, Mat4::identity()}});

    Px center = pixelAt(img, img.width / 2, img.height / 2);
    // Expected: background (0.2) + additive (0.3) = 0.5, clamped to 255*0.5 = 127-128.
    CHECK(approxColor(center, 128, 128, 128, 12));
    CHECK(center.r > static_cast<int>(background.x * 255.0f) + 40); // strictly brighter than plain background
}

TEST_CASE("MeshRenderer: alpha blending over a known background gives the expected colour") {
    AS3D_REQUIRE_HEADLESS_GL(ctx);
    RenderTarget target = makeTarget();
    Texture2D white = makeWhiteTexture();
    MeshRenderer renderer;
    std::string error;
    REQUIRE(renderer.init(&error));

    Camera cam;
    cam.eye = Vec3{0, -5, 0};
    cam.target = Vec3{0, 0, 0};
    cam.worldUp = Vec3{0, 0, 1};
    cam.aspect = 1.0f;
    SceneLighting lighting;
    lighting.fogStart = 100000.0f;
    lighting.fogEnd = 200000.0f;

    Vec4 background{0.2f, 0.2f, 0.2f, 1.0f};
    GpuMesh backQuad = makeQuad(Vec3{0, 0.1f, 0}, Vec3{0, -1, 0}, Vec3{1, 0, 0}, Vec3{0, 0, 1}, 1.0f);
    GpuMesh frontQuad = makeQuad(Vec3{0, -0.1f, 0}, Vec3{0, -1, 0}, Vec3{1, 0, 0}, Vec3{0, 0, 1}, 1.0f);

    Material bgMat = makeMaterial(white, MaterialBlend::Opaque, Vec4{background.x, background.y, background.z, 1},
                                   /*noLighting=*/true, SortBucket::Opaque);
    // Pure red at alpha 0.5.
    Material alphaMat =
        makeMaterial(white, MaterialBlend::Alpha, Vec4{1.0f, 0.0f, 0.0f, 0.5f}, /*noLighting=*/true, SortBucket::Trans);

    Image img = renderOneFrame(target, background, renderer, cam, lighting,
                                {{&frontQuad, &alphaMat, Mat4::identity()}, {&backQuad, &bgMat, Mat4::identity()}});

    // GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA: result = src*a + dst*(1-a).
    float expectedR = 1.0f * 0.5f + background.x * 0.5f;
    float expectedG = 0.0f * 0.5f + background.y * 0.5f;
    float expectedB = 0.0f * 0.5f + background.z * 0.5f;
    Px center = pixelAt(img, img.width / 2, img.height / 2);
    CHECK(approxColor(center, static_cast<int>(expectedR * 255), static_cast<int>(expectedG * 255),
                       static_cast<int>(expectedB * 255), 12));
}

TEST_CASE("MeshRenderer: fog shifts a distant object's colour toward the fog colour") {
    AS3D_REQUIRE_HEADLESS_GL(ctx);
    RenderTarget target = makeTarget();
    Texture2D white = makeWhiteTexture();
    MeshRenderer renderer;
    std::string error;
    REQUIRE(renderer.init(&error));

    Camera cam;
    cam.eye = Vec3{0, 0, 0};
    cam.target = Vec3{0, 1, 0};
    cam.worldUp = Vec3{0, 0, 1};
    cam.aspect = 1.0f;
    cam.farPlane = 5000.0f;

    SceneLighting lighting;
    lighting.fogColor = Vec3{0.9f, 0.1f, 0.1f}; // distinctive red fog, easy to detect
    lighting.fogStart = 10.0f;
    lighting.fogEnd = 50.0f;
    lighting.ambientColor = Vec3{1, 1, 1};
    lighting.sunColor = Vec3{0, 0, 0};

    // Near quad (well inside fogStart -> unfogged) and far quad (well beyond fogEnd ->
    // fully fogged), both bright green so the fog shift is unmistakable.
    GpuMesh nearQuad = makeQuad(Vec3{0, 5, 0}, Vec3{0, -1, 0}, Vec3{1, 0, 0}, Vec3{0, 0, 1}, 2.0f);
    GpuMesh farQuad = makeQuad(Vec3{0, 500, 0}, Vec3{0, -1, 0}, Vec3{1, 0, 0}, Vec3{0, 0, 1}, 200.0f);
    Material greenMat =
        makeMaterial(white, MaterialBlend::Opaque, Vec4{0.1f, 0.9f, 0.1f, 1.0f}, /*noLighting=*/true, SortBucket::Opaque);

    Image nearImg = renderOneFrame(target, Vec4{0, 0, 0, 1}, renderer, cam, lighting,
                                    {{&nearQuad, &greenMat, Mat4::identity()}});
    Image farImg = renderOneFrame(target, Vec4{0, 0, 0, 1}, renderer, cam, lighting,
                                   {{&farQuad, &greenMat, Mat4::identity()}});

    Px nearPx = pixelAt(nearImg, nearImg.width / 2, nearImg.height / 2);
    Px farPx = pixelAt(farImg, farImg.width / 2, farImg.height / 2);

    CHECK(nearPx.g > nearPx.r + 40); // near: unfogged, dominant green
    CHECK(approxColor(farPx, 230, 25, 25, 25)); // far: ~fully shifted to the fog colour
}

TEST_CASE("MeshRenderer: opaque draws before transparent regardless of submission order "
          "(SORT_OPAQUE, SORT_TRANS, SORT_EFFECT)") {
    AS3D_REQUIRE_HEADLESS_GL(ctx);
    RenderTarget target = makeTarget();
    Texture2D white = makeWhiteTexture();
    MeshRenderer renderer;
    std::string error;
    REQUIRE(renderer.init(&error));

    Camera cam;
    cam.eye = Vec3{0, -5, 0};
    cam.target = Vec3{0, 0, 0};
    cam.worldUp = Vec3{0, 0, 1};
    cam.aspect = 1.0f;
    SceneLighting lighting;
    lighting.fogStart = 100000.0f;
    lighting.fogEnd = 200000.0f;

    Vec4 background{0.0f, 0.0f, 0.0f, 1.0f};
    // Two OPAQUE quads at different depths (so if they drew in submission order instead
    // of an opaque-then-transparent bucket order that also respects depth, the wrong
    // one could win); plus one transparent quad blended on top of whichever opaque quad
    // is nearest, submitted FIRST (out of bucket order) to prove end() reorders it.
    GpuMesh farOpaque = makeQuad(Vec3{0, 1.0f, 0}, Vec3{0, -1, 0}, Vec3{1, 0, 0}, Vec3{0, 0, 1}, 1.0f);
    GpuMesh nearOpaque = makeQuad(Vec3{0, -1.0f, 0}, Vec3{0, -1, 0}, Vec3{1, 0, 0}, Vec3{0, 0, 1}, 1.0f);
    GpuMesh transQuad = makeQuad(Vec3{0, -2.0f, 0}, Vec3{0, -1, 0}, Vec3{1, 0, 0}, Vec3{0, 0, 1}, 1.0f);

    Material farMat =
        makeMaterial(white, MaterialBlend::Opaque, Vec4{1, 0, 0, 1}, /*noLighting=*/true, SortBucket::Opaque);
    Material nearMat =
        makeMaterial(white, MaterialBlend::Opaque, Vec4{0, 1, 0, 1}, /*noLighting=*/true, SortBucket::Opaque);
    Material transMat =
        makeMaterial(white, MaterialBlend::Alpha, Vec4{0, 0, 1, 0.5f}, /*noLighting=*/true, SortBucket::Trans);

    Image img = renderOneFrame(target, background, renderer, cam, lighting,
                                {{&transQuad, &transMat, Mat4::identity()}, // submitted first
                                 {&farOpaque, &farMat, Mat4::identity()},
                                 {&nearOpaque, &nearMat, Mat4::identity()}});

    Px center = pixelAt(img, img.width / 2, img.height / 2);
    // Depth testing must have already resolved nearOpaque (green) as the visible
    // opaque surface (closer to the camera than farOpaque/red); the transparent blue
    // quad (drawn after, per SORT_TRANS) then blends on top of it: expect a
    // green+blue mix, with NO red contribution at all (red would only appear if
    // opaque ordering/depth were wrong).
    CHECK(center.r < 40);
    CHECK(center.g > 80);
    CHECK(center.b > 80);
}

TEST_CASE("Culling: CullMode::Back (this engine's chosen setting) shows a CCW-wound "
          "triangle's front but not its back") {
    AS3D_REQUIRE_HEADLESS_GL(ctx);
    RenderTarget target = makeTarget();
    Texture2D white = makeWhiteTexture();
    MeshRenderer renderer;
    std::string error;
    REQUIRE(renderer.init(&error));

    // A triangle wound (v0, v1, v2) counter-clockwise as seen from +Z looking toward
    // -Z, matching buildRenderMesh's own winding convention (docs/spec/mdl.md: CCW in
    // object space as seen from the side the normal points to).
    GpuMesh tri;
    RenderVertex v[3];
    v[0].position = Vec3{-0.6f, -0.6f, 0.0f};
    v[1].position = Vec3{0.6f, -0.6f, 0.0f};
    v[2].position = Vec3{0.0f, 0.6f, 0.0f};
    for (auto& vertex : v) {
        vertex.normal = Vec3{0, 0, 1};
        vertex.uv = Vec2{0, 0};
    }
    u16 idx[3] = {0, 1, 2};
    tri.vbo.upload(v, sizeof v);
    tri.ibo.upload(idx, sizeof idx, IndexType::U16);
    VertexLayout layout;
    layout.strideBytes = sizeof(RenderVertex);
    layout.attribs = {
        {0, 3, offsetof(RenderVertex, position), false},
        {1, 3, offsetof(RenderVertex, normal), false},
        {2, 2, offsetof(RenderVertex, uv), false},
    };
    tri.vao.create(tri.vbo, layout, &tri.ibo);
    tri.indexCount = 3;
    tri.valid = true;

    Material mat = makeMaterial(white, MaterialBlend::Opaque, Vec4{1, 1, 1, 1}, true, SortBucket::Opaque);
    mat.noCulling = false; // exercise the real applyMaterialState() cull decision

    SceneLighting lighting;
    lighting.fogStart = 100000.0f;
    lighting.fogEnd = 200000.0f;
    Vec4 background{0, 0, 0, 1};

    Camera front;
    front.eye = Vec3{0, 0, 5};
    front.target = Vec3{0, 0, 0};
    front.worldUp = Vec3{0, 1, 0};
    front.aspect = 1.0f;
    Image frontImg = renderOneFrame(target, background, renderer, front, lighting, {{&tri, &mat, Mat4::identity()}});

    Camera back;
    back.eye = Vec3{0, 0, -5};
    back.target = Vec3{0, 0, 0};
    back.worldUp = Vec3{0, 1, 0};
    back.aspect = 1.0f;
    Image backImg = renderOneFrame(target, background, renderer, back, lighting, {{&tri, &mat, Mat4::identity()}});

    Px frontCenter = pixelAt(frontImg, frontImg.width / 2, frontImg.height * 3 / 5);
    Px backCenter = pixelAt(backImg, backImg.width / 2, backImg.height * 3 / 5);
    CHECK(approxColor(frontCenter, 255, 255, 255, 20)); // front face visible (white, lit flat)
    CHECK(approxColor(backCenter, 0, 0, 0, 5));          // back face culled -> background shows through
}

// ---------------------------------------------------------------------------------
// Smoke test over real game data.
// ---------------------------------------------------------------------------------

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

int countCloseTo(const Image& img, int r, int g, int b, int tol) {
    int count = 0;
    size_t n = static_cast<size_t>(img.width) * static_cast<size_t>(img.height);
    for (size_t i = 0; i < n; i++) {
        int pr = img.rgba[i * 4 + 0], pg = img.rgba[i * 4 + 1], pb = img.rgba[i * 4 + 2];
        if (std::abs(pr - r) <= tol && std::abs(pg - g) <= tol && std::abs(pb - b) <= tol) count++;
    }
    return count;
}
} // namespace

TEST_CASE("Smoke test: 20 assorted real objects render visibly with no unexpected placeholder texture") {
    AS3D_REQUIRE_HEADLESS_GL(ctx);
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();

    Vfs vfs;
    REQUIRE(mountOriginalPaks(vfs));
    DefDatabase db;
    REQUIRE(db.load(vfs));

    // Objects whose skin is one of docs/spec/obj.md's 13 documented unresolved
    // references: rendering these is EXPECTED to show the magenta placeholder.
    const std::set<std::string> allowedMagenta = {"expl_wave", "expl_wave_big", "p_expl_wave", "expl1_hit"};

    std::vector<std::string> selection;
    std::set<std::string> seen;
    for (const ObjectDef& o : db.objects()) {
        if (o.model.empty() || o.skin.empty() || o.name.empty()) continue;
        if (!seen.insert(o.name).second) continue;
        selection.push_back(o.name);
        if (selection.size() >= 19) break;
    }
    // Deliberately include one of the documented broken-skin objects, if present, to
    // exercise the placeholder path for real rather than only by construction.
    if (db.findObject("expl_wave")) selection.push_back("expl_wave");
    REQUIRE(selection.size() >= 10);

    ResourceCache cache(vfs);
    MeshRenderer renderer;
    std::string error;
    REQUIRE(renderer.init(&error));

    RenderTarget target = makeTarget(160);
    Vec4 background{0.10f, 0.10f, 0.12f, 1.0f};
    SceneLighting lighting = SceneLighting::fromMission1(db);

    int checked = 0;
    for (const std::string& name : selection) {
        const ObjectDef* def = db.findObject(name);
        REQUIRE(def != nullptr);
        const GpuMesh& mesh = cache.mesh(def->model);
        if (!mesh.valid) continue; // one of the 2 corrupt / 4 empty models; nothing to frame

        Material mat = Material::fromObjectDef(*def, cache);
        // Single-sided panels (e.g. boss1's doors) are invisible from their back with
        // back-face culling, so try a second view from the opposite side.
        double differingFraction = 0.0;
        Image img;
        int totalPixels = 0;
        for (float yaw : {35.0f, 215.0f}) {
            Camera cam = Camera::framing(mesh.model.boundsMin, mesh.model.boundsMax, yaw, 25.0f, 1.0f, 1.0f);
            target.bind();
            setViewport(0, 0, target.width(), target.height());
            clear(background, true);
            renderer.begin(cam, lighting);
            renderer.submit(mesh, mat, Mat4::identity());
            renderer.end();
            REQUIRE(target.readPixels(img));
            totalPixels = img.width * img.height;
            int bgPixels = countCloseTo(img, static_cast<int>(background.x * 255), static_cast<int>(background.y * 255),
                                         static_cast<int>(background.z * 255), 6);
            differingFraction = 1.0 - static_cast<double>(bgPixels) / totalPixels;
            if (differingFraction > 0.02) break;
        }
        INFO("object: ", name, " differing fraction: ", differingFraction);
        CHECK(differingFraction > 0.02); // the object is visible and framed

        int magentaPixels = countCloseTo(img, 255, 0, 255, 30);
        double magentaFraction = static_cast<double>(magentaPixels) / totalPixels;
        if (allowedMagenta.count(name) == 0) {
            INFO("object: ", name, " magenta fraction: ", magentaFraction);
            CHECK(magentaFraction < 0.02);
        }
        checked++;
    }
    CHECK(checked >= 8);
    std::fprintf(stderr, "scene smoke test: rendered %d/%zu selected objects\n", checked, selection.size());
}
