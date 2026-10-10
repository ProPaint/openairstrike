// WorldScene: the backend-neutral half of WorldRenderer::prepare (as3d/world_render.h), shared
// by the GL renderer (engine/src/render/gles/world_render.cpp), the PICA renderer
// (engine/src/render/pica/world_renderer_pica.cpp) and the 3DS test modes. Private to the render
// module and the 3DS app; no GL, no citro3d.
//
// What it does, once per frame (prepare()): the world camera (worldViewOf) and the cull volume
// widened for the eyes, the level lighting and fog, the dynamic lights (the PlaceLight queue then
// the definition lights of the drawn entities), the entity pass in think order (worldRenderOrder)
// that fills the opaque, transparent and effect model lists (mesh, material per (definition,
// skin), entity matrix, colour), the shadow requests of the model records, the sprites and the
// enemy health bars, the ground-mark descriptions, the sequels' skid trails, the lightning bolts
// and the live particle emitters. It loads meshes and textures through the ResourceCache it is
// given (first use), never draws. What only a backend can do is left to it: generating shadow
// silhouettes (ShadowRequest), cutting mark geometry, uploading changed terrain chunks
// (terrainChanges()).
//
// The level-start precache's closure walk (WorldRenderer::precache) is here too: precache()
// loads the meshes, materials and textures of the closure and calls the backend's hooks for the
// shadow silhouettes and the particle textures.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "as3d/core.h"
#include "as3d/defs.h"
#include "as3d/dynamic_lights.h"
#include "as3d/ground_marks.h"
#include "as3d/health_bar.h"
#include "as3d/precache.h"
#include "as3d/render_rules.h"
#include "as3d/scene.h"
#include "as3d/shadow_render.h"
#include "as3d/skid_render.h"
#include "as3d/sprite_render.h"
#include "as3d/terrain_grid.h"
#include "as3d/world.h"
#include "as3d/world_render.h"

namespace as3d {

class DefDatabase;
class Vfs;
struct ObjectDef;

// List capacities of render-pipeline.md 1.2 (sprites and marks are bounded by their renderers:
// kMaxSprites, GroundMarkRenderer::kMaxMarks).
constexpr int kOpaqueCap = 512;
constexpr int kTransCap = 128;
constexpr int kEffectCap = 256;

// One model record of the entity pass.
struct MeshRecord {
    const GpuMesh* mesh = nullptr;
    const Material* material = nullptr;
    Mat4 model;
    Vec4 colour;
    int entity = -1; // the entity slot that submitted it
};

// obj.md "shadow" after the parser's remap (render-pipeline.md 5.1).
struct ShadowSpec {
    bool any = false;
    ShadowKind kind = ShadowKind::Planar;
    ShadowQuality quality = ShadowQuality::Normal;
};
ShadowSpec shadowSpecOf(ShadowMode m);

// A model record that casts a shadow (5.3): the backend looks up or generates the silhouette of
// (modelPath, skinPath, spec, steps) and places it at `origin` with `yawDegrees`.
struct ShadowRequest {
    const GpuMesh* mesh = nullptr;
    const Material* material = nullptr;
    const std::string* modelPath = nullptr; // the entity's model path (stable until the next prepare)
    const std::string* skinPath = nullptr;  // the entity's skin override, may be empty
    ShadowSpec spec;
    int steps = 0;         // projected shadows: the 30 degree yaw step of the root's spawn key
    Vec3 origin;
    float yawDegrees = 0.0f;
};

// The entity matrix (render-pipeline.md 3): axis rows 44..52, base origin 41..43, scale 32.
Mat4 entityMatrix(const Entity& e);
// The root of an attachment chain (parent links, at most 64).
int rootOfEntity(const World& w, int i);
DecalBlend decalBlendOf(BlendMode b);

class WorldScene {
public:
    // `cache`, `vfs` and `db` must outlive the scene.
    void init(ResourceCache& cache, Vfs& vfs, const DefDatabase& db);
    // The health bar objects named by the rules (hbarReady() is false without them).
    void resolveHealthBar(const GameRules& rules);
    // After World::loadLevel: forgets the per-slot memory (spawn origins, shadow keys) and the
    // mark set, and takes the world's terrain revision as the one the backend's meshes hold.
    void beginLevel(const World& world, const RenderRules& rules);

    // The material of (definition, skin override), created on first use; addresses are stable.
    const Material& materialFor(const ObjectDef& def, const std::string& skinOverride);

    // Reads the world for one frame. `withShadows`: collect ShadowRequests (a backend without a
    // shadow pass passes false; options.shadows still applies). `withSkids`: collect the skid
    // trails (the GL renderer's skid pass is ready). Does nothing for an empty viewport
    // (ready() false). Fills the list counts of `stats` (models, sprites, marks, lights, dropped,
    // healthBars); shadows and the per-draw counts are the backend's.
    void prepare(const World& world, const Viewport& viewport, const WorldRenderOptions& options, bool withShadows,
                 bool withSkids, WorldRenderStats& stats);

    // The terrain cells changed since the revision the meshes hold (craters, TerraMorph), when
    // `built` is the world's terrain (the one the backend built); empty otherwise. Advances the
    // revision. Call once per frame, before drawing.
    const std::vector<VertexRect>& terrainChanges(const World& world, const Terrain* built);

    // Level-start precache (WorldRenderer::precache): the closure of World::precacheSet() (or one
    // computed from the scripts compiled so far). `shadow` gets every silhouette the closure can
    // need (null: none), `particleTexture` every particle texture (null: none); with
    // `particlesFirst` the particle textures are loaded before the meshes and skins (the PICA
    // puts the first textures in VRAM). The report counts the loads done (LoadStats deltas);
    // `shadows` is the caller's.
    struct PrecacheHooks {
        std::function<void(const GpuMesh& mesh, const std::string& modelPath, const Material& material,
                           const ShadowSpec& spec, int steps)>
            shadow;
        std::function<void(const std::string& path)> particleTexture;
        bool particlesFirst = false;
        // `drawnFirst`: before everything else, the meshes and materials of the entities alive
        // now (the player's helicopter and what hangs on it) and of the level's placements with
        // their attachments, in placement order: what is drawn most gets created first (the
        // PICA's VRAM budget goes in creation order).
        bool drawnFirst = false;
    };
    PrecacheReport precache(const World& world, const DefDatabase& db, const std::function<void(float)>& progress,
                            const PrecacheHooks& hooks);
    // The shadows of a placed object and its attachments (the GL beginLevel's precache, before
    // the closure precache existed): calls `shadow` for each.
    void placedShadows(const ObjectDef* def, int steps, bool night, int depth,
                       const std::function<void(const GpuMesh&, const std::string&, const Material&,
                                                const ShadowSpec&, int)>& shadow);
    // The projected shadows of what a placed object's script spawns (the objects its compiled
    // script names: the ruins of a destroyed building) at rotation key `steps` (the caller's:
    // `create` keys a spawn by its creator's int(yaw in degrees) mod 12). Two levels deep (a
    // spawn's own script). Calls `shadow` for each.
    void spawnedShadows(const World& world, const ObjectDef* def, int steps, int depth,
                        const std::function<void(const GpuMesh&, const std::string&, const Material&,
                                                 const ShadowSpec&, int)>& shadow);

    // The frame read by the last prepare().
    struct Frame {
        bool ready = false;
        Viewport viewport;
        WorldRenderOptions options;
        float aspect = 1.0f;
        WorldView wv;
        CullFrustum cull;
        const Terrain* terrain = nullptr;
        SceneLighting light;
        bool hasFog = false;
        Vec3 clearColour{0.0f, 0.0f, 0.0f}; // pass 0: the fog colour, black without fog
        float time = 0.0f;
        float mapPos = 0.0f;
        const Texture2D* boltTexture = nullptr;
    };
    const Frame& frame() const { return frame_; }
    void invalidate() { frame_.ready = false; }

    std::vector<MeshRecord> opaque, trans, effect;
    std::vector<ShadowRequest> shadowRequests;
    std::vector<SpriteInstance> sprites;
    // Ground marks in submission order, each with (slot << 32 | generation): the GL renderer
    // re-cuts the geometry when the set changes.
    std::vector<std::pair<GroundMarkDesc, std::uint64_t>> markDescs;
    std::vector<SkidTrail> skidTrails; // the first skidTrailCount are this frame's
    size_t skidTrailCount = 0;
    std::vector<Vec3> boltStarts, boltEnds;
    std::vector<const ParticleEmitter*> emitters;
    DynamicLightList lights;
    int dropped = 0;

    // Optional profile of prepare() (the PICA pass profile): with `clock` set, each prepare adds
    // the clock's ticks of its parts. No effect on what prepare() produces.
    struct PrepareProfile {
        std::uint64_t (*clock)() = nullptr;
        std::uint64_t setup = 0;    // camera, lighting, the queued lights
        std::uint64_t order = 0;    // worldRenderOrder
        std::uint64_t entities = 0; // the entity pass
        std::uint64_t tail = 0;     // skid trails, bolts, particle emitters
        std::uint64_t entityCount = 0;
    };
    PrepareProfile profile;

    bool hbarReady() const { return hbarReady_; }
    const HealthBarSpriteDef& hbarEmpty() const { return hbarEmpty_; }
    const HealthBarSpriteDef& hbarFull() const { return hbarFull_; }

private:
    // Per entity slot: what the renderer remembers about the entity since it first saw it (the
    // original builds mark and projected-shadow display lists once, at spawn).
    struct SlotMemory {
        u32 generation = 0;
        bool seen = false;
        Vec3 spawnOrigin;
        int shadowSteps = 0;
        // The last lookups of the slot's entity (the same pointers the cache returns, without the
        // path normalisation and hashing of every frame): its mesh by model path, its material by
        // (definition, skin override), a sprite's or mark's texture by skin path.
        std::string meshPath;
        const GpuMesh* mesh = nullptr;
        const ObjectDef* materialDef = nullptr;
        std::string materialSkin;
        const Material* material = nullptr;
        std::string texturePath;
        const Texture2D* texture = nullptr;
    };
    const GpuMesh& meshOf(SlotMemory& mem, const std::string& path);
    const Material& materialOf(SlotMemory& mem, const ObjectDef& def, const std::string& skinOverride);
    const Texture2D* textureOf(SlotMemory& mem, const std::string& path);
    void precacheObject(const ObjectDef& def, const std::string& swapped, const PrecacheHooks& hooks);
    void precacheDrawn(const World& world);
    void precachePlaced(const ObjectDef* def, bool night, int depth);
    void collectSkidTrails(const World& world);

    ResourceCache* cache_ = nullptr;
    Vfs* vfs_ = nullptr;
    const DefDatabase* db_ = nullptr;
    RenderRules rules_ = renderRules(GameId::AirStrike3D);
    std::map<std::pair<const ObjectDef*, std::string>, std::unique_ptr<Material>> materials_;
    std::vector<SlotMemory> memory_;
    HealthBarSpriteDef hbarEmpty_, hbarFull_;
    bool hbarReady_ = false;
    std::vector<int> order_;
    std::vector<char> visited_;
    u32 terrainRevision_ = 0;
    std::vector<TerrainChange> changeScratch_;
    std::vector<VertexRect> rects_;
    Frame frame_;
};

} // namespace as3d
