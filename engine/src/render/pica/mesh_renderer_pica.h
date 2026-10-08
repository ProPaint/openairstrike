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
//   * environment-mapped materials draw as plain textured models (TODO M3.4: env.v.pica).
// Order: GL's (mesh_renderer.cpp): the records sorted by SortBucket (opaque, trans, effect),
// submission order inside a bucket (render-pipeline.md 1.2: the original never depth-sorts).
#pragma once

#include <cstddef>
#include <vector>

#include "as3d/dynamic_lights.h"
#include "as3d/material_key.h"
#include "as3d/math.h"
#include "as3d/scene.h"

namespace as3d::pica {

struct MeshDrawStats {
    int draws = 0;     // C3D_DrawElements calls
    int vertices = 0;  // vertices of the drawn meshes
    int triangles = 0;
    size_t bytes = 0;  // vertex + index bytes the draws read
};

class MeshPass {
public:
    // Starts a new list (clears the previous one). `lights` (kept up to kMaxDynamicLights) are
    // evaluated at each model origin in submit(); RF_NOLIGHTING and RF_NODLIGHT ignore them.
    void begin(const SceneLighting& lighting, const DynamicLight* lights, size_t lightCount);
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

private:
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
    };
    SceneLighting lighting_;
    std::vector<DynamicLight> lights_;
    std::vector<Record> records_;
    MeshDrawStats stats_;
};

// The sum of every MeshPass::draw since the last pica::frameBegin() (perf logs).
MeshDrawStats meshFrameStats();

} // namespace as3d::pica
