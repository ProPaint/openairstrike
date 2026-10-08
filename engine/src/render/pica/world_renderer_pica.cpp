// The PICA WorldRenderer (as3d/world_render.h), 3DS only: the same public API as the GL one
// (engine/src/render/gles/world_render.cpp), the same neutral entity pass (WorldScene,
// engine/src/render/world_scene.h), the passes the PICA backend has so far.
//
// prepare(), once per frame: the terrain follows the world's craters (TerrainRenderer::update),
// WorldScene::prepare reads the world (camera, cull volume, lighting, dynamic lights, the model,
// sprite and particle lists), then the two mesh passes are built (pica::MeshPass begin / submit /
// finish: the opaque list; the transparent then the effect list), all CPU work, nothing drawn.
//
// draw(eye), per eye, in the GL pass order: pass 0 clear to the fog colour (black without fog),
// W-buffer depth mapping for the near plane and the level fog; projection = the eye's
// picaEyeProjection with the CENTRAL view (citro3d's Mtx_PerspStereoTilt carries the eye
// translation); pass 2 terrain, pass 6 opaque list, passes 8 and 9 transparent and effect lists,
// pass 10 particles, pass 12 sprites (health bars included). The particle and sprite quads are
// built by the first eye of a frame and reused by the second (fx_pica.h).
//
// Not drawn yet (counted 0 in lastStats(), listed once in the log at beginLevel): shadows (pass
// 5), ground marks (4), skid trails, water (7), lightning bolts, environment maps (the materials
// draw as plain textured models, mesh_renderer_pica.h).
//
// Textures: the 4.2 MB VRAM budget (texture_budget.h) is filled in creation order: the terrain's
// textures at beginLevel first, then precache() puts the particle textures, then the level
// closure's meshes and skins in definition order; whatever does not fit goes to linear memory.
#include "as3d/world_render.h"

#include <citro3d.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/defs.h"
#include "as3d/particle_render.h"
#include "as3d/render_state.h"
#include "as3d/scene.h"
#include "as3d/sprite_render.h"
#include "as3d/terrain_render.h"
#include "as3d/vfs.h"
#include "flat_fill.h"
#include "fx_pica.h"
#include "mesh_renderer_pica.h"
#include "pica_frame.h"
#include "terrain_pica.h"
#include "texture_budget.h"
#include "world_pica.h"
#include "world_scene.h"

namespace as3d {

namespace pica {
namespace {
WorldPicaStats g_worldStats;
} // namespace
const WorldPicaStats& worldPicaStats() { return g_worldStats; }
} // namespace pica

struct WorldRenderer::Impl {
    Vfs* vfs = nullptr;
    const DefDatabase* db = nullptr;
    std::unique_ptr<ResourceCache> cache;
    WorldScene scene;
    pica::MeshPass opaquePass, restPass;
    ParticleRenderer particles;
    SpriteRenderer sprites;
    std::unique_ptr<TerrainRenderer> terrain;
    const Terrain* terrainOf = nullptr;
    RenderRules rules = renderRules(GameId::AirStrike3D);
    bool haveTerrain = false; // of the prepared frame
};

WorldRenderer::WorldRenderer() : impl_(new Impl) {}
WorldRenderer::~WorldRenderer() = default;

bool WorldRenderer::init(Vfs& vfs, const DefDatabase& db, std::string* error) {
    Impl& im = *impl_;
    im.vfs = &vfs;
    im.db = &db;
    im.cache.reset(new ResourceCache(vfs));
    im.scene.init(*im.cache, vfs, db);
    if (!im.particles.init(vfs, error)) return false;
    if (!im.sprites.init(error)) return false;
    im.scene.resolveHealthBar(defaultGameRules());
    return true;
}

bool WorldRenderer::beginLevel(const World& world, std::string* error) {
    Impl& im = *impl_;
    im.scene.resolveHealthBar(world.rules());
    im.rules = renderRulesFor(world.rules());
    im.cache->setMissingTextureWhite(im.rules.missingTextureWhite);
    im.terrain.reset();
    im.terrainOf = nullptr;
    im.scene.beginLevel(world, im.rules);
    const Terrain* t = world.terrain();
    if (!t) return true;
    std::unique_ptr<TerrainRenderer> tr(new TerrainRenderer());
    if (!tr->build(*t, *im.vfs, error, im.rules)) return false;
    im.terrain = std::move(tr);
    im.terrainOf = t;
    const pica::TerrainPicaStats& ts = pica::terrainPicaStats();
    AS3D_INFO("pica world: terrain %d chunks, %d textures %.1f KB (VRAM %.1f KB, linear %.1f KB); not drawn on "
              "the PICA yet: shadows, ground marks, skid trails, water, lightning, environment maps",
              ts.chunks, ts.textures, static_cast<double>(ts.textureBytes) / 1024.0,
              static_cast<double>(ts.textureVramBytes) / 1024.0, static_cast<double>(ts.textureLinearBytes) / 1024.0);
    return true;
}

PrecacheReport WorldRenderer::precache(const World& world, const DefDatabase& db, const std::function<void(float)>& progress) {
    Impl& im = *impl_;
    WorldScene::PrecacheHooks hooks;
    // No shadow pass: no silhouettes. VRAM goes in creation order (the terrain's textures came
    // with beginLevel): first the models alive now and the level's placements (the player's
    // helicopter is the most drawn texture), then the particle textures (large additive smoke and
    // fire), then the rest of the closure (mostly other helicopters' skins and script spawns).
    hooks.particleTexture = [&im](const std::string& path) { im.particles.preload(path); };
    hooks.particlesFirst = true;
    hooks.drawnFirst = true;
    const PrecacheReport r = im.scene.precache(world, db, progress, hooks);
    // Where the model skins went (texture_budget.h): VRAM in creation order until the budget is
    // full, linear memory after. Logged once per level: AS3D_TEXTURES.
    if (const PrecacheSet* set = world.precacheSet()) {
        std::vector<const Texture2D*> seen;
        size_t vramBytes = 0, linearBytes = 0;
        int vram = 0, linear = 0;
        std::string linearNames;
        for (const ObjectDef* def : set->objects) {
            if (def->type != ObjectType::Model || def->model.empty()) continue;
            const Material& m = im.scene.materialFor(*def, std::string());
            if (!m.texture || std::find(seen.begin(), seen.end(), m.texture) != seen.end()) continue;
            seen.push_back(m.texture);
            pica::Placement pl;
            if (!pica::textureInfo(*m.texture, &pl, nullptr, nullptr, nullptr, nullptr)) continue;
            if (pl == pica::Placement::Vram) {
                ++vram;
                vramBytes += m.texture->bytes();
            } else {
                ++linear;
                linearBytes += m.texture->bytes();
                if (linearNames.size() < 600) linearNames += " " + (def->skin.empty() ? def->name : def->skin);
            }
        }
        const pica::TextureStats ts = pica::textureStats();
        AS3D_INFO("AS3D_TEXTURES all: VRAM %d = %.1f KB, linear %d = %.1f KB; model skins: VRAM %d = %.1f KB, linear "
                  "%d = %.1f KB; linear skins:%s",
                  ts.vramCount, static_cast<double>(ts.vramBytes) / 1024.0, ts.linearCount,
                  static_cast<double>(ts.linearBytes) / 1024.0, vram, static_cast<double>(vramBytes) / 1024.0, linear,
                  static_cast<double>(linearBytes) / 1024.0, linearNames.c_str());
    }
    return r;
}

const RenderRules& WorldRenderer::rules() const { return impl_->rules; }
int WorldRenderer::shadowMapCount() const { return 0; }
int WorldRenderer::lateShadowMaps() const { return 0; }

void WorldRenderer::render(const World& world, int width, int height, const WorldRenderOptions& options) {
    render(world, 0, 0, width, height, options);
}

void WorldRenderer::render(const World& world, int x, int y, int width, int height, const WorldRenderOptions& options) {
    Viewport vp;
    vp.x = x;
    vp.y = y;
    vp.width = width;
    vp.height = height;
    prepare(world, vp, options);
    draw(EyeView{});
}

const CullFrustum& WorldRenderer::preparedFrustum() const { return impl_->scene.frame().cull; }

void WorldRenderer::prepare(const World& world, const Viewport& viewport, const WorldRenderOptions& options) {
    Impl& im = *impl_;
    WorldScene& sc = im.scene;
    stats_ = WorldRenderStats();
    sc.invalidate();
    if (viewport.width <= 0 || viewport.height <= 0) return;
    const Terrain* terrain = world.terrain();
    im.haveTerrain = terrain && im.terrain && im.terrainOf == terrain;
    // Craters since the last frame (copy-on-write chunks, terrain_pica.h).
    const std::vector<VertexRect>& rects = sc.terrainChanges(world, im.terrainOf);
    if (!rects.empty() && im.terrain) im.terrain->update(rects.data(), rects.size(), TerrainGridView::of(*im.terrainOf));
    sc.prepare(world, viewport, options, false, false, stats_);
    // The mesh passes: all per-model CPU work (cube colours, material keys) once per frame.
    const SceneLighting& light = sc.frame().light;
    im.opaquePass.begin(light, sc.lights.data(), sc.lights.size());
    for (const MeshRecord& r : sc.opaque) im.opaquePass.submit(*r.mesh, *r.material, r.model, r.colour);
    im.opaquePass.finish();
    im.restPass.begin(light, sc.lights.data(), sc.lights.size());
    for (const MeshRecord& r : sc.trans) im.restPass.submit(*r.mesh, *r.material, r.model, r.colour);
    for (const MeshRecord& r : sc.effect) im.restPass.submit(*r.mesh, *r.material, r.model, r.colour);
    im.restPass.finish();
    // Which of this frame's model textures live in linear memory (the VRAM split).
    pica::WorldPicaStats& ws = pica::g_worldStats;
    ws.records = ws.linearRecords = 0;
    ws.linearSkins.clear();
    for (const std::vector<MeshRecord>* list : {&sc.opaque, &sc.trans, &sc.effect}) {
        for (const MeshRecord& r : *list) {
            ++ws.records;
            pica::Placement pl;
            if (r.material->texture && pica::textureInfo(*r.material->texture, &pl, nullptr, nullptr, nullptr, nullptr) &&
                pl == pica::Placement::Linear) {
                ++ws.linearRecords;
                const std::string& name = world.entity(r.entity).def->name;
                if (ws.linearSkins.find(" " + name) == std::string::npos && ws.linearSkins.size() < 400)
                    ws.linearSkins += " " + name;
            }
        }
    }
}

void WorldRenderer::draw(const EyeView& eye) {
    const Viewport& vp = impl_->scene.frame().viewport;
    draw(eye, vp.x, vp.y);
}

void WorldRenderer::draw(const EyeView& eye, int vx, int vy) {
    Impl& im = *impl_;
    const WorldScene& sc = im.scene;
    const WorldScene::Frame& fr = sc.frame();
    stats_.terrainChunks = stats_.waterChunks = stats_.skidTrails = stats_.bolts = 0;
    stats_.emitters = stats_.particles = 0;
    if (!fr.ready) return;
    const WorldRenderOptions& options = fr.options;
    const SceneLighting& light = fr.light;
    const WorldView& wv = fr.wv;

    // The eye: the central view with the eye's stereo projection (pica_frame.h).
    const Mat4 proj = pica::picaEyeProjection(wv, eye, fr.aspect);
    const Mat4 viewProj = proj * wv.view;
    auto levelDepthAndFog = [&]() {
        pica::setDepthMapping(FogDepthMap::WBuffer, wv.nearPlane);
        pica::setLevelFogColor(light.fogColor);
        if (fr.hasFog) pica::setFog(wv.nearPlane, wv.farPlane, light.fogStart, light.fogEnd);
        else pica::disableFog();
    };

    // Pass 0: clear to the fog colour (black without fog).
    setViewport(vx, vy, fr.viewport.width, fr.viewport.height);
    const Vec3 cc = fr.clearColour;
    clear(Vec4{cc.x, cc.y, cc.z, 1.0f}, true, 1.0f);
    setBlend(GlBlend::Off);
    setDepth(true, true);
    setCull(CullMode::Back);
    levelDepthAndFog();

    // Pass 2: terrain.
    if (options.terrain && im.haveTerrain) {
        TerrainViewParams tv;
        tv.view = wv.view;
        tv.projection = proj;
        tv.cameraPos = eyePosition(wv, eye);
        tv.fogColour = light.fogColor;
        tv.fogStart = light.fogStart;
        tv.fogEnd = light.fogEnd;
        tv.time = fr.time;
        tv.mapPos = fr.mapPos;
        tv.lights = sc.lights.data();
        tv.lightCount = sc.lights.size();
        im.terrain->render(tv);
        stats_.terrainChunks = im.terrain->lastVisibleChunks();
        const pica::TerrainPicaStats& ts = pica::terrainPicaStats();
        stats_.drawCalls += ts.drawCalls;
        stats_.vertices += static_cast<int>(ts.indicesDrawn);
        levelDepthAndFog();
    }
    setDepth(true, true);
    setCull(CullMode::Back);
    // Pass 6: opaque list; passes 8 and 9: transparent then effect list (pass 7, water, is not
    // drawn on the PICA yet).
    im.opaquePass.draw(viewProj);
    stats_.drawCalls += im.opaquePass.lastStats().draws;
    stats_.vertices += im.opaquePass.lastStats().vertices;
    im.restPass.draw(viewProj);
    stats_.drawCalls += im.restPass.lastStats().draws;
    stats_.vertices += im.restPass.lastStats().vertices;
    setBlend(GlBlend::Off);
    setDepth(true, true);
    setCull(CullMode::Back);
    // Pass 10: particles.
    if (options.particles) {
        ParticleViewParams pv;
        pv.view = wv.view;
        pv.projection = proj;
        pv.fogColour = light.fogColor;
        pv.fogStart = light.fogStart;
        pv.fogEnd = light.fogEnd;
        if (!sc.emitters.empty()) {
            im.particles.draw(sc.emitters.data(), sc.emitters.size(), pv);
            const pica::FxPicaStats& fs = pica::fxPicaStats();
            stats_.drawCalls += fs.particleDraws;
            stats_.vertices += fs.particleQuads * 6;
        }
        stats_.emitters = static_cast<int>(sc.emitters.size());
        stats_.particles = im.particles.lastParticleCount();
    }
    // Pass 12: sprites.
    if (!sc.sprites.empty()) {
        SpriteViewParams sv;
        sv.view = wv.view;
        sv.projection = proj;
        sv.fogColour = light.fogColor;
        sv.fogStart = light.fogStart;
        sv.fogEnd = light.fogEnd;
        sv.cullBackFaces = im.rules.spriteCulling;
        im.sprites.draw(sc.sprites.data(), sc.sprites.size(), sv);
        const pica::FxPicaStats& fs = pica::fxPicaStats();
        stats_.drawCalls += fs.spriteDraws;
        stats_.vertices += fs.spriteQuads * 6;
    }
    setBlend(GlBlend::Off);
    setDepth(true, true);
    setCull(CullMode::Back);
    pica::disableFog();
}

void WorldRenderer::drawBrightness(int width, int height, float brightness) { drawBrightness(0, 0, width, height, brightness); }

// Pass 14 (render-pipeline.md 1.5): dst * b + b * dst, the blend (DST_COLOR, SRC_COLOR) of
// RenderBlend::Brightness, over the rectangle (GL convention: y from the bottom).
void WorldRenderer::drawBrightness(int x, int y, int width, int height, float brightness) {
    if (brightness <= 0.0f || width <= 0 || height <= 0) return;
    const pica::FrameTarget& t = pica::currentTarget();
    MaterialKey key;
    key.blend = RenderBlend::Brightness;
    key.depthTest = false;
    key.depthWrite = false;
    key.cull = CullMode::Off;
    const uint8_t b = static_cast<uint8_t>(std::min(brightness, 1.0f) * 255.0f + 0.5f);
    const float top = static_cast<float>(t.height - y - height);
    pica::fillRect(static_cast<float>(x), top, static_cast<float>(x + width), top + static_cast<float>(height), b, b, b,
                   255, key);
    setBlend(GlBlend::Off);
    setDepth(true, true);
    setCull(CullMode::Back);
}

} // namespace as3d
