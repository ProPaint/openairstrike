// Terrain-following decals, GL part: the shared decal drawer and the ground mark renderer. The
// geometry and the draw runs are neutral (engine/src/render/decal_build.cpp).
// docs/spec/render-pipeline.md 3.4 (marks), 5.3 and 5.4 (shadows draped over the terrain).
#include "as3d/gfx.h"
#include "as3d/ground_marks.h"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>

#include "decal_draw.h"

namespace as3d {

// ---------------------------------------------------------------------------
// Decal drawer
// ---------------------------------------------------------------------------

namespace {

const char* const kDecalVertexSrc = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
uniform mat4 uView;
uniform mat4 uProj;
out vec2 vUv;
out float vDepth;
void main() {
    vec4 eye = uView * vec4(aPos, 1.0);
    vDepth = -eye.z;
    vUv = aUv;
    gl_Position = uProj * eye;
}
)";

const char* const kDecalFragmentSrc = R"(#version 300 es
precision highp float;
in vec2 vUv;
in float vDepth;
uniform sampler2D uTex;
uniform vec3 uColour;
uniform float uAlpha;
uniform int uShadow;       // 1: (0, 0, 0, texture alpha); 2: grey 1 − texture alpha (multiplied)
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
out vec4 fragColor;
void main() {
    vec4 t = texture(uTex, vUv);
    vec4 c = uShadow == 1 ? vec4(0.0, 0.0, 0.0, t.a)
           : uShadow == 2 ? vec4(vec3(1.0 - t.a), 1.0)
           : vec4(t.rgb * uColour, t.a * uAlpha);
    float f = clamp((uFogEnd - vDepth) / max(uFogEnd - uFogStart, 1e-3), 0.0, 1.0);
    fragColor = vec4(mix(uFogColor, c.rgb, f), c.a);
}
)";

} // namespace

bool DecalDrawer::init(std::string* error) {
    if (program_.valid()) return true;
    if (!program_.compile(kDecalVertexSrc, kDecalFragmentSrc, error)) return false;
    DecalVertex dummy[3] = {};
    vbo_.upload(dummy, sizeof(dummy), true);
    capacity_ = sizeof(dummy);
    VertexLayout l;
    l.strideBytes = sizeof(DecalVertex);
    l.attribs = {{0, 3, offsetof(DecalVertex, x), false}, {1, 2, offsetof(DecalVertex, u), false}};
    vao_.create(vbo_, l);
    return true;
}

void DecalDrawer::draw(const DecalViewParams& params, const std::vector<DecalVertex>& vertices,
                       const std::vector<DecalBatch>& batches, DecalLook look) {
    if (!program_.valid() || vertices.empty() || batches.empty()) return;
    vbo_.upload(vertices.data(), vertices.size() * sizeof(DecalVertex), true);
    program_.use();
    program_.setMat4("uView", params.view);
    program_.setMat4("uProj", params.projection);
    program_.setFloat("uFogStart", params.fogStart);
    program_.setFloat("uFogEnd", params.fogEnd);
    program_.setInt("uTex", 0);
    program_.setInt("uShadow", look == DecalLook::ShadowAlpha ? 1 : look == DecalLook::ShadowMultiply ? 2 : 0);
    vao_.bind();

    runs_.clear();
    buildDecalRuns(batches, runs_);
    for (const DrawRun& r : runs_) {
        applyMaterialState(r.key);
        program_.setVec3("uFogColor", fogColorFor(r.key.fog, params.fogColour));
        program_.setVec3("uColour", {r.tint.x, r.tint.y, r.tint.z});
        program_.setFloat("uAlpha", r.tint.w);
        r.texture->bind(0);
        glDrawArrays(GL_TRIANGLES, r.first, r.count);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_BLEND);
    setDepth(true, true);
}

// ---------------------------------------------------------------------------
// Ground mark renderer
// ---------------------------------------------------------------------------

struct GroundMarkRenderer::Impl {
    struct Mark {
        std::vector<DecalVertex> vertices;
        const Texture2D* texture = nullptr;
        DecalBlend blend = DecalBlend::Filter;
        Vec3 colour;
        float alpha = 1.0f;
    };
    DecalDrawer drawer;
    std::vector<Mark> marks;
};

GroundMarkRenderer::GroundMarkRenderer() : impl_(new Impl) {}
GroundMarkRenderer::~GroundMarkRenderer() = default;

bool GroundMarkRenderer::init(std::string* error) { return impl_->drawer.init(error); }

int GroundMarkRenderer::addMark(const Terrain& terrain, const GroundMarkDesc& desc) {
    return addMark(TerrainGridView::of(terrain), desc);
}

int GroundMarkRenderer::addMark(const TerrainGridView& terrain, const GroundMarkDesc& desc) {
    if (impl_->marks.size() >= kMaxMarks) return -1;
    Impl::Mark m;
    // The rectangle stays axis aligned in the world and never rotates (spec 3.4).
    GroundRect rect = GroundRect::axisAligned(desc.origin.x + desc.minX, desc.origin.y + desc.minY,
                                              desc.origin.x + desc.maxX, desc.origin.y + desc.maxY);
    buildTerrainDecal(terrain, rect, m.vertices);
    if (m.vertices.empty()) return -1;
    if (desc.unflippedTexture) {
        for (DecalVertex& v : m.vertices) v.v = 1.0f - v.v; // v = t (as2 delta 3.4)
    }
    m.texture = desc.texture;
    m.blend = desc.blend;
    m.colour = desc.colour;
    m.alpha = desc.alpha;
    impl_->marks.push_back(std::move(m));
    return static_cast<int>(impl_->marks.size()) - 1;
}

void GroundMarkRenderer::clear() { impl_->marks.clear(); }
size_t GroundMarkRenderer::size() const { return impl_->marks.size(); }
size_t GroundMarkRenderer::triangleCount(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= impl_->marks.size()) return 0;
    return impl_->marks[static_cast<size_t>(index)].vertices.size() / 3;
}

void GroundMarkRenderer::draw(const DecalViewParams& params) {
    std::vector<DecalVertex> verts;
    std::vector<DecalBatch> batches;
    for (const Impl::Mark& m : impl_->marks) {
        DecalBatch b;
        b.first = verts.size();
        b.count = m.vertices.size();
        b.texture = m.texture;
        b.blend = m.blend;
        b.colour = m.colour;
        b.alpha = m.alpha;
        verts.insert(verts.end(), m.vertices.begin(), m.vertices.end());
        batches.push_back(b);
    }
    impl_->drawer.draw(params, verts, batches, DecalLook::Mark);
}

} // namespace as3d
