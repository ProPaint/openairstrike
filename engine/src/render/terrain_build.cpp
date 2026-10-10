// The terrain's CPU geometry (as3d/terrain_build.h): chunk meshes, tile quads, base textures,
// the crater update, the frustum cull and the dynamic-light loop of the vertex shader.
#include "as3d/terrain_build.h"

#include <algorithm>
#include <cmath>

namespace as3d {

void generateBaseTextureRgb(const LevelData& level, const Image textures[4], int block, std::vector<u8>& rgb) {
    int w = static_cast<int>(level.width), h = static_cast<int>(level.height);
    int rowStart = block * kBaseTextureRows;
    int sh = std::min(kBaseTextureRows, h - rowStart);
    rgb.assign(static_cast<size_t>(kBaseTextureSize) * kBaseTextureSize * 3, 0);
    if (sh < 1 || w < 1) return;
    std::vector<u8> plane(static_cast<size_t>(w) * static_cast<size_t>(h));
    for (size_t i = 0; i < plane.size(); i++) plane[i] = level.cells[i].height;
    std::vector<u8> hs(static_cast<size_t>(kBaseTextureSize) * kBaseTextureSize);
    if (sh == 1) {
        for (auto& v : hs) v = plane[static_cast<size_t>(rowStart) * w];
    } else {
        resampleHeightField(plane.data(), w, sh, static_cast<size_t>(rowStart) * static_cast<size_t>(w), hs.data(),
                            kBaseTextureSize, kBaseTextureSize);
    }
    // Per height value (a byte): the two textures and the blend factor, as the per-texel
    // formula t = h / band, i0 = clamp(floor t), i1 = clamp(ceil t), fr = t - floor t (tabulated:
    // the same double operations, the same results).
    struct Blend {
        int i0, i1;
        double fr, omf;
    };
    Blend blend[256];
    for (int v = 0; v < 256; v++) {
        double t = static_cast<double>(v) / static_cast<double>(kTerrainHeightBand);
        Blend& b = blend[v];
        b.i0 = std::min(std::max(static_cast<int>(std::floor(t)), 0), 3);
        b.i1 = std::min(std::max(static_cast<int>(std::ceil(t)), 0), 3);
        b.fr = t - std::floor(t);
        b.omf = 1.0 - b.fr;
    }
    // The original reads TGA rows in file order, bottom row first (render-pipeline.md
    // section 0); Image row 0 is the top, so index from the bottom.
    std::vector<size_t> colOff[4];
    for (int k = 0; k < 4; k++) {
        const Image& tx = textures[k];
        colOff[k].resize(kBaseTextureSize);
        for (int c = 0; c < kBaseTextureSize; c++)
            colOff[k][static_cast<size_t>(c)] = tx.width > 0 ? static_cast<size_t>(c % tx.width) * 4 : 0;
    }
    for (int r = 0; r < kBaseTextureSize; r++) {
        size_t rowOff[4];
        for (int k = 0; k < 4; k++) {
            const Image& tx = textures[k];
            rowOff[k] = tx.height > 0 ? static_cast<size_t>(tx.height - 1 - r % tx.height) * static_cast<size_t>(tx.width) * 4 : 0;
        }
        const u8* hrow = &hs[static_cast<size_t>(r) * kBaseTextureSize];
        u8* out = &rgb[static_cast<size_t>(r) * kBaseTextureSize * 3];
        for (int c = 0; c < kBaseTextureSize; c++, out += 3) {
            const Blend& b = blend[hrow[c]];
            const u8* pa = textures[b.i0].rgba.data() + rowOff[b.i0] + colOff[b.i0][static_cast<size_t>(c)];
            const u8* pb = textures[b.i1].rgba.data() + rowOff[b.i1] + colOff[b.i1][static_cast<size_t>(c)];
            for (int k = 0; k < 3; k++) {
                double v = static_cast<double>(pa[k]) * b.omf + static_cast<double>(pb[k]) * b.fr;
                out[k] = static_cast<u8>(static_cast<int>(v) & 0xFF);
            }
        }
    }
}

void tileCornerUvs(int atlasWidth, int atlasHeight, int tileIndex, int rotation, Vec2 out[4], bool halfTexelInset) {
    int perRow = std::max(1, atlasWidth / 64);
    int perCol = std::max(1, atlasHeight / 64);
    float du = 1.0f / static_cast<float>(perRow);
    float dv = 1.0f / static_cast<float>(perCol);
    int idx = tileIndex < 0 ? 0 : tileIndex;
    int col = idx % perRow;
    int row = (idx / perRow) % perCol; // indices past the atlas wrap (GL_REPEAT in the original)
    float u0 = du * static_cast<float>(col), u1 = u0 + du;
    float vTop = dv * static_cast<float>(row), vBot = vTop + dv; // v = 0 at the image top
    if (halfTexelInset && atlasWidth > 0 && atlasHeight > 0) {
        const float iu = 0.5f / static_cast<float>(atlasWidth), iv = 0.5f / static_cast<float>(atlasHeight);
        u0 += iu;
        u1 -= iu;
        vTop += iv;
        vBot -= iv;
    }
    // Spec corners A=(u0,v0) B=(u1,v0) C=(u1,v1) D=(u0,v1) with v0 the lower edge in GL
    // bottom-up terms: A = bottom-left, B = bottom-right, C = top-right, D = top-left.
    Vec2 A{u0, vBot}, B{u1, vBot}, C{u1, vTop}, D{u0, vTop};
    switch (rotation) {
        case 3: out[0] = D; out[1] = A; out[2] = B; out[3] = C; break;
        case 6: out[0] = C; out[1] = D; out[2] = A; out[3] = B; break;
        case 9: out[0] = B; out[1] = C; out[2] = D; out[3] = A; break;
        default: out[0] = A; out[1] = B; out[2] = C; out[3] = D; break;
    }
}

void centralDifferenceNormals(const TerrainGridView& grid, std::vector<Vec3>& out) {
    out.clear();
    if (!grid.valid()) return;
    const int vw = grid.vertsWide(), vh = grid.vertsHigh();
    out.resize(static_cast<size_t>(vw) * static_cast<size_t>(vh));
    for (int r = 0; r < vh; ++r) {
        for (int c = 0; c < vw; ++c) {
            float sx = 0.0f, sy = 0.0f;
            if (c > 0 && c < vw - 1) sx = (grid.at(c + 1, r).z - grid.at(c - 1, r).z) / (2.0f * kHmapCellSize);
            if (r > 0 && r < vh - 1) sy = (grid.at(c, r + 1).z - grid.at(c, r - 1).z) / (2.0f * kHmapCellSize);
            out[static_cast<size_t>(r) * vw + c] = normalize(normalize(Vec3{-sx, 0.0f, 1.0f}) + normalize(Vec3{0.0f, -sy, 1.0f}));
        }
    }
}

void sequelTerrainColours(const TerrainGridView& grid, const std::vector<Vec3>& normals, const TerrainStyle& st,
                          std::vector<TerrainVertexColor>& out) {
    out.clear();
    if (!grid.valid() || normals.size() != static_cast<size_t>(grid.vertsWide()) * grid.vertsHigh()) return;
    const float sun[3] = {st.sun[0], st.sun[1], st.sun[2]};
    const float amb[3] = {st.sun[6], st.sun[7], st.sun[8]};
    Vec3 L{st.sun[3], st.sun[4], st.sun[5]};
    L = length(L) > 0.0f ? normalize(L) : Vec3{0.0f, 0.0f, 1.0f};
    out.resize(normals.size());
    for (size_t i = 0; i < normals.size(); ++i) {
        const float z = grid.positions[i].z;
        float f = 1.0f;
        if (st.hasWater && z < st.waterLevel) {
            const float denom = st.waterLevel - st.hmin;
            f = std::fabs(denom) > 1e-9f ? (z - st.hmin) / denom : 0.0f;
        }
        const float d = std::min(1.0f, std::max(0.0f, dot(normals[i], L)));
        float rgb[3];
        for (int k = 0; k < 3; ++k) rgb[k] = std::min(255.0f, std::max(0.0f, (sun[k] * d + amb[k]) * f * 255.0f));
        out[i].r = static_cast<u8>(static_cast<int>(rgb[0]));
        out[i].g = static_cast<u8>(static_cast<int>(rgb[1]));
        out[i].b = static_cast<u8>(static_cast<int>(rgb[2]));
    }
}

std::vector<int> terrainTileSets(const LevelData& level) {
    bool used[256] = {};
    for (const LevelCell& c : level.cells) used[c.tileSet] = true;
    std::vector<int> sets;
    for (int s = 1; s < 256; ++s) {
        if (used[s]) sets.push_back(s);
    }
    return sets;
}

TerrainGeometry buildTerrainGeometry(const Terrain& terrain, const RenderRules& rules,
                                     const std::map<int, std::pair<int, int>>& atlasSizes) {
    TerrainGeometry g;
    if (!terrain.level() || terrain.positions().empty()) return g;
    const LevelData& level = *terrain.level();
    const TerrainStyle& def = terrain.style();
    const int W = terrain.width(), H = terrain.height();
    const int vw = terrain.vertsWide();
    g.width = W;
    g.height = H;

    const auto& pos = terrain.positions();
    // Normals and static colours: the terrain's own (the first game's rules), or the sequels'
    // central differences with their colour formula (delta 12.2), from the heights at build.
    std::vector<Vec3> seqNormals;
    std::vector<TerrainVertexColor> seqColours;
    if (rules.terrainCentralNormals) {
        const TerrainGridView grid = TerrainGridView::of(terrain);
        centralDifferenceNormals(grid, seqNormals);
        sequelTerrainColours(grid, seqNormals, def, seqColours);
    }
    const auto& col = rules.terrainCentralNormals ? seqColours : terrain.colors();
    const auto& nrm = rules.terrainCentralNormals ? seqNormals : terrain.normals();

    // Chunks of 8 rows, each with its own vertices (the map texture changes every 32 rows, so a
    // vertex on a block edge needs a different v in each block).
    const int nChunks = (H + kTerrainChunkRows - 1) / kTerrainChunkRows;
    g.chunks.resize(static_cast<size_t>(nChunks));
    for (int k = 0; k < nChunks; k++) {
        TerrainChunkGeometry& ch = g.chunks[static_cast<size_t>(k)];
        ch.firstRow = k * kTerrainChunkRows;
        ch.rows = std::min(kTerrainChunkRows, H - ch.firstRow);
        ch.block = ch.firstRow / kBaseTextureRows;
        ch.baseVertex = g.vertexCount;
        ch.bounds.min = {1e30f, 1e30f, 1e30f};
        ch.bounds.max = {-1e30f, -1e30f, -1e30f};
        ch.verts.reserve(static_cast<size_t>(ch.rows + 1) * static_cast<size_t>(W + 1));
        for (int lr = 0; lr <= ch.rows; lr++) {
            int r = ch.firstRow + lr;
            for (int c = 0; c <= W; c++) {
                size_t gi = static_cast<size_t>(r) * vw + c;
                TerrainVertex v;
                v.pos[0] = pos[gi].x; v.pos[1] = pos[gi].y; v.pos[2] = pos[gi].z;
                v.col[0] = col[gi].r / 255.0f; v.col[1] = col[gi].g / 255.0f; v.col[2] = col[gi].b / 255.0f;
                v.uv0[0] = static_cast<float>(c) / 32.0f;
                v.uv0[1] = static_cast<float>(r - ch.block * kBaseTextureRows) / 32.0f;
                v.uv1[0] = static_cast<float>(c) * 0.25f;
                v.uv1[1] = static_cast<float>(r) * 0.25f;
                v.nrm[0] = nrm[gi].x; v.nrm[1] = nrm[gi].y; v.nrm[2] = nrm[gi].z;
                ch.verts.push_back(v);
                ch.bounds.min.x = std::min(ch.bounds.min.x, pos[gi].x); ch.bounds.max.x = std::max(ch.bounds.max.x, pos[gi].x);
                ch.bounds.min.y = std::min(ch.bounds.min.y, pos[gi].y); ch.bounds.max.y = std::max(ch.bounds.max.y, pos[gi].y);
                ch.bounds.min.z = std::min(ch.bounds.min.z, pos[gi].z); ch.bounds.max.z = std::max(ch.bounds.max.z, pos[gi].z);
            }
        }
        g.vertexCount += ch.verts.size();
        for (int lr = 0; lr < ch.rows; lr++) {
            for (int c = 0; c < W; c++) {
                uint16_t v00 = static_cast<uint16_t>(lr * (W + 1) + c);
                uint16_t v10 = static_cast<uint16_t>(v00 + 1);
                uint16_t v01 = static_cast<uint16_t>(v00 + (W + 1));
                uint16_t v11 = static_cast<uint16_t>(v01 + 1);
                ch.tris.insert(ch.tris.end(), {v00, v10, v11, v00, v11, v01});
                ch.wire.insert(ch.wire.end(), {v00, v10, v00, v01, v00, v11});
            }
        }
    }

    // Tile overlays, one batch per atlas.
    for (const auto& kv : atlasSizes) {
        TerrainTileGeometry t;
        t.set = kv.first;
        t.ranges.assign(static_cast<size_t>(nChunks), {0, 0});
        g.tiles.push_back(std::move(t));
    }
    for (int k = 0; k < nChunks; k++) {
        const TerrainChunkGeometry& ch = g.chunks[static_cast<size_t>(k)];
        size_t b = 0;
        for (const auto& kv : atlasSizes) {
            TerrainTileGeometry& t = g.tiles[b++];
            const int set = kv.first;
            auto& tv = t.verts;
            const int first = static_cast<int>(tv.size());
            for (int lr = 0; lr < ch.rows; lr++) {
                int r = ch.firstRow + lr;
                for (int c = 0; c < W; c++) {
                    const LevelCell& cell = level.cellAt(c, r);
                    if (cell.tileSet != set) continue;
                    Vec2 uv[4];
                    tileCornerUvs(kv.second.first, kv.second.second, cell.tileIndex, cell.tileRotation, uv,
                                  rules.tileHalfTexelInset);
                    t.cells.push_back({static_cast<uint16_t>(c), static_cast<uint16_t>(r)});
                    size_t gi[4] = {static_cast<size_t>(r) * vw + c, static_cast<size_t>(r) * vw + c + 1,
                                    static_cast<size_t>(r + 1) * vw + c + 1, static_cast<size_t>(r + 1) * vw + c};
                    TerrainVertex q[4];
                    for (int i = 0; i < 4; i++) {
                        q[i].pos[0] = pos[gi[i]].x; q[i].pos[1] = pos[gi[i]].y; q[i].pos[2] = pos[gi[i]].z;
                        q[i].col[0] = col[gi[i]].r / 255.0f; q[i].col[1] = col[gi[i]].g / 255.0f; q[i].col[2] = col[gi[i]].b / 255.0f;
                        q[i].uv0[0] = uv[i].x; q[i].uv0[1] = uv[i].y;
                        q[i].uv1[0] = q[i].uv1[1] = 0.0f;
                        q[i].nrm[0] = nrm[gi[i]].x; q[i].nrm[1] = nrm[gi[i]].y; q[i].nrm[2] = nrm[gi[i]].z;
                    }
                    // triangles (v00, v11, v01) and (v00, v10, v11)
                    tv.push_back(q[0]); tv.push_back(q[2]); tv.push_back(q[3]);
                    tv.push_back(q[0]); tv.push_back(q[1]); tv.push_back(q[2]);
                }
            }
            t.ranges[static_cast<size_t>(k)] = {first, static_cast<int>(tv.size()) - first};
        }
    }
    return g;
}

std::vector<int> updateChunkHeights(TerrainGeometry& g, const VertexRect* rects, size_t count,
                                    const TerrainGridView& grid) {
    std::vector<int> changed;
    if (!g.valid() || !grid.valid() || grid.width != g.width || grid.height != g.height) return changed;
    const int W = grid.width;
    // Tile quads: corners v00, v11, v01, v00, v10, v11 (the order buildTerrainGeometry writes).
    static const int kCornerDc[6] = {0, 1, 0, 0, 1, 1}, kCornerDr[6] = {0, 1, 1, 0, 0, 1};
    for (size_t k = 0; k < g.chunks.size(); ++k) {
        TerrainChunkGeometry& ch = g.chunks[k];
        const int r0 = ch.firstRow, r1 = ch.firstRow + ch.rows;
        bool touched = false;
        for (size_t i = 0; i < count && !touched; ++i) {
            const VertexRect& rc = rects[i];
            touched = !rc.empty() && rc.r0 <= r1 && rc.r1 >= r0 && rc.c0 <= W && rc.c1 >= 0;
        }
        if (!touched) continue;
        const int vw = grid.vertsWide();
        for (int lr = 0; lr <= ch.rows; ++lr) {
            for (int c = 0; c <= W; ++c) {
                const Vec3& p = grid.at(c, r0 + lr);
                TerrainVertex& v = ch.verts[static_cast<size_t>(lr) * vw + c];
                v.pos[0] = p.x;
                v.pos[1] = p.y;
                v.pos[2] = p.z;
                ch.bounds.min.z = std::min(ch.bounds.min.z, p.z);
                ch.bounds.max.z = std::max(ch.bounds.max.z, p.z);
            }
        }
        for (TerrainTileGeometry& t : g.tiles) {
            const auto range = t.ranges[k];
            for (int q = 0; q < range.second / 6; ++q) {
                const size_t quad = static_cast<size_t>(range.first / 6 + q);
                const auto cell = t.cells[quad];
                for (int j = 0; j < 6; ++j) {
                    const Vec3& p = grid.at(cell.first + kCornerDc[j], cell.second + kCornerDr[j]);
                    TerrainVertex& v = t.verts[quad * 6 + static_cast<size_t>(j)];
                    v.pos[0] = p.x;
                    v.pos[1] = p.y;
                    v.pos[2] = p.z;
                }
            }
        }
        changed.push_back(static_cast<int>(k));
    }
    return changed;
}

TerrainFrustum extractTerrainFrustum(const Mat4& m) {
    // Rows of the clip matrix (column-major storage: at(col,row)).
    auto row = [&](int r, float out[4]) { for (int c = 0; c < 4; c++) out[c] = m.at(c, r); };
    float r0[4], r1[4], r2[4], r3[4];
    row(0, r0); row(1, r1); row(2, r2); row(3, r3);
    TerrainFrustum f;
    for (int i = 0; i < 4; i++) {
        f.p[0][i] = r3[i] + r0[i];
        f.p[1][i] = r3[i] - r0[i];
        f.p[2][i] = r3[i] + r1[i];
        f.p[3][i] = r3[i] - r1[i];
        f.p[4][i] = r3[i] + r2[i];
        f.p[5][i] = r3[i] - r2[i];
    }
    return f;
}

bool terrainBoxVisible(const TerrainFrustum& f, const Aabb& box) {
    for (int i = 0; i < 6; i++) {
        float x = f.p[i][0] >= 0 ? box.max.x : box.min.x;
        float y = f.p[i][1] >= 0 ? box.max.y : box.min.y;
        float z = f.p[i][2] >= 0 ? box.max.z : box.min.z;
        if (f.p[i][0] * x + f.p[i][1] * y + f.p[i][2] * z + f.p[i][3] < 0) return false;
    }
    return true;
}

std::vector<int> visibleTerrainChunks(const TerrainGeometry& g, const TerrainFrustum& frustum) {
    std::vector<int> visible;
    for (size_t k = 0; k < g.chunks.size(); k++) {
        if (terrainBoxVisible(frustum, g.chunks[k].bounds)) visible.push_back(static_cast<int>(k));
    }
    return visible;
}

std::vector<int> visibleTerrainChunks(const TerrainGeometry& g, const Mat4& viewProjection) {
    return visibleTerrainChunks(g, extractTerrainFrustum(viewProjection));
}

void terrainDynamicLightColors(const TerrainChunkGeometry& chunk, const DynamicLight* lights, int count,
                               std::vector<uint8_t>& out) {
    const int n = std::max(0, std::min(count, kMaxDynamicLights));
    out.resize(chunk.verts.size() * 4);
    for (size_t vi = 0; vi < chunk.verts.size(); ++vi) {
        const TerrainVertex& tv = chunk.verts[vi];
        const Vec3 aPos{tv.pos[0], tv.pos[1], tv.pos[2]};
        const Vec3 aNormal{tv.nrm[0], tv.nrm[1], tv.nrm[2]};
        Vec3 col{tv.col[0], tv.col[1], tv.col[2]};
        for (int i = 0; i < n; i++) {
            const DynamicLight& l = lights[i];
            Vec3 v = l.position - aPos;
            const float radius = l.radius;
            // Conservative early out before the square root (as evaluateLight): beyond the
            // radius for certain; the exact test below decides every other vertex.
            const float d2 = dot(v, v);
            if (d2 > radius * radius * 1.01f) continue;
            const float dist = std::sqrt(d2);
            if (radius <= 0.0f || dist > radius) continue;
            if (dist > 0.0f) v = Vec3{v.x / dist, v.y / dist, v.z / dist};
            float spot = 1.0f;
            if (l.spot) {
                const float c = dot(Vec3{-v.x, -v.y, -v.z}, l.direction);
                if (c < l.cosOuter) continue;
                if (c < l.cosInner) spot = (c - l.cosOuter) / (l.cosInner - l.cosOuter);
            }
            const float nd = dot(aNormal, v);
            if (nd <= 0.0f) continue;
            col = col + l.colour * ((0.7f * nd + 0.3f) * (radius - dist) / radius * spot);
        }
        const float rgb[3] = {std::min(col.x, 1.0f), std::min(col.y, 1.0f), std::min(col.z, 1.0f)};
        for (int k = 0; k < 3; ++k) {
            const float b = std::floor(std::max(rgb[k], 0.0f) * 255.0f + 0.5f);
            out[vi * 4 + static_cast<size_t>(k)] = static_cast<uint8_t>(std::min(b, 255.0f));
        }
        out[vi * 4 + 3] = 255;
    }
}

} // namespace as3d
