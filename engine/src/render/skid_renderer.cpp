// Skid-mark trails, docs/spec/as2/render-pipeline.delta.md 7.7 (as3d/skid_render.h).
#include "as3d/skid_render.h"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>

#include "as3d/scene.h"

namespace as3d {

SkidTrailParams skidTrailParams(const GameRules& rules) {
    SkidTrailParams p;
    if (rules.skidTrailPool > 0) p.maxTrails = rules.skidTrailPool;
    if (rules.skidMaxNodes > 0) p.maxNodes = rules.skidMaxNodes;
    if (rules.skidLife > 0.0f) {
        p.life = rules.skidLife;
        p.fadeStart = rules.skidFadeStart;
    }
    p.heightOffset = rules.skidHeightOffset;
    return p;
}

float skidNodeAlpha(float age, const SkidTrailParams& p) {
    if (age <= p.fadeStart) return 1.0f;
    if (age >= p.life) return 0.0f;
    const float span = p.life - p.fadeStart;
    return span > 0.0f ? 1.0f - (age - p.fadeStart) / span : 0.0f;
}

size_t buildSkidTrailStrip(const SkidTrail& trail, const TerrainGridView& grid, const SkidTrailParams& params,
                           std::vector<SkidVertex>& out) {
    const size_t n = trail.nodes.size();
    const size_t keep = std::min(n, static_cast<size_t>(std::max(params.maxNodes, 0)));
    if (keep < 2) return 0;
    const size_t first = n - keep;
    const size_t before = out.size();
    for (size_t i = first; i < n; ++i) {
        const SkidNode& nd = trail.nodes[i];
        Vec2 fwd = nd.direction;
        const float len = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y);
        if (len > 0.0f) fwd = Vec2{fwd.x / len, fwd.y / len};
        const Vec2 lat{fwd.y, -fwd.x}; // X̂: the vehicle's right-hand side
        const float hw = nd.width * 0.5f;
        const float a = skidNodeAlpha(nd.age, params);
        const float v = nd.width != 0.0f ? nd.distance / nd.width : 0.0f;
        const Vec2 p0{nd.position.x - lat.x * hw, nd.position.y - lat.y * hw};
        const Vec2 p1{nd.position.x + lat.x * hw, nd.position.y + lat.y * hw};
        out.push_back({p0.x, p0.y, grid.heightAt(p0.x, p0.y) + params.heightOffset, 0.0f, v, a});
        out.push_back({p1.x, p1.y, grid.heightAt(p1.x, p1.y) + params.heightOffset, 1.0f, v, a});
    }
    return out.size() - before;
}

namespace {

const char* const kSkidVertexSrc = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in float aAlpha;
uniform mat4 uView;
uniform mat4 uProj;
out vec2 vUv;
out float vAlpha;
out float vDepth;
void main() {
    vec4 eye = uView * vec4(aPos, 1.0);
    vDepth = -eye.z;
    vUv = aUv;
    vAlpha = aAlpha;
    gl_Position = uProj * eye;
}
)";

const char* const kSkidFragmentSrc = R"(#version 300 es
precision highp float;
in vec2 vUv;
in float vAlpha;
in float vDepth;
uniform sampler2D uTex;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
out vec4 fragColor;
void main() {
    vec4 t = texture(uTex, vUv);
    float f = clamp((uFogEnd - vDepth) / max(uFogEnd - uFogStart, 1e-3), 0.0, 1.0);
    fragColor = vec4(mix(uFogColor, t.rgb, f), t.a * vAlpha);
}
)";

} // namespace

struct SkidTrailRenderer::Impl {
    ResourceCache* cache = nullptr;
    SkidTrailParams params;
    ShaderProgram program;
    VertexBuffer vbo;
    VertexArray vao;
    size_t capacity = 0; // vertices
    std::vector<SkidVertex> verts;
    std::vector<std::pair<size_t, size_t>> ranges; // first, count per drawn trail
    std::vector<const Texture2D*> textures;
};

SkidTrailRenderer::SkidTrailRenderer() : impl_(new Impl) {}
SkidTrailRenderer::~SkidTrailRenderer() = default;

bool SkidTrailRenderer::init(ResourceCache& cache, const SkidTrailParams& params, std::string* error) {
    Impl& im = *impl_;
    im.cache = &cache;
    im.params = params;
    if (!im.program.valid() && !im.program.compile(kSkidVertexSrc, kSkidFragmentSrc, error)) return false;
    im.capacity = static_cast<size_t>(std::max(params.maxTrails, 1)) * static_cast<size_t>(std::max(params.maxNodes, 2)) * 2;
    im.verts.reserve(im.capacity);
    im.ranges.reserve(static_cast<size_t>(std::max(params.maxTrails, 1)));
    im.textures.reserve(static_cast<size_t>(std::max(params.maxTrails, 1)));
    im.vbo.upload(nullptr, im.capacity * sizeof(SkidVertex), true);
    VertexLayout l;
    l.strideBytes = sizeof(SkidVertex);
    l.attribs = {{0, 3, offsetof(SkidVertex, x), false},
                 {1, 2, offsetof(SkidVertex, u), false},
                 {2, 1, offsetof(SkidVertex, alpha), false}};
    im.vao.create(im.vbo, l);
    return true;
}

void SkidTrailRenderer::draw(const SkidTrail* trails, size_t count, const TerrainGridView& grid,
                             const DecalViewParams& view) {
    Impl& im = *impl_;
    lastTrails_ = 0;
    lastVertices_ = 0;
    if (!im.program.valid() || !im.cache || !grid.valid() || count == 0) return;
    im.verts.clear();
    im.ranges.clear();
    im.textures.clear();
    const size_t maxTrails = static_cast<size_t>(std::max(im.params.maxTrails, 0));
    for (size_t i = 0; i < count && im.ranges.size() < maxTrails; ++i) {
        const SkidTrail& t = trails[i];
        if (t.texture.empty()) continue;
        const size_t first = im.verts.size();
        const size_t n = buildSkidTrailStrip(t, grid, im.params, im.verts);
        if (n == 0) continue;
        if (im.verts.size() > im.capacity) { // cannot happen with the params of init()
            im.verts.resize(first);
            break;
        }
        im.ranges.push_back({first, n});
        im.textures.push_back(im.cache->texture(t.texture).texture);
    }
    // A trail counts once it has at least one section pair, as the original's draw list.
    lastTrails_ = static_cast<int>(im.ranges.size());
    lastVertices_ = im.verts.size();
    if (im.ranges.empty()) return;

    glBindBuffer(GL_ARRAY_BUFFER, im.vbo.id());
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(im.verts.size() * sizeof(SkidVertex)), im.verts.data());
    im.program.use();
    im.program.setMat4("uView", view.view);
    im.program.setMat4("uProj", view.projection);
    im.program.setVec3("uFogColor", view.fogColour);
    im.program.setFloat("uFogStart", view.fogStart);
    im.program.setFloat("uFogEnd", view.fogEnd);
    im.program.setInt("uTex", 0);
    im.vao.bind();
    setDepth(true, false);
    setCull(CullMode::Back);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -1.0f);
    for (size_t k = 0; k < im.ranges.size(); ++k) {
        if (im.textures[k]) im.textures[k]->bind(0);
        glDrawArrays(GL_TRIANGLE_STRIP, static_cast<GLint>(im.ranges[k].first), static_cast<GLsizei>(im.ranges[k].second));
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_BLEND);
    setDepth(true, true);
}

} // namespace as3d
