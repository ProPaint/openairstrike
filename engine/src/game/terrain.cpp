// Terrain vertex grid: heights, positions, normals, static lighting, height/normal
// queries. See docs/spec/hmap.md ("Terrain geometry", "Static lighting",
// "TerrainHeight") and engine/include/as3d/terrain.h for the deliberate deviations.
#include "as3d/terrain.h"

#include "as3d/defs.h"

#include <algorithm>
#include <cmath>

namespace as3d {


// ---------------------------------------------------------------------------
// Resampler (VERIFIED-CODE 0x4185b0; must match tools/ref/hmap.py's resample() exactly)
// ---------------------------------------------------------------------------
void resampleHeightField(const u8* src, int sw, int sh, size_t srcOffset, u8* dst, int dw, int dh) {
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    if (dw == 1 || dh == 1) {
        // Not exercised by any caller in this codebase (every destination is at least
        // 33x33 for vertex heights, 256x256 for map textures), but guard against a
        // divide-by-zero in the step computation below rather than assume it away.
        for (int j = 0; j < dh; j++)
            for (int i = 0; i < dw; i++)
                dst[static_cast<size_t>(j) * static_cast<size_t>(dw) + static_cast<size_t>(i)] =
                    src[srcOffset];
        return;
    }
    std::uint64_t stepx = (static_cast<std::uint64_t>(sw - 1) * 65536ull) / static_cast<std::uint64_t>(dw - 1);
    std::uint64_t stepy = (static_cast<std::uint64_t>(sh - 1) * 65536ull) / static_cast<std::uint64_t>(dh - 1);
    std::uint64_t fyAcc = 0;
    size_t o = 0;
    for (int j = 0; j < dh; j++) {
        int iy = static_cast<int>(fyAcc >> 16);
        double fy = static_cast<double>(fyAcc & 0xFFFFull) / 65536.0;
        std::uint64_t fxAcc = 0;
        size_t rowbase = srcOffset + static_cast<size_t>(iy) * static_cast<size_t>(sw);
        for (int i = 0; i < dw; i++) {
            int ix = static_cast<int>(fxAcc >> 16);
            double fx = static_cast<double>(fxAcc & 0xFFFFull) / 65536.0;
            u8 a = src[rowbase + static_cast<size_t>(ix)];
            u8 b, c, d;
            if (ix < sw - 1) {
                b = src[rowbase + static_cast<size_t>(ix) + 1];
                if (iy < sh - 1) {
                    c = src[rowbase + static_cast<size_t>(sw) + static_cast<size_t>(ix)];
                    d = src[rowbase + static_cast<size_t>(sw) + static_cast<size_t>(ix) + 1];
                } else {
                    c = a;
                    d = b;
                }
            } else {
                b = a;
                if (iy < sh - 1) {
                    c = src[rowbase + static_cast<size_t>(sw) + static_cast<size_t>(ix)];
                } else {
                    c = a;
                }
                d = c;
            }
            double top = static_cast<double>(a) * (1.0 - fx) + static_cast<double>(b) * fx;
            double bot = static_cast<double>(c) * (1.0 - fx) + static_cast<double>(d) * fx;
            double result = top * (1.0 - fy) + bot * fy;
            dst[o++] = static_cast<u8>(static_cast<int>(result) & 0xFF);
            fxAcc += stepx;
        }
        fyAcc += stepy;
    }
}

namespace {

Vec3 vsub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 vcross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float vlen(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
Vec3 vnorm(const Vec3& v) {
    float l = vlen(v);
    if (!(l > 1e-12f)) return {0.0f, 0.0f, 0.0f};
    return {v.x / l, v.y / l, v.z / l};
}
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// docs/spec/hmap.md "Normals": per-component acos/mean/cos combination of the (up to 6)
// adjacent face normals, then normalized. `face(c, r, tri)` returns the normalized face
// normal of triangle `tri` (0 or 1) of cell (c, r).
template <typename FaceFn>
Vec3 combineFaceNormals(int c, int r, int w, int h, FaceFn face) {
    Vec3 acc[6];
    int n = 0;
    if (c < w && r < h) {
        acc[n++] = face(c, r, 0);
        acc[n++] = face(c, r, 1);
    }
    if (c > 0) {
        if (r < h) acc[n++] = face(c - 1, r, 0);
        if (r > 0) {
            acc[n++] = face(c - 1, r - 1, 0);
            acc[n++] = face(c - 1, r - 1, 1);
        }
    }
    if (c < w && r > 0) acc[n++] = face(c, r - 1, 1);
    if (n == 0) return {0.0f, 0.0f, 0.0f};
    float sums[3] = {0.0f, 0.0f, 0.0f};
    for (int k = 0; k < n; k++) {
        sums[0] += std::acos(clampf(acc[k].x, -1.0f, 1.0f));
        sums[1] += std::acos(clampf(acc[k].y, -1.0f, 1.0f));
        sums[2] += std::acos(clampf(acc[k].z, -1.0f, 1.0f));
    }
    Vec3 combined{std::cos(sums[0] / n), std::cos(sums[1] / n), std::cos(sums[2] / n)};
    return vnorm(combined);
}

} // namespace

TerrainStyle terrainStyleFromDef(const LevelDef& d) {
    TerrainStyle s;
    s.id = d.id;
    s.mapPath = d.map;
    s.texturesDir = d.textures;
    s.hmin = d.hmin;
    s.hmax = d.hmax;
    s.hasFog = d.hasFog;
    for (int i = 0; i < 3; i++) s.fogColor[i] = d.fogColor[i];
    s.fogNear = d.fogNear;
    s.fogFar = d.fogFar;
    for (int i = 0; i < 9; i++) s.sun[i] = d.sun[i];
    s.hasWater = d.hasWater;
    s.waterTexture = d.waterTexture;
    s.waterLevel = d.waterLevel;
    s.waterAlpha = d.waterAlpha;
    s.night = d.night;
    s.hasIntermission = d.hasIntermission;
    for (int i = 0; i < 6; i++) s.intermission[i] = d.intermission[i];
    return s;
}

bool Terrain::build(const LevelData& level, const LevelDef& def) { return build(level, terrainStyleFromDef(def)); }

bool Terrain::build(const LevelData& level, const TerrainStyle& def) {
    *this = Terrain{};
    if (level.width < 1 || level.height < 1) return false;
    level_ = &level;
    style_ = def;
    width_ = level.width;
    height_ = level.height;

    int w = static_cast<int>(width_), h = static_cast<int>(height_);
    int vw = w + 1, vh = h + 1;

    // Extract the height plane (byte 0 of every cell) into a contiguous buffer for the
    // resampler, then resample to the (W+1) x (H+1) vertex grid (docs/spec/hmap.md
    // "Vertex grid").
    std::vector<u8> heightPlane(static_cast<size_t>(w) * static_cast<size_t>(h));
    for (size_t i = 0; i < heightPlane.size(); i++) heightPlane[i] = level.cells[i].height;
    rawHeights_.assign(static_cast<size_t>(vw) * static_cast<size_t>(vh), 0);
    resampleHeightField(heightPlane.data(), w, h, 0, rawHeights_.data(), vw, vh);

    float hmin = def.hmin, hmax = def.hmax;
    positions_.resize(rawHeights_.size());
    for (int r = 0; r < vh; r++) {
        for (int c = 0; c < vw; c++) {
            size_t i = static_cast<size_t>(r) * static_cast<size_t>(vw) + static_cast<size_t>(c);
            float raw = static_cast<float>(rawHeights_[i]);
            float z = (hmax - hmin) * (raw / 255.0f) + hmin;
            positions_[i] = {static_cast<float>(c) * kHmapCellSize, static_cast<float>(r) * kHmapCellSize, z};
        }
    }

    // Face normals per cell (2 triangles: (v00,v10,v11) and (v00,v11,v01)).
    std::vector<Vec3> face0(static_cast<size_t>(w) * static_cast<size_t>(h));
    std::vector<Vec3> face1(static_cast<size_t>(w) * static_cast<size_t>(h));
    auto posAt = [&](int c, int r) -> const Vec3& { return positions_[static_cast<size_t>(r) * vw + c]; };
    for (int r = 0; r < h; r++) {
        for (int c = 0; c < w; c++) {
            const Vec3& p00 = posAt(c, r);
            const Vec3& p10 = posAt(c + 1, r);
            const Vec3& p11 = posAt(c + 1, r + 1);
            const Vec3& p01 = posAt(c, r + 1);
            size_t fi = static_cast<size_t>(r) * w + c;
            face0[fi] = vnorm(vcross(vsub(p10, p00), vsub(p11, p00)));
            face1[fi] = vnorm(vcross(vsub(p11, p00), vsub(p01, p00)));
        }
    }
    auto face = [&](int c, int r, int tri) -> Vec3 {
        size_t fi = static_cast<size_t>(r) * w + c;
        return tri == 0 ? face0[fi] : face1[fi];
    };

    normals_.resize(positions_.size());
    for (int r = 0; r < vh; r++) {
        for (int c = 0; c < vw; c++) {
            normals_[static_cast<size_t>(r) * vw + c] = combineFaceNormals(c, r, w, h, face);
        }
    }

    // Static lighting (docs/spec/hmap.md "Static lighting"), with our deliberate
    // deviation: a vertex facing away from the sun gets the ambient colour, not black.
    Vec3 sunCol{def.sun[0], def.sun[1], def.sun[2]};
    Vec3 lightDir = vnorm(Vec3{def.sun[3], def.sun[4], def.sun[5]});
    Vec3 ambient{def.sun[6], def.sun[7], def.sun[8]};
    bool water = def.hasWater;
    float waterLevelZ = def.waterLevel;

    colors_.resize(positions_.size());
    for (size_t i = 0; i < positions_.size(); i++) {
        const Vec3& p = positions_[i];
        const Vec3& n = normals_[i];
        float f = 1.0f;
        if (water && p.z < waterLevelZ) {
            float denom = waterLevelZ - hmin;
            f = (std::fabs(denom) > 1e-9f) ? (p.z - hmin) / denom : 0.0f;
        }
        float d = n.x * lightDir.x + n.y * lightDir.y + n.z * lightDir.z;
        if (d > 1.0f) d = 1.0f;
        float rgb[3];
        if (d > 0.0f) {
            float sc[3] = {sunCol.x, sunCol.y, sunCol.z};
            float am[3] = {ambient.x, ambient.y, ambient.z};
            for (int k = 0; k < 3; k++) {
                float v = (sc[k] * d + am[k]) * 255.0f * f;
                rgb[k] = std::min(255.0f, v);
            }
        } else {
            // Deviation from the original (docs/spec/README.md table): properly scaled
            // ambient instead of the original's near-black bug.
            float am[3] = {ambient.x, ambient.y, ambient.z};
            for (int k = 0; k < 3; k++) rgb[k] = clampf(am[k] * 255.0f, 0.0f, 255.0f);
        }
        TerrainVertexColor tc;
        tc.r = static_cast<u8>(static_cast<int>(rgb[0]));
        tc.g = static_cast<u8>(static_cast<int>(rgb[1]));
        tc.b = static_cast<u8>(static_cast<int>(rgb[2]));
        colors_[i] = tc;
    }
    return true;
}

float Terrain::heightAt(float worldX, float worldY) const {
    if (positions_.empty()) return 0.0f;
    int w = width(), h = height();
    int vw = vertsWide();
    float maxX = static_cast<float>(w) * kHmapCellSize;
    float maxY = static_cast<float>(h) * kHmapCellSize;
    float cx = clampf(worldX, 0.0f, maxX);
    float cy = clampf(worldY, 0.0f, maxY);
    int ix = static_cast<int>(std::floor(cx / kHmapCellSize));
    int iy = static_cast<int>(std::floor(cy / kHmapCellSize));
    ix = std::min(std::max(ix, 0), w - 1);
    iy = std::min(std::max(iy, 0), h - 1);
    float fx = cx / kHmapCellSize - static_cast<float>(ix);
    float fy = cy / kHmapCellSize - static_cast<float>(iy);
    auto zAt = [&](int c, int r) { return positions_[static_cast<size_t>(r) * vw + c].z; };
    float z00 = zAt(ix, iy), z10 = zAt(ix + 1, iy), z01 = zAt(ix, iy + 1), z11 = zAt(ix + 1, iy + 1);
    float h0 = z00 + fx * (z10 - z00);
    float h1 = z01 + fx * (z11 - z01);
    return h0 + fy * (h1 - h0);
}

float Terrain::vertexZ(int c, int r) const {
    if (c < 0 || r < 0 || c > width() || r > height() || positions_.empty()) return 0.0f;
    return positions_[static_cast<size_t>(r) * static_cast<size_t>(vertsWide()) + static_cast<size_t>(c)].z;
}

bool Terrain::setVertexZ(int c, int r, float z) {
    if (c < 0 || r < 0 || c > width() || r > height() || positions_.empty()) return false;
    positions_[static_cast<size_t>(r) * static_cast<size_t>(vertsWide()) + static_cast<size_t>(c)].z = z;
    return true;
}

Vec3 Terrain::normalAt(float worldX, float worldY) const {
    if (normals_.empty()) return {0.0f, 0.0f, 1.0f};
    int w = width(), h = height();
    int vw = vertsWide();
    float maxX = static_cast<float>(w) * kHmapCellSize;
    float maxY = static_cast<float>(h) * kHmapCellSize;
    float cx = clampf(worldX, 0.0f, maxX);
    float cy = clampf(worldY, 0.0f, maxY);
    int ix = static_cast<int>(std::floor(cx / kHmapCellSize));
    int iy = static_cast<int>(std::floor(cy / kHmapCellSize));
    ix = std::min(std::max(ix, 0), w - 1);
    iy = std::min(std::max(iy, 0), h - 1);
    float fx = cx / kHmapCellSize - static_cast<float>(ix);
    float fy = cy / kHmapCellSize - static_cast<float>(iy);
    auto nAt = [&](int c, int r) { return normals_[static_cast<size_t>(r) * vw + c]; };
    Vec3 n00 = nAt(ix, iy), n10 = nAt(ix + 1, iy), n01 = nAt(ix, iy + 1), n11 = nAt(ix + 1, iy + 1);
    Vec3 top{n00.x + fx * (n10.x - n00.x), n00.y + fx * (n10.y - n00.y), n00.z + fx * (n10.z - n00.z)};
    Vec3 bot{n01.x + fx * (n11.x - n01.x), n01.y + fx * (n11.y - n01.y), n01.z + fx * (n11.z - n01.z)};
    Vec3 combined{top.x + fy * (bot.x - top.x), top.y + fy * (bot.y - top.y), top.z + fy * (bot.z - top.z)};
    return vnorm(combined);
}

} // namespace as3d
