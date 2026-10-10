// The PICA core test (pica_test.h). Layout of the top screen (upright pixels, origin top-left):
//   corners   6x6 markers TL (250,10,130), TR (10,250,130), BL (130,10,250) to locate the screen
//   y   8..52 one 44x44 quad per texture format (four colour quadrants each), drawn REPLACE
//             with alpha blending over the grey clear; the last one is RGBA8 forced to linear
//   y  60..100 the five RenderBlend modes (constant colour over grey), the stereo markers,
//             an 8-step grey ramp texture
//   y 120..160 depth (test, write off, test off), cull, viewport, scissored clearColor
//   y 176..216 terrain shader + 2-stage TEV, mesh shader (baked cube weights)
//   y 174..190 / 198..214 fog patches at eye distances 60..1800, Z-buffer / W-buffer table
#include "pica_test.h"

#include <3ds.h>
#include <citro3d.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <vector>

#include "as3d/fog_lut.h"
#include "as3d/image.h"
#include "as3d/material_key.h"
#include "as3d/render_state.h"
#include "as3d/texconv.h"
#include "as3d/texture.h"
#include "as3d/world_render.h"
#include "ctr_frame.h"
#include "pica_frame.h"
#include "pica_space.h"
#include "shader_set.h"
#include "tev.h"
#include "texture_budget.h"

using namespace as3d;

namespace picatest {
namespace {

#define PT_TRANSFER_FLAGS                                                                               \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |                  \
     GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |     \
     GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

std::FILE* g_log = nullptr;
std::FILE* g_checks = nullptr;
bool g_writeChecks = true; // the checks are written during the first frame only

void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void say(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::fputs(buf, stdout);
    if (g_log) {
        std::fputs(buf, g_log);
        std::fflush(g_log);
    }
}

struct Rgb {
    int r, g, b;
};

void check(const char* name, int x, int y, Rgb c, int tol = 4) {
    if (g_writeChecks && g_checks) std::fprintf(g_checks, "%s %d %d %d %d %d %d\n", name, x, y, c.r, c.g, c.b, tol);
}

u8 byte(float v) { return static_cast<u8>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); }

// Pixel space (origin top-left, y down, z in [-1, 1], 1 nearest) on the tilted top target.
Mat4 pixelMatrix() { return pica::picaTiltMatrix() * ortho(0.0f, 400.0f, 240.0f, 0.0f, -1.0f, 1.0f); }

struct Rect {
    float x0, y0, x1, y1;
};

// Two triangles; ccw = counter-clockwise as seen on the screen. UV (0,0) at the top-left.
void drawRect(const Rect& r, float z, u8 cr, u8 cg, u8 cb, u8 ca, bool ccw = true, const Mat4* mvp = nullptr) {
    auto* v = pica::frameArena().allocArray<pica::FlatVertex>(6);
    if (!v) return;
    struct C {
        float x, y, u, v;
    };
    const C tl{r.x0, r.y0, 0, 0}, tr{r.x1, r.y0, 1, 0}, br{r.x1, r.y1, 1, 1}, bl{r.x0, r.y1, 0, 1};
    const C ccwOrder[6] = {bl, br, tr, bl, tr, tl};
    const C cwOrder[6] = {bl, tr, br, bl, tl, tr};
    const C* order = ccw ? ccwOrder : cwOrder;
    for (int i = 0; i < 6; ++i) {
        v[i].pos[0] = order[i].x;
        v[i].pos[1] = order[i].y;
        v[i].pos[2] = z;
        v[i].uv[0] = order[i].u;
        v[i].uv[1] = order[i].v;
        v[i].rgba[0] = cr;
        v[i].rgba[1] = cg;
        v[i].rgba[2] = cb;
        v[i].rgba[3] = ca;
    }
    pica::ShaderSet& sh = pica::shaders();
    sh.bind(pica::Shader::Flat);
    sh.setMvp(mvp ? *mvp : pixelMatrix());
    sh.setVertices(v);
    C3D_DrawArrays(GPU_TRIANGLES, 0, 6);
}

void solid(const Rect& r, float z, Rgb c, bool ccw = true) {
    pica::applyTev(pica::tevConstantColor(static_cast<u8>(c.r), static_cast<u8>(c.g), static_cast<u8>(c.b), 255));
    drawRect(r, z, 255, 255, 255, 255, ccw);
}

Image solidImage(int w, int h, u8 r, u8 g, u8 b, u8 a) {
    Image img;
    img.width = w;
    img.height = h;
    img.hasAlpha = a != 255;
    img.rgba.resize(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < img.rgba.size(); i += 4) {
        img.rgba[i] = r;
        img.rgba[i + 1] = g;
        img.rgba[i + 2] = b;
        img.rgba[i + 3] = a;
    }
    return img;
}

// 32x32, quadrants TL, TR, BL, BR.
Image quadrantImage(const u8 (&c)[4][4]) {
    Image img = solidImage(32, 32, 0, 0, 0, 255);
    img.hasAlpha = true;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            const int q = (y >= 16 ? 2 : 0) + (x >= 16 ? 1 : 0);
            for (int k = 0; k < 4; ++k) img.rgba[(static_cast<size_t>(y) * 32 + x) * 4 + k] = c[q][k];
        }
    return img;
}

struct FormatCase {
    const char* name;
    TextureClass cls;
    TextureOptions::Residency res;
    u8 quad[4][4];
};

const FormatCase kFormats[] = {
    {"rgba8", TextureClass::TrueColor, TextureOptions::Residency::Auto,
     {{250, 20, 20, 255}, {20, 250, 20, 160}, {20, 20, 250, 80}, {200, 200, 40, 255}}},
    {"rgb565", TextureClass::Opaque, TextureOptions::Residency::Auto,
     {{200, 100, 50, 255}, {10, 130, 240, 255}, {255, 255, 255, 255}, {60, 60, 60, 255}}},
    {"rgba5551", TextureClass::Cutout, TextureOptions::Residency::Auto,
     {{240, 40, 160, 255}, {40, 240, 160, 0}, {160, 160, 40, 255}, {0, 0, 0, 0}}},
    {"rgba4", TextureClass::Alpha, TextureOptions::Residency::Auto,
     {{255, 136, 0, 255}, {0, 136, 255, 136}, {255, 255, 255, 68}, {136, 0, 136, 204}}},
    {"la8", TextureClass::Font, TextureOptions::Residency::Auto,
     {{255, 255, 255, 255}, {200, 200, 200, 128}, {64, 64, 64, 255}, {255, 255, 255, 40}}},
    {"a8", TextureClass::Shadow, TextureOptions::Residency::Auto,
     {{0, 0, 0, 255}, {0, 0, 0, 128}, {0, 0, 0, 30}, {0, 0, 0, 0}}},
    {"rgba8_linear", TextureClass::TrueColor, TextureOptions::Residency::Linear,
     {{30, 200, 200, 255}, {200, 30, 200, 255}, {200, 200, 30, 255}, {90, 90, 90, 128}}},
};
constexpr int kFormatCount = sizeof(kFormats) / sizeof(kFormats[0]);

constexpr Rgb kGrey{128, 128, 128};

// Expected = sampled texel (texconv's own round trip, expanded as the PICA does) over grey
// with SRC_ALPHA, ONE_MINUS_SRC_ALPHA.
Rgb overGrey(const u8* t) {
    auto mix = [&](int c) { return (c * t[3] + kGrey.r * (255 - t[3]) + 127) / 255; };
    return Rgb{mix(t[0]), mix(t[1]), mix(t[2])};
}

struct Scene {
    Texture2D formats[kFormatCount];
    Image formatSampled[kFormatCount]; // texconv round trip of each image in its format
    Texture2D ramp, white, terrainBase, terrainDetail;
    Image rampSampled, baseSampled, detailSampled;
    Texture2D late; // created inside frame kLateFrame (gfx_pica.cpp's late queue)
};

// The late texture check (3DS round 2, P1): a texture created inside a frame is an 8x8 placeholder
// of its average colour for that frame, built after C3D_FrameEnd (pica::drainLateTextures) and
// drawn for real from the next frame. 32x32, left half red, right half blue: the placeholder is
// (127, 0, 127). picatest_late_f<N>.ppm are the top screen after frames kLateFrame and
// kLateFrame + 2; the quad is at kLateRect.
constexpr int kLateFrame = 2;
constexpr float kLateRect[4] = {300.0f, 196.0f, 364.0f, 228.0f};

Image lateImage() {
    Image img;
    img.width = img.height = 32;
    img.rgba.assign(32 * 32 * 4, 0);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            u8* p = &img.rgba[(static_cast<size_t>(y) * 32 + static_cast<size_t>(x)) * 4];
            p[0] = x < 16 ? 255 : 0;
            p[2] = x < 16 ? 0 : 255;
            p[3] = 255;
        }
    return img;
}

void lateTextureInfo(const Scene& s, const char* when) {
    pica::Placement pl = pica::Placement::Linear;
    int tw = 0, th = 0, levels = 0;
    PicaFormat pf = PicaFormat::RGBA8;
    const bool ok = pica::textureInfo(s.late, &pl, &tw, &th, &pf, &levels);
    say("late texture %s: valid %d, %dx%d fmt %d %s, queued %d built %d pending %u\n", when, ok ? 1 : 0, tw, th,
        static_cast<int>(pf), pl == pica::Placement::Vram ? "VRAM" : "linear", pica::lateTextureStats().queued,
        pica::lateTextureStats().built, static_cast<unsigned>(pica::lateTexturesPending()));
}

void drawLate(Scene& s, int frame) {
    if (frame == kLateFrame) {
        TextureOptions nearest;
        nearest.minFilter = nearest.magFilter = Filter::Nearest;
        nearest.wrapS = nearest.wrapT = Wrap::ClampToEdge;
        s.late.create(lateImage(), nearest); // inside the frame: queued
        lateTextureInfo(s, "created inside the frame");
    }
    if (frame < kLateFrame) return;
    setBlend(GlBlend::Off);
    setDepth(false, false);
    setCull(CullMode::Off);
    pica::applyTev(pica::tevReplaceTex());
    s.late.bind(0);
    drawRect(Rect{kLateRect[0], kLateRect[1], kLateRect[2], kLateRect[3]}, 0.0f, 255, 255, 255, 255);
}

Image roundTrip(const Image& img, TextureClass cls) {
    PicaFormat f = chooseFormat(img, cls);
    std::vector<uint8_t> data = swizzle(img, f);
    return unswizzle(data.data(), img.width, img.height, f);
}

const u8* texel(const Image& img, int x, int y) { return &img.rgba[(static_cast<size_t>(y) * img.width + x) * 4]; }

void createTextures(Scene& s) {
    TextureOptions nearest;
    nearest.minFilter = nearest.magFilter = Filter::Nearest;
    nearest.wrapS = nearest.wrapT = Wrap::ClampToEdge;
    size_t expectedBytes = 0;
    const u32 vram0 = vramSpaceFree();
    for (int i = 0; i < kFormatCount; ++i) {
        const FormatCase& fc = kFormats[i];
        Image img = quadrantImage(fc.quad);
        TextureOptions o = nearest;
        o.cls = fc.cls;
        o.res = fc.res;
        s.formats[i].create(img, o);
        s.formatSampled[i] = roundTrip(img, fc.cls);
        pica::Placement pl = pica::Placement::Linear;
        int tw = 0, th = 0, levels = 0;
        PicaFormat pf = PicaFormat::RGBA8;
        pica::textureInfo(s.formats[i], &pl, &tw, &th, &pf, &levels);
        const size_t want = byteSize(32, 32, chooseFormat(img, fc.cls), 1);
        expectedBytes += want;
        say("tex %-12s %dx%d fmt %d %s levels %d bytes %u (byteSize %u)\n", fc.name, tw, th, static_cast<int>(pf),
            pl == pica::Placement::Vram ? "VRAM" : "linear", levels, static_cast<unsigned>(s.formats[i].bytes()),
            static_cast<unsigned>(want));
    }
    // The grey ramp: 8 columns 0, 36, ..., 252 (RGB565), stretched 6 px per column.
    Image ramp = solidImage(8, 8, 0, 0, 0, 255);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            for (int k = 0; k < 3; ++k) ramp.rgba[(static_cast<size_t>(y) * 8 + x) * 4 + k] = static_cast<u8>(x * 36);
    TextureOptions ro = nearest;
    s.ramp.create(ramp, ro);
    s.rampSampled = roundTrip(ramp, TextureClass::Auto);
    expectedBytes += byteSize(8, 8, PicaFormat::RGB565, 1);

    s.white.create(solidImage(8, 8, 255, 255, 255, 255), nearest);
    expectedBytes += byteSize(8, 8, PicaFormat::RGB565, 1);

    // Terrain base and detail, with mip chains (64x64: levels 64, 32, 16, 8).
    TextureOptions mip;
    mip.mipmaps = true;
    Image base = solidImage(64, 64, 153, 102, 51, 255), detail = solidImage(64, 64, 179, 128, 77, 255);
    s.terrainBase.create(base, mip);
    s.terrainDetail.create(detail, mip);
    s.baseSampled = roundTrip(base, TextureClass::Auto);
    s.detailSampled = roundTrip(detail, TextureClass::Auto);
    expectedBytes += 2 * byteSize(64, 64, PicaFormat::RGB565, 4);
    int levels = 0;
    pica::textureInfo(s.terrainBase, nullptr, nullptr, nullptr, nullptr, &levels);
    say("terrain base: %d mip levels, %u bytes\n", levels, static_cast<unsigned>(s.terrainBase.bytes()));

    const pica::TextureStats st = pica::textureStats();
    say("textureStats: vram %u B in %d, linear %u B in %d, limit %u, fallbacks %d, failures %d\n",
        static_cast<unsigned>(st.vramBytes), st.vramCount, static_cast<unsigned>(st.linearBytes), st.linearCount,
        static_cast<unsigned>(st.vramLimit), st.vramFallbacks, st.failures);
    say("byte accounting: stats %u == sum of texconv::byteSize %u: %s; vramSpaceFree dropped %u B\n",
        static_cast<unsigned>(st.vramBytes + st.linearBytes), static_cast<unsigned>(expectedBytes),
        st.vramBytes + st.linearBytes == expectedBytes ? "OK" : "MISMATCH",
        static_cast<unsigned>(vram0 - vramSpaceFree()));
}

// The eye-projection check: citro3d's matrices against the engine's GL eye.
void checkEyeProjection() {
    WorldView view;
    view.view = rotationX(-70.0f) * translation(Vec3{-100.0f, -50.0f, -300.0f});
    view.fovY = 60.0f;
    view.nearPlane = 4.0f;
    view.farPlane = 2000.0f;
    const float aspect = 400.0f / 240.0f;
    float worst = 0, worstNdc = 0;
    const float offsets[] = {-3.0f, 0.0f, 1.5f};
    const float focals[] = {0.0f, 60.0f, 400.0f};
    for (float e : offsets) {
        for (float d : focals) {
            const EyeView eye{e, d};
            const Mat4 c3d = pica::picaEyeProjection(view, eye, aspect);
            const Mat4 ref = pica::picaEyeProjectionReference(view, eye, aspect);
            for (int i = 0; i < 16; ++i) worst = std::max(worst, std::fabs(c3d.m[i] - ref.m[i]));
            const Vec4 wp{140.0f, 20.0f, 30.0f, 1.0f};
            const Vec4 g = eyeProjection(view, eye, aspect) * (eyeViewMatrix(view, eye) * wp);
            const Vec4 q = c3d * (view.view * wp);
            worstNdc = std::max(worstNdc, std::fabs(q.y / q.w + g.x / g.w));
            if (e == 1.5f && d == 400.0f)
                say("eye e=%.1f D=%.0f: GL ndc x %.6f, PICA ndc y %.6f (= -GL x), PICA ndc x %.6f vs GL y %.6f\n", e, d,
                    g.x / g.w, q.y / q.w, q.x / q.w, g.y / g.w);
        }
    }
    say("picaEyeProjection vs reference: max |diff| %.3g over 9 eyes, max ndc diff %.3g\n", worst, worstNdc);
}

void drawFormats(const Scene& s) {
    setBlend(GlBlend::AlphaBlend);
    setDepth(false, false);
    setCull(CullMode::Off);
    pica::applyTev(pica::tevReplaceTex());
    for (int i = 0; i < kFormatCount; ++i) {
        const float x0 = 12.0f + 55.0f * static_cast<float>(i), y0 = 8.0f;
        s.formats[i].bind(0);
        drawRect(Rect{x0, y0, x0 + 44, y0 + 44}, 0.0f, 255, 255, 255, 255);
        const int qx[4] = {11, 33, 11, 33}, qy[4] = {11, 11, 33, 33};
        const int tx[4] = {8, 24, 8, 24}, ty[4] = {8, 8, 24, 24};
        for (int q = 0; q < 4; ++q) {
            char name[48];
            std::snprintf(name, sizeof name, "%s_q%d", kFormats[i].name, q);
            check(name, static_cast<int>(x0) + qx[q], static_cast<int>(y0) + qy[q],
                  overGrey(texel(s.formatSampled[i], tx[q], ty[q])));
        }
    }
    // The grey ramp, 8 columns of 6 px at x 340..388, y 60..100.
    s.ramp.bind(0);
    setBlend(GlBlend::Off);
    drawRect(Rect{340, 60, 388, 100}, 0.0f, 255, 255, 255, 255);
    for (int c = 0; c < 8; ++c) {
        const u8* t = texel(s.rampSampled, c, 0);
        char name[32];
        std::snprintf(name, sizeof name, "ramp_%d", c);
        check(name, 340 + 6 * c + 3, 80, Rgb{t[0], t[1], t[2]}, 6);
    }
}

void drawBlends() {
    const Rgb src{200, 100, 50};
    const int a = 128;
    const RenderBlend modes[5] = {RenderBlend::None, RenderBlend::Alpha, RenderBlend::Add, RenderBlend::Filter,
                                  RenderBlend::Brightness};
    const char* names[5] = {"blend_none", "blend_alpha", "blend_add", "blend_filter", "blend_brightness"};
    pica::applyTev(pica::tevConstantColor(static_cast<u8>(src.r), static_cast<u8>(src.g), static_cast<u8>(src.b), a));
    for (int i = 0; i < 5; ++i) {
        MaterialKey k = blendKey(modes[i], false, false, CullMode::Off);
        applyMaterialState(k);
        const float x0 = 12.0f + 44.0f * static_cast<float>(i);
        drawRect(Rect{x0, 60, x0 + 40, 100}, 0.0f, 255, 255, 255, 255);
        auto expect = [&](int s, int d) {
            switch (modes[i]) {
            case RenderBlend::None: return s;
            case RenderBlend::Alpha: return (s * a + d * (255 - a) + 127) / 255;
            case RenderBlend::Add: return std::min(255, s + d);
            case RenderBlend::Filter: return (d * s + 127) / 255;
            case RenderBlend::Brightness: return std::min(255, (2 * d * s + 127) / 255);
            }
            return s;
        };
        check(names[i], static_cast<int>(x0) + 20, 80,
              Rgb{expect(src.r, kGrey.r), expect(src.g, kGrey.g), expect(src.b, kGrey.b)});
    }
    applyMaterialState(MaterialKey{});
}

void drawDepthCullViewportScissor() {
    setBlend(GlBlend::Off);
    setCull(CullMode::Off);
    const float y0 = 120, y1 = 160;
    const int cy = 140;
    // Depth test: red near, then green far over it: the overlap stays red.
    setDepth(true, true);
    solid(Rect{8, y0, 38, y1}, 0.5f, Rgb{255, 0, 0});
    solid(Rect{23, y0, 53, y1}, -0.5f, Rgb{0, 255, 0});
    check("depth_near", 15, cy, Rgb{255, 0, 0});
    check("depth_overlap", 30, cy, Rgb{255, 0, 0});
    check("depth_far", 46, cy, Rgb{0, 255, 0});
    // Depth write off: blue near without writing, yellow far over it wins.
    setDepth(true, false);
    solid(Rect{60, y0, 90, y1}, 0.5f, Rgb{0, 0, 255});
    setDepth(true, true);
    solid(Rect{75, y0, 105, y1}, -0.5f, Rgb{255, 255, 0});
    check("nowrite_blue", 67, cy, Rgb{0, 0, 255});
    check("nowrite_overlap", 82, cy, Rgb{255, 255, 0});
    // Depth test off (GL then writes no depth): magenta near, cyan farther over it wins.
    setDepth(false, true);
    solid(Rect{112, y0, 142, y1}, 0.9f, Rgb{255, 0, 255});
    setDepth(true, true);
    solid(Rect{127, y0, 157, y1}, 0.0f, Rgb{0, 255, 255});
    check("notest_magenta", 119, cy, Rgb{255, 0, 255});
    check("notest_overlap", 134, cy, Rgb{0, 255, 255});
    setDepth(false, false);
    // Cull: CCW with Back culling shows, CW with Back culling does not, CW with Front shows.
    setCull(CullMode::Back);
    solid(Rect{165, y0, 195, y1}, 0.0f, Rgb{255, 255, 255}, true);
    solid(Rect{200, y0, 230, y1}, 0.0f, Rgb{255, 255, 255}, false);
    setCull(CullMode::Front);
    solid(Rect{235, y0, 265, y1}, 0.0f, Rgb{255, 128, 0}, false);
    solid(Rect{235, y0, 265, y1}, 0.0f, Rgb{0, 0, 0}, true); // culled
    setCull(CullMode::Off);
    check("cull_back_ccw", 180, cy, Rgb{255, 255, 255});
    check("cull_back_cw", 215, cy, kGrey);
    check("cull_front_cw", 250, cy, Rgb{255, 128, 0});
    // Viewport: GL rectangle x 272, y (from the bottom) 240-160 = 80, 40 x 40; a quad over
    // the whole clip square fills exactly that.
    setViewport(272, 80, 40, 40);
    pica::applyTev(pica::tevConstantColor(0, 200, 100, 255));
    const Mat4 identity;
    drawRect(Rect{-1, 1, 1, -1}, 0.0f, 255, 255, 255, 255, false, &identity);
    setViewport(0, 0, 400, 240);
    check("viewport_in", 292, cy, Rgb{0, 200, 100});
    check("viewport_in_corner", 274, 122, Rgb{0, 200, 100});
    check("viewport_left", 268, cy, kGrey);
    check("viewport_right", 316, cy, kGrey);
    check("viewport_above", 292, 116, kGrey);
    // Scissor + clearColor: x 330..370, y from the bottom 80..120.
    setScissor(true, 330, 80, 40, 40);
    clearColor(Vec4{0.0f, 0.0f, 1.0f, 1.0f});
    setScissor(false);
    check("scissor_in", 350, cy, Rgb{0, 0, 255});
    check("scissor_left", 326, cy, kGrey);
    check("scissor_right", 374, cy, kGrey);
    check("scissor_below", 350, 164, kGrey);
}

void drawTerrainAndMesh(const Scene& s) {
    pica::ShaderSet& sh = pica::shaders();
    setBlend(GlBlend::Off);
    setDepth(false, false);
    setCull(CullMode::Off);
    // Terrain: static colour (100, 255, 255) + dynamic (28, 0, 0) = (128, 255, 255).
    auto* tv = pica::frameArena().allocArray<pica::TerrainVertexPica>(6);
    auto* dyn = static_cast<uint8_t(*)[4]>(pica::frameArena().alloc(6 * 4, 4));
    if (tv && dyn) {
        const float xs[6] = {12, 52, 52, 12, 52, 12}, ys[6] = {216, 216, 176, 216, 176, 176};
        for (int i = 0; i < 6; ++i) {
            tv[i].pos[0] = xs[i];
            tv[i].pos[1] = ys[i];
            tv[i].pos[2] = 0.0f;
            tv[i].col[0] = 100;
            tv[i].col[1] = tv[i].col[2] = tv[i].col[3] = 255;
            tv[i].uv0[0] = (xs[i] - 12) / 40;
            tv[i].uv0[1] = (ys[i] - 176) / 40;
            dyn[i][0] = 28;
            dyn[i][1] = dyn[i][2] = dyn[i][3] = 0;
        }
        sh.bind(pica::Shader::Terrain);
        sh.setMvp(pixelMatrix());
        sh.setTerrainDetail(1.0f / 64.0f, 1.0f / 64.0f, 0.0f, 0.0f);
        sh.setTerrainVertices(tv, dyn);
        s.terrainBase.bind(0);
        s.terrainDetail.bind(1);
        pica::applyTev(pica::tevTerrain2Stage());
        C3D_DrawArrays(GPU_TRIANGLES, 0, 6);
        const u8* b = texel(s.baseSampled, 0, 0);
        const u8* d = texel(s.detailSampled, 0, 0);
        auto t = [](int base, int v, int det) { return std::clamp((base * v + 127) / 255 + det - 128, 0, 255); };
        check("terrain_tev", 32, 196, Rgb{t(b[0], 128, d[0]), t(b[1], 255, d[1]), t(b[2], 255, d[2])}, 5);
    }
    // Mesh: weights (255 on +X) -> cube[0]; (128 on +X, 127 on -Y) -> mix of cube[0], cube[4].
    const Vec3 cube[6] = {{0.2f, 0.6f, 0.4f}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {1.0f, 0.0f, 0.0f}, {0, 0, 0}};
    for (int k = 0; k < 2; ++k) {
        auto* mv = pica::frameArena().allocArray<pica::MeshVertex>(6);
        if (!mv) break;
        const float x0 = 62.0f + 50.0f * static_cast<float>(k);
        const float xs[6] = {x0, x0 + 40, x0 + 40, x0, x0 + 40, x0}, ys[6] = {216, 216, 176, 216, 176, 176};
        for (int i = 0; i < 6; ++i) {
            mv[i].pos[0] = xs[i];
            mv[i].pos[1] = ys[i];
            mv[i].pos[2] = 0.0f;
            mv[i].wPos[0] = k == 0 ? 255 : 128;
            mv[i].wPos[1] = mv[i].wPos[2] = mv[i].wPos[3] = 0;
            mv[i].wNeg[0] = mv[i].wNeg[2] = mv[i].wNeg[3] = 0;
            mv[i].wNeg[1] = k == 0 ? 0 : 127;
            mv[i].uv[0] = mv[i].uv[1] = 0.5f;
        }
        sh.bind(pica::Shader::Mesh);
        sh.setMvp(pixelMatrix());
        sh.setMeshCube(cube);
        sh.setMeshTint(Vec4{1.0f, 1.0f, 1.0f, 1.0f});
        sh.setVertices(mv);
        s.white.bind(0);
        pica::applyTev(pica::tevModulateTexColor());
        C3D_DrawArrays(GPU_TRIANGLES, 0, 6);
    }
    check("mesh_cube0", 82, 196, Rgb{51, 153, 102});
    const float w0 = 128.0f / 255.0f, w4 = 127.0f / 255.0f;
    check("mesh_mix", 132, 196, Rgb{byte(w0 * 0.2f + w4), byte(w0 * 0.6f), byte(w0 * 0.4f)});
}

// Fog: white patches at eye distances `kFogZ`, fog black, linear 100..1500, planes 4..2000.
void drawFog() {
    const float kFogZ[8] = {60, 150, 300, 500, 700, 900, 1200, 1600};
    const float n = 4, f = 2000, fs = 100, fe = 1500;
    WorldView view; // identity view, camera at the origin looking down -z
    view.fovY = 60.0f;
    view.nearPlane = n;
    view.farPlane = f;
    const float aspect = 400.0f / 240.0f;
    const float F = 1.0f / std::tan(degToRad(30.0f)), A = F / aspect;
    setBlend(GlBlend::Off);
    setDepth(false, false);
    setCull(CullMode::Off);
    pica::applyTev(pica::tevConstantColor(255, 255, 255, 255));
    pica::applyFogColor(Vec3{0.0f, 0.0f, 0.0f});
    for (int row = 0; row < 2; ++row) {
        const FogDepthMap map = row == 0 ? FogDepthMap::ZBuffer : FogDepthMap::WBuffer;
        pica::setDepthMapping(map, n);
        pica::setFog(n, f, fs, fe);
        float lut[256];
        buildLinearFogLut(n, f, fs, fe, lut, map);
        const Mat4 proj = pica::picaEyeProjection(view, EyeView{}, aspect);
        const int cy = row == 0 ? 182 : 206;
        for (int k = 0; k < 8; ++k) {
            const float d = kFogZ[k];
            const int cx = 232 + 21 * k;
            const float nx = static_cast<float>(cx) / 200.0f - 1.0f, ny = 1.0f - static_cast<float>(cy) / 120.0f;
            const float ex = nx * d / A, ey = ny * d / F, hx = (8.0f / 200.0f) * d / A, hy = (8.0f / 120.0f) * d / F;
            auto* v = pica::frameArena().allocArray<pica::FlatVertex>(6);
            if (!v) break;
            const float xs[6] = {ex - hx, ex + hx, ex + hx, ex - hx, ex + hx, ex - hx};
            const float ys[6] = {ey - hy, ey - hy, ey + hy, ey - hy, ey + hy, ey + hy};
            for (int i = 0; i < 6; ++i) {
                v[i].pos[0] = xs[i];
                v[i].pos[1] = ys[i];
                v[i].pos[2] = -d;
                v[i].uv[0] = v[i].uv[1] = 0;
                v[i].rgba[0] = v[i].rgba[1] = v[i].rgba[2] = v[i].rgba[3] = 255;
            }
            pica::ShaderSet& sh = pica::shaders();
            sh.bind(pica::Shader::Flat);
            sh.setMvp(proj);
            sh.setVertices(v);
            C3D_DrawArrays(GPU_TRIANGLES, 0, 6);
            const float hw = fogLutLookup(lut, fogDepthFromEye(n, f, d, map));
            const float gl = fogFactorAtEyeDepth(fs, fe, d);
            char name[32];
            std::snprintf(name, sizeof name, "fog_%s_%d", row == 0 ? "z" : "w", static_cast<int>(d));
            const int v8 = byte(hw);
            check(name, cx, cy, Rgb{v8, v8, v8}, 6);
            if (g_writeChecks)
                say("fog %s z=%4.0f: table %.3f (%3d), GL linear fog %.3f (%3d)\n", row == 0 ? "zbuf" : "wbuf", d, hw,
                    v8, gl, byte(gl));
        }
    }
    pica::disableFog();
    pica::setDepthMapping(FogDepthMap::ZBuffer, n);
}

// Stereo: the left eye (e = -20, zero parallax at 200) through picaEyeProjection with the
// central view; the patches must land where the engine's GL eye math puts them.
void drawStereoMarkers() {
    WorldView view;
    view.fovY = 60.0f;
    view.nearPlane = 4.0f;
    view.farPlane = 2000.0f;
    const float aspect = 400.0f / 240.0f;
    const EyeView eye{-20.0f, 200.0f};
    const float F = 1.0f / std::tan(degToRad(30.0f)), A = F / aspect;
    const Mat4 proj = pica::picaEyeProjection(view, eye, aspect);
    const Mat4 glEye = eyeProjection(view, eye, aspect) * eyeViewMatrix(view, eye);
    pica::applyTev(pica::tevConstantColor(0, 160, 0, 255));
    setBlend(GlBlend::Off);
    setDepth(false, false);
    setCull(CullMode::Off);
    const float depths[2] = {200.0f, 600.0f};
    const float rows[2] = {72.0f, 92.0f};
    for (int k = 0; k < 2; ++k) {
        const float d = depths[k];
        // World point that the MONO camera would show at pixel (300, rows[k]).
        const float nx = 300.0f / 200.0f - 1.0f, ny = 1.0f - rows[k] / 120.0f;
        const Vec4 wp{nx * d / A, ny * d / F, -d, 1.0f};
        const Vec4 g = glEye * wp;
        const int px = static_cast<int>(std::lround((g.x / g.w + 1.0f) * 200.0f));
        const int py = static_cast<int>(std::lround((1.0f - g.y / g.w) * 120.0f));
        const float h = 5.0f * d / (200.0f * A);
        auto* v = pica::frameArena().allocArray<pica::FlatVertex>(6);
        if (!v) return;
        const float xs[6] = {wp.x - h, wp.x + h, wp.x + h, wp.x - h, wp.x + h, wp.x - h};
        const float ys[6] = {wp.y - h, wp.y - h, wp.y + h, wp.y - h, wp.y + h, wp.y + h};
        for (int i = 0; i < 6; ++i) {
            v[i].pos[0] = xs[i];
            v[i].pos[1] = ys[i];
            v[i].pos[2] = -d;
            v[i].uv[0] = v[i].uv[1] = 0;
            v[i].rgba[0] = v[i].rgba[1] = v[i].rgba[2] = v[i].rgba[3] = 255;
        }
        pica::ShaderSet& sh = pica::shaders();
        sh.bind(pica::Shader::Flat);
        sh.setMvp(proj);
        sh.setVertices(v);
        C3D_DrawArrays(GPU_TRIANGLES, 0, 6);
        char name[32];
        std::snprintf(name, sizeof name, "stereo_d%d", static_cast<int>(d));
        check(name, px, py, Rgb{0, 160, 0});
        if (g_writeChecks) say("stereo marker d=%.0f: GL eye math puts it at pixel (%d, %d) (mono 300)\n", d, px, py);
    }
}

void drawMarkers() {
    setBlend(GlBlend::Off);
    setDepth(false, false);
    setCull(CullMode::Off);
    solid(Rect{0, 0, 6, 6}, 0.0f, Rgb{250, 10, 130});
    solid(Rect{394, 0, 400, 6}, 0.0f, Rgb{10, 250, 130});
    solid(Rect{0, 234, 6, 240}, 0.0f, Rgb{130, 10, 250});
}

void drawFrame(const Scene& s) {
    clear(Vec4{128.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f, 1.0f}, true, 1.0f);
    drawMarkers();
    drawFormats(s);
    drawBlends();
    drawStereoMarkers();
    drawDepthCullViewportScissor();
    drawTerrainAndMesh(s);
    drawFog();
}

} // namespace

void run(const std::string& dir, int holdFrames) {
    g_log = std::fopen((dir + "/picatest_log.txt").c_str(), "w");
    g_checks = std::fopen((dir + "/picatest_checks.txt").c_str(), "w");
    say("PICA core test (M1.3)\n");
    if (void* warm = vramAlloc(0x100)) vramFree(warm);
    C3D_RenderTarget* top = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    if (!top) {
        say("cannot create the top target\n");
        return;
    }
    C3D_RenderTargetSetOutput(top, GFX_TOP, GFX_LEFT, PT_TRANSFER_FLAGS);
    if (!pica::initBackend()) {
        say("pica::initBackend failed\n");
        C3D_RenderTargetDelete(top);
        return;
    }
    checkEyeProjection();
    Scene* scene = new Scene;
    createTextures(*scene);

    unsigned maxArena = 0;
    for (int frame = 0; aptMainLoop() && (holdFrames <= 0 || frame < holdFrames); ++frame) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        pica::frameBegin();
        pica::drawOn(pica::FrameTarget{top, 400, 240, true});
        drawFrame(*scene);
        drawLate(*scene, frame);
        pica::frameEnd();
        C3D_FrameEnd(0);
        if (frame == kLateFrame || frame == kLateFrame + 2) {
            char name[64];
            std::snprintf(name, sizeof name, "/picatest_late_f%d.ppm", frame);
            const bool dumped = ctrapp::dumpRenderTarget(top, 400, dir + name);
            say("late texture: frame %d dumped %d (quad %.0f,%.0f..%.0f,%.0f)\n", frame, dumped ? 1 : 0, kLateRect[0],
                kLateRect[1], kLateRect[2], kLateRect[3]);
        }
        if (frame == kLateFrame) {
            const int built = pica::drainLateTextures();
            say("late texture: drained %d after C3D_FrameEnd\n", built);
            lateTextureInfo(*scene, "after the drain");
        }
        maxArena = std::max<unsigned>(maxArena, static_cast<unsigned>(pica::frameArena().used()));
        if (frame == 0) {
            g_writeChecks = false;
            if (g_checks) std::fclose(g_checks);
            g_checks = nullptr;
            say("frame 0 drawn; arena used %u B, failures %u\n", static_cast<unsigned>(pica::frameArena().used()),
                pica::frameArena().failures());
        }
        if (frame == 120) say("frame 120: arena high water %u B, half %d, GPU %.2f ms\n",
                              static_cast<unsigned>(pica::frameArena().highWater()), pica::frameArena().currentHalf(),
                              static_cast<double>(C3D_GetDrawingTime()));
    }
    say("picatest: done\n");
    delete scene; // textures freed: the budget must return to zero
    const pica::TextureStats st = pica::textureStats();
    say("after free: vram %u B (%d), linear %u B (%d)\n", static_cast<unsigned>(st.vramBytes), st.vramCount,
        static_cast<unsigned>(st.linearBytes), st.linearCount);
    pica::shutdownBackend();
    C3D_RenderTargetDelete(top);
    if (g_log) std::fclose(g_log);
    g_log = nullptr;
}

} // namespace picatest
