// Water surface data and the animated height (as3d/water.h).
#include "as3d/water.h"

#include <algorithm>
#include <cmath>

#include "as3d/math.h"

namespace as3d {

WaterSurface buildWaterSurface(const TerrainGridView& grid, bool present, bool waves, float level, float opacity,
                               const std::string& baseTexture, const std::string& shineTexture) {
    WaterSurface s;
    if (!present || !grid.valid()) return s;
    s.present = true;
    s.waves = waves;
    s.level = level;
    s.opacity = opacity;
    s.baseTexture = baseTexture;
    s.shineTexture = shineTexture;
    s.width = grid.width;
    s.height = grid.height;
    const int vw = grid.vertsWide(), vh = grid.vertsHigh();
    s.weight.assign(static_cast<size_t>(vw) * static_cast<size_t>(vh), 0.0f);
    for (int r = 0; r < vh; ++r) {
        for (int c = 0; c < vw; ++c) {
            float w = (level - grid.at(c, r).z) / kWaterFadeDepth;
            s.weight[static_cast<size_t>(r) * vw + c] = std::min(1.0f, std::max(0.0f, w));
        }
    }
    s.wetCell.assign(static_cast<size_t>(grid.width) * static_cast<size_t>(grid.height), 0);
    for (int r = 0; r < grid.height; ++r) {
        for (int c = 0; c < grid.width; ++c) {
            bool wet = grid.at(c, r).z <= level || grid.at(c + 1, r).z <= level || grid.at(c, r + 1).z <= level ||
                       grid.at(c + 1, r + 1).z <= level;
            s.wetCell[static_cast<size_t>(r) * grid.width + c] = wet ? 1 : 0;
        }
    }
    return s;
}

WaterSurface buildWaterSurface(const Terrain& terrain, bool waves, const std::string& shineTexture) {
    const TerrainStyle& st = terrain.style();
    return buildWaterSurface(TerrainGridView::of(terrain), st.hasWater, waves, st.waterLevel, st.waterAlpha,
                             st.waterTexture, shineTexture);
}

float waterWaveOffset(float weight, int col, int row, float t) {
    return kWaterWaveAmplitude * weight * 0.5f *
           (std::sin(0.75f * static_cast<float>(row) + t) + std::sin(static_cast<float>(col) + t));
}

float waterVertexHeight(const WaterSurface& s, const TerrainGridView& grid, int c, int r, float t) {
    if (!s.present) return grid.valid() ? grid.at(c, r).z : 0.0f;
    if (!s.waves) return s.level;
    float w = s.weightAt(c, r);
    if (w >= kWaterMinWeight && r != 0 && r != s.height) return s.level + waterWaveOffset(w, c, r, t);
    return grid.at(c, r).z;
}

namespace {

float surfaceHeight(const WaterSurface& s, const TerrainGridView& grid, float x, float y, float t) {
    if (!s.present) return grid.heightAt(x, y);
    if (!s.waves) return s.level;
    const int w = s.width, h = s.height;
    const float cx = std::min(std::max(x, 0.0f), static_cast<float>(w) * kHmapCellSize);
    const float cy = std::min(std::max(y, 0.0f), static_cast<float>(h) * kHmapCellSize);
    int ix = std::min(std::max(static_cast<int>(std::floor(cx / kHmapCellSize)), 0), w - 1);
    int iy = std::min(std::max(static_cast<int>(std::floor(cy / kHmapCellSize)), 0), h - 1);
    const float fx = cx / kHmapCellSize - static_cast<float>(ix);
    const float fy = cy / kHmapCellSize - static_cast<float>(iy);
    const float z00 = waterVertexHeight(s, grid, ix, iy, t), z10 = waterVertexHeight(s, grid, ix + 1, iy, t);
    const float z01 = waterVertexHeight(s, grid, ix, iy + 1, t), z11 = waterVertexHeight(s, grid, ix + 1, iy + 1, t);
    const float h0 = z00 + fx * (z10 - z00);
    const float h1 = z01 + fx * (z11 - z01);
    return h0 + fy * (h1 - h0);
}

} // namespace

WaterSample waterHeightAt(const WaterSurface& s, const TerrainGridView& grid, float x, float y, float t) {
    WaterSample out;
    out.height = surfaceHeight(s, grid, x, y, t);
    const Vec3 a{x - 10.0f, y + 15.0f, surfaceHeight(s, grid, x - 10.0f, y + 15.0f, t)};
    const Vec3 b{x + 10.0f, y + 15.0f, surfaceHeight(s, grid, x + 10.0f, y + 15.0f, t)};
    const Vec3 c{x, y - 25.0f, surfaceHeight(s, grid, x, y - 25.0f, t)};
    Vec3 n = cross(c - a, b - a);
    const float len = length(n);
    if (len > 1e-6f) out.normal = n * (1.0f / len);
    if (out.normal.z < 0.0f) out.normal = out.normal * -1.0f;
    return out;
}

WaterSample waterHeightAt(const WaterSurface& s, const Terrain& terrain, float x, float y, float t) {
    return waterHeightAt(s, TerrainGridView::of(terrain), x, y, t);
}

} // namespace as3d
