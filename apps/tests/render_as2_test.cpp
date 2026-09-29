// AirStrike 2's rendering (docs/spec/as2/render-pipeline.delta.md): the level file's water
// statement, the water grid and waterHeightAt, terrain re-upload after TerraMorph, the
// multiplied shadows, skid trails, environment coordinates, the render rules and headless
// renders of the sequel's levels. The formula tests need no GL and no data; the GL tests skip
// loudly without a headless GLES context, the level renders without AirStrike 2's data
// (selected explicitly, so they also run in the default as3d pass of tools/ci.sh).
#include "doctest.h"

#include "as3d/envmap.h"
#include "as3d/game_data.h"
#include "as3d/ground_marks.h"
#include "as3d/level.h"
#include "as3d/platform.h"
#include "as3d/render_rules.h"
#include "as3d/scene.h"
#include "as3d/shadow_render.h"
#include "as3d/skid_render.h"
#include "as3d/terrain.h"
#include "as3d/terrain_grid.h"
#include "as3d/terrain_render.h"
#include "as3d/ui.h"
#include "as3d/water.h"

#include "as3d/defs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>

#include "../viewer/level_render.h"
#include "test_data.h"

using namespace as3d;

namespace {

// ---------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------

// A W x H cell level; vertex heights (0..255 mapped 1:1 to z) from heightOf(col, row).
struct SyntheticTerrain {
    LevelData level;
    Terrain terrain;
    SyntheticTerrain(int w, int h, int (*heightOf)(int col, int row), bool water = false, float waterLevel = 0.0f) {
        level.width = static_cast<u32>(w);
        level.height = static_cast<u32>(h);
        level.cells.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
        for (int r = 0; r < h; r++)
            for (int c = 0; c < w; c++) level.cells[static_cast<size_t>(r) * w + c].height = static_cast<u8>(heightOf(c, r));
        TerrainStyle style;
        style.hmin = 0.0f;
        style.hmax = 255.0f;
        style.sun[0] = style.sun[1] = style.sun[2] = 0.6f;
        style.sun[5] = 1.0f;
        style.sun[6] = style.sun[7] = style.sun[8] = 0.4f;
        style.hasWater = water;
        style.waterLevel = waterLevel;
        style.waterAlpha = 0.5f;
        style.waterTexture = "gfx\\water\\none.tga";
        REQUIRE(terrain.build(level, style));
    }
};

int flat100(int, int) { return 100; }
// A basin: 20 at the centre columns, rising to 200 at the sides.
int basin(int c, int) { return std::min(200, 20 + std::abs(c - 16) * 12); }

// A vertex grid of W x H cells whose vertex (c, r) has height heightOf(c, r) exactly (the
// Terrain's resampler would move the samples).
struct GridHeights {
    std::vector<Vec3> positions;
    int width, height;
    GridHeights(int w, int h, int (*heightOf)(int col, int row)) : width(w), height(h) {
        for (int r = 0; r <= h; r++)
            for (int c = 0; c <= w; c++)
                positions.push_back({c * kHmapCellSize, r * kHmapCellSize, static_cast<float>(heightOf(c, r))});
    }
    TerrainGridView view() const { return TerrainGridView{positions.data(), width, height}; }
};

WaterSurface basinWater(const GridHeights& g, bool waves = true) {
    return buildWaterSurface(g.view(), true, waves, 100.0f, 0.5f, "base", "shine");
}

// An in-memory file source for DefDatabase tests.
class MemorySource : public IFileSource {
public:
    void add(const std::string& path, const std::string& text) { files_[normalizePath(path)] = text; }
    bool exists(const std::string& path) override { return files_.count(path) != 0; }
    bool read(const std::string& path, Blob& out) override {
        auto it = files_.find(path);
        if (it == files_.end()) return false;
        out.assign(it->second.begin(), it->second.end());
        return true;
    }
    void list(std::vector<std::string>& out) override {
        for (const auto& kv : files_) out.push_back(kv.first);
    }

private:
    std::map<std::string, std::string> files_;
};

GraphicsContext* glContext() {
    static std::unique_ptr<GraphicsContext> ctx = [] {
        GraphicsConfig cfg;
        cfg.width = 256;
        cfg.height = 256;
        cfg.headless = true;
        cfg.title = "as3d_tests";
        return createGraphicsContext(cfg);
    }();
    if (ctx) ctx->makeCurrent();
    return ctx.get();
}

#define REQUIRE_GL()                                                                           \
    if (!glContext()) {                                                                        \
        std::fprintf(stderr, "SKIPPED (no headless GLES context available): %s\n", __FILE__);  \
        return;                                                                                \
    }

// Screen pixel (x from the left, y from the top) of a world point.
Vec2 toScreen(const Mat4& viewProj, const Vec3& p, int w, int h) {
    Vec4 c = viewProj * Vec4{p.x, p.y, p.z, 1.0f};
    return {(c.x / c.w * 0.5f + 0.5f) * static_cast<float>(w), (1.0f - (c.y / c.w * 0.5f + 0.5f)) * static_cast<float>(h)};
}

int maxChannelDiff(const Image& a, const Image& b, size_t px) {
    int m = 0;
    for (int k = 0; k < 3; k++) m = std::max(m, std::abs(static_cast<int>(a.rgba[px * 4 + k]) - static_cast<int>(b.rgba[px * 4 + k])));
    return m;
}

} // namespace

// ---------------------------------------------------------------------------------------
// Render rules
// ---------------------------------------------------------------------------------------

TEST_CASE("render rules: the first game keeps today's renderer, the sequels switch per the delta") {
    const RenderRules& a = renderRules(GameId::AirStrike3D);
    CHECK(a.water == WaterStyle::FlatPlane);
    CHECK_FALSE(a.terrainCentralNormals);
    CHECK_FALSE(a.tileHalfTexelInset);
    CHECK(a.shadowEraseBelowGround);
    CHECK(a.shadowHeightExpCap == 7);
    CHECK(a.shadowMaxSide == 0);
    CHECK(a.shadowMipmaps);
    CHECK_FALSE(a.shadowMultiply);
    CHECK(a.spriteCulling);
    CHECK_FALSE(a.envViewNormal);
    CHECK_FALSE(a.missingTextureWhite);
    CHECK_FALSE(a.skidMarkPass);
    CHECK_FALSE(a.ui2dHalfPixelShift);
    for (GameId id : {GameId::AirStrike2, GameId::GulfThunder}) {
        const RenderRules& s = renderRules(id);
        CHECK(s.water == WaterStyle::WaveGrid);
        CHECK(s.terrainCentralNormals);
        CHECK(s.tileHalfTexelInset);
        CHECK_FALSE(s.shadowEraseBelowGround);
        CHECK(s.shadowHeightExpCap == 8);
        CHECK(s.shadowMaxSide == 256);
        CHECK_FALSE(s.shadowMipmaps);
        CHECK(s.shadowMultiply);
        CHECK_FALSE(s.spriteCulling);
        CHECK(s.envViewNormal);
        CHECK(s.missingTextureWhite);
        CHECK(s.skidMarkPass);
        CHECK(s.ui2dHalfPixelShift);
    }
    CHECK(&renderRulesFor(gameProfile(GameId::AirStrike2).rules) == &renderRules(GameId::AirStrike2));
    CHECK(&renderRulesFor(defaultGameRules()) == &renderRules(GameId::AirStrike3D));
    GameRules own;
    CHECK(&renderRulesFor(own) == &renderRules(GameId::AirStrike3D));
}

// ---------------------------------------------------------------------------------------
// The level water statement
// ---------------------------------------------------------------------------------------

TEST_CASE("level water statement: the first game's form and the sequels' two-texture form") {
    auto src = std::make_unique<MemorySource>();
    src->add("objects\\a.obj", "dummy\n{\n\ttype TYPE_MODEL\n}\n");
    src->add("maps\\levels.txt",
             "{\n\tid \"one\"\n\twater \"gfx\\water\\water_lake2.tga\" -68 0.4\n}\n"
             "{\n\tid \"two\"\n\twater \"gfx\\water\\water_ocean_1.tga\" \"gfx\\water\\water_ocean_1_shine.tga\" -97 0.5\n}\n"
             "{\n\tid \"three\"\n}\n");
    Vfs vfs;
    vfs.mount(std::move(src));
    DefDatabase db;
    REQUIRE(db.load(vfs));
    REQUIRE(db.levels().size() == 3);
    const LevelDef& one = db.levels()[0];
    CHECK(one.hasWater);
    CHECK(one.waterTexture == "gfx\\water\\water_lake2.tga");
    CHECK(one.waterShine.empty());
    CHECK(one.waterLevel == doctest::Approx(-68.0f));
    CHECK(one.waterAlpha == doctest::Approx(0.4f));
    const LevelDef& two = db.levels()[1];
    CHECK(two.hasWater);
    CHECK(two.waterTexture == "gfx\\water\\water_ocean_1.tga");
    CHECK(two.waterShine == "gfx\\water\\water_ocean_1_shine.tga");
    CHECK(two.waterLevel == doctest::Approx(-97.0f));
    CHECK(two.waterAlpha == doctest::Approx(0.5f));
    CHECK_FALSE(db.levels()[2].hasWater);
    // The canonical line only for the two-texture form (issue as2/250).
    CHECK(canonicalLevel(one).find("waterShine") == std::string::npos);
    CHECK(canonicalLevel(two).find("\nwaterShine=gfx\\water\\water_ocean_1_shine.tga\n") != std::string::npos);
}

// ---------------------------------------------------------------------------------------
// Water grid
// ---------------------------------------------------------------------------------------

TEST_CASE("water: depth weights fade over the first 16 units, wet cells, first-game plane") {
    GridHeights g(32, 16, basin);
    const TerrainGridView grid = g.view();
    WaterSurface s = basinWater(g);
    REQUIRE(s.present);
    CHECK(s.waves);
    CHECK(s.shineTexture == "shine");
    CHECK(s.opacity == doctest::Approx(0.5f));
    for (int c = 0; c <= 32; c++) {
        const float z = grid.at(c, 5).z;
        const float want = std::min(1.0f, std::max(0.0f, (100.0f - z) / 16.0f));
        CHECK(s.weightAt(c, 5) == doctest::Approx(want));
    }
    // Hand values of the basin (height 20 + 12·|c − 16|): 92 at c = 22 -> w = 0.5; 104 at
    // c = 23 -> 0; 20 at c = 16 -> 1.
    CHECK(grid.at(22, 5).z == doctest::Approx(92.0f));
    CHECK(s.weightAt(22, 5) == doctest::Approx(0.5f));
    CHECK(s.weightAt(23, 5) == doctest::Approx(0.0f));
    CHECK(s.weightAt(16, 5) == doctest::Approx(1.0f));
    // A cell is wet when one corner is at or below the level: c = 22 (corners 92 and 104) is,
    // c = 23 (104, 116) is not.
    CHECK(s.wet(22, 5));
    CHECK_FALSE(s.wet(23, 5));
    CHECK(s.wet(9, 5));
    CHECK_FALSE(s.wet(8, 5));

    // From a Terrain's own water fields; no water: no surface; the first game's surface is the
    // flat level.
    SyntheticTerrain st(32, 16, basin, true, 100.0f);
    WaterSurface fromTerrain = buildWaterSurface(st.terrain, true, "shine");
    CHECK(fromTerrain.present);
    CHECK(fromTerrain.level == doctest::Approx(100.0f));
    CHECK(fromTerrain.baseTexture == "gfx\\water\\none.tga");
    SyntheticTerrain dry(32, 16, basin);
    CHECK_FALSE(buildWaterSurface(dry.terrain, true, "").present);
    WaterSurface plane = buildWaterSurface(st.terrain, false, "");
    CHECK(waterHeightAt(plane, st.terrain, 640.0f, 200.0f, 3.0f).height == doctest::Approx(100.0f));
}

TEST_CASE("water: wave heights against the formula, not on the first and last rows nor on dry vertices") {
    GridHeights g(32, 16, basin);
    const TerrainGridView grid = g.view();
    WaterSurface s = basinWater(g);
    // Hand values (double precision): level + 16·w·0.5·(sin(0.75·row + T) + sin(col + T)).
    // Deep vertex (16, 4), w = 1, T = 0.5: 100 + 8·(sin(3.5) + sin(16.5)) = 91.499451.
    CHECK(waterVertexHeight(s, grid, 16, 4, 0.5f) == doctest::Approx(91.499451f).epsilon(1e-5));
    // Half-deep vertex (22, 7) (z 92), w = 0.5, T = 2: 100 + 4·(sin(7.25) + sin(24)) = 99.670010.
    CHECK(waterVertexHeight(s, grid, 22, 7, 2.0f) == doctest::Approx(99.67001f).epsilon(1e-5));
    CHECK(waterWaveOffset(1.0f, 0, 0, 0.0f) == doctest::Approx(0.0f));
    CHECK(waterWaveOffset(1.0f, 0, 2, kPi / 2.0f) == doctest::Approx(8.0f * (std::sin(1.5f + kPi / 2.0f) + 1.0f)));
    // First and last map rows and dry vertices sit on the terrain.
    CHECK(waterVertexHeight(s, grid, 16, 0, 0.5f) == doctest::Approx(grid.at(16, 0).z));
    CHECK(waterVertexHeight(s, grid, 16, 16, 0.5f) == doctest::Approx(grid.at(16, 16).z));
    CHECK(waterVertexHeight(s, grid, 30, 7, 0.5f) == doctest::Approx(grid.at(30, 7).z));
    // The amplitude never exceeds 16.
    for (int r = 1; r < 16; r++)
        for (int c = 0; c <= 32; c++)
            for (float t : {0.0f, 1.3f, 7.7f})
                if (s.weightAt(c, r) >= kWaterMinWeight) CHECK(std::fabs(waterVertexHeight(s, grid, c, r, t) - 100.0f) <= 16.0001f);
}

TEST_CASE("water: waterHeightAt is continuous, meets the vertices, and gives the surface normal") {
    GridHeights g(32, 16, basin);
    const TerrainGridView grid = g.view();
    WaterSurface s = basinWater(g);
    const float T = 4.2f;
    CHECK(waterHeightAt(s, grid, 16 * 40.0f, 6 * 40.0f, T).height == doctest::Approx(waterVertexHeight(s, grid, 16, 6, T)));
    // Walking across cell edges in 0.5-unit steps: no jump larger than the steepest slope
    // (at most 16 units over a 40-unit cell per wave, plus the shore) allows.
    float prev = waterHeightAt(s, grid, 300.0f, 100.0f, T).height;
    float maxStep = 0.0f;
    for (float x = 300.5f; x < 980.0f; x += 0.5f) {
        const float h = waterHeightAt(s, grid, x, 100.0f + (x - 300.0f) * 0.3f, T).height;
        maxStep = std::max(maxStep, std::fabs(h - prev));
        prev = h;
    }
    CHECK(maxStep < 0.5f * 3.0f);
    // A flat surface has an upright normal.
    WaterSurface plane = basinWater(g, false);
    WaterSample flat = waterHeightAt(plane, grid, 640.0f, 300.0f, T);
    CHECK(flat.normal.z == doctest::Approx(1.0f));
    // The normal is the plane through the three samples of G_AlignToWater.
    WaterSample w = waterHeightAt(s, grid, 640.0f, 300.0f, T);
    const Vec3 a{630.0f, 315.0f, waterHeightAt(s, grid, 630.0f, 315.0f, T).height};
    const Vec3 b{650.0f, 315.0f, waterHeightAt(s, grid, 650.0f, 315.0f, T).height};
    const Vec3 c{640.0f, 275.0f, waterHeightAt(s, grid, 640.0f, 275.0f, T).height};
    Vec3 n = normalize(cross(c - a, b - a));
    CHECK(w.normal.x == doctest::Approx(n.x));
    CHECK(w.normal.y == doctest::Approx(n.y));
    CHECK(w.normal.z == doctest::Approx(n.z));
    CHECK(w.normal.z > 0.5f);
    // Without water: the terrain.
    SyntheticTerrain dry(32, 16, basin);
    CHECK(waterHeightAt(buildWaterSurface(dry.terrain, true, ""), dry.terrain, 500.0f, 200.0f, T).height ==
          doctest::Approx(dry.terrain.heightAt(500.0f, 200.0f)));
}

TEST_CASE("water: grid chunk geometry and the two texture layers") {
    GridHeights g(32, 16, basin);
    const TerrainGridView grid = g.view();
    WaterSurface s = basinWater(g);
    std::vector<WaterGridVertex> v;
    std::vector<u16> idx;
    buildWaterChunk(s, grid, 8, 8, v, idx);
    CHECK(v.size() == static_cast<size_t>(9 * 33));
    CHECK(v[0].row == doctest::Approx(8.0f));
    CHECK(v[33 * 2 + 5].col == doctest::Approx(5.0f));
    CHECK(v[33 * 2 + 5].row == doctest::Approx(10.0f));
    CHECK(v[33 * 2 + 5].weight == doctest::Approx(s.weightAt(5, 10)));
    int wet = 0;
    for (int r = 8; r < 16; r++)
        for (int c = 0; c < 32; c++) wet += s.wet(c, r) ? 1 : 0;
    CHECK(idx.size() == static_cast<size_t>(wet) * 6);
    CHECK(wet == 8 * 14); // columns 9..22 of every row
    // The terrain's split: (v00, v10, v11), (v00, v11, v01), first wet cell (9, 8).
    CHECK(idx[0] == 9);
    CHECK(idx[1] == 10);
    CHECK(idx[2] == 33 + 10);
    CHECK(idx[5] == 33 + 9);
    // Layers at T = 5 (t = π/2), as2/render-corrections.md C1: base 1.5·(c/4, r/4) + (0.4,
    // 0.4 sin(π/4)), shine 2·(c/4, r/4) + (0.4 sin(π/4) + 0.2, −0.2 sin(π/8) − 0.3).
    WaterGridFrame f = computeWaterGridFrame(5.0f);
    CHECK(f.shineOffset.x == doctest::Approx(0.48284271f));
    CHECK(f.shineOffset.y == doctest::Approx(-0.37653669f));
    CHECK(f.baseOffset.x == doctest::Approx(0.4f));
    CHECK(f.baseOffset.y == doctest::Approx(0.28284271f));
    Vec2 b = waterBaseUv(4, 8, f), sh = waterShineUv(4, 8, f);
    CHECK(sh.x == doctest::Approx(2.0f + 0.48284271f));
    CHECK(sh.y == doctest::Approx(4.0f - 0.37653669f));
    CHECK(b.x == doctest::Approx(1.5f + 0.4f));
    CHECK(b.y == doctest::Approx(3.0f + 0.28284271f));
}

// ---------------------------------------------------------------------------------------
// Terrain: normals, tiles
// ---------------------------------------------------------------------------------------

TEST_CASE("terrain: central-difference normals and the sequels' colours; tile half-texel inset") {
    SyntheticTerrain st(8, 8, flat100); // for its style (sun 0.6, ambient 0.4)
    GridHeights g(8, 8, [](int c, int) { return c * 10; });
    const TerrainGridView grid = g.view();
    std::vector<Vec3> n;
    centralDifferenceNormals(grid, n);
    REQUIRE(n.size() == 81u);
    // Interior of a ramp rising 10 per 40 units in x: sx = 20/80 = 0.25, sy = 0.
    const Vec3 want = normalize(normalize(Vec3{-0.25f, 0.0f, 1.0f}) + Vec3{0.0f, 0.0f, 1.0f});
    const Vec3& got = n[static_cast<size_t>(4) * 9 + 3];
    CHECK(got.x == doctest::Approx(want.x));
    CHECK(got.z == doctest::Approx(want.z));
    // Border: difference 0.
    CHECK(n[0].x == doctest::Approx(0.0f));
    CHECK(n[0].z == doctest::Approx(1.0f));
    std::vector<TerrainVertexColor> col;
    sequelTerrainColours(grid, n, st.terrain.style(), col);
    // Flat border vertex: d = 1 -> (0.6 + 0.4)·255 = 255; slope facing away keeps the ambient.
    CHECK(col[0].r == 255);
    TerrainStyle away = st.terrain.style();
    away.sun[3] = 1.0f; // the sun low in +x: the ramp's normal leans to −x
    away.sun[5] = 0.0f;
    sequelTerrainColours(grid, n, away, col);
    CHECK(col[static_cast<size_t>(4) * 9 + 3].r == static_cast<u8>(static_cast<int>(0.4f * 255.0f)));

    Vec2 plain[4], inset[4];
    tileCornerUvs(256, 64, 1, 0, plain);
    tileCornerUvs(256, 64, 1, 0, inset, true);
    CHECK(inset[0].x == doctest::Approx(plain[0].x + 0.5f / 256.0f));
    CHECK(inset[1].x == doctest::Approx(plain[1].x - 0.5f / 256.0f));
    CHECK(inset[0].y == doctest::Approx(plain[0].y - 0.5f / 64.0f));
    CHECK(inset[2].y == doctest::Approx(plain[2].y + 0.5f / 64.0f));
}

// ---------------------------------------------------------------------------------------
// Shadows: sizes (no GL), blend (GL)
// ---------------------------------------------------------------------------------------

TEST_CASE("shadows: the sequels' texture size limits") {
    const RenderRules& a = renderRules(GameId::AirStrike3D);
    const RenderRules& s = renderRules(GameId::AirStrike2);
    ShadowBounds b;
    b.xmin = 0; b.xmax = 200; b.ymin = 0; b.ymax = 200;
    int w, h;
    shadowTextureSize(b, ShadowQuality::Normal, w, h, a);
    CHECK(w == 256);
    CHECK(h == 128);
    shadowTextureSize(b, ShadowQuality::Normal, w, h, s);
    CHECK(w == 256);
    CHECK(h == 256);
    shadowTextureSize(b, ShadowQuality::High, w, h, a);
    CHECK(w == 512);
    CHECK(h == 256);
    shadowTextureSize(b, ShadowQuality::High, w, h, s); // halved back to at most 256
    CHECK(w == 256);
    CHECK(h == 256);
    b.xmax = 40; b.ymax = 30;
    shadowTextureSize(b, ShadowQuality::Low, w, h, s);
    int wa, ha;
    shadowTextureSize(b, ShadowQuality::Low, wa, ha, a);
    CHECK(w == wa);
    CHECK(h == ha);
}

TEST_CASE("shadows: the sequels' multiplied shadow darkens by at most 40 percent and never brightens") {
    REQUIRE_GL();
    SyntheticTerrain st(32, 16, flat100);
    Vfs vfs; // no data: the terrain draws its magenta placeholder, lit
    TerrainRenderer tr;
    std::string err;
    REQUIRE(tr.build(st.terrain, vfs, &err, renderRules(GameId::AirStrike2)));
    ShadowRenderer sr;
    REQUIRE(sr.init(&err));
    sr.setRules(renderRules(GameId::AirStrike2));
    // A box standing on the ground, sun straight above.
    ModelData box;
    for (int i = 0; i < 8; i++) box.positions.push_back({(i & 1) ? 60.0f : -60.0f, (i & 2) ? 40.0f : -40.0f, (i & 4) ? 50.0f : -10.0f});
    box.uvs.push_back({0.5f, 0.5f});
    const int quads[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
    for (auto& q : quads) {
        Face f1, f2;
        f1.v[0] = static_cast<u16>(q[0]); f1.v[1] = static_cast<u16>(q[1]); f1.v[2] = static_cast<u16>(q[2]);
        f2.v[0] = static_cast<u16>(q[0]); f2.v[1] = static_cast<u16>(q[2]); f2.v[2] = static_cast<u16>(q[3]);
        box.faces.push_back(f1);
        box.faces.push_back(f2);
    }
    ShadowMap map;
    REQUIRE(sr.generate(box, nullptr, ShadowKind::Projected, Vec3{0.0f, 0.0f, 1.0f}, 0, ShadowQuality::Normal, map));
    // No ground erase: the part below z = 0 casts too (the silhouette covers the full box).
    CHECK(map.bounds.xmin == -60);
    CHECK(map.bounds.xmax == 60);

    RenderTarget rt;
    REQUIRE(rt.create(128, 128, 0));
    Camera cam;
    cam.eye = {640.0f, 320.0f, 900.0f};
    cam.target = {640.0f, 320.0f, 100.0f};
    cam.worldUp = {0.0f, 1.0f, 0.0f};
    cam.fovYDegrees = 40.0f;
    cam.aspect = 1.0f;
    cam.nearPlane = 10.0f;
    cam.farPlane = 3000.0f;
    TerrainViewParams tv;
    tv.view = cam.viewMatrix();
    tv.projection = cam.projMatrix();
    DecalViewParams dv;
    dv.view = tv.view;
    dv.projection = tv.projection;
    Image before, after;
    rt.bind();
    clear({0, 0, 0, 1}, true);
    tr.render(tv);
    REQUIRE(rt.readPixels(before));
    ShadowInstance inst;
    inst.map = &map;
    inst.origin = {640.0f, 320.0f, 100.0f};
    rt.bind();
    sr.draw(st.terrain, &inst, 1, dv);
    REQUIRE(rt.readPixels(after));
    int darker = 0, brighter = 0, tooDark = 0;
    for (size_t i = 0; i < static_cast<size_t>(128 * 128); i++) {
        for (int k : {0, 2}) {
            const int b0 = before.rgba[i * 4 + k], a0 = after.rgba[i * 4 + k];
            if (a0 > b0 + 1) brighter++;
            if (a0 < b0 - 1) darker++;
            if (b0 > 40 && a0 < static_cast<int>(0.6f * static_cast<float>(b0)) - 2) tooDark++;
        }
    }
    CHECK(darker > 100);
    CHECK(brighter == 0);
    CHECK(tooDark == 0);
    glContext()->makeCurrent();
}

// ---------------------------------------------------------------------------------------
// Terrain re-upload after TerraMorph (GL)
// ---------------------------------------------------------------------------------------

TEST_CASE("terrain update re-uploads exactly the touched chunks and changes pixels only there") {
    REQUIRE_GL();
    SyntheticTerrain st(32, 64, flat100);
    Vfs vfs;
    TerrainRenderer tr;
    std::string err;
    REQUIRE(tr.build(st.terrain, vfs, &err, renderRules(GameId::AirStrike2)));
    REQUIRE(tr.chunkCount() == 8);
    std::vector<Vec3> heights = st.terrain.positions();
    const TerrainGridView grid{heights.data(), 32, 64};

    // Chunk counts: rows 20..22 lie in chunk 2 only (rows 16..24); a rectangle on a chunk
    // boundary row touches both chunks that share it.
    VertexRect inside{10, 20, 12, 22}, boundary{0, 16, 3, 16}, twoChunks{5, 23, 6, 25}, none;
    tr.update(&inside, 1, grid);
    CHECK(tr.lastUpdatedChunks() == 1);
    tr.update(&boundary, 1, grid);
    CHECK(tr.lastUpdatedChunks() == 2);
    tr.update(&twoChunks, 1, grid);
    CHECK(tr.lastUpdatedChunks() == 2);
    tr.update(&none, 1, grid);
    CHECK(tr.lastUpdatedChunks() == 0);
    VertexRect both[2] = {inside, VertexRect{0, 60, 32, 64}};
    tr.update(both, 2, grid);
    CHECK(tr.lastUpdatedChunks() == 2);

    RenderTarget rt;
    const int S = 256;
    REQUIRE(rt.create(S, S, 0));
    Camera cam;
    cam.eye = {640.0f, 1280.0f, 3000.0f};
    cam.target = {640.0f, 1280.0f, 0.0f};
    cam.worldUp = {0.0f, 1.0f, 0.0f};
    cam.fovYDegrees = 60.0f;
    cam.aspect = 1.0f;
    cam.nearPlane = 100.0f;
    cam.farPlane = 6000.0f;
    TerrainViewParams tv;
    tv.view = cam.viewMatrix();
    tv.projection = cam.projMatrix();
    tv.fogColour = {0.0f, 1.0f, 0.0f};
    tv.fogStart = 2700.0f; // the depth reaches the colour through the fog
    tv.fogEnd = 3100.0f;
    Image before, after;
    rt.bind();
    clear({0, 0, 0, 1}, true);
    tr.render(tv);
    REQUIRE(rt.readPixels(before));

    // Raise the vertices of columns 10..12, rows 20..22 by 150 (a stamp's effect).
    for (int r = 20; r <= 22; r++)
        for (int c = 10; c <= 12; c++) heights[static_cast<size_t>(r) * 33 + c].z += 150.0f;
    tr.update(&inside, 1, grid);
    CHECK(tr.lastUpdatedChunks() == 1);
    rt.bind();
    clear({0, 0, 0, 1}, true);
    tr.render(tv);
    REQUIRE(rt.readPixels(after));
    // Changed pixels only over the cells around the raised vertices (columns 9..12, rows
    // 19..22, i.e. x 360..520, y 760..920), projected with the raised height.
    const Mat4 vp = tv.projection * tv.view;
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (float x : {360.0f, 520.0f})
        for (float y : {760.0f, 920.0f})
            for (float z : {100.0f, 250.0f}) {
                Vec2 p = toScreen(vp, {x, y, z}, S, S);
                x0 = std::min(x0, p.x); x1 = std::max(x1, p.x);
                y0 = std::min(y0, p.y); y1 = std::max(y1, p.y);
            }
    int inChanged = 0, outChanged = 0;
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            const size_t i = static_cast<size_t>(y) * S + x;
            if (maxChannelDiff(before, after, i) <= 2) continue;
            const bool in = x >= x0 - 2 && x <= x1 + 2 && y >= y0 - 2 && y <= y1 + 2;
            (in ? inChanged : outChanged)++;
        }
    CHECK(inChanged > 50);
    CHECK(outChanged == 0);
    glContext()->makeCurrent();
}

// ---------------------------------------------------------------------------------------
// Skid trails
// ---------------------------------------------------------------------------------------

TEST_CASE("skid trails: sections, heights, texture coordinates, fade and the section cap") {
    SyntheticTerrain st(8, 8, [](int c, int) { return 50 + c * 5; });
    const TerrainGridView grid = TerrainGridView::of(st.terrain);
    SkidTrailParams p = skidTrailParams(gameProfile(GameId::AirStrike2).rules);
    CHECK(p.maxTrails == 64);
    CHECK(p.maxNodes == 23);
    CHECK(p.fadeStart == doctest::Approx(5.0f));
    CHECK(p.life == doctest::Approx(10.0f));
    CHECK(p.heightOffset == doctest::Approx(2.0f));
    CHECK(skidNodeAlpha(0.0f, p) == doctest::Approx(1.0f));
    CHECK(skidNodeAlpha(5.0f, p) == doctest::Approx(1.0f));
    CHECK(skidNodeAlpha(7.5f, p) == doctest::Approx(0.5f));
    CHECK(skidNodeAlpha(10.0f, p) == doctest::Approx(0.0f));
    CHECK(skidNodeAlpha(12.0f, p) == doctest::Approx(0.0f));

    SkidTrail t;
    t.texture = "gfx/marks/jeepmark1.tga";
    for (int k = 0; k < 3; k++) {
        SkidNode n;
        n.position = {100.0f, 100.0f + 20.0f * k};
        n.direction = {0.0f, 1.0f}; // driving along +y: the section runs along +x
        n.width = 16.0f;
        n.distance = 20.0f * k;
        n.age = 6.0f - 0.5f * k;
        t.nodes.push_back(n);
    }
    std::vector<SkidVertex> v;
    REQUIRE(buildSkidTrailStrip(t, grid, p, v) == 6);
    CHECK(v[0].x == doctest::Approx(92.0f)); // u = 0 on the a − w/2 side
    CHECK(v[1].x == doctest::Approx(108.0f));
    CHECK(v[0].u == doctest::Approx(0.0f));
    CHECK(v[1].u == doctest::Approx(1.0f));
    CHECK(v[4].v == doctest::Approx(40.0f / 16.0f));
    CHECK(v[0].z == doctest::Approx(grid.heightAt(92.0f, 100.0f) + 2.0f));
    CHECK(v[1].z == doctest::Approx(grid.heightAt(108.0f, 100.0f) + 2.0f));
    CHECK(v[0].alpha == doctest::Approx(0.8f));
    CHECK(v[5].alpha == doctest::Approx(1.0f));
    // Forward winding: the first triangle (s0 left, s0 right, s1 left) faces up.
    const float cz = (v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[1].y - v[0].y) * (v[2].x - v[0].x);
    CHECK(cz > 0.0f);
    // Cap: 30 sections keep the newest 23.
    for (int k = 3; k < 30; k++) {
        SkidNode n = t.nodes.back();
        n.position.y += 20.0f;
        n.distance += 20.0f;
        t.nodes.push_back(n);
    }
    v.clear();
    CHECK(buildSkidTrailStrip(t, grid, p, v) == 46);
    CHECK(v[0].y == doctest::Approx(100.0f + 20.0f * 7));
    SkidTrail one;
    one.nodes.resize(1);
    v.clear();
    CHECK(buildSkidTrailStrip(one, grid, p, v) == 0);
}

TEST_CASE("skid trails: the renderer draws at most the pool size") {
    REQUIRE_GL();
    SyntheticTerrain st(8, 8, flat100);
    Vfs vfs;
    ResourceCache cache(vfs);
    SkidTrailRenderer r;
    std::string err;
    SkidTrailParams p = skidTrailParams(gameProfile(GameId::AirStrike2).rules);
    REQUIRE(r.init(cache, p, &err));
    std::vector<SkidTrail> trails(70);
    for (SkidTrail& t : trails) {
        t.texture = "gfx/marks/none.tga";
        for (int k = 0; k < 30; k++) {
            SkidNode n;
            n.position = {100.0f, 20.0f + 5.0f * k};
            t.nodes.push_back(n);
        }
    }
    DecalViewParams dv;
    dv.view = lookAt({160, 160, 500}, {160, 160, 0}, {0, 1, 0});
    dv.projection = perspective(60.0f, 1.0f, 1.0f, 2000.0f);
    r.draw(trails.data(), trails.size(), TerrainGridView::of(st.terrain), dv);
    CHECK(r.lastTrailCount() == 64);
    CHECK(r.lastVertexCount() == static_cast<size_t>(64 * 23 * 2));
    glContext()->makeCurrent();
}

// ---------------------------------------------------------------------------------------
// Environment coordinates, 2D layer
// ---------------------------------------------------------------------------------------

TEST_CASE("environment coordinates: the view-space folded normal, scale included") {
    const Mat4 I = Mat4::identity();
    Vec2 uv = viewNormalEnvUv(I, I, {1.0f, 0.0f, 0.0f});
    CHECK(uv.x == doctest::Approx(1.0f));
    CHECK(uv.y == doctest::Approx(0.5f));
    uv = viewNormalEnvUv(I, I, {-1.0f, 0.0f, 0.0f}); // folded to |n|
    CHECK(uv.x == doctest::Approx(1.0f));
    uv = viewNormalEnvUv(I, I, {0.0f, 0.0f, 1.0f});
    CHECK(uv.x == doctest::Approx(0.5f));
    CHECK(uv.y == doctest::Approx(0.5f));
    uv = viewNormalEnvUv(I, scale(Vec3{2.0f, 2.0f, 2.0f}), {0.0f, 1.0f, 0.0f}); // spreads and wraps
    CHECK(uv.y == doctest::Approx(1.5f));
    // A view turned 90 degrees about z: world +x appears as view -y.
    Mat4 view = rotationZ(90.0f);
    Vec3 n = transformDirection(view, Vec3{1.0f, 0.0f, 0.0f});
    uv = viewNormalEnvUv(view, I, {1.0f, 0.0f, 0.0f});
    CHECK(uv.x == doctest::Approx(0.5f + 0.5f * n.x));
    CHECK(uv.y == doctest::Approx(0.5f + 0.5f * n.y));
}

TEST_CASE("2D layer: half-pixel shift and rotation are additions that leave the default unchanged") {
    ui::Renderer2D r;
    r.begin(800, 600);
    ui::Quad q;
    q.x = 100; q.y = 50; q.w = 40; q.h = 20;
    float px[4], py[4];
    r.quadCorners(q, px, py);
    CHECK(px[0] == doctest::Approx(100.0f));
    CHECK(py[0] == doctest::Approx(50.0f));
    CHECK(px[2] == doctest::Approx(140.0f));
    CHECK(py[2] == doctest::Approx(70.0f));
    r.setHalfPixelShift(true);
    r.quadCorners(q, px, py);
    CHECK(px[0] == doctest::Approx(99.5f));
    CHECK(py[0] == doctest::Approx(49.5f));
    r.setHalfPixelShift(false);
    q.rotation = 90.0f; // about the centre (120, 60): the top-left corner goes to (130, 40)
    r.quadCorners(q, px, py);
    CHECK(px[0] == doctest::Approx(130.0f));
    CHECK(py[0] == doctest::Approx(40.0f));
    CHECK(px[2] == doctest::Approx(110.0f));
    CHECK(py[2] == doctest::Approx(80.0f));
}

// ---------------------------------------------------------------------------------------
// Headless renders of AirStrike 2's levels
// ---------------------------------------------------------------------------------------

TEST_CASE("AirStrike 2 levels render with visible water, no black lake bed and no magenta") {
    const GameProfile& as2 = gameProfile(GameId::AirStrike2);
    const GameData data = locateGameData(testdata::root(), as2);
    if (data.paks.empty()) {
        std::fprintf(stderr, "SKIPPED (no AirStrike 2 data under %s): %s\n", testdata::root().c_str(), __FILE__);
        return;
    }
    REQUIRE_GL();
    Vfs vfs;
    for (const std::string& path : data.paks) {
        auto src = makePakSource(openFileStream(path));
        REQUIRE(src);
        vfs.mount(std::move(src));
    }
    DefDatabase db;
    REQUIRE(db.load(vfs));
    ResourceCache cache(vfs);
    MeshRenderer renderer;
    std::string error;
    REQUIRE(renderer.init(&error));

    struct Case { const char* level; float scroll; float minBlue; };
    // Mission 3's lake (the delta's "lake beds render black" case), mission 4's desert water,
    // and mission 1 without water (all three daytime levels).
    const Case cases[] = {{"3", 6300.0f, 0.15f}, {"4", 1500.0f, -1.0f}, {"1", 2000.0f, -1.0f}};
    for (const Case& c : cases) {
        INFO("as2 level ", c.level, " scroll ", c.scroll);
        viewer::LevelRenderOptions o;
        o.width = 320;
        o.height = 240;
        o.scroll = c.scroll;
        o.msaa = 0;
        o.game = GameId::AirStrike2;
        o.time = 3.0f;
        Image img;
        viewer::LevelRenderStats stats;
        REQUIRE(viewer::renderLevel(vfs, db, cache, renderer, c.level, o, img, &stats, error));
        glContext()->makeCurrent();
        int blue = 0, black = 0, magenta = 0;
        const int n = img.width * img.height;
        for (int i = 0; i < n; i++) {
            const int r = img.rgba[i * 4], g = img.rgba[i * 4 + 1], b = img.rgba[i * 4 + 2];
            if (b > r + 12 && b >= g) blue++;
            if (r < 10 && g < 10 && b < 10) black++;
            if (r > 225 && g < 40 && b > 225) magenta++;
        }
        CHECK(stats.missingTextures == 0);
        CHECK(magenta == 0);
        CHECK(black < n / 50);
        if (c.minBlue > 0.0f) {
            CHECK(stats.waterGrid);
            CHECK(stats.wetCells > 100);
            CHECK(stats.waterChunks > 0);
            CHECK(static_cast<float>(blue) / static_cast<float>(n) > c.minBlue);
        }
    }
}
