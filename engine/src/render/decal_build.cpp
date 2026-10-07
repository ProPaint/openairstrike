// Terrain-following decals, backend neutral: geometry (rectangle clipped to the terrain grid)
// and the draw runs of marks and shadows. docs/spec/render-pipeline.md 3.4 (marks), 5.3 and 5.4
// (shadows draped over the terrain).
#include "as3d/ground_marks.h"

#include <algorithm>
#include <cmath>

namespace as3d {

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

GroundRect GroundRect::axisAligned(float x0, float y0, float x1, float y1) {
    GroundRect r;
    r.origin = {x0, y0};
    r.sizeX = x1 - x0;
    r.sizeY = y1 - y0;
    return r;
}

GroundRect GroundRect::rotated(const Vec2& pivot, float yawDegrees, float xmin, float ymin, float xmax, float ymax) {
    float a = degToRad(yawDegrees);
    float c = std::cos(a), s = std::sin(a);
    GroundRect r;
    r.axisX = {c, s};
    r.axisY = {-s, c};
    r.origin = {pivot.x + xmin * c - ymin * s, pivot.y + xmin * s + ymin * c};
    r.sizeX = xmax - xmin;
    r.sizeY = ymax - ymin;
    return r;
}

Vec2 GroundRect::coords(float x, float y) const {
    Vec2 d{x - origin.x, y - origin.y};
    return {sizeX != 0.0f ? dot(d, axisX) / sizeX : 0.0f, sizeY != 0.0f ? dot(d, axisY) / sizeY : 0.0f};
}

namespace {

struct P3 {
    float x, y, z;
};

// Clips a convex polygon against the half plane w(p) >= 0, where w is linear in xy.
template <typename F>
void clipPolygon(std::vector<P3>& poly, F w) {
    std::vector<P3> out;
    size_t n = poly.size();
    for (size_t i = 0; i < n; i++) {
        const P3& a = poly[i];
        const P3& b = poly[(i + 1) % n];
        float wa = w(a), wb = w(b);
        bool ina = wa >= 0.0f, inb = wb >= 0.0f;
        if (ina) out.push_back(a);
        if (ina != inb) {
            float t = wa / (wa - wb);
            out.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t});
        }
    }
    poly.swap(out);
}

void emitClipped(const GroundRect& rect, P3 a, P3 b, P3 c, std::vector<DecalVertex>& out) {
    std::vector<P3> poly{a, b, c};
    auto sCoord = [&](const P3& p) { return (p.x - rect.origin.x) * rect.axisX.x + (p.y - rect.origin.y) * rect.axisX.y; };
    auto tCoord = [&](const P3& p) { return (p.x - rect.origin.x) * rect.axisY.x + (p.y - rect.origin.y) * rect.axisY.y; };
    clipPolygon(poly, [&](const P3& p) { return sCoord(p); });
    if (poly.size() < 3) return;
    clipPolygon(poly, [&](const P3& p) { return rect.sizeX - sCoord(p); });
    if (poly.size() < 3) return;
    clipPolygon(poly, [&](const P3& p) { return tCoord(p); });
    if (poly.size() < 3) return;
    clipPolygon(poly, [&](const P3& p) { return rect.sizeY - tCoord(p); });
    if (poly.size() < 3) return;
    auto vert = [&](const P3& p) {
        Vec2 st = rect.coords(p.x, p.y);
        return DecalVertex{p.x, p.y, p.z, st.x, 1.0f - st.y};
    };
    for (size_t i = 1; i + 1 < poly.size(); i++) {
        out.push_back(vert(poly[0]));
        out.push_back(vert(poly[i]));
        out.push_back(vert(poly[i + 1]));
    }
}

} // namespace

void buildTerrainDecal(const Terrain& terrain, const GroundRect& rect, std::vector<DecalVertex>& out) {
    buildTerrainDecal(TerrainGridView::of(terrain), rect, out);
}

void buildTerrainDecal(const TerrainGridView& terrain, const GroundRect& rect, std::vector<DecalVertex>& out) {
    if (!terrain.valid() || rect.sizeX <= 0.0f || rect.sizeY <= 0.0f) return;
    Vec2 corners[4] = {rect.origin,
                       rect.origin + rect.axisX * rect.sizeX,
                       rect.origin + rect.axisX * rect.sizeX + rect.axisY * rect.sizeY,
                       rect.origin + rect.axisY * rect.sizeY};
    float minX = corners[0].x, maxX = corners[0].x, minY = corners[0].y, maxY = corners[0].y;
    for (int i = 1; i < 4; i++) {
        minX = std::min(minX, corners[i].x); maxX = std::max(maxX, corners[i].x);
        minY = std::min(minY, corners[i].y); maxY = std::max(maxY, corners[i].y);
    }
    int W = terrain.width, H = terrain.height;
    int c0 = std::max(0, static_cast<int>(std::floor(minX / kHmapCellSize)));
    int c1 = std::min(W - 1, static_cast<int>(std::floor(maxX / kHmapCellSize)));
    int r0 = std::max(0, static_cast<int>(std::floor(minY / kHmapCellSize)));
    int r1 = std::min(H - 1, static_cast<int>(std::floor(maxY / kHmapCellSize)));
    int vw = terrain.vertsWide();
    const Vec3* pos = terrain.positions;
    for (int r = r0; r <= r1; r++) {
        for (int c = c0; c <= c1; c++) {
            const Vec3& v00 = pos[static_cast<size_t>(r) * vw + c];
            const Vec3& v10 = pos[static_cast<size_t>(r) * vw + c + 1];
            const Vec3& v01 = pos[static_cast<size_t>(r + 1) * vw + c];
            const Vec3& v11 = pos[static_cast<size_t>(r + 1) * vw + c + 1];
            // The terrain renderer's triangles: (v00, v10, v11) and (v00, v11, v01).
            emitClipped(rect, {v00.x, v00.y, v00.z}, {v10.x, v10.y, v10.z}, {v11.x, v11.y, v11.z}, out);
            emitClipped(rect, {v00.x, v00.y, v00.z}, {v11.x, v11.y, v11.z}, {v01.x, v01.y, v01.z}, out);
        }
    }
}


// ---------------------------------------------------------------------------
// Draw runs
// ---------------------------------------------------------------------------

MaterialKey decalMaterialKey(DecalBlend blend) {
    return blendKey(renderBlendOf(blend), true, false, CullMode::Back, true);
}

void buildDecalRuns(const std::vector<DecalBatch>& batches, std::vector<DrawRun>& runs) {
    for (const DecalBatch& b : batches) {
        if (!b.texture || b.count == 0) continue;
        DrawRun r;
        r.key = decalMaterialKey(b.blend);
        r.texture = b.texture;
        r.first = static_cast<int>(b.first);
        r.count = static_cast<int>(b.count);
        r.tint = {b.colour.x, b.colour.y, b.colour.z, b.alpha};
        appendDrawRun(runs, r);
    }
}

} // namespace as3d
