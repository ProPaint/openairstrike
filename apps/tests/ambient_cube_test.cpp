// Tests for as3d/ambient_cube.h against a line-by-line C++ transcription of the mesh vertex
// shader's ambient cube (engine/src/render/gles/shaders.cpp, kMeshVertexSrc).
#include "doctest.h"

#include "as3d/ambient_cube.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "as3d/dynamic_lights.h"
#include "as3d/scene.h"

using namespace as3d;

namespace {

// Minimal GLSL vocabulary for the transcription below.
using vec3 = Vec3;
vec3 normalize_(const vec3& v) {
    float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return vec3{v.x / l, v.y / l, v.z / l};
}
float max_(float a, float b) { return std::max(a, b); }
float min_(float a, float b) { return std::min(a, b); }
float abs_(float a) { return std::fabs(a); }
float acos_(float a) { return std::acos(a); }
float clamp_(float v, float lo, float hi) { return std::min(std::max(v, lo), hi); }

struct ShaderInputs {
    Mat4 uModel = Mat4::identity();
    vec3 uSunDir, uSunColor, uAmbientColor;
    vec3 uDynCube[6];
    vec3 aNormal;
};

// The GLSL, transcribed. Original lines (kMeshVertexSrc) in the comments, each followed by
// its C++. `unclamped` gets the sum before the final clamp.
vec3 shaderVertexLight(const ShaderInputs& in, vec3* unclamped) {
    const float kHalfPi = 1.57079632679f;
    const Mat4& uModel = in.uModel;
    auto column = [&](int i) { return vec3{uModel.at(i, 0), uModel.at(i, 1), uModel.at(i, 2)}; };
    // vec3 L = normalize(uSunDir);
    vec3 L = normalize_(in.uSunDir);
    // vec3 cPos[3];
    // vec3 cNeg[3];
    vec3 cPos[3];
    vec3 cNeg[3];
    // for (int i = 0; i < 3; i++) {
    for (int i = 0; i < 3; i++) {
        // vec3 a = normalize(uModel[i].xyz);
        vec3 a = normalize_(column(i));
        // float d = dot(a, L);
        float d = dot(a, L);
        // cPos[i] = uAmbientColor + max(d, 0.0) * uSunColor + uDynCube[i];
        cPos[i] = in.uAmbientColor + in.uSunColor * max_(d, 0.0f) + in.uDynCube[i];
        // cNeg[i] = uAmbientColor + max(-d, 0.0) * uSunColor + uDynCube[i + 3];
        cNeg[i] = in.uAmbientColor + in.uSunColor * max_(-d, 0.0f) + in.uDynCube[i + 3];
    }
    // vec3 n = normalize(aNormal + vec3(0.0, 0.0, 1e-9));
    vec3 n = normalize_(in.aNormal + vec3{0.0f, 0.0f, 1e-9f});
    // float a = acos(min(abs(n.x), 1.0)) / kHalfPi;
    float a = acos_(min_(abs_(n.x), 1.0f)) / kHalfPi;
    // float b = acos(min(abs(n.z), 1.0)) / kHalfPi;
    float b = acos_(min_(abs_(n.z), 1.0f)) / kHalfPi;
    // float wx = b - a * b;
    float wx = b - a * b;
    // float wy = a * b;
    float wy = a * b;
    // float wz = 1.0 - b;
    float wz = 1.0f - b;
    // vec3 cx = n.x >= 0.0 ? cPos[0] : cNeg[0];
    vec3 cx = n.x >= 0.0f ? cPos[0] : cNeg[0];
    // vec3 cy = n.y >= 0.0 ? cPos[1] : cNeg[1];
    vec3 cy = n.y >= 0.0f ? cPos[1] : cNeg[1];
    // vec3 cz = n.z >= 0.0 ? cPos[2] : cNeg[2];
    vec3 cz = n.z >= 0.0f ? cPos[2] : cNeg[2];
    // vLight = clamp(wx * cx + wy * cy + wz * cz, 0.0, 1.0);
    vec3 sum = cx * wx + cy * wy + cz * wz;
    if (unclamped) *unclamped = sum;
    return vec3{clamp_(sum.x, 0.0f, 1.0f), clamp_(sum.y, 0.0f, 1.0f), clamp_(sum.z, 0.0f, 1.0f)};
}

float maxDiff(const Vec3& a, const Vec3& b) {
    return std::max({std::fabs(a.x - b.x), std::fabs(a.y - b.y), std::fabs(a.z - b.z)});
}

} // namespace

TEST_CASE("ambient cube: CPU weights and colours equal the vertex shader on random inputs") {
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> sym(-1.0f, 1.0f);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_real_distribution<float> angle(-180.0f, 180.0f);
    std::uniform_real_distribution<float> scl(0.2f, 3.0f);
    auto randVec = [&](std::uniform_real_distribution<float>& d) { return Vec3{d(rng), d(rng), d(rng)}; };

    float worst = 0.0f;
    int checked = 0;
    for (int it = 0; it < 10000; it++) {
        ShaderInputs in;
        // A rotated, non-uniformly scaled, translated model matrix.
        in.uModel = translation(randVec(sym) * 100.0f) * rotationZ(angle(rng)) * rotationX(angle(rng)) *
                    rotationY(angle(rng)) * scale(Vec3{scl(rng), scl(rng), scl(rng)});
        do {
            in.uSunDir = randVec(sym);
        } while (length(in.uSunDir) < 0.05f);
        in.uSunColor = randVec(unit);
        in.uAmbientColor = randVec(unit) * 0.5f;
        bool dyn = it % 2 == 0;
        for (Vec3& c : in.uDynCube) c = dyn ? randVec(unit) * 1.5f : Vec3{0, 0, 0};
        // Normals: random directions and lengths, plus axis-aligned and zero cases.
        switch (it % 10) {
            case 0: in.aNormal = Vec3{0, 0, 0}; break;
            case 1: in.aNormal = Vec3{(it & 16) ? -1.0f : 1.0f, 0, 0}; break;
            case 2: in.aNormal = Vec3{0, (it & 16) ? -1.0f : 1.0f, 0}; break;
            case 3: in.aNormal = Vec3{0, 0, (it & 16) ? -1.0f : 1.0f}; break;
            default: in.aNormal = randVec(sym) * scl(rng); break;
        }

        Vec3 expectedUnclamped;
        Vec3 expected = shaderVertexLight(in, &expectedUnclamped);

        Vec3 axes[3];
        modelAxes(in.uModel, axes);
        SceneLighting lighting;
        lighting.sunDirection = in.uSunDir;
        lighting.sunColor = in.uSunColor;
        lighting.ambientColor = in.uAmbientColor;
        Vec3 colors[6];
        ambientCubeColors(lighting, axes, in.uDynCube, colors);
        float w[6];
        ambientCubeWeights(in.aNormal, w);
        Vec3 got = ambientCubeLight(w, colors);

        float d = maxDiff(got, expectedUnclamped);
        worst = std::max(worst, d);
        CHECK(d <= 1e-5f);
        Vec3 clamped{std::clamp(got.x, 0.0f, 1.0f), std::clamp(got.y, 0.0f, 1.0f), std::clamp(got.z, 0.0f, 1.0f)};
        CHECK(maxDiff(clamped, expected) <= 1e-5f);

        // The weights: non-negative, one side per axis, summing to 1.
        float sum = 0.0f;
        for (int i = 0; i < 6; i++) {
            CHECK(w[i] >= -1e-6f);
            sum += w[i];
        }
        for (int i = 0; i < 3; i++) CHECK((w[i] == 0.0f || w[i + 3] == 0.0f));
        CHECK(sum == doctest::Approx(1.0f).epsilon(1e-5));

        // Quantised weights within 1/255 of the floats.
        AmbientCubeWeightsU8 q = quantizeAmbientCubeWeights(w);
        CHECK(q.pos[3] == 0);
        CHECK(q.neg[3] == 0);
        float back[6];
        dequantizeAmbientCubeWeights(q, back);
        for (int i = 0; i < 6; i++) CHECK(std::fabs(back[i] - w[i]) <= 1.0f / 255.0f);
        ++checked;
    }
    CHECK(checked == 10000);
    MESSAGE("worst |CPU - shader| over 10k inputs: " << worst);
}

TEST_CASE("ambient cube: colours with the dynamic lights of accumulateModelLights") {
    // The cube that MeshRenderer hands the shader as uDynCube, built the same way.
    DynamicLight light = DynamicLight::point(Vec3{3, 0, 1}, Vec3{1.0f, 0.5f, 0.25f}, 10.0f);
    Mat4 model = translation(Vec3{0, 0, 0}) * rotationZ(30.0f);
    Vec3 axes[3];
    modelAxes(model, axes);
    Vec3 cube[6];
    accumulateModelLights(&light, 1, Vec3{0, 0, 0}, axes, cube);
    SceneLighting lighting;
    Vec3 colors[6];
    ambientCubeColors(lighting, axes, cube, colors);

    ShaderInputs in;
    in.uModel = model;
    in.uSunDir = lighting.sunDirection;
    in.uSunColor = lighting.sunColor;
    in.uAmbientColor = lighting.ambientColor;
    for (int i = 0; i < 6; i++) in.uDynCube[i] = cube[i];
    for (const Vec3& n : {Vec3{1, 0, 0}, Vec3{0.3f, -0.8f, 0.2f}, Vec3{-0.5f, 0.5f, -0.7f}}) {
        in.aNormal = n;
        Vec3 expected;
        shaderVertexLight(in, &expected);
        float w[6];
        ambientCubeWeights(n, w);
        CHECK(maxDiff(ambientCubeLight(w, colors), expected) <= 1e-5f);
    }
    // The light is on the +X side of the rotated model: some slot got it.
    float total = 0.0f;
    for (const Vec3& c : cube) total += c.x;
    CHECK(total > 0.0f);
}

TEST_CASE("ambient cube: baked mesh weights follow buildRenderMesh's vertices") {
    ModelData model;
    model.positions = {Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};
    model.uvs = {Vec2{0, 0}, Vec2{1, 0}, Vec2{0, 1}};
    Face f1;
    f1.v[0] = 0, f1.v[1] = 1, f1.v[2] = 2;
    f1.uv[0] = 0, f1.uv[1] = 1, f1.uv[2] = 2;
    Face f2;
    f2.v[0] = 0, f2.v[1] = 3, f2.v[2] = 1;
    f2.uv[0] = 0, f2.uv[1] = 1, f2.uv[2] = 2;
    model.faces = {f1, f2};
    model.normalsPerFace = true;
    model.normals = {Vec3{0, 0, 1}, Vec3{0, -1, 0}};

    std::vector<RenderVertex> verts, plainVerts;
    std::vector<u16> indices, plainIndices;
    std::vector<AmbientCubeWeightsU8> weights;
    REQUIRE(buildRenderMeshBaked(model, verts, indices, weights));
    REQUIRE(buildRenderMesh(model, plainVerts, plainIndices));
    CHECK(verts.size() == plainVerts.size());
    CHECK(indices == plainIndices);
    REQUIRE(weights.size() == verts.size());
    for (size_t i = 0; i < verts.size(); i++) {
        float w[6];
        ambientCubeWeights(verts[i].normal, w);
        AmbientCubeWeightsU8 q = quantizeAmbientCubeWeights(w);
        for (int k = 0; k < 4; k++) {
            CHECK(weights[i].pos[k] == q.pos[k]);
            CHECK(weights[i].neg[k] == q.neg[k]);
        }
    }
    // An up-facing vertex puts everything on +Z, a -Y facing one on -Y.
    bool sawUp = false, sawSouth = false;
    for (size_t i = 0; i < verts.size(); i++) {
        if (verts[i].normal.z > 0.99f) {
            sawUp = true;
            CHECK(weights[i].pos[2] == 255);
        }
        if (verts[i].normal.y < -0.99f) {
            sawSouth = true;
            CHECK(weights[i].neg[1] == 255);
        }
    }
    CHECK(sawUp);
    CHECK(sawSouth);
}
