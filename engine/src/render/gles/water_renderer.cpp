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

namespace {

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
    WaterGridGeometry geo;
};

WaterRenderer::WaterRenderer() : impl_(new Impl) {}
WaterRenderer::~WaterRenderer() = default;
bool WaterRenderer::active() const { return impl_->active; }
bool WaterRenderer::grid() const { return impl_->grid; }
int WaterRenderer::wetCells() const { return impl_->geo.wetCells; }

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
        Image clear; // no shine layer: black under the base (seen only where the base alpha < 1)
        clear.width = clear.height = 1;
        clear.hasAlpha = true;
        clear.rgba = {0, 0, 0, 255};
        im.shine.create(clear, to);
    }

    im.geo = buildWaterGridGeometry(surface, grid);
    if (im.geo.verts.size() > 65535) {
        if (error) *error = "water grid too large for 16-bit indices";
        return false;
    }
    const std::vector<u16>& allIdx = im.geo.indices;
    im.vbo.upload(im.geo.verts.data(), im.geo.verts.size() * sizeof(WaterGridVertex), true);
    if (!allIdx.empty()) im.ibo.upload(allIdx.data(), allIdx.size() * sizeof(u16), IndexType::U16);
    VertexLayout l;
    l.strideBytes = sizeof(WaterGridVertex);
    l.attribs = {{0, 4, 0, false}};
    im.vao.create(im.vbo, l, allIdx.empty() ? nullptr : &im.ibo);
    im.grid = true;
    im.active = im.geo.wetCells > 0;
    return true;
}

void WaterRenderer::update(const VertexRect* rects, size_t count, const TerrainGridView& grid) {
    Impl& im = *impl_;
    lastUpdated_ = 0;
    if (!im.grid) return;
    for (int k : updateWaterGridHeights(im.geo, im.surface, rects, count, grid)) {
        const WaterGridChunk& ch = im.geo.chunks[static_cast<size_t>(k)];
        glBindBuffer(GL_ARRAY_BUFFER, im.vbo.id());
        glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(ch.firstVertex * sizeof(WaterGridVertex)),
                        static_cast<GLsizeiptr>(ch.vertexCount * sizeof(WaterGridVertex)), &im.geo.verts[ch.firstVertex]);
        ++lastUpdated_;
    }
}

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
        for (int k : visibleWaterChunks(im.geo, m)) {
            const WaterGridChunk& ch = im.geo.chunks[static_cast<size_t>(k)];
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
