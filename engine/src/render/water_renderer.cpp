// Water renderer: the first game's plane (docs/spec/hmap.md "Water") and the sequels' grid
// (docs/spec/as2/render-pipeline.delta.md 12.5).
#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>

#include "as3d/terrain_render.h"

namespace as3d {

extern const char* const kWaterVertexSrc;
extern const char* const kWaterFragmentSrc;
extern const char* const kWaterGridVertexSrc;
extern const char* const kWaterGridFragmentSrc;

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
                     std::vector<WaterGridVertex>& vertices, std::vector<u16>& indices) {
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
            const u16 v00 = static_cast<u16>(lr * vw + c), v10 = static_cast<u16>(v00 + 1);
            const u16 v01 = static_cast<u16>(v00 + vw), v11 = static_cast<u16>(v01 + 1);
            indices.insert(indices.end(), {v00, v10, v11, v00, v11, v01});
        }
    }
}

WaterGridFrame computeWaterGridFrame(float timeSeconds) {
    WaterGridFrame f;
    const float t = timeSeconds * kPi * 0.1f;
    f.waveTime = timeSeconds;
    f.baseOffset = {0.4f * std::sin(t * 0.5f) + 0.2f, -0.2f * std::sin(t * 0.25f) - 0.3f};
    f.shineOffset = {0.4f * std::sin(t), 0.4f * std::sin(t * 0.5f)};
    return f;
}

Vec2 waterBaseUv(int col, int row, const WaterGridFrame& f) {
    return {2.0f * static_cast<float>(col) * 0.25f + f.baseOffset.x, 2.0f * static_cast<float>(row) * 0.25f + f.baseOffset.y};
}

Vec2 waterShineUv(int col, int row, const WaterGridFrame& f) {
    return {1.5f * static_cast<float>(col) * 0.25f + f.shineOffset.x, 1.5f * static_cast<float>(row) * 0.25f + f.shineOffset.y};
}

namespace {

constexpr int kWaterChunkRows = 8; // the terrain's chunks (as2@0x437c60)

// The texture name has its extension replaced by .tga before loading; a missing file gives a
// magenta placeholder and a warning.
Image loadWaterTexture(Vfs& vfs, const std::string& name, int* missing) {
    std::string path = name;
    size_t dot = path.find_last_of('.');
    size_t slash = path.find_last_of("/\\");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) path.resize(dot);
    path += ".tga";
    Blob blob;
    Image img;
    if (!vfs.read(path, blob) || !decodeTga(blob.data(), blob.size(), img)) {
        AS3D_WARN("water: missing texture '%s'", path.c_str());
        (*missing)++;
        img = Image();
        img.width = img.height = 4;
        img.rgba.assign(64, 0);
        for (int i = 0; i < 16; i++) { img.rgba[i * 4] = 255; img.rgba[i * 4 + 2] = 255; img.rgba[i * 4 + 3] = 255; }
    }
    return img;
}

struct GridChunk {
    int rowStart = 0, rows = 0;
    size_t firstVertex = 0, vertexCount = 0;
    size_t firstIndex = 0, indexCount = 0;
    Vec3 bmin, bmax;
};

} // namespace

struct WaterRenderer::Impl {
    const Terrain* terrain = nullptr;
    bool active = false;
    bool grid = false;
    ShaderProgram program;
    Texture2D texture;  // flat plane; grid: base layer
    Texture2D shine;    // grid: second layer
    VertexBuffer vbo;
    IndexBuffer ibo;
    VertexArray vao;
    int quads = 0;
    float quadHeight = 320.0f;
    // Grid water.
    WaterSurface surface;
    std::vector<GridChunk> chunks;
    std::vector<WaterGridVertex> cpu; // all chunks' vertices, as uploaded
    int wetCells = 0;
};

WaterRenderer::WaterRenderer() : impl_(new Impl) {}
WaterRenderer::~WaterRenderer() = default;
bool WaterRenderer::active() const { return impl_->active; }
bool WaterRenderer::grid() const { return impl_->grid; }
int WaterRenderer::wetCells() const { return impl_->wetCells; }

bool WaterRenderer::build(const Terrain& terrain, Vfs& vfs, std::string* error) {
    Impl& im = *impl_;
    im = Impl{};
    missing_ = 0;
    if (!terrain.style().hasWater || terrain.style().waterTexture.empty()) return true;
    im.terrain = &terrain;
    if (!im.program.compile(kWaterVertexSrc, kWaterFragmentSrc, error)) return false;

    Image img = loadWaterTexture(vfs, terrain.style().waterTexture, &missing_);
    TextureOptions to;
    to.mipmaps = true;
    to.wrapS = Wrap::Repeat;
    to.wrapT = Wrap::Repeat;
    im.texture.create(img, to);

    // One quad per 320 world units of map length (one per 8-row chunk): x 0..width*40,
    // texcoords (0,0)-(8,2) per quad, i.e. one repeat per 160 units.
    float width = static_cast<float>(terrain.width()) * kHmapCellSize;
    float length = static_cast<float>(terrain.height()) * kHmapCellSize;
    im.quads = static_cast<int>(std::ceil(length / im.quadHeight));
    struct V { float x, y, z, u, v; };
    std::vector<V> verts;
    float uMax = width / 160.0f;
    float vMax = im.quadHeight / 160.0f;
    for (int k = 0; k < im.quads; k++) {
        float y0 = im.quadHeight * static_cast<float>(k), y1 = y0 + im.quadHeight;
        V a{0, y0, 0, 0, 0}, b{width, y0, 0, uMax, 0}, c{width, y1, 0, uMax, vMax}, d{0, y1, 0, 0, vMax};
        verts.insert(verts.end(), {a, b, c, a, c, d});
    }
    im.vbo.upload(verts.data(), verts.size() * sizeof(V));
    VertexLayout l;
    l.strideBytes = sizeof(V);
    l.attribs = {{0, 3, 0, false}, {1, 2, 3 * sizeof(float), false}};
    im.vao.create(im.vbo, l);
    im.active = true;
    return true;
}

bool WaterRenderer::build(const Terrain& terrain, const WaterSurface& surface, const TerrainGridView& grid, Vfs& vfs,
                          std::string* error) {
    if (!surface.waves) return build(terrain, vfs, error);
    Impl& im = *impl_;
    im = Impl{};
    missing_ = 0;
    lastVisible_ = lastUpdated_ = 0;
    if (!surface.present || surface.baseTexture.empty() || !grid.valid() || surface.width != grid.width ||
        surface.height != grid.height)
        return true;
    im.terrain = &terrain;
    im.surface = surface;
    if (!im.program.compile(kWaterGridVertexSrc, kWaterGridFragmentSrc, error)) return false;
    TextureOptions to;
    to.mipmaps = true;
    to.wrapS = Wrap::Repeat;
    to.wrapT = Wrap::Repeat;
    im.texture.create(loadWaterTexture(vfs, surface.baseTexture, &missing_), to);
    if (!surface.shineTexture.empty()) {
        im.shine.create(loadWaterTexture(vfs, surface.shineTexture, &missing_), to);
    } else {
        Image clear; // no shine layer: alpha 0 keeps the base
        clear.width = clear.height = 1;
        clear.hasAlpha = true;
        clear.rgba = {0, 0, 0, 0};
        im.shine.create(clear, to);
    }

    const int H = grid.height;
    const int nChunks = (H + kWaterChunkRows - 1) / kWaterChunkRows;
    std::vector<u16> allIdx;
    std::vector<WaterGridVertex> cv;
    std::vector<u16> ci;
    im.chunks.resize(static_cast<size_t>(nChunks));
    for (int k = 0; k < nChunks; ++k) {
        GridChunk& ch = im.chunks[static_cast<size_t>(k)];
        ch.rowStart = k * kWaterChunkRows;
        ch.rows = std::min(kWaterChunkRows, H - ch.rowStart);
        buildWaterChunk(surface, grid, ch.rowStart, ch.rows, cv, ci);
        ch.firstVertex = im.cpu.size();
        ch.vertexCount = cv.size();
        ch.firstIndex = allIdx.size();
        ch.indexCount = ci.size();
        im.wetCells += static_cast<int>(ci.size() / 6);
        for (u16 i : ci) allIdx.push_back(static_cast<u16>(ch.firstVertex + i));
        float zmin = surface.level - kWaterWaveAmplitude, zmax = surface.level + kWaterWaveAmplitude;
        for (const WaterGridVertex& v : cv) {
            zmin = std::min(zmin, v.terrainZ);
            zmax = std::max(zmax, v.terrainZ);
        }
        ch.bmin = {0.0f, static_cast<float>(ch.rowStart) * kHmapCellSize, zmin};
        ch.bmax = {static_cast<float>(grid.width) * kHmapCellSize, static_cast<float>(ch.rowStart + ch.rows) * kHmapCellSize, zmax};
        im.cpu.insert(im.cpu.end(), cv.begin(), cv.end());
    }
    if (im.cpu.size() > 65535) {
        if (error) *error = "water grid too large for 16-bit indices";
        return false;
    }
    im.vbo.upload(im.cpu.data(), im.cpu.size() * sizeof(WaterGridVertex), true);
    if (!allIdx.empty()) im.ibo.upload(allIdx.data(), allIdx.size() * sizeof(u16), IndexType::U16);
    VertexLayout l;
    l.strideBytes = sizeof(WaterGridVertex);
    l.attribs = {{0, 4, 0, false}};
    im.vao.create(im.vbo, l, allIdx.empty() ? nullptr : &im.ibo);
    im.grid = true;
    im.active = im.wetCells > 0;
    return true;
}

void WaterRenderer::update(const VertexRect* rects, size_t count, const TerrainGridView& grid) {
    Impl& im = *impl_;
    lastUpdated_ = 0;
    if (!im.grid || !grid.valid() || grid.width != im.surface.width || grid.height != im.surface.height) return;
    const int vw = grid.width + 1;
    for (GridChunk& ch : im.chunks) {
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
                WaterGridVertex& v = im.cpu[ch.firstVertex + static_cast<size_t>(lr) * vw + c];
                v.terrainZ = grid.at(c, ch.rowStart + lr).z;
                ch.bmin.z = std::min(ch.bmin.z, v.terrainZ);
                ch.bmax.z = std::max(ch.bmax.z, v.terrainZ);
            }
        }
        glBindBuffer(GL_ARRAY_BUFFER, im.vbo.id());
        glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(ch.firstVertex * sizeof(WaterGridVertex)),
                        static_cast<GLsizeiptr>(ch.vertexCount * sizeof(WaterGridVertex)), &im.cpu[ch.firstVertex]);
        ++lastUpdated_;
    }
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

void WaterRenderer::render(const TerrainViewParams& params) {
    Impl& im = *impl_;
    lastVisible_ = 0;
    if (!im.active) return;
    if (im.grid) {
        const WaterGridFrame f = computeWaterGridFrame(params.time);
        const Mat4 m = params.projection * params.view;
        setCull(CullMode::Off);
        setBlend(GlBlend::AlphaBlend);
        setDepth(true, true); // delta 12.5: depth test and write on
        im.program.use();
        im.program.setMat4("uView", params.view);
        im.program.setMat4("uProj", params.projection);
        im.program.setFloat("uLevel", im.surface.level);
        im.program.setFloat("uOpacity", im.surface.opacity);
        im.program.setFloat("uTime", f.waveTime);
        im.program.setFloat("uLastRow", static_cast<float>(im.surface.height));
        im.program.setVec2("uBaseOffset", f.baseOffset);
        im.program.setVec2("uShineOffset", f.shineOffset);
        im.program.setVec3("uFogColor", params.fogColour);
        im.program.setFloat("uFogStart", params.fogStart);
        im.program.setFloat("uFogEnd", params.fogEnd);
        im.program.setInt("uBase", 0);
        im.program.setInt("uShine", 1);
        im.shine.bind(1);
        im.texture.bind(0);
        im.vao.bind();
        for (const GridChunk& ch : im.chunks) {
            if (ch.indexCount == 0 || !boxInClip(m, ch.bmin, ch.bmax)) continue;
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(ch.indexCount), GL_UNSIGNED_SHORT,
                           reinterpret_cast<const void*>(ch.firstIndex * sizeof(u16)));
            ++lastVisible_;
        }
        setDepth(true, true);
        setBlend(GlBlend::Off);
        return;
    }
    WaterParams wp = computeWaterParams(im.terrain->style(), params.time);

    // Cull quads against the frustum (x range 0..width, z = plane height).
    Mat4 m = params.projection * params.view;
    float width = static_cast<float>(im.terrain->width()) * kHmapCellSize;
    setCull(CullMode::Off);
    setBlend(GlBlend::AlphaBlend);
    setDepth(true, true); // render-pipeline.md 1.1 pass 7: water writes depth
    im.program.use();
    im.program.setMat4("uView", params.view);
    im.program.setMat4("uProj", params.projection);
    im.program.setFloat("uPlaneZ", wp.planeZ);
    im.program.setVec2("uTexOffset", wp.texOffset);
    im.program.setVec3("uColor", wp.colour);
    im.program.setFloat("uAlpha", wp.alpha);
    im.program.setVec3("uFogColor", params.fogColour);
    im.program.setFloat("uFogStart", params.fogStart);
    im.program.setFloat("uFogEnd", params.fogEnd);
    im.program.setInt("uTex0", 0);
    im.texture.bind(0);
    im.vao.bind();
    for (int k = 0; k < im.quads; k++) {
        float y0 = im.quadHeight * static_cast<float>(k), y1 = y0 + im.quadHeight;
        // conservative clip-space test of the quad's 4 corners
        bool anyIn = false;
        int outs[6] = {0, 0, 0, 0, 0, 0};
        const float xs[2] = {0.0f, width}, ys[2] = {y0, y1};
        for (int i = 0; i < 4; i++) {
            Vec4 c = m * Vec4{xs[i & 1], ys[i >> 1], wp.planeZ, 1.0f};
            if (c.x < -c.w) outs[0]++;
            if (c.x > c.w) outs[1]++;
            if (c.y < -c.w) outs[2]++;
            if (c.y > c.w) outs[3]++;
            if (c.z < -c.w) outs[4]++;
            if (c.z > c.w) outs[5]++;
        }
        anyIn = true;
        for (int i = 0; i < 6; i++) if (outs[i] == 4) anyIn = false;
        if (anyIn) {
            glDrawArrays(GL_TRIANGLES, k * 6, 6);
            ++lastVisible_;
        }
    }
    setDepth(true, true);
    setBlend(GlBlend::Off);
}

} // namespace as3d
