// citro3d renderer of the 3DS spike. See render.h.
#include "render.h"

#include <3ds.h>
#include <citro3d.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "as3d/defs.h"
#include "as3d/image.h"
#include "as3d/math.h"
#include "as3d/model.h"
#include "as3d/terrain.h"
#include "as3d/vfs.h"
#include "as3d/world.h"

#include "scene_shbin.h"

using namespace as3d;

namespace spike {

namespace {

#define DISPLAY_TRANSFER_FLAGS                                                                          \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |                  \
     GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |     \
     GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

// One vertex for both the terrain and the model: 24 bytes.
struct Vertex {
    float pos[3];
    u8 col[4];
    float uv[2];
};

struct Mesh {
    Vertex* verts = nullptr; // linear memory
    u16* indices = nullptr;  // linear memory
    int vertCount = 0;
    int indexCount = 0;
    float minY = 0, maxY = 0; // world y range (terrain chunks)
};

C3D_RenderTarget* g_target = nullptr;
DVLB_s* g_dvlb = nullptr;
shaderProgram_s g_program;
int g_uMvp = -1;
C3D_AttrInfo g_attr;

std::vector<Mesh> g_chunks;
Mesh g_heli;
C3D_Tex g_terrainTex, g_heliTex;
bool g_terrainTexOk = false, g_heliTexOk = false;
u32 g_clearColor = 0x406080FF;

constexpr int kChunkRows = 16;

void freeMesh(Mesh& m) {
    if (m.verts) linearFree(m.verts);
    if (m.indices) linearFree(m.indices);
    m = Mesh();
}

bool allocMesh(Mesh& m, int verts, int indices) {
    m.verts = static_cast<Vertex*>(linearAlloc(sizeof(Vertex) * static_cast<size_t>(verts)));
    m.indices = static_cast<u16*>(linearAlloc(sizeof(u16) * static_cast<size_t>(indices)));
    m.vertCount = verts;
    m.indexCount = indices;
    return m.verts && m.indices;
}

void flushMesh(const Mesh& m) {
    GSPGPU_FlushDataCache(m.verts, sizeof(Vertex) * static_cast<u32>(m.vertCount));
    GSPGPU_FlushDataCache(m.indices, sizeof(u16) * static_cast<u32>(m.indexCount));
}

int nextPow2(int v) {
    int p = 8;
    while (p < v) p <<= 1;
    return p;
}

// Morton index of (x, y) inside an 8x8 tile.
inline u32 morton8(u32 x, u32 y) {
    return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
}

// Converts an engine image (RGBA8, top row first, any size) to a PICA texture: power-of-two
// sides (nearest resampling, at most 256), 8x8 tiles in Morton order, ABGR byte order.
bool uploadTexture(const Image& img, C3D_Tex* tex, std::string* report, const char* what) {
    int w = std::min(nextPow2(img.width), 256), h = std::min(nextPow2(img.height), 256);
    if (!C3D_TexInit(tex, static_cast<u16>(w), static_cast<u16>(h), GPU_RGBA8)) return false;
    u32* dst = static_cast<u32*>(tex->data);
    for (int y = 0; y < h; ++y) {
        int sy = y * img.height / h;
        for (int x = 0; x < w; ++x) {
            int sx = x * img.width / w;
            const u8* p = &img.rgba[(static_cast<size_t>(sy) * img.width + sx) * 4];
            u32 off = ((static_cast<u32>(y) >> 3) * (static_cast<u32>(w) >> 3) + (static_cast<u32>(x) >> 3)) * 64 +
                      morton8(static_cast<u32>(x) & 7, static_cast<u32>(y) & 7);
            dst[off] = (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16) | (static_cast<u32>(p[2]) << 8) |
                       static_cast<u32>(p[3]);
        }
    }
    C3D_TexFlush(tex);
    C3D_TexSetFilter(tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(tex, GPU_REPEAT, GPU_REPEAT);
    char line[160];
    std::snprintf(line, sizeof line, "%s texture %dx%d -> %dx%d RGBA8 (%d KB)\n", what, img.width, img.height, w, h,
                  w * h * 4 / 1024);
    *report += line;
    return true;
}

bool loadImage(Vfs& vfs, const std::string& path, Image& img) {
    Blob blob;
    return vfs.read(path, blob) && decodeTga(blob.data(), blob.size(), img);
}

// as3d Mat4 (column-major, m[col*4+row]) to a citro3d matrix (rows).
void toC3D(const Mat4& m, C3D_Mtx* out) {
    for (int r = 0; r < 4; ++r) {
        out->r[r].x = m.at(0, r);
        out->r[r].y = m.at(1, r);
        out->r[r].z = m.at(2, r);
        out->r[r].w = m.at(3, r);
    }
}

Mat4 entityMatrix(const Entity& e) {
    Vec3 a0 = e.v3(F_AXIS), a1 = e.v3(F_AXIS + 3), a2 = e.v3(F_AXIS + 6), o = e.v3(F_BASE_ORIGIN);
    Mat4 m;
    m.at(0, 0) = a0.x; m.at(0, 1) = a0.y; m.at(0, 2) = a0.z; m.at(0, 3) = 0.0f;
    m.at(1, 0) = a1.x; m.at(1, 1) = a1.y; m.at(1, 2) = a1.z; m.at(1, 3) = 0.0f;
    m.at(2, 0) = a2.x; m.at(2, 1) = a2.y; m.at(2, 2) = a2.z; m.at(2, 3) = 0.0f;
    m.at(3, 0) = o.x;  m.at(3, 1) = o.y;  m.at(3, 2) = o.z;  m.at(3, 3) = 1.0f;
    float s = e.f(F_SCALE);
    if (s > 0.001f) m = m * scale(Vec3{s, s, s});
    return m;
}

void bindMesh(const Mesh& m) {
    C3D_BufInfo* buf = C3D_GetBufInfo();
    BufInfo_Init(buf);
    BufInfo_Add(buf, m.verts, sizeof(Vertex), 3, 0x210);
}

} // namespace

bool renderInit(std::string* error) {
    g_target = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    if (!g_target) {
        *error = "cannot create the top render target";
        return false;
    }
    C3D_RenderTargetSetOutput(g_target, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

    g_dvlb = DVLB_ParseFile(const_cast<u32*>(reinterpret_cast<const u32*>(scene_shbin)), scene_shbin_size);
    shaderProgramInit(&g_program);
    shaderProgramSetVsh(&g_program, &g_dvlb->DVLE[0]);
    C3D_BindProgram(&g_program);
    g_uMvp = shaderInstanceGetUniformLocation(g_program.vertexShader, "mvp");

    AttrInfo_Init(&g_attr);
    AttrInfo_AddLoader(&g_attr, 0, GPU_FLOAT, 3);          // v0 position
    AttrInfo_AddLoader(&g_attr, 1, GPU_UNSIGNED_BYTE, 4);  // v1 colour 0..255
    AttrInfo_AddLoader(&g_attr, 2, GPU_FLOAT, 2);          // v2 uv
    C3D_SetAttrInfo(&g_attr);

    C3D_TexEnv* env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
    C3D_CullFace(GPU_CULL_NONE);
    return true;
}

bool renderLoadScene(Vfs& vfs, const World& world, std::string* report) {
    char line[200];
    const Terrain* t = world.terrain();
    if (!t) {
        *report += "no terrain\n";
        return false;
    }
    // Terrain: chunks of kChunkRows cell rows, each with its own vertices so that 16-bit
    // indices suffice; colour = static lighting, uv = one texture repeated every 4 cells.
    const int W = t->width(), H = t->height(), VW = t->vertsWide();
    const std::vector<Vec3>& pos = t->positions();
    const std::vector<TerrainVertexColor>& col = t->colors();
    int totalVerts = 0;
    for (int r0 = 0; r0 < H; r0 += kChunkRows) {
        int rows = std::min(kChunkRows, H - r0);
        Mesh m;
        if (!allocMesh(m, VW * (rows + 1), W * rows * 6)) {
            *report += "out of linear memory for the terrain\n";
            return false;
        }
        m.minY = 1e30f;
        m.maxY = -1e30f;
        for (int r = 0; r <= rows; ++r) {
            for (int c = 0; c < VW; ++c) {
                size_t si = static_cast<size_t>(r0 + r) * VW + c;
                Vertex& v = m.verts[r * VW + c];
                v.pos[0] = pos[si].x;
                v.pos[1] = pos[si].y;
                v.pos[2] = pos[si].z;
                v.col[0] = col[si].r;
                v.col[1] = col[si].g;
                v.col[2] = col[si].b;
                v.col[3] = 255;
                v.uv[0] = static_cast<float>(c) * 0.25f;
                v.uv[1] = static_cast<float>(r0 + r) * 0.25f;
                m.minY = std::min(m.minY, pos[si].y);
                m.maxY = std::max(m.maxY, pos[si].y);
            }
        }
        u16* ix = m.indices;
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < W; ++c) {
                u16 a = static_cast<u16>(r * VW + c), b = static_cast<u16>(a + 1);
                u16 d = static_cast<u16>(a + VW), e = static_cast<u16>(d + 1);
                *ix++ = a; *ix++ = b; *ix++ = e;
                *ix++ = a; *ix++ = e; *ix++ = d;
            }
        }
        flushMesh(m);
        totalVerts += m.vertCount;
        g_chunks.push_back(m);
    }
    std::snprintf(line, sizeof line, "terrain %dx%d cells, %d chunks, %d verts, %d KB\n", W, H,
                  static_cast<int>(g_chunks.size()), totalVerts,
                  static_cast<int>((totalVerts * sizeof(Vertex) + W * H * 6 * sizeof(u16)) / 1024));
    *report += line;
    const TerrainStyle& st = t->style();
    if (st.hasFog) {
        auto b = [](float f) { return static_cast<u32>(std::max(0.0f, std::min(1.0f, f)) * 255.0f); };
        g_clearColor = (b(st.fogColor[0]) << 24) | (b(st.fogColor[1]) << 16) | (b(st.fogColor[2]) << 8) | 0xFF;
    }
    Image img;
    std::string tpath = st.texturesDir + "\\texture1.tga";
    if (loadImage(vfs, tpath, img)) g_terrainTexOk = uploadTexture(img, &g_terrainTex, report, "terrain");
    else *report += "cannot load " + tpath + "\n";

    {
        // Projection check: where view-space (1,0,-10) and (0,1,-10) land in clip space.
        C3D_Mtx proj;
        Mtx_PerspTilt(&proj, C3D_AngleFromDegrees(60.0f), C3D_AspectRatioTop, 4.0f, 2000.0f, false);
        C3D_FVec a = Mtx_MultiplyFVec4(&proj, FVec4_New(1, 0, -10, 1));
        C3D_FVec b = Mtx_MultiplyFVec4(&proj, FVec4_New(0, 1, -10, 1));
        std::snprintf(line, sizeof line, "proj x1 -> (%.3f %.3f) y1 -> (%.3f %.3f)\n", a.x / a.w, a.y / a.w, b.x / b.w,
                      b.y / b.w);
        *report += line;
    }
    // The player's helicopter: its root mesh and skin.
    int pi = world.playerEntityIndex(0);
    if (pi < 0 || !world.entity(pi).model) {
        *report += "no player model\n";
        return true;
    }
    const Entity& e = world.entity(pi);
    std::vector<RenderVertex> rv;
    std::vector<u16> ri;
    if (!buildRenderMesh(*e.model, rv, ri) || !allocMesh(g_heli, static_cast<int>(rv.size()), static_cast<int>(ri.size()))) {
        *report += "cannot build the helicopter mesh\n";
        return true;
    }
    for (size_t i = 0; i < rv.size(); ++i) {
        Vertex& v = g_heli.verts[i];
        v.pos[0] = rv[i].position.x;
        v.pos[1] = rv[i].position.y;
        v.pos[2] = rv[i].position.z;
        v.col[0] = v.col[1] = v.col[2] = v.col[3] = 255;
        v.uv[0] = rv[i].uv.x;
        v.uv[1] = 1.0f - rv[i].uv.y; // engine uvs are top-down; PICA t=1 is the first row
    }
    std::memcpy(g_heli.indices, ri.data(), ri.size() * sizeof(u16));
    flushMesh(g_heli);
    std::string skin = !e.skinPath.empty() ? e.skinPath : (e.def && !e.def->skin.empty() ? e.def->skin : e.model->sourceTexturePath);
    std::snprintf(line, sizeof line, "heli %s: %d verts, %d tris\n", e.modelPath.c_str(), g_heli.vertCount,
                  g_heli.indexCount / 3);
    *report += line;
    Image himg;
    if (loadImage(vfs, skin, himg)) g_heliTexOk = uploadTexture(himg, &g_heliTex, report, "heli");
    else *report += "cannot load skin " + skin + "\n";
    return true;
}

DrawStats renderFrame(const World& world) {
    DrawStats ds;
    C3D_RenderTargetClear(g_target, C3D_CLEAR_ALL, g_clearColor, 0);
    C3D_FrameDrawOn(g_target);

    // The engine's camera (world_render.cpp worldViewOf), with the PICA's tilted projection.
    const CameraState& c = world.camera();
    Vec3 eye{c.field[15], c.field[16], c.field[17]};
    float fovY = c.field[14] > 1.0f ? c.field[14] : 60.0f;
    float farPlane = 2000.0f;
    if (const Terrain* t = world.terrain()) {
        if (t->style().hasFog) farPlane = std::max(t->style().fogFar, 1000.0f);
    }
    Mat4 view = rotationX(c.field[18]) * rotationY(c.field[19]) * rotationZ(c.field[20]) * translation(-eye);
    C3D_Mtx proj, viewM, mvp;
    Mtx_PerspTilt(&proj, C3D_AngleFromDegrees(fovY), C3D_AspectRatioTop, 4.0f, farPlane, false);
    toC3D(view, &viewM);
    Mtx_Multiply(&mvp, &proj, &viewM);

    // Terrain: the chunks within reach of the camera (y from just behind the eye to far).
    if (g_terrainTexOk) C3D_TexBind(0, &g_terrainTex);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g_uMvp, &mvp);
    for (const Mesh& m : g_chunks) {
        if (m.maxY < eye.y - 200.0f || m.minY > eye.y + farPlane) continue;
        bindMesh(m);
        C3D_DrawElements(GPU_TRIANGLES, m.indexCount, C3D_UNSIGNED_SHORT, m.indices);
        ds.terrainChunks++;
        ds.vertices += m.indexCount;
    }

    // The helicopter.
    int pi = world.playerEntityIndex(0);
    if (g_heli.verts && pi >= 0) {
        C3D_Mtx model, mv;
        toC3D(entityMatrix(world.entity(pi)), &model);
        Mtx_Multiply(&mv, &mvp, &model);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g_uMvp, &mv);
        if (g_heliTexOk) C3D_TexBind(0, &g_heliTex);
        bindMesh(g_heli);
        C3D_DrawElements(GPU_TRIANGLES, g_heli.indexCount, C3D_UNSIGNED_SHORT, g_heli.indices);
        ds.vertices += g_heli.indexCount;
    }
    return ds;
}

void renderShutdown() {
    for (Mesh& m : g_chunks) freeMesh(m);
    g_chunks.clear();
    freeMesh(g_heli);
    if (g_terrainTexOk) C3D_TexDelete(&g_terrainTex);
    if (g_heliTexOk) C3D_TexDelete(&g_heliTex);
    shaderProgramFree(&g_program);
    if (g_dvlb) DVLB_Free(g_dvlb);
    if (g_target) C3D_RenderTargetDelete(g_target);
}

} // namespace spike
