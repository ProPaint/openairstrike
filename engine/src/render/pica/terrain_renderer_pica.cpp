// TerrainRenderer (as3d/terrain_render.h) on the PICA (3DS). The GL twin is
// engine/src/render/gles/terrain_renderer.cpp; both draw the neutral geometry of
// as3d/terrain_build.h. terrain_pica.h says how render() reads TerrainViewParams.
//
// Memory (all GPU data in linear memory, textures through the budget of gfx_pica.cpp):
//   * one static vertex buffer per chunk (TerrainVertexPica: position, baked colour, uv0; the
//     detail uv comes from the position in the shader, the normals stay on the CPU for the
//     light stream), and per chunk one buffer per tile atlas with that chunk's overlay quads;
//   * one index buffer shared by every chunk: the chunk-local triangle list of 8 rows (a
//     shorter last chunk draws its prefix), u16;
//   * one zero buffer used as the dynamic-light stream of every unlit draw (the shader always
//     reads the stream, docs/3ds-port.md M1.3);
//   * base textures (one 256x256 per 32 rows, RGB565 with mip chains), the detail texture
//     (RGB565, mip chain) and the tile atlases (by content: RGBA5551 or RGBA4, no mips).
// Crater updates (update()) are copy-on-write: a touched chunk gets new buffers and the old
// ones are freed two frames later (the GPU may still read them in the frame in flight).
//
// Passes per render() (the GL pass order and state):
//   1. chunks: cull off, depth test + write, no blend; TEV base * colour + detail - 0.5
//      (tevTerrain2Stage); base texture on unit 0 per 32-row block, detail on unit 1;
//   2. overlays: alpha blend, depth test without write, the depth bias of terrain_pica.h,
//      TEV texture * colour (tevModulateTexColor, alpha = the atlas alpha).
// Both passes are fogged with the level's fog colour.
#include <3ds.h>
#include <citro3d.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

#include "as3d/material_key.h"
#include "as3d/render_state.h"
#include "as3d/terrain_render.h"
#include "pica_frame.h"
#include "shader_set.h"
#include "terrain_pica.h"
#include "tev.h"
#include "texture_budget.h"

namespace as3d {

namespace pica {
namespace {

TerrainPicaStats& stats() {
    static TerrainPicaStats s;
    return s;
}

float& overlayOffset() {
    static float v = kPolygonOffsetDepth;
    return v;
}

} // namespace

const TerrainPicaStats& terrainPicaStats() { return stats(); }
void setTerrainOverlayDepthOffset(float offset) { overlayOffset() = offset; }
float terrainOverlayDepthOffset() { return overlayOffset(); }

} // namespace pica

namespace {

using pica::TerrainVertexPica;
using LightBytes = uint8_t[4];

Image placeholderImage() {
    Image img;
    img.width = img.height = 8;
    img.rgba.resize(8 * 8 * 4);
    for (int i = 0; i < 64; i++) {
        img.rgba[i * 4 + 0] = 255;
        img.rgba[i * 4 + 1] = 0;
        img.rgba[i * 4 + 2] = 255;
        img.rgba[i * 4 + 3] = 255;
    }
    return img;
}

Image loadTexture(Vfs& vfs, const std::string& path, int* missing) {
    Blob blob;
    Image img;
    if (!vfs.read(path, blob) || !decodeTga(blob.data(), blob.size(), img)) {
        AS3D_WARN("terrain: missing texture '%s'", path.c_str());
        if (missing) (*missing)++;
        return placeholderImage();
    }
    return img;
}

uint8_t colourByte(float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); }

// A linear-memory copy of `count` vertices of `src` in the PICA layout, flushed. Null on failure.
TerrainVertexPica* uploadVertices(const TerrainVertex* src, size_t count) {
    if (count == 0) return nullptr;
    auto* dst = static_cast<TerrainVertexPica*>(linearAlloc(count * sizeof(TerrainVertexPica)));
    if (!dst) return nullptr;
    for (size_t i = 0; i < count; ++i) {
        const TerrainVertex& s = src[i];
        TerrainVertexPica& d = dst[i];
        d.pos[0] = s.pos[0];
        d.pos[1] = s.pos[1];
        d.pos[2] = s.pos[2];
        d.col[0] = colourByte(s.col[0]);
        d.col[1] = colourByte(s.col[1]);
        d.col[2] = colourByte(s.col[2]);
        d.col[3] = 255;
        d.uv0[0] = s.uv0[0];
        d.uv0[1] = s.uv0[1];
    }
    GSPGPU_FlushDataCache(dst, static_cast<u32>(count * sizeof(TerrainVertexPica)));
    return dst;
}

// Squared distance from `p` to the box (0 inside).
float boxDistanceSq(const Aabb& b, const Vec3& p) {
    const float dx = std::max({b.min.x - p.x, 0.0f, p.x - b.max.x});
    const float dy = std::max({b.min.y - p.y, 0.0f, p.y - b.max.y});
    const float dz = std::max({b.min.z - p.z, 0.0f, p.z - b.max.z});
    return dx * dx + dy * dy + dz * dz;
}

// Chunk-local vertex indices of the six overlay vertices of a quad at (c, lr), in the order of
// buildTerrainGeometry: (v00, v11, v01), (v00, v10, v11).
void quadCorners(int W, int c, int lr, int out[6]) {
    const int v00 = lr * (W + 1) + c, v10 = v00 + 1, v01 = v00 + W + 1, v11 = v01 + 1;
    out[0] = v00;
    out[1] = v11;
    out[2] = v01;
    out[3] = v00;
    out[4] = v10;
    out[5] = v11;
}

struct ChunkGpu {
    TerrainVertexPica* verts = nullptr;
    size_t vertCount = 0;
    std::vector<TerrainVertexPica*> tiles;   // per atlas, null when the chunk has no quad of it
    std::vector<size_t> tileCounts;
    // The light streams of this frame (the second eye reuses them).
    unsigned lightFrame = ~0u;
    const DynamicLight* lightSource = nullptr;
    size_t lightSourceCount = 0;
    const LightBytes* dyn = nullptr;         // null: unlit (the zero buffer)
    std::vector<const LightBytes*> tileDyn;
};

struct Retired {
    void* ptr;
    unsigned frame;
};

} // namespace

struct TerrainRenderer::Impl {
    const Terrain* terrain = nullptr;
    TerrainGeometry geo;
    std::vector<ChunkGpu> chunks;
    uint16_t* indices = nullptr; // the triangle list of a full chunk
    size_t indexCount = 0;
    LightBytes* zeros = nullptr; // zero light stream, as long as the longest buffer
    size_t zeroCount = 0;
    std::vector<Texture2D> baseTextures;
    Texture2D detail;
    std::vector<Texture2D> atlases; // parallel to geo.tiles
    float detailScale[2] = {0.25f / kHmapCellSize, 0.25f / kHmapCellSize};
    float detailOrigin[2] = {0.0f, 0.0f}; // grid vertex (0, 0)
    std::vector<Retired> retired;
    std::vector<uint8_t> lightScratch;
    std::vector<const DynamicLight*> lightPick;

    ~Impl() { release(); }
    void release();
    void retire(void* p);
    void collectRetired(bool all);
    void computeLights(size_t k, const DynamicLight* lights, size_t count);
};

void TerrainRenderer::Impl::release() {
    for (ChunkGpu& c : chunks) {
        if (c.verts) linearFree(c.verts);
        for (TerrainVertexPica* t : c.tiles)
            if (t) linearFree(t);
    }
    chunks.clear();
    collectRetired(true);
    if (indices) linearFree(indices);
    if (zeros) linearFree(zeros);
    indices = nullptr;
    zeros = nullptr;
    indexCount = zeroCount = 0;
    baseTextures.clear();
    detail = Texture2D();
    atlases.clear();
    geo = TerrainGeometry();
    terrain = nullptr;
}

void TerrainRenderer::Impl::retire(void* p) {
    if (!p) return;
    const unsigned frame = pica::backendReady() ? pica::frameArena().frames() : 0u;
    retired.push_back({p, frame});
}

// A buffer retired during (or after) frame F was last read by the GPU in frame F; the
// C3D_FrameBegin of F+1 waits for that frame's GPU work, so it is free from F+2's render() on.
void TerrainRenderer::Impl::collectRetired(bool all) {
    const unsigned now = pica::backendReady() ? pica::frameArena().frames() : 0u;
    size_t keep = 0;
    for (const Retired& r : retired) {
        if (all || now - r.frame >= 2u) linearFree(r.ptr);
        else retired[keep++] = r;
    }
    retired.resize(keep);
    pica::stats().deferredFrees = static_cast<int>(retired.size());
}

// The light streams of chunk k for this frame: the lights reaching its box, nearest first, at
// most kTerrainPicaLightsPerChunk; terrainDynamicLightColors gives the lit colours, the stream
// holds what they add to the baked colour (the shader adds the two and clamps).
void TerrainRenderer::Impl::computeLights(size_t k, const DynamicLight* lights, size_t count) {
    ChunkGpu& cg = chunks[k];
    const unsigned frame = pica::frameArena().frames();
    if (cg.lightFrame == frame && cg.lightSource == lights && cg.lightSourceCount == count) return;
    cg.lightFrame = frame;
    cg.lightSource = lights;
    cg.lightSourceCount = count;
    cg.dyn = nullptr;
    std::fill(cg.tileDyn.begin(), cg.tileDyn.end(), nullptr);
    if (!lights || count == 0) return;

    const TerrainChunkGeometry& ch = geo.chunks[k];
    lightPick.clear();
    for (size_t i = 0; i < count; ++i) {
        const DynamicLight& l = lights[i];
        if (l.radius <= 0.0f) continue;
        if (boxDistanceSq(ch.bounds, l.position) <= l.radius * l.radius) lightPick.push_back(&l);
    }
    if (lightPick.empty()) return;
    std::stable_sort(lightPick.begin(), lightPick.end(), [&](const DynamicLight* a, const DynamicLight* b) {
        return boxDistanceSq(ch.bounds, a->position) < boxDistanceSq(ch.bounds, b->position);
    });
    if (lightPick.size() > static_cast<size_t>(pica::kTerrainPicaLightsPerChunk))
        lightPick.resize(static_cast<size_t>(pica::kTerrainPicaLightsPerChunk));
    DynamicLight picked[pica::kTerrainPicaLightsPerChunk];
    for (size_t i = 0; i < lightPick.size(); ++i) picked[i] = *lightPick[i];
    terrainDynamicLightColors(ch, picked, static_cast<int>(lightPick.size()), lightScratch);

    // Lit colour minus the baked byte, per vertex.
    const size_t n = ch.verts.size();
    bool any = false;
    for (size_t i = 0; i < n; ++i) {
        for (int c = 0; c < 3; ++c) {
            const int lit = lightScratch[i * 4 + static_cast<size_t>(c)];
            const int base = cg.verts ? cg.verts[i].col[c] : colourByte(ch.verts[i].col[c]);
            const int d = std::max(lit - base, 0);
            lightScratch[i * 4 + static_cast<size_t>(c)] = static_cast<uint8_t>(d);
            any = any || d != 0;
        }
        lightScratch[i * 4 + 3] = 0;
    }
    if (!any) return;
    auto* dyn = pica::frameArena().allocArray<LightBytes>(n);
    if (!dyn) return; // arena full: drawn unlit
    std::memcpy(dyn, lightScratch.data(), n * 4);
    cg.dyn = dyn;
    pica::stats().arenaBytes += n * 4;

    // The overlay quads share the chunk's grid vertices (same position, colour and normal).
    const int W = geo.width;
    for (size_t b = 0; b < geo.tiles.size(); ++b) {
        const TerrainTileGeometry& tg = geo.tiles[b];
        const auto range = tg.ranges[k];
        if (range.second <= 0) continue;
        auto* td = pica::frameArena().allocArray<LightBytes>(static_cast<size_t>(range.second));
        if (!td) continue;
        const int q0 = range.first / 6, quads = range.second / 6;
        for (int q = 0; q < quads; ++q) {
            const auto cell = tg.cells[static_cast<size_t>(q0 + q)];
            int corner[6];
            quadCorners(W, cell.first, cell.second - ch.firstRow, corner);
            for (int i = 0; i < 6; ++i)
                std::memcpy(td[q * 6 + i], &lightScratch[static_cast<size_t>(corner[i]) * 4], 4);
        }
        cg.tileDyn[b] = td;
        pica::stats().arenaBytes += static_cast<size_t>(range.second) * 4;
    }
}

TerrainRenderer::TerrainRenderer() : impl_(new Impl) {}
TerrainRenderer::~TerrainRenderer() = default;

int TerrainRenderer::chunkCount() const { return static_cast<int>(impl_->geo.chunks.size()); }

bool TerrainRenderer::chunkRows(int k, int& rowStart, int& rows) const {
    if (k < 0 || k >= chunkCount()) return false;
    rowStart = impl_->geo.chunks[static_cast<size_t>(k)].firstRow;
    rows = impl_->geo.chunks[static_cast<size_t>(k)].rows;
    return true;
}

bool TerrainRenderer::build(const Terrain& terrain, Vfs& vfs, std::string* error, const RenderRules& rules) {
    Impl& im = *impl_;
    im.release();
    missing_ = 0;
    lastUpdated_ = 0;
    lastVisible_ = 0;
    pica::TerrainPicaStats& st = pica::stats();
    st = pica::TerrainPicaStats();
    if (!terrain.level() || terrain.positions().empty()) {
        if (error) *error = "terrain not built";
        return false;
    }
    const LevelData& level = *terrain.level();
    const TerrainStyle& def = terrain.style();
    const int H = terrain.height();

    // Base textures: RGB565 with mip chains, VRAM first (texture_budget.h).
    Image src[4];
    for (int i = 0; i < 4; i++)
        src[i] = loadTexture(vfs, def.texturesDir + "\\texture" + std::to_string(i + 1) + ".tga", &missing_);
    const int blocks = (H + kBaseTextureRows - 1) / kBaseTextureRows;
    TextureOptions topts;
    topts.mipmaps = true;
    topts.minFilter = Filter::Linear;
    topts.magFilter = Filter::Linear;
    topts.cls = TextureClass::Opaque;
    im.baseTextures.resize(static_cast<size_t>(blocks));
    {
        std::vector<u8> rgb;
        Image img;
        img.width = img.height = kBaseTextureSize;
        img.rgba.resize(static_cast<size_t>(kBaseTextureSize) * kBaseTextureSize * 4);
        for (int b = 0; b < blocks; b++) {
            generateBaseTextureRgb(level, src, b, rgb);
            for (size_t i = 0; i < static_cast<size_t>(kBaseTextureSize) * kBaseTextureSize; i++) {
                img.rgba[i * 4 + 0] = rgb[i * 3 + 0];
                img.rgba[i * 4 + 1] = rgb[i * 3 + 1];
                img.rgba[i * 4 + 2] = rgb[i * 3 + 2];
                img.rgba[i * 4 + 3] = 255;
            }
            im.baseTextures[static_cast<size_t>(b)].create(img, topts);
        }
    }
    for (Image& s : src) s = Image();
    Image det = loadTexture(vfs, def.texturesDir + "\\detail.tga", &missing_);
    im.detail.create(det, topts);
    det = Image();

    // Tile atlases (their sizes give the overlay UVs).
    std::map<int, std::pair<int, int>> atlasSizes;
    std::map<int, Image> atlasImages;
    for (int set : terrainTileSets(level)) {
        Image img = loadTexture(vfs, "tiles\\tiles" + std::to_string(set) + ".tga", &missing_);
        atlasSizes[set] = {img.width, img.height};
        atlasImages[set] = std::move(img);
    }

    im.geo = buildTerrainGeometry(terrain, rules, atlasSizes);
    if (!im.geo.valid()) {
        if (error) *error = "terrain geometry empty";
        im.release();
        return false;
    }
    const int W = im.geo.width;
    if (static_cast<size_t>(kTerrainChunkRows + 1) * static_cast<size_t>(W + 1) > 65535) {
        if (error) *error = "terrain chunk too large for 16-bit indices";
        im.release();
        return false;
    }

    TextureOptions aopts;
    aopts.mipmaps = false;
    aopts.wrapS = aopts.wrapT = Wrap::Repeat;
    aopts.cls = TextureClass::Auto; // RGBA5551 for cut-out atlases, RGBA4 with soft edges
    im.atlases.resize(im.geo.tiles.size());
    for (size_t b = 0; b < im.geo.tiles.size(); ++b) im.atlases[b].create(atlasImages[im.geo.tiles[b].set], aopts);
    atlasImages.clear();

    // Detail uv = (column, row) * 0.25 from the position (the grid is regular).
    {
        const TerrainChunkGeometry& c0 = im.geo.chunks.front();
        if (c0.verts.size() > static_cast<size_t>(W + 1) && W > 0) {
            const TerrainVertex& a = c0.verts[0];
            const TerrainVertex& bx = c0.verts[1];
            const TerrainVertex& by = c0.verts[static_cast<size_t>(W + 1)];
            const float dx = bx.pos[0] - a.pos[0], dy = by.pos[1] - a.pos[1];
            if (dx != 0.0f) im.detailScale[0] = 0.25f / dx;
            if (dy != 0.0f) im.detailScale[1] = 0.25f / dy;
            im.detailOrigin[0] = a.pos[0];
            im.detailOrigin[1] = a.pos[1];
        }
    }

    // Shared index list of a full chunk (chunk-local indices as the builder makes them).
    size_t longest = 0;
    for (const TerrainChunkGeometry& c : im.geo.chunks) {
        if (c.tris.size() > im.indexCount) im.indexCount = c.tris.size();
        longest = std::max(longest, c.verts.size());
    }
    for (const TerrainTileGeometry& tg : im.geo.tiles)
        for (const auto& r : tg.ranges) longest = std::max(longest, static_cast<size_t>(std::max(r.second, 0)));
    for (const TerrainChunkGeometry& c : im.geo.chunks) {
        if (c.tris.size() == im.indexCount) {
            im.indices = static_cast<uint16_t*>(linearAlloc(im.indexCount * sizeof(uint16_t)));
            if (im.indices) {
                std::memcpy(im.indices, c.tris.data(), im.indexCount * sizeof(uint16_t));
                GSPGPU_FlushDataCache(im.indices, static_cast<u32>(im.indexCount * sizeof(uint16_t)));
            }
            break;
        }
    }
    im.zeroCount = longest;
    im.zeros = static_cast<LightBytes*>(linearAlloc(std::max<size_t>(longest, 1) * 4));
    if (im.zeros) {
        std::memset(im.zeros, 0, std::max<size_t>(longest, 1) * 4);
        GSPGPU_FlushDataCache(im.zeros, static_cast<u32>(std::max<size_t>(longest, 1) * 4));
    }

    // Vertex buffers.
    bool ok = im.indices && im.zeros;
    im.chunks.resize(im.geo.chunks.size());
    for (size_t k = 0; ok && k < im.geo.chunks.size(); ++k) {
        const TerrainChunkGeometry& c = im.geo.chunks[k];
        ChunkGpu& g = im.chunks[k];
        g.vertCount = c.verts.size();
        g.verts = uploadVertices(c.verts.data(), c.verts.size());
        ok = g.verts != nullptr;
        g.tiles.assign(im.geo.tiles.size(), nullptr);
        g.tileCounts.assign(im.geo.tiles.size(), 0);
        g.tileDyn.assign(im.geo.tiles.size(), nullptr);
        st.vertexBytes += g.vertCount * sizeof(TerrainVertexPica);
        for (size_t b = 0; ok && b < im.geo.tiles.size(); ++b) {
            const auto range = im.geo.tiles[b].ranges[k];
            if (range.second <= 0) continue;
            g.tiles[b] = uploadVertices(&im.geo.tiles[b].verts[static_cast<size_t>(range.first)],
                                        static_cast<size_t>(range.second));
            g.tileCounts[b] = static_cast<size_t>(range.second);
            ok = g.tiles[b] != nullptr;
            st.vertexBytes += g.tileCounts[b] * sizeof(TerrainVertexPica);
        }
    }
    if (!ok) {
        if (error) *error = "out of linear memory for the terrain";
        im.release();
        return false;
    }
    im.terrain = &terrain;

    st.chunks = static_cast<int>(im.geo.chunks.size());
    st.tileSets = static_cast<int>(im.geo.tiles.size());
    st.vertices = im.geo.vertexCount;
    for (const TerrainTileGeometry& tg : im.geo.tiles) {
        st.tileVertices += tg.verts.size();
        st.tileQuads += static_cast<int>(tg.cells.size());
    }
    st.indexBytes = im.indexCount * sizeof(uint16_t) + std::max<size_t>(longest, 1) * 4;
    auto count = [&st](const Texture2D& t) {
        if (!t.valid()) return;
        st.textureBytes += t.bytes();
        ++st.textures;
        pica::Placement p = pica::Placement::Linear;
        pica::textureInfo(t, &p, nullptr, nullptr, nullptr, nullptr);
        (p == pica::Placement::Vram ? st.textureVramBytes : st.textureLinearBytes) += t.bytes();
    };
    for (const Texture2D& t : im.baseTextures) count(t);
    count(im.detail);
    for (const Texture2D& t : im.atlases) count(t);
    return true;
}

void TerrainRenderer::update(const VertexRect* rects, size_t count, const TerrainGridView& grid) {
    Impl& im = *impl_;
    lastUpdated_ = 0;
    if (!im.terrain) return;
    im.collectRetired(false);
    const std::vector<int> changed = updateChunkHeights(im.geo, rects, count, grid);
    for (int ki : changed) {
        const size_t k = static_cast<size_t>(ki);
        const TerrainChunkGeometry& c = im.geo.chunks[k];
        ChunkGpu& g = im.chunks[k];
        // Copy on write: the frame in flight may still read the old buffers.
        if (TerrainVertexPica* nv = uploadVertices(c.verts.data(), c.verts.size())) {
            im.retire(g.verts);
            g.verts = nv;
        }
        for (size_t b = 0; b < im.geo.tiles.size(); ++b) {
            const auto range = im.geo.tiles[b].ranges[k];
            if (range.second <= 0 || !g.tiles[b]) continue;
            if (TerrainVertexPica* nt = uploadVertices(&im.geo.tiles[b].verts[static_cast<size_t>(range.first)],
                                                       static_cast<size_t>(range.second))) {
                im.retire(g.tiles[b]);
                g.tiles[b] = nt;
            }
        }
        g.lightFrame = ~0u; // the lit colours follow the moved vertices
        ++lastUpdated_;
    }
    pica::stats().updatedChunks = lastUpdated_;
    pica::stats().deferredFrees = static_cast<int>(im.retired.size());
}

void TerrainRenderer::render(const TerrainViewParams& params, const TerrainRenderOptions& options) {
    Impl& im = *impl_;
    pica::TerrainPicaStats& st = pica::stats();
    st.visibleChunks = st.litChunks = st.drawCalls = 0;
    st.indicesDrawn = 0;
    st.arenaBytes = 0;
    if (!im.terrain || !pica::backendReady()) return;
    im.collectRetired(false);

    const Mat4 vp = params.projection * params.view;
    // Culling: the GL frustum planes of the matrix, except the far plane: the PICA's clip z
    // runs from -w (near) to 0 (far), so "far" is z <= 0.
    TerrainFrustum fr = extractTerrainFrustum(vp);
    fr.p[5][0] = -vp.at(0, 2);
    fr.p[5][1] = -vp.at(1, 2);
    fr.p[5][2] = -vp.at(2, 2);
    fr.p[5][3] = -vp.at(3, 2);
    const std::vector<int> visible = visibleTerrainChunks(im.geo, fr);
    lastVisible_ = static_cast<int>(visible.size());
    st.visibleChunks = lastVisible_;
    if (visible.empty()) return;

    // Planes from the projection: z_clip = a * z_eye + b with near at -w and far at 0.
    const float a = params.projection.at(2, 2), b = params.projection.at(3, 2);
    float nearPlane = 4.0f, farPlane = 2000.0f;
    if (a != 0.0f && a != 1.0f) {
        nearPlane = b / (a - 1.0f);
        farPlane = b / a;
    }
    if (!(nearPlane > 0.0f) || !(farPlane > nearPlane)) {
        nearPlane = 4.0f;
        farPlane = 2000.0f;
    }
    pica::setDepthMapping(FogDepthMap::WBuffer, nearPlane);
    const bool fog = params.fogStart < 1.0e7f && params.fogEnd > params.fogStart;
    if (fog) pica::setFog(nearPlane, farPlane, params.fogStart, params.fogEnd);
    else pica::disableFog();
    pica::setLevelFogColor(params.fogColour);

    MaterialKey key;
    key.blend = RenderBlend::None;
    key.depthTest = true;
    key.depthWrite = true;
    key.cull = CullMode::Off;
    key.fog = FogColorMode::Level;
    applyMaterialState(key);

    pica::ShaderSet& sh = pica::shaders();
    sh.bind(pica::Shader::Terrain);
    sh.setMvp(vp);
    const bool useDetail = options.detail && im.detail.valid();
    pica::applyTev(useDetail ? pica::tevTerrain2Stage() : pica::tevModulateTexColor());
    if (useDetail) im.detail.bind(1);

    const size_t lightCount = std::min(params.lightCount, static_cast<size_t>(kMaxDynamicLights));
    const int W = im.geo.width;
    int boundBlock = -1;
    for (int ki : visible) {
        const size_t k = static_cast<size_t>(ki);
        const TerrainChunkGeometry& c = im.geo.chunks[k];
        ChunkGpu& g = im.chunks[k];
        im.computeLights(k, params.lights, lightCount);
        if (g.dyn) ++st.litChunks;
        if (c.block != boundBlock) {
            im.baseTextures[static_cast<size_t>(c.block)].bind(0);
            boundBlock = c.block;
        }
        // Detail uv relative to the chunk's first row (whole repeats dropped) keeps the
        // interpolated coordinates small.
        const float rowUv = std::floor(static_cast<float>(c.firstRow) * 0.25f);
        sh.setTerrainDetail(im.detailScale[0], im.detailScale[1], -im.detailOrigin[0] * im.detailScale[0],
                            -im.detailOrigin[1] * im.detailScale[1] - rowUv);
        sh.setTerrainVertices(g.verts, g.dyn ? g.dyn : im.zeros);
        const int n = c.rows * W * 6;
        C3D_DrawElements(GPU_TRIANGLES, n, C3D_UNSIGNED_SHORT, im.indices);
        ++st.drawCalls;
        st.indicesDrawn += static_cast<size_t>(n);
    }

    if (options.tiles && !im.atlases.empty()) {
        key.blend = RenderBlend::Alpha;
        key.depthWrite = false;
        applyMaterialState(key);
        pica::applyDepthMap(pica::terrainOverlayDepthOffset());
        pica::applyTev(pica::tevModulateTexColor());
        for (size_t bi = 0; bi < im.atlases.size(); ++bi) {
            if (!im.atlases[bi].valid()) continue;
            im.atlases[bi].bind(0);
            for (int ki : visible) {
                const ChunkGpu& g = im.chunks[static_cast<size_t>(ki)];
                if (!g.tiles[bi]) continue;
                sh.setTerrainVertices(g.tiles[bi], g.tileDyn[bi] ? g.tileDyn[bi] : im.zeros);
                C3D_DrawArrays(GPU_TRIANGLES, 0, static_cast<int>(g.tileCounts[bi]));
                ++st.drawCalls;
                st.indicesDrawn += g.tileCounts[bi];
            }
        }
        pica::applyDepthMap(0.0f);
    }

    // The state the GL renderer leaves: depth test + write, no blend (cull stays off).
    key.blend = RenderBlend::None;
    key.depthWrite = true;
    applyMaterialState(key);
}

} // namespace as3d
