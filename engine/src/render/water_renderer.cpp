// Water plane renderer (docs/spec/hmap.md "Water").
#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>

#include "as3d/terrain_render.h"

namespace as3d {

extern const char* const kWaterVertexSrc;
extern const char* const kWaterFragmentSrc;

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

struct WaterRenderer::Impl {
    const Terrain* terrain = nullptr;
    bool active = false;
    ShaderProgram program;
    Texture2D texture;
    VertexBuffer vbo;
    VertexArray vao;
    int quads = 0;
    float quadHeight = 320.0f;
};

WaterRenderer::WaterRenderer() : impl_(new Impl) {}
WaterRenderer::~WaterRenderer() = default;
bool WaterRenderer::active() const { return impl_->active; }

bool WaterRenderer::build(const Terrain& terrain, Vfs& vfs, std::string* error) {
    Impl& im = *impl_;
    im = Impl{};
    missing_ = 0;
    if (!terrain.style().hasWater || terrain.style().waterTexture.empty()) return true;
    im.terrain = &terrain;
    if (!im.program.compile(kWaterVertexSrc, kWaterFragmentSrc, error)) return false;

    // The texture name has its extension replaced by .tga before loading.
    std::string path = terrain.style().waterTexture;
    size_t dot = path.find_last_of('.');
    size_t slash = path.find_last_of("/\\");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) path.resize(dot);
    path += ".tga";
    Blob blob;
    Image img;
    if (!vfs.read(path, blob) || !decodeTga(blob.data(), blob.size(), img)) {
        AS3D_WARN("water: missing texture '%s'", path.c_str());
        missing_++;
        img.width = img.height = 4;
        img.rgba.assign(64, 0);
        for (int i = 0; i < 16; i++) { img.rgba[i * 4] = 255; img.rgba[i * 4 + 2] = 255; img.rgba[i * 4 + 3] = 255; }
    }
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

void WaterRenderer::render(const TerrainViewParams& params) {
    Impl& im = *impl_;
    if (!im.active) return;
    WaterParams wp = computeWaterParams(im.terrain->style(), params.time);

    // Cull quads against the frustum (x range 0..width, z = plane height).
    Mat4 m = params.projection * params.view;
    float width = static_cast<float>(im.terrain->width()) * kHmapCellSize;
    setCull(CullMode::Off);
    setBlend(BlendMode::AlphaBlend);
    setDepth(true, false);
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
        if (anyIn) glDrawArrays(GL_TRIANGLES, k * 6, 6);
    }
    setDepth(true, true);
    setBlend(BlendMode::Off);
}

} // namespace as3d
