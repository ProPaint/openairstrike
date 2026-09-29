// The terrain as the sequels' game rules change it: TerraMorph stamps and the water height
// (docs/spec/as2/rcsl-builtins-semantics.delta.md 95 and 67, engine-behaviour.delta.md 4.2
// and 4.7, docs/spec/as2/issues/200). Simulation side only: the renderer mirrors the
// changed vertices through World::takeTerrainChanges.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "as3d/image.h"
#include "as3d/vfs.h"
#include "as3d/world.h"
#include "world_internal.h"

namespace as3d {

namespace {

// Largest stamp side we accept. The shipped stamps are 4x4 to 11x11; a bigger image is
// still applied (only the part on the grid counts), this only bounds the allocation.
constexpr int kMaxStampSide = 1024;

// The stored name of a stamp: `name` with the extension (from the last '.' after the last
// '/' or '\') replaced by ".tga", or ".tga" appended when there is none.
std::string stampName(const char* name) {
    std::string s = name;
    size_t slash = s.find_last_of("/\\");
    size_t dot = s.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) s.erase(dot);
    return s + ".tga";
}

// Reads an 8-bit stamp through the VFS: the header is checked as the original's raw reader
// does (uncompressed types 1, 2 and 3, 8 bits per pixel), the pixels come from our TGA
// decoder and are put back in file order (the first stored row first), since the original
// does not flip rows. False for anything else.
bool loadStamp(Vfs& vfs, const std::string& path, int& w, int& h, std::vector<u8>& pixels) {
    Blob blob;
    if (!vfs.read(path, blob) || blob.size() < 18) return false;
    const u8* b = blob.data();
    const u8 type = b[2];
    w = b[12] | (b[13] << 8);
    h = b[14] | (b[15] << 8);
    const u8 bpp = b[16];
    const bool topDown = (b[17] & 0x20) != 0;
    if ((type != 1 && type != 2 && type != 3) || bpp != 8) return false;
    if (w < 1 || h < 1 || w > kMaxStampSide || h > kMaxStampSide) return false;
    Image img;
    if (!decodeTga(blob.data(), blob.size(), img) || img.width != w || img.height != h) return false;
    if (img.rgba.size() != static_cast<size_t>(w) * static_cast<size_t>(h) * 4u) return false;
    pixels.assign(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
    for (int j = 0; j < h; ++j) {
        // Decoded row 0 is the top row; file row j is the top row only for a top-down file.
        int src = topDown ? j : h - 1 - j;
        for (int i = 0; i < w; ++i) {
            // Grey images decode to R = G = B; for the other types R stands for the byte
            // (docs/spec/as2/issues/230).
            pixels[static_cast<size_t>(j) * w + i] = img.rgba[(static_cast<size_t>(src) * w + i) * 4u];
        }
    }
    return true;
}

// Truncation toward zero with the executable's out-of-range rule (INT32_MIN), in double.
i32 truncToInt(double v) {
    if (!(v > -2147483649.0 && v < 2147483648.0)) return static_cast<i32>(0x80000000u);
    return static_cast<i32>(v);
}

} // namespace

bool World::terraMorph(float x, float y, const char* name) {
    if (!name || !terrainValid_ || !vfs_) return false;
    // 1. Stamp lookup: byte-for-byte against the stored names, in load order.
    const Stamp* st = nullptr;
    for (const Stamp& s : stamps_) {
        if (s.name == name) {
            st = &s;
            break;
        }
    }
    if (!st) {
        if (static_cast<int>(stamps_.size()) >= kMaxTerraMorphStamps) return false;
        Stamp s;
        s.name = stampName(name);
        if (!loadStamp(*vfs_, s.name, s.w, s.h, s.pixels)) return false; // retried next call
        stamps_.push_back(std::move(s));
        st = &stamps_.back();
    }
    // 2. Placement: stamp pixel (hw, hh) on the vertex c0 + hw, r0 + hh, truncated toward 0.
    const int hw = st->w / 2, hh = st->h / 2;
    const i32 c0 = truncToInt(static_cast<double>(x) / static_cast<double>(kHmapCellSize) - hw);
    const i32 r0 = truncToInt(static_cast<double>(y) / static_cast<double>(kHmapCellSize) - hh);
    if (c0 == static_cast<i32>(0x80000000u) || r0 == static_cast<i32>(0x80000000u)) return false;
    // 4. One grey level is one heightmap level of this map; 128 is neutral; no clamp.
    const TerrainStyle& style = terrain_.style();
    const double scale = (static_cast<double>(style.hmax) - static_cast<double>(style.hmin)) / 255.0;
    const int W = terrain_.width(), H = terrain_.height();
    TerrainChange ch;
    ch.c0 = W + 1;
    ch.r0 = H + 1;
    for (int j = 0; j < st->h; ++j) {
        const long long r = static_cast<long long>(r0) + j;
        if (r < 0 || r > H) continue;
        for (int i = 0; i < st->w; ++i) {
            const long long c = static_cast<long long>(c0) + i;
            if (c < 0 || c > W) continue;
            const float z = terrain_.vertexZ(static_cast<int>(c), static_cast<int>(r));
            // Underwater ground never changes (the comparison lets a NaN through).
            if (hasWater_ && z < waterLevel_) continue;
            const int p = st->pixels[static_cast<size_t>(j) * st->w + i];
            const double nz = static_cast<double>(z) + static_cast<double>(p - 128) * scale;
            terrain_.setVertexZ(static_cast<int>(c), static_cast<int>(r), static_cast<float>(nz));
            ch.c0 = std::min(ch.c0, static_cast<int>(c));
            ch.r0 = std::min(ch.r0, static_cast<int>(r));
            ch.c1 = std::max(ch.c1, static_cast<int>(c));
            ch.r1 = std::max(ch.r1, static_cast<int>(r));
        }
    }
    // 5. Nothing else: normals, colours, textures and water stay as loaded.
    if (ch.c1 >= ch.c0 && ch.r1 >= ch.r0) noteTerrainChange(ch);
    return true;
}

void World::noteTerrainChange(const TerrainChange& c) {
    if (terrainChanges_.size() < kMaxTerrainChanges) {
        terrainChanges_.push_back(c);
        return;
    }
    TerrainChange& all = terrainChanges_.back();
    all.c0 = std::min(all.c0, c.c0);
    all.r0 = std::min(all.r0, c.r0);
    all.c1 = std::max(all.c1, c.c1);
    all.r1 = std::max(all.r1, c.r1);
}

std::vector<TerrainChange> World::takeTerrainChanges() {
    std::vector<TerrainChange> out;
    out.swap(terrainChanges_);
    return out;
}

float World::waterHeight(float x, float y) const {
    if (!hasWater_ || !terrainValid_) return terrainHeight(x, y);
    // The water grid: a flooded vertex holds the water level, any other one the terrain,
    // the first and last rows too; bilinear in between, clamped to the map like
    // TerrainHeight (our deviation). The original adds an animated wave term to flooded
    // vertices, which its renderer computes (open question 3 of the builtins delta).
    const int W = terrain_.width(), H = terrain_.height();
    float cx = std::min(std::max(x, 0.0f), static_cast<float>(W) * kHmapCellSize);
    float cy = std::min(std::max(y, 0.0f), static_cast<float>(H) * kHmapCellSize);
    int ix = std::min(std::max(static_cast<int>(std::floor(cx / kHmapCellSize)), 0), W - 1);
    int iy = std::min(std::max(static_cast<int>(std::floor(cy / kHmapCellSize)), 0), H - 1);
    float fx = cx / kHmapCellSize - static_cast<float>(ix);
    float fy = cy / kHmapCellSize - static_cast<float>(iy);
    auto zAt = [&](int c, int r) {
        float t = terrain_.vertexZ(c, r);
        if (r == 0 || r == H) return t;
        return t < waterLevel_ ? waterLevel_ : t;
    };
    float z00 = zAt(ix, iy), z10 = zAt(ix + 1, iy), z01 = zAt(ix, iy + 1), z11 = zAt(ix + 1, iy + 1);
    float h0 = z00 + fx * (z10 - z00);
    float h1 = z01 + fx * (z11 - z01);
    return h0 + fy * (h1 - h0);
}

} // namespace as3d
