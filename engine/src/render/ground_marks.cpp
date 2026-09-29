// Terrain-following decals: geometry, the shared decal drawer and the ground mark renderer.
// docs/spec/render-pipeline.md 3.4 (marks), 5.3 and 5.4 (shadows draped over the terrain).
#include "as3d/ground_marks.h"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>

#include "decal_draw.h"

namespace as3d {

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

GroundRect GroundRect::axisAligned(float x0, float y0, float x1, float y1) {
    GroundRect r;
    r.origin = {x0, y0};
    r.sizeX = x1 - x0;
    r.sizeY = y1 - y0;
    return r;
}

GroundRect GroundRect::rotated(const Vec2& pivot, float yawDegrees, float xmin, float ymin, float xmax, float ymax) {
    float a = degToRad(yawDegrees);
    float c = std::cos(a), s = std::sin(a);
    GroundRect r;
    r.axisX = {c, s};
    r.axisY = {-s, c};
    r.origin = {pivot.x + xmin * c - ymin * s, pivot.y + xmin * s + ymin * c};
    r.sizeX = xmax - xmin;
    r.sizeY = ymax - ymin;
    return r;
}

Vec2 GroundRect::coords(float x, float y) const {
    Vec2 d{x - origin.x, y - origin.y};
    return {sizeX != 0.0f ? dot(d, axisX) / sizeX : 0.0f, sizeY != 0.0f ? dot(d, axisY) / sizeY : 0.0f};
}

namespace {

struct P3 {
    float x, y, z;
};

// Clips a convex polygon against the half plane w(p) >= 0, where w is linear in xy.
template <typename F>
void clipPolygon(std::vector<P3>& poly, F w) {
    std::vector<P3> out;
    size_t n = poly.size();
    for (size_t i = 0; i < n; i++) {
        const P3& a = poly[i];
        const P3& b = poly[(i + 1) % n];
        float wa = w(a), wb = w(b);
        bool ina = wa >= 0.0f, inb = wb >= 0.0f;
        if (ina) out.push_back(a);
        if (ina != inb) {
            float t = wa / (wa - wb);
            out.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t});
        }
    }
    poly.swap(out);
}

void emitClipped(const GroundRect& rect, P3 a, P3 b, P3 c, std::vector<DecalVertex>& out) {
    std::vector<P3> poly{a, b, c};
    auto sCoord = [&](const P3& p) { return (p.x - rect.origin.x) * rect.axisX.x + (p.y - rect.origin.y) * rect.axisX.y; };
    auto tCoord = [&](const P3& p) { return (p.x - rect.origin.x) * rect.axisY.x + (p.y - rect.origin.y) * rect.axisY.y; };
    clipPolygon(poly, [&](const P3& p) { return sCoord(p); });
    if (poly.size() < 3) return;
    clipPolygon(poly, [&](const P3& p) { return rect.sizeX - sCoord(p); });
    if (poly.size() < 3) return;
    clipPolygon(poly, [&](const P3& p) { return tCoord(p); });
    if (poly.size() < 3) return;
    clipPolygon(poly, [&](const P3& p) { return rect.sizeY - tCoord(p); });
    if (poly.size() < 3) return;
    auto vert = [&](const P3& p) {
        Vec2 st = rect.coords(p.x, p.y);
        return DecalVertex{p.x, p.y, p.z, st.x, 1.0f - st.y};
    };
    for (size_t i = 1; i + 1 < poly.size(); i++) {
        out.push_back(vert(poly[0]));
        out.push_back(vert(poly[i]));
        out.push_back(vert(poly[i + 1]));
    }
}

} // namespace

void buildTerrainDecal(const Terrain& terrain, const GroundRect& rect, std::vector<DecalVertex>& out) {
    if (terrain.positions().empty() || rect.sizeX <= 0.0f || rect.sizeY <= 0.0f) return;
    Vec2 corners[4] = {rect.origin,
                       rect.origin + rect.axisX * rect.sizeX,
                       rect.origin + rect.axisX * rect.sizeX + rect.axisY * rect.sizeY,
                       rect.origin + rect.axisY * rect.sizeY};
    float minX = corners[0].x, maxX = corners[0].x, minY = corners[0].y, maxY = corners[0].y;
    for (int i = 1; i < 4; i++) {
        minX = std::min(minX, corners[i].x); maxX = std::max(maxX, corners[i].x);
        minY = std::min(minY, corners[i].y); maxY = std::max(maxY, corners[i].y);
    }
    int W = terrain.width(), H = terrain.height();
    int c0 = std::max(0, static_cast<int>(std::floor(minX / kHmapCellSize)));
    int c1 = std::min(W - 1, static_cast<int>(std::floor(maxX / kHmapCellSize)));
    int r0 = std::max(0, static_cast<int>(std::floor(minY / kHmapCellSize)));
    int r1 = std::min(H - 1, static_cast<int>(std::floor(maxY / kHmapCellSize)));
    int vw = terrain.vertsWide();
    const auto& pos = terrain.positions();
    for (int r = r0; r <= r1; r++) {
        for (int c = c0; c <= c1; c++) {
            const Vec3& v00 = pos[static_cast<size_t>(r) * vw + c];
            const Vec3& v10 = pos[static_cast<size_t>(r) * vw + c + 1];
            const Vec3& v01 = pos[static_cast<size_t>(r + 1) * vw + c];
            const Vec3& v11 = pos[static_cast<size_t>(r + 1) * vw + c + 1];
            // The terrain renderer's triangles: (v00, v10, v11) and (v00, v11, v01).
            emitClipped(rect, {v00.x, v00.y, v00.z}, {v10.x, v10.y, v10.z}, {v11.x, v11.y, v11.z}, out);
            emitClipped(rect, {v00.x, v00.y, v00.z}, {v11.x, v11.y, v11.z}, {v01.x, v01.y, v01.z}, out);
        }
    }
}

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
uniform int uShadow;       // 1: (0, 0, 0, texture alpha), the shadow look
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
out vec4 fragColor;
void main() {
    vec4 t = texture(uTex, vUv);
    vec4 c = uShadow != 0 ? vec4(0.0, 0.0, 0.0, t.a) : vec4(t.rgb * uColour, t.a);
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
                       const std::vector<DecalBatch>& batches, bool shadowMode) {
    if (!program_.valid() || vertices.empty() || batches.empty()) return;
    vbo_.upload(vertices.data(), vertices.size() * sizeof(DecalVertex), true);
    program_.use();
    program_.setMat4("uView", params.view);
    program_.setMat4("uProj", params.projection);
    program_.setFloat("uFogStart", params.fogStart);
    program_.setFloat("uFogEnd", params.fogEnd);
    program_.setInt("uTex", 0);
    program_.setInt("uShadow", shadowMode ? 1 : 0);
    vao_.bind();

    setDepth(true, false);
    setCull(CullMode::Back);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -1.0f);
    for (const DecalBatch& b : batches) {
        if (!b.texture || b.count == 0) continue;
        Vec3 fog = params.fogColour;
        switch (b.blend) {
            case DecalBlend::None: glDisable(GL_BLEND); break;
            case DecalBlend::Alpha: glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
            case DecalBlend::Add: glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE); fog = {0, 0, 0}; break;
            case DecalBlend::Filter: glEnable(GL_BLEND); glBlendFunc(GL_DST_COLOR, GL_ZERO); fog = {1, 1, 1}; break;
        }
        program_.setVec3("uFogColor", fog);
        program_.setVec3("uColour", b.colour);
        b.texture->bind(0);
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(b.first), static_cast<GLsizei>(b.count));
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
    };
    DecalDrawer drawer;
    std::vector<Mark> marks;
};

GroundMarkRenderer::GroundMarkRenderer() : impl_(new Impl) {}
GroundMarkRenderer::~GroundMarkRenderer() = default;

bool GroundMarkRenderer::init(std::string* error) { return impl_->drawer.init(error); }

int GroundMarkRenderer::addMark(const Terrain& terrain, const GroundMarkDesc& desc) {
    if (impl_->marks.size() >= kMaxMarks) return -1;
    Impl::Mark m;
    // The rectangle stays axis aligned in the world and never rotates (spec 3.4).
    GroundRect rect = GroundRect::axisAligned(desc.origin.x + desc.minX, desc.origin.y + desc.minY,
                                              desc.origin.x + desc.maxX, desc.origin.y + desc.maxY);
    buildTerrainDecal(terrain, rect, m.vertices);
    if (m.vertices.empty()) return -1;
    m.texture = desc.texture;
    m.blend = desc.blend;
    m.colour = desc.colour;
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
        verts.insert(verts.end(), m.vertices.begin(), m.vertices.end());
        batches.push_back(b);
    }
    impl_->drawer.draw(params, verts, batches, false);
}

} // namespace as3d
