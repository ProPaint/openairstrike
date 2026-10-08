// ShaderSet (shader_set.h): the embedded shbins, attribute layouts and uniform locations.
#include "shader_set.h"

#include "env_shbin.h"
#include "flat2_shbin.h"
#include "flat_shbin.h"
#include "mesh_shbin.h"
#include "terrain_shbin.h"

namespace as3d::pica {

void toC3D(const Mat4& m, C3D_Mtx* out) {
    for (int r = 0; r < 4; ++r) {
        out->r[r].x = m.at(0, r);
        out->r[r].y = m.at(1, r);
        out->r[r].z = m.at(2, r);
        out->r[r].w = m.at(3, r);
    }
}

Mat4 fromC3D(const C3D_Mtx& m) {
    Mat4 o;
    for (int r = 0; r < 4; ++r) {
        o.at(0, r) = m.r[r].x;
        o.at(1, r) = m.r[r].y;
        o.at(2, r) = m.r[r].z;
        o.at(3, r) = m.r[r].w;
    }
    return o;
}

bool ShaderSet::loadOne(Shader s, const void* shbin, uint32_t size) {
    Program& p = programs_[static_cast<int>(s)];
    p.dvlb = DVLB_ParseFile(const_cast<u32*>(static_cast<const u32*>(shbin)), size);
    if (!p.dvlb) return false;
    shaderProgramInit(&p.program);
    if (shaderProgramSetVsh(&p.program, &p.dvlb->DVLE[0]) < 0) return false;
    shaderInstance_s* vsh = p.program.vertexShader;
    p.loc.mvp = shaderInstanceGetUniformLocation(vsh, "mvp");
    p.loc.cube = shaderInstanceGetUniformLocation(vsh, "cube");
    p.loc.tint = shaderInstanceGetUniformLocation(vsh, "tint");
    p.loc.detail = shaderInstanceGetUniformLocation(vsh, "detail");
    p.loc.mv = shaderInstanceGetUniformLocation(vsh, "mv");
    p.loc.nmat = shaderInstanceGetUniformLocation(vsh, "nmat");
    p.loc.envsel = shaderInstanceGetUniformLocation(vsh, "envsel");
    p.loc.envrot = shaderInstanceGetUniformLocation(vsh, "envrot");

    AttrInfo_Init(&p.attr);
    switch (s) {
    case Shader::Flat:
        AttrInfo_AddLoader(&p.attr, 0, GPU_FLOAT, 3);         // v0 position
        AttrInfo_AddLoader(&p.attr, 1, GPU_FLOAT, 2);         // v1 texcoord
        AttrInfo_AddLoader(&p.attr, 2, GPU_UNSIGNED_BYTE, 4); // v2 colour
        p.stride = sizeof(FlatVertex);
        break;
    case Shader::Flat2:
        AttrInfo_AddLoader(&p.attr, 0, GPU_FLOAT, 3);         // v0 position
        AttrInfo_AddLoader(&p.attr, 1, GPU_FLOAT, 2);         // v1 texcoord
        AttrInfo_AddLoader(&p.attr, 2, GPU_UNSIGNED_BYTE, 4); // v2 colour
        AttrInfo_AddLoader(&p.attr, 3, GPU_FLOAT, 2);         // v3 texcoord2
        p.stride = sizeof(Flat2Vertex);
        break;
    case Shader::Mesh:
        AttrInfo_AddLoader(&p.attr, 0, GPU_FLOAT, 3);         // v0 position
        AttrInfo_AddLoader(&p.attr, 1, GPU_UNSIGNED_BYTE, 4); // v1 +X +Y +Z weights
        AttrInfo_AddLoader(&p.attr, 2, GPU_UNSIGNED_BYTE, 4); // v2 -X -Y -Z weights
        AttrInfo_AddLoader(&p.attr, 3, GPU_FLOAT, 2);         // v3 texcoord
        p.stride = sizeof(MeshVertex);
        break;
    case Shader::Env:
        AttrInfo_AddLoader(&p.attr, 0, GPU_FLOAT, 3);         // v0 position   (mesh stream)
        AttrInfo_AddLoader(&p.attr, 1, GPU_UNSIGNED_BYTE, 4); // v1 +X +Y +Z weights
        AttrInfo_AddLoader(&p.attr, 2, GPU_UNSIGNED_BYTE, 4); // v2 -X -Y -Z weights
        AttrInfo_AddLoader(&p.attr, 3, GPU_FLOAT, 2);         // v3 texcoord
        AttrInfo_AddLoader(&p.attr, 4, GPU_UNSIGNED_BYTE, 4); // v4 folded normal (normal stream)
        p.stride = sizeof(MeshVertex);
        break;
    case Shader::Terrain:
        AttrInfo_AddLoader(&p.attr, 0, GPU_FLOAT, 3);         // v0 position   (static stream)
        AttrInfo_AddLoader(&p.attr, 1, GPU_UNSIGNED_BYTE, 4); // v1 colour     (static stream)
        AttrInfo_AddLoader(&p.attr, 2, GPU_UNSIGNED_BYTE, 4); // v2 dynamic    (dynamic stream)
        AttrInfo_AddLoader(&p.attr, 3, GPU_FLOAT, 2);         // v3 uv0        (static stream)
        p.stride = sizeof(TerrainVertexPica);
        break;
    }
    p.ok = p.loc.mvp >= 0;
    return p.ok;
}

bool ShaderSet::load() {
    unload();
    bool ok = loadOne(Shader::Flat, flat_shbin, flat_shbin_size);
    ok = loadOne(Shader::Flat2, flat2_shbin, flat2_shbin_size) && ok;
    ok = loadOne(Shader::Mesh, mesh_shbin, mesh_shbin_size) && ok;
    ok = loadOne(Shader::Terrain, terrain_shbin, terrain_shbin_size) && ok;
    ok = loadOne(Shader::Env, env_shbin, env_shbin_size) && ok;
    loaded_ = ok;
    if (!ok) unload();
    return ok;
}

void ShaderSet::unload() {
    for (Program& p : programs_) {
        if (p.dvlb) {
            shaderProgramFree(&p.program);
            DVLB_Free(p.dvlb);
        }
        p = Program();
    }
    loaded_ = false;
}

void ShaderSet::bind(Shader s) {
    Program& p = programs_[static_cast<int>(s)];
    if (!p.ok) return;
    C3D_BindProgram(&p.program);
    C3D_SetAttrInfo(&p.attr);
    bound_ = s;
}

void ShaderSet::setMvp(const C3D_Mtx& m) {
    int loc = programs_[static_cast<int>(bound_)].loc.mvp;
    if (loc >= 0) C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, loc, &m);
}

void ShaderSet::setMvp(const Mat4& m) {
    C3D_Mtx c;
    toC3D(m, &c);
    setMvp(c);
}

void ShaderSet::setMeshCube(const Vec3 colors[6]) {
    int loc = programs_[static_cast<int>(bound_)].loc.cube;
    if (loc < 0) return;
    for (int i = 0; i < 6; ++i) C3D_FVUnifSet(GPU_VERTEX_SHADER, loc + i, colors[i].x, colors[i].y, colors[i].z, 1.0f);
}

void ShaderSet::setMeshTint(const Vec4& c) {
    int loc = programs_[static_cast<int>(bound_)].loc.tint;
    if (loc >= 0) C3D_FVUnifSet(GPU_VERTEX_SHADER, loc, c.x, c.y, c.z, c.w);
}

void ShaderSet::setTerrainDetail(float sx, float sy, float ox, float oy) {
    int loc = programs_[static_cast<int>(bound_)].loc.detail;
    if (loc >= 0) C3D_FVUnifSet(GPU_VERTEX_SHADER, loc, sx, sy, ox, oy);
}

void ShaderSet::setEnvUniforms(const Mat4& mv, const float n[3][3], float selView, float selSphere,
                               const float rot[2][2]) {
    const Locations& l = programs_[static_cast<int>(bound_)].loc;
    if (l.mv >= 0)
        for (int r = 0; r < 3; ++r) C3D_FVUnifSet(GPU_VERTEX_SHADER, l.mv + r, mv.at(0, r), mv.at(1, r), mv.at(2, r), mv.at(3, r));
    if (l.nmat >= 0)
        for (int r = 0; r < 3; ++r) C3D_FVUnifSet(GPU_VERTEX_SHADER, l.nmat + r, n[r][0], n[r][1], n[r][2], 0.0f);
    if (l.envsel >= 0) C3D_FVUnifSet(GPU_VERTEX_SHADER, l.envsel, selView, selSphere, 0.0f, 0.0f);
    if (l.envrot >= 0)
        for (int r = 0; r < 2; ++r) C3D_FVUnifSet(GPU_VERTEX_SHADER, l.envrot + r, rot[r][0], rot[r][1], 0.0f, 0.0f);
}

void ShaderSet::setEnvVertices(const MeshVertex* vertices, const EnvNormal* normals) {
    C3D_BufInfo* buf = C3D_GetBufInfo();
    BufInfo_Init(buf);
    BufInfo_Add(buf, vertices, sizeof(MeshVertex), 4, 0x3210);
    BufInfo_Add(buf, normals, sizeof(EnvNormal), 1, 0x4);
}

void ShaderSet::setVertices(const void* vertices) {
    const Program& p = programs_[static_cast<int>(bound_)];
    C3D_BufInfo* buf = C3D_GetBufInfo();
    BufInfo_Init(buf);
    if (bound_ == Shader::Mesh) BufInfo_Add(buf, vertices, p.stride, 4, 0x3210);
    else if (bound_ == Shader::Flat2) BufInfo_Add(buf, vertices, p.stride, 4, 0x3210);
    else BufInfo_Add(buf, vertices, p.stride, 3, 0x210);
}

void ShaderSet::setTerrainVertices(const TerrainVertexPica* vertices, const uint8_t (*dynamic)[4]) {
    C3D_BufInfo* buf = C3D_GetBufInfo();
    BufInfo_Init(buf);
    // Static stream: position (v0), colour (v1), uv0 (v3); dynamic stream: v2.
    BufInfo_Add(buf, vertices, sizeof(TerrainVertexPica), 3, 0x310);
    BufInfo_Add(buf, dynamic, 4, 1, 0x2);
}

} // namespace as3d::pica
