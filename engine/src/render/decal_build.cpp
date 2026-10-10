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

// A triangle clipped by the four edges of the rectangle has at most 7 corners; fixed storage,
// no allocation per terrain triangle (the 3DS drapes every shadow every frame).
struct Poly {
    P3 p[8];
    int n = 0;
};

// Clips a convex polygon against the half plane w(p) >= 0, where w is linear in xy.
template <typename F>
void clipPolygon(Poly& poly, F w) {
    Poly out;
    const int n = poly.n;
    for (int i = 0; i < n; i++) {
        const P3& a = poly.p[i];
        const P3& b = poly.p[(i + 1) % n];
        float wa = w(a), wb = w(b);
        bool ina = wa >= 0.0f, inb = wb >= 0.0f;
        if (ina && out.n < 8) out.p[out.n++] = a;
        if (ina != inb && out.n < 8) {
            float t = wa / (wa - wb);
            out.p[out.n++] = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
        }
    }
    poly = out;
}

void emitClipped(const GroundRect& rect, P3 a, P3 b, P3 c, std::vector<DecalVertex>& out) {
    Poly poly;
    poly.p[0] = a;
    poly.p[1] = b;
    poly.p[2] = c;
    poly.n = 3;
    auto sCoord = [&](const P3& p) { return (p.x - rect.origin.x) * rect.axisX.x + (p.y - rect.origin.y) * rect.axisX.y; };
    auto tCoord = [&](const P3& p) { return (p.x - rect.origin.x) * rect.axisY.x + (p.y - rect.origin.y) * rect.axisY.y; };
    // Shortcuts with the same output as the four clips: a triangle inside every edge passes each
    // clip unchanged (the same corners in the same order); one with every corner outside the
    // first edge clips to nothing; one with every corner clearly outside another edge too (the
    // margin covers the rounding of the corners the earlier clips create, which lie on the
    // triangle: their distance to the edge is a convex mix of the corners').
    {
        const float sa = sCoord(a), sb = sCoord(b), sc = sCoord(c);
        const float ta = tCoord(a), tb = tCoord(b), tc = tCoord(c);
        if (sa < 0.0f && sb < 0.0f && sc < 0.0f) return;
        constexpr float kOut = -0.05f; // world units, far above the rounding of map coordinates
        if (rect.sizeX - sa < kOut && rect.sizeX - sb < kOut && rect.sizeX - sc < kOut) return;
        if (ta < kOut && tb < kOut && tc < kOut) return;
        if (rect.sizeY - ta < kOut && rect.sizeY - tb < kOut && rect.sizeY - tc < kOut) return;
        if (sa >= 0.0f && sb >= 0.0f && sc >= 0.0f && rect.sizeX - sa >= 0.0f && rect.sizeX - sb >= 0.0f &&
            rect.sizeX - sc >= 0.0f && ta >= 0.0f && tb >= 0.0f && tc >= 0.0f && rect.sizeY - ta >= 0.0f &&
            rect.sizeY - tb >= 0.0f && rect.sizeY - tc >= 0.0f) {
            auto vert = [&](const P3& p) {
                Vec2 st = rect.coords(p.x, p.y);
                return DecalVertex{p.x, p.y, p.z, st.x, 1.0f - st.y};
            };
            out.push_back(vert(a));
            out.push_back(vert(b));
            out.push_back(vert(c));
            return;
        }
    }
    clipPolygon(poly, [&](const P3& p) { return sCoord(p); });
    if (poly.n < 3) return;
    clipPolygon(poly, [&](const P3& p) { return rect.sizeX - sCoord(p); });
    if (poly.n < 3) return;
    clipPolygon(poly, [&](const P3& p) { return tCoord(p); });
    if (poly.n < 3) return;
    clipPolygon(poly, [&](const P3& p) { return rect.sizeY - tCoord(p); });
    if (poly.n < 3) return;
    auto vert = [&](const P3& p) {
        Vec2 st = rect.coords(p.x, p.y);
        return DecalVertex{p.x, p.y, p.z, st.x, 1.0f - st.y};
    };
    for (int i = 1; i + 1 < poly.n; i++) {
        out.push_back(vert(poly.p[0]));
        out.push_back(vert(poly.p[i]));
        out.push_back(vert(poly.p[i + 1]));
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
