// The water on the PICA: the first game's plane and the sequels' wave grid (water_renderer_pica.h).
#include "water_renderer_pica.h"

#include <3ds.h>
#include <citro3d.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

#include "as3d/core.h"
#include "as3d/draw_run.h"
#include "as3d/image.h"
#include "as3d/level.h"
#include "as3d/material_key.h"
#include "as3d/render_state.h"
#include "fx_draw_pica.h"
#include "eye_replay_pica.h"
#include "fx_pica.h"
#include "pica_frame.h"
#include "state_check_pica.h"
#include "tev.h"
#include "texture_budget.h"

namespace as3d::pica {

namespace {

WaterPicaStats g_stats;

constexpr float kQuadHeight = 320.0f; // world units of map length per quad (one per 8-row chunk)

// The texture name has its extension replaced by .tga; a missing file gives a magenta placeholder
// and a warning (as the GL).
Image loadWaterTexture(Vfs& vfs, const std::string& name, int* missing) {
    std::string path = name;
    const size_t dot = path.find_last_of('.');
    const size_t slash = path.find_last_of("/\\");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) path.resize(dot);
    path += ".tga";
    Blob blob;
    Image img;
    if (!vfs.read(path, blob) || !decodeTga(blob.data(), blob.size(), img)) {
        AS3D_WARN("water: missing texture '%s'", path.c_str());
        ++*missing;
        img = Image();
        img.width = img.height = 4;
        img.rgba.assign(64, 0);
        for (int i = 0; i < 16; i++) {
            img.rgba[i * 4] = 255;
            img.rgba[i * 4 + 2] = 255;
            img.rgba[i * 4 + 3] = 255;
        }
    }
    return img;
}

uint8_t byteOf(float f) { return static_cast<uint8_t>(std::min(std::max(f, 0.0f), 1.0f) * 255.0f + 0.5f); }

// Per-frame vertices (arena) of this frame.
const FlatVertex* g_arena = nullptr;
unsigned g_arenaFrame = ~0u;

// setWaterOptions (hardware A/B).
bool g_waterEnabled = true;
float g_waterTile = kDefaultWaterTile;
unsigned g_drawCount = 0; // plane draws since start (one per eye), for the periodic log line
std::vector<FlatVertex> g_tileVerts; // the tiled plane of one eye, before the copy to the arena
// The tiled plane's corner grid of one quad (draw()): clip position, outcode, NDC, vertex index.
struct GridVertex {
    Vec4 c;
    uint8_t out = 0;
    bool ndc = false;
    int index = -1;
    float ndcX = 0.0f, ndcY = 0.0f;
};
std::vector<GridVertex> g_grid;
std::vector<float> g_gridX, g_gridY;
std::vector<FlatVertex> g_gridVerts;  // the grid corners the tiles use, in first-use order
std::vector<uint16_t> g_gridIndices; // six per tile

// The tiled plane: a tile is split in four while it spans more than this in NDC (2 = the screen),
// at most kMaxTileSplits times and not below kMinTileSize world units.
constexpr float kMaxTileSpan = 0.6f;
constexpr int kMaxTileSplits = 3;
constexpr float kMinTileSize = 10.0f;

// The water's own vertex buffer (WaterPass::buf_), per half: the plane's tiles of both eyes
// (mission 1: about 100 to 600 tiles of at most 9 vertices per eye), or the grid's heights.
constexpr size_t kPlaneBufHalfBytes = 192 * 1024;

void* waterBufAlloc(size_t bytes, size_t align) { return linearMemAlign(bytes, align); }
void waterBufRelease(void* p) { deferLinearFree(p); } // a frame in flight may still read it
void waterBufFlush(const void* p, size_t bytes) { GSPGPU_FlushDataCache(p, static_cast<u32>(bytes)); }

bool initWaterBuf(LinearArena& buf, size_t halfBytes) {
    ArenaMemory mem;
    mem.alloc = waterBufAlloc;
    mem.release = waterBufRelease;
    mem.flush = waterBufFlush;
    const bool ok = buf.init(halfBytes, mem);
    if (!ok)
        AS3D_WARN("pica water: no linear memory for its vertex buffer (2 x %u KB): the frame arena instead",
                  static_cast<unsigned>(halfBytes / 1024));
    return ok;
}

} // namespace

const WaterPicaStats& waterPicaStats() { return g_stats; }

void setWaterOptions(bool enabled, float tileSize) {
    g_waterEnabled = enabled;
    g_waterTile = tileSize > 0.0f ? tileSize : 0.0f;
}

WaterPass::~WaterPass() {
    freeGrid();
    buf_.shutdown();
}

void* WaterPass::allocVertices(size_t bytes, size_t align) {
    if (buf_.valid()) {
        const uint32_t serial = frameSerial();
        if (bufFrame_ != serial) {
            // The first water draw of this frame takes the other half: its last frame must be done.
            buf_.beginFrame();
            const int h = buf_.currentHalf();
            if (!gpuDone(bufSerial_[h])) ++g_stats.bufHazards;
            bufSerial_[h] = serial;
            bufFrame_ = serial;
            g_stats.bufBytes = 0;
            if (arenaPoison()) poisonLinear(const_cast<uint8_t*>(buf_.halfBase()), buf_.capacity());
        }
        if (void* p = buf_.alloc(bytes, align)) {
            g_stats.bufHalf = buf_.currentHalf();
            g_stats.bufOffset = static_cast<size_t>(static_cast<const uint8_t*>(p) - buf_.halfBase());
            g_stats.bufBytes += bytes;
            return p;
        }
    }
    ++g_stats.bufFallbacks;
    g_stats.bufHalf = -1;
    return frameArena().alloc(bytes, align);
}

void WaterPass::freeGrid() {
    if (static_) deferLinearFree(static_);
    if (indices_) deferLinearFree(indices_);
    static_ = nullptr;
    indices_ = nullptr;
    chunks_.clear();
    src_.clear();
    geo_ = WaterGridGeometry();
    surface_ = WaterSurface();
}

void WaterPass::reset() {
    active_ = false;
    grid_ = false;
    texture_ = Texture2D();
    shine_ = Texture2D();
    freeGrid();
    quads_ = 0;
    buf_.shutdown();
    bufFrame_ = 0;
    bufSerial_[0] = bufSerial_[1] = 0;
    g_stats = WaterPicaStats();
    g_arena = nullptr;
    g_arenaFrame = ~0u;
}

bool WaterPass::build(const Terrain& terrain, Vfs& vfs, const WaterSurface* grid, const TerrainGridView& heights,
                      std::string* error) {
    reset();
    if (grid && grid->waves) return buildGrid(*grid, heights, vfs, error);
    if (!terrain.style().hasWater || terrain.style().waterTexture.empty()) return true;
    style_ = terrain.style();
    int missing = 0;
    const Image img = loadWaterTexture(vfs, style_.waterTexture, &missing);
    TextureOptions to;
    to.mipmaps = true;
    to.wrapS = Wrap::Repeat;
    to.wrapT = Wrap::Repeat;
    to.cls = TextureClass::Opaque; // RGB565: the plane's alpha is the style's, not the texture's
    texture_.create(img, to);
    width_ = static_cast<float>(terrain.width()) * kHmapCellSize;
    const float length = static_cast<float>(terrain.height()) * kHmapCellSize;
    quads_ = static_cast<int>(std::ceil(length / kQuadHeight));
    active_ = texture_.valid() && quads_ > 0;
    if (active_ && initWaterBuf(buf_, kPlaneBufHalfBytes)) g_stats.bufCapacity = buf_.capacity();
    g_stats.active = active_;
    g_stats.quads = quads_;
    g_stats.textureWidth = texture_.width();
    g_stats.textureHeight = texture_.height();
    g_stats.textureBytes = texture_.bytes();
    g_stats.missingTextures = missing;
    Placement pl = Placement::Linear;
    textureInfo(texture_, &pl, nullptr, nullptr, nullptr, nullptr);
    g_stats.textureVram = pl == Placement::Vram;
    AS3D_INFO("pica water: %d quads, texture '%s' %dx%d %.1f KB in %s", quads_, style_.waterTexture.c_str(),
              texture_.width(), texture_.height(), static_cast<double>(texture_.bytes()) / 1024.0,
              g_stats.textureVram ? "VRAM" : "linear memory");
    return true;
}

void WaterPass::prepare(float timeSeconds) {
    g_stats.quadsDrawn = g_stats.draws = 0;
    g_stats.arenaBytes = 0;
    g_arena = nullptr;
    g_arenaFrame = ~0u;
    g_stats.chunksAnimated = g_stats.verticesAnimated = 0;
    g_stats.animateMs = 0.0;
    g_stats.chunksVisible = g_stats.trianglesDrawn = 0;
    if (!active_) return;
    time_ = timeSeconds;
    if (grid_) {
        // The frame's offsets and the wave's two sines per row and per column (the operands of
        // animateWaterGrid: sin(0.75 r + T), sin(c + T) with r, c the float indices).
        frame_ = computeWaterGridFrame(timeSeconds);
        g_stats.waveTime = frame_.waveTime;
        g_stats.uvOffset = frame_.baseOffset;
        g_stats.shineOffset = frame_.shineOffset;
        for (size_t r = 0; r < rowSin_.size(); ++r)
            rowSin_[r] = std::sin(0.75f * static_cast<float>(r) + frame_.waveTime);
        for (size_t c = 0; c < colSin_.size(); ++c) colSin_[c] = std::sin(static_cast<float>(c) + frame_.waveTime);
        for (GridChunk& ch : chunks_) {
            ch.heights = nullptr;
            ch.frame = ~0u;
        }
        return;
    }
    params_ = computeWaterParams(style_, timeSeconds);
    g_stats.planeZ = params_.planeZ;
    g_stats.uvOffset = params_.texOffset;
}

int WaterPass::draw(const Mat4& view, const Mat4& projection, const Vec3& fogColour, float fogStart, float fogEnd) {
    if (!active_ || !backendReady() || !g_waterEnabled) return 0;
    if (grid_) return drawGrid(view, projection, fogColour, fogStart, fogEnd);
    LinearArena& arena = frameArena();
    const uint8_t cr = byteOf(params_.colour.x), cg = byteOf(params_.colour.y), cb = byteOf(params_.colour.z),
                  ca = byteOf(params_.alpha);
    const Vec2 off = params_.texOffset;
    // One vertex of the plane: uv = (x / 160, (y - y0) / 160) + offset, y0 the quad's first row
    // (the GL's (0,0)-(width/160, 2) per quad).
    auto put = [&](FlatVertex& o, float x, float y, float y0) {
        o.pos[0] = x;
        o.pos[1] = y;
        o.pos[2] = params_.planeZ;
        o.uv[0] = x / 160.0f + off.x;
        o.uv[1] = (y - y0) / 160.0f + off.y;
        o.rgba[0] = cr;
        o.rgba[1] = cg;
        o.rgba[2] = cb;
        o.rgba[3] = ca;
    };
    const bool tiled = g_waterTile > 0.0f && g_waterTile < kQuadHeight;
    if (!tiled && (!g_arena || g_arenaFrame != arena.frames())) {
        g_arenaFrame = arena.frames();
        const size_t n = static_cast<size_t>(quads_) * 6;
        auto* v = static_cast<FlatVertex*>(allocVertices(n * sizeof(FlatVertex), 4));
        g_arena = v;
        if (!v) return 0;
        for (int k = 0; k < quads_; ++k) {
            const float y0 = kQuadHeight * static_cast<float>(k), y1 = y0 + kQuadHeight;
            const float xs[4] = {0.0f, width_, width_, 0.0f}, ys[4] = {y0, y0, y1, y1};
            const int order[6] = {0, 1, 2, 0, 2, 3}; // a b c, a c d
            for (int i = 0; i < 6; ++i) put(v[k * 6 + i], xs[order[i]], ys[order[i]], y0);
        }
        GSPGPU_FlushDataCache(v, static_cast<u32>(n * sizeof(FlatVertex)));
        g_stats.arenaBytes = n * sizeof(FlatVertex);
    }
    if (!tiled && !g_arena) return 0;

    // The GL's conservative clip-space test of a rectangle's 4 corners against this eye's matrix.
    const Mat4 m = projection * view;
    // While this eye records for both eyes (eye_replay_pica.h), a corner is outside a y plane only
    // when it is outside in both eyes: the right eye's clip y is c.y + k1 * c.w + k2 (the x, z and w
    // of the two eyes are equal).
    float shiftK1 = 0.0f, shiftK2 = 0.0f;
    const bool bothEyes = eyeReplayRecording() && eyeReplayShift(&shiftK1, &shiftK2);
    auto yOutLow = [&](const Vec4& c) {
        return c.y < -c.w && (!bothEyes || c.y + shiftK1 * c.w + shiftK2 < -c.w);
    };
    auto yOutHigh = [&](const Vec4& c) {
        return c.y > c.w && (!bothEyes || c.y + shiftK1 * c.w + shiftK2 > c.w);
    };
    float minW = 1.0e30f, maxW = -1.0e30f;
    // Diagnostics of the emitted tiles: the largest span in NDC (2 = the whole screen) after the cut,
    // the tiles cut at the near plane, the tiles split, the smallest w emitted.
    float maxSpan = 0.0f, minCutW = 1.0e30f;
    int nearTiles = 0, splitTiles = 0;
    auto visible = [&](float xa, float xb, float ya, float yb, bool track) {
        const float xs[2] = {xa, xb}, ys[2] = {ya, yb};
        int outs[6] = {0, 0, 0, 0, 0, 0};
        float lo = 1.0e30f, hi = -1.0e30f;
        for (int i = 0; i < 4; ++i) {
            const Vec4 c = m * Vec4{xs[i & 1], ys[i >> 1], params_.planeZ, 1.0f};
            lo = std::min(lo, c.w);
            hi = std::max(hi, c.w);
            if (c.x < -c.w) outs[0]++;
            if (c.x > c.w) outs[1]++;
            if (yOutLow(c)) outs[2]++;
            if (yOutHigh(c)) outs[3]++;
            if (c.z < -c.w) outs[4]++;
            if (c.z > c.w) outs[5]++;
        }
        for (int i = 0; i < 6; ++i)
            if (outs[i] == 4) return false;
        if (track) {
            minW = std::min(minW, lo);
            maxW = std::max(maxW, hi);
        }
        return true;
    };
    std::vector<DrawRun> runs;
    const MaterialKey key = blendKey(RenderBlend::Alpha, true, true, CullMode::Off);
    const uint16_t* gridIndices = nullptr; // the tiled plane's indexed part (its grid)
    int gridIndexCount = 0;
    int drawn = 0, tiles = 0;
    const FlatVertex* verts = g_arena;
    if (!tiled) {
        for (int k = 0; k < quads_; ++k) {
            const float y0 = kQuadHeight * static_cast<float>(k);
            if (!visible(0.0f, width_, y0, y0 + kQuadHeight, true)) continue;
            DrawRun r;
            r.key = key;
            r.texture = &texture_;
            r.first = k * 6;
            r.count = 6;
            appendDrawRun(runs, r);
            ++drawn;
        }
    } else {
        // Per eye: the tiles of the visible quads, each culled with the same test, cut at the eye's
        // near plane on the CPU (a polygon with a corner behind the camera goes through the PICA's
        // clipper with the precision failure of the large quads), and split in four while one spans
        // more than kMaxTileSpan of the screen (no water triangle is ever large on screen).
        float nearPlane = 4.0f, farPlane = 2000.0f;
        detail::planesOfProjection(projection.m, &nearPlane, &farPlane);
        const float wCut = nearPlane * 1.01f + 0.01f;
        std::vector<FlatVertex>& out = g_tileVerts;
        out.clear();
        struct Pt {
            float x, y;
            Vec4 c;
        };
        auto clipOf = [&](float x, float y) { return m * Vec4{x, y, params_.planeZ, 1.0f}; };
        std::function<void(float, float, float, float, float, int)> tile = [&](float x0, float x1, float y0, float y1,
                                                                               float qy, int depth) {
            const Pt corners[4] = {{x0, y0, clipOf(x0, y0)}, {x1, y0, clipOf(x1, y0)}, {x1, y1, clipOf(x1, y1)},
                                   {x0, y1, clipOf(x0, y1)}};
            int outs[6] = {0, 0, 0, 0, 0, 0};
            float hi = -1.0e30f, lo = 1.0e30f;
            for (const Pt& p : corners) {
                const Vec4& c = p.c;
                hi = std::max(hi, c.w);
                lo = std::min(lo, c.w);
                if (c.x < -c.w) outs[0]++;
                if (c.x > c.w) outs[1]++;
                if (yOutLow(c)) outs[2]++;
                if (yOutHigh(c)) outs[3]++;
                if (c.z < -c.w) outs[4]++;
                if (c.z > c.w) outs[5]++;
            }
            for (int i = 0; i < 6; ++i)
                if (outs[i] == 4) return;
            if (hi <= wCut) return; // wholly behind the near plane
            // Sutherland-Hodgman against w >= wCut (w is affine in the plane's x, y).
            Pt poly[5];
            int n = 0;
            for (int i = 0; i < 4; ++i) {
                const Pt& a = corners[i];
                const Pt& b = corners[(i + 1) % 4];
                const bool ain = a.c.w >= wCut, bin = b.c.w >= wCut;
                if (ain) poly[n++] = a;
                if (ain != bin) {
                    const float t = (wCut - a.c.w) / (b.c.w - a.c.w);
                    const float x = a.x + (b.x - a.x) * t, y = a.y + (b.y - a.y) * t;
                    poly[n++] = {x, y, clipOf(x, y)};
                }
            }
            if (n < 3) return;
            float nx0 = 1.0e30f, nx1 = -1.0e30f, ny0 = 1.0e30f, ny1 = -1.0e30f;
            for (int i = 0; i < n; ++i) {
                const Vec4& c = poly[i].c;
                nx0 = std::min(nx0, c.x / c.w);
                nx1 = std::max(nx1, c.x / c.w);
                ny0 = std::min(ny0, c.y / c.w);
                ny1 = std::max(ny1, c.y / c.w);
            }
            const float span = std::max(nx1 - nx0, ny1 - ny0);
            if (span > kMaxTileSpan && depth < kMaxTileSplits && x1 - x0 > kMinTileSize) {
                const float xm = 0.5f * (x0 + x1), ym = 0.5f * (y0 + y1);
                tile(x0, xm, y0, ym, qy, depth + 1);
                tile(xm, x1, y0, ym, qy, depth + 1);
                tile(x0, xm, ym, y1, qy, depth + 1);
                tile(xm, x1, ym, y1, qy, depth + 1);
                ++splitTiles;
                return;
            }
            if (lo < wCut) ++nearTiles;
            maxSpan = std::max(maxSpan, span);
            for (int i = 0; i < n; ++i) minCutW = std::min(minCutW, poly[i].c.w);
            for (int i = 1; i + 1 < n; ++i) {
                const Pt* tri[3] = {&poly[0], &poly[i], &poly[i + 1]};
                for (const Pt* p : tri) {
                    out.emplace_back();
                    put(out.back(), p->x, p->y, qy);
                }
            }
            ++tiles;
        };
        const int nx = std::max(1, static_cast<int>(std::ceil(width_ / g_waterTile)));
        const int ny = std::max(1, static_cast<int>(std::ceil(kQuadHeight / g_waterTile)));
        const float tx = width_ / static_cast<float>(nx), ty = kQuadHeight / static_cast<float>(ny);
        // The tiles as an indexed grid (round 3, R3.2: the per-tile path cost the console 1.2 ms
        // of CPU per eye for ~230 tiles of 6 vertices): each corner of a visible quad's tile grid is
        // transformed once (clip position, outcode), its NDC and its vertex made once when a tile
        // uses them, and a tile adds six 16-bit indices. The corner values are the per-tile path's
        // bit for bit (the same expressions on the same coordinates) and the triangles the same (a b
        // c, a c d per tile, in tile order), so the picture is the same; tiles cut at the near plane
        // or split still go through the per-tile path (`tile`, non-indexed, drawn after the indexed
        // part: the tiles never overlap). A grid whose shared corners would not
        // equal the per-tile corners exactly (x0 + tx != tx * (i + 1) in floats) takes the
        // per-tile path throughout.
        std::vector<FlatVertex>& gridVerts = g_gridVerts;
        std::vector<uint16_t>& indices = g_gridIndices;
        gridVerts.clear();
        indices.clear();
        g_gridX.resize(static_cast<size_t>(nx) + 1);
        for (int i = 0; i <= nx; ++i) g_gridX[i] = i == nx ? width_ : tx * static_cast<float>(i);
        bool exactX = true;
        for (int i = 0; i + 1 < nx; ++i) exactX = exactX && g_gridX[i] + tx == g_gridX[i + 1];
        g_gridY.resize(static_cast<size_t>(ny) + 1);
        g_grid.resize(static_cast<size_t>(nx + 1) * static_cast<size_t>(ny + 1));
        for (int k = 0; k < quads_; ++k) {
            const float qy = kQuadHeight * static_cast<float>(k);
            if (!visible(0.0f, width_, qy, qy + kQuadHeight, true)) continue;
            ++drawn;
            for (int j = 0; j <= ny; ++j) g_gridY[j] = j == ny ? qy + kQuadHeight : qy + ty * static_cast<float>(j);
            bool exact = exactX && gridVerts.size() + static_cast<size_t>((nx + 1) * (ny + 1)) < 65536u;
            for (int j = 0; j + 1 < ny; ++j) exact = exact && g_gridY[j] + ty == g_gridY[j + 1];
            if (!exact) {
                for (int j = 0; j < ny; ++j)
                    for (int i = 0; i < nx; ++i) {
                        const float x0 = tx * static_cast<float>(i), y0 = qy + ty * static_cast<float>(j);
                        // The last tiles end exactly on the quad's edges.
                        const float x1 = i + 1 == nx ? width_ : x0 + tx;
                        const float y1 = j + 1 == ny ? qy + kQuadHeight : y0 + ty;
                        tile(x0, x1, y0, y1, qy, 0);
                    }
                continue;
            }
            GridVertex* g = g_grid.data();
            for (int j = 0; j <= ny; ++j)
                for (int i = 0; i <= nx; ++i, ++g) {
                    g->c = clipOf(g_gridX[i], g_gridY[j]);
                    const Vec4& c = g->c;
                    g->out = static_cast<uint8_t>((c.x < -c.w ? 1 : 0) | (c.x > c.w ? 2 : 0) | (yOutLow(c) ? 4 : 0) |
                                                  (yOutHigh(c) ? 8 : 0) | (c.z < -c.w ? 16 : 0) | (c.z > c.w ? 32 : 0));
                    g->ndc = false;
                    g->index = -1;
                }
            const int stride = nx + 1;
            for (int j = 0; j < ny; ++j) {
                GridVertex* row0 = &g_grid[static_cast<size_t>(j) * stride];
                GridVertex* row1 = row0 + stride;
                for (int i = 0; i < nx; ++i) {
                    GridVertex* q[4] = {&row0[i], &row0[i + 1], &row1[i + 1], &row1[i]};
                    if ((q[0]->out & q[1]->out & q[2]->out & q[3]->out) != 0) continue; // outside one plane
                    float hi = q[0]->c.w, lo = q[0]->c.w;
                    for (int n = 1; n < 4; ++n) {
                        hi = std::max(hi, q[n]->c.w);
                        lo = std::min(lo, q[n]->c.w);
                    }
                    if (hi <= wCut) continue; // wholly behind the near plane
                    const float x0 = g_gridX[i], x1 = g_gridX[i + 1], y0 = g_gridY[j], y1 = g_gridY[j + 1];
                    if (lo < wCut) { // cut at the near plane: the per-tile path
                        tile(x0, x1, y0, y1, qy, 0);
                        continue;
                    }
                    float nx0 = 1.0e30f, nx1 = -1.0e30f, ny0 = 1.0e30f, ny1 = -1.0e30f;
                    for (GridVertex* v : q) {
                        if (!v->ndc) {
                            v->ndcX = v->c.x / v->c.w;
                            v->ndcY = v->c.y / v->c.w;
                            v->ndc = true;
                        }
                        nx0 = std::min(nx0, v->ndcX);
                        nx1 = std::max(nx1, v->ndcX);
                        ny0 = std::min(ny0, v->ndcY);
                        ny1 = std::max(ny1, v->ndcY);
                    }
                    const float span = std::max(nx1 - nx0, ny1 - ny0);
                    if (span > kMaxTileSpan && 0 < kMaxTileSplits && x1 - x0 > kMinTileSize) { // split: the per-tile path
                        tile(x0, x1, y0, y1, qy, 0);
                        continue;
                    }
                    maxSpan = std::max(maxSpan, span);
                    static const int kCol[4] = {0, 1, 1, 0}, kRow[4] = {0, 0, 1, 1};
                    uint16_t id[4];
                    for (int n = 0; n < 4; ++n) {
                        GridVertex* v = q[n];
                        if (v->index < 0) {
                            v->index = static_cast<int>(gridVerts.size());
                            gridVerts.emplace_back();
                            put(gridVerts.back(), g_gridX[i + kCol[n]], g_gridY[j + kRow[n]], qy);
                        }
                        minCutW = std::min(minCutW, v->c.w);
                        id[n] = static_cast<uint16_t>(v->index);
                    }
                    indices.push_back(id[0]);
                    indices.push_back(id[1]);
                    indices.push_back(id[2]);
                    indices.push_back(id[0]);
                    indices.push_back(id[2]);
                    indices.push_back(id[3]);
                    ++tiles;
                }
            }
        }
        // One block of the water buffer: the grid's vertices, the per-tile path's after them, then
        // the indices.
        const size_t vertexCount = gridVerts.size() + out.size();
        if (vertexCount > 0) {
            const size_t vbytes = vertexCount * sizeof(FlatVertex);
            const size_t ibytes = indices.size() * sizeof(uint16_t);
            auto* block = static_cast<uint8_t*>(allocVertices(vbytes + ibytes, 4));
            if (!block) return 0;
            auto* v = reinterpret_cast<FlatVertex*>(block);
            std::copy(gridVerts.begin(), gridVerts.end(), v);
            std::copy(out.begin(), out.end(), v + gridVerts.size());
            auto* idx = reinterpret_cast<uint16_t*>(block + vbytes);
            std::copy(indices.begin(), indices.end(), idx);
            GSPGPU_FlushDataCache(block, static_cast<u32>(vbytes + ibytes));
            g_stats.arenaBytes += vbytes + ibytes;
            if (!out.empty()) {
                DrawRun r;
                r.key = key;
                r.texture = &texture_;
                r.first = static_cast<int>(gridVerts.size());
                r.count = static_cast<int>(out.size());
                appendDrawRun(runs, r);
            }
            gridIndices = indices.empty() ? nullptr : idx;
            gridIndexCount = static_cast<int>(indices.size());
            verts = v;
        }
    }
    g_stats.quadsDrawn = drawn;
    g_stats.tilesDrawn = tiles;
    g_stats.minW = drawn ? minW : 0.0f;
    g_stats.maxW = drawn ? maxW : 0.0f;
    g_stats.nearTiles = nearTiles;
    g_stats.splitTiles = splitTiles;
    g_stats.maxSpan = maxSpan;
    g_stats.minCutW = tiles ? minCutW : 0.0f;
    if (++g_drawCount % 300 == 1)
        AS3D_INFO("pica water: draw %u planeZ %.2f quads %d of %d, tiles %d (tile %.0f, %d split), quad corner w "
                  "%.1f..%.1f, tiles cut at the near plane %d, smallest w drawn %.1f, largest tile span %.2f NDC",
                  g_drawCount, static_cast<double>(params_.planeZ), drawn, quads_, tiles,
                  static_cast<double>(g_waterTile), splitTiles, static_cast<double>(g_stats.minW),
                  static_cast<double>(g_stats.maxW), nearTiles, static_cast<double>(tiles ? minCutW : 0.0f),
                  static_cast<double>(maxSpan));
    if (runs.empty() && gridIndexCount == 0) return 0;
    // The water's state is issued in full right before its draw, whatever the previous passes
    // left (docs/3ds-port.md "Frame synchronisation"): the shadow copies and the texture bindings
    // are forgotten, so drawFlatRuns' depth map, depth test, blend, cull, fog and texture calls all
    // reach citro3d and its draw emits them.
    detail::c3dInvalidate();
    detail::forgetTextureBindings();
    FxEyeParams eye;
    eye.view = view;
    eye.projection = projection;
    eye.fogColour = fogColour;
    eye.fogStart = fogStart;
    eye.fogEnd = fogEnd;
    PassExpect expect;
    if (stateCheck()) {
        float nearPlane = 4.0f, farPlane = 2000.0f;
        detail::planesOfProjection(projection.m, &nearPlane, &farPlane);
        expect.pass = "water";
        expectDepthMap(expect, nearPlane, 0.0f);
        expect.src = GPU_SRC_ALPHA;
        expect.dst = GPU_ONE_MINUS_SRC_ALPHA;
        expect.cull = GPU_CULL_NONE;
        expect.tex0 = c3dTexOf(texture_);
        eye.check = &expect;
        eye.probe = "water";
    }
    DrawRun gridStyle;
    gridStyle.key = key;
    gridStyle.texture = &texture_;
    g_stats.draws = drawFlatRunsIndexed(verts, gridIndices, gridIndexCount, gridStyle, runs, eye);
    MaterialKey plain;
    applyMaterialState(plain);
    return g_stats.draws;
}

// ---------------------------------------------------------------------------------------
// The sequels' wave grid.
// ---------------------------------------------------------------------------------------

bool WaterPass::buildGrid(const WaterSurface& surface, const TerrainGridView& heights, Vfs& vfs, std::string* error) {
    // As the GL grid build: nothing for an absent surface or a grid that does not match.
    if (!surface.present || surface.baseTexture.empty() || !heights.valid() || surface.width != heights.width ||
        surface.height != heights.height)
        return true;
    surface_ = surface;
    geo_ = buildWaterGridGeometry(surface_, heights);
    if (geo_.verts.size() > 65535) {
        if (error) *error = "water grid too large for 16-bit indices";
        return false;
    }
    int missing = 0;
    TextureOptions to;
    to.mipmaps = true;
    to.wrapS = Wrap::Repeat;
    to.wrapT = Wrap::Repeat;
    // The base layer's alpha chooses between the layers: RGBA8 when it varies (RGBA4 would band
    // the colour and the mix), RGB565 when it is opaque. The shine layer's alpha is not used.
    const Image base = loadWaterTexture(vfs, surface_.baseTexture, &missing);
    const bool baseOpaque = chooseFormat(base, TextureClass::Auto) == PicaFormat::RGB565;
    to.cls = baseOpaque ? TextureClass::Opaque : TextureClass::TrueColor;
    texture_.create(base, to);
    to.cls = TextureClass::Opaque;
    if (!surface_.shineTexture.empty()) {
        shine_.create(loadWaterTexture(vfs, surface_.shineTexture, &missing), to);
    } else {
        Image clear; // no shine layer: black under the base (the GL's 1x1; the PICA's smallest is 8x8)
        clear.width = clear.height = 8;
        clear.rgba.assign(8 * 8 * 4, 0);
        for (size_t i = 3; i < clear.rgba.size(); i += 4) clear.rgba[i] = 255;
        shine_.create(clear, to);
    }

    // Per chunk: the vertices its wet cells use, compacted (each chunk's range starts on a
    // multiple of 4 vertices: 48-byte aligned), and its indices into that range.
    const size_t nv = geo_.verts.size();
    std::vector<int> local(nv, -1);
    std::vector<uint16_t> idx;
    idx.reserve(geo_.indices.size());
    chunks_.clear();
    src_.clear();
    for (const WaterGridChunk& gc : geo_.chunks) {
        GridChunk ch;
        while (src_.size() % 4) src_.push_back(src_.back()); // padding, never indexed
        ch.firstUsed = src_.size();
        ch.firstIndex = idx.size();
        ch.originY = static_cast<float>(gc.rowStart) * kHmapCellSize;
        for (size_t i = 0; i < gc.indexCount; ++i) {
            const uint32_t g = geo_.indices[gc.firstIndex + i];
            if (local[g] < 0) {
                local[g] = static_cast<int>(src_.size() - ch.firstUsed);
                src_.push_back(g);
            }
            idx.push_back(static_cast<uint16_t>(local[g]));
        }
        ch.usedCount = src_.size() - ch.firstUsed;
        ch.indexCount = idx.size() - ch.firstIndex;
        for (size_t k = ch.firstUsed; k < src_.size(); ++k) local[src_[k]] = -1;
        chunks_.push_back(ch);
    }
    if (!src_.empty()) {
        static_ = static_cast<WaterGridStaticPica*>(linearAlloc(src_.size() * sizeof(WaterGridStaticPica)));
        indices_ = static_cast<uint16_t*>(linearAlloc(std::max<size_t>(idx.size(), 1) * sizeof(uint16_t)));
        if (!static_ || !indices_) {
            freeGrid();
            if (error) *error = "pica water: no linear memory for the wave grid";
            return false;
        }
        for (size_t k = 0; k < src_.size(); ++k) {
            const WaterGridVertex& v = geo_.verts[src_[k]];
            WaterGridStaticPica& o = static_[k];
            o.xy[0] = v.col * kHmapCellSize;
            o.xy[1] = v.row * kHmapCellSize;
            o.rgba[0] = o.rgba[1] = o.rgba[2] = 255;
            o.rgba[3] = byteOf(v.weight * surface_.opacity);
        }
        std::copy(idx.begin(), idx.end(), indices_);
        GSPGPU_FlushDataCache(static_, static_cast<u32>(src_.size() * sizeof(WaterGridStaticPica)));
        GSPGPU_FlushDataCache(indices_, static_cast<u32>(std::max<size_t>(idx.size(), 1) * sizeof(uint16_t)));
    }
    // The grid's heights of one frame: every used vertex once (each chunk 4-byte aligned).
    if (!src_.empty()) {
        initWaterBuf(buf_, src_.size() * sizeof(float) + chunks_.size() * 16 + 256);
        g_stats.bufCapacity = buf_.capacity();
    }
    rowSin_.assign(static_cast<size_t>(surface_.height) + 1, 0.0f);
    colSin_.assign(static_cast<size_t>(surface_.width) + 1, 0.0f);

    grid_ = true;
    active_ = geo_.wetCells > 0 && texture_.valid() && shine_.valid() && static_ && indices_;
    g_stats.active = active_;
    g_stats.grid = true;
    g_stats.gridWidth = surface_.width;
    g_stats.gridHeight = surface_.height;
    g_stats.gridChunks = static_cast<int>(geo_.chunks.size());
    g_stats.wetCells = geo_.wetCells;
    g_stats.gridVertices = static_cast<int>(nv);
    g_stats.usedVertices = static_cast<int>(src_.size());
    g_stats.indices = static_cast<int>(idx.size());
    g_stats.staticBytes = src_.size() * sizeof(WaterGridStaticPica);
    g_stats.indexBytes = idx.size() * sizeof(uint16_t);
    g_stats.textureWidth = texture_.width();
    g_stats.textureHeight = texture_.height();
    g_stats.textureBytes = texture_.bytes();
    g_stats.shineWidth = shine_.width();
    g_stats.shineHeight = shine_.height();
    g_stats.shineBytes = shine_.bytes();
    g_stats.baseTrueColor = !baseOpaque;
    g_stats.missingTextures = missing;
    Placement pl = Placement::Linear;
    textureInfo(texture_, &pl, nullptr, nullptr, nullptr, nullptr);
    g_stats.textureVram = pl == Placement::Vram;
    pl = Placement::Linear;
    textureInfo(shine_, &pl, nullptr, nullptr, nullptr, nullptr);
    g_stats.shineVram = pl == Placement::Vram;
    AS3D_INFO("pica water: wave grid %dx%d, %d chunks, %d wet cells, %d of %d grid vertices used (%.1f KB static, "
              "%d indices %.1f KB); base '%s' %dx%d %s %.1f KB in %s, shine '%s' %dx%d %.1f KB in %s",
              surface_.width, surface_.height, g_stats.gridChunks, geo_.wetCells, g_stats.usedVertices,
              g_stats.gridVertices, static_cast<double>(g_stats.staticBytes) / 1024.0, g_stats.indices,
              static_cast<double>(g_stats.indexBytes) / 1024.0, surface_.baseTexture.c_str(), texture_.width(),
              texture_.height(), baseOpaque ? "RGB565" : "RGBA8", static_cast<double>(texture_.bytes()) / 1024.0,
              g_stats.textureVram ? "VRAM" : "linear memory", surface_.shineTexture.c_str(), shine_.width(),
              shine_.height(), static_cast<double>(shine_.bytes()) / 1024.0,
              g_stats.shineVram ? "VRAM" : "linear memory");
    return true;
}

void WaterPass::update(const VertexRect* rects, size_t count, const TerrainGridView& heights) {
    // The heights of the non-animated vertices live in geo_.verts (the next animate() reads them);
    // the static stream (x, y, alpha) does not change: the weights and wet cells stay.
    if (grid_ && count > 0) updateWaterGridHeights(geo_, surface_, rects, count, heights);
}

const float* WaterPass::animate(GridChunk& ch) {
    LinearArena& arena = frameArena();
    if (ch.heights && ch.frame == arena.frames()) return ch.heights;
    const u64 t0 = svcGetSystemTick();
    float* z = static_cast<float*>(allocVertices(std::max<size_t>(ch.usedCount, 1) * sizeof(float), 4));
    ch.frame = arena.frames();
    ch.heights = z;
    if (!z) {
        ++g_stats.arenaFailures;
        return nullptr;
    }
    // animateWaterGrid's height with the tabulated sines (the same float operations).
    const float level = surface_.level, lastRow = static_cast<float>(surface_.height) - 0.5f;
    const uint32_t* src = src_.data() + ch.firstUsed;
    for (size_t i = 0; i < ch.usedCount; ++i) {
        const WaterGridVertex& v = geo_.verts[src[i]];
        float h = v.terrainZ;
        if (v.weight >= kWaterMinWeight && v.row > 0.5f && v.row < lastRow)
            h = level + kWaterWaveAmplitude * v.weight * 0.5f *
                            (rowSin_[static_cast<size_t>(v.row)] + colSin_[static_cast<size_t>(v.col)]);
        z[i] = h;
    }
    // Self-check against the neutral CPU reference on the chunk's first vertices.
    float err = 0.0f;
    for (size_t i = 0; i < std::min<size_t>(ch.usedCount, 16); ++i) {
        const WaterGridAnimated a = animateWaterGrid(surface_, geo_.verts[src[i]], frame_);
        err = std::max(err, std::fabs(a.pos.z - z[i]));
    }
    GSPGPU_FlushDataCache(z, static_cast<u32>(std::max<size_t>(ch.usedCount, 1) * sizeof(float)));
    g_stats.maxHeightError = std::max(g_stats.maxHeightError, err);
    g_stats.arenaBytes += ch.usedCount * sizeof(float);
    ++g_stats.chunksAnimated;
    g_stats.verticesAnimated += static_cast<int>(ch.usedCount);
    g_stats.animateMs += static_cast<double>(svcGetSystemTick() - t0) / (SYSCLOCK_ARM11 / 1000.0);
    return z;
}

int WaterPass::drawGrid(const Mat4& view, const Mat4& projection, const Vec3& fogColour, float fogStart, float fogEnd) {
    const Mat4 m = projection * view;
    const std::vector<int> visible = visibleWaterChunks(geo_, m);
    g_stats.chunksVisible = 0;
    g_stats.trianglesDrawn = 0;
    g_stats.draws = 0;
    g_stats.quadsDrawn = 0;
    if (visible.empty()) return 0;

    // The GL grid state: alpha blend, depth test and write on, no culling, the level fog; issued
    // in full whatever the previous passes left (as the plane above).
    detail::c3dInvalidate();
    detail::forgetTextureBindings();
    float nearPlane, farPlane;
    detail::planesOfProjection(projection.m, &nearPlane, &farPlane);
    setDepthMapping(FogDepthMap::WBuffer, nearPlane);
    if (fogStart < 1.0e7f && fogEnd > fogStart) setFog(nearPlane, farPlane, fogStart, fogEnd);
    else disableFog();
    setLevelFogColor(fogColour);
    ShaderSet& sh = shaders();
    sh.bind(Shader::Water);
    sh.setMvp(m);
    applyMaterialState(blendKey(RenderBlend::Alpha, true, true, CullMode::Off));
    applyTev(tevWaterGrid());
    texture_.bind(0);
    shine_.bind(1);
    if (stateCheck()) {
        PassExpect expect;
        expect.pass = "water_grid";
        expectDepthMap(expect, nearPlane, 0.0f);
        expect.src = GPU_SRC_ALPHA;
        expect.dst = GPU_ONE_MINUS_SRC_ALPHA;
        expect.cull = GPU_CULL_NONE;
        expect.tex0 = c3dTexOf(texture_);
        expect.tex1 = c3dTexOf(shine_);
        checkPassState(expect);
    }

    // Texture coordinates per world unit: the GL's 1.5 * (c / 4) and 2 * (c / 4) with c = x / 40.
    const float sBase = 1.5f * 0.25f / kHmapCellSize, sShine = 2.0f * 0.25f / kHmapCellSize;
    int draws = 0;
    for (int k : visible) {
        GridChunk& ch = chunks_[static_cast<size_t>(k)];
        if (ch.indexCount == 0) continue;
        const float* z = animate(ch);
        if (!z) continue;
        // On the chunk's first row: uv = (y - y0) * s + (offset + y0 * s), the whole repeats of
        // the second term dropped (the textures wrap).
        const float vb = frame_.baseOffset.y + ch.originY * sBase, vs = frame_.shineOffset.y + ch.originY * sShine;
        const float base[4] = {sBase, sBase, frame_.baseOffset.x, vb - std::floor(vb)};
        const float shine[4] = {sShine, sShine, frame_.shineOffset.x, vs - std::floor(vs)};
        sh.setWaterUniforms(0.0f, -ch.originY, base, shine);
        sh.setWaterVertices(static_ + ch.firstUsed, z);
        C3D_DrawElements(GPU_TRIANGLES, static_cast<int>(ch.indexCount), C3D_UNSIGNED_SHORT, indices_ + ch.firstIndex);
        ++draws;
        ++g_stats.chunksVisible;
        g_stats.trianglesDrawn += static_cast<int>(ch.indexCount / 3);
    }
    if (draws > 0) probeGpuState("water_grid");
    g_stats.draws = draws;
    g_stats.quadsDrawn = g_stats.chunksVisible;
    MaterialKey plain;
    applyMaterialState(plain);
    return draws;
}

} // namespace as3d::pica
