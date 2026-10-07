// The water's CPU side (as3d/water_build.h).
#include "as3d/water_build.h"

#include <algorithm>
#include <cmath>

namespace as3d {

WaterParams computeWaterParams(const TerrainStyle& def, float timeSeconds) {
    WaterParams w;
    w.present = def.hasWater && !def.waterTexture.empty();
    w.level = def.waterLevel;
    w.alpha = def.waterAlpha;
    Vec3 dir = normalize(Vec3{def.sun[3], def.sun[4], def.sun[5]});
    auto ch = [&](int k) { return std::min(1.0f, std::max(0.0f, def.sun[k] * dir.z + def.sun[6 + k])); };
    w.colour = {ch(0), ch(1), ch(2)};
    w.phase = timeSeconds * kPi * 0.1f;
    w.planeZ = w.level + std::sin(w.phase);
    w.texOffset = {0.4f * std::sin(w.phase), 0.4f * std::sin(w.phase * 0.5f)};
    return w;
}

void buildWaterChunk(const WaterSurface& s, const TerrainGridView& grid, int rowStart, int rows,
                     std::vector<WaterGridVertex>& vertices, std::vector<uint16_t>& indices) {
    vertices.clear();
    indices.clear();
    if (!s.present || !grid.valid() || rows < 1) return;
    const int W = grid.width, vw = W + 1;
    for (int lr = 0; lr <= rows; ++lr) {
        const int r = rowStart + lr;
        for (int c = 0; c <= W; ++c) {
            WaterGridVertex v;
            v.col = static_cast<float>(c);
            v.row = static_cast<float>(r);
            v.terrainZ = grid.at(c, r).z;
            v.weight = s.weightAt(c, r);
            vertices.push_back(v);
        }
    }
    for (int lr = 0; lr < rows; ++lr) {
        for (int c = 0; c < W; ++c) {
            if (!s.wet(c, rowStart + lr)) continue;
            const uint16_t v00 = static_cast<uint16_t>(lr * vw + c), v10 = static_cast<uint16_t>(v00 + 1);
            const uint16_t v01 = static_cast<uint16_t>(v00 + vw), v11 = static_cast<uint16_t>(v01 + 1);
            indices.insert(indices.end(), {v00, v10, v11, v00, v11, v01});
        }
    }
}

WaterGridFrame computeWaterGridFrame(float timeSeconds) {
    WaterGridFrame f;
    const float t = timeSeconds * kPi * 0.1f;
    f.waveTime = timeSeconds;
    f.baseOffset = {0.4f * std::sin(t), 0.4f * std::sin(t * 0.5f)};
    f.shineOffset = {0.4f * std::sin(t * 0.5f) + 0.2f, -0.2f * std::sin(t * 0.25f) - 0.3f};
    return f;
}

Vec2 waterBaseUv(int col, int row, const WaterGridFrame& f) {
    return {1.5f * static_cast<float>(col) * 0.25f + f.baseOffset.x, 1.5f * static_cast<float>(row) * 0.25f + f.baseOffset.y};
}

Vec2 waterShineUv(int col, int row, const WaterGridFrame& f) {
    return {2.0f * static_cast<float>(col) * 0.25f + f.shineOffset.x, 2.0f * static_cast<float>(row) * 0.25f + f.shineOffset.y};
}

WaterGridAnimated animateWaterGrid(const WaterSurface& s, const WaterGridVertex& v, const WaterGridFrame& f) {
    // The same operations as kWaterGridVertexSrc.
    const float c = v.col, r = v.row, w = v.weight;
    float z = v.terrainZ;
    if (w >= kWaterMinWeight && r > 0.5f && r < static_cast<float>(s.height) - 0.5f)
        z = s.level + kWaterWaveAmplitude * w * 0.5f * (std::sin(0.75f * r + f.waveTime) + std::sin(c + f.waveTime));
    WaterGridAnimated o;
    o.pos = {c * 40.0f, r * 40.0f, z};
    const float cx = c * 0.25f, cy = r * 0.25f;
    o.uvBase = {1.5f * cx + f.baseOffset.x, 1.5f * cy + f.baseOffset.y};
    o.uvShine = {2.0f * cx + f.shineOffset.x, 2.0f * cy + f.shineOffset.y};
    o.alpha = w * s.opacity;
    return o;
}

void animateWaterGrid(const WaterSurface& s, const WaterGridVertex* vertices, size_t count, float timeSeconds,
                      std::vector<WaterGridAnimated>& out) {
    const WaterGridFrame f = computeWaterGridFrame(timeSeconds);
    out.resize(count);
    for (size_t i = 0; i < count; ++i) out[i] = animateWaterGrid(s, vertices[i], f);
}

WaterGridGeometry buildWaterGridGeometry(const WaterSurface& surface, const TerrainGridView& grid) {
    WaterGridGeometry g;
    if (!surface.present || !grid.valid() || surface.width != grid.width || surface.height != grid.height) return g;
    const int H = grid.height;
    const int nChunks = (H + kWaterChunkRows - 1) / kWaterChunkRows;
    std::vector<WaterGridVertex> cv;
    std::vector<uint16_t> ci;
    g.chunks.resize(static_cast<size_t>(nChunks));
    for (int k = 0; k < nChunks; ++k) {
        WaterGridChunk& ch = g.chunks[static_cast<size_t>(k)];
        ch.rowStart = k * kWaterChunkRows;
        ch.rows = std::min(kWaterChunkRows, H - ch.rowStart);
        buildWaterChunk(surface, grid, ch.rowStart, ch.rows, cv, ci);
        ch.firstVertex = g.verts.size();
        ch.vertexCount = cv.size();
        ch.firstIndex = g.indices.size();
        ch.indexCount = ci.size();
        g.wetCells += static_cast<int>(ci.size() / 6);
        for (uint16_t i : ci) g.indices.push_back(static_cast<uint16_t>(ch.firstVertex + i));
        float zmin = surface.level - kWaterWaveAmplitude, zmax = surface.level + kWaterWaveAmplitude;
        for (const WaterGridVertex& v : cv) {
            zmin = std::min(zmin, v.terrainZ);
            zmax = std::max(zmax, v.terrainZ);
        }
        ch.bounds.min = {0.0f, static_cast<float>(ch.rowStart) * kHmapCellSize, zmin};
        ch.bounds.max = {static_cast<float>(grid.width) * kHmapCellSize,
                         static_cast<float>(ch.rowStart + ch.rows) * kHmapCellSize, zmax};
        g.verts.insert(g.verts.end(), cv.begin(), cv.end());
    }
    return g;
}

std::vector<int> updateWaterGridHeights(WaterGridGeometry& g, const WaterSurface& surface, const VertexRect* rects,
                                        size_t count, const TerrainGridView& grid) {
    std::vector<int> changed;
    if (!g.valid() || !grid.valid() || grid.width != surface.width || grid.height != surface.height) return changed;
    const int vw = grid.width + 1;
    for (size_t k = 0; k < g.chunks.size(); ++k) {
        WaterGridChunk& ch = g.chunks[k];
        const int r0 = ch.rowStart, r1 = ch.rowStart + ch.rows;
        bool touched = false;
        for (size_t i = 0; i < count && !touched; ++i) {
            const VertexRect& rc = rects[i];
            if (rc.empty()) continue;
            touched = rc.r0 <= r1 && rc.r1 >= r0 && rc.c0 <= grid.width && rc.c1 >= 0;
        }
        if (!touched) continue;
        for (int lr = 0; lr <= ch.rows; ++lr) {
            for (int c = 0; c < vw; ++c) {
                WaterGridVertex& v = g.verts[ch.firstVertex + static_cast<size_t>(lr) * vw + c];
                v.terrainZ = grid.at(c, ch.rowStart + lr).z;
                ch.bounds.min.z = std::min(ch.bounds.min.z, v.terrainZ);
                ch.bounds.max.z = std::max(ch.bounds.max.z, v.terrainZ);
            }
        }
        changed.push_back(static_cast<int>(k));
    }
    return changed;
}

namespace {

bool boxInClip(const Mat4& m, const Vec3& mn, const Vec3& mx) {
    int outs[6] = {0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 8; i++) {
        Vec4 c = m * Vec4{(i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z, 1.0f};
        if (c.x < -c.w) outs[0]++;
        if (c.x > c.w) outs[1]++;
        if (c.y < -c.w) outs[2]++;
        if (c.y > c.w) outs[3]++;
        if (c.z < -c.w) outs[4]++;
        if (c.z > c.w) outs[5]++;
    }
    for (int i = 0; i < 6; i++)
        if (outs[i] == 8) return false;
    return true;
}

} // namespace

std::vector<int> visibleWaterChunks(const WaterGridGeometry& g, const Mat4& viewProjection) {
    std::vector<int> visible;
    for (size_t k = 0; k < g.chunks.size(); ++k) {
        const WaterGridChunk& ch = g.chunks[k];
        if (ch.indexCount == 0 || !boxInClip(viewProjection, ch.bounds.min, ch.bounds.max)) continue;
        visible.push_back(static_cast<int>(k));
    }
    return visible;
}

} // namespace as3d
