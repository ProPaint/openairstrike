// A read-only view of a terrain's vertex grid: (W+1) x (H+1) world positions, row-major,
// vertex (c, r) at index r·(W+1) + c (docs/spec/hmap.md "Vertex grid").
//
// The renderer's decals, skid trails and water, and waterHeightAt (as3d/water.h), read the
// heights through this view instead of a Terrain so that they follow heights that changed
// after the Terrain was built (TerraMorph, as2/rcsl-builtins-semantics.delta.md): today the
// viewer's --morph keeps its own copy of the heights because Terrain has no mutable access
// yet; once it has, TerrainGridView::of(terrain) sees the changed heights and nothing else
// needs to change. Header only, no GL.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "as3d/level.h"
#include "as3d/terrain.h"
#include "as3d/vec.h"

namespace as3d {

struct TerrainGridView {
    const Vec3* positions = nullptr;
    int width = 0;  // W, cells
    int height = 0; // H, cells

    static TerrainGridView of(const Terrain& t) {
        TerrainGridView v;
        if (!t.positions().empty()) {
            v.positions = t.positions().data();
            v.width = t.width();
            v.height = t.height();
        }
        return v;
    }
    bool valid() const { return positions && width > 0 && height > 0; }
    int vertsWide() const { return width + 1; }
    int vertsHigh() const { return height + 1; }
    const Vec3& at(int c, int r) const { return positions[static_cast<size_t>(r) * static_cast<size_t>(width + 1) + static_cast<size_t>(c)]; }

    // Bilinear height over the vertex grid, clamped to the map edges: the same rule as
    // Terrain::heightAt (our deviation from the original's reads past the edge).
    float heightAt(float worldX, float worldY) const {
        if (!valid()) return 0.0f;
        const float maxX = static_cast<float>(width) * kHmapCellSize;
        const float maxY = static_cast<float>(height) * kHmapCellSize;
        const float cx = std::min(std::max(worldX, 0.0f), maxX);
        const float cy = std::min(std::max(worldY, 0.0f), maxY);
        int ix = static_cast<int>(std::floor(cx / kHmapCellSize));
        int iy = static_cast<int>(std::floor(cy / kHmapCellSize));
        ix = std::min(std::max(ix, 0), width - 1);
        iy = std::min(std::max(iy, 0), height - 1);
        const float fx = cx / kHmapCellSize - static_cast<float>(ix);
        const float fy = cy / kHmapCellSize - static_cast<float>(iy);
        const float z00 = at(ix, iy).z, z10 = at(ix + 1, iy).z, z01 = at(ix, iy + 1).z, z11 = at(ix + 1, iy + 1).z;
        const float h0 = z00 + fx * (z10 - z00);
        const float h1 = z01 + fx * (z11 - z01);
        return h0 + fy * (h1 - h0);
    }
};

// A rectangle of grid vertices, inclusive: columns c0..c1, rows r0..r1 (what a TerraMorph
// stamp touched).
struct VertexRect {
    int c0 = 0, r0 = 0, c1 = -1, r1 = -1;
    bool empty() const { return c1 < c0 || r1 < r0; }
};

} // namespace as3d
