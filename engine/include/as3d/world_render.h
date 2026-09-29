// Draws the live game world (as3d/world.h) each frame: terrain, ground marks, the entity
// lists, water, particles, sprites and the brightness overlay, in the pass order of
// docs/spec/render-pipeline.md 1.1, seen through the world's own camera structure
// (engine-behaviour.md 9). Reuses the engine's renderers (TerrainRenderer, WaterRenderer,
// MeshRenderer, ParticleRenderer); the renderer only reads the world, so rendering can
// never change the simulation.
//
// Particle emitters: the world keeps emitter *holders* (entities whose `emitter` is set)
// but does not simulate particles. WorldParticles mirrors every holder with one
// ParticleEmitter, moved to the holder's attachment every frame and advanced at the fixed
// step (render-pipeline.md 6.3, 6.4). Its random numbers come from its own as3d::Rng, never
// from the world's.
//
// Interim parts, until the dedicated renderers land (another package owns sprites, marks,
// shadows, dynamic lights and environment maps in engine/src/render): sprites and marks are
// drawn here by a small unlit batch (SpriteBatch); shadows, dynamic lights, environment maps
// and lightning bolts are not drawn.
//
// Needs a current GLES 3.0 context for everything except WorldParticles.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/math.h"
#include "as3d/particles.h"
#include "as3d/world.h"

namespace as3d {

class Vfs;
class DefDatabase;

// ---------------------------------------------------------------------------------------
// Camera.
// ---------------------------------------------------------------------------------------

struct WorldView {
    Mat4 view;
    Mat4 projection;
    Vec3 eye;
    float fovY = 60.0f;
    float nearPlane = 4.0f;
    float farPlane = 2000.0f;
};

// The world's camera (view origin 15..17, view angles 18..20, field of view 14): view =
// Rx(a0) Ry(a1) Rz(a2) T(-origin) (render-pipeline.md 2.1), perspective with `aspect`,
// near 4, far = max(fog end, 1000) with fog, else 2000.
WorldView worldViewOf(const World& world, float aspect);

// ---------------------------------------------------------------------------------------
// Particles (GL free).
// ---------------------------------------------------------------------------------------

constexpr int kMaxWorldEmitters = 256; // particle-system instance pool (render-pipeline.md 6.1)

class WorldParticles {
public:
    WorldParticles();
    ~WorldParticles();
    WorldParticles(const WorldParticles&) = delete;
    WorldParticles& operator=(const WorldParticles&) = delete;

    // Drops every emitter and reseeds (call at level start).
    void reset(u32 seed = 1);
    // One fixed step after world.step(): picks up new holders, moves every emitter to its
    // holder, stops emitters whose holder was deactivated or removed (they finish their
    // live particles), then advances all of them by `dt`. Do not call while the world is
    // paused (particles freeze with it).
    void update(const World& world, float dt);

    // Live instances, most recently created first (the original's draw order).
    void collect(std::vector<const ParticleEmitter*>& out) const;
    int emitterCount() const;
    int liveParticles() const;
    // Instances that could not be created because the pool was full.
    std::uint64_t refused() const { return refused_; }
    // Hash of the state of every emitter, for determinism tests.
    u32 stateHash() const;

private:
    struct Instance;
    const ParticleDesc* descOf(const ParticleSystemDef* def);

    std::vector<std::unique_ptr<Instance>> instances_; // creation order
    std::vector<std::pair<const ParticleSystemDef*, std::unique_ptr<ParticleDesc>>> descs_;
    // Per entity slot: the instance mirroring its holder (checked against the generation).
    std::vector<Instance*> bySlot_;
    std::vector<u32> refusedGen_; // per slot: holder generation + 1 whose creation was refused
    Rng rng_{1};
    std::uint64_t refused_ = 0;
};

// ---------------------------------------------------------------------------------------
// The renderer.
// ---------------------------------------------------------------------------------------

struct WorldRenderOptions {
    // config.ini Brightness (render-pipeline.md 1.5); 0.5 is neutral, the default 0.6
    // brightens by 1.2x. Values <= 0 skip the overlay.
    float brightness = 0.6f;
    bool terrain = true;
    bool models = true;
    bool particles = true;
    bool sprites = true;
};

struct WorldRenderStats {
    int models = 0;       // mesh draws submitted
    int dropped = 0;      // records dropped because a list was full (render-pipeline.md 1.2)
    int sprites = 0;
    int marks = 0;
    int emitters = 0;
    int particles = 0;
    int terrainChunks = 0;
};

// A 2D rectangle in the virtual 800x600 screen (origin top-left, y down).
struct OverlayRect {
    float x = 0, y = 0, w = 0, h = 0;
    Vec4 colour{1, 1, 1, 1};
};

class WorldRenderer {
public:
    WorldRenderer();
    ~WorldRenderer();
    WorldRenderer(const WorldRenderer&) = delete;
    WorldRenderer& operator=(const WorldRenderer&) = delete;

    // Compiles shaders. `vfs` and `db` must outlive the renderer.
    bool init(Vfs& vfs, const DefDatabase& db, std::string* error);
    // Builds the terrain and water of the world's current level; resets the particles.
    // Call after every World::loadLevel.
    bool beginLevel(const World& world, std::string* error);

    // Advances the particles by one fixed step (see WorldParticles::update).
    void step(const World& world, float dt) { particles_.update(world, dt); }

    // Draws the world into the currently bound framebuffer with viewport (0, 0, width,
    // height). Leaves depth test on, blending off.
    void render(const World& world, int width, int height, const WorldRenderOptions& options = {});
    // Flat-colour rectangles over everything (alpha blended, no depth), in the virtual
    // 800x600 screen stretched over (width, height).
    void drawOverlay(const OverlayRect* rects, size_t count, int width, int height);

    const WorldRenderStats& lastStats() const { return stats_; }
    const WorldParticles& particles() const { return particles_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    WorldParticles particles_;
    WorldRenderStats stats_;
};

} // namespace as3d
