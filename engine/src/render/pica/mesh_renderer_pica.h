// The PICA mesh pass (private to the PICA backend and the 3DS app): what MeshRenderer
// (as3d/scene.h) does on the PICA, split so that a frame prepares its model lists once and
// draws them once per eye (stereo).
//
//     MeshPass pass;
//     pass.begin(lighting, lights, lightCount);          // prepare, once per frame
//     for each record: pass.submit(mesh, material, model, colour);
//     pass.finish();                                     // the bucket order
//     for each eye:                                      // draw, per eye
//         pica::drawOn(target of the eye); depth mapping, fog (setFog/disableFog,
//         setLevelFogColor) and viewport are the caller's;
//         pass.draw(picaEyeProjection(view, eye, aspect) * view.view);
//
// submit() does all the per-model CPU work (the six ambient-cube colours: level sun and ambient
// plus the dynamic lights through accumulateModelLights, exactly as the GL shader's
// cPos/cNeg; the MaterialKey; the TEV choice) so that draw() only issues uniforms and
// C3D_DrawElements. Per draw:
//   * Shader::Mesh, mvp = viewProj * model, cube = the six colours, tint = (1, 1, 1, 1): the
//     vertex colour is clamp(sum w_i cube_i) with alpha 1, as the GL vLight;
//   * lit and textured: tevModulateTexColor (skin * light, alpha = skin alpha);
//     RF_NOLIGHTING: tevModulateTexConstant with the entity colour * material tint (GL uColor);
//     no texture: the vertex colour (lit) or the constant (unlit) alone;
//   * applyMaterialState(materialKeyFor(material)): blend, depth test/write, cull and the fog
//     colour of the blend (black for Add, white for Filter);
//   * environment-mapped materials (M3.4; as GL: Material::envTexture set, an env mode, and a
//     smooth-normal mesh, so one with GpuMeshBuffers::envNormals) draw in ONE draw as the GL
//     mesh shader does: Shader::Env (shaders/env.v.pica: the same light plus texcoord1 = the env
//     coordinate: GL_SPHERE_MAP of the folded normal, or the sequels' view normal, ENV_QUAD
//     rotated by time * 30 degrees), the skin on TEXTURE0, the env map on TEXTURE1 and
//     tevEnvGlitter / tevEnvChrome / tevEnvQuad (tev.h: combineEnvMap) with the vertex colour
//     (lit) or the constant (RF_NOLIGHTING) as the colour. The coordinates use the view given
//     to setEnvironment() (the central camera view, as GL's uView; the same for both eyes).
//     A glitter or chrome material without a skin texture draws as a plain model.
// Order: GL's (mesh_renderer.cpp): the records sorted by SortBucket (opaque, trans, effect),
// submission order inside a bucket (render-pipeline.md 1.2: the original never depth-sorts);
// then, with setMeshStateSort (default), the opaque runs without blending in state order (below).
#pragma once

#include <cstddef>
#include <vector>

#include "as3d/dynamic_lights.h"
#include "as3d/material_key.h"
#include "as3d/math.h"
#include "as3d/scene.h"
#include "shader_set.h"

namespace as3d::pica {

struct MeshDrawStats {
    int draws = 0;     // C3D_DrawElements calls
    int vertices = 0;  // vertices of the drawn meshes
    int triangles = 0;
    size_t bytes = 0;  // vertex + index (+ env normal) bytes the draws read
    int envDraws = 0;  // of `draws`: environment-mapped (Shader::Env)
    int textureBinds = 0; // skin binds that changed texture unit 0
    int stateChanges = 0; // material states issued (applyMaterialState)
    int mergedDraws = 0;   // of `draws`: merged runs of small records (setMeshMerge)
    int mergedRecords = 0; // the records those draws cover
    size_t mergeArenaBytes = 0; // their world-space vertices in the frame arena (first eye)
};

// run.ini mesh_merge= (default off): a run of small records (at most kMergeMaxVertices vertices,
// no environment map) in a row in drawing order with the same skin and material state is one
// draw: the first eye of a frame writes their triangles into the frame arena in world space,
// with each corner's colour as the mesh shader computes it (the lit cube colours, or the unlit
// constant) as 8-bit vertex colours, and draws them with the flat shader. The triangles keep
// their order, so blended effects compose as before; the pixels differ only by the rounding of
// the CPU's vertex transform and colour against the GPU's. Off by default: under Azahar the
// CPU baking costs as much as the draws it saves (docs/3ds-port.md "Sequels' CPU"); a console
// A/B switch.
void setMeshMerge(bool on);
bool meshMerge();
constexpr u32 kMergeMaxVertices = 32;
constexpr u32 kMergeMaxIndices = 48;
constexpr size_t kMergeMaxRunVertices = 6144; // triangle corners of one merged draw

// run.ini mesh_sort= (default on): finish() orders each run of opaque records without blending
// and with depth test and write (whose order only matters for equal depths) by shader, skin, TEV
// and material state, ties in submission order; blended or depth-write-off records keep their
// place. Off: the GL's submission order everywhere (A/B dumps).
void setMeshStateSort(bool on);
bool meshStateSort();

class MeshPass {
public:
    // Starts a new list (clears the previous one). `lights` (kept up to kMaxDynamicLights) are
    // evaluated at each model origin in submit(); RF_NOLIGHTING and RF_NODLIGHT ignore them.
    void begin(const SceneLighting& lighting, const DynamicLight* lights, size_t lightCount);
    // The environment maps' inputs for the following submit() calls (kept across begin()): the
    // camera's view matrix (GL uView), the game time in seconds (ENV_QUAD rotation, GL
    // MeshRenderer::setTime) and the sequels' view-normal coordinates (RenderRules::envViewNormal).
    void setEnvironment(const Mat4& view, float timeSeconds, bool viewNormal);
    // One model. Placeholders (invalid meshes) are skipped. `colour` is the entity colour (used
    // by RF_NOLIGHTING materials only, times material.tint, as GL).
    void submit(const GpuMesh& mesh, const Material& material, const Mat4& model,
                const Vec4& colour = Vec4{1.0f, 1.0f, 1.0f, 1.0f});
    // Puts the records in drawing order. Call once after the last submit().
    void finish();
    // Draws every record with `viewProj`: the PICA projection (tilt included) times the view.
    // Needs pica::initBackend and a frame (pica::drawOn).
    void draw(const Mat4& viewProj);

    size_t size() const { return records_.size(); }
    const MeshDrawStats& lastStats() const { return stats_; } // of the last draw()

    struct Record {
        const GpuMeshBuffers* gpu = nullptr;
        const Texture2D* texture = nullptr;
        MaterialKey key;
        Mat4 model;
        Vec3 cube[6];
        u8 constant[4] = {255, 255, 255, 255};
        bool unlit = false;
        bool textured = true;
        SortBucket bucket = SortBucket::Opaque;
        // Environment map (env != 0: as3d::EnvMapMode Glitter 1, Chrome 2, Quad 3).
        int env = 0;
        bool envAlpha = true; // glitter alpha times the env alpha (off for the sequels)
        float envSelView = 0.0f, envSelSphere = 1.0f;
        const Texture2D* envTexture = nullptr;
        Mat4 modelView;
        float normalRows[3][3] = {};
        float rot[2][2] = {{1.0f, 0.0f}, {0.0f, 1.0f}};
    };

private:
    Mat4 envView_;
    float envTime_ = 0.0f;
    bool envViewNormal_ = false;
    SceneLighting lighting_;
    std::vector<DynamicLight> lights_;
    std::vector<Record> records_; // in submission order
    std::vector<u16> order_;      // the drawing order (finish())
    // The draws (finish()): `count` records from order_[first]; a merged run has count > 1 and
    // its `vertices` triangle corners in the arena (`verts`, built in `arenaFrame`).
    struct DrawItem {
        u16 first = 0, count = 1;
        size_t vertices = 0;
        FlatVertex* verts = nullptr;
        unsigned arenaFrame = ~0u;
    };
    std::vector<DrawItem> items_;
    std::vector<FlatVertex> scratch_;
    void buildItems();
    void bakeRecord(const Record& r, FlatVertex* out);
    MeshDrawStats stats_;
};

// The sum of every MeshPass::draw since the last pica::frameBegin() (perf logs).
MeshDrawStats meshFrameStats();

} // namespace as3d::pica
