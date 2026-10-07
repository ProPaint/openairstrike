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
C3D_RenderTarget* g_targetRight = nullptr;
C3D_RenderTarget* g_targetBottom = nullptr;
FrameOptions g_opts;
TexMode g_texMode = TexMode::LinearRGBA8;
Image g_terrainImg, g_heliImg;
Mesh g_quad; // full-screen quad in clip space
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

// Writes one mip level (w x h RGBA8 bytes, top row first) swizzled into `dst`: 8x8 tiles in
// Morton order. GPU_RGBA8 texels are ABGR-ordered words, GPU_RGB565 texels 16-bit.
void swizzleLevel(const u8* rgba, int w, int h, void* dst, bool is565) {
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const u8* p = &rgba[(static_cast<size_t>(y) * w + x) * 4];
            u32 off = ((static_cast<u32>(y) >> 3) * (static_cast<u32>(w) >> 3) + (static_cast<u32>(x) >> 3)) * 64 +
                      morton8(static_cast<u32>(x) & 7, static_cast<u32>(y) & 7);
            if (is565) {
                static_cast<u16*>(dst)[off] =
                    static_cast<u16>(((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3));
            } else {
                static_cast<u32*>(dst)[off] = (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16) |
                                              (static_cast<u32>(p[2]) << 8) | static_cast<u32>(p[3]);
            }
        }
    }
}

const char* texModeName(TexMode m) {
    switch (m) {
    case TexMode::LinearRGBA8: return "RGBA8 linear";
    case TexMode::VramRGBA8: return "RGBA8 VRAM";
    case TexMode::VramRGB565: return "RGB565 VRAM";
    case TexMode::VramRGB565Mip: return "RGB565 VRAM+mips";
    }
    return "?";
}

// Converts an engine image (RGBA8, top row first, any size) to a PICA texture: power-of-two
// sides (nearest resampling, at most 256), 8x8 tiles in Morton order, in the layout of `mode`
// (memory, texel format, optional CPU-generated mip chain with trilinear filtering).
bool uploadTexture(const Image& img, C3D_Tex* tex, TexMode mode, std::string* report, const char* what) {
    int w = std::min(nextPow2(img.width), 256), h = std::min(nextPow2(img.height), 256);
    bool vram = mode != TexMode::LinearRGBA8;
    bool is565 = mode == TexMode::VramRGB565 || mode == TexMode::VramRGB565Mip;
    bool mip = mode == TexMode::VramRGB565Mip;
    u8 maxLevel = mip ? static_cast<u8>(C3D_TexCalcMaxLevel(static_cast<u32>(w), static_cast<u32>(h))) : 0;
    if (!C3D_TexInitWithParams(tex, nullptr,
                               C3D_TexInitParams{static_cast<u16>(w), static_cast<u16>(h), maxLevel,
                                                 is565 ? GPU_RGB565 : GPU_RGBA8, GPU_TEX_2D, vram}))
        return false;
    std::vector<u8> cur(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        int sy = y * img.height / h;
        for (int x = 0; x < w; ++x) {
            int sx = x * img.width / w;
            std::memcpy(&cur[(static_cast<size_t>(y) * w + x) * 4], &img.rgba[(static_cast<size_t>(sy) * img.width + sx) * 4], 4);
        }
    }
    int lw = w, lh = h;
    for (int level = 0; level <= maxLevel; ++level) {
        // VRAM is not CPU-writable on real hardware (data abort; Azahar lets it through):
        // swizzle into a linear staging buffer and let citro3d copy it (memcpy for linear
        // textures, a GX texture copy for VRAM ones).
        u32 levelBytes = 0;
        C3D_Tex2DGetImagePtr(tex, level, &levelBytes);
        void* staging = linearAlloc(levelBytes);
        if (!staging) return false;
        swizzleLevel(cur.data(), lw, lh, staging, is565);
        GSPGPU_FlushDataCache(staging, levelBytes);
        C3D_TexLoadImage(tex, staging, GPU_TEXFACE_2D, level);
        linearFree(staging);
        if (level == maxLevel) break;
        int nw = lw / 2, nh = lh / 2; // 2x2 box filter
        std::vector<u8> next(static_cast<size_t>(nw) * nh * 4);
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x)
                for (int c = 0; c < 4; ++c) {
                    int sum = 0;
                    for (int dy = 0; dy < 2; ++dy)
                        for (int dx = 0; dx < 2; ++dx) sum += cur[(static_cast<size_t>(2 * y + dy) * lw + 2 * x + dx) * 4 + c];
                    next[(static_cast<size_t>(y) * nw + x) * 4 + c] = static_cast<u8>((sum + 2) / 4);
                }
        cur.swap(next);
        lw = nw;
        lh = nh;
    }
    C3D_TexFlush(tex);
    C3D_TexSetFilter(tex, GPU_LINEAR, GPU_LINEAR);
    if (mip) C3D_TexSetFilterMipmap(tex, GPU_LINEAR);
    C3D_TexSetWrap(tex, GPU_REPEAT, GPU_REPEAT);
    char line[160];
    std::snprintf(line, sizeof line, "%s texture %dx%d -> %dx%d %s, %d levels (%d KB)\n", what, img.width, img.height, w,
                  h, texModeName(mode), maxLevel + 1,
                  static_cast<int>(C3D_TexCalcTotalSize(tex->size, tex->maxLevel) / 1024));
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

    // A full-screen quad in clip space (z inside the PICA's [-w, 0] range) for the probe.
    if (!allocMesh(g_quad, 4, 6)) {
        *error = "cannot allocate the probe quad";
        return false;
    }
    const float qx[4] = {-1, 1, 1, -1}, qy[4] = {-1, -1, 1, 1};
    for (int i = 0; i < 4; ++i) {
        Vertex& v = g_quad.verts[i];
        v.pos[0] = qx[i];
        v.pos[1] = qy[i];
        v.pos[2] = -0.5f;
        v.col[0] = v.col[1] = v.col[2] = 255;
        v.col[3] = 64; // blend quads; the bottom screen quad overrides alpha in its tex env
        v.uv[0] = (qx[i] + 1.0f) * 0.5f;
        v.uv[1] = (qy[i] + 1.0f) * 0.5f;
    }
    const u16 qi[6] = {0, 1, 2, 0, 2, 3};
    std::memcpy(g_quad.indices, qi, sizeof qi);
    flushMesh(g_quad);
    return true;
}

bool renderSetTextures(TexMode mode, std::string* report) {
    if (g_terrainTexOk) C3D_TexDelete(&g_terrainTex);
    if (g_heliTexOk) C3D_TexDelete(&g_heliTex);
    g_terrainTexOk = g_heliTexOk = false;
    g_texMode = mode;
    bool ok = true;
    if (!g_terrainImg.rgba.empty()) {
        g_terrainTexOk = uploadTexture(g_terrainImg, &g_terrainTex, mode, report, "terrain");
        ok = ok && g_terrainTexOk;
    }
    if (!g_heliImg.rgba.empty()) {
        g_heliTexOk = uploadTexture(g_heliImg, &g_heliTex, mode, report, "heli");
        ok = ok && g_heliTexOk;
    }
    return ok;
}

bool renderSetTargets(bool right, bool bottom) {
    gfxSet3D(right);
    if (right && !g_targetRight) {
        g_targetRight = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
        if (!g_targetRight) return false;
        C3D_RenderTargetSetOutput(g_targetRight, GFX_TOP, GFX_RIGHT, DISPLAY_TRANSFER_FLAGS);
    } else if (!right && g_targetRight) {
        C3D_RenderTargetDelete(g_targetRight);
        g_targetRight = nullptr;
    }
    if (bottom && !g_targetBottom) {
        // consoleInit made the bottom screen RGB565 and single-buffered; the transfer writes RGB8.
        gfxSetScreenFormat(GFX_BOTTOM, GSP_BGR8_OES);
        gfxSetDoubleBuffering(GFX_BOTTOM, true);
        // 16-bit depth: the bottom screen is a flat quad.
        g_targetBottom = C3D_RenderTargetCreate(240, 320, GPU_RB_RGBA8, GPU_RB_DEPTH16);
        if (!g_targetBottom) return false;
        C3D_RenderTargetSetOutput(g_targetBottom, GFX_BOTTOM, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);
    } else if (!bottom && g_targetBottom) {
        C3D_RenderTargetDelete(g_targetBottom);
        g_targetBottom = nullptr;
    }
    return true;
}

void renderSetOptions(const FrameOptions& opts) { g_opts = opts; }

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
    std::string tpath = st.texturesDir + "\\texture1.tga";
    if (loadImage(vfs, tpath, g_terrainImg)) g_terrainTexOk = uploadTexture(g_terrainImg, &g_terrainTex, g_texMode, report, "terrain");
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
    if (loadImage(vfs, skin, g_heliImg)) g_heliTexOk = uploadTexture(g_heliImg, &g_heliTex, g_texMode, report, "heli");
    else *report += "cannot load skin " + skin + "\n";
    return true;
}

namespace {

// Draws the scene (terrain `passes` times, helicopter) into the currently selected target.
void drawScene(const World& world, const C3D_Mtx& mvp, const Vec3& eye, float farPlane, DrawStats& ds) {
    if (g_terrainTexOk) C3D_TexBind(0, &g_terrainTex);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g_uMvp, &mvp);
    for (int pass = 0; pass < g_opts.terrainPasses; ++pass) {
        for (const Mesh& m : g_chunks) {
            if (m.maxY < eye.y - 200.0f || m.minY > eye.y + farPlane) continue;
            bindMesh(m);
            C3D_DrawElements(GPU_TRIANGLES, m.indexCount, C3D_UNSIGNED_SHORT, m.indices);
            if (pass == 0) ds.terrainChunks++;
            ds.vertices += m.indexCount;
        }
    }
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
}

void drawQuad(C3D_Tex* tex) {
    C3D_Mtx id;
    Mtx_Identity(&id);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g_uMvp, &id);
    C3D_TexBind(0, tex);
    bindMesh(g_quad);
    C3D_DrawElements(GPU_TRIANGLES, g_quad.indexCount, C3D_UNSIGNED_SHORT, g_quad.indices);
}

// Quads use the vertex alpha as is; the scene keeps the texture alpha too.
void quadEnv(bool on) {
    C3D_TexEnv* env = C3D_GetTexEnv(0);
    if (on) {
        C3D_TexEnvSrc(env, C3D_Alpha, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
    } else {
        C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(env, C3D_Alpha, GPU_MODULATE);
    }
}

} // namespace

DrawStats renderFrame(const World& world) {
    DrawStats ds;
    // The engine's camera (world_render.cpp worldViewOf), with the PICA's tilted projection.
    const CameraState& c = world.camera();
    Vec3 eye{c.field[15], c.field[16], c.field[17]};
    float fovY = c.field[14] > 1.0f ? c.field[14] : 60.0f;
    float farPlane = 2000.0f;
    if (const Terrain* t = world.terrain()) {
        if (t->style().hasFog) farPlane = std::max(t->style().fogFar, 1000.0f);
    }
    Mat4 view = rotationX(c.field[18]) * rotationY(c.field[19]) * rotationZ(c.field[20]) * translation(-eye);
    C3D_Mtx viewM;
    toC3D(view, &viewM);
    const bool stereo = g_targetRight != nullptr;

    // Top eyes: left, then right (a slightly different sky so a capture shows both were drawn).
    for (int eyeIdx = 0; eyeIdx < (stereo ? 2 : 1); ++eyeIdx) {
        C3D_RenderTarget* target = eyeIdx == 0 ? g_target : g_targetRight;
        C3D_Mtx proj, mvp;
        if (stereo) {
            float iod = eyeIdx == 0 ? -g_opts.iod : g_opts.iod;
            Mtx_PerspStereoTilt(&proj, C3D_AngleFromDegrees(fovY), C3D_AspectRatioTop, 4.0f, farPlane, iod, 2.0f, false);
        } else {
            Mtx_PerspTilt(&proj, C3D_AngleFromDegrees(fovY), C3D_AspectRatioTop, 4.0f, farPlane, false);
        }
        Mtx_Multiply(&mvp, &proj, &viewM);
        C3D_RenderTargetClear(target, C3D_CLEAR_ALL, eyeIdx == 0 ? g_clearColor : (g_clearColor ^ 0x30000000u), 0);
        C3D_FrameDrawOn(target);
        drawScene(world, mvp, eye, farPlane, ds);
        if (g_opts.blendQuads > 0 && g_terrainTexOk) {
            C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
            C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA,
                           GPU_ONE_MINUS_SRC_ALPHA);
            quadEnv(true);
            for (int q = 0; q < g_opts.blendQuads; ++q) drawQuad((q & 1) && g_heliTexOk ? &g_heliTex : &g_terrainTex);
            quadEnv(false);
            C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
            C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
        }
    }

    // Bottom screen: one textured quad.
    if (g_targetBottom && g_terrainTexOk) {
        C3D_RenderTargetClear(g_targetBottom, C3D_CLEAR_ALL, 0x202020FF, 0);
        C3D_FrameDrawOn(g_targetBottom);
        C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
        quadEnv(true);
        drawQuad(&g_terrainTex);
        quadEnv(false);
        C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
        ds.vertices += g_quad.indexCount;
    }
    return ds;
}

void renderShutdown() {
    for (Mesh& m : g_chunks) freeMesh(m);
    g_chunks.clear();
    freeMesh(g_heli);
    freeMesh(g_quad);
    renderSetTargets(false, false);
    if (g_terrainTexOk) C3D_TexDelete(&g_terrainTex);
    if (g_heliTexOk) C3D_TexDelete(&g_heliTex);
    shaderProgramFree(&g_program);
    if (g_dvlb) DVLB_Free(g_dvlb);
    if (g_target) C3D_RenderTargetDelete(g_target);
}

} // namespace spike
