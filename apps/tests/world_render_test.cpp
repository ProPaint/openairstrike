// WorldRenderer::prepare / draw (M0.5 of the 3DS port): the eye matrices and the widened cull
// volume (as3d/world_render.h, engine/src/render/eye_view.cpp) need neither GL nor data; the
// render tests need the first game's data and a headless GLES context, and skip loudly
// without them.
#include "doctest.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "as3d/defs.h"
#include "as3d/gfx.h"
#include "as3d/input.h"
#include "as3d/platform.h"
#include "as3d/vfs.h"
#include "as3d/world.h"
#include "as3d/world_render.h"
#include "test_data.h"

using namespace as3d;

namespace {

bool sameBits(const Mat4& a, const Mat4& b) { return std::memcmp(a.m, b.m, sizeof a.m) == 0; }

// A camera like the game's: tilted down, a little yaw, somewhere over a level.
// The field of view is read from a volatile so that the reference projection's tangent is
// computed at run time by the C library, as the engine's is (worldViewOf, eyeProjection): a
// literal lets the compiler fold std::tan at build time, correctly rounded, which differs by a
// bit from an older glibc's tanf, and the bit-for-bit checks below then fail on that machine.
volatile float g_testFovY = 60.0f;

WorldView testView(float aspect) {
    WorldView v;
    v.eye = Vec3{320.0f, 1200.0f, 420.0f};
    v.fovY = g_testFovY;
    v.view = rotationX(-50.0f) * rotationY(0.0f) * rotationZ(7.0f) * translation(-v.eye);
    v.projection = perspective(v.fovY, aspect, v.nearPlane, v.farPlane);
    return v;
}

// The world point at eye-space (x, y, z) of `v` (rows of the view rotation: right, up, back).
Vec3 worldPoint(const WorldView& v, float x, float y, float z) {
    const Vec3 right{v.view.at(0, 0), v.view.at(1, 0), v.view.at(2, 0)};
    const Vec3 up{v.view.at(0, 1), v.view.at(1, 1), v.view.at(2, 1)};
    const Vec3 back{v.view.at(0, 2), v.view.at(1, 2), v.view.at(2, 2)};
    return v.eye + right * x + up * y + back * z;
}

float ndcX(const Mat4& proj, const Mat4& view, const Vec3& p) {
    const Vec4 c = proj * (view * Vec4{p.x, p.y, p.z, 1.0f});
    return c.x / c.w;
}

bool sameStats(const WorldRenderStats& a, const WorldRenderStats& b) {
    return a.models == b.models && a.dropped == b.dropped && a.sprites == b.sprites && a.marks == b.marks &&
           a.shadows == b.shadows && a.lights == b.lights && a.emitters == b.emitters && a.particles == b.particles &&
           a.bolts == b.bolts && a.skidTrails == b.skidTrails && a.waterChunks == b.waterChunks &&
           a.healthBars == b.healthBars && a.terrainChunks == b.terrainChunks;
}

bool samePixels(const Image& a, const Image& b) {
    return a.width == b.width && a.height == b.height && a.rgba == b.rgba;
}

GraphicsContext* glContext() {
    static std::unique_ptr<GraphicsContext> ctx = [] {
        GraphicsConfig cfg;
        cfg.width = 64;
        cfg.height = 64;
        cfg.headless = true;
        cfg.title = "as3d_tests";
        return createGraphicsContext(cfg);
    }();
    if (ctx) ctx->makeCurrent();
    return ctx.get();
}

} // namespace

TEST_CASE("eye matrices: EyeView{} is the world camera bit for bit") {
    const float aspect = 4.0f / 3.0f;
    const WorldView v = testView(aspect);
    CHECK(sameBits(eyeViewMatrix(v, EyeView{}), v.view));
    CHECK(sameBits(eyeProjection(v, EyeView{}, aspect), v.projection));
    // A focal distance alone does not move the eye.
    EyeView focusOnly;
    focusOnly.focalDistance = 300.0f;
    CHECK(sameBits(eyeProjection(v, focusOnly, aspect), v.projection));
    CHECK(sameBits(eyeViewMatrix(v, focusOnly), v.view));
    const Vec3 p = eyePosition(v, EyeView{});
    CHECK(p.x == v.eye.x);
    CHECK(p.y == v.eye.y);
    CHECK(p.z == v.eye.z);
}

TEST_CASE("eye matrices: parallel eyes are the camera moved along its right axis") {
    const float aspect = 400.0f / 240.0f;
    const WorldView v = testView(aspect);
    const float e = 3.0f;
    const EyeView right{e, 0.0f};
    // Parallel eyes keep the symmetric projection.
    CHECK(sameBits(eyeProjection(v, right, aspect), v.projection));
    // The eye position maps to the origin of the eye's view, and the eye looks the same way.
    const Mat4 ev = eyeViewMatrix(v, right);
    const Vec3 pos = eyePosition(v, right);
    const Vec4 o = ev * Vec4{pos.x, pos.y, pos.z, 1.0f};
    CHECK(o.x == doctest::Approx(0.0f).epsilon(1e-3));
    CHECK(o.y == doctest::Approx(0.0f).epsilon(1e-3));
    CHECK(o.z == doctest::Approx(0.0f).epsilon(1e-3));
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 4; ++r) CHECK(ev.at(c, r) == v.view.at(c, r));
    // A point ahead of the camera moves left in the right eye's image, by A e / depth.
    const Vec3 ahead = worldPoint(v, 0.0f, 0.0f, -100.0f);
    const float a = v.projection.at(0, 0);
    CHECK(ndcX(v.projection, ev, ahead) == doctest::Approx(-a * e / 100.0f).epsilon(1e-4));
}

TEST_CASE("eye matrices: converging eyes are mirror off-axis frusta with zero parallax at the focal distance") {
    const float aspect = 400.0f / 240.0f;
    const WorldView v = testView(aspect);
    const float e = 2.5f, d = 250.0f;
    const EyeView left{-e, d}, right{e, d};
    const Mat4 pl = eyeProjection(v, left, aspect), pr = eyeProjection(v, right, aspect);
    // Only the off-axis term differs from the symmetric projection, with opposite signs.
    for (int i = 0; i < 16; ++i) {
        if (i == 2 * 4 + 0) continue; // at(2, 0)
        CHECK(pl.m[i] == v.projection.m[i]);
        CHECK(pr.m[i] == v.projection.m[i]);
    }
    const float a = v.projection.at(0, 0);
    CHECK(pr.at(2, 0) == doctest::Approx(-a * e / d));
    CHECK(pl.at(2, 0) == -pr.at(2, 0));
    CHECK(pr.at(2, 0) != 0.0f);
    // Zero parallax straight ahead at the focal distance, in both eyes.
    const Mat4 vl = eyeViewMatrix(v, left), vr = eyeViewMatrix(v, right);
    const Vec3 focus = worldPoint(v, 0.0f, 0.0f, -d);
    CHECK(ndcX(pl, vl, focus) == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(ndcX(pr, vr, focus) == doctest::Approx(0.0f).epsilon(1e-4));
    // Mirror images: a point at eye-space x maps to -x_ndc of the point at -x in the other eye.
    for (float x : {-80.0f, -5.0f, 0.0f, 30.0f}) {
        for (float z : {-20.0f, -250.0f, -900.0f}) {
            const float l = ndcX(pl, vl, worldPoint(v, x, 10.0f, z));
            const float r = ndcX(pr, vr, worldPoint(v, -x, 10.0f, z));
            CHECK(l == doctest::Approx(-r).epsilon(1e-3));
        }
    }
    // Nearer than the focal distance comes out of the screen (crossed: right image left of
    // the left image), farther goes in.
    const Vec3 nearP = worldPoint(v, 0.0f, 0.0f, -50.0f), farP = worldPoint(v, 0.0f, 0.0f, -800.0f);
    CHECK(ndcX(pr, vr, nearP) < ndcX(pl, vl, nearP));
    CHECK(ndcX(pr, vr, farP) > ndcX(pl, vl, farP));
    // The image shift form: x_ndc(off-axis) = x_ndc(symmetric, same eye) - B.
    const Vec3 q = worldPoint(v, 17.0f, -4.0f, -333.0f);
    CHECK(ndcX(pr, vr, q) == doctest::Approx(ndcX(v.projection, vr, q) - pr.at(2, 0)).epsilon(1e-4));
}

TEST_CASE("eye cull volume: the camera's frustum at offset 0, widened for the eyes") {
    const float aspect = 4.0f / 3.0f;
    const WorldView v = testView(aspect);
    const float tanX = std::tan(degToRad(v.fovY) * 0.5f) * aspect;
    const CullFrustum mono = eyeCullFrustum(v, aspect);
    // Agrees with the projection: points just inside and just outside the clip volume.
    for (float depth : {10.0f, 300.0f, 1500.0f}) {
        const float edge = tanX * depth;
        CHECK(mono.sphereVisible(worldPoint(v, edge * 0.98f, 0.0f, -depth), 0.0f));
        CHECK_FALSE(mono.sphereVisible(worldPoint(v, edge * 1.02f, 0.0f, -depth), 0.0f));
        CHECK_FALSE(mono.sphereVisible(worldPoint(v, -edge * 1.02f, 0.0f, -depth), 0.0f));
    }
    CHECK_FALSE(mono.sphereVisible(worldPoint(v, 0.0f, 0.0f, -1.0f), 0.5f));    // before the near plane
    CHECK_FALSE(mono.sphereVisible(worldPoint(v, 0.0f, 0.0f, -2100.0f), 50.0f)); // beyond the far plane
    CHECK(mono.sphereVisible(worldPoint(v, 0.0f, 0.0f, -2020.0f), 30.0f));       // touches it

    // A sphere of radius 1 just outside the right edge: dropped by the camera's frustum, kept
    // by the volume of eyes up to 10 units apart.
    const float m = 10.0f, depth = 100.0f;
    const Vec3 c = worldPoint(v, tanX * depth + 5.0f, 0.0f, -depth);
    CHECK_FALSE(mono.sphereVisible(c, 1.0f));
    const CullFrustum wide = eyeCullFrustum(v, aspect, m);
    CHECK(wide.sphereVisible(c, 1.0f));
    // ...and the right eye's own frustum does see it (its projection puts it on screen).
    const EyeView re{m, 0.0f};
    const Vec4 clip = eyeProjection(v, re, aspect) * (eyeViewMatrix(v, re) * Vec4{c.x, c.y, c.z, 1.0f});
    CHECK(std::fabs(clip.x / clip.w) < 1.0f);
    // The same on the left.
    const Vec3 cl = worldPoint(v, -tanX * depth - 5.0f, 0.0f, -depth);
    CHECK_FALSE(mono.sphereVisible(cl, 1.0f));
    CHECK(wide.sphereVisible(cl, 1.0f));
    // Top and bottom do not move.
    const float tanY = std::tan(degToRad(v.fovY) * 0.5f);
    CHECK_FALSE(wide.sphereVisible(worldPoint(v, 0.0f, tanY * depth * 1.05f, -depth), 0.0f));

    // Converging eyes see farther sideways beyond the focal distance: the volume follows.
    const float focal = 50.0f, far = 1500.0f;
    const EyeView le{-m, focal};
    // A point the left eye sees at its right edge, far away.
    const Mat4 pl = eyeProjection(v, le, aspect), vl = eyeViewMatrix(v, le);
    // x_ndc = A x_eye / far - B = 0.99, x_eye = x + m.
    const float xEdge = -m + (pl.at(2, 0) + 0.99f) * far / pl.at(0, 0);
    const Vec3 p = worldPoint(v, xEdge, 0.0f, -far);
    CHECK(ndcX(pl, vl, p) == doctest::Approx(0.99f).epsilon(1e-3));
    CHECK_FALSE(eyeCullFrustum(v, aspect, m).sphereVisible(p, 0.0f)); // parallel widening misses it
    CHECK(eyeCullFrustum(v, aspect, m, focal).sphereVisible(p, 0.0f));
}

TEST_CASE("world renderer: prepare once, draw twice; render() is prepare + draw(EyeView{})") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    AS3D_REQUIRE_FIRST_GAME("mission 1 scene");
    GraphicsContext* ctx = glContext();
    if (!ctx) {
        std::fprintf(stderr, "SKIPPED (no headless GLES context available): %s\n", __FILE__);
        return;
    }
    Vfs vfs;
    vfs.mount(makeDirSource(testdata::extractedDir()));
    DefDatabase db;
    REQUIRE(db.load(vfs));
    World w;
    WorldConfig cfg;
    cfg.godMode = true;
    w.init(vfs, db, cfg);
    std::string err;
    REQUIRE_MESSAGE(w.loadLevel("1", &err), err);
    // Into the level: enemies, shots, particles and marks on screen.
    for (u32 f = 0; f < 600; ++f) w.step(botInput(w, f).toPlayerInput());

    WorldRenderer renderer;
    REQUIRE_MESSAGE(renderer.init(vfs, db, &err), err);
    REQUIRE_MESSAGE(renderer.beginLevel(w, &err), err);
    const int W = 400, H = 240;
    RenderTarget target;
    REQUIRE(target.create(W, H, 0));
    // A first frame so that the shadow silhouettes and marks of this scene exist.
    target.bind();
    renderer.render(w, W, H);

    Image ref, a, b;
    target.bind();
    renderer.render(w, W, H);
    REQUIRE(target.readPixels(ref));
    const WorldRenderStats refStats = renderer.lastStats();
    CHECK(refStats.models > 10);
    CHECK(refStats.terrainChunks > 0);

    Viewport vp;
    vp.width = W;
    vp.height = H;
    renderer.prepare(w, vp);
    target.bind();
    renderer.draw();
    REQUIRE(target.readPixels(a));
    const WorldRenderStats sa = renderer.lastStats();
    target.bind();
    renderer.draw(EyeView{});
    REQUIRE(target.readPixels(b));
    const WorldRenderStats sb = renderer.lastStats();
    CHECK(samePixels(a, ref));
    CHECK(samePixels(b, ref));
    CHECK(sameStats(sa, refStats));
    CHECK(sameStats(sb, refStats));

    // Stereo: one prepare announcing the eyes, each eye drawn twice gives the same image and
    // stats; the eyes differ from each other and from the mono image.
    for (float focal : {0.0f, 400.0f}) {
        INFO("focal distance " << focal);
        Viewport sv = vp;
        sv.maxEyeOffset = 4.0f;
        sv.minFocalDistance = focal;
        renderer.prepare(w, sv);
        const EyeView le{-4.0f, focal}, re{4.0f, focal};
        Image l1, r1, l2, r2;
        target.bind();
        renderer.draw(le);
        REQUIRE(target.readPixels(l1));
        const WorldRenderStats sl1 = renderer.lastStats();
        target.bind();
        renderer.draw(re);
        REQUIRE(target.readPixels(r1));
        target.bind();
        renderer.draw(le);
        REQUIRE(target.readPixels(l2));
        const WorldRenderStats sl2 = renderer.lastStats();
        target.bind();
        renderer.draw(re);
        REQUIRE(target.readPixels(r2));
        CHECK(samePixels(l1, l2));
        CHECK(samePixels(r1, r2));
        CHECK(sameStats(sl1, sl2));
        CHECK_FALSE(samePixels(l1, r1));
        CHECK_FALSE(samePixels(l1, ref));
        CHECK(sl1.models == refStats.models);
        CHECK(sl1.sprites == refStats.sprites);
    }
    // And mono again after the stereo frames: still the reference.
    target.bind();
    renderer.render(w, W, H);
    Image again;
    REQUIRE(target.readPixels(again));
    CHECK(samePixels(again, ref));
}
