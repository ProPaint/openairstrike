// Particle tests: simulation on synthetic definitions and on every shipped system, plus a
// headless render of a shipped explosion.
#include "doctest.h"

#include "as3d/particle_render.h"

#include "as3d/defs.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

#include "as3d/platform.h"
#include "as3d/vfs.h"
#include "test_data.h"

using namespace as3d;

namespace {

constexpr float kStep = 1.0f / 60.0f;

ParticleDesc continuousDesc() {
    ParticleDesc d;
    d.name = "SYNTH";
    d.blend = ParticleBlend::Add;
    d.emitRate = 10;
    d.lifeTime = 1.0f;
    d.initSize[0] = 4;
    d.initSize[1] = 2;
    d.initVelocity[2] = 10;
    d.initVelocity[3] = 5;
    d.initVelocity[4] = 5;
    d.initColor[0] = d.initColor[1] = d.initColor[2] = d.initColor[3] = 1.0f;
    d.fadeLinear = true;
    return d;
}

void run(ParticleEmitter& e, float seconds) {
    int n = static_cast<int>(seconds / kStep + 0.5f);
    for (int i = 0; i < n; i++) e.update(kStep);
}

bool finiteState(const ParticleEmitter& e) {
    for (const Particle& p : e.particles()) {
        for (float f : p.p) if (!std::isfinite(f)) return false;
        for (float f : p.v) if (!std::isfinite(f)) return false;
        for (float f : p.rgba) if (!std::isfinite(f)) return false;
        if (!std::isfinite(p.size) || !std::isfinite(p.age) || !std::isfinite(p.angle)) return false;
    }
    return true;
}

bool mountOriginalPaks(Vfs& vfs) {
    std::string dataDir = testdata::installDir() + "/data";
    for (const char* name : {"pak0.apk", "pak1.apk", "pak2.apk"}) {
        auto src = makePakSource(openFileStream(dataDir + "/" + name));
        if (!src) return false;
        vfs.mount(std::move(src));
    }
    return true;
}

GraphicsContext* headlessContext() {
    static std::unique_ptr<GraphicsContext> ctx = [] {
        GraphicsConfig cfg;
        cfg.width = 256;
        cfg.height = 256;
        cfg.headless = true;
        cfg.title = "as3d_tests";
        return createGraphicsContext(cfg);
    }();
    if (ctx) ctx->makeCurrent();
    return ctx.get();
}

} // namespace

TEST_CASE("particles: pool size follows the original's formula") {
    ParticleDesc d = continuousDesc();
    CHECK(d.poolSize() == 14); // int(10 * 1.0 + 4)
    d.emitOnce = true;
    CHECK(d.poolSize() == 10);
    d.emitRate = 0;
    CHECK(d.poolSize() == 0);
}

TEST_CASE("particles: the pool has a hard upper bound") {
    ParticleDesc d = continuousDesc();
    d.emitRate = 1000000;
    d.lifeTime = 100.0f;
    CHECK(d.poolSize() == kMaxParticlesPerEmitter);
    ParticleEmitter e(d, 1);
    CHECK(e.capacity() == kMaxParticlesPerEmitter);
    run(e, 2.0f);
    CHECK(e.liveCount() <= e.capacity());
    CHECK(finiteState(e));
    // A huge step must not spin forever or overflow the pool.
    e.update(1.0e9f);
    CHECK(e.liveCount() <= e.capacity());
}

TEST_CASE("particles: same seed gives identical state, different seed differs") {
    ParticleDesc d = continuousDesc();
    ParticleEmitter a(d, 1234), b(d, 1234), c(d, 99);
    for (int i = 0; i < 200; i++) {
        a.update(kStep);
        b.update(kStep);
        c.update(kStep);
        CHECK(a.stateHash() == b.stateHash());
    }
    CHECK(a.stateHash() != c.stateHash());
}

TEST_CASE("particles: continuous emitter live counts over time") {
    ParticleDesc d = continuousDesc();
    ParticleEmitter e(d, 7);
    CHECK(e.liveCount() == 0);
    run(e, 0.5f);
    CHECK(e.liveCount() >= 4);
    CHECK(e.liveCount() <= 5);
    run(e, 2.5f); // steady state: rate * life
    CHECK(e.liveCount() >= 9);
    CHECK(e.liveCount() <= 11);
    e.stop();
    run(e, 1.2f);
    CHECK(e.liveCount() == 0);
    CHECK(e.finished());
}

TEST_CASE("particles: EMIT_ONCE spawns everything at once and never again") {
    ParticleDesc d = continuousDesc();
    d.emitOnce = true;
    d.emitRate = 8;
    ParticleEmitter e(d, 3);
    CHECK(e.liveCount() == 8);
    run(e, 0.5f);
    CHECK(e.liveCount() == 8);
    run(e, 0.7f);
    CHECK(e.liveCount() == 0);
    CHECK(e.finished());
    e.activate();
    run(e, 1.0f);
    CHECK(e.liveCount() == 0);
}

TEST_CASE("particles: linear fade, velocity, acceleration and size growth") {
    ParticleDesc d;
    d.blend = ParticleBlend::Add;
    d.emitOnce = true;
    d.emitRate = 1;
    d.lifeTime = 2.0f;
    d.initSize[0] = 4.7f; // base is truncated to 4
    d.initVelocity[0] = 10;
    d.accel[2] = -10;
    d.initColor[0] = 0.8f;
    d.initColor[1] = 0.4f;
    d.initColor[2] = 0.2f;
    d.fadeLinear = true;
    d.fadeFactor = 1.0f;
    d.sizeRate = 2.0f;
    ParticleEmitter e(d, 1);
    CHECK(e.particles()[0].size == doctest::Approx(4.0f));
    run(e, 1.0f);
    const Particle& p = e.particles()[0];
    // The last update saw age ~ 1 - dt, so k is close to 0.5.
    CHECK(p.rgba[0] == doctest::Approx(0.4f).epsilon(0.05));
    CHECK(p.rgba[1] == doctest::Approx(0.2f).epsilon(0.05));
    CHECK(p.p[0] == doctest::Approx(10.0f).epsilon(0.05));
    CHECK(p.p[2] == doctest::Approx(-5.0f).epsilon(0.1));
    CHECK(p.size == doctest::Approx(6.0f).epsilon(0.05));
}

TEST_CASE("particles: CILINDER particles follow the emitter, DECART ones stay") {
    ParticleDesc d;
    d.emitOnce = true;
    d.emitRate = 1;
    d.lifeTime = 5.0f;
    d.initOffset[1] = 10; // radius 10
    ParticleDesc cyl = d;
    cyl.coords = ParticleCoords::Cilinder;
    ParticleEmitter a(cyl, 1, {100, 0, 0}), b(d, 1, {100, 0, 0});
    a.update(kStep);
    b.update(kStep);
    a.setOrigin({200, 0, 0});
    b.setOrigin({200, 0, 0});
    Vec3 pa = a.worldPosition(a.particles()[0]);
    Vec3 pb = b.worldPosition(b.particles()[0]);
    // Cylinder: azimuth 0, radius 10 around the current origin.
    CHECK(pa.x == doctest::Approx(210.0f));
    CHECK(pa.y == doctest::Approx(0.0f).epsilon(0.01));
    // Decart: offset from the origin at creation (y offset 10, x 100).
    CHECK(pb.x == doctest::Approx(100.0f));
    CHECK(pb.y == doctest::Approx(10.0f));
}

TEST_CASE("particles: every shipped system simulates for 5 s without NaN or runaway counts") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    AS3D_REQUIRE_FIRST_GAME("flamethrowers");
    Vfs vfs;
    REQUIRE(mountOriginalPaks(vfs));
    DefDatabase db;
    REQUIRE(db.load(vfs));
    REQUIRE(db.particleSystems().size() == 80);
    for (const ParticleSystemDef& def : db.particleSystems()) {
        CAPTURE(def.name);
        ParticleDesc d = ParticleDesc::fromDef(def);
        CHECK(d.poolSize() <= kMaxParticlesPerEmitter);
        ParticleEmitter e(d, 42);
        int maxLive = 0;
        for (int i = 0; i < 300; i++) {
            e.update(kStep);
            maxLive = std::max(maxLive, e.liveCount());
        }
        CHECK(finiteState(e));
        CHECK(maxLive <= e.capacity());
        CHECK(e.capacity() <= kMaxParticlesPerEmitter);
        // Determinism on real data too.
        ParticleEmitter f(d, 42);
        for (int i = 0; i < 300; i++) f.update(kStep);
        CHECK(e.stateHash() == f.stateHash());
    }
}

TEST_CASE("particles: a shipped explosion renders non-black pixels headless") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    GraphicsContext* ctx = headlessContext();
    if (!ctx) {
        std::fprintf(stderr, "SKIPPED (no headless GLES context available): %s\n", __FILE__);
        return;
    }
    Vfs vfs;
    REQUIRE(mountOriginalPaks(vfs));
    DefDatabase db;
    REQUIRE(db.load(vfs));
    const ParticleSystemDef* def = db.findParticleSystem("PS_EXPLOSION_FIRE");
    REQUIRE(def != nullptr);
    ParticleDesc desc = ParticleDesc::fromDef(*def);
    ParticleEmitter e(desc, 1);
    run(e, 0.8f);
    REQUIRE(e.liveCount() > 0);

    ParticleRenderer renderer;
    std::string err;
    REQUIRE_MESSAGE(renderer.init(vfs, &err), err);
    RenderTarget target;
    REQUIRE(target.create(256, 256, 0));
    ParticleViewParams vp;
    vp.view = lookAt({0, -80, 45}, {0, 0, 25}, {0, 0, 1});
    vp.projection = perspective(60.0f, 1.0f, 4.0f, 2000.0f);
    target.bind();
    clear({0, 0, 0, 1}, true);
    renderer.draw(e, vp);
    CHECK(renderer.lastParticleCount() == e.liveCount());
    CHECK(renderer.missingTextures() == 0);
    Image img;
    REQUIRE(target.readPixels(img));
    int lit = 0;
    for (size_t i = 0; i + 3 < img.rgba.size(); i += 4)
        if (img.rgba[i] > 24 || img.rgba[i + 1] > 24 || img.rgba[i + 2] > 24) lit++;
    CHECK(lit > 200);
}
