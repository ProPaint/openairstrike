#include "level_render.h"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "as3d/game_camera.h"
#include "as3d/ground_marks.h"
#include "as3d/object_tree.h"
#include "as3d/shadow_render.h"
#include "as3d/sprite_render.h"
#include "as3d/terrain_render.h"

#define BlendMode AS3D_DEFS_BlendMode
#include "as3d/defs.h"
#undef BlendMode

namespace viewer {

using namespace as3d;

namespace {

// Objects that are neither on the ground nor on the water start at z = 0 in the original and
// are lifted by their scripts, which this viewer does not run. They are drawn this far above
// the terrain instead so that aircraft do not sit buried in the ground.
constexpr float kAirObjectHeight = 90.0f;

struct TypeEntry {
    ObjectTree tree;
    bool valid = false;
    u32 rootFlags = 0;
};

struct PartInfo {
    const GpuMesh* mesh = nullptr;
    const Material* material = nullptr;
    Mat4 model;
    Vec4 colour;
};

// Rotation taking +z to `normal`, applied about `pivot`.
Mat4 tiltAround(const Vec3& pivot, const Vec3& normal) {
    Vec3 z{0, 0, 1};
    Vec3 axis = cross(z, normal);
    float s = length(axis);
    float c = std::min(1.0f, std::max(-1.0f, normal.z));
    if (s < 1e-6f) return Mat4::identity();
    axis = axis * (1.0f / s);
    float t = 1.0f - c;
    Mat4 r;
    r.at(0, 0) = t * axis.x * axis.x + c;
    r.at(1, 0) = t * axis.x * axis.y - s * axis.z;
    r.at(2, 0) = t * axis.x * axis.z + s * axis.y;
    r.at(0, 1) = t * axis.x * axis.y + s * axis.z;
    r.at(1, 1) = t * axis.y * axis.y + c;
    r.at(2, 1) = t * axis.y * axis.z - s * axis.x;
    r.at(0, 2) = t * axis.x * axis.z - s * axis.y;
    r.at(1, 2) = t * axis.y * axis.z + s * axis.x;
    r.at(2, 2) = t * axis.z * axis.z + c;
    return translation(pivot) * r * translation(pivot * -1.0f);
}

DecalBlend toDecalBlend(AS3D_DEFS_BlendMode b) {
    switch (b) {
        case AS3D_DEFS_BlendMode::Alpha: return DecalBlend::Alpha;
        case AS3D_DEFS_BlendMode::Add: return DecalBlend::Add;
        case AS3D_DEFS_BlendMode::Filter: return DecalBlend::Filter;
        default: return DecalBlend::None;
    }
}

struct ShadowSpec {
    ShadowKind kind = ShadowKind::Planar;
    ShadowQuality quality = ShadowQuality::Normal;
    bool any = false;
};

ShadowSpec shadowSpec(ShadowMode m) {
    ShadowSpec s;
    switch (m) {
        case ShadowMode::Projected: s = {ShadowKind::Projected, ShadowQuality::Normal, true}; break;
        case ShadowMode::ProjectedLow: s = {ShadowKind::Projected, ShadowQuality::Low, true}; break;
        case ShadowMode::ProjectedHigh: s = {ShadowKind::Projected, ShadowQuality::High, true}; break;
        case ShadowMode::Planar: s = {ShadowKind::Planar, ShadowQuality::Normal, true}; break;
        case ShadowMode::PlanarLow: s = {ShadowKind::Planar, ShadowQuality::Low, true}; break;
        case ShadowMode::PlanarHigh: s = {ShadowKind::Planar, ShadowQuality::High, true}; break;
        default: break;
    }
    return s;
}

// Shadow pieces, in submission order; the map pointers stay valid for the frame.
struct ShadowPart {
    const ShadowMap* map = nullptr;
    Vec3 origin;
    float yaw = 0.0f;
};

// Places a ground mark object (`mark`, `expl1_light`, ...) by name; false if unknown.
bool addMarkObject(const DefDatabase& db, ResourceCache& cache, const Terrain& terrain, GroundMarkRenderer& marks,
                   const std::string& name, const Vec3& pos, const Vec3& colour) {
    const ObjectDef* def = db.findObject(name);
    if (!def || def->type != ObjectType::Mark || !def->hasBbox) return false;
    GroundMarkDesc d;
    d.origin = pos;
    d.minX = def->bboxMin[0]; d.minY = def->bboxMin[1];
    d.maxX = def->bboxMax[0]; d.maxY = def->bboxMax[1];
    d.texture = cache.texture(def->skin).texture;
    d.blend = toDecalBlend(def->blend);
    d.colour = colour;
    return marks.addMark(terrain, d) >= 0;
}

// "x,y[,object];x,y[,object]..." plus the word "demo", which scatters scorch marks.
void parseMarks(const std::string& spec, float scroll, std::vector<std::pair<Vec2, std::string>>& out) {
    size_t i = 0;
    while (i < spec.size()) {
        size_t j = spec.find(';', i);
        if (j == std::string::npos) j = spec.size();
        std::string item = spec.substr(i, j - i);
        i = j + 1;
        if (item == "demo") {
            for (int k = 0; k < 7; k++) {
                float x = 560.0f + 90.0f * static_cast<float>((k * 5) % 7);
                float y = scroll + 120.0f + 75.0f * static_cast<float>(k);
                out.push_back({{x, y}, "mark"});
            }
            continue;
        }
        float x = 0, y = 0;
        char name[64] = "mark";
        int n = std::sscanf(item.c_str(), "%f,%f,%63s", &x, &y, name);
        if (n >= 2) out.push_back({{x, y}, name});
    }
}

} // namespace

bool renderLevel(Vfs& vfs, const DefDatabase& db, ResourceCache& cache, MeshRenderer& renderer,
                 const std::string& levelRef, const LevelRenderOptions& opts, Image& out, LevelRenderStats* stats,
                 std::string& error) {
    LoadedLevel loaded;
    if (!loadLevelByRef(vfs, levelRef, loaded, &error)) return false;
    const TerrainStyle& style = loaded.style;
    Terrain terrain;
    if (!terrain.build(loaded.data, style)) { error = "terrain build failed"; return false; }
    TerrainRenderer terrainRenderer;
    WaterRenderer waterRenderer;
    if (!terrainRenderer.build(terrain, vfs, &error)) return false;
    if (!waterRenderer.build(terrain, vfs, &error)) return false;

    RenderTarget target;
    if (!target.create(opts.width, opts.height, opts.msaa)) { error = "could not create render target"; return false; }
    target.bind();
    setViewport(0, 0, opts.width, opts.height);

    ShadowRenderer shadowRenderer;
    SpriteRenderer spriteRenderer;
    GroundMarkRenderer markRenderer;
    if (!shadowRenderer.init(&error) || !spriteRenderer.init(&error) || !markRenderer.init(&error)) return false;

    const float aspect = static_cast<float>(opts.width) / static_cast<float>(opts.height);
    const float mapLength = static_cast<float>(terrain.height()) * kHmapCellSize;
    const float mapWidth = static_cast<float>(terrain.width()) * kHmapCellSize;

    SceneLighting light;
    light.sunColor = Vec3{style.sun[0], style.sun[1], style.sun[2]};
    light.sunDirection = Vec3{style.sun[3], style.sun[4], style.sun[5]};
    light.ambientColor = Vec3{style.sun[6], style.sun[7], style.sun[8]};
    light.fogColor = Vec3{style.fogColor[0], style.fogColor[1], style.fogColor[2]};
    light.fogStart = style.fogNear;
    light.fogEnd = style.fogFar;
    if (!style.hasFog) { light.fogStart = 1.0e8f; light.fogEnd = 2.0e8f; }

    Camera cam;
    cam.aspect = aspect;
    float y0 = 0.0f, y1 = mapLength; // world y range of interest for objects
    if (!opts.overview) {
        int mode = std::min(std::max(opts.cameraMode, 0), 3);
        GameCamera gc = computeGameCamera(mode, opts.scroll, opts.cameraX, aspect, style.hasFog, style.fogFar);
        const CameraPreset& p = kGameCameraPresets[mode];
        float t = degToRad(-p.pitchDegrees);
        cam.eye = gc.position;
        cam.target = gc.position + Vec3{0.0f, std::sin(t), -std::cos(t)};
        cam.worldUp = Vec3{0.0f, std::cos(t), std::sin(t)};
        cam.fovYDegrees = p.fovDegrees;
        cam.nearPlane = gc.nearPlane;
        cam.farPlane = gc.farPlane;
        y0 = opts.scroll - 300.0f;
        y1 = opts.scroll + 2100.0f;
    } else {
        float from = std::max(0.0f, opts.scroll);
        float len = opts.span > 0.0f ? opts.span : mapLength - from;
        y0 = from;
        y1 = from + len;
        const float fov = 30.0f;
        float half = std::tan(degToRad(fov) * 0.5f);
        float dist = std::max(len * 0.5f / half, (mapWidth * 0.5f / aspect) / half) * 1.02f;
        Vec3 centre{mapWidth * 0.5f, from + len * 0.5f, 0.0f};
        cam.eye = centre + Vec3{0.0f, 0.0f, dist};
        cam.target = centre;
        cam.worldUp = Vec3{0.0f, 1.0f, 0.0f};
        cam.fovYDegrees = fov;
        cam.nearPlane = dist * 0.3f;
        cam.farPlane = dist * 2.0f;
        light.fogStart = 1.0e8f; // no distance fog in the overview
        light.fogEnd = 2.0e8f;
    }

    Vec3 clearColour = style.hasFog ? light.fogColor : Vec3{0.0f, 0.0f, 0.0f};
    if (opts.overview) clearColour = Vec3{0.05f, 0.05f, 0.08f};
    clear({clearColour.x, clearColour.y, clearColour.z, 1.0f}, true);

    TerrainViewParams tv;
    tv.view = cam.viewMatrix();
    tv.projection = cam.projMatrix();
    tv.cameraPos = cam.eye;
    tv.fogColour = light.fogColor;
    tv.fogStart = light.fogStart;
    tv.fogEnd = light.fogEnd;
    tv.mapPos = opts.scroll;

    // Objects: every placement in the window, in the engine's draw order (the entity pass
    // walks newest first, i.e. the highest y first; parents before children).
    std::vector<Material> materials;
    materials.reserve(4096); // element addresses must stay stable
    std::unordered_map<const ObjectDef*, size_t> materialOf;
    std::vector<PartInfo> parts;
    std::vector<ShadowPart> shadowParts;
    std::vector<SpriteInstance> spriteParts;
    std::vector<DynamicLight> lightCandidates;
    std::unordered_map<std::string, std::unique_ptr<ShadowMap>> shadowMaps;
    std::unordered_map<std::string, TypeEntry> types;
    const Vec3 towardsSun = normalize(Vec3{style.sun[3], style.sun[4], style.sun[5]});
    int envParts = 0;
    int placementsUsed = 0;
    if (opts.objects) {
        std::vector<const Placement*> order;
        for (const Placement& p : loaded.data.placements) order.push_back(&p);
        std::stable_sort(order.begin(), order.end(), [](const Placement* a, const Placement* b) { return a->y < b->y; });
        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            const Placement& p = **it;
            Vec3 pos = p.spawnPosition();
            if (pos.y < y0 || pos.y > y1) continue;
            const std::string& name = loaded.data.typeName(p);
            auto found = types.find(name);
            if (found == types.end()) {
                TypeEntry te;
                ObjectTree::BuildOptions bo;
                bo.night = style.night;
                te.tree = ObjectTree::build(db, vfs, name, bo);
                te.valid = !te.tree.nodes().empty();
                if (te.valid && te.tree.nodes()[0].def) te.rootFlags = te.tree.nodes()[0].def->flags;
                else if (!te.valid) AS3D_WARN("level: object type '%s' not found", name.c_str());
                found = types.emplace(name, std::move(te)).first;
            }
            TypeEntry& te = found->second;
            if (!te.valid) continue;
            placementsUsed++;

            float ground = terrain.heightAt(pos.x, pos.y);
            bool tilt = false;
            if (te.rootFlags & FL_ONWATER) pos.z = style.hasWater ? style.waterLevel : ground;
            else if (te.rootFlags & FL_ONGROUND) { pos.z = ground; tilt = (te.rootFlags & 3u) == 3u; }
            else pos.z = std::max(ground, style.hasWater ? style.waterLevel : ground) + kAirObjectHeight;

            ObjectTree tree = te.tree; // per-instance copy
            std::vector<ObjectNode>& nodes = tree.nodes();
            nodes[0].localOffset = pos;
            nodes[0].angles = Vec3{0.0f, 0.0f, p.yawDegrees()};
            tree.updateTransforms();
            Mat4 extra = Mat4::identity();
            if (tilt) {
                // The engine samples the terrain at three points around the object (hmap.md
                // "Spawning") and builds its axes from that plane.
                Vec3 a{pos.x - 10.0f, pos.y + 15.0f, terrain.heightAt(pos.x - 10.0f, pos.y + 15.0f)};
                Vec3 b{pos.x + 10.0f, pos.y + 15.0f, terrain.heightAt(pos.x + 10.0f, pos.y + 15.0f)};
                Vec3 c{pos.x, pos.y - 25.0f, terrain.heightAt(pos.x, pos.y - 25.0f)};
                Vec3 n = cross(c - a, b - a);
                if (n.z < 0) n = n * -1.0f;
                if (length(n) > 1e-6f) extra = tiltAround(pos, normalize(n));
            }
            std::vector<char> hidden(nodes.size(), 0);
            for (size_t i = 0; i < nodes.size(); i++) {
                const ObjectNode& n = nodes[i];
                if (!n.active || (n.parent >= 0 && hidden[static_cast<size_t>(n.parent)])) hidden[i] = 1;
                if (hidden[i] || n.kind != NodeKind::Object) continue;
                if (!n.def || (n.def->flags & FL_NODRAW)) continue;
                const Mat4 world = extra * n.world;
                const Vec3 wpos{world.at(3, 0), world.at(3, 1), world.at(3, 2)};
                const float yaw = radToDeg(std::atan2(world.at(0, 1), world.at(0, 0)));

                // Entity lights (spec 2.4): position = entity origin, spot direction rotated by the axes.
                if (opts.dataLights && (n.def->hasLight || n.def->hasLightDir) && n.def->lightRadius > 0) {
                    Vec3 col{n.def->lightColor[0], n.def->lightColor[1], n.def->lightColor[2]};
                    float radius = static_cast<float>(n.def->lightRadius);
                    if (n.def->hasLightDir) {
                        Vec3 d{n.def->lightDir[0], n.def->lightDir[1], n.def->lightDir[2]};
                        Vec3 wd = Vec3{world.at(0, 0), world.at(0, 1), world.at(0, 2)} * d.x +
                                  Vec3{world.at(1, 0), world.at(1, 1), world.at(1, 2)} * d.y +
                                  Vec3{world.at(2, 0), world.at(2, 1), world.at(2, 2)} * d.z;
                        lightCandidates.push_back(DynamicLight::spotLight(wpos, col, radius, wd, n.def->lightConeAngle));
                    } else {
                        lightCandidates.push_back(DynamicLight::point(wpos, col, radius));
                    }
                }

                if (n.def->type == ObjectType::Sprite || n.def->type == ObjectType::HSprite ||
                    n.def->type == ObjectType::VSprite) {
                    if (!opts.sprites || !n.def->hasBbox) continue;
                    SpriteInstance sp;
                    sp.kind = n.def->type == ObjectType::Sprite ? SpriteKind::Billboard
                              : n.def->type == ObjectType::HSprite ? SpriteKind::Horizontal : SpriteKind::Vertical;
                    sp.origin = wpos;
                    sp.yawDegrees = yaw;
                    sp.colour = n.colour;
                    sp.minX = n.def->bboxMin[0]; sp.minY = n.def->bboxMin[1];
                    sp.minS = n.def->bboxMin[2]; sp.minT = n.def->bboxMin[3];
                    sp.maxX = n.def->bboxMax[0]; sp.maxY = n.def->bboxMax[1];
                    sp.maxS = n.def->bboxMax[2]; sp.maxT = n.def->bboxMax[3];
                    if (n.def->hasFrames && n.def->frameStart > 0) {
                        sp.frameCols = n.def->frameStart; // render-pipeline.md 11.3: columns and rows
                        sp.frameRows = std::max(n.def->frameEnd, 1);
                    }
                    sp.texture = cache.texture(n.def->skin).texture;
                    sp.blend = toDecalBlend(n.def->blend);
                    sp.noDepthTest = (n.def->rflag & RF_NODEPTHTEST) != 0;
                    sp.noDepthWrite = (n.def->rflag & RF_NODEPTHWRITE) != 0;
                    spriteParts.push_back(sp);
                    continue;
                }
                if (n.def->type == ObjectType::Mark) {
                    if (opts.marks)
                        addMarkObject(db, cache, terrain, markRenderer, n.def->name, wpos,
                                      Vec3{n.colour.x, n.colour.y, n.colour.z});
                    continue;
                }

                if (n.def->model.empty()) continue;
                const GpuMesh& mesh = cache.mesh(n.def->model);
                if (!mesh.valid) continue;
                auto mi = materialOf.find(n.def);
                if (mi == materialOf.end()) {
                    if (materials.size() >= materials.capacity()) { error = "too many distinct object definitions"; return false; }
                    materials.push_back(Material::fromObjectDef(*n.def, cache));
                    if (!opts.envmaps) materials.back().envModeRaw = 0;
                    mi = materialOf.emplace(n.def, materials.size() - 1).first;
                }
                const Material& mat = materials[mi->second];
                if (mat.envTexture && mat.envModeRaw != 0 && mesh.model.smoothNormals) envParts++;
                parts.push_back({&mesh, &mat, world, n.colour});

                ShadowSpec ss = shadowSpec(n.def->shadow);
                if (opts.shadows && ss.any && mat.sortBucket != SortBucket::Effect) {
                    int steps = ss.kind == ShadowKind::Projected ? p.rotationSteps : 0;
                    std::string key = n.def->model + "|" + n.def->skin + "|" +
                                      std::to_string(static_cast<int>(ss.kind)) + "|" +
                                      std::to_string(static_cast<int>(ss.quality)) + "|" + std::to_string(steps);
                    auto sm = shadowMaps.find(key);
                    if (sm == shadowMaps.end()) {
                        auto map = std::make_unique<ShadowMap>();
                        if (!shadowRenderer.generate(mesh.model, mat.texture, ss.kind, towardsSun, steps, ss.quality, *map))
                            map.reset();
                        if (std::getenv("AS3D_DEBUG_SHADOWS"))
                            std::fprintf(stderr, "shadow %s kind %d q %d: %s bounds %d..%d x %d..%d size %dx%d\n", key.c_str(), (int)ss.kind, (int)ss.quality, map ? "ok" : "FAILED", map ? map->bounds.xmin : 0, map ? map->bounds.xmax : 0, map ? map->bounds.ymin : 0, map ? map->bounds.ymax : 0, map ? map->width : 0, map ? map->height : 0);
                        sm = shadowMaps.emplace(key, std::move(map)).first;
                    }
                    if (sm->second) shadowParts.push_back({sm->second.get(), wpos, yaw});
                }
            }
        }
    }

    // Requested extra marks.
    if (opts.marks && !opts.extraMarks.empty()) {
        std::vector<std::pair<Vec2, std::string>> extraMarks;
        parseMarks(opts.extraMarks, opts.scroll, extraMarks);
        for (const auto& m : extraMarks) {
            Vec3 pos{m.first.x, m.first.y, terrain.heightAt(m.first.x, m.first.y)};
            if (!addMarkObject(db, cache, terrain, markRenderer, m.second, pos, Vec3{1, 1, 1}))
                AS3D_WARN("level: mark '%s' is not a mark object or touches no terrain", m.second.c_str());
        }
    }

    // The frame's light list: extra lights first, then object lights that can reach the view,
    // nearest to the camera first, at most 32 (the original drops the rest; which ones it
    // keeps depends on its entity order, so this viewer keeps the nearest).
    DynamicLightList lightList;
    for (const DynamicLight& l : opts.extraLights) lightList.add(l);
    {
        Mat4 vp = cam.viewProjMatrix();
        std::vector<DynamicLight> visible;
        for (const DynamicLight& l : lightCandidates)
            if (lightMayBeVisible(l, vp)) visible.push_back(l);
        std::stable_sort(visible.begin(), visible.end(), [&](const DynamicLight& a, const DynamicLight& b) {
            return length(a.position - cam.eye) < length(b.position - cam.eye);
        });
        for (const DynamicLight& l : visible) lightList.add(l);
    }
    renderer.setDynamicLights(lightList.data(), lightList.size());
    tv.lights = lightList.data();
    tv.lightCount = lightList.size();

    DecalViewParams dv;
    dv.view = tv.view;
    dv.projection = tv.projection;
    dv.fogColour = tv.fogColour;
    dv.fogStart = tv.fogStart;
    dv.fogEnd = tv.fogEnd;
    SpriteViewParams sv;
    sv.view = tv.view;
    sv.projection = tv.projection;
    sv.fogColour = tv.fogColour;
    sv.fogStart = tv.fogStart;
    sv.fogEnd = tv.fogEnd;

    // Pass order of render-pipeline.md 1.1: terrain, opaque list, water, transparent and
    // effect lists.
    terrainRenderer.render(tv);
    markRenderer.draw(dv);
    std::vector<ShadowInstance> shadowInstances;
    shadowParts.erase(shadowParts.begin(),
                      shadowParts.begin() + std::min(shadowParts.size(), static_cast<size_t>(std::max(opts.shadowFirst, 0))));
    if (opts.shadowLimit >= 0 && shadowParts.size() > static_cast<size_t>(opts.shadowLimit))
        shadowParts.resize(static_cast<size_t>(opts.shadowLimit));
    for (const ShadowPart& sp : shadowParts) shadowInstances.push_back({sp.map, sp.origin, sp.yaw});
    shadowRenderer.draw(terrain, shadowInstances.data(), shadowInstances.size(), dv);
    renderer.begin(cam, light);
    for (const PartInfo& p : parts)
        if (p.material->sortBucket == SortBucket::Opaque) renderer.submit(*p.mesh, *p.material, p.model, p.colour);
    renderer.end();
    waterRenderer.render(tv);
    renderer.begin(cam, light);
    for (const PartInfo& p : parts)
        if (p.material->sortBucket != SortBucket::Opaque) renderer.submit(*p.mesh, *p.material, p.model, p.colour);
    renderer.end();
    spriteRenderer.draw(spriteParts.data(), spriteParts.size(), sv);

    if (!target.readPixels(out)) { error = "readPixels failed"; return false; }
    if (stats) {
        stats->placements = placementsUsed;
        stats->drawnObjects = static_cast<int>(parts.size());
        stats->visibleChunks = terrainRenderer.lastVisibleChunks();
        stats->missingTextures = terrainRenderer.missingTextures() + waterRenderer.missingTextures();
        stats->hasWater = waterRenderer.active();
        stats->shadowsDrawn = static_cast<int>(shadowInstances.size());
        stats->spritesDrawn = spriteRenderer.lastSpriteCount();
        stats->marksDrawn = static_cast<int>(markRenderer.size());
        stats->lightsUsed = static_cast<int>(lightList.size());
        stats->envParts = envParts;
        stats->levelId = style.id;
    }
    return true;
}

} // namespace viewer
