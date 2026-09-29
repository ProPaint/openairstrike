#include "level_render.h"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "as3d/game_camera.h"
#include "as3d/object_tree.h"
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
    std::unordered_map<std::string, TypeEntry> types;
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
                if (!n.def || n.def->model.empty() || (n.def->flags & FL_NODRAW)) continue;
                const GpuMesh& mesh = cache.mesh(n.def->model);
                if (!mesh.valid) continue;
                auto mi = materialOf.find(n.def);
                if (mi == materialOf.end()) {
                    if (materials.size() >= materials.capacity()) { error = "too many distinct object definitions"; return false; }
                    materials.push_back(Material::fromObjectDef(*n.def, cache));
                    mi = materialOf.emplace(n.def, materials.size() - 1).first;
                }
                parts.push_back({&mesh, &materials[mi->second], extra * n.world, n.colour});
            }
        }
    }

    // Pass order of render-pipeline.md 1.1: terrain, opaque list, water, transparent and
    // effect lists.
    terrainRenderer.render(tv);
    renderer.begin(cam, light);
    for (const PartInfo& p : parts)
        if (p.material->sortBucket == SortBucket::Opaque) renderer.submit(*p.mesh, *p.material, p.model, p.colour);
    renderer.end();
    waterRenderer.render(tv);
    renderer.begin(cam, light);
    for (const PartInfo& p : parts)
        if (p.material->sortBucket != SortBucket::Opaque) renderer.submit(*p.mesh, *p.material, p.model, p.colour);
    renderer.end();

    if (!target.readPixels(out)) { error = "readPixels failed"; return false; }
    if (stats) {
        stats->placements = placementsUsed;
        stats->drawnObjects = static_cast<int>(parts.size());
        stats->visibleChunks = terrainRenderer.lastVisibleChunks();
        stats->missingTextures = terrainRenderer.missingTextures() + waterRenderer.missingTextures();
        stats->hasWater = waterRenderer.active();
        stats->levelId = style.id;
    }
    return true;
}

} // namespace viewer
