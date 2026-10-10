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
//
// Far-chunk level of detail (pica::RenderQuality::terrainLod, docs/3ds-port.md "Stereo at 60"): a
// chunk whose box is farther than terrainLodDistance from the central camera draws a coarse grid,
// every second vertex in both directions, from a "flattened" copy of its vertex buffer in which
// every odd vertex lies on the coarse triangles (its height and colour interpolated along the
// coarse edge or diagonal it sits on). The coarse cells keep the fine diagonal (v00-v11), so each
// fine overlay quad drawn on the flattened vertices lies inside one coarse triangle and the tile
// overlays keep their depth test. Seams: two coarse chunks share their flattened edge row (the
// same float operations, so the same vertices); a coarse chunk next to a fine one closes the gap
// between the fine edge polyline and its straight coarse edge with vertical fill triangles (the
// fine edge's odd vertices are appended to the flattened buffer), and the coarse triangles along
// both edge rows are split at the edge midpoints so no T-junction remains. The light streams cover
// the appended vertices (copied from their grid vertex).
#include <3ds.h>
#include <citro3d.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

#include "as3d/core.h"
#include "as3d/material_key.h"
#include "as3d/render_state.h"
#include "as3d/terrain_render.h"
#include "pica_frame.h"
#include "state_check_pica.h"
#include "quality_pica.h"
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
    // The coarse level (null when the chunk has none): the flattened grid plus the fine edge
    // rows' odd vertices (top row first), and the overlay quads on the flattened grid.
    TerrainVertexPica* coarse = nullptr;
    std::vector<TerrainVertexPica*> coarseTiles;
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

// The coarse index lists of a chunk of `rows` rows (even), W columns (even), in linear memory:
// variant (top fill ? 1 : 0) | (bottom fill ? 2 : 0) is lists[variant].
struct CoarseLists {
    uint16_t* idx[4] = {nullptr, nullptr, nullptr, nullptr};
    int count[4] = {0, 0, 0, 0};
};

bool coarseEligible(int rows, int W) { return rows >= 2 && (rows & 1) == 0 && W >= 2 && (W & 1) == 0; }

// The visible chunks that may change level in one frame (render()); the camera crosses a chunk
// (8 rows, 320 world units) every second or two, so this only bites when many are due at once.
constexpr size_t kLodSwitchesPerFrame = 2;

// The flattened copy of a chunk's vertices (see the header) plus the appended fine edge vertices.
void flattenChunk(const TerrainChunkGeometry& c, int W, std::vector<TerrainVertex>& out) {
    const int vw = W + 1, R = c.rows;
    out.assign(c.verts.begin(), c.verts.end());
    auto at = [&](int lr, int col) -> const TerrainVertex& { return c.verts[static_cast<size_t>(lr * vw + col)]; };
    auto lerp = [&](TerrainVertex& o, const TerrainVertex& a, const TerrainVertex& b) {
        o.pos[2] = a.pos[2] + 0.5f * (b.pos[2] - a.pos[2]);
        for (int k = 0; k < 3; ++k) o.col[k] = a.col[k] + 0.5f * (b.col[k] - a.col[k]);
    };
    for (int lr = 0; lr <= R; ++lr) {
        for (int col = 0; col <= W; ++col) {
            const bool oddR = (lr & 1) != 0, oddC = (col & 1) != 0;
            if (!oddR && !oddC) continue;
            TerrainVertex& o = out[static_cast<size_t>(lr * vw + col)];
            if (!oddR) lerp(o, at(lr, col - 1), at(lr, col + 1));          // a coarse row edge
            else if (!oddC) lerp(o, at(lr - 1, col), at(lr + 1, col));     // a coarse column edge
            else lerp(o, at(lr - 1, col - 1), at(lr + 1, col + 1));        // the coarse diagonal
        }
    }
    for (int col = 1; col < W; col += 2) out.push_back(at(0, col));
    for (int col = 1; col < W; col += 2) out.push_back(at(R, col));
}

// The fine grid vertex an appended edge vertex copies (light streams).
size_t coarseExtraSource(int i, int W, int rows) {
    const int half = W / 2;
    return i < half ? static_cast<size_t>(2 * i + 1) : static_cast<size_t>(rows * (W + 1) + 2 * (i - half) + 1);
}

std::vector<uint16_t> buildCoarseIndices(int R, int W, bool topFill, bool bottomFill) {
    std::vector<uint16_t> out;
    const int vw = W + 1;
    const int n = (R + 1) * vw;
    auto v = [&](int lr, int col) { return static_cast<uint16_t>(lr * vw + col); };
    auto tri = [&](uint16_t a, uint16_t b, uint16_t c) {
        out.push_back(a);
        out.push_back(b);
        out.push_back(c);
    };
    for (int r = 0; r < R; r += 2) {
        for (int c = 0; c < W; c += 2) {
            const uint16_t v00 = v(r, c), v10 = v(r, c + 2), v01 = v(r + 2, c), v11 = v(r + 2, c + 2);
            if (r == 0) { // (v00 v10 v11) split at the top edge's midpoint
                const uint16_t m = v(0, c + 1);
                tri(v00, m, v11);
                tri(m, v10, v11);
            } else {
                tri(v00, v10, v11);
            }
            if (r + 2 == R) { // (v00 v11 v01) split at the bottom edge's midpoint
                const uint16_t m = v(R, c + 1);
                tri(v00, v11, m);
                tri(v00, m, v01);
            } else {
                tri(v00, v11, v01);
            }
        }
    }
    const int half = W / 2;
    for (int j = 0; j < half; ++j) {
        if (topFill) {
            const uint16_t t = static_cast<uint16_t>(n + j), m = v(0, 2 * j + 1);
            tri(v(0, 2 * j), m, t);
            tri(m, v(0, 2 * j + 2), t);
        }
        if (bottomFill) {
            const uint16_t t = static_cast<uint16_t>(n + half + j), m = v(R, 2 * j + 1);
            tri(v(R, 2 * j), t, m);
            tri(m, t, v(R, 2 * j + 2));
        }
    }
    return out;
}

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
    std::map<int, CoarseLists> coarseLists; // by chunk row count
    std::vector<TerrainVertex> flatScratch, flatTileScratch;
    std::vector<uint8_t> coarseOf;          // the level per chunk (1 = coarse), kept between frames (hysteresis)
    unsigned lodFrame = ~0u;                // the arena frame whose first render decided coarseOf
    std::vector<uint8_t> lodVisibleMark;    // scratch: the visible chunks of that render
    std::vector<std::pair<float, size_t>> lodCandidates; // scratch: visible chunks due a change (cosine, chunk)

    ~Impl() { release(); }
    void release();
    void retire(void* p);
    void collectRetired(bool all);
    void computeLights(size_t k, const DynamicLight* lights, size_t count);
    size_t extraOf(size_t k) const {
        return chunks[k].coarse ? static_cast<size_t>(geo.width) : 0u; // appended edge vertices
    }
    bool buildCoarse(size_t k, bool replace);
    const CoarseLists* coarseListsFor(int rows);
};

// The coarse buffers of chunk k from its current vertices (build and crater updates); with
// `replace` the old ones are retired. False without linear memory (the chunk stays fine).
bool TerrainRenderer::Impl::buildCoarse(size_t k, bool replace) {
    const TerrainChunkGeometry& c = geo.chunks[k];
    ChunkGpu& g = chunks[k];
    const int W = geo.width;
    if (!coarseEligible(c.rows, W)) return false;
    flattenChunk(c, W, flatScratch);
    TerrainVertexPica* nv = uploadVertices(flatScratch.data(), flatScratch.size());
    if (!nv) return false;
    std::vector<TerrainVertexPica*> nt(geo.tiles.size(), nullptr);
    for (size_t b = 0; b < geo.tiles.size(); ++b) {
        const TerrainTileGeometry& tg = geo.tiles[b];
        const auto range = tg.ranges[k];
        if (range.second <= 0) continue;
        flatTileScratch.assign(tg.verts.begin() + range.first, tg.verts.begin() + range.first + range.second);
        const int q0 = range.first / 6, quads = range.second / 6;
        for (int q = 0; q < quads; ++q) {
            const auto cell = tg.cells[static_cast<size_t>(q0 + q)];
            int corner[6];
            quadCorners(W, cell.first, cell.second - c.firstRow, corner);
            for (int i = 0; i < 6; ++i) {
                const TerrainVertex& f = flatScratch[static_cast<size_t>(corner[i])];
                TerrainVertex& o = flatTileScratch[static_cast<size_t>(q * 6 + i)];
                o.pos[2] = f.pos[2];
                for (int ch = 0; ch < 3; ++ch) o.col[ch] = f.col[ch];
            }
        }
        nt[b] = uploadVertices(flatTileScratch.data(), flatTileScratch.size());
        if (!nt[b]) {
            for (TerrainVertexPica* t : nt)
                if (t) linearFree(t);
            linearFree(nv);
            return false;
        }
    }
    if (replace) {
        retire(g.coarse);
        for (TerrainVertexPica* t : g.coarseTiles) retire(t);
    }
    g.coarse = nv;
    g.coarseTiles = std::move(nt);
    return true;
}

const CoarseLists* TerrainRenderer::Impl::coarseListsFor(int rows) {
    auto found = coarseLists.find(rows);
    if (found != coarseLists.end()) return found->second.idx[0] ? &found->second : nullptr;
    CoarseLists& l = coarseLists[rows];
    if (!coarseEligible(rows, geo.width)) return nullptr;
    for (int var = 0; var < 4; ++var) {
        const std::vector<uint16_t> list = buildCoarseIndices(rows, geo.width, (var & 1) != 0, (var & 2) != 0);
        auto* buf = static_cast<uint16_t*>(linearAlloc(list.size() * sizeof(uint16_t)));
        if (!buf) {
            for (uint16_t*& p : l.idx) {
                if (p) linearFree(p);
                p = nullptr;
            }
            return nullptr;
        }
        std::memcpy(buf, list.data(), list.size() * sizeof(uint16_t));
        GSPGPU_FlushDataCache(buf, static_cast<u32>(list.size() * sizeof(uint16_t)));
        l.idx[var] = buf;
        l.count[var] = static_cast<int>(list.size());
    }
    return &l;
}

void TerrainRenderer::Impl::release() {
    for (ChunkGpu& c : chunks) {
        if (c.verts) linearFree(c.verts);
        for (TerrainVertexPica* t : c.tiles)
            if (t) linearFree(t);
        if (c.coarse) linearFree(c.coarse);
        for (TerrainVertexPica* t : c.coarseTiles)
            if (t) linearFree(t);
    }
    chunks.clear();
    for (auto& kv : coarseLists)
        for (uint16_t* p : kv.second.idx)
            if (p) linearFree(p);
    coarseLists.clear();
    coarseOf.clear();
    lodFrame = ~0u;
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
    // Only the vertices the picked lights can reach: a conservative rectangle of the chunk's
    // grid (vertex (c, r) sits at (c, r) * kHmapCellSize; outside it every light is farther than
    // its radius, so the lit colour is the baked one and what it adds is 0).
    const size_t n = ch.verts.size();
    const size_t extra = extraOf(k);
    const int vw = geo.width + 1;
    int c0 = vw, c1 = -1, r0 = ch.rows + 1, r1 = -1;
    for (size_t i = 0; i < lightPick.size(); ++i) {
        const DynamicLight& l = picked[i];
        const float R = l.radius * 1.01f + 1.0f;
        const float x0 = (l.position.x - R) / kHmapCellSize, x1 = (l.position.x + R) / kHmapCellSize;
        const float y0 = (l.position.y - R) / kHmapCellSize - static_cast<float>(ch.firstRow);
        const float y1 = (l.position.y + R) / kHmapCellSize - static_cast<float>(ch.firstRow);
        if (!(x0 > -1.0e6f && x1 < 1.0e6f && y0 > -1.0e6f && y1 < 1.0e6f)) { // not finite or huge: all
            c0 = r0 = 0;
            c1 = vw - 1;
            r1 = ch.rows;
            break;
        }
        c0 = std::min(c0, static_cast<int>(std::floor(x0)));
        c1 = std::max(c1, static_cast<int>(std::ceil(x1)));
        r0 = std::min(r0, static_cast<int>(std::floor(y0)));
        r1 = std::max(r1, static_cast<int>(std::ceil(y1)));
    }
    c0 = std::max(c0, 0);
    r0 = std::max(r0, 0);
    c1 = std::min(c1, vw - 1);
    r1 = std::min(r1, ch.rows);
    lightScratch.assign(n * 4, 0);
    if (c0 > c1 || r0 > r1) return;
    terrainDynamicLightColors(ch, vw, picked, static_cast<int>(lightPick.size()), c0, c1, r0, r1, lightScratch);

    // Lit colour minus the baked byte, per vertex of the rectangle (0 elsewhere).
    bool any = false;
    for (int r = r0; r <= r1; ++r) {
        for (int col = c0; col <= c1; ++col) {
            const size_t i = static_cast<size_t>(r) * static_cast<size_t>(vw) + static_cast<size_t>(col);
            for (int c = 0; c < 3; ++c) {
                const int lit = lightScratch[i * 4 + static_cast<size_t>(c)];
                const int base = cg.verts ? cg.verts[i].col[c] : colourByte(ch.verts[i].col[c]);
                const int d = std::max(lit - base, 0);
                lightScratch[i * 4 + static_cast<size_t>(c)] = static_cast<uint8_t>(d);
                any = any || d != 0;
            }
            lightScratch[i * 4 + 3] = 0;
        }
    }
    if (!any) return;
    auto* dyn = pica::frameArena().allocArray<LightBytes>(n + extra);
    if (!dyn) return; // arena full: drawn unlit
    std::memcpy(dyn, lightScratch.data(), n * 4);
    for (size_t i = 0; i < extra; ++i) // the coarse level's appended edge vertices
        std::memcpy(dyn[n + i], &lightScratch[coarseExtraSource(static_cast<int>(i), geo.width, ch.rows) * 4], 4);
    cg.dyn = dyn;
    pica::stats().arenaBytes += (n + extra) * 4;

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

    // The load profile (AS3D_LOAD phase=terrain): read+decode, base generation, texture
    // creation, geometry, vertex upload.
    const u64 tStart = svcGetSystemTick();
    u64 tGenerate = 0, tCreate = 0;
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
            const u64 g0 = svcGetSystemTick();
            generateBaseTextureRgb(level, src, b, rgb);
            for (size_t i = 0; i < static_cast<size_t>(kBaseTextureSize) * kBaseTextureSize; i++) {
                img.rgba[i * 4 + 0] = rgb[i * 3 + 0];
                img.rgba[i * 4 + 1] = rgb[i * 3 + 1];
                img.rgba[i * 4 + 2] = rgb[i * 3 + 2];
                img.rgba[i * 4 + 3] = 255;
            }
            const u64 g1 = svcGetSystemTick();
            im.baseTextures[static_cast<size_t>(b)].create(img, topts);
            tGenerate += g1 - g0;
            tCreate += svcGetSystemTick() - g1;
        }
    }
    for (Image& s : src) s = Image();
    if (pica::renderQuality().terrainDetail) { // M4.1: the Old 3DS profile has no detail texture
        Image det = loadTexture(vfs, def.texturesDir + "\\detail.tga", &missing_);
        im.detail.create(det, topts);
    }

    // Tile atlases (their sizes give the overlay UVs).
    std::map<int, std::pair<int, int>> atlasSizes;
    std::map<int, Image> atlasImages;
    for (int set : terrainTileSets(level)) {
        Image img = loadTexture(vfs, "tiles\\tiles" + std::to_string(set) + ".tga", &missing_);
        atlasSizes[set] = {img.width, img.height};
        atlasImages[set] = std::move(img);
    }

    const u64 tGeo0 = svcGetSystemTick();
    im.geo = buildTerrainGeometry(terrain, rules, atlasSizes);
    const u64 tGeo1 = svcGetSystemTick();
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
    const u64 tAtlas0 = svcGetSystemTick();
    for (size_t b = 0; b < im.geo.tiles.size(); ++b) im.atlases[b].create(atlasImages[im.geo.tiles[b].set], aopts);
    atlasImages.clear();
    tCreate += svcGetSystemTick() - tAtlas0;

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
    longest += static_cast<size_t>(W); // a coarse level's appended edge vertices
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
    // The coarse level of every eligible chunk (far-chunk LOD; a chunk without one stays fine).
    for (size_t k = 0; k < im.geo.chunks.size(); ++k) {
        if (!im.buildCoarse(k, false)) continue;
        ++st.coarseBuilt;
        st.vertexBytes += (im.chunks[k].vertCount + static_cast<size_t>(W)) * sizeof(TerrainVertexPica);
        for (size_t b = 0; b < im.geo.tiles.size(); ++b)
            if (im.chunks[k].coarseTiles[b]) st.vertexBytes += im.chunks[k].tileCounts[b] * sizeof(TerrainVertexPica);
    }
    im.coarseListsFor(kTerrainChunkRows);
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
    {
        const double k = static_cast<double>(SYSCLOCK_ARM11) / 1000.0;
        const u64 tEnd = svcGetSystemTick();
        const u64 geo = tGeo1 - tGeo0;
        const u64 rest = (tEnd - tStart) - tGenerate - tCreate - geo; // reads, decodes, vertex upload
        AS3D_INFO("AS3D_LOAD phase=terrain ms=%.0f generate_base=%.0f (%d blocks) textures_create=%.0f geometry=%.0f "
                  "read_decode_upload=%.0f",
                  static_cast<double>(tEnd - tStart) / k, static_cast<double>(tGenerate) / k, blocks,
                  static_cast<double>(tCreate) / k, static_cast<double>(geo) / k, static_cast<double>(rest) / k);
    }
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
        if (g.coarse) im.buildCoarse(k, true); // the flattened copy follows (kept as it was without memory)
        g.lightFrame = ~0u; // the lit colours follow the moved vertices
        ++lastUpdated_;
    }
    pica::stats().updatedChunks = lastUpdated_;
    pica::stats().deferredFrees = static_cast<int>(im.retired.size());
}

void TerrainRenderer::render(const TerrainViewParams& params, const TerrainRenderOptions& options) {
    Impl& im = *impl_;
    pica::TerrainPicaStats& st = pica::stats();
    st.visibleChunks = st.litChunks = st.drawCalls = st.coarseChunks = 0;
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
    // The terrain's state is issued in full, whatever the previous passes left (docs/3ds-port.md
    // "Frame synchronisation"): the shadow copies and texture bindings are forgotten first.
    pica::detail::c3dInvalidate();
    pica::detail::forgetTextureBindings();
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
    const bool useDetail = options.detail && pica::renderQuality().terrainDetail && im.detail.valid();
    pica::applyTev(useDetail ? pica::tevTerrain2Stage() : pica::tevModulateTexColor());
    if (useDetail) im.detail.bind(1);
    if (pica::stateCheck()) {
        pica::PassExpect e;
        e.pass = "terrain";
        pica::expectDepthMap(e, nearPlane, 0.0f);
        e.cull = GPU_CULL_NONE;
        pica::checkPassState(e);
    }

    const size_t lightCount = std::min(params.lightCount, static_cast<size_t>(kMaxDynamicLights));
    const int W = im.geo.width;

    // Far-chunk LOD: the level of every chunk (visible or not: a neighbour's level decides the
    // seam fill) from its box's distance to the central camera, the same for both eyes: decided by
    // the first render of a frame and kept for the second eye. Hysteresis (docs/3ds-port.md "Stereo
    // comfort and frame polish"): a fine chunk turns coarse beyond distance + band, a coarse one
    // fine again under distance - band. Of the visible chunks at most kLodSwitchesPerFrame change in
    // a frame, those farthest from the camera's forward direction first (a change at the edge of
    // the view shows least); hidden chunks change freely, and so does everything on the frame the
    // LOD comes on.
    const size_t chunkCount = im.geo.chunks.size();
    const unsigned lodNow = pica::frameArena().frames();
    if (!pica::terrainLodActive()) {
        im.coarseOf.assign(chunkCount, 0);
        im.lodFrame = ~0u;
    } else if (im.lodFrame != lodNow || im.coarseOf.size() != chunkCount) {
        const bool fresh = im.lodFrame == ~0u || im.coarseOf.size() != chunkCount;
        if (im.coarseOf.size() != chunkCount) im.coarseOf.assign(chunkCount, 0);
        im.lodFrame = lodNow;
        const Mat4& v = params.view; // rigid: the camera is -R^T t
        Vec3 cam;
        cam.x = -(v.at(0, 0) * v.at(3, 0) + v.at(0, 1) * v.at(3, 1) + v.at(0, 2) * v.at(3, 2));
        cam.y = -(v.at(1, 0) * v.at(3, 0) + v.at(1, 1) * v.at(3, 1) + v.at(1, 2) * v.at(3, 2));
        cam.z = -(v.at(2, 0) * v.at(3, 0) + v.at(2, 1) * v.at(3, 1) + v.at(2, 2) * v.at(3, 2));
        const Vec3 fwd{-v.at(0, 2), -v.at(1, 2), -v.at(2, 2)};
        const pica::RenderQuality& q = pica::renderQuality();
        const float band = fresh ? 0.0f : std::max(0.0f, std::min(q.terrainLodBand, q.terrainLodDistance));
        const float up = q.terrainLodDistance + band, down = q.terrainLodDistance - band;
        const float up2 = up * up, down2 = down * down;
        std::vector<uint8_t>& visibleMark = im.lodVisibleMark;
        visibleMark.assign(chunkCount, 0);
        for (int ki : visible) visibleMark[static_cast<size_t>(ki)] = 1;
        im.lodCandidates.clear();
        size_t switched = 0;
        for (size_t k = 0; k < chunkCount; ++k) {
            const bool can = im.chunks[k].coarse && im.coarseListsFor(im.geo.chunks[k].rows);
            const float dist2 = boxDistanceSq(im.geo.chunks[k].bounds, cam);
            uint8_t want = im.coarseOf[k];
            if (!can) want = 0;
            else if (im.coarseOf[k]) want = dist2 < down2 ? 0 : 1;
            else want = dist2 > up2 ? 1 : 0;
            if (want == im.coarseOf[k]) continue;
            if (fresh || !visibleMark[k]) {
                im.coarseOf[k] = want;
                if (!fresh) ++switched;
                continue;
            }
            // How far off the forward direction the chunk's centre is (cosine: smaller = farther).
            const Aabb& b = im.geo.chunks[k].bounds;
            const Vec3 c{0.5f * (b.min.x + b.max.x) - cam.x, 0.5f * (b.min.y + b.max.y) - cam.y,
                         0.5f * (b.min.z + b.max.z) - cam.z};
            const float len = std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z);
            const float cosA = len > 0.0f ? (c.x * fwd.x + c.y * fwd.y + c.z * fwd.z) / len : 1.0f;
            im.lodCandidates.push_back({cosA, k});
        }
        std::sort(im.lodCandidates.begin(), im.lodCandidates.end(),
                  [](const std::pair<float, size_t>& a, const std::pair<float, size_t>& b) {
                      return a.first < b.first || (a.first == b.first && a.second < b.second);
                  });
        for (size_t i = 0; i < im.lodCandidates.size() && i < kLodSwitchesPerFrame; ++i) {
            uint8_t& lvl = im.coarseOf[im.lodCandidates[i].second];
            lvl = lvl ? 0 : 1;
            ++switched;
        }
        if (switched) {
            st.lodSwitches += switched;
            ++st.lodSwitchFrames;
        }
    }

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
        int n = c.rows * W * 6;
        if (im.coarseOf[k]) {
            // The seam fill towards a fine neighbour (above: chunk k - 1, below: k + 1).
            const bool top = k > 0 && !im.coarseOf[k - 1];
            const bool bottom = k + 1 < chunkCount && !im.coarseOf[k + 1];
            const CoarseLists* l = im.coarseListsFor(c.rows);
            const int var = (top ? 1 : 0) | (bottom ? 2 : 0);
            n = l->count[var];
            sh.setTerrainVertices(g.coarse, g.dyn ? g.dyn : im.zeros);
            C3D_DrawElements(GPU_TRIANGLES, n, C3D_UNSIGNED_SHORT, l->idx[var]);
            ++st.coarseChunks;
        } else {
            sh.setTerrainVertices(g.verts, g.dyn ? g.dyn : im.zeros);
            C3D_DrawElements(GPU_TRIANGLES, n, C3D_UNSIGNED_SHORT, im.indices);
        }
        ++st.drawCalls;
        st.indicesDrawn += static_cast<size_t>(n);
    }
    st.coarseTotal += static_cast<size_t>(st.coarseChunks);
    st.visibleTotal += visible.size();
    pica::probeGpuState("terrain");

    if (options.tiles && !im.atlases.empty()) {
        // The overlays' state in full as well (blend, no depth write, the bias).
        pica::detail::c3dInvalidate();
        pica::detail::forgetTextureBindings();
        pica::setDepthMapping(FogDepthMap::WBuffer, nearPlane);
        key.blend = RenderBlend::Alpha;
        key.depthWrite = false;
        applyMaterialState(key);
        pica::applyDepthMap(pica::terrainOverlayDepthOffset());
        pica::applyTev(pica::tevModulateTexColor());
        if (pica::stateCheck()) {
            pica::PassExpect e;
            e.pass = "terrain_tiles";
            e.writeMask = GPU_WRITE_COLOR;
            pica::expectDepthMap(e, nearPlane, pica::terrainOverlayDepthOffset());
            e.src = GPU_SRC_ALPHA;
            e.dst = GPU_ONE_MINUS_SRC_ALPHA;
            e.cull = GPU_CULL_NONE;
            pica::checkPassState(e);
        }
        for (size_t bi = 0; bi < im.atlases.size(); ++bi) {
            if (!im.atlases[bi].valid()) continue;
            im.atlases[bi].bind(0);
            for (int ki : visible) {
                const ChunkGpu& g = im.chunks[static_cast<size_t>(ki)];
                if (!g.tiles[bi]) continue;
                // On a coarse chunk the quads lie on its flattened grid.
                const TerrainVertexPica* tv = im.coarseOf[static_cast<size_t>(ki)] ? g.coarseTiles[bi] : g.tiles[bi];
                sh.setTerrainVertices(tv, g.tileDyn[bi] ? g.tileDyn[bi] : im.zeros);
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
