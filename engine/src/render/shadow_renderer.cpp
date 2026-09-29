// Silhouette shadow generation and drawing, docs/spec/render-pipeline.md section 5.
#include "as3d/shadow_render.h"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>

#include "decal_draw.h"

namespace as3d {

namespace {

// Straight-down light for planar shadows.
const Vec3 kDown{0.0f, 0.0f, 1.0f};

struct Projected {
    float x, y, z;
};

Projected projectVertex(const Vec3& v, float cosR, float sinR, const Vec3& L) {
    float x = v.x * cosR - v.y * sinR;
    float y = v.x * sinR + v.y * cosR;
    return {x - v.z * L.x / L.z, y - v.z * L.y / L.z, v.z};
}

Vec3 effectiveLight(ShadowKind kind, const Vec3& towardsSun) {
    if (kind == ShadowKind::Planar) return kDown;
    Vec3 L = normalize(towardsSun);
    if (L.z < 0.05f) L.z = 0.05f; // every shipped level has L.z > 0; keep the projection finite
    return L;
}

const char* const kSilVertexSrc = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
uniform mat4 uMvp;
out vec2 vUv;
void main() {
    vUv = aUv;
    gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

// uMode 0: black with the skin's alpha; 1: flat colour.
const char* const kSilFragmentSrc = R"(#version 300 es
precision highp float;
in vec2 vUv;
uniform sampler2D uTex;
uniform int uMode;
uniform vec4 uColour;
out vec4 fragColor;
void main() {
    fragColor = uMode == 0 ? vec4(0.0, 0.0, 0.0, texture(uTex, vUv).a) : uColour;
}
)";

#ifdef __EMSCRIPTEN__
// The web version (docs/spec/issues/150): step 6 on the GPU. glReadPixels is a synchronous
// stall in WebGL, about 1.2 ms per silhouette and 110 of them per level, so the 2x2 box filter
// is a pass into the final texture instead: each output texel samples the supersampled
// silhouette halfway between its four texels (bilinear = their mean). Rows come out in the
// order Texture2D::create gives the CPU path (top row first). No CPU copy is kept.
const char* const kDownVertexSrc = R"(#version 300 es
void main() {
    vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);
    gl_Position = vec4(p, 0.0, 1.0);
}
)";
const char* const kDownFragmentSrc = R"(#version 300 es
precision highp float;
uniform sampler2D uTex;
uniform vec2 uSize;
out vec4 fragColor;
void main() {
    vec2 uv = vec2(gl_FragCoord.x / uSize.x, 1.0 - gl_FragCoord.y / uSize.y);
    float r = texture(uTex, uv).r;
    fragColor = vec4(vec3(114.0 / 255.0), 1.0 - r);
}
)";
#endif

} // namespace

ShadowBounds computeShadowBounds(const ModelData& model, ShadowKind kind, const Vec3& towardsSun, int rotationSteps) {
    Vec3 L = effectiveLight(kind, towardsSun);
    float a = kind == ShadowKind::Planar ? 0.0f : degToRad(static_cast<float>(rotationSteps) * kShadowRotationStepDegrees);
    float c = std::cos(a), s = std::sin(a);
    float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    for (const Vec3& v : model.positions) {
        Projected p = projectVertex(v, c, s, L);
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
    }
    ShadowBounds b;
    if (model.positions.empty()) return b;
    // A hair of tolerance so that float noise (cos of a quarter turn) does not widen the box.
    const float eps = 1e-3f;
    b.xmin = static_cast<int>(std::floor(minX + eps));
    b.ymin = static_cast<int>(std::floor(minY + eps));
    b.xmax = static_cast<int>(std::ceil(maxX - eps));
    b.ymax = static_cast<int>(std::ceil(maxY - eps));
    if (b.xmax <= b.xmin) b.xmax = b.xmin + 1;
    if (b.ymax <= b.ymin) b.ymax = b.ymin + 1;
    return b;
}

void shadowTextureSize(const ShadowBounds& bounds, ShadowQuality quality, int& width, int& height) {
    shadowTextureSize(bounds, quality, width, height, renderRules(GameId::AirStrike3D));
}

void shadowTextureSize(const ShadowBounds& bounds, ShadowQuality quality, int& width, int& height,
                       const RenderRules& rules) {
    float w = static_cast<float>(bounds.xmax - bounds.xmin);
    float h = static_cast<float>(bounds.ymax - bounds.ymin);
    const int capY = rules.shadowHeightExpCap;
    const float limitY = static_cast<float>(1 << capY);
    float ex = w < 256.0f ? std::log2(std::max(w, 1.0f)) : 8.0f;
    float ey = h < limitY ? std::log2(std::max(h, 1.0f)) : static_cast<float>(capY);
    int W = 1 << static_cast<int>(ex + 0.5f);
    int H = 1 << static_cast<int>(ey + 0.5f);
    if (quality == ShadowQuality::Low) { W /= 2; H /= 2; }
    else if (quality == ShadowQuality::High) { W *= 2; H *= 2; }
    if (rules.shadowMaxSide > 0) { // as2 delta 5.2 step 1: halved until 2·side <= 2·max
        while (W > rules.shadowMaxSide) W /= 2;
        while (H > rules.shadowMaxSide) H /= 2;
    }
    width = std::max(W, 1);
    height = std::max(H, 1);
}

GroundRect shadowRect(const ShadowMap& map, const Vec3& origin, float yawDegrees) {
    const ShadowBounds& b = map.bounds;
    if (map.kind == ShadowKind::Projected) {
        return GroundRect::axisAligned(origin.x + static_cast<float>(b.xmin), origin.y + static_cast<float>(b.ymin),
                                       origin.x + static_cast<float>(b.xmax), origin.y + static_cast<float>(b.ymax));
    }
    return GroundRect::rotated({origin.x, origin.y}, yawDegrees, static_cast<float>(b.xmin), static_cast<float>(b.ymin),
                               static_cast<float>(b.xmax), static_cast<float>(b.ymax));
}

struct ShadowRenderer::Impl {
    DecalDrawer drawer;
    ShaderProgram silProgram;
    RenderRules rules = renderRules(GameId::AirStrike3D);
#ifdef __EMSCRIPTEN__
    ShaderProgram downProgram;
#endif
};

ShadowRenderer::ShadowRenderer() : impl_(new Impl) {}
ShadowRenderer::~ShadowRenderer() = default;

void ShadowRenderer::setRules(const RenderRules& rules) { impl_->rules = rules; }

bool ShadowRenderer::init(std::string* error) {
    if (!impl_->drawer.init(error)) return false;
#ifdef __EMSCRIPTEN__
    if (!impl_->downProgram.valid() && !impl_->downProgram.compile(kDownVertexSrc, kDownFragmentSrc, error)) return false;
#endif
    if (impl_->silProgram.valid()) return true;
    return impl_->silProgram.compile(kSilVertexSrc, kSilFragmentSrc, error);
}

bool ShadowRenderer::generate(const ModelData& model, const Texture2D* skin, ShadowKind kind, const Vec3& towardsSun,
                              int rotationSteps, ShadowQuality quality, ShadowMap& out) {
    if (!impl_->silProgram.valid() || model.faces.empty() || model.positions.empty()) return false;
    if (kind == ShadowKind::Planar) rotationSteps = 0;
    Vec3 L = effectiveLight(kind, towardsSun);
    ShadowBounds bounds = computeShadowBounds(model, kind, towardsSun, rotationSteps);
    int W, H;
    const RenderRules& rules = impl_->rules;
    shadowTextureSize(bounds, quality, W, H, rules);
    const int rw = 2 * W, rh = 2 * H; // supersampled render size
    const float sx = static_cast<float>(rw) / static_cast<float>(bounds.xmax - bounds.xmin);
    const float sy = static_cast<float>(rh) / static_cast<float>(bounds.ymax - bounds.ymin);

    // Triangle corners: (x, y, z) in silhouette pixels plus the model UV.
    float a = degToRad(static_cast<float>(rotationSteps) * kShadowRotationStepDegrees);
    float c = std::cos(a), s = std::sin(a);
    std::vector<float> verts;
    verts.reserve(model.faces.size() * 15 + 30);
    float zmin = 1e30f, zmax = -1e30f;
    for (const Face& f : model.faces) {
        for (int k = 0; k < 3; k++) {
            if (f.v[k] >= model.positions.size()) continue;
            Projected p = projectVertex(model.positions[f.v[k]], c, s, L);
            Vec2 uv = f.uv[k] < model.uvs.size() ? model.uvs[f.uv[k]] : Vec2{};
            verts.insert(verts.end(), {(p.x - static_cast<float>(bounds.xmin)) * sx,
                                       (p.y - static_cast<float>(bounds.ymin)) * sy, p.z, uv.x, uv.y});
            zmin = std::min(zmin, p.z);
            zmax = std::max(zmax, p.z);
        }
    }
    size_t meshVerts = verts.size() / 5;
    if (meshVerts < 3) return false;
    zmin = std::min(zmin, 0.0f);
    zmax = std::max(zmax, 0.0f);
    // Two rectangle quads (z = 0), appended after the mesh.
    const float fw = static_cast<float>(rw), fh = static_cast<float>(rh);
    const float quad[6][5] = {{0, 0, 0, 0, 0}, {fw, 0, 0, 0, 0}, {fw, fh, 0, 0, 0},
                              {0, 0, 0, 0, 0}, {fw, fh, 0, 0, 0}, {0, fh, 0, 0, 0}};
    for (auto& q : quad) verts.insert(verts.end(), q, q + 5);

    // Save the caller's framebuffer state.
    GLint prevFbo = 0, prevViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    RenderTarget target;
    if (!target.create(rw, rh, 0)) {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
        return false;
    }
    VertexBuffer vbo;
    vbo.upload(verts.data(), verts.size() * sizeof(float));
    VertexLayout layout;
    layout.strideBytes = 5 * sizeof(float);
    layout.attribs = {{0, 3, 0, false}, {1, 2, 3 * sizeof(float), false}};
    VertexArray vao;
    vao.create(vbo, layout);

    target.bind();
    clear({1.0f, 1.0f, 1.0f, 1.0f}, true);
    ShaderProgram& prog = impl_->silProgram;
    prog.use();
    prog.setMat4("uMvp", ortho(0.0f, fw, 0.0f, fh, -(zmax + 1.0f), -(zmin - 1.0f)));
    prog.setInt("uTex", 0);
    vao.bind();
    glDisable(GL_CULL_FACE);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    // Step 3: the model in black, blended by its skin alpha, nearest-to-light (highest z) wins.
    setDepth(true, true);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (skin) {
        skin->bind(0);
        prog.setInt("uMode", 0);
    } else {
        prog.setInt("uMode", 1);
        prog.setVec4("uColour", {0.0f, 0.0f, 0.0f, 1.0f});
    }
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(meshVerts));

    // Step 4 (projected shadows only): a white quad at z = 0 erases what is at or below the
    // ground, so foundations sunk into the terrain cast no shadow.
    prog.setInt("uMode", 1);
    if (kind == ShadowKind::Projected && rules.shadowEraseBelowGround) {
        glDisable(GL_BLEND);
        prog.setVec4("uColour", {1.0f, 1.0f, 1.0f, 1.0f});
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(meshVerts), 6);
    }

    // Step 5: add 0.6 grey everywhere: covered pixels end at 0.6, the rest saturates at 1.
    setDepth(false, false);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    prog.setVec4("uColour", {0.6f, 0.6f, 0.6f, 1.0f});
    glDrawArrays(GL_TRIANGLES, static_cast<GLint>(meshVerts), 6);
    glDisable(GL_BLEND);
    setDepth(true, true);

#ifdef __EMSCRIPTEN__
    {
        Image blank;
        blank.width = W;
        blank.height = H;
        blank.hasAlpha = true;
        blank.rgba.assign(static_cast<size_t>(W) * static_cast<size_t>(H) * 4, 0);
        TextureOptions opts;
        opts.mipmaps = rules.shadowMipmaps;
        opts.wrapS = Wrap::ClampToEdge;
        opts.wrapT = Wrap::ClampToEdge;
        out.texture.create(blank, opts);
        GLuint fbo = 0;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, out.texture.id(), 0);
        const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        if (complete) {
            glViewport(0, 0, W, H);
            setDepth(false, false);
            glDisable(GL_BLEND);
            ShaderProgram& down = impl_->downProgram;
            down.use();
            down.setInt("uTex", 0);
            down.setVec2("uSize", {static_cast<float>(W), static_cast<float>(H)});
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, target.colorTextureId());
            glBindVertexArray(0);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            setDepth(true, true);
            glBindTexture(GL_TEXTURE_2D, out.texture.id());
            if (rules.shadowMipmaps) glGenerateMipmap(GL_TEXTURE_2D);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
        glDeleteFramebuffers(1, &fbo);
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        if (!complete) return false;
        out.image = Image();
        out.bounds = bounds;
        out.kind = kind;
        out.width = W;
        out.height = H;
        return out.texture.valid();
    }
#endif
    Image big;
    bool ok = target.readPixels(big);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (!ok) return false;

    // Step 6: 2x2 box filter to W x H, RGB (114, 114, 114), alpha = 255 - red.
    Image img;
    img.width = W;
    img.height = H;
    img.hasAlpha = true;
    img.rgba.resize(static_cast<size_t>(W) * static_cast<size_t>(H) * 4);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int sum = 0;
            for (int dy = 0; dy < 2; dy++)
                for (int dx = 0; dx < 2; dx++)
                    sum += big.rgba[(static_cast<size_t>(2 * y + dy) * static_cast<size_t>(rw) + static_cast<size_t>(2 * x + dx)) * 4];
            u8* px = &img.rgba[(static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x)) * 4];
            px[0] = px[1] = px[2] = 114;
            px[3] = static_cast<u8>(255 - sum / 4);
        }
    }
    TextureOptions opts;
    opts.mipmaps = rules.shadowMipmaps;
    opts.wrapS = Wrap::ClampToEdge;
    opts.wrapT = Wrap::ClampToEdge;
    out.texture.create(img, opts);
    out.image = std::move(img);
    out.bounds = bounds;
    out.kind = kind;
    out.width = W;
    out.height = H;
    return out.texture.valid();
}

void ShadowRenderer::draw(const Terrain& terrain, const ShadowInstance* instances, size_t count,
                          const DecalViewParams& params) {
    draw(TerrainGridView::of(terrain), instances, count, params);
}

void ShadowRenderer::draw(const TerrainGridView& terrain, const ShadowInstance* instances, size_t count,
                          const DecalViewParams& params) {
    const bool multiply = impl_->rules.shadowMultiply;
    std::vector<DecalVertex> verts;
    std::vector<DecalBatch> batches;
    for (size_t i = 0; i < count; i++) {
        const ShadowInstance& inst = instances[i];
        if (!inst.map || !inst.map->valid()) continue;
        DecalBatch b;
        b.first = verts.size();
        buildTerrainDecal(terrain, shadowRect(*inst.map, inst.origin, inst.yawDegrees), verts);
        b.count = verts.size() - b.first;
        if (b.count == 0) continue;
        b.texture = &inst.map->texture;
        b.blend = multiply ? DecalBlend::Filter : DecalBlend::Alpha;
        batches.push_back(b);
    }
    lastTriangles_ = verts.size() / 3;
    impl_->drawer.draw(params, verts, batches, multiply ? DecalLook::ShadowMultiply : DecalLook::ShadowAlpha);
}

} // namespace as3d
