// The model "ambient cube" vertex lighting (docs/spec/render-pipeline.md 2.3) on the CPU,
// platform neutral. The GL backend evaluates it per vertex in the mesh vertex shader
// (engine/src/render/gles/shaders.cpp); these functions reproduce that shader exactly so a
// backend without per-vertex acos (PICA) can bake the weights per vertex once and compute the
// six colours once per draw: light = sum over the 6 slots of weight * colour.
//
// Slots: 0, 1, 2 = the +X, +Y, +Z sides of the model's local axes; 3, 4, 5 = -X, -Y, -Z
// (the same order as accumulateModelLights' `cube`). For each axis only the side the normal
// points to gets a weight, so at most three slots are non-zero, and the three weights sum to 1.
#pragma once

#include <vector>

#include "as3d/core.h"
#include "as3d/math.h"
#include "as3d/model.h"

namespace as3d {

struct SceneLighting;

// The six weights of a model-space normal (need not be unit length), exactly as the shader:
// n = normalize(normal + (0, 0, 1e-9)); a = acos(min(|n.x|, 1)) / (pi/2);
// b = acos(min(|n.z|, 1)) / (pi/2); wx = b - a*b, wy = a*b, wz = 1 - b, each on the + slot
// when the component is >= 0, else on the - slot.
void ambientCubeWeights(const Vec3& modelSpaceNormal, float out[6]);

// The world-space directions of a model matrix's local X, Y, Z axes, normalised (the shader's
// normalize(uModel[i].xyz); also the `axes` accumulateModelLights takes).
void modelAxes(const Mat4& model, Vec3 axes[3]);

// The six colours of one draw: ambient + max(+-dot(axis_i, normalize(sunDirection)), 0) * sun
// + the dynamic-light cube (accumulateModelLights' result, zero without lights), as the shader
// builds cPos/cNeg. `axes` from modelAxes.
void ambientCubeColors(const SceneLighting& lighting, const Vec3 axes[3], const Vec3 dynCube[6], Vec3 out[6]);

// sum_i w[i] * c[i]: the vertex light before the shader's clamp to [0, 1].
Vec3 ambientCubeLight(const float w[6], const Vec3 c[6]);

// Quantised weights for a vertex stream: two u8x4 attributes, (+X, +Y, +Z, 0) and
// (-X, -Y, -Z, 0), each weight stored as round(w * 255) and read back as q / 255 (so within
// 0.5/255 of the float weight; the three non-zero ones sum to 255 +- 1).
struct AmbientCubeWeightsU8 {
    u8 pos[4];
    u8 neg[4];
};

AmbientCubeWeightsU8 quantizeAmbientCubeWeights(const float w[6]);
void dequantizeAmbientCubeWeights(const AmbientCubeWeightsU8& q, float out[6]);

// buildRenderMesh plus one quantised weight entry per output vertex (from its normal), for a
// backend that bakes the ambient cube weights. Same return value and vertices/indices.
bool buildRenderMeshBaked(const ModelData& model, std::vector<RenderVertex>& vertices, std::vector<u16>& indices,
                          std::vector<AmbientCubeWeightsU8>& weights);
// The same bake for an existing vertex list.
void bakeAmbientCubeWeights(const RenderVertex* vertices, size_t count, std::vector<AmbientCubeWeightsU8>& weights);

} // namespace as3d
