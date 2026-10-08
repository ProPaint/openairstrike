// ShaderSet: the PICA backend's three vertex shaders (shaders/*.v.pica), their vertex layouts
// and uniform locations. 3DS only.
//
// The .v.pica sources are assembled by picasso in the 3DS build (engine/src/render/module.cmake,
// ctr_add_shader_library) and embedded into as3d_render as flat_shbin, mesh_shbin and
// terrain_shbin (dkp_add_embedded_binary_library).
//
// PICA float uniforms are global registers, not per program: after bind() set every uniform
// of the newly bound shader again (setMvp etc.) before drawing.
#pragma once

#include <3ds.h>
#include <citro3d.h>

#include <cstdint>

#include "as3d/math.h"

namespace as3d::pica {

enum class Shader { Flat = 0, Mesh = 1, Terrain = 2, Flat2 = 3 };
constexpr int kShaderCount = 4;

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

// terrain.v.pica, static stream; the dynamic-light stream is one u8 x4 per vertex.
struct TerrainVertexPica {
    float pos[3];
    uint8_t col[4];
    float uv0[2];
};
static_assert(sizeof(TerrainVertexPica) == 24, "TerrainVertexPica layout");

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
    void setMvp(const C3D_Mtx& m);
    void setMeshCube(const Vec3 colors[6]);  // mesh: slots +X, +Y, +Z, -X, -Y, -Z
    void setMeshTint(const Vec4& rgba);      // mesh
    void setTerrainDetail(float sx, float sy, float ox, float oy); // terrain: uv1 = pos.xy * s + o

    // Vertex buffers (linear memory) of the bound shader: one interleaved stream for Flat, Flat2 and
    // Mesh; the static stream plus the dynamic-light stream (u8 x4 per vertex) for Terrain.
    void setVertices(const void* vertices);
    void setTerrainVertices(const TerrainVertexPica* vertices, const uint8_t (*dynamic)[4]);

    struct Locations {
        int mvp = -1, cube = -1, tint = -1, detail = -1;
    };
    const Locations& locations(Shader s) const { return programs_[static_cast<int>(s)].loc; }

private:
    struct Program {
        DVLB_s* dvlb = nullptr;
        shaderProgram_s program{};
        C3D_AttrInfo attr{};
        int stride = 0;
        Locations loc;
        bool ok = false;
    };
    bool loadOne(Shader s, const void* shbin, uint32_t size);
    Program programs_[kShaderCount];
    Shader bound_ = Shader::Flat;
    bool loaded_ = false;
};

} // namespace as3d::pica
