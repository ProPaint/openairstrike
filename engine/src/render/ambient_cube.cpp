// Implements as3d/ambient_cube.h: the CPU form of the mesh vertex shader's ambient cube
// (engine/src/render/gles/shaders.cpp, kMeshVertexSrc). Keep the two in step; the test
// apps/tests/ambient_cube_test.cpp holds a line-by-line transcription of the GLSL.
#include "as3d/ambient_cube.h"

#include <algorithm>
#include <cmath>

#include "as3d/scene.h"

namespace as3d {

namespace {

constexpr float kHalfPi = 1.57079632679f; // the shader's constant, not M_PI / 2

// GLSL normalize(): v / length(v), no zero guard (the callers never pass a zero vector:
// the weights add 1e-9 to z first, a model axis is never zero).
Vec3 glslNormalize(const Vec3& v) {
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return Vec3{v.x / len, v.y / len, v.z / len};
}

u8 quantize(float w) { return static_cast<u8>(std::lround(std::clamp(w, 0.0f, 1.0f) * 255.0f)); }

} // namespace

void ambientCubeWeights(const Vec3& modelSpaceNormal, float out[6]) {
    Vec3 n = glslNormalize(Vec3{modelSpaceNormal.x, modelSpaceNormal.y, modelSpaceNormal.z + 1e-9f});
    float a = std::acos(std::min(std::fabs(n.x), 1.0f)) / kHalfPi;
    float b = std::acos(std::min(std::fabs(n.z), 1.0f)) / kHalfPi;
    float wx = b - a * b;
    float wy = a * b;
    float wz = 1.0f - b;
    const float w[3] = {wx, wy, wz};
    const float c[3] = {n.x, n.y, n.z};
    for (int i = 0; i < 3; i++) {
        out[i] = c[i] >= 0.0f ? w[i] : 0.0f;
        out[i + 3] = c[i] >= 0.0f ? 0.0f : w[i];
    }
}

void modelAxes(const Mat4& model, Vec3 axes[3]) {
    for (int i = 0; i < 3; i++) axes[i] = glslNormalize(Vec3{model.at(i, 0), model.at(i, 1), model.at(i, 2)});
}

void ambientCubeColors(const SceneLighting& lighting, const Vec3 axes[3], const Vec3 dynCube[6], Vec3 out[6]) {
    Vec3 L = glslNormalize(lighting.sunDirection);
    for (int i = 0; i < 3; i++) {
        float d = dot(axes[i], L);
        out[i] = lighting.ambientColor + lighting.sunColor * std::max(d, 0.0f) + dynCube[i];
        out[i + 3] = lighting.ambientColor + lighting.sunColor * std::max(-d, 0.0f) + dynCube[i + 3];
    }
}

Vec3 ambientCubeLight(const float w[6], const Vec3 c[6]) {
    Vec3 sum{0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 6; i++) sum = sum + c[i] * w[i];
    return sum;
}

AmbientCubeWeightsU8 quantizeAmbientCubeWeights(const float w[6]) {
    AmbientCubeWeightsU8 q{};
    for (int i = 0; i < 3; i++) {
        q.pos[i] = quantize(w[i]);
        q.neg[i] = quantize(w[i + 3]);
    }
    q.pos[3] = 0;
    q.neg[3] = 0;
    return q;
}

void dequantizeAmbientCubeWeights(const AmbientCubeWeightsU8& q, float out[6]) {
    for (int i = 0; i < 3; i++) {
        out[i] = q.pos[i] / 255.0f;
        out[i + 3] = q.neg[i] / 255.0f;
    }
}

void bakeAmbientCubeWeights(const RenderVertex* vertices, size_t count, std::vector<AmbientCubeWeightsU8>& weights) {
    weights.resize(count);
    for (size_t i = 0; i < count; i++) {
        float w[6];
        ambientCubeWeights(vertices[i].normal, w);
        weights[i] = quantizeAmbientCubeWeights(w);
    }
}

bool buildRenderMeshBaked(const ModelData& model, std::vector<RenderVertex>& vertices, std::vector<u16>& indices,
                          std::vector<AmbientCubeWeightsU8>& weights) {
    weights.clear();
    if (!buildRenderMesh(model, vertices, indices)) return false;
    bakeAmbientCubeWeights(vertices.data(), vertices.size(), weights);
    return true;
}

} // namespace as3d
