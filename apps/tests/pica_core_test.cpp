// Desktop tests of the platform-neutral parts of the PICA backend (engine/src/render/pica):
// the per-frame linear arena, the texture budget, the TEV presets (evaluated by a CPU model
// of the combiner) and the coordinate conventions of pica_space.h, including the eye
// projection against a transcription of citro3d's matrix functions.
#include "doctest.h"

#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <vector>

#include "as3d/math.h"
#include "as3d/world_render.h"
#include "fx_pica.h"
#include "linear_arena.h"
#include "pica_space.h"
#include "tev.h"
#include "texture_budget.h"

using namespace as3d;
using namespace as3d::pica;

namespace {

// ---- arena stub memory ----------------------------------------------------------------
struct StubMemory {
    static int allocs, releases, flushes;
    static const void* lastFlushPtr;
    static size_t lastFlushBytes, lastAllocBytes;
    static void* alloc(size_t bytes, size_t align) {
        ++allocs;
        lastAllocBytes = bytes;
        return std::aligned_alloc(align, (bytes + align - 1) / align * align);
    }
    static void release(void* p) {
        ++releases;
        std::free(p);
    }
    static void flush(const void* p, size_t bytes) {
        ++flushes;
        lastFlushPtr = p;
        lastFlushBytes = bytes;
    }
    static ArenaMemory memory() {
        ArenaMemory m;
        m.alloc = alloc;
        m.release = release;
        m.flush = flush;
        return m;
    }
};
int StubMemory::allocs = 0, StubMemory::releases = 0, StubMemory::flushes = 0;
const void* StubMemory::lastFlushPtr = nullptr;
size_t StubMemory::lastFlushBytes = 0, StubMemory::lastAllocBytes = 0;

// ---- a CPU model of the TEV combiner -------------------------------------------------------
struct Rgba {
    float r = 0, g = 0, b = 0, a = 0;
};

struct TevInputs {
    Rgba primary, tex0, tex1, tex2;
};

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

Rgba source(TevSrc s, const TevInputs& in, const Rgba& previous, const TevStage& st) {
    switch (s) {
    case TevSrc::Primary: return in.primary;
    case TevSrc::Texture0: return in.tex0;
    case TevSrc::Texture1: return in.tex1;
    case TevSrc::Texture2: return in.tex2;
    case TevSrc::Constant:
        return Rgba{st.constant[0] / 255.0f, st.constant[1] / 255.0f, st.constant[2] / 255.0f, st.constant[3] / 255.0f};
    case TevSrc::Previous:
    case TevSrc::PreviousBuffer: return previous;
    }
    return Rgba{};
}

float combine(TevFunc f, float a, float b, float c) {
    switch (f) {
    case TevFunc::Replace: return clamp01(a);
    case TevFunc::Modulate: return clamp01(a * b);
    case TevFunc::Add: return clamp01(a + b);
    case TevFunc::AddSigned: return clamp01(a + b - 0.5f);
    case TevFunc::Interpolate: return clamp01(a * c + b * (1.0f - c));
    case TevFunc::Subtract: return clamp01(a - b);
    case TevFunc::MultiplyAdd: return clamp01(a * b + c);
    case TevFunc::AddMultiply: return clamp01(std::fmin(a + b, 1.0f) * c);
    }
    return 0.0f;
}

Rgba evaluate(const TevConfig& cfg, const TevInputs& in) {
    Rgba prev = in.primary; // what an unconfigured first stage would pass on
    for (int i = 0; i < cfg.count; ++i) {
        const TevStage& st = cfg.stages[i];
        float rgb[3][3];
        float alpha[3];
        for (int k = 0; k < 3; ++k) {
            Rgba s = source(st.srcRgb[k], in, prev, st);
            switch (st.opRgb[k]) {
            case TevOpRgb::SrcColor: rgb[k][0] = s.r, rgb[k][1] = s.g, rgb[k][2] = s.b; break;
            case TevOpRgb::OneMinusSrcColor: rgb[k][0] = 1 - s.r, rgb[k][1] = 1 - s.g, rgb[k][2] = 1 - s.b; break;
            case TevOpRgb::SrcAlpha: rgb[k][0] = rgb[k][1] = rgb[k][2] = s.a; break;
            case TevOpRgb::OneMinusSrcAlpha: rgb[k][0] = rgb[k][1] = rgb[k][2] = 1 - s.a; break;
            }
            Rgba sa = source(st.srcA[k], in, prev, st);
            alpha[k] = st.opA[k] == TevOpA::SrcAlpha ? sa.a : 1 - sa.a;
        }
        Rgba out;
        out.r = combine(st.funcRgb, rgb[0][0], rgb[1][0], rgb[2][0]);
        out.g = combine(st.funcRgb, rgb[0][1], rgb[1][1], rgb[2][1]);
        out.b = combine(st.funcRgb, rgb[0][2], rgb[1][2], rgb[2][2]);
        out.a = combine(st.funcA, alpha[0], alpha[1], alpha[2]);
        prev = out;
    }
    return prev;
}

void checkRgba(const Rgba& got, float r, float g, float b, float a) {
    CHECK(got.r == doctest::Approx(r).epsilon(1e-4));
    CHECK(got.g == doctest::Approx(g).epsilon(1e-4));
    CHECK(got.b == doctest::Approx(b).epsilon(1e-4));
    CHECK(got.a == doctest::Approx(a).epsilon(1e-4));
}

// ---- citro3d 1.7 matrix functions, transcribed (source/maths/mtx_persptilt.c and
// mtx_perspstereotilt.c, right-handed), rows as in C3D_Mtx: m.at(col, row) = r[row].c[col].
Mat4 citroPerspTilt(float fovy, float invaspect, float n, float f) {
    Mat4 m;
    for (float& v : m.m) v = 0.0f;
    float t = std::tan(fovy / 2.0f);
    m.at(1, 0) = 1.0f / t;               // r[0].y
    m.at(0, 1) = -1.0f / (t * invaspect); // r[1].x
    m.at(3, 2) = f * n / (n - f);        // r[2].w
    m.at(2, 3) = -1.0f;                  // r[3].z
    m.at(2, 2) = -m.at(2, 3) * n / (n - f);
    return m;
}

Mat4 citroPerspStereoTilt(float fovy, float invaspect, float n, float f, float iod, float screen) {
    Mat4 m;
    for (float& v : m.m) v = 0.0f;
    float t = std::tan(fovy / 2.0f);
    float ti = t * invaspect;
    float shift = iod / (2.0f * screen);
    m.at(1, 0) = 1.0f / t;
    m.at(0, 1) = -1.0f / ti;
    m.at(3, 1) = iod / 2.0f;
    m.at(3, 2) = n * f / (n - f);
    m.at(2, 3) = -1.0f;
    m.at(2, 1) = -m.at(2, 3) * shift / ti;
    m.at(2, 2) = -m.at(2, 3) * n / (n - f);
    return m;
}

Mat4 citroFor(const PicaStereoParams& p) {
    return p.stereo ? citroPerspStereoTilt(p.fovYRadians, p.aspect, p.nearPlane, p.farPlane, p.iod, p.screen)
                    : citroPerspTilt(p.fovYRadians, p.aspect, p.nearPlane, p.farPlane);
}

float maxAbsDiff(const Mat4& a, const Mat4& b) {
    float d = 0;
    for (int i = 0; i < 16; ++i) d = std::fmax(d, std::fabs(a.m[i] - b.m[i]));
    return d;
}

WorldView testView() {
    WorldView v;
    v.view = rotationX(-70.0f) * translation(Vec3{-100.0f, -50.0f, -300.0f});
    v.fovY = 60.0f;
    v.nearPlane = 4.0f;
    v.farPlane = 2000.0f;
    return v;
}

} // namespace

TEST_CASE("pica linear arena: alignment, double buffering, high water, flush range") {
    LinearArena arena;
    CHECK_FALSE(arena.valid());
    CHECK(arena.alloc(16) == nullptr);
    REQUIRE(arena.init(1000, StubMemory::memory()));
    CHECK(arena.capacity() == 1024); // rounded up to kBaseAlign
    CHECK(StubMemory::lastAllocBytes == 2048);
    CHECK(reinterpret_cast<std::uintptr_t>(arena.halfBase()) % LinearArena::kBaseAlign == 0);

    arena.beginFrame();
    const int half = arena.currentHalf();
    const uint8_t* base = arena.halfBase();
    void* a = arena.alloc(3, 1);
    void* b = arena.alloc(10, 16);
    void* c = arena.alloc(8, 128);
    REQUIRE(a);
    REQUIRE(b);
    REQUIRE(c);
    CHECK(static_cast<uint8_t*>(a) == base);
    CHECK(static_cast<uint8_t*>(b) - base == 16);
    CHECK(static_cast<uint8_t*>(c) - base == 128);
    CHECK(arena.used() == 136);
    CHECK(arena.alloc(8, 3) == nullptr); // not a power of two
    CHECK(arena.alloc(8, 256) == nullptr); // above the half's own alignment
    CHECK(arena.failures() == 2);

    arena.flush();
    CHECK(StubMemory::lastFlushPtr == base);
    CHECK(StubMemory::lastFlushBytes == 136);

    // The next frame gets the other half, empty; the one after that the first half again.
    arena.beginFrame();
    CHECK(arena.currentHalf() == 1 - half);
    CHECK(arena.used() == 0);
    CHECK(arena.halfBase() == (half == 0 ? base + arena.capacity() : base - arena.capacity()));
    float* f = arena.allocArray<float>(10);
    REQUIRE(f);
    CHECK(reinterpret_cast<std::uintptr_t>(f) % 4 == 0);
    arena.beginFrame();
    CHECK(arena.halfBase() == base);
    CHECK(arena.highWater() == 136);

    // A full half refuses, and a frame later there is room again.
    CHECK(arena.alloc(1024, 16) != nullptr);
    CHECK(arena.alloc(1, 1) == nullptr);
    CHECK(arena.highWater() == 1024);
    int flushes = StubMemory::flushes;
    arena.beginFrame();
    arena.flush(); // nothing used: no flush call
    CHECK(StubMemory::flushes == flushes);
    CHECK(arena.alloc(1024, 16) != nullptr);
    CHECK(arena.frames() == 4);
    arena.resetStats();
    CHECK(arena.failures() == 0);

    int releases = StubMemory::releases;
    arena.shutdown();
    CHECK(StubMemory::releases == releases + 1);
    CHECK_FALSE(arena.valid());
}

TEST_CASE("pica texture budget: VRAM first, linear beyond the limit, forced residency") {
    TextureBudget budget(1000);
    using R = TextureOptions::Residency;
    CHECK(budget.choose(600, R::Auto) == pica::Placement::Vram);
    budget.add(pica::Placement::Vram, 600);
    CHECK(budget.choose(400, R::Auto) == pica::Placement::Vram); // exactly fills it
    CHECK(budget.choose(401, R::Auto) == pica::Placement::Linear);
    CHECK(budget.choose(401, R::Vram) == pica::Placement::Vram);
    CHECK(budget.choose(10, R::Linear) == pica::Placement::Linear);
    budget.add(pica::Placement::Linear, 401);
    CHECK(budget.stats().vramBytes == 600);
    CHECK(budget.stats().linearBytes == 401);
    CHECK(budget.stats().vramCount == 1);
    CHECK(budget.stats().linearCount == 1);
    budget.remove(pica::Placement::Vram, 600);
    CHECK(budget.stats().vramBytes == 0);
    CHECK(budget.stats().vramCount == 0);
    CHECK(budget.choose(1000, R::Auto) == pica::Placement::Vram);
    CHECK(TextureBudget().vramLimit() == TextureBudget::kDefaultVramLimit);
}

TEST_CASE("pica tev presets: the combiner computes the GL shaders' formulas") {
    TevInputs in;
    in.primary = {0.5f, 1.0f, 1.0f, 0.8f};
    in.tex0 = {0.6f, 0.4f, 0.2f, 0.5f};
    in.tex1 = {0.7f, 0.5f, 0.3f, 0.25f};

    checkRgba(evaluate(tevReplaceTex(), in), 0.6f, 0.4f, 0.2f, 0.5f);
    checkRgba(evaluate(tevModulateTexColor(), in), 0.3f, 0.4f, 0.2f, 0.4f);
    // Particles' kTexClampAdd: clamp(t.rgb + c.rgb), t.a * c.a.
    checkRgba(evaluate(tevClampAddTexColor(), in), 1.0f, 1.0f, 1.0f, 0.4f);
    TevInputs dim = in;
    dim.primary = {0.1f, 0.2f, 0.3f, 0.5f};
    checkRgba(evaluate(tevClampAddTexColor(), dim), 0.7f, 0.6f, 0.5f, 0.25f);
    checkRgba(evaluate(tevConstantColor(255, 0, 51, 102), in), 1.0f, 0.0f, 0.2f, 0.4f);
    checkRgba(evaluate(tevModulateTexConstant(255, 0, 255, 51), in), 0.6f, 0.0f, 0.2f, 0.1f);
    // Terrain: base * vColor + detail - 0.5 (terrain fragment shader), alpha from the vertex.
    checkRgba(evaluate(tevTerrain2Stage(), in), 0.6f * 0.5f + 0.7f - 0.5f, 0.4f + 0.5f - 0.5f, 0.0f, 0.8f);
    // Shadow: black, texture alpha * 0.4.
    checkRgba(evaluate(tevShadow(), in), 0.0f, 0.0f, 0.0f, 0.5f * 102.0f / 255.0f);

    // The 2D layer's second texture (renderer2d_gl.cpp): c = tex * colour, then
    // 1 mix(c.rgb, t2.rgb, t2.a), 2 c.rgb + t2.rgb, 3 c * t2, else t2.
    const Rgba c{0.3f, 0.4f, 0.2f, 0.4f};
    const Rgba& t2 = in.tex1;
    checkRgba(evaluate(tevCombine2(0), in), c.r, c.g, c.b, c.a);
    checkRgba(evaluate(tevCombine2(1), in), c.r + (t2.r - c.r) * t2.a, c.g + (t2.g - c.g) * t2.a,
              c.b + (t2.b - c.b) * t2.a, c.a);
    checkRgba(evaluate(tevCombine2(2), in), 1.0f, 0.9f, 0.5f, c.a);
    checkRgba(evaluate(tevCombine2(3), in), c.r * t2.r, c.g * t2.g, c.b * t2.b, c.a * t2.a);
    checkRgba(evaluate(tevCombine2(4), in), t2.r, t2.g, t2.b, t2.a);
    for (int m = 0; m <= 4; ++m) CHECK(tevCombine2(m).count == (m == 0 ? 1 : 2));
}

TEST_CASE("pica space: GL rectangles on the rotated framebuffer") {
    // Top screen, 400 x 240 upright; the framebuffer is 240 wide, 400 tall.
    FbRect full = picaFramebufferRect(400, 0, 0, 400, 240);
    CHECK(full.x == 0);
    CHECK(full.y == 0);
    CHECK(full.w == 240);
    CHECK(full.h == 400);
    // The left half of the screen is the upper half of the framebuffer's y range.
    FbRect left = picaFramebufferRect(400, 0, 0, 200, 240);
    CHECK(left.y == 200);
    CHECK(left.h == 200);
    // A 20 x 10 box 30 px from the left, 40 px from the bottom.
    FbRect box = picaFramebufferRect(400, 30, 40, 20, 10);
    CHECK(box.x == 40);
    CHECK(box.y == 350);
    CHECK(box.w == 10);
    CHECK(box.h == 20);
    // Bottom screen.
    FbRect bottom = picaFramebufferRect(320, 0, 0, 320, 240);
    CHECK(bottom.w == 240);
    CHECK(bottom.h == 320);
}

TEST_CASE("pica space: tilt of a GL projection equals citro3d's Mtx_PerspTilt and OrthoTilt") {
    const float fovy = degToRad(60.0f), aspect = 400.0f / 240.0f;
    Mat4 gl = perspective(60.0f, aspect, 4.0f, 2000.0f);
    CHECK(maxAbsDiff(picaTiltMatrix() * gl, citroPerspTilt(fovy, aspect, 4.0f, 2000.0f)) < 1e-5f);
    // A pixel-space ortho (origin top-left, y down): the corners land where the rotated
    // framebuffer has them: x_pica = y_gl, y_pica = -x_gl.
    Mat4 px = picaTiltMatrix() * ortho(0.0f, 400.0f, 240.0f, 0.0f, -1.0f, 1.0f);
    Vec4 tl = px * Vec4{0.0f, 0.0f, 0.0f, 1.0f};
    Vec4 br = px * Vec4{400.0f, 240.0f, 0.0f, 1.0f};
    CHECK(tl.x == doctest::Approx(1.0f));  // top of the screen
    CHECK(tl.y == doctest::Approx(1.0f));  // left of the screen
    CHECK(br.x == doctest::Approx(-1.0f));
    CHECK(br.y == doctest::Approx(-1.0f));
    CHECK(tl.z == doctest::Approx(-0.5f)); // GL z 0 -> PICA (0 - 1) / 2
}

TEST_CASE("pica space: the eye projection equals citro3d's Mtx_PerspStereoTilt with iod = 2Ae, screen = AD") {
    const WorldView view = testView();
    const float aspect = 400.0f / 240.0f;
    for (float e : {-3.0f, -0.5f, 0.0f, 0.75f, 2.0f}) {
        for (float d : {0.0f, 40.0f, 150.0f, 900.0f}) {
            const EyeView eye{e, d};
            const PicaStereoParams p = picaStereoParams(view, eye, aspect);
            CHECK(p.stereo == (e != 0.0f));
            const Mat4 reference = picaEyeProjectionReference(view, eye, aspect);
            CHECK(maxAbsDiff(citroFor(p), reference) < 2e-5f);

            // The same point through the GL eye and through the PICA projection with the
            // central view: PICA ndc y = -GL ndc x, PICA ndc x = GL ndc y, PICA z = (z - 1)/2.
            for (Vec3 wp : {Vec3{100.0f, 60.0f, 0.0f}, Vec3{140.0f, 20.0f, 30.0f}, Vec3{60.0f, 200.0f, -10.0f}}) {
                Vec4 g = eyeProjection(view, eye, aspect) * (eyeViewMatrix(view, eye) * Vec4{wp.x, wp.y, wp.z, 1.0f});
                Vec4 q = citroFor(p) * (view.view * Vec4{wp.x, wp.y, wp.z, 1.0f});
                REQUIRE(g.w > 0.0f);
                CHECK(q.y / q.w == doctest::Approx(-g.x / g.w).epsilon(1e-4));
                CHECK(q.x / q.w == doctest::Approx(g.y / g.w).epsilon(1e-4));
                CHECK(q.z / q.w == doctest::Approx((g.z / g.w - 1.0f) / 2.0f).epsilon(1e-4));
            }
        }
    }
    // The naive mapping (iod = 2e, screen = D) is off by the factor A = 1.04 at 60 degrees.
    const EyeView eye{1.0f, 200.0f};
    const Mat4 naive = citroPerspStereoTilt(degToRad(60.0f), aspect, 4.0f, 2000.0f, 2.0f, 200.0f);
    CHECK(maxAbsDiff(naive, picaEyeProjectionReference(view, eye, aspect)) > 0.03f);
}

TEST_CASE("fx_pica: capRuns cuts the built lists at the cap") {
    auto run = [](int first, int count) {
        DrawRun r;
        r.first = first;
        r.count = count;
        return r;
    };
    std::vector<DrawRun> runs = {run(0, 12), run(12, 18), run(30, 6)};
    CHECK(capRuns(runs, 36, 40) == 36); // under the cap: untouched
    CHECK(runs.size() == 3);
    CHECK(capRuns(runs, 36, 24) == 24); // cuts the second run
    REQUIRE(runs.size() == 2);
    CHECK(runs[1].first == 12);
    CHECK(runs[1].count == 12);
    CHECK(capRuns(runs, 24, 12) == 12); // drops the second run
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].count == 12);
    CHECK(kMaxParticleQuadsPica * 6 == 9216);
}
