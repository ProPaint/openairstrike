// PICA (3DS) backend of the 2D quad batcher: Renderer2D::flush() through the flat shaders. See
// as3d/ui.h, as3d/batch2d.h (the neutral batch) and ui_pica.h (the contract and counters).
//
// The GL flush (gles/renderer2d_gl.cpp) in PICA terms:
//  * ortho: GL maps framebuffer pixels (origin top-left, y down) with uInvHalfSize; here
//    tilt * ortho(0, W, H, 0, -1, 1) of the current frame target's upright size (W x H = 400 x
//    240 or 320 x 240), z = 0 for every vertex. The begin() origin (GL convention, lower left)
//    shifts the layout inside the target the same way glViewport does.
//  * circles and rings: no fwidth, so the batch is built with tessellateShapes on (a vertex
//    alpha ramp one pixel wide at the edge, batch2d.h).
//  * blend: C3D_AlphaBlend with the factors of the GL table (Alpha SRC_ALPHA/ONE_MINUS_SRC_ALPHA,
//    Add ONE/ONE, Filter DST_COLOR/ZERO, Opaque ONE/ZERO), alpha channel ZERO/ONE as GL.
//  * TEV: texture * vertex colour (tevModulateTexColor); untextured runs use the vertex colour
//    alone (colours differ per quad inside one run, so tevConstantColor does not apply);
//    a second texture adds the stage of tevCombine2(mode) on texture unit 1 and the Flat2
//    shader, which carries the second texture coordinate.
//  * scissor: not touched, set by the caller with as3d::setScissor (GL rectangles).
#include <citro3d.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "as3d/batch2d.h"
#include "as3d/core.h"
#include "as3d/render_state.h"
#include "as3d/ui.h"
#include "pica_frame.h"
#include "pica_space.h"
#include "shader_set.h"
#include "tev.h"
#include "ui_pica.h"

namespace as3d::ui {

namespace {

Ui2dPicaStats g_stats;
bool g_snap = true;

// The vertex colour alone (RGB and alpha), for runs without a texture.
pica::TevConfig tevPrimaryOnly() {
    pica::TevConfig c;
    c.count = 1;
    c.stages[0].srcRgb[0] = pica::TevSrc::Primary;
    c.stages[0].srcA[0] = pica::TevSrc::Primary;
    c.stages[0].funcRgb = pica::TevFunc::Replace;
    c.stages[0].funcA = pica::TevFunc::Replace;
    return c;
}

void setBlendFor(Blend b) {
    switch (b) {
    case Blend::Alpha:
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ZERO, GPU_ONE);
        break;
    case Blend::Add:
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ONE, GPU_ZERO, GPU_ONE);
        break;
    case Blend::Filter:
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_DST_COLOR, GPU_ZERO, GPU_ZERO, GPU_ONE);
        break;
    case Blend::Opaque:
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ZERO, GPU_ONE);
        break;
    }
}

// Rounds the corners of every axis-aligned two-triangle quad of [first, first + count).
void snapQuads(std::vector<Vertex2D>& v, int first, int count) {
    if (count % 6 != 0) return;
    for (int i = first; i < first + count; i += 6) {
        Vertex2D* q = &v[static_cast<size_t>(i)];
        float xs[2], ys[2];
        int nx = 0, ny = 0;
        bool ok = true;
        for (int k = 0; k < 6 && ok; ++k) {
            const float x = q[k].x, y = q[k].y;
            int a = 0;
            while (a < nx && xs[a] != x) ++a;
            if (a == nx) {
                if (nx == 2) ok = false;
                else xs[nx++] = x;
            }
            int b = 0;
            while (b < ny && ys[b] != y) ++b;
            if (b == ny) {
                if (ny == 2) ok = false;
                else ys[ny++] = y;
            }
        }
        if (!ok || nx != 2 || ny != 2) continue;
        for (int k = 0; k < 6; ++k) {
            q[k].x = std::floor(q[k].x + 0.5f);
            q[k].y = std::floor(q[k].y + 0.5f);
        }
    }
}

} // namespace

const Ui2dPicaStats& ui2dPicaStats() { return g_stats; }
void ui2dPicaResetStats() { g_stats = Ui2dPicaStats(); }
void ui2dPicaSetSnap(bool on) { g_snap = on; }

struct Renderer2D::Impl {
    Texture2D white;
    std::vector<Vertex2D> verts;
    std::vector<Run2D> runs;
};

Renderer2D::Renderer2D() : impl_(new Impl) { mapping_ = computeMapping(320, 240); }
Renderer2D::~Renderer2D() = default;

bool Renderer2D::init(std::string* error) {
    Image px;
    px.width = px.height = 8;
    px.hasAlpha = false;
    px.rgba.assign(8 * 8 * 4, 255);
    TextureOptions to;
    to.minFilter = to.magFilter = Filter::Nearest;
    to.wrapS = to.wrapT = Wrap::ClampToEdge;
    to.cls = TextureClass::Opaque;
    impl_->white.create(px, to);
    if (!impl_->white.valid()) {
        if (error) *error = "cannot create the 2D white texture";
        return false;
    }
    return true;
}

int Renderer2D::flush() {
    Impl& im = *impl_;
    if (quads_.empty() || !pica::backendReady()) return 0;
    const pica::FrameTarget& target = pica::currentTarget();
    if (!target.target) return 0;

    Batch2DOptions opts;
    opts.halfPixelShift = halfPixelShift_;
    opts.tessellateShapes = true;
    opts.circleSegments = segments_;
    buildBatch2D(quads_, mapping_, im.verts, im.runs, opts);
    if (im.verts.empty()) return 0;
    if (g_snap)
        for (const Run2D& run : im.runs)
            if (run.tex && !run.shape) snapQuads(im.verts, run.first, run.count);

    // Vertex arrays in the frame arena: FlatVertex for every vertex, Flat2Vertex for the
    // ranges of the two-texture runs.
    pica::LinearArena& arena = pica::frameArena();
    const size_t count = im.verts.size();
    auto* flat = arena.allocArray<pica::FlatVertex>(count);
    size_t flat2Count = 0;
    for (const Run2D& run : im.runs)
        if (run.tex2 && run.combine2 != CombineMode::None) flat2Count += static_cast<size_t>(run.count);
    auto* flat2 = flat2Count ? arena.allocArray<pica::Flat2Vertex>(flat2Count) : nullptr;
    if (!flat || (flat2Count && !flat2)) {
        ++g_stats.arenaFailures;
        return 0;
    }
    g_stats.arenaBytes += count * sizeof(pica::FlatVertex) + flat2Count * sizeof(pica::Flat2Vertex);
    for (size_t i = 0; i < count; ++i) {
        const Vertex2D& s = im.verts[i];
        pica::FlatVertex& o = flat[i];
        o.pos[0] = s.x;
        o.pos[1] = s.y;
        o.pos[2] = 0.0f;
        o.uv[0] = s.u;
        o.uv[1] = s.v;
        for (int k = 0; k < 4; ++k) o.rgba[k] = s.rgba[k];
    }

    // The matrix: our pixel (x, y) is target pixel (x + originX, y + yTop), see the header.
    const int yTop = target.height - originY_ - mapping_.fbHeight;
    const float left = static_cast<float>(-originX_), right = static_cast<float>(target.width - originX_);
    const float top = static_cast<float>(-yTop), bottom = static_cast<float>(target.height - yTop);
    const Mat4 mvp = pica::picaTiltMatrix() * ortho(left, right, bottom, top, -1.0f, 1.0f);

    pica::ShaderSet& sh = pica::shaders();
    pica::disableFog();
    setDepth(false, false);
    setCull(CullMode::Off);
    C3D_AlphaTest(false, GPU_ALWAYS, 0);

    int calls = 0;
    size_t flat2Used = 0;
    for (const Run2D& run : im.runs) {
        if (run.count <= 0) continue;
        const bool two = run.tex2 && run.combine2 != CombineMode::None;
        const Texture2D* tex = run.tex;
        if (tex && !tex->valid()) {
            // A texture that is not loaded: callers create them outside frames (UiAssets).
            ++g_stats.missingTextures;
            AS3D_WARN("ui2d pica: draw with an unloaded texture, drawn white");
            tex = &im.white;
        }
        if (two && !tex) tex = &im.white;
        setBlendFor(run.blend);
        const void* vertices = flat;
        if (two) {
            for (int i = 0; i < run.count; ++i) {
                const Vertex2D& s = im.verts[static_cast<size_t>(run.first + i)];
                pica::Flat2Vertex& o = flat2[flat2Used + static_cast<size_t>(i)];
                o.pos[0] = s.x;
                o.pos[1] = s.y;
                o.pos[2] = 0.0f;
                o.uv[0] = s.u;
                o.uv[1] = s.v;
                for (int k = 0; k < 4; ++k) o.rgba[k] = s.rgba[k];
                o.uv2[0] = s.u2;
                o.uv2[1] = s.v2;
            }
            sh.bind(pica::Shader::Flat2);
            vertices = flat2 + flat2Used;
        } else {
            sh.bind(pica::Shader::Flat);
        }
        sh.setMvp(mvp);
        sh.setVertices(vertices);
        if (two) {
            pica::applyTev(pica::tevCombine2(static_cast<int>(run.combine2)));
            const Texture2D* t2 = run.tex2->valid() ? run.tex2 : &im.white;
            if (!run.tex2->valid()) ++g_stats.missingTextures;
            t2->bind(1);
            tex->bind(0);
            C3D_DrawArrays(GPU_TRIANGLES, 0, run.count);
            flat2Used += static_cast<size_t>(run.count);
            ++g_stats.flat2Runs;
        } else {
            if (tex) {
                pica::applyTev(pica::tevModulateTexColor());
                tex->bind(0);
            } else {
                pica::applyTev(tevPrimaryOnly());
            }
            C3D_DrawArrays(GPU_TRIANGLES, run.first, run.count);
        }
        ++calls;
        g_stats.vertices += run.count;
    }
    ++g_stats.flushes;
    g_stats.runs += calls;
    // The state the other PICA renderers expect.
    setBlend(GlBlend::Off);
    setDepth(true, true);
    setCull(CullMode::Back);
    return calls;
}

} // namespace as3d::ui
