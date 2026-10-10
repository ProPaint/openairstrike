// The PICA side of the silhouette shadows (3DS only; the public API is as3d/shadow_render.h,
// its PICA body shadow_renderer_pica.cpp). docs/3ds-port.md "M3.1".
//
// Generation. The PICA cannot render to a texture and read it back cheaply, so
// ShadowRenderer::generate() runs the CPU rasteriser of as3d/shadow_raster.h
// (rasterizeShadowSilhouette, within IoU 0.97 of the GL FBO path) and uploads the alpha as an A8
// texture (TextureClass::Shadow), in linear memory (VRAM is full with the terrain, UI and skins;
// a shadow covers a small part of the screen), clamped, with the GL side's mipmaps. A side above
// kMaxShadowSide is rasterised at kMaxShadowSide (the same bounds, fewer texels): at 400x240 a 128-texel shadow spans about 150 px at the
// camera's distance in mission 1 (1.16 px per world unit at depth 179), so the cap only halves
// the largest projected shadows (256 world units, nearly the screen width) to about 2.3 px per
// texel, which the bilinear filter and the soft edges hide. Textures are created outside frames:
// generate() must not be called between C3D_FrameBegin and C3D_FrameEnd.
//
// Skins. generate() gets the skin as a Texture2D, which the PICA cannot read; the CPU alpha of
// a skin comes from the source set with setShadowSkinSource (the world renderer reloads the
// image from Material::texturePath). No source, or null from it: the skin casts opaque.
//
// Drawing (ShadowPass). Per frame, outside frames: begin(), add() per shadow (a silhouette
// texture or the blob, and its ground rectangle), build(grid) cuts the draped geometry from the
// terrain on the CPU (buildTerrainDecal, the GL side's). Per eye: draw() copies the vertices into
// the frame arena as FlatVertex at its first call of an arena frame and reuses them for the
// second eye, then draws each batch: flat shader, alpha blend (the sequels: the Filter blend with
// grey 1 - alpha), depth test on and write off, the polygon offset (kPolygonOffsetDepth, as the
// terrain overlays and the ground marks), back faces culled, the level fog (applyMaterialState),
// TEV constant black with the texture's alpha (tevShadow(255): the silhouette's alpha already
// holds 0..102). The blob is a radial A8 texture made once (init()): what an entity's shadow
// looks like while its silhouette is not generated yet (the late path, at most one silhouette
// per frame).
#pragma once

#include <cstddef>
#include <string>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include "as3d/ground_marks.h"
#include "as3d/math.h"
#include "as3d/render_rules.h"
#include "as3d/shadow_raster.h"
#include "as3d/terrain_grid.h"
#include "as3d/texture.h"

namespace as3d::pica {

// Largest silhouette texture side on the PICA (see above).
constexpr int kMaxShadowSide = 128;
// The blob texture's side.
constexpr int kShadowBlobSide = 32;

// What one generate() did (the last call; ShadowPicaStats sums them).
struct ShadowGenInfo {
    int width = 0, height = 0;    // the GL side's silhouette size
    int texWidth = 0, texHeight = 0; // the PICA texture's (at most kMaxShadowSide)
    size_t bytes = 0;             // texture bytes (mip chain included)
    double ms = 0.0;              // rasteriser + texture creation
    bool skinAlpha = false;       // a skin with alpha took part
};

// The SD cache of silhouettes (docs/3ds-port.md "Load: shadows"): rasterising one costs about
// 8 ms on the console, a level start rasterises 140. setShadowCacheFile loads the entries of a
// file (key: a hash of everything the silhouette depends on: the model's positions, faces and
// coordinates, the skin's alpha, kind, rotation, quality, sun, the rules' shadow fields, the side
// cap; value: bounds, sizes and the alpha texels), ShadowRenderer::generate takes a cached
// silhouette instead of rasterising it, and saveShadowCache writes the file again when new
// silhouettes were made. The same texels either way. An empty path turns the cache off.
void setShadowCacheFile(const std::string& path);
struct ShadowCacheStats {
    int loaded = 0;      // entries read from the file
    int hits = 0, misses = 0;
    double loadMs = 0.0, saveMs = 0.0;
    size_t fileBytes = 0;
};
const ShadowCacheStats& shadowCacheStats();
// Writes the file when entries were added since it was loaded; returns the entries written.
// With `write` (the 3DS app's writer thread, ctr_perf.h queueFileWrite) the bytes are handed to
// it instead of being written here (the first start's write and fsync took 2.1 s on the
// console); `write` returns false when it cannot take them (then they are written here).
using ShadowCacheWriter = std::function<bool(const std::string& path, std::vector<u8>&& bytes)>;
int saveShadowCache(const ShadowCacheWriter& write = ShadowCacheWriter());

struct ShadowPicaStats {
    // Since resetShadowPicaStats() (WorldRenderer::beginLevel): silhouettes generated, failed,
    // their texture bytes (live: freed maps are subtracted), the generation time.
    int generated = 0;
    int failed = 0;
    size_t textureBytes = 0;
    double generateMs = 0.0;
    double maxGenerateMs = 0.0;
    int capped = 0;               // rasterised smaller than the GL side (kMaxShadowSide)
    int bySide[8] = {};           // by the silhouette's larger side: <= 8, 16, 32, ..., 1024
    size_t blobBytes = 0;
    // The last prepared frame (ShadowPass::begin .. draw).
    int drawn = 0;                // shadows with their silhouette
    int blobs = 0;                // shadows drawn as the blob
    int late = 0;                 // silhouettes generated in this frame's prepare
    double placeMs = 0.0;         // prepare(): lookups, rectangles and draping (no generation)
    double buildMs = 0.0;         // of which the draping (ShadowPass::build)
    int drapedNow = 0;            // shadows draped this frame (the others came from the cache)
    int vertices = 0;             // draped vertices (one eye)
    int draws = 0;                // draw calls (all eyes)
    size_t arenaBytes = 0;
};
const ShadowPicaStats& shadowPicaStats();
void resetShadowPicaStats();
ShadowGenInfo lastShadowGenInfo();
// The world renderer's prepare(): how many silhouettes it generated for this frame, and the
// time of the shadow placement and draping without that generation.
void noteShadowFrame(int late, double placeMs);

// The CPU alpha of a skin texture for generate() (null: opaque). Process-wide; set by the world
// renderer at init, cleared with an empty function.
void setShadowSkinSource(std::function<const ShadowSkin*(const Texture2D* skin)> source);

// WorldRenderer::precache: with false (the default) only the placements' silhouettes (their own
// rotation) and the closure's planar ones are generated at level start; a projected silhouette
// in another 30-degree rotation (a script spawn) is generated on first sight, at most one per
// frame, the blob standing in until then. True: also the 12 rotations of every projected one of
// the closure, as the GL precache (mission 1: 726 silhouettes instead of 126, 17.8 s of
// generation under Azahar instead of 2.7 s).
void setShadowPrecacheRotations(bool all);
bool shadowPrecacheRotations();

class ShadowPass {
public:
    // Creates the blob texture (outside frames). False without memory.
    bool init();
    bool ready() const { return blob_.valid(); }
    const Texture2D& blob() const { return blob_; }

    void setMultiply(bool on) { multiply_ = on; } // RenderRules::shadowMultiply
    void begin();
    // `isStatic`: the rectangle never moves (projected shadows sit at the spawn origin), so its
    // draped geometry is kept between frames (until invalidate() or 120 builds without it).
    void add(const Texture2D* texture, const GroundRect& rect, bool isBlob, bool isStatic);
    void build(const TerrainGridView& grid);
    // The terrain heights changed (craters): drape every shadow again.
    void invalidate() { cache_.clear(); }
    // One eye: `viewProj` = the eye's PICA projection * the central view. The caller has set the
    // depth mapping, the fog table and the level fog colour (the world renderer's level state).
    // Leaves blend off, depth test + write on, no polygon offset, cull back.
    void draw(const Mat4& viewProj);
    size_t shadowCount() const { return items_.size(); }

private:
    struct Item {
        const Texture2D* texture = nullptr;
        GroundRect rect;
        bool blob = false;
        bool isStatic = false;
        size_t first = 0, count = 0;
    };
    struct CacheKey {
        const Texture2D* texture;
        float x, y, sx, sy;
        bool operator==(const CacheKey& o) const {
            return texture == o.texture && x == o.x && y == o.y && sx == o.sx && sy == o.sy;
        }
    };
    struct CacheHash {
        size_t operator()(const CacheKey& k) const;
    };
    struct Cached {
        std::vector<DecalVertex> verts;
        unsigned lastBuild = 0;
    };
    Texture2D blob_;
    bool multiply_ = false;
    std::vector<Item> items_;
    std::unordered_map<CacheKey, Cached, CacheHash> cache_;
    unsigned builds_ = 0;
    std::vector<DecalVertex> verts_;
    // The draws (build): one per texture, in the order each texture first appears; the shadows
    // of one texture (every blob, the copies of one model's silhouette) are contiguous in verts_.
    // Shadows write no depth and darken by a factor (alpha blend of black, or multiply), so their
    // order does not change the picture beyond the 8-bit rounding of each blend.
    struct Group {
        const Texture2D* texture = nullptr;
        size_t first = 0, count = 0;
    };
    std::vector<Group> groups_;
    std::vector<DecalVertex> scratch_;
    const void* arenaVerts_ = nullptr;
    unsigned arenaFrame_ = ~0u;
};

} // namespace as3d::pica
