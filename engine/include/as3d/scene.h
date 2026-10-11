// Mesh rendering (WP-30): a resource cache for game-data textures/meshes, a Material
// derived from an ObjectDef, a Camera/SceneLighting pair and a MeshRenderer that sorts
// and draws submitted meshes (GL backend: one GLSL ES 3.00 program; the GPU side of a mesh
// and the renderer's program are private to the backend). See docs/graphics.md for
// the coordinate-system, winding and material-state-table writeups this implements.
//
// `Material::fromObjectDef` takes `const ObjectDef&` by reference, so this header only needs a
// forward declaration of ObjectDef.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "as3d/core.h"
#include "as3d/dynamic_lights.h"
#include "as3d/material_key.h"
#include "as3d/math.h"
#include "as3d/model.h"
#include "as3d/texture.h"

namespace as3d {

class Vfs;
struct ObjectDef;
class DefDatabase;

// ---------------------------------------------------------------------------
// ResourceCache: textures and GPU meshes, cached by normalized game path.
// ---------------------------------------------------------------------------

// The backend's buffers for one mesh (GL: engine/src/render/gles/gpu_mesh_gl.h). Opaque here.
struct GpuMeshBuffers;

// One .mdl file's GPU-ready form. Kept alongside the parsed ModelData so callers can
// still read tags/bounds without re-parsing the file. The GPU side is backend-defined and
// only reached through buffers() by that backend's renderers.
struct GpuMesh {
    GpuMesh();
    ~GpuMesh();
    GpuMesh(GpuMesh&&) noexcept;
    GpuMesh& operator=(GpuMesh&&) noexcept;
    GpuMesh(const GpuMesh&) = delete;
    GpuMesh& operator=(const GpuMesh&) = delete;

    ModelData model;
    size_t indexCount = 0;
    // false for the placeholder returned when the file is missing/corrupt/unbuildable:
    // 0 indices, nothing is drawn (there is no reasonable stand-in geometry, unlike a
    // missing texture, which gets a visible magenta checker -- see ResourceCache::texture).
    bool valid = false;

    // Backend: uploads a buildRenderMesh triangle list (replacing any previous one), sets
    // indexCount, and sets valid when there is at least one index. Needs the graphics
    // context (GL: a current context). Returns valid.
    bool upload(const RenderVertex* vertices, size_t vertexCount, const u16* indices, size_t indexCount);

    GpuMeshBuffers* buffers() const { return buffers_.get(); }

private:
    std::unique_ptr<GpuMeshBuffers> buffers_;
};

// 3DS R4.11 (docs/3ds-port.md "The sequels on the console (round 4, R4.11)", run.ini skin_half):
// a model skin whose larger side is at least `minSide` texels is created from level 1 of its mip
// chain (TextureOptions::baseLevel = 1: half the sides, a quarter of the bytes, the same texels
// the minified levels had), so the sequels' skins fit the VRAM budget. 0 (the default, and the
// desktop's GL backend in every case) leaves every skin at its size. Set before a level's
// textures load; a process-wide setting read by every ResourceCache::texture call.
void setSkinHalfMinSide(int minSide);
int skinHalfMinSide();

class ResourceCache {
public:
    explicit ResourceCache(Vfs& vfs);

    struct LoadedTexture {
        const Texture2D* texture = nullptr; // never null: the magenta-checker placeholder on failure
        bool hasAlpha = false;
    };

    // Loads and caches by normalizePath(gamePath). A missing or malformed file logs one
    // warning (deduplicated per path) and returns the shared magenta-checker
    // placeholder -- never null, never a crash. Mipmapped trilinear filtering and
    // repeat wrap by default, matching the work package's defaults.
    // `skin`: a model skin (Material::fromObjectDef), the textures skinHalfMinSide() applies to.
    LoadedTexture texture(const std::string& gamePath, bool mipmaps = true, bool skin = false);

    // Loads and caches a GPU mesh built (via buildRenderMesh) from a .mdl file. A
    // missing/corrupt/empty file logs one warning and returns an empty placeholder
    // (GpuMesh::valid == false); this covers the 4 zero-byte files, the 2 corrupt
    // files, and any of the 13 documented unresolved references that point at a model.
    const GpuMesh& mesh(const std::string& gamePath);

    const std::vector<std::string>& warnings() const { return warnings_; }

    // The sequels draw an object whose texture is missing untextured (white × vertex colour,
    // as2/render-pipeline.delta.md 3.2 and 9.1, RenderRules::missingTextureWhite): texture()
    // then returns a white texture instead of the magenta checker for every path that fails
    // to load from now on. Set before the textures of a level load.
    void setMissingTextureWhite(bool on) { missingWhite_ = on; }
    // True when `texture` is one of the placeholders returned for a missing file.
    bool isPlaceholder(const Texture2D* texture) const;

private:
    Vfs& vfs_;
    bool missingWhite_ = false;
    std::unique_ptr<Texture2D> whiteTexture_;
    const Texture2D& whiteTexture();
    std::unordered_map<std::string, std::unique_ptr<Texture2D>> textures_;
    std::unordered_map<std::string, bool> textureAlpha_;
    std::unordered_map<std::string, std::unique_ptr<GpuMesh>> meshes_;
    std::unordered_set<std::string> warnedKeys_;
    std::vector<std::string> warnings_;
    std::unique_ptr<Texture2D> placeholderTexture_;

    const Texture2D& placeholderTexture();
    void warnOnce(const std::string& key, const std::string& message);
};

// ---------------------------------------------------------------------------
// Material: derived from an ObjectDef (see docs/spec/obj.md's blend/envmode/rflag/sort
// statement tables). ENV_* modes, dynamic lights and shadows are out of scope for this
// package -- see the TODO in material.cpp; the base texture is always drawn.
// ---------------------------------------------------------------------------

enum class MaterialBlend { Opaque, Alpha, Add, Filter };
enum class SortBucket { Opaque, Trans, Effect };

struct Material {
    const Texture2D* texture = nullptr; // never null (ResourceCache placeholder on failure)
    bool textureHasAlpha = false;
    // The game path `texture` was loaded from (the skin, else the model's own texture path; the
    // skin override of WorldScene::materialFor), empty without one. A backend that cannot read a
    // texture back (the PICA's CPU shadow silhouettes) reloads the image from it.
    std::string texturePath;
    MaterialBlend blend = MaterialBlend::Opaque;
    // as3d::EnvMode's underlying value (obj.md: 0 none, 1 glitter, 2 chrome, 3 quad);
    // not implemented, kept only so a TODO/debug view can report which mode was asked
    // for. Storing the raw int (not the enum) keeps this header defs.h-free.
    int envModeRaw = 0;
    // The obj `envmap` texture (render-pipeline.md 4.3), null without one. Drawn through
    // envModeRaw only on smooth-normal models; set envModeRaw to 0 to switch it off.
    const Texture2D* envTexture = nullptr;
    u32 rflag = 0; // raw copy of ObjectDef::rflag, for callers that want the bits directly
    bool noLighting = false;
    bool noCulling = false;
    bool noDepthTest = false;
    bool noDepthWrite = false;
    // Alpha-tested cutout, drawn in the opaque bucket: set when blend == Opaque but the
    // texture has a real alpha channel (see material.cpp for the reasoning).
    bool alphaTest = false;
    SortBucket sortBucket = SortBucket::Opaque;
    Vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};

    // See this header's top comment for why `def` is a forward-declared reference.
    static Material fromObjectDef(const ObjectDef& def, ResourceCache& cache);
};

// Sets the backend's blend/depth/cull state for one material: the same as
// applyMaterialState(materialKeyFor(material)). The entire flag-bits -> state mapping lives
// in materialKeyFor (as3d/material_key.h, engine/src/render/material_key.cpp), driven by a
// small table, so a later render-pipeline spec can correct it in one place.
void applyMaterialState(const Material& material);

// Debug/verification hook (used by the viewer's --cull option, which produced the winding
// evidence in docs/graphics.md): forces every material's cull mode. -1 = no override
// (default), 0 = off, 1 = back, 2 = front.
void setCullDebugOverride(int mode);

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------

struct Camera {
    Vec3 eye{0.0f, -10.0f, 5.0f};
    Vec3 target{0.0f, 0.0f, 0.0f};
    // Z-up: this field is deliberately the ENTIRE model/level-space-to-view-space
    // conversion (docs/graphics.md, "Coordinate system"). as3d::lookAt() doesn't assume
    // any particular "up" axis, so passing the game's own Z-up world convention here
    // means every position/angle coming from game data or (later) scripts is usable
    // completely unchanged -- nothing elsewhere in the pipeline flips an axis.
    Vec3 worldUp{0.0f, 0.0f, 1.0f};
    float fovYDegrees = 55.0f;
    float aspect = 4.0f / 3.0f;
    float nearPlane = 0.1f;
    float farPlane = 4000.0f;

    Mat4 viewMatrix() const { return lookAt(eye, target, worldUp); }
    Mat4 projMatrix() const { return perspective(fovYDegrees, aspect, nearPlane, farPlane); }
    Mat4 viewProjMatrix() const { return projMatrix() * viewMatrix(); }

    // Three-quarter, auto-framed view of an axis-aligned box: `yawDeg`/`pitchDeg` orbit
    // its center, `distFactor` scales the default framing distance (1.0 keeps the
    // box's bounding sphere comfortably inside the field of view).
    static Camera framing(const Vec3& boundsMin, const Vec3& boundsMax, float yawDeg, float pitchDeg,
                          float distFactor, float aspect);

    // Placeholder for the game's own top-down gameplay camera (being specified
    // elsewhere -- see docs/graphics.md, "Top-down camera"): positioned `height` above
    // `target`, tilted `tiltDeg` forward off straight-down and offset back along -Y so
    // the view also looks slightly "ahead" along +Y, the direction the player's
    // helicopter flies. All parameters are exposed (not baked in) so the real spec can
    // replace this trivially.
    static Camera topDown(const Vec3& target, float height, float tiltDeg, float aspect);
};

// ---------------------------------------------------------------------------
// SceneLighting
// ---------------------------------------------------------------------------

struct SceneLighting {
    // Defaults below are mission 1's own values (maps\levels.txt via DefDatabase; see
    // docs/spec/levels-txt.md), used verbatim when fromMission1() can't find the level.
    // Points TOWARD the sun (used directly as the Lambert light vector: diff =
    // max(dot(normal, normalize(sunDirection)), 0)), not the direction light travels.
    Vec3 sunDirection{-1.0f, -0.5f, 1.0f};
    Vec3 sunColor{0.9f, 0.9f, 0.9f};
    Vec3 ambientColor{0.3f, 0.3f, 0.1f};
    Vec3 fogColor{0.4f, 0.4f, 0.3f};
    float fogStart = 650.0f;
    float fogEnd = 900.0f;

    // Reads the level whose id() == "mission1" from `db` and groups its 9-float "sun"
    // statement as (diffuse RGB, direction, ambient RGB) -- GUESS on the grouping per
    // docs/spec/levels-txt.md, but the only reading consistent with the shipped values.
    // Falls back to the hardcoded defaults above if `db` has no such level.
    static SceneLighting fromMission1(const DefDatabase& db);
};

// ---------------------------------------------------------------------------
// MeshRenderer
// ---------------------------------------------------------------------------

class MeshRenderer {
public:
    MeshRenderer();
    ~MeshRenderer();
    MeshRenderer(const MeshRenderer&) = delete;
    MeshRenderer& operator=(const MeshRenderer&) = delete;

    // Compiles the shared shader program (GL: engine/src/render/gles/shaders.cpp). Safe to call
    // more than once (a no-op once already valid). Returns false, with `error` set, on
    // a shader compile/link failure.
    bool init(std::string* error = nullptr);

    void begin(const Camera& camera, const SceneLighting& lighting);
    // `mesh`/`material` must outlive end() (submitted by pointer, not copied).
    void submit(const GpuMesh& mesh, const Material& material, const Mat4& modelMatrix,
                const Vec4& colour = Vec4{1.0f, 1.0f, 1.0f, 1.0f});
    // Dynamic lights for models (render-pipeline.md 2.4): kept until replaced, so set them
    // once per frame before the first begin(). Each draw evaluates them once at the model
    // origin into the ambient cube; RF_NOLIGHTING and RF_NODLIGHT materials ignore them.
    void setDynamicLights(const DynamicLight* lights, size_t count);
    // Game time in seconds; drives the ENV_QUAD texture rotation.
    void setTime(float seconds);
    // The sequels' environment coordinates and ENV_GLITTER alpha (as2/render-pipeline.delta.md
    // 4.3, RenderRules::envViewNormal; as3d/envmap.h viewNormalEnvUv). Off: the GL sphere map.
    void setEnvViewNormal(bool on);
    // Sorts (SORT_OPAQUE, then SORT_TRANS back-to-front by distance to the camera, then
    // SORT_EFFECT) and issues the draw calls.
    void end();

private:
    struct Impl; // backend (GL: engine/src/render/gles/mesh_renderer.cpp)
    std::unique_ptr<Impl> impl_;
};

} // namespace as3d
