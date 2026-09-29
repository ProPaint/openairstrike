// Draws the live game world (as3d/world.h) each frame in the pass order of
// docs/spec/render-pipeline.md 1.1, seen through the world's own camera structure
// (engine-behaviour.md 9). Every pass is one of the engine's renderers (terrain, water,
// meshes, ground marks, shadows, particles, sprites) fed from the entity fields; the
// renderer only reads the world, so rendering can never change the simulation.
//
// Particles are part of the simulation (World::particles(), as3d/world_particles.h): the
// renderer draws the world's particle-system instances and simulates nothing itself.
//
// Passes: terrain (with dynamic lights), ground marks, shadows, the opaque list, water, the
// transparent and effect lists (lit, dynamic lights, environment maps), particles, sprites,
// and the brightness overlay after the 2D layer. Not drawn yet: lightning bolts (7.1).
//
// Needs a current GLES 3.0 context for everything except worldViewOf and worldRenderOrder.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/math.h"
#include "as3d/particles.h"
#include "as3d/world.h"
#include "as3d/world_particles.h"

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

// The order in which the entity pass thinks the entities, which is the order they submit
// their render records (render-pipeline.md 1.2): newest pool entity first, each followed by
// its definition children (parents before children); an entity attached with AttachEntity
// comes after its root, which thinks first (engine-behaviour.md 4.1). Removed entities are
// left out; emitter holders are included. `visited` is scratch space.
void worldRenderOrder(const World& world, std::vector<int>& order, std::vector<char>& visited);

// ---------------------------------------------------------------------------------------
// The renderer.
// ---------------------------------------------------------------------------------------

struct WorldRenderOptions {
    bool terrain = true;
    bool models = true;
    bool shadows = true;
    bool lights = true;    // dynamic lights (PlaceLight and definition lights)
    bool particles = true;
    bool sprites = true;   // sprites and ground marks
};

struct WorldRenderStats {
    int models = 0;       // mesh records submitted
    int dropped = 0;      // records dropped because a list was full (render-pipeline.md 1.2)
    int sprites = 0;
    int marks = 0;
    int shadows = 0;
    int lights = 0;
    int emitters = 0;
    int particles = 0;
    int terrainChunks = 0;
};

class WorldRenderer {
public:
    WorldRenderer();
    ~WorldRenderer();
    WorldRenderer(const WorldRenderer&) = delete;
    WorldRenderer& operator=(const WorldRenderer&) = delete;

    // Compiles shaders. `vfs` and `db` must outlive the renderer.
    bool init(Vfs& vfs, const DefDatabase& db, std::string* error);
    // Builds the terrain and water of the world's current level, generates the shadow
    // silhouettes of the placed objects. Call after every
    // World::loadLevel.
    bool beginLevel(const World& world, std::string* error);

    // Nothing to do: the world advances its own particles (kept for callers written when
    // the renderer simulated them).
    void step(const World&, float) {}

    // Draws the world (3D passes 0 to 12 of render-pipeline.md 1.1) into the currently bound
    // framebuffer with viewport (0, 0, width, height). Leaves depth test on, blending off.
    void render(const World& world, int width, int height, const WorldRenderOptions& options = {});
    // Pass 14, after the 2D layer: dst * b + b * dst over the whole framebuffer
    // (render-pipeline.md 1.5; config.ini Brightness, 0.5 neutral, default 0.6).
    void drawBrightness(int width, int height, float brightness);

    const WorldRenderStats& lastStats() const { return stats_; }
    // Shadow silhouettes held, and how many were generated after the level load (entities
    // created by scripts whose model no placement uses).
    int shadowMapCount() const;
    int lateShadowMaps() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    WorldRenderStats stats_;
};

} // namespace as3d
