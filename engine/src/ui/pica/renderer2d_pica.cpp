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
#include <cstring>
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

constexpr int kCacheSlots = 24; // per list: the touch band alone has 13 units (Renderer2D::cacheBreak)
constexpr int kConsolidateFlushes = 30; // a split list unchanged this long is drawn as one unit

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
    std::vector<Quad> unitQuads; // a cache unit's quads (flush)
    unsigned useStamp = 0;
    int allHitFlushes = 0; // flushes in a row whose units all hit (consolidation)
    struct Replay { // the last flush's list and draw list (flush)
        bool valid = false;
        std::vector<Quad> quads;
        std::vector<size_t> breaks;
        Mapping mapping;
        bool halfPixel = false, snap = true;
        int segments = 0;
        struct U {
            const std::vector<Run2D>* runs = nullptr;
            const pica::FlatVertex* flat = nullptr;
            const pica::Flat2Vertex* flat2 = nullptr;
            CacheSlot* slot = nullptr;
        };
        std::vector<U> units;
    } replay;
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
    // The cache units: the whole list, or the ranges between cacheBreak() boundaries (as3d/ui.h),
    // each converted and cached on its own.
    struct Unit {
        size_t first = 0, count = 0;
        const std::vector<Run2D>* runs = nullptr;
        const pica::FlatVertex* flat = nullptr;
        const pica::Flat2Vertex* flat2 = nullptr;
        bool hit = false;
        const CacheSlot* slot = nullptr; // the slot it was drawn from (hits)
    };
    std::vector<Unit> units;
    std::vector<std::vector<Run2D>> arenaRuns; // the runs of units drawn from the frame arena
    // The same list as the last flush (the touch band, a static HUD, the second eye): its units are
    // still in their cache slots (only this list's own flushes rewrite them), so the last flush's
    // draw list is replayed without matching the units against the slots (round 3, R3.2).
    Impl::Replay& rp = im.replay;
    bool replayed = rp.valid && rp.quads.size() == quads_.size() && rp.breaks == breaks_ && rp.halfPixel == halfPixelShift_ &&
                    rp.snap == g_snap && rp.segments == segments_ && sameMapping(rp.mapping, mapping_);
    for (size_t i = 0; replayed && i < quads_.size(); ++i) replayed = sameQuad(rp.quads[i], quads_[i]);
    if (replayed) {
        for (const Impl::Replay::U& r : rp.units) {
            Unit u;
            u.runs = r.runs;
            u.flat = r.flat;
            u.flat2 = r.flat2;
            u.hit = true;
            u.slot = r.slot;
            if (r.slot) {
                r.slot->usedFrame = frame;
                r.slot->lastUse = ++im.useStamp;
            }
            units.push_back(u);
            ++g_stats.cacheHits;
        }
        g_stats.units += static_cast<int>(units.size());
        ++g_stats.replays;
        // A split list replayed unchanged counts towards its consolidation (the next flush then
        // takes the matching path, which joins it).
        if (units.size() > 1 && ++im.allHitFlushes >= kConsolidateFlushes) rp.valid = false;
    } else {
    {
        size_t from = 0;
        for (size_t b : breaks_) {
            if (b > from && b <= quads_.size()) {
                units.push_back(Unit{from, b - from});
                from = b;
            }
        }
        if (from < quads_.size()) units.push_back(Unit{from, quads_.size() - from});
    }
    auto sameAs = [&](const CacheSlot& c, const Unit& u) {
        if (!c.valid || c.quads.size() != u.count || c.halfPixel != halfPixelShift_ || c.snap != g_snap ||
            c.segments != segments_ || !sameMapping(c.mapping, mapping_))
            return false;
        for (size_t i = 0; i < u.count; ++i)
            if (!sameQuad(c.quads[i], quads_[u.first + i])) return false;
        return true;
    };
    // Consolidation: a list split into units that stayed wholly cached for kConsolidateFlushes
    // flushes gets a slot holding the whole list, its units' vertices copied end to end and their
    // runs joined where texture and blend agree (fewer draws: the static menus of the front end);
    // it is drawn from that slot while the list does not change. A change anywhere misses the whole
    // and falls back to the units, most of which are still cached.
    bool consolidated = false;
    if (units.size() > 1) {
        const Unit whole{0, quads_.size()};
        for (const CacheSlot& c : im.slots) {
            if (sameAs(c, whole)) {
                units.assign(1, whole);
                consolidated = true;
                im.allHitFlushes = 0;
                break;
            }
        }
    }
    // First the units whose cached conversion is still there (a cached conversion of the same
    // quads: the vertices depend on the quads, the mapping, the half-pixel shift, the circle
    // segments and the snapping only), so that rebuilding a changed unit never takes a slot an
    // unchanged one of this flush is about to reuse.
    for (Unit& u : units) {
        for (CacheSlot& c : im.slots) {
            if (!sameAs(c, u)) continue;
            ++g_stats.cacheHits;
            u.hit = true;
            u.runs = &c.runs;
            u.flat = c.flat;
            u.flat2 = c.flat2;
            u.slot = &c;
            c.usedFrame = frame;
            c.lastUse = ++im.useStamp;
            break;
        }
    }
    if (!consolidated && units.size() > 1) {
        bool allHit = true;
        for (const Unit& u : units) allHit = allHit && u.hit;
        im.allHitFlushes = allHit ? im.allHitFlushes + 1 : 0;
        CacheSlot* dst = nullptr;
        if (allHit && im.allHitFlushes >= kConsolidateFlushes) {
            for (CacheSlot& c : im.slots)
                if (c.usedFrame != frame && (!dst || !c.valid || (dst->valid && c.lastUse < dst->lastUse))) dst = &c;
        }
        if (dst) {
            im.allHitFlushes = 0;
            size_t n = 0, n2 = 0;
            for (const Unit& u : units) {
                n += u.slot->count;
                n2 += u.slot->flat2Count;
            }
            dst->valid = false;
            dst->flat = growLinear(dst->flat, dst->flatCap, n);
            if (n2) dst->flat2 = growLinear(dst->flat2, dst->flat2Cap, n2);
            if (dst->flat && (!n2 || dst->flat2)) {
                std::vector<Run2D> runs;
                size_t off = 0, off2 = 0;
                auto twoTex = [](const Run2D& r) { return r.tex2 && r.combine2 != CombineMode::None; };
                for (const Unit& u : units) {
                    const CacheSlot& s = *u.slot;
                    std::memcpy(dst->flat + off, s.flat, s.count * sizeof(pica::FlatVertex));
                    if (s.flat2Count) std::memcpy(dst->flat2 + off2, s.flat2, s.flat2Count * sizeof(pica::Flat2Vertex));
                    for (Run2D r : s.runs) {
                        r.first += static_cast<int>(off);
                        if (!runs.empty() && !twoTex(r)) {
                            Run2D& p = runs.back();
                            if (!twoTex(p) && p.blend == r.blend && p.tex == r.tex && p.shape == r.shape &&
                                p.first + p.count == r.first) {
                                p.count += r.count;
                                continue;
                            }
                        }
                        runs.push_back(r);
                    }
                    off += s.count;
                    off2 += s.flat2Count;
                }
                GSPGPU_FlushDataCache(dst->flat, static_cast<u32>(n * sizeof(pica::FlatVertex)));
                if (n2) GSPGPU_FlushDataCache(dst->flat2, static_cast<u32>(n2 * sizeof(pica::Flat2Vertex)));
                dst->valid = true;
                dst->quads = quads_;
                dst->mapping = mapping_;
                dst->halfPixel = halfPixelShift_;
                dst->snap = g_snap;
                dst->segments = segments_;
                dst->runs = std::move(runs);
                dst->count = n;
                dst->flat2Count = n2;
                dst->usedFrame = frame;
                dst->lastUse = ++im.useStamp;
                Unit w{0, quads_.size()};
                w.runs = &dst->runs;
                w.flat = dst->flat;
                w.flat2 = dst->flat2;
                w.hit = true;
                w.slot = dst;
                units.assign(1, w);
                ++g_stats.consolidations;
            }
        }
    }
    arenaRuns.reserve(units.size());
    g_stats.units += static_cast<int>(units.size());
    for (Unit& u : units) {
        if (u.hit) continue;
        ++g_stats.unitsBuilt;
        const u64 tBatch = svcGetSystemTick();
        Batch2DOptions opts;
        opts.halfPixelShift = halfPixelShift_;
        opts.tessellateShapes = true;
        opts.circleSegments = segments_;
        const bool whole = u.first == 0 && u.count == quads_.size();
        if (whole) {
            buildBatch2D(quads_, mapping_, im.verts, im.runs, opts);
        } else {
            im.unitQuads.assign(quads_.begin() + static_cast<long>(u.first),
                                quads_.begin() + static_cast<long>(u.first + u.count));
            buildBatch2D(im.unitQuads, mapping_, im.verts, im.runs, opts);
        }
        if (im.verts.empty()) {
            u.runs = nullptr;
            continue;
        }
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
                u.runs = nullptr;
                continue;
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
            slot->quads.assign(quads_.begin() + static_cast<long>(u.first),
                               quads_.begin() + static_cast<long>(u.first + u.count));
            slot->mapping = mapping_;
            slot->halfPixel = halfPixelShift_;
            slot->snap = g_snap;
            slot->segments = segments_;
            slot->runs = im.runs;
            slot->count = count;
            slot->flat2Count = flat2Count;
            slot->usedFrame = frame;
            slot->lastUse = ++im.useStamp;
            u.runs = &slot->runs;
            u.slot = slot;
        } else {
            arenaRuns.push_back(im.runs);
            u.runs = &arenaRuns.back();
        }
        u.flat = outFlat;
        u.flat2 = outFlat2;
        pica::passProfile().add(pica::Pass::Ui2dCopy, svcGetSystemTick() - tCopy, 0, static_cast<int>(count));
    }
    // What the next flush may replay: every unit in a cache slot (none in the frame arena).
    rp.valid = arenaRuns.empty();
    if (rp.valid) {
        rp.quads = quads_;
        rp.breaks = breaks_;
        rp.mapping = mapping_;
        rp.halfPixel = halfPixelShift_;
        rp.snap = g_snap;
        rp.segments = segments_;
        rp.units.clear();
        for (const Unit& u : units) {
            Impl::Replay::U r;
            r.runs = u.runs;
            r.flat = u.flat;
            r.flat2 = u.flat2;
            r.slot = const_cast<CacheSlot*>(u.slot);
            rp.units.push_back(r);
        }
    }
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
    // The shader, its matrix and the vertex buffer are issued when they change, not per run (round
    // 3, R3.2: C3D_BindProgram reloads the program's constants and attribute layout even for the
    // bound program, and every buffer setup is re-emitted with the next draw).
    int boundShader = -1;
    const void* boundVertices = nullptr;
    for (const Unit& u : units) {
        if (!u.runs) continue;
        size_t flat2Used = 0;
        for (const Run2D& run : *u.runs) {
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
            const void* vertices = two ? static_cast<const void*>(u.flat2 + flat2Used) : static_cast<const void*>(u.flat);
            const int want = two ? 1 : 0;
            if (want != boundShader) {
                sh.bind(two ? pica::Shader::Flat2 : pica::Shader::Flat);
                sh.setMvp(mvp);
                boundShader = want;
                boundVertices = nullptr;
            }
            if (vertices != boundVertices) {
                sh.setVertices(vertices);
                boundVertices = vertices;
            }
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
