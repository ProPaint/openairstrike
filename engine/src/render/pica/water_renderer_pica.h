// The water on the PICA (3DS only): the first game's flat scrolling plane and the sequels' wave grid
// of the GL WaterRenderer (engine/src/render/gles/water_renderer.cpp, docs/spec/hmap.md "Water",
// docs/spec/as2/render-pipeline.delta.md 12.5). docs/3ds-port.md "M3.5" and "M5.1".
//
// The plane is one quad per 320 world units of map length (x 0..width*40), texture coordinates
// (0,0)-(width/160, 2) per quad, at z = planeZ(time) = level + sin(t), with the uv offset
// (0.4 sin t, 0.4 sin t/2) and the colour sun*sun_dir.z + ambient (computeWaterParams), alpha =
// the style's water alpha, alpha blend, depth test and write on, no culling, fog towards the
// level colour. The GL shader adds the plane z and the uv offset per frame as uniforms; the PICA
// flat shader has neither, so the few quads (about 13 in mission 1: 78 vertices, 1.9 KB) are
// written into the frame arena by the first eye of an arena frame with z and uv already moved,
// the colour and alpha in the vertex colour (TEV: texture x vertex colour; the texture is opaque
// RGB565, so the alpha is the style's, as the GL's which ignores the texture alpha). Each eye
// culls the quads against its own matrix as the GL does (conservative clip-space test of the 4
// corners) and draws the visible ones, adjacent quads as one draw call. On the console the quads'
// large triangles lose depth precision (the plane showed through the terrain), so by default each
// visible quad is cut into tiles of about kDefaultWaterTile units, each culled the same way and
// written to the arena per eye (docs/3ds-port.md "Water on hardware").
//
// The texture is created at beginLevel (after the terrain's, so it goes to VRAM while the 4.2 MB
// budget has room, else linear memory: waterPicaStats().textureVram).
//
// The sequels' wave grid (WaterStyle::WaveGrid, the GL grid path of the same file, kWaterGridVertexSrc,
// as3d/water_build.h): the grid on the terrain vertices of the wet cells, 8-row chunks, depth-faded
// alpha, the vertex wave, two scrolled layers (base over shine by the base alpha), unlit, alpha
// blend, depth test and write on, no culling, fog towards the level colour, one draw per visible
// chunk (visibleWaterChunks with the eye's matrix, as the GL). The PICA has no sine in its vertex
// shader, so the wave is computed on the CPU (the operations of animateWaterGrid, with the frame's
// sin(0.75 r + T) and sin(c + T) tabulated per row and column) and only the height goes to the GPU:
// shaders/water.v.pica reads a static stream built at beginLevel (x, y and the alpha byte of each
// vertex a wet cell uses, compacted per chunk: WaterGridStaticPica, 12 bytes) and one float per
// vertex per frame from the frame arena (written by the first eye that sees the chunk, reused by
// the other). The texture coordinates are affine in x and y: the shader computes them from the
// position with the frame's offsets as uniforms (per chunk, rebased on the chunk's first row so
// they stay small). TEV tevWaterGrid.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "as3d/math.h"
#include "as3d/terrain.h"
#include "as3d/texture.h"
#include "as3d/vfs.h"
#include "as3d/water_build.h"
#include "shader_set.h"

namespace as3d::pica {

struct WaterPicaStats {
    bool active = false;
    bool grid = false;          // the sequels' wave grid (the fields below `grid*`)
    int quads = 0;              // the plane's quads (all)
    int textureWidth = 0, textureHeight = 0;
    size_t textureBytes = 0;
    bool textureVram = false;
    int missingTextures = 0;
    // The last prepared frame.
    float planeZ = 0.0f;
    Vec2 uvOffset;              // (0.4 sin t, 0.4 sin t/2), the scroll
    // The last draw() (one eye).
    int quadsDrawn = 0;
    int tilesDrawn = 0;         // with setWaterOptions' tiles: the tiles of the drawn quads kept
    float minW = 0.0f, maxW = 0.0f; // clip w over the corners of the drawn quads (the eye's matrix)
    int draws = 0;
    size_t arenaBytes = 0;      // vertex bytes written to the frame arena this frame
    // Wave grid: build (beginLevel).
    int gridWidth = 0, gridHeight = 0; // cells W x H
    int gridChunks = 0;         // 8-row chunks
    int wetCells = 0;
    int gridVertices = 0;       // the GL's grid vertices ((rows + 1) x (W + 1) per chunk)
    int usedVertices = 0;       // the vertices a wet cell uses (the static stream)
    int indices = 0;
    size_t staticBytes = 0, indexBytes = 0; // linear memory, kept for the level
    int shineWidth = 0, shineHeight = 0;
    size_t shineBytes = 0;
    bool shineVram = false;
    bool baseTrueColor = false; // the base layer as RGBA8 (its alpha varies)
    // Wave grid, the last frame (both eyes): chunks animated, their vertices, the time.
    int chunksAnimated = 0;
    int verticesAnimated = 0;
    double animateMs = 0.0;
    float waveTime = 0.0f;
    Vec2 shineOffset;
    // Wave grid, the last draw() (one eye).
    int chunksVisible = 0;
    int trianglesDrawn = 0;
    // Wave grid since build(): max |z - animateWaterGrid| over the first 16 vertices of every
    // animated chunk (self-check), and arena failures.
    float maxHeightError = 0.0f;
    int arenaFailures = 0;
};
const WaterPicaStats& waterPicaStats();

// Hardware A/B of the first game's plane (run.ini water=, water_tile=; outside frames):
// `enabled` false draws no water at all (both water styles); `tileSize` > 0 splits each 320-unit
// quad into tiles of about that size (world units), each culled on its own against the eye, the
// texture mapping unchanged; 0 keeps the GL's quads.
void setWaterOptions(bool enabled, float tileSize);
constexpr float kDefaultWaterTile = 80.0f;

class WaterPass {
public:
    // Level start (outside frames). `grid` null: the first game's plane; else the sequels' wave
    // grid over `heights` (the terrain's grid; the GL WaterRenderer's grid build). Returns false
    // only on a hard error.
    bool build(const Terrain& terrain, Vfs& vfs, const WaterSurface* grid, const TerrainGridView& heights,
               std::string* error);
    void reset();
    bool active() const { return active_; }
    bool grid() const { return grid_; }
    // Craters (outside frames): the grid's heights follow the terrain (the GL update()).
    void update(const VertexRect* rects, size_t count, const TerrainGridView& heights);
    // Per frame, outside frames: the plane's or the grid's values at world time `timeSeconds`.
    void prepare(float timeSeconds);
    // One eye; viewProj = projection * view as the other passes. Returns the draw calls; leaves
    // blend off, depth test + write on, cull back.
    int draw(const Mat4& view, const Mat4& projection, const Vec3& fogColour, float fogStart, float fogEnd);

    WaterPass() = default;
    ~WaterPass();
    WaterPass(const WaterPass&) = delete;
    WaterPass& operator=(const WaterPass&) = delete;

private:
    bool buildGrid(const WaterSurface& surface, const TerrainGridView& heights, Vfs& vfs, std::string* error);
    struct GridChunk {
        size_t firstUsed = 0, usedCount = 0; // in the static stream / src_
        size_t firstIndex = 0, indexCount = 0;
        float originY = 0.0f;                // world y of the chunk's first row
        const float* heights = nullptr;      // this arena frame's, once animated
        unsigned frame = ~0u;
    };
    int drawGrid(const Mat4& view, const Mat4& projection, const Vec3& fogColour, float fogStart, float fogEnd);
    const float* animate(GridChunk& ch);
    void freeGrid();

    bool active_ = false;
    bool grid_ = false;
    Texture2D texture_;
    Texture2D shine_;
    WaterSurface surface_;
    WaterGridGeometry geo_;
    std::vector<GridChunk> chunks_;
    std::vector<uint32_t> src_;            // per static vertex: its index in geo_.verts
    WaterGridStaticPica* static_ = nullptr; // linear memory
    uint16_t* indices_ = nullptr;           // linear memory, per chunk local to its static range
    WaterGridFrame frame_;
    std::vector<float> rowSin_, colSin_;
    int quads_ = 0;
    float width_ = 0.0f;
    float level_ = 0.0f;
    TerrainStyle style_;
    WaterParams params_;
    float time_ = 0.0f;
};

} // namespace as3d::pica
