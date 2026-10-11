// ShaderSet: the PICA backend's vertex shaders (shaders/*.v.pica), their vertex layouts
// and uniform locations. 3DS only.
//
// The .v.pica sources are assembled by picasso in the 3DS build (engine/src/render/module.cmake,
// ctr_add_shader_library) into one binary, one entry point (DVLE) per program in the Shader
// enum's order, embedded into as3d_render as pica_all_shbin (dkp_add_embedded_binary_library).
//
// PICA float uniforms are global registers, not per program: after bind() set every uniform
// of the newly bound shader again (setMvp etc.) before drawing.
#pragma once

#include <3ds.h>
#include <citro3d.h>

#include <cstdint>

#include "as3d/math.h"

namespace as3d::pica {

enum class Shader { Flat = 0, Mesh = 1, Terrain = 2, Flat2 = 3, Env = 4, Water = 5 };
constexpr int kShaderCount = 6;

// flat.v.pica: particles, sprites, decals, 2D quads, fills.
struct FlatVertex {
    float pos[3];
    float uv[2];
    uint8_t rgba[4];
};
static_assert(sizeof(FlatVertex) == 24, "FlatVertex layout");

// flat2.v.pica: FlatVertex plus a second texture coordinate (the 2D layer's two-texture runs).
struct Flat2Vertex {
    float pos[3];
    float uv[2];
    uint8_t rgba[4];
    float uv2[2];
};
static_assert(sizeof(Flat2Vertex) == 32, "Flat2Vertex layout");

// mesh.v.pica: models with baked ambient-cube weights (as3d/ambient_cube.h AmbientCubeWeightsU8).
struct MeshVertex {
    float pos[3];
    uint8_t wPos[4]; // +X, +Y, +Z, 0
    uint8_t wNeg[4]; // -X, -Y, -Z, 0
    float uv[2];
};
static_assert(sizeof(MeshVertex) == 28, "MeshVertex layout");

// env.v.pica: MeshVertex (stream 0) plus this stream (stream 1): the folded model normal
// |n| * 255 of each vertex, kept by GpuMesh::upload for smooth-normal meshes (the only ones the
// GL environment-maps).
struct EnvNormal {
    uint8_t n[4]; // |n.x|, |n.y|, |n.z|, 0
};
static_assert(sizeof(EnvNormal) == 4, "EnvNormal layout");

// terrain.v.pica, static stream; the dynamic-light stream is one u8 x4 per vertex.
struct TerrainVertexPica {
    float pos[3];
    uint8_t col[4];
    float uv0[2];
};
static_assert(sizeof(TerrainVertexPica) == 24, "TerrainVertexPica layout");

// water.v.pica, static stream (the sequels' wave grid, water_renderer_pica.h); the dynamic
// stream is one float per vertex, the height of this frame.
struct WaterGridStaticPica {
    float xy[2];
    uint8_t rgba[4]; // 255, 255, 255, weight * opacity
};
static_assert(sizeof(WaterGridStaticPica) == 12, "WaterGridStaticPica layout");

// as3d Mat4 (column-major, at(col, row)) to a citro3d matrix (rows) and back.
void toC3D(const Mat4& m, C3D_Mtx* out);
Mat4 fromC3D(const C3D_Mtx& m);

class ShaderSet {
public:
    ShaderSet() = default;
    ~ShaderSet() { unload(); }
    ShaderSet(const ShaderSet&) = delete;
    ShaderSet& operator=(const ShaderSet&) = delete;

    // Parses the embedded shbins, caches uniform locations and attribute layouts. Needs C3D_Init.
    bool load();
    void unload();
    bool loaded() const { return loaded_; }

    // C3D_BindProgram + C3D_SetAttrInfo of `s`.
    void bind(Shader s);
    Shader bound() const { return bound_; }

    // Uniforms of the bound shader (ignored when it has no such uniform).
    void setMvp(const Mat4& m);
    // The eye shift uniform `eye` (every program declares it first, so it is the same register in
    // all of them; eye_replay_pica.h): clip y += x * w + y in the world shaders. (0, 0) but while
    // the right eye replays the left eye's commands. eyeRegister() is -1 when the programs disagree.
    void setEye(float x, float y);
    int eyeRegister() const { return eyeRegister_; }
    void setMvp(const C3D_Mtx& m);
    void setMeshCube(const Vec3 colors[6]);  // mesh: slots +X, +Y, +Z, -X, -Y, -Z
    void setMeshTint(const Vec4& rgba);      // mesh
    void setTerrainDetail(float sx, float sy, float ox, float oy); // terrain: uv1 = pos.xy * s + o
    // env: `modelView` rows 0..2 (the eye position), `normalRows` (nmat, 3 rows of x y z),
    // `select` (envsel: (0.5, 0) view normal, (0, 1) sphere map) and the ENV_QUAD rotation
    // `rot` (2 rows of x y; identity for the other modes). See shaders/env.v.pica.
    void setEnvUniforms(const Mat4& modelView, const float normalRows[3][3], float selView, float selSphere,
                        const float rot[2][2]);

    // water: worig, wbase, wshine (see shaders/water.v.pica).
    void setWaterUniforms(float ox, float oy, const float base[4], const float shine[4]);

    // Vertex buffers (linear memory) of the bound shader: one interleaved stream for Flat, Flat2 and
    // Mesh; the static stream plus the dynamic-light stream (u8 x4 per vertex) for Terrain.
    void setVertices(const void* vertices);
    void setTerrainVertices(const TerrainVertexPica* vertices, const uint8_t (*dynamic)[4]);
    // Env: the mesh stream plus the normal stream (same vertex count).
    void setEnvVertices(const MeshVertex* vertices, const EnvNormal* normals);
    // Water: the static stream plus the per-frame heights (same vertex count).
    void setWaterVertices(const WaterGridStaticPica* vertices, const float* heights);

    struct Locations {
        int mvp = -1, cube = -1, tint = -1, detail = -1;
        int mv = -1, nmat = -1, envsel = -1, envrot = -1;
        int worig = -1, wbase = -1, wshine = -1;
        int eye = -1;
    };
    const Locations& locations(Shader s) const { return programs_[static_cast<int>(s)].loc; }

private:
    struct Program {
        shaderProgram_s program{};
        C3D_AttrInfo attr{};
        int stride = 0;
        Locations loc;
        bool ok = false;
    };
    bool loadOne(Shader s, DVLE_s* dvle);
    // The six programs are entry points (DVLEs) of one shader binary (round 3, R3.2): they share
    // its code blob, so switching programs no longer uploads the vertex shader's code (citro3d
    // uploads it whenever the new program's DVLP differs from the old one's).
    DVLB_s* dvlb_ = nullptr;
    Program programs_[kShaderCount];
    Shader bound_ = Shader::Flat;
    bool loaded_ = false;
    int eyeRegister_ = -1;
};

} // namespace as3d::pica
