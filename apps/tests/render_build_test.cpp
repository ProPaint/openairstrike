// Neutral render builders (particle, sprite, lightning, decal, skid): vertex counts, run splits
// and merges, hand-computed corners and UVs, and run keys equal to the GL state the passes
// used before the builders were split out. No GL context, no game data.
#include "doctest.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "as3d/draw_run.h"
#include "as3d/ground_marks.h"
#include "as3d/level.h"
#include "as3d/lightning_render.h"
#include "as3d/particle_render.h"
#include "../../engine/src/render/particle_quads.h"
#include "as3d/skid_render.h"
#include "as3d/sprite_render.h"
#include "as3d/terrain.h"

using namespace as3d;

namespace {

constexpr float kEps = 1e-4f;

// Two distinct texture handles; the builders only compare and copy the pointers.
Texture2D gTexA, gTexB;

struct FlatTerrain {
    LevelData level;
    Terrain terrain;
    FlatTerrain(int w, int h, u8 height) {
        level.width = static_cast<u32>(w);
        level.height = static_cast<u32>(h);
        level.cells.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
        for (auto& c : level.cells) c.height = height;
        TerrainStyle style;
        style.hmin = 0.0f;
        style.hmax = 255.0f;
        REQUIRE(terrain.build(level, style));
    }
};

ParticleDesc particleDesc(const char* texture, ParticleBlend blend, bool noDepthTest) {
    ParticleDesc d;
    d.name = "SYNTH";
    d.texture = texture;
    d.blend = blend;
    d.noDepthTest = noDepthTest;
    d.emitRate = 10;
    d.lifeTime = 1.0f;
    d.initSize[0] = 3;
    d.initColor[0] = 1.0f;
    d.initColor[1] = 0.5f;
    d.initColor[2] = 0.25f;
    d.initColor[3] = 0.75f;
    return d;
}

void runFor(ParticleEmitter& e, float seconds) {
    for (int i = 0, n = static_cast<int>(seconds * 60.0f); i < n; i++) e.update(1.0f / 60.0f);
}

const Particle* firstLive(const ParticleEmitter& e) {
    for (const Particle& p : e.particles())
        if (e.isLive(p)) return &p;
    return nullptr;
}

int liveCount(const ParticleEmitter& e) {
    int n = 0;
    for (const Particle& p : e.particles()) n += e.isLive(p) ? 1 : 0;
    return n;
}

const Texture2D* resolveTex(const std::string& path) { return path == "a" ? &gTexA : &gTexB; }

} // namespace

TEST_CASE("particles: quads, runs and keys") {
    ParticleDesc dNone = particleDesc("a", ParticleBlend::None, false);
    ParticleDesc dAlphaB = particleDesc("b", ParticleBlend::Alpha, false);
    ParticleDesc dAdd = particleDesc("a", ParticleBlend::Add, true);
    ParticleDesc dFilter = particleDesc("a", ParticleBlend::Filter, false);
    ParticleEmitter e1(dNone, 1), e2(dNone, 2), e3(dAlphaB, 3), e4(dAdd, 4), e5(dFilter, 5);
    ParticleEmitter* all[] = {&e1, &e2, &e3, &e4, &e5};
    for (ParticleEmitter* e : all) runFor(*e, 0.5f);
    const ParticleEmitter* list[] = {&e1, &e2, &e3, &e4, &e5};
    int total = 0;
    for (auto* e : list) {
        REQUIRE(liveCount(*e) > 0);
        total += liveCount(*e);
    }

    std::vector<ParticleVertex> verts;
    std::vector<DrawRun> runs;
    int built = buildParticleQuads(list, 5, Mat4::identity(), resolveTex, verts, runs);
    CHECK(built == total);
    CHECK(verts.size() == static_cast<size_t>(total) * 6);

    // e1 and e2 share blend, depth flag and texture: one run; then alpha/b, add/a, filter/a.
    REQUIRE(runs.size() == 4);
    CHECK(runs[0].first == 0);
    CHECK(runs[0].count == (liveCount(e1) + liveCount(e2)) * 6);
    CHECK(runs[0].key.blend == RenderBlend::None);
    CHECK(runs[0].key.depthTest);
    CHECK_FALSE(runs[0].key.depthWrite);
    CHECK(runs[0].key.cull == CullMode::Back);
    CHECK_FALSE(runs[0].key.polygonOffset);
    CHECK(runs[0].key.fog == FogColorMode::Level);
    CHECK(runs[0].texture == &gTexA);
    CHECK(runs[0].texMode == kTexModulate);
    CHECK(runs[1].texture == &gTexB);
    CHECK(runs[1].key.blend == RenderBlend::Alpha);
    CHECK(runs[2].key.blend == RenderBlend::Add);
    CHECK_FALSE(runs[2].key.depthTest);
    CHECK_FALSE(runs[2].key.depthWrite);
    CHECK(runs[2].key.fog == FogColorMode::Black);
    CHECK(runs[3].key.blend == RenderBlend::Filter);
    CHECK(runs[3].key.fog == FogColorMode::White);
    CHECK(runs[3].texMode == kTexClampAdd);
    int sum = 0;
    for (const DrawRun& r : runs) {
        CHECK(r.first == sum);
        sum += r.count;
    }
    CHECK(sum == static_cast<int>(verts.size()));
}

TEST_CASE("particles: emitter bounds hold every live particle, the cull keeps what touches the view") {
    // Decart: the sphere holds every live world position plus the rotated quad's reach.
    ParticleDesc d = particleDesc("a", ParticleBlend::Alpha, false);
    ParticleEmitter e(d, 7, {100.0f, -20.0f, 5.0f});
    Vec3 c;
    float r = -1.0f;
    CHECK_FALSE(particleEmitterBounds(e, c, r)); // nothing live yet
    runFor(e, 0.5f);
    REQUIRE(liveCount(e) > 0);
    REQUIRE(particleEmitterBounds(e, c, r));
    CHECK(r > 0.0f);
    for (const Particle& p : e.particles()) {
        if (!e.isLive(p)) continue;
        const Vec3 w = e.worldPosition(p);
        const float dx = w.x - c.x, dy = w.y - c.y, dz = w.z - c.z;
        CHECK(std::sqrt(dx * dx + dy * dy + dz * dz) + std::fabs(p.size) * 1.4142f <= r + 1e-3f);
    }
    // Cilinder and Sphere: around the origin, within the radial reach.
    for (ParticleCoords coords : {ParticleCoords::Cilinder, ParticleCoords::Sphere}) {
        ParticleDesc dc = d;
        dc.coords = coords;
        ParticleEmitter ec(dc, 3, {-7.0f, 8.0f, 9.0f});
        runFor(ec, 0.5f);
        REQUIRE(liveCount(ec) > 0);
        Vec3 cc;
        float rc = 0.0f;
        REQUIRE(particleEmitterBounds(ec, cc, rc));
        CHECK(cc.x == -7.0f);
        CHECK(cc.y == 8.0f);
        CHECK(cc.z == 9.0f);
        for (const Particle& p : ec.particles()) {
            if (!ec.isLive(p)) continue;
            const Vec3 w = ec.worldPosition(p);
            const float dx = w.x - cc.x, dy = w.y - cc.y, dz = w.z - cc.z;
            CHECK(std::sqrt(dx * dx + dy * dy + dz * dz) + std::fabs(p.size) * 1.4142f <= rc + 1e-3f);
        }
    }
}

TEST_CASE("particles: the description's reach bounds a still emitter and the radial ones without a walk") {
    // Offsets, velocity ranges, acceleration and growth all in play.
    ParticleDesc d = particleDesc("a", ParticleBlend::Alpha, false);
    d.emitRate = 60;
    d.lifeTime = 0.5f;
    d.initOffset[0] = 4.0f, d.initOffset[1] = -3.0f, d.initOffset[2] = 2.0f;
    d.initOffset[3] = 10.0f, d.initOffset[4] = 5.0f, d.initOffset[5] = 1.0f;
    d.initVelocity[0] = 30.0f, d.initVelocity[1] = -20.0f, d.initVelocity[2] = 50.0f;
    d.initVelocity[3] = 15.0f, d.initVelocity[4] = 15.0f, d.initVelocity[5] = 5.0f;
    d.accel[2] = -100.0f;
    d.initSize[0] = 3.0f, d.initSize[1] = 2.0f;
    d.sizeRate = 4.0f;
    auto holds = [](const ParticleEmitter& e, const Vec3& c, float r) {
        int checked = 0;
        for (const Particle& p : e.particles()) {
            if (!e.isLive(p)) continue;
            const Vec3 w = e.worldPosition(p);
            const float dx = w.x - c.x, dy = w.y - c.y, dz = w.z - c.z;
            CHECK(std::sqrt(dx * dx + dy * dy + dz * dz) + std::fabs(p.size) * 1.4142f <= r + 1e-3f);
            ++checked;
        }
        return checked;
    };
    // Still for three lives: the analytic sphere around the origin plus the mean offset.
    ParticleEmitter still(d, 11, {100.0f, 200.0f, 10.0f});
    runFor(still, 1.5f);
    REQUIRE(still.stillSeconds() >= d.lifeTime);
    Vec3 c;
    float r = 0.0f;
    REQUIRE(particleEmitterBounds(still, c, r));
    CHECK(c.x == doctest::Approx(104.0f));
    CHECK(c.y == doctest::Approx(197.0f));
    CHECK(c.z == doctest::Approx(12.0f));
    CHECK(holds(still, c, r) > 0);
    // Moving every step: the exact walk, which still holds every particle.
    ParticleEmitter moving(d, 12, {0.0f, 0.0f, 0.0f});
    for (int i = 0; i < 90; i++) {
        moving.setOrigin({static_cast<float>(i) * 3.0f, 0.0f, 0.0f});
        moving.update(1.0f / 60.0f);
    }
    REQUIRE(moving.stillSeconds() < d.lifeTime);
    Vec3 cm;
    float rm = 0.0f;
    REQUIRE(particleEmitterBounds(moving, cm, rm));
    CHECK(holds(moving, cm, rm) > 0);
    // Cilinder and Sphere with radial velocity and growth: the origin plus the radial reach.
    for (ParticleCoords coords : {ParticleCoords::Cilinder, ParticleCoords::Sphere}) {
        ParticleDesc dc = d;
        dc.coords = coords;
        ParticleEmitter ec(dc, 13, {-7.0f, 8.0f, 9.0f});
        for (int i = 0; i < 90; i++) { // moving too: radial particles follow the origin
            ec.setOrigin({-7.0f + static_cast<float>(i), 8.0f, 9.0f});
            ec.update(1.0f / 60.0f);
        }
        Vec3 cc;
        float rc = 0.0f;
        REQUIRE(particleEmitterBounds(ec, cc, rc));
        CHECK(cc.x == ec.origin().x);
        CHECK(holds(ec, cc, rc) > 0);
    }
}

TEST_CASE("particles: thinning spreads the cap over every emitter") {
    ParticleDesc d = particleDesc("a", ParticleBlend::Alpha, false);
    ParticleEmitter e1(d, 1), e2(d, 2), e3(d, 3);
    for (ParticleEmitter* e : {&e1, &e2, &e3}) runFor(*e, 0.5f);
    const ParticleEmitter* list[] = {&e1, &e2, &e3};
    const int live = liveCount(e1) + liveCount(e2) + liveCount(e3);
    REQUIRE(live >= 9);
    const int keep = live / 3;
    std::vector<ParticleVertex> verts;
    std::vector<DrawRun> runs;
    int quads = 0;
    // One run for all three (same key).
    int built = visitParticleQuads(
        list, 3, Mat4::identity(), resolveTex,
        [&](const ParticleQuad&) { verts.insert(verts.end(), 6, ParticleVertex{}); quads++; },
        [&]() { return verts.size(); }, runs, ParticleThin{keep, live});
    CHECK(built == keep);
    CHECK(quads == keep);
    CHECK(verts.size() == static_cast<size_t>(keep) * 6);
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].count == keep * 6);
    // Each emitter alone keeps about a third of its particles (within one).
    for (const ParticleEmitter* e : list) {
        std::vector<ParticleVertex> v1;
        std::vector<DrawRun> r1;
        const int n = liveCount(*e);
        int b = visitParticleQuads(
            &e, 1, Mat4::identity(), resolveTex, [&](const ParticleQuad&) { v1.insert(v1.end(), 6, ParticleVertex{}); },
            [&]() { return v1.size(); }, r1, ParticleThin{n / 3, n});
        CHECK(b == n / 3);
    }
    // keep 0 or keep >= of: no thinning.
    verts.clear();
    runs.clear();
    built = visitParticleQuads(
        list, 3, Mat4::identity(), resolveTex, [&](const ParticleQuad&) { verts.insert(verts.end(), 6, ParticleVertex{}); },
        [&]() { return verts.size(); }, runs, ParticleThin{live, live});
    CHECK(built == live);
}

TEST_CASE("particles: the still emitters fade to the floor, the moving and one-shot ones keep every particle") {
    ParticleDesc d = particleDesc("a", ParticleBlend::Alpha, false);
    d.emitRate = 60;
    d.lifeTime = 1.0f;
    ParticleDesc once = d;
    once.emitOnce = true;
    ParticleEmitter still(d, 1), young(d, 2), moving(d, 3), burst(once, 4);
    runFor(still, 15.0f); // stood still for 15 s: past stillEnd
    runFor(young, 2.0f);  // 2 s: before stillStart
    for (int i = 0; i < 15 * 60; i++) { // moves every step: never still
        moving.setOrigin({static_cast<float>(i), 0.0f, 0.0f});
        moving.update(1.0f / 60.0f);
    }
    CHECK(still.stillSeconds() > 14.0f);
    CHECK(young.stillSeconds() < 3.0f);
    CHECK(moving.stillSeconds() < 0.02f);
    const ParticleEmitter* list[] = {&still, &young, &moving, &burst};
    ParticleFade fade;
    fade.enabled = true;
    std::vector<int> shares;
    int faded = -1;
    int built = particleThinShares(list, 4, fade, 0, shares, &faded);
    REQUIRE(shares.size() == 4);
    CHECK(shares[0] == kParticleThinUnit / 4);
    CHECK(shares[1] == kParticleThinUnit);
    CHECK(shares[2] == kParticleThinUnit);
    CHECK(shares[3] == kParticleThinUnit);
    const int liveStill = liveCount(still);
    CHECK(faded == liveStill - liveStill / 4 - (liveStill % 4 >= 2 ? 1 : 0)); // round(0.75 * live)
    CHECK(built == liveStill / 4 + liveCount(young) + liveCount(moving) + liveCount(burst));
    // The visitor builds exactly what the shares say, per emitter.
    std::vector<ParticleVertex> verts;
    std::vector<DrawRun> runs;
    ParticleThin thin;
    thin.shares = shares.data();
    int visited = visitParticleQuads(
        list, 4, Mat4::identity(), resolveTex, [&](const ParticleQuad&) { verts.insert(verts.end(), 6, ParticleVertex{}); },
        [&]() { return verts.size(); }, runs, thin);
    CHECK(visited == built);
    // Midway between stillStart and stillEnd the share is between the floor and one.
    ParticleEmitter mid(d, 5);
    runFor(mid, 8.5f);
    const ParticleEmitter* one[] = {&mid};
    particleThinShares(one, 1, fade, 0, shares);
    CHECK(shares[0] > kParticleThinUnit / 4);
    CHECK(shares[0] < kParticleThinUnit);
    // The fade off: every share one; a cap scales every share by the same factor and the total
    // built fits the cap.
    fade.enabled = false;
    built = particleThinShares(list, 4, fade, 0, shares, &faded);
    CHECK(faded == 0);
    for (int s : shares) CHECK(s == kParticleThinUnit);
    const int total = liveCount(still) + liveCount(young) + liveCount(moving) + liveCount(burst);
    CHECK(built == total);
    built = particleThinShares(list, 4, fade, total / 2, shares, &faded);
    CHECK(built <= total / 2);
    CHECK(built >= total / 2 - 4);
    for (int s : shares) CHECK(s == shares[0]);
    // The fade and the cap together: the still emitter keeps a quarter of the others' share.
    fade.enabled = true;
    built = particleThinShares(list, 4, fade, total / 2, shares, &faded);
    CHECK(built <= total / 2);
    CHECK(shares[1] == shares[2]);
    CHECK(shares[0] * 4 <= shares[1] + 4);
    CHECK(shares[0] * 4 >= shares[1] - 4);
    // A share of zero leaves the emitter out of the runs.
    shares = {0, kParticleThinUnit, kParticleThinUnit, kParticleThinUnit};
    verts.clear();
    runs.clear();
    thin.shares = shares.data();
    visited = visitParticleQuads(
        list, 4, Mat4::identity(), resolveTex, [&](const ParticleQuad&) { verts.insert(verts.end(), 6, ParticleVertex{}); },
        [&]() { return verts.size(); }, runs, thin);
    CHECK(visited == total - liveCount(still));
}

TEST_CASE("particles: billboard and flat corners") {
    // Frame 1 of a 2x2 sheet; the original's row index divides by rows: u0 = 0.5, v0 = 0.5 (1 / 2 rows = 0).
    ParticleDesc d = particleDesc("a", ParticleBlend::Alpha, false);
    d.texCols = 2;
    d.texRows = 2;
    d.initFrame[0] = 1;
    d.initFrame[1] = 0;
    ParticleEmitter e(d, 7, {10, 20, 30});
    runFor(e, 0.4f);
    const Particle* p = firstLive(e);
    REQUIRE(p);
    REQUIRE(p->frame == 1);
    const Vec3 P = e.worldPosition(*p);
    const float s = p->size;
    CHECK(s == doctest::Approx(3.0f));

    // Identity view: right (1,0,0), up (0,1,0).
    {
        std::vector<ParticleVertex> v;
        std::vector<DrawRun> r;
        const ParticleEmitter* one = &e;
        buildParticleQuads(&one, 1, Mat4::identity(), resolveTex, v, r);
        REQUIRE(v.size() >= 6);
        // Particle 0 of the pool may not be the first live one; locate the quad by position.
        size_t q = 0;
        for (; q + 6 <= v.size(); q += 6)
            if (std::fabs(v[q].x - (P.x - s)) < kEps && std::fabs(v[q].y - (P.y - s)) < kEps) break;
        REQUIRE(q + 6 <= v.size());
        const float cx[4] = {P.x - s, P.x + s, P.x + s, P.x - s};
        const float cy[4] = {P.y - s, P.y - s, P.y + s, P.y + s};
        const float cu[4] = {0.5f, 1.0f, 1.0f, 0.5f};
        const float cv[4] = {0.5f, 0.5f, 0.0f, 0.0f}; // 1 - t, t0 = 0.5, t1 = 1
        const int order[6] = {0, 1, 2, 0, 2, 3};
        for (int k = 0; k < 6; k++) {
            const ParticleVertex& pv = v[q + static_cast<size_t>(k)];
            CHECK(pv.x == doctest::Approx(cx[order[k]]));
            CHECK(pv.y == doctest::Approx(cy[order[k]]));
            CHECK(pv.z == doctest::Approx(P.z));
            CHECK(pv.u == doctest::Approx(cu[order[k]]));
            CHECK(pv.v == doctest::Approx(cv[order[k]]));
            CHECK(pv.r == doctest::Approx(p->rgba[0]));
            CHECK(pv.a == doctest::Approx(p->rgba[3]));
        }
    }
    // Flat quads ignore the view: Horiz X = (c, s, 0), Y = (-s, c, 0); Vert X = (c, 0, s),
    // Y = (-s, 0, c) with the particle's angle truncated to whole degrees.
    for (ParticleDrawMode mode : {ParticleDrawMode::Horiz, ParticleDrawMode::Vert}) {
        ParticleDesc df = d;
        df.drawMode = mode;
        ParticleEmitter ef(df, 7, {10, 20, 30});
        runFor(ef, 0.4f);
        const Particle* pf = firstLive(ef);
        REQUIRE(pf);
        const Vec3 Pf = ef.worldPosition(*pf);
        const float a = degToRad(static_cast<float>(static_cast<int>(pf->angle)));
        const float c = std::cos(a), sn = std::sin(a), sf = pf->size;
        const Vec3 X = mode == ParticleDrawMode::Horiz ? Vec3{c, sn, 0} : Vec3{c, 0, sn};
        const Vec3 Y = mode == ParticleDrawMode::Horiz ? Vec3{-sn, c, 0} : Vec3{-sn, 0, c};
        const Vec3 c0 = Pf - X * sf - Y * sf, c2 = Pf + X * sf + Y * sf;
        std::vector<ParticleVertex> v;
        std::vector<DrawRun> r;
        const ParticleEmitter* one = &ef;
        buildParticleQuads(&one, 1, rotationX(40.0f), resolveTex, v, r);
        size_t q = 0;
        for (; q + 6 <= v.size(); q += 6)
            if (std::fabs(v[q].x - c0.x) < kEps && std::fabs(v[q].y - c0.y) < kEps && std::fabs(v[q].z - c0.z) < kEps) break;
        REQUIRE(q + 6 <= v.size());
        CHECK(v[q + 2].x == doctest::Approx(c2.x));
        CHECK(v[q + 2].y == doctest::Approx(c2.y));
        CHECK(v[q + 2].z == doctest::Approx(c2.z));
    }
}

TEST_CASE("sprites: billboard quad, run splits and cap") {
    SpriteInstance a;
    a.origin = {10, 20, 30};
    a.colour = {0.1f, 0.2f, 0.3f, 0.4f};
    a.texture = &gTexA;
    a.blend = DecalBlend::Add;
    SpriteInstance b = a;
    SpriteInstance c = a;
    c.noDepthTest = true;
    SpriteInstance d = a;
    d.texture = &gTexB;
    SpriteInstance e = a;
    e.noDepthWrite = true;
    SpriteInstance none = a;
    none.texture = nullptr;
    SpriteInstance list[] = {a, b, none, c, d, e};

    std::vector<SpriteVertex> verts;
    std::vector<DrawRun> runs;
    int built = buildSpriteBatches(list, 6, Mat4::identity(), true, verts, runs);
    CHECK(built == 5);
    CHECK(verts.size() == 30);
    REQUIRE(runs.size() == 4); // a+b | c | d | e
    CHECK(runs[0].count == 12);
    CHECK(runs[1].count == 6);
    CHECK(runs[1].key.depthTest == false);
    CHECK(runs[1].key.depthWrite == true);
    CHECK(runs[2].texture == &gTexB);
    CHECK(runs[3].key.depthWrite == false);
    CHECK(runs[0].key.blend == RenderBlend::Add);
    CHECK(runs[0].key.fog == FogColorMode::Black);
    CHECK(runs[0].key.cull == CullMode::Back);
    CHECK(runs[0].key.depthTest);
    CHECK(runs[0].key.depthWrite);

    // Quad of the first sprite: corners (min..max) = (-8..8) around the origin, uv (0..1).
    const int order[6] = {0, 1, 2, 0, 2, 3};
    const float cx[4] = {2, 18, 18, 2}, cy[4] = {12, 12, 28, 28};
    const float cu[4] = {0, 1, 1, 0}, cv[4] = {1, 1, 0, 0};
    for (int k = 0; k < 6; k++) {
        CHECK(verts[static_cast<size_t>(k)].x == doctest::Approx(cx[order[k]]));
        CHECK(verts[static_cast<size_t>(k)].y == doctest::Approx(cy[order[k]]));
        CHECK(verts[static_cast<size_t>(k)].z == doctest::Approx(30.0f));
        CHECK(verts[static_cast<size_t>(k)].u == doctest::Approx(cu[order[k]]));
        CHECK(verts[static_cast<size_t>(k)].v == doctest::Approx(cv[order[k]]));
        CHECK(verts[static_cast<size_t>(k)].r == doctest::Approx(0.1f));
        CHECK(verts[static_cast<size_t>(k)].a == doctest::Approx(0.4f));
    }

    // Unculled (the sequels) and the 512 record cap.
    verts.clear();
    runs.clear();
    std::vector<SpriteInstance> many(kMaxSprites + 88, a);
    built = buildSpriteBatches(many.data(), many.size(), Mat4::identity(), false, verts, runs);
    CHECK(built == static_cast<int>(kMaxSprites));
    CHECK(verts.size() == kMaxSprites * 6);
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].key.cull == CullMode::Off);
}

TEST_CASE("lightning: one bolt, quads and run") {
    const Vec3 start{0, 0, 0}, end{96, 0, 0};
    std::vector<LightningVertex> verts;
    std::vector<DrawRun> runs;
    CHECK(buildLightningBolts(&start, &end, 1, 0.0f, nullptr, verts, runs) == 0);
    CHECK(verts.empty());

    const Vec3 starts[3] = {start, {5, 5, 5}, start};
    const Vec3 ends[3] = {end, {5, 5, 5}, {0, 96, 0}}; // the middle bolt has zero length
    CHECK(buildLightningBolts(starts, ends, 3, 0.0f, &gTexA, verts, runs) == 2);
    CHECK(verts.size() == 24);
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].count == 24);
    CHECK(runs[0].texture == &gTexA);
    CHECK(runs[0].key.blend == RenderBlend::Add);
    CHECK_FALSE(runs[0].key.depthTest);
    CHECK_FALSE(runs[0].key.depthWrite);
    CHECK(runs[0].key.cull == CullMode::Off);
    CHECK(runs[0].key.fog == FogColorMode::Black);

    // Bolt along +X: p1 = +Y, p2 = +Z. First quad corners start-8p, end-8p, end+8p, start+8p.
    const float cx[4] = {0, 96, 96, 0}, cy[4] = {-8, -8, 8, 8};
    const float cu[4] = {0, 1, 1, 0}, cv[4] = {1, 1, 0, 0};
    const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int k = 0; k < 6; k++) {
        const LightningVertex& v = verts[static_cast<size_t>(k)];
        CHECK(v.pos.x == doctest::Approx(cx[order[k]]));
        CHECK(v.pos.y == doctest::Approx(cy[order[k]]));
        CHECK(v.pos.z == doctest::Approx(0.0f));
        CHECK(v.uv.x == doctest::Approx(cu[order[k]]));
        CHECK(v.uv.y == doctest::Approx(cv[order[k]]));
    }
    // Second quad of the bolt is crossed: it spreads along Z.
    CHECK(verts[6].pos.z == doctest::Approx(-8.0f));
    CHECK(verts[6].pos.y == doctest::Approx(0.0f));
}

TEST_CASE("decals: clipped by the terrain grid, runs") {
    FlatTerrain ft(8, 8, 50); // 8 x 8 cells of 40 units, z = 50
    // Inside one cell: area 200, uv from the rectangle.
    std::vector<DecalVertex> v;
    buildTerrainDecal(ft.terrain, GroundRect::axisAligned(50, 50, 70, 60), v);
    REQUIRE(!v.empty());
    REQUIRE(v.size() % 3 == 0);
    float area = 0;
    for (size_t i = 0; i + 2 < v.size(); i += 3)
        area += 0.5f * std::fabs((v[i + 1].x - v[i].x) * (v[i + 2].y - v[i].y) - (v[i + 2].x - v[i].x) * (v[i + 1].y - v[i].y));
    CHECK(area == doctest::Approx(200.0f).epsilon(1e-3));
    for (const DecalVertex& d : v) {
        CHECK(d.z == doctest::Approx(50.0f));
        CHECK(d.u == doctest::Approx((d.x - 50.0f) / 20.0f));
        CHECK(d.v == doctest::Approx(1.0f - (d.y - 50.0f) / 10.0f));
    }

    // Hanging over the west edge (x < 0): only the part on the grid is built.
    v.clear();
    buildTerrainDecal(ft.terrain, GroundRect::axisAligned(-30, 10, 20, 30), v);
    REQUIRE(!v.empty());
    area = 0;
    bool corner = false;
    for (size_t i = 0; i + 2 < v.size(); i += 3)
        area += 0.5f * std::fabs((v[i + 1].x - v[i].x) * (v[i + 2].y - v[i].y) - (v[i + 2].x - v[i].x) * (v[i + 1].y - v[i].y));
    for (const DecalVertex& d : v) {
        CHECK(d.x >= -kEps);
        if (std::fabs(d.x) < kEps && std::fabs(d.y - 10.0f) < kEps) {
            corner = true;
            CHECK(d.u == doctest::Approx(0.6f));
            CHECK(d.v == doctest::Approx(1.0f));
        }
    }
    CHECK(area == doctest::Approx(400.0f).epsilon(1e-3));
    CHECK(corner);

    // Runs: textureless and empty batches are dropped, equal neighbours merge, marks carry
    // (colour, alpha) as tint, every run has the decal state.
    DecalBatch m1, m2, m3, m4, m5;
    m1.first = 0;   m1.count = 3; m1.texture = &gTexA; m1.blend = DecalBlend::Filter; m1.colour = {1, 0.5f, 0.25f}; m1.alpha = 0.5f;
    m2.first = 3;   m2.count = 3; m2.texture = &gTexA; m2.blend = DecalBlend::Filter; m2.colour = {1, 0.5f, 0.25f}; m2.alpha = 0.5f;
    m3.first = 6;   m3.count = 3; m3.texture = nullptr;
    m4.first = 9;   m4.count = 3; m4.texture = &gTexA; m4.blend = DecalBlend::Add;
    m5.first = 12;  m5.count = 0; m5.texture = &gTexB;
    std::vector<DrawRun> runs;
    buildDecalRuns({m1, m2, m3, m4, m5}, runs);
    REQUIRE(runs.size() == 2);
    CHECK(runs[0].first == 0);
    CHECK(runs[0].count == 6);
    CHECK(runs[0].tint.x == doctest::Approx(1.0f));
    CHECK(runs[0].tint.y == doctest::Approx(0.5f));
    CHECK(runs[0].tint.w == doctest::Approx(0.5f));
    CHECK(runs[1].first == 9);
    for (const DrawRun& r : runs) {
        CHECK(r.key.polygonOffset);
        CHECK(r.key.depthTest);
        CHECK_FALSE(r.key.depthWrite);
        CHECK(r.key.cull == CullMode::Back);
    }
    CHECK(runs[0].key.blend == RenderBlend::Filter);
    CHECK(runs[0].key.fog == FogColorMode::White);
    CHECK(runs[1].key.blend == RenderBlend::Add);
    CHECK(runs[1].key.fog == FogColorMode::Black);
}

TEST_CASE("skid trails: strips, one run per trail") {
    FlatTerrain ft(8, 8, 10);
    const TerrainGridView grid = TerrainGridView::of(ft.terrain);
    SkidTrailParams params;
    params.maxTrails = 2;
    params.maxNodes = 3;
    params.heightOffset = 2.0f;

    SkidTrail t;
    t.texture = "a";
    for (int i = 0; i < 2; i++) {
        SkidNode n;
        n.position = {100.0f, 100.0f + 10.0f * static_cast<float>(i)};
        n.direction = {0, 1};
        n.width = 16.0f;
        n.distance = 8.0f * static_cast<float>(i);
        t.nodes.push_back(n);
    }
    SkidTrail tooShort = t;
    tooShort.nodes.resize(1);
    SkidTrail noTex = t;
    noTex.texture.clear();
    SkidTrail trails[] = {tooShort, noTex, t, t, t};

    std::vector<SkidVertex> verts;
    std::vector<DrawRun> runs;
    int built = buildSkidTrails(trails, 5, grid, params, resolveTex, verts, runs);
    CHECK(built == 2); // maxTrails
    CHECK(verts.size() == 8);
    REQUIRE(runs.size() == 2); // strips never merge
    CHECK(runs[0].strip);
    CHECK(runs[0].first == 0);
    CHECK(runs[0].count == 4);
    CHECK(runs[1].first == 4);
    CHECK(runs[0].texture == &gTexA);
    CHECK(runs[0].key.blend == RenderBlend::Alpha);
    CHECK(runs[0].key.depthTest);
    CHECK_FALSE(runs[0].key.depthWrite);
    CHECK(runs[0].key.polygonOffset);
    CHECK(runs[0].key.cull == CullMode::Back);
    CHECK(runs[0].key.fog == FogColorMode::Level);

    // Direction (0,1): lateral (1,0); section 0 spans x 92..108 at y 100, z = 10 + 2; v = distance / width.
    CHECK(verts[0].x == doctest::Approx(92.0f));
    CHECK(verts[1].x == doctest::Approx(108.0f));
    CHECK(verts[0].y == doctest::Approx(100.0f));
    CHECK(verts[0].z == doctest::Approx(12.0f));
    CHECK(verts[0].u == doctest::Approx(0.0f));
    CHECK(verts[1].u == doctest::Approx(1.0f));
    CHECK(verts[0].v == doctest::Approx(0.0f));
    CHECK(verts[2].v == doctest::Approx(0.5f));
    CHECK(verts[2].y == doctest::Approx(110.0f));
    CHECK(verts[0].alpha == doctest::Approx(1.0f));
    CHECK(skidVertexCapacity(params) == 12);
}

TEST_CASE("appendDrawRun merges only contiguous equal runs") {
    std::vector<DrawRun> runs;
    DrawRun a;
    a.texture = &gTexA;
    a.first = 0;
    a.count = 6;
    appendDrawRun(runs, a);
    DrawRun b = a;
    b.first = 6;
    appendDrawRun(runs, b);
    CHECK(runs.size() == 1);
    CHECK(runs[0].count == 12);
    DrawRun gap = a;
    gap.first = 20;
    appendDrawRun(runs, gap);
    CHECK(runs.size() == 2);
    DrawRun empty = a;
    empty.count = 0;
    appendDrawRun(runs, empty);
    CHECK(runs.size() == 2);
}
