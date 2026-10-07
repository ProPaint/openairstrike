// Tests for the neutral terrain and water builders (terrain_build.h, water_build.h).
#include "doctest.h"

#include "as3d/terrain_build.h"
#include "as3d/water_build.h"
#include "test_data.h"

#include <algorithm>
#include <cmath>
#include <random>

using namespace as3d;

namespace {

struct Synthetic {
    LevelData level;
    TerrainStyle style;
    Terrain terrain;
};

void makeSynthetic(Synthetic& s, int w, int h, unsigned seed) {
    std::mt19937 rng(seed);
    s.level.width = static_cast<u32>(w);
    s.level.height = static_cast<u32>(h);
    s.level.cells.resize(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < s.level.cells.size(); ++i) {
        LevelCell& c = s.level.cells[i];
        c.height = static_cast<u8>(rng() % 120);
        if (i % 7 == 0) {
            c.tileSet = static_cast<u8>(1 + i % 2);
            c.tileIndex = static_cast<u8>(i % 5);
            c.tileRotation = static_cast<u8>((i % 4) * 3);
        }
    }
    s.style.hmin = 0.0f;
    s.style.hmax = 400.0f;
    s.style.sun[0] = s.style.sun[1] = s.style.sun[2] = 0.7f;
    s.style.sun[3] = 0.3f; s.style.sun[4] = 0.2f; s.style.sun[5] = 0.9f;
    s.style.sun[6] = s.style.sun[7] = s.style.sun[8] = 0.3f;
    REQUIRE(s.terrain.build(s.level, s.style));
}

std::map<int, std::pair<int, int>> atlases() { return {{1, {256, 256}}, {2, {128, 256}}}; }

// A direct transcription of the vertex shader's loop (kTerrainVertexSrc) on plain arrays.
void glslLoop(const float aPos[3], const float aColor[3], const float aNormal[3], const DynamicLight* lights, int n,
              float out[3]) {
    float col[3] = {aColor[0], aColor[1], aColor[2]};
    for (int i = 0; i < n; i++) {
        float v[3] = {lights[i].position.x - aPos[0], lights[i].position.y - aPos[1], lights[i].position.z - aPos[2]};
        float radius = lights[i].radius;
        float dist = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (radius <= 0.0f || dist > radius) continue;
        if (dist > 0.0f) for (float& x : v) x /= dist;
        float spot = 1.0f;
        if (lights[i].spot) {
            float c = -v[0] * lights[i].direction.x - v[1] * lights[i].direction.y - v[2] * lights[i].direction.z;
            if (c < lights[i].cosOuter) continue;
            if (c < lights[i].cosInner) spot = (c - lights[i].cosOuter) / (lights[i].cosInner - lights[i].cosOuter);
        }
        float nd = aNormal[0] * v[0] + aNormal[1] * v[1] + aNormal[2] * v[2];
        if (nd <= 0.0f) continue;
        float k = (0.7f * nd + 0.3f) * (radius - dist) / radius * spot;
        col[0] += lights[i].colour.x * k;
        col[1] += lights[i].colour.y * k;
        col[2] += lights[i].colour.z * k;
    }
    for (int k = 0; k < 3; k++) out[k] = std::min(col[k], 1.0f);
}

} // namespace

TEST_CASE("terrain build: chunk counts, vertex counts, indices and boxes (synthetic)") {
    Synthetic s;
    makeSynthetic(s, 20, 37, 1);
    TerrainGeometry g = buildTerrainGeometry(s.terrain, renderRules(GameId::AirStrike3D), atlases());
    REQUIRE(g.valid());
    CHECK(g.chunks.size() == 5); // ceil(37 / 8)
    size_t total = 0;
    for (size_t k = 0; k < g.chunks.size(); ++k) {
        const TerrainChunkGeometry& ch = g.chunks[k];
        CHECK(ch.firstRow == static_cast<int>(k) * 8);
        CHECK(ch.rows == std::min(8, 37 - ch.firstRow));
        CHECK(ch.verts.size() == static_cast<size_t>(ch.rows + 1) * 21);
        CHECK(ch.tris.size() == static_cast<size_t>(ch.rows) * 20 * 6);
        CHECK(ch.wire.size() == static_cast<size_t>(ch.rows) * 20 * 6);
        CHECK(ch.baseVertex == total);
        total += ch.verts.size();
        for (uint16_t i : ch.tris) CHECK(i < ch.verts.size());
        for (uint16_t i : ch.wire) CHECK(i < ch.verts.size());
        for (const TerrainVertex& v : ch.verts) {
            CHECK(v.pos[0] >= ch.bounds.min.x); CHECK(v.pos[0] <= ch.bounds.max.x);
            CHECK(v.pos[1] >= ch.bounds.min.y); CHECK(v.pos[1] <= ch.bounds.max.y);
            CHECK(v.pos[2] >= ch.bounds.min.z); CHECK(v.pos[2] <= ch.bounds.max.z);
        }
    }
    CHECK(g.vertexCount == total);
    REQUIRE(g.tiles.size() == 2);
    for (const TerrainTileGeometry& t : g.tiles) {
        CHECK(t.verts.size() == t.cells.size() * 6);
        int sum = 0;
        for (auto r : t.ranges) sum += r.second;
        CHECK(sum == static_cast<int>(t.verts.size()));
    }
    CHECK(terrainTileSets(s.level) == std::vector<int>{1, 2});
}

TEST_CASE("terrain build: mission 1 geometry") {
    AS3D_REQUIRE_DATA();
    Vfs vfs;
    if (!testdata::mountGamePaks(vfs)) return;
    LoadedLevel ll;
    std::string error;
    REQUIRE_MESSAGE(loadLevelByRef(vfs, "1", ll, &error), error);
    Terrain terrain;
    REQUIRE(terrain.build(ll.data, ll.style));
    const int W = terrain.width(), H = terrain.height();
    TerrainGeometry g = buildTerrainGeometry(terrain, renderRules(testdata::game().id), atlases());
    const int chunks = (H + 7) / 8;
    CHECK(g.chunks.size() == static_cast<size_t>(chunks));
    CHECK(g.vertexCount == static_cast<size_t>(H + chunks) * static_cast<size_t>(W + 1));
    CHECK(g.vertexCount <= 65535);
    for (const TerrainChunkGeometry& ch : g.chunks) {
        for (const TerrainVertex& v : ch.verts) {
            CHECK(v.pos[2] >= ch.bounds.min.z);
            CHECK(v.pos[2] <= ch.bounds.max.z);
        }
    }
}

TEST_CASE("terrain build: updateChunkHeights touches only the chunks covering the crater") {
    Synthetic s;
    makeSynthetic(s, 16, 40, 2);
    const RenderRules rules = renderRules(GameId::AirStrike3D);
    TerrainGeometry g = buildTerrainGeometry(s.terrain, rules, atlases());
    const TerrainGeometry before = g;

    VertexRect rc;
    rc.c0 = 4; rc.c1 = 7; rc.r0 = 18; rc.r1 = 20; // chunk 2 covers rows 16..24 only
    for (int r = rc.r0; r <= rc.r1; ++r)
        for (int c = rc.c0; c <= rc.c1; ++c) s.terrain.setVertexZ(c, r, -500.0f);
    std::vector<int> changed = updateChunkHeights(g, &rc, 1, TerrainGridView::of(s.terrain));
    REQUIRE(changed.size() == 1);
    CHECK(changed[0] == 2);
    for (size_t k = 0; k < g.chunks.size(); ++k) {
        if (k == 2) {
            CHECK(g.chunks[k].bounds.min.z <= -500.0f);
            continue;
        }
        CHECK(g.chunks[k].verts.size() == before.chunks[k].verts.size());
        for (size_t i = 0; i < g.chunks[k].verts.size(); ++i)
            CHECK(g.chunks[k].verts[i].pos[2] == before.chunks[k].verts[i].pos[2]);
        CHECK(g.chunks[k].bounds.min.z == before.chunks[k].bounds.min.z);
    }
    // The vertices of the crater changed, nothing else in the chunk but z of the rect.
    const TerrainChunkGeometry& ch = g.chunks[2];
    for (int lr = 0; lr <= ch.rows; ++lr) {
        for (int c = 0; c <= 16; ++c) {
            const int r = ch.firstRow + lr;
            const bool inside = r >= rc.r0 && r <= rc.r1 && c >= rc.c0 && c <= rc.c1;
            const TerrainVertex& v = ch.verts[static_cast<size_t>(lr) * 17 + c];
            if (inside) CHECK(v.pos[2] == -500.0f);
            CHECK(v.col[0] == before.chunks[2].verts[static_cast<size_t>(lr) * 17 + c].col[0]);
        }
    }
    // A rectangle on a shared row touches both chunks; an empty one none.
    VertexRect edge;
    edge.c0 = 0; edge.c1 = 1; edge.r0 = 8; edge.r1 = 8;
    changed = updateChunkHeights(g, &edge, 1, TerrainGridView::of(s.terrain));
    CHECK(changed == std::vector<int>{0, 1});
    VertexRect none;
    CHECK(updateChunkHeights(g, &none, 1, TerrainGridView::of(s.terrain)).empty());
    // Tile quads follow the heights.
    for (const TerrainTileGeometry& t : g.tiles) {
        for (size_t q = 0; q < t.cells.size(); ++q) {
            const int c = t.cells[q].first, r = t.cells[q].second;
            if (c < rc.c0 || c + 1 > rc.c1 || r < rc.r0 || r + 1 > rc.r1) continue;
            for (int j = 0; j < 6; ++j) CHECK(t.verts[q * 6 + static_cast<size_t>(j)].pos[2] == -500.0f);
        }
    }
}

TEST_CASE("terrain build: frustum cull keeps what a wide frustum sees and drops what is behind") {
    Synthetic s;
    makeSynthetic(s, 16, 64, 3);
    TerrainGeometry g = buildTerrainGeometry(s.terrain, renderRules(GameId::AirStrike3D), {});
    const Mat4 proj = perspective(20.0f, 1.0f, 10.0f, 100000.0f);
    // Camera far below the map looking along +y over its first chunks.
    Mat4 view = lookAt(Vec3{320.0f, -200.0f, 300.0f}, Vec3{320.0f, 600.0f, 0.0f}, Vec3{0, 0, 1});
    std::vector<int> vis = visibleTerrainChunks(g, proj * view);
    CHECK(!vis.empty());
    CHECK(vis.size() < g.chunks.size());
    CHECK(vis[0] == 0);
    Mat4 behind = lookAt(Vec3{320.0f, -200.0f, 300.0f}, Vec3{320.0f, -2000.0f, 0.0f}, Vec3{0, 0, 1});
    CHECK(visibleTerrainChunks(g, proj * behind).empty());
}

TEST_CASE("terrain build: CPU dynamic lights equal the shader loop") {
    Synthetic s;
    makeSynthetic(s, 16, 16, 4);
    TerrainGeometry g = buildTerrainGeometry(s.terrain, renderRules(GameId::AirStrike3D), {});
    std::mt19937 rng(77);
    auto uni = [&](float a, float b) { return a + (b - a) * static_cast<float>(rng() % 100000) / 100000.0f; };
    for (int trial = 0; trial < 6; ++trial) {
        std::vector<DynamicLight> lights;
        const int n = 1 + trial * 5;
        for (int i = 0; i < n; ++i) {
            Vec3 pos{uni(0, 640), uni(0, 640), uni(0, 300)};
            Vec3 col{uni(0, 2.5f), uni(0, 2.5f), uni(0, 2.5f)};
            float radius = uni(-10, 500);
            lights.push_back(i % 2 ? DynamicLight::spotLight(pos, col, radius, Vec3{uni(-1, 1), uni(-1, 1), -1.0f}, uni(20, 120))
                                   : DynamicLight::point(pos, col, radius));
        }
        for (const TerrainChunkGeometry& ch : g.chunks) {
            std::vector<uint8_t> rgba;
            terrainDynamicLightColors(ch, lights.data(), static_cast<int>(lights.size()), rgba);
            REQUIRE(rgba.size() == ch.verts.size() * 4);
            for (size_t i = 0; i < ch.verts.size(); ++i) {
                float ref[3];
                glslLoop(ch.verts[i].pos, ch.verts[i].col, ch.verts[i].nrm, lights.data(), n, ref);
                for (int k = 0; k < 3; ++k) {
                    CHECK(std::fabs(static_cast<float>(rgba[i * 4 + static_cast<size_t>(k)]) - ref[k] * 255.0f) <= 1.0f);
                }
                CHECK(rgba[i * 4 + 3] == 255);
            }
        }
    }
    // No lights: the baked colour.
    std::vector<uint8_t> rgba;
    terrainDynamicLightColors(g.chunks[0], nullptr, 0, rgba);
    for (size_t i = 0; i < g.chunks[0].verts.size(); ++i)
        CHECK(std::fabs(static_cast<float>(rgba[i * 4]) - g.chunks[0].verts[i].col[0] * 255.0f) <= 0.5f + 1e-3f);
}

TEST_CASE("water build: params, grid chunks and the CPU wave") {
    Synthetic s;
    makeSynthetic(s, 12, 21, 5);
    s.style.hasWater = true;
    s.style.waterTexture = "water.tga";
    s.style.waterLevel = 20.0f;
    s.style.waterAlpha = 0.5f;
    WaterParams wp = computeWaterParams(s.style, 0.0f);
    CHECK(wp.present);
    CHECK(wp.planeZ == doctest::Approx(20.0f));
    CHECK(wp.texOffset.x == doctest::Approx(0.0f));
    wp = computeWaterParams(s.style, 5.0f); // phase = pi / 2
    CHECK(wp.planeZ == doctest::Approx(21.0f).epsilon(1e-4));
    CHECK(wp.texOffset.x == doctest::Approx(0.4f).epsilon(1e-4));

    const TerrainGridView grid = TerrainGridView::of(s.terrain);
    float zmin = 1e30f, zmax = -1e30f;
    for (int r = 0; r <= 21; ++r)
        for (int c = 0; c <= 12; ++c) { zmin = std::min(zmin, grid.at(c, r).z); zmax = std::max(zmax, grid.at(c, r).z); }
    const float level = 0.5f * (zmin + zmax);
    WaterSurface surface = buildWaterSurface(grid, true, true, level, 0.8f, "a.tga", "b.tga");
    WaterGridGeometry wg = buildWaterGridGeometry(surface, grid);
    REQUIRE(wg.valid());
    CHECK(wg.chunks.size() == 3); // ceil(21 / 8)
    size_t verts = 0;
    int wet = 0;
    for (int r = 0; r < 21; ++r)
        for (int c = 0; c < 12; ++c) wet += surface.wet(c, r) ? 1 : 0;
    CHECK(wet > 0);
    CHECK(wg.wetCells == wet);
    for (const WaterGridChunk& ch : wg.chunks) {
        CHECK(ch.vertexCount == static_cast<size_t>(ch.rows + 1) * 13);
        CHECK(ch.firstVertex == verts);
        verts += ch.vertexCount;
        for (size_t i = 0; i < ch.indexCount; ++i) {
            const uint16_t idx = wg.indices[ch.firstIndex + i];
            CHECK(idx >= ch.firstVertex);
            CHECK(idx < ch.firstVertex + ch.vertexCount);
        }
        for (size_t i = 0; i < ch.vertexCount; ++i) {
            const WaterGridVertex& v = wg.verts[ch.firstVertex + i];
            CHECK(v.terrainZ >= ch.bounds.min.z);
            CHECK(v.terrainZ <= ch.bounds.max.z);
        }
    }
    CHECK(wg.verts.size() == verts);

    // The CPU wave equals waterVertexHeight (the simulation's) at every vertex.
    for (float t : {0.0f, 1.7f, 33.3f}) {
        std::vector<WaterGridAnimated> anim;
        animateWaterGrid(surface, wg.verts.data(), wg.verts.size(), t, anim);
        const WaterGridFrame f = computeWaterGridFrame(t);
        for (size_t i = 0; i < wg.verts.size(); ++i) {
            const int c = static_cast<int>(wg.verts[i].col), r = static_cast<int>(wg.verts[i].row);
            CHECK(anim[i].pos.z == doctest::Approx(waterVertexHeight(surface, grid, c, r, t)).epsilon(1e-4));
            CHECK(anim[i].pos.x == c * 40.0f);
            CHECK(anim[i].pos.y == r * 40.0f);
            CHECK(anim[i].alpha == doctest::Approx(surface.weightAt(c, r) * surface.opacity));
            CHECK(anim[i].uvBase.x == doctest::Approx(waterBaseUv(c, r, f).x));
            CHECK(anim[i].uvShine.y == doctest::Approx(waterShineUv(c, r, f).y));
        }
    }

    // A crater update re-reads the touched chunks only.
    VertexRect rc;
    rc.c0 = 2; rc.c1 = 3; rc.r0 = 10; rc.r1 = 11;
    s.terrain.setVertexZ(2, 10, -300.0f);
    std::vector<int> changed = updateWaterGridHeights(wg, surface, &rc, 1, TerrainGridView::of(s.terrain));
    CHECK(changed == std::vector<int>{1});
    CHECK(wg.chunks[1].bounds.min.z <= -300.0f);
    CHECK(wg.chunks[0].bounds.min.z > -300.0f);

    // Culling: a camera looking along the map sees chunks, one looking away none.
    const Mat4 proj = perspective(60.0f, 1.0f, 10.0f, 100000.0f);
    Mat4 view = lookAt(Vec3{240.0f, -200.0f, 300.0f}, Vec3{240.0f, 400.0f, 0.0f}, Vec3{0, 0, 1});
    CHECK(!visibleWaterChunks(wg, proj * view).empty());
    Mat4 away = lookAt(Vec3{240.0f, -200.0f, 300.0f}, Vec3{240.0f, -2000.0f, 0.0f}, Vec3{0, 0, 1});
    CHECK(visibleWaterChunks(wg, proj * away).empty());
}
