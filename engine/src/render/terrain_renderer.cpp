// Terrain renderer: generated base textures, chunked mesh, detail pass, tile overlays.
// See docs/spec/hmap.md "Terrain texturing" / "Tile overlays" and docs/graphics.md.
#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>
#include <map>

#include "as3d/terrain_render.h"

namespace as3d {

extern const char* const kTerrainVertexSrc;
extern const char* const kTerrainFragmentSrc;

namespace {

constexpr int kChunkRows = 8;
constexpr float kHeightBand = 86.0f;

struct Vertex {
    float pos[3];
    float col[3];
    float uv0[2];
    float uv1[2];
};

Image placeholderImage() {
    Image img;
    img.width = img.height = 4;
    img.rgba.resize(4 * 4 * 4);
    for (int i = 0; i < 16; i++) {
        img.rgba[i * 4 + 0] = 255;
        img.rgba[i * 4 + 1] = 0;
        img.rgba[i * 4 + 2] = 255;
        img.rgba[i * 4 + 3] = 255;
    }
    return img;
}

// Loads a TGA through the vfs; on failure logs a warning and returns a magenta
// placeholder (and bumps *missing).
Image loadTexture(Vfs& vfs, const std::string& path, int* missing) {
    Blob blob;
    Image img;
    if (!vfs.read(path, blob) || !decodeTga(blob.data(), blob.size(), img)) {
        AS3D_WARN("terrain: missing texture '%s'", path.c_str());
        if (missing) (*missing)++;
        return placeholderImage();
    }
    return img;
}

VertexLayout terrainLayout() {
    VertexLayout l;
    l.strideBytes = sizeof(Vertex);
    l.attribs = {{0, 3, offsetof(Vertex, pos), false},
                 {1, 3, offsetof(Vertex, col), false},
                 {2, 2, offsetof(Vertex, uv0), false},
                 {3, 2, offsetof(Vertex, uv1), false}};
    return l;
}

struct Chunk {
    int rowStart = 0, rows = 0;
    Vec3 bmin, bmax;
    size_t triOffset = 0, triCount = 0;   // in indices
    size_t wireOffset = 0, wireCount = 0; // in indices
    int block = 0;
};

struct TileBatch {
    int set = 0;
    Texture2D texture;
    VertexBuffer vbo;
    VertexArray vao;
    std::vector<std::pair<int, int>> ranges; // per chunk: first vertex, vertex count
};

} // namespace

// ---------------------------------------------------------------------------
// Public helpers
// ---------------------------------------------------------------------------

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
    for (int r = 0; r < kBaseTextureSize; r++) {
        for (int c = 0; c < kBaseTextureSize; c++) {
            double t = static_cast<double>(hs[static_cast<size_t>(r) * kBaseTextureSize + c]) / static_cast<double>(kHeightBand);
            int i0 = static_cast<int>(std::floor(t));
            int i1 = static_cast<int>(std::ceil(t));
            i0 = std::min(std::max(i0, 0), 3);
            i1 = std::min(std::max(i1, 0), 3);
            double fr = t - std::floor(t);
            const Image& ta = textures[i0];
            const Image& tb = textures[i1];
            size_t pa = (static_cast<size_t>(r % ta.height) * ta.width + static_cast<size_t>(c % ta.width)) * 4;
            size_t pb = (static_cast<size_t>(r % tb.height) * tb.width + static_cast<size_t>(c % tb.width)) * 4;
            for (int k = 0; k < 3; k++) {
                double v = static_cast<double>(ta.rgba[pa + k]) * (1.0 - fr) + static_cast<double>(tb.rgba[pb + k]) * fr;
                rgb[(static_cast<size_t>(r) * kBaseTextureSize + c) * 3 + k] = static_cast<u8>(static_cast<int>(v) & 0xFF);
            }
        }
    }
}

void tileCornerUvs(int atlasWidth, int atlasHeight, int tileIndex, int rotation, Vec2 out[4]) {
    int perRow = std::max(1, atlasWidth / 64);
    int perCol = std::max(1, atlasHeight / 64);
    float du = 1.0f / static_cast<float>(perRow);
    float dv = 1.0f / static_cast<float>(perCol);
    int idx = tileIndex < 0 ? 0 : tileIndex;
    int col = idx % perRow;
    int row = (idx / perRow) % perCol; // indices past the atlas wrap (GL_REPEAT in the original)
    float u0 = du * static_cast<float>(col), u1 = u0 + du;
    float vTop = dv * static_cast<float>(row), vBot = vTop + dv; // v = 0 at the image top
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

// ---------------------------------------------------------------------------
// TerrainRenderer
// ---------------------------------------------------------------------------

struct TerrainRenderer::Impl {
    const Terrain* terrain = nullptr;
    ShaderProgram program;
    VertexBuffer vbo;
    IndexBuffer triIbo, wireIbo;
    VertexArray triVao, wireVao;
    std::vector<Chunk> chunks;
    std::vector<Texture2D> baseTextures;
    Texture2D detail;
    std::vector<std::unique_ptr<TileBatch>> tiles;
};

TerrainRenderer::TerrainRenderer() : impl_(new Impl) {}
TerrainRenderer::~TerrainRenderer() = default;

int TerrainRenderer::chunkCount() const { return static_cast<int>(impl_->chunks.size()); }

bool TerrainRenderer::build(const Terrain& terrain, Vfs& vfs, std::string* error) {
    Impl& im = *impl_;
    im = Impl{};
    missing_ = 0;
    if (!terrain.level() || terrain.positions().empty()) {
        if (error) *error = "terrain not built";
        return false;
    }
    im.terrain = &terrain;
    const LevelData& level = *terrain.level();
    const TerrainStyle& def = terrain.style();
    int W = terrain.width(), H = terrain.height();
    int vw = terrain.vertsWide();

    if (!im.program.compile(kTerrainVertexSrc, kTerrainFragmentSrc, error)) return false;

    // Textures.
    Image src[4];
    for (int i = 0; i < 4; i++) {
        src[i] = loadTexture(vfs, def.texturesDir + "\\texture" + std::to_string(i + 1) + ".tga", &missing_);
    }
    int blocks = (H + kBaseTextureRows - 1) / kBaseTextureRows;
    im.baseTextures.resize(static_cast<size_t>(blocks));
    TextureOptions topts;
    topts.mipmaps = true;
    topts.minFilter = Filter::Linear;
    topts.magFilter = Filter::Linear;
    for (int b = 0; b < blocks; b++) {
        std::vector<u8> rgb;
        generateBaseTextureRgb(level, src, b, rgb);
        Image img;
        img.width = img.height = kBaseTextureSize;
        img.rgba.resize(static_cast<size_t>(kBaseTextureSize) * kBaseTextureSize * 4);
        for (size_t i = 0; i < static_cast<size_t>(kBaseTextureSize) * kBaseTextureSize; i++) {
            img.rgba[i * 4 + 0] = rgb[i * 3 + 0];
            img.rgba[i * 4 + 1] = rgb[i * 3 + 1];
            img.rgba[i * 4 + 2] = rgb[i * 3 + 2];
            img.rgba[i * 4 + 3] = 255;
        }
        im.baseTextures[static_cast<size_t>(b)].create(img, topts);
    }
    Image det = loadTexture(vfs, def.texturesDir + "\\detail.tga", &missing_);
    im.detail.create(det, topts);

    // Mesh: chunks of 8 rows, each with its own vertices (the map texture changes every
    // 32 rows, so a vertex on a block edge needs a different v in each block).
    int nChunks = (H + kChunkRows - 1) / kChunkRows;
    std::vector<Vertex> verts;
    std::vector<u16> tri, wire;
    im.chunks.resize(static_cast<size_t>(nChunks));
    const auto& pos = terrain.positions();
    const auto& col = terrain.colors();
    for (int k = 0; k < nChunks; k++) {
        Chunk& ch = im.chunks[static_cast<size_t>(k)];
        ch.rowStart = k * kChunkRows;
        ch.rows = std::min(kChunkRows, H - ch.rowStart);
        ch.block = ch.rowStart / kBaseTextureRows;
        size_t base = verts.size();
        ch.bmin = {1e30f, 1e30f, 1e30f};
        ch.bmax = {-1e30f, -1e30f, -1e30f};
        for (int lr = 0; lr <= ch.rows; lr++) {
            int r = ch.rowStart + lr;
            for (int c = 0; c <= W; c++) {
                size_t gi = static_cast<size_t>(r) * vw + c;
                Vertex v;
                v.pos[0] = pos[gi].x; v.pos[1] = pos[gi].y; v.pos[2] = pos[gi].z;
                v.col[0] = col[gi].r / 255.0f; v.col[1] = col[gi].g / 255.0f; v.col[2] = col[gi].b / 255.0f;
                v.uv0[0] = static_cast<float>(c) / 32.0f;
                v.uv0[1] = static_cast<float>(r - ch.block * kBaseTextureRows) / 32.0f;
                v.uv1[0] = static_cast<float>(c) * 0.25f;
                v.uv1[1] = static_cast<float>(r) * 0.25f;
                verts.push_back(v);
                ch.bmin.x = std::min(ch.bmin.x, pos[gi].x); ch.bmax.x = std::max(ch.bmax.x, pos[gi].x);
                ch.bmin.y = std::min(ch.bmin.y, pos[gi].y); ch.bmax.y = std::max(ch.bmax.y, pos[gi].y);
                ch.bmin.z = std::min(ch.bmin.z, pos[gi].z); ch.bmax.z = std::max(ch.bmax.z, pos[gi].z);
            }
        }
        ch.triOffset = tri.size();
        ch.wireOffset = wire.size();
        for (int lr = 0; lr < ch.rows; lr++) {
            for (int c = 0; c < W; c++) {
                u16 v00 = static_cast<u16>(base + static_cast<size_t>(lr) * (W + 1) + c);
                u16 v10 = static_cast<u16>(v00 + 1);
                u16 v01 = static_cast<u16>(v00 + (W + 1));
                u16 v11 = static_cast<u16>(v01 + 1);
                tri.insert(tri.end(), {v00, v10, v11, v00, v11, v01});
                wire.insert(wire.end(), {v00, v10, v00, v01, v00, v11});
            }
        }
        ch.triCount = tri.size() - ch.triOffset;
        ch.wireCount = wire.size() - ch.wireOffset;
    }
    if (verts.size() > 65535) {
        if (error) *error = "terrain mesh too large for 16-bit indices";
        return false;
    }
    im.vbo.upload(verts.data(), verts.size() * sizeof(Vertex));
    im.triIbo.upload(tri.data(), tri.size() * sizeof(u16), IndexType::U16);
    im.wireIbo.upload(wire.data(), wire.size() * sizeof(u16), IndexType::U16);
    im.triVao.create(im.vbo, terrainLayout(), &im.triIbo);
    im.wireVao.create(im.vbo, terrainLayout(), &im.wireIbo);

    // Tile overlays, one batch per atlas.
    std::map<int, TileBatch*> bySet;
    std::map<int, std::vector<Vertex>> tileVerts;
    std::map<int, std::vector<std::pair<int, int>>> tileRanges;
    std::map<int, std::pair<int, int>> atlasSize;
    for (const LevelCell& c : level.cells) {
        if (c.tileSet && !atlasSize.count(c.tileSet)) atlasSize[c.tileSet] = {0, 0};
    }
    std::map<int, Image> atlasImages;
    for (auto& kv : atlasSize) {
        Image img = loadTexture(vfs, "tiles\\tiles" + std::to_string(kv.first) + ".tga", &missing_);
        kv.second = {img.width, img.height};
        atlasImages[kv.first] = std::move(img);
        tileRanges[kv.first].assign(static_cast<size_t>(nChunks), {0, 0});
    }
    for (int k = 0; k < nChunks; k++) {
        const Chunk& ch = im.chunks[static_cast<size_t>(k)];
        for (auto& kv : atlasSize) {
            int set = kv.first;
            auto& tv = tileVerts[set];
            int first = static_cast<int>(tv.size());
            for (int lr = 0; lr < ch.rows; lr++) {
                int r = ch.rowStart + lr;
                for (int c = 0; c < W; c++) {
                    const LevelCell& cell = level.cellAt(c, r);
                    if (cell.tileSet != set) continue;
                    Vec2 uv[4];
                    tileCornerUvs(kv.second.first, kv.second.second, cell.tileIndex, cell.tileRotation, uv);
                    size_t g[4] = {static_cast<size_t>(r) * vw + c, static_cast<size_t>(r) * vw + c + 1,
                                   static_cast<size_t>(r + 1) * vw + c + 1, static_cast<size_t>(r + 1) * vw + c};
                    Vertex q[4];
                    for (int i = 0; i < 4; i++) {
                        q[i].pos[0] = pos[g[i]].x; q[i].pos[1] = pos[g[i]].y; q[i].pos[2] = pos[g[i]].z;
                        q[i].col[0] = col[g[i]].r / 255.0f; q[i].col[1] = col[g[i]].g / 255.0f; q[i].col[2] = col[g[i]].b / 255.0f;
                        q[i].uv0[0] = uv[i].x; q[i].uv0[1] = uv[i].y;
                        q[i].uv1[0] = q[i].uv1[1] = 0.0f;
                    }
                    // triangles (v00, v11, v01) and (v00, v10, v11)
                    tv.push_back(q[0]); tv.push_back(q[2]); tv.push_back(q[3]);
                    tv.push_back(q[0]); tv.push_back(q[1]); tv.push_back(q[2]);
                }
            }
            tileRanges[set][static_cast<size_t>(k)] = {first, static_cast<int>(tv.size()) - first};
        }
    }
    for (auto& kv : atlasSize) {
        auto batch = std::make_unique<TileBatch>();
        batch->set = kv.first;
        TextureOptions to;
        to.mipmaps = false;
        to.wrapS = Wrap::Repeat;
        to.wrapT = Wrap::Repeat;
        batch->texture.create(atlasImages[kv.first], to);
        auto& tv = tileVerts[kv.first];
        if (!tv.empty()) {
            batch->vbo.upload(tv.data(), tv.size() * sizeof(Vertex));
            batch->vao.create(batch->vbo, terrainLayout());
        }
        batch->ranges = tileRanges[kv.first];
        im.tiles.push_back(std::move(batch));
    }
    return true;
}

namespace {

struct Frustum {
    float p[6][4];
};

Frustum extractFrustum(const Mat4& m) {
    // Rows of the clip matrix (column-major storage: at(col,row)).
    auto row = [&](int r, float out[4]) { for (int c = 0; c < 4; c++) out[c] = m.at(c, r); };
    float r0[4], r1[4], r2[4], r3[4];
    row(0, r0); row(1, r1); row(2, r2); row(3, r3);
    Frustum f;
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

bool boxVisible(const Frustum& f, const Vec3& mn, const Vec3& mx) {
    for (int i = 0; i < 6; i++) {
        float x = f.p[i][0] >= 0 ? mx.x : mn.x;
        float y = f.p[i][1] >= 0 ? mx.y : mn.y;
        float z = f.p[i][2] >= 0 ? mx.z : mn.z;
        if (f.p[i][0] * x + f.p[i][1] * y + f.p[i][2] * z + f.p[i][3] < 0) return false;
    }
    return true;
}

} // namespace

void TerrainRenderer::render(const TerrainViewParams& params, const TerrainRenderOptions& options) {
    Impl& im = *impl_;
    if (!im.terrain || !im.program.valid()) return;
    Frustum fr = extractFrustum(params.projection * params.view);
    std::vector<int> visible;
    for (size_t k = 0; k < im.chunks.size(); k++) {
        if (boxVisible(fr, im.chunks[k].bmin, im.chunks[k].bmax)) visible.push_back(static_cast<int>(k));
    }
    lastVisible_ = static_cast<int>(visible.size());

    setCull(CullMode::Off);
    setDepth(true, true);
    setBlend(BlendMode::Off);

    im.program.use();
    im.program.setMat4("uView", params.view);
    im.program.setMat4("uProj", params.projection);
    im.program.setVec3("uFogColor", params.fogColour);
    im.program.setFloat("uFogStart", params.fogStart);
    im.program.setFloat("uFogEnd", params.fogEnd);
    im.program.setInt("uTex0", 0);
    im.program.setInt("uTex1", 1);
    im.program.setInt("uDetail", options.detail ? 1 : 0);
    im.program.setInt("uMode", 0);
    im.detail.bind(1);

    const VertexArray& vao = options.wireframe ? im.wireVao : im.triVao;
    vao.bind();
    for (int k : visible) {
        const Chunk& ch = im.chunks[static_cast<size_t>(k)];
        im.baseTextures[static_cast<size_t>(ch.block)].bind(0);
        if (options.wireframe) {
            glDrawElements(GL_LINES, static_cast<GLsizei>(ch.wireCount), GL_UNSIGNED_SHORT,
                           reinterpret_cast<const void*>(ch.wireOffset * sizeof(u16)));
        } else {
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(ch.triCount), GL_UNSIGNED_SHORT,
                           reinterpret_cast<const void*>(ch.triOffset * sizeof(u16)));
        }
    }

    if (options.tiles && !options.wireframe) {
        setBlend(BlendMode::AlphaBlend);
        setDepth(true, false);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -1.0f);
        im.program.setInt("uMode", 1);
        for (auto& batch : im.tiles) {
            if (!batch->vao.valid()) continue;
            batch->texture.bind(0);
            batch->vao.bind();
            for (int k : visible) {
                auto range = batch->ranges[static_cast<size_t>(k)];
                if (range.second > 0) glDrawArrays(GL_TRIANGLES, range.first, range.second);
            }
        }
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    setDepth(true, true);
    setBlend(BlendMode::Off);
}

} // namespace as3d
