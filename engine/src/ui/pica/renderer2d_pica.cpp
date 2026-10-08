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
#include <vector>

#include "as3d/batch2d.h"
#include "as3d/core.h"
#include "as3d/render_state.h"
#include "as3d/ui.h"
#include "pass_profile.h"
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
        pica::detail::c3dAlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ZERO, GPU_ONE);
        break;
    case Blend::Add:
        pica::detail::c3dAlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ONE, GPU_ZERO, GPU_ONE);
        break;
    case Blend::Filter:
        pica::detail::c3dAlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_DST_COLOR, GPU_ZERO, GPU_ZERO, GPU_ONE);
        break;
    case Blend::Opaque:
        pica::detail::c3dAlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ZERO, GPU_ONE);
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

bool sameColor(const Color& a, const Color& b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

bool sameQuad(const Quad& a, const Quad& b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h && a.s0 == b.s0 && a.t0 == b.t0 && a.s1 == b.s1 &&
           a.t1 == b.t1 && a.texture == b.texture && sameColor(a.color, b.color) && a.blend == b.blend &&
           a.line == b.line && a.shape == b.shape && a.inner == b.inner && a.feather == b.feather &&
           a.rotation == b.rotation && a.texture2 == b.texture2 && a.s0b == b.s0b && a.t0b == b.t0b &&
           a.s1b == b.s1b && a.t1b == b.t1b && a.combine2 == b.combine2;
}

bool sameMapping(const Mapping& a, const Mapping& b) {
    return a.fbWidth == b.fbWidth && a.fbHeight == b.fbHeight && a.scaleX == b.scaleX && a.scaleY == b.scaleY &&
           a.offsetX == b.offsetX && a.offsetY == b.offsetY;
}

// The converted vertices of one quad list, kept in linear memory of its own between frames
// (docs/3ds-port.md "Hardware tuning"): a list flushed again unchanged (the second eye, the touch
// band, a static HUD) skips buildBatch2D, the snapping and the conversion. A slot is rewritten
// only if no draw of the current frame uses it (the GPU finished the previous frames: C3D_FrameBegin
// waited for them).
struct CacheSlot {
    bool valid = false;
    std::vector<Quad> quads;
    Mapping mapping;
    bool halfPixel = false, snap = true;
    int segments = 0;
    std::vector<Run2D> runs;
    size_t count = 0, flat2Count = 0;
    pica::FlatVertex* flat = nullptr;
    size_t flatCap = 0;
    pica::Flat2Vertex* flat2 = nullptr;
    size_t flat2Cap = 0;
    unsigned usedFrame = ~0u; // the arena frame of the last flush that drew from it
    unsigned lastUse = 0;     // LRU stamp
};

constexpr int kCacheSlots = 6;

template <class T> T* growLinear(T* p, size_t& cap, size_t need) {
    if (need <= cap && p) return p;
    if (p) pica::deferLinearFree(p);
    const size_t n = need + need / 4 + 64;
    p = static_cast<T*>(linearAlloc(n * sizeof(T)));
    cap = p ? n : 0;
    return p;
}

} // namespace

const Ui2dPicaStats& ui2dPicaStats() { return g_stats; }
void ui2dPicaResetStats() { g_stats = Ui2dPicaStats(); }
void ui2dPicaSetSnap(bool on) { g_snap = on; }

struct Renderer2D::Impl {
    Texture2D white;
    std::vector<Vertex2D> verts;
    std::vector<Run2D> runs;
    CacheSlot slots[kCacheSlots];
    unsigned useStamp = 0;
    ~Impl() {
        for (CacheSlot& c : slots) {
            pica::deferLinearFree(c.flat);
            pica::deferLinearFree(c.flat2);
        }
    }
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

    const unsigned frame = pica::frameArena().frames();
    // A cached conversion of the same list (the vertices depend on the quads, the mapping, the
    // half-pixel shift, the circle segments and the snapping only).
    CacheSlot* hit = nullptr;
    for (CacheSlot& c : im.slots) {
        if (!c.valid || c.quads.size() != quads_.size() || c.halfPixel != halfPixelShift_ || c.snap != g_snap ||
            c.segments != segments_ || !sameMapping(c.mapping, mapping_))
            continue;
        bool same = true;
        for (size_t i = 0; same && i < quads_.size(); ++i) same = sameQuad(c.quads[i], quads_[i]);
        if (same) {
            hit = &c;
            break;
        }
    }
    const std::vector<Run2D>* runs = nullptr;
    const pica::FlatVertex* flat = nullptr;
    const pica::Flat2Vertex* flat2 = nullptr;
    if (hit) {
        ++g_stats.cacheHits;
        runs = &hit->runs;
        flat = hit->flat;
        flat2 = hit->flat2;
        hit->usedFrame = frame;
        hit->lastUse = ++im.useStamp;
    } else {
        const u64 tBatch = svcGetSystemTick();
        Batch2DOptions opts;
        opts.halfPixelShift = halfPixelShift_;
        opts.tessellateShapes = true;
        opts.circleSegments = segments_;
        buildBatch2D(quads_, mapping_, im.verts, im.runs, opts);
        if (im.verts.empty()) return 0;
        if (g_snap)
            for (const Run2D& run : im.runs)
                if (run.tex && !run.shape) snapQuads(im.verts, run.first, run.count);

        const u64 tCopy = svcGetSystemTick();
        pica::passProfile().add(pica::Pass::Ui2dBatch, tCopy - tBatch, 0, static_cast<int>(im.verts.size()));
        // FlatVertex for every vertex, Flat2Vertex for the ranges of the two-texture runs (in run
        // order): into a cache slot no draw of this frame uses (the least recently used), else
        // into the frame arena.
        const size_t count = im.verts.size();
        size_t flat2Count = 0;
        for (const Run2D& run : im.runs)
            if (run.tex2 && run.combine2 != CombineMode::None) flat2Count += static_cast<size_t>(run.count);
        CacheSlot* slot = nullptr;
        for (CacheSlot& c : im.slots)
            if (c.usedFrame != frame && (!slot || !c.valid || (slot->valid && c.lastUse < slot->lastUse))) slot = &c;
        pica::FlatVertex* outFlat = nullptr;
        pica::Flat2Vertex* outFlat2 = nullptr;
        if (slot) {
            slot->valid = false;
            slot->flat = growLinear(slot->flat, slot->flatCap, count);
            if (flat2Count) slot->flat2 = growLinear(slot->flat2, slot->flat2Cap, flat2Count);
            if (slot->flat && (!flat2Count || slot->flat2)) {
                outFlat = slot->flat;
                outFlat2 = flat2Count ? slot->flat2 : nullptr;
            } else {
                slot = nullptr;
            }
        }
        if (!slot) {
            pica::LinearArena& arena = pica::frameArena();
            outFlat = arena.allocArray<pica::FlatVertex>(count);
            outFlat2 = flat2Count ? arena.allocArray<pica::Flat2Vertex>(flat2Count) : nullptr;
            if (!outFlat || (flat2Count && !outFlat2)) {
                ++g_stats.arenaFailures;
                return 0;
            }
            g_stats.arenaBytes += count * sizeof(pica::FlatVertex) + flat2Count * sizeof(pica::Flat2Vertex);
        }
        for (size_t i = 0; i < count; ++i) {
            const Vertex2D& v = im.verts[i];
            pica::FlatVertex& o = outFlat[i];
            o.pos[0] = v.x;
            o.pos[1] = v.y;
            o.pos[2] = 0.0f;
            o.uv[0] = v.u;
            o.uv[1] = v.v;
            for (int k = 0; k < 4; ++k) o.rgba[k] = v.rgba[k];
        }
        size_t f2 = 0;
        for (const Run2D& run : im.runs) {
            if (!(run.tex2 && run.combine2 != CombineMode::None)) continue;
            for (int i = 0; i < run.count; ++i) {
                const Vertex2D& v = im.verts[static_cast<size_t>(run.first + i)];
                pica::Flat2Vertex& o = outFlat2[f2++];
                o.pos[0] = v.x;
                o.pos[1] = v.y;
                o.pos[2] = 0.0f;
                o.uv[0] = v.u;
                o.uv[1] = v.v;
                for (int k = 0; k < 4; ++k) o.rgba[k] = v.rgba[k];
                o.uv2[0] = v.u2;
                o.uv2[1] = v.v2;
            }
        }
        if (slot) {
            GSPGPU_FlushDataCache(outFlat, static_cast<u32>(count * sizeof(pica::FlatVertex)));
            if (flat2Count) GSPGPU_FlushDataCache(outFlat2, static_cast<u32>(flat2Count * sizeof(pica::Flat2Vertex)));
            slot->valid = true;
            slot->quads = quads_;
            slot->mapping = mapping_;
            slot->halfPixel = halfPixelShift_;
            slot->snap = g_snap;
            slot->segments = segments_;
            slot->runs = im.runs;
            slot->count = count;
            slot->flat2Count = flat2Count;
            slot->usedFrame = frame;
            slot->lastUse = ++im.useStamp;
            runs = &slot->runs;
        } else {
            runs = &im.runs;
        }
        flat = outFlat;
        flat2 = outFlat2;
        pica::passProfile().add(pica::Pass::Ui2dCopy, svcGetSystemTick() - tCopy, 0, static_cast<int>(count));
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
    pica::detail::c3dAlphaTest(false, GPU_ALWAYS, 0);

    int calls = 0;
    size_t flat2Used = 0;
    for (const Run2D& run : *runs) {
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
