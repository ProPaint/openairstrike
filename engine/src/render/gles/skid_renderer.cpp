// Skid-mark trails, GL part (geometry and runs: engine/src/render/skid_build.cpp),
// docs/spec/as2/render-pipeline.delta.md 7.7 (as3d/skid_render.h).
#include "as3d/skid_render.h"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>

#include "as3d/gfx.h"
#include "as3d/scene.h"

namespace as3d {

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
    std::vector<DrawRun> runs;
};

SkidTrailRenderer::SkidTrailRenderer() : impl_(new Impl) {}
SkidTrailRenderer::~SkidTrailRenderer() = default;

bool SkidTrailRenderer::init(ResourceCache& cache, const SkidTrailParams& params, std::string* error) {
    Impl& im = *impl_;
    im.cache = &cache;
    im.params = params;
    if (!im.program.valid() && !im.program.compile(kSkidVertexSrc, kSkidFragmentSrc, error)) return false;
    im.capacity = skidVertexCapacity(params);
    im.verts.reserve(im.capacity);
    im.runs.reserve(static_cast<size_t>(std::max(params.maxTrails, 1)));
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
    im.runs.clear();
    // A trail counts once it has at least one section pair, as the original's draw list.
    lastTrails_ = buildSkidTrails(
        trails, count, grid, im.params, [&](const std::string& path) { return im.cache->texture(path).texture; },
        im.verts, im.runs);
    lastVertices_ = im.verts.size();
    if (im.runs.empty()) return;

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
    for (const DrawRun& r : im.runs) {
        applyMaterialState(r.key);
        if (r.texture) r.texture->bind(0);
        glDrawArrays(GL_TRIANGLE_STRIP, r.first, r.count);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_BLEND);
    setDepth(true, true);
}

} // namespace as3d
