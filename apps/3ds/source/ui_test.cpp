// The 2D layer test (ui_test.h).
#include "ui_test.h"

#include <3ds.h>
#include <citro3d.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "as3d/defs.h"
#include "as3d/game_profile.h"
#include "as3d/image.h"
#include "as3d/input.h"
#include "as3d/render_state.h"
#include "as3d/touch_controls.h"
#include "as3d/ui.h"
#include "as3d/vfs.h"
#include "as3d/world.h"
#include "game_session.h"
#include "pica_frame.h"
#include "texture_budget.h"
#include "ui_pica.h"

using namespace as3d;

namespace uitest {
namespace {

#define UI_TRANSFER_FLAGS                                                                               \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |                  \
     GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |     \
     GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

std::FILE* g_log = nullptr;
std::FILE* g_checks = nullptr;

void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void say(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (g_log) {
        std::fputs(buf, g_log);
        std::fflush(g_log);
    }
}

double kb(size_t b) { return static_cast<double>(b) / 1024.0; }

// A target as a PPM. The raw target is 240 x width (rotated, bytes A B G R per pixel); its row x
// is screen column x, its column y counted from the bottom.
void dumpTarget(C3D_RenderTarget* t, int width, const std::string& path) {
    const size_t bytes = static_cast<size_t>(240) * static_cast<size_t>(width) * 4;
    auto* lin = static_cast<u8*>(linearAlloc(bytes));
    if (!lin) return;
    C3D_SyncDisplayTransfer(static_cast<u32*>(t->frameBuf.colorBuf), GX_BUFFER_DIM(240, width),
                            reinterpret_cast<u32*>(lin), GX_BUFFER_DIM(240, width),
                            GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
                                GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                                GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    GSPGPU_InvalidateDataCache(lin, bytes);
    if (std::FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fprintf(f, "P6\n%d 240\n255\n", width);
        std::vector<u8> row(static_cast<size_t>(width) * 3);
        for (int y = 0; y < 240; ++y) {
            for (int x = 0; x < width; ++x) {
                const u8* px = lin + (static_cast<size_t>(x) * 240 + static_cast<size_t>(239 - y)) * 4;
                row[static_cast<size_t>(x) * 3 + 0] = px[3];
                row[static_cast<size_t>(x) * 3 + 1] = px[2];
                row[static_cast<size_t>(x) * 3 + 2] = px[1];
            }
            std::fwrite(row.data(), 1, row.size(), f);
        }
        std::fclose(f);
    }
    linearFree(lin);
}

// The level's display name (GameSession::levelName without a session).
std::string levelNameOf(const World& w, const DefDatabase& db) {
    if (const Terrain* t = w.terrain()) {
        for (const LevelDef& d : db.levels())
            if (d.id == t->style().id) return d.name.empty() ? d.id : d.name;
    }
    return std::string();
}

// ---- phase 1: the font ----

void drawFontBlock(ui::Renderer2D& r, const ui::UiAssets& a) {
    const ui::Font font = a.uiFont();
    const ui::FontMetrics& fm = *font.metrics;
    ui::TextStyle st;
    st.scale = 1.5f; // the level name / message scale of the bottom HUD
    st.color = ui::orange();
    ui::drawText(r, font, 20, 14, "AIRSTRIKE 3D  HUD SCALE 1.5", st);
    st.scale = 1.0f;
    st.color = ui::Color{1, 1, 1, 1};
    ui::drawText(r, font, 20, 56, "Scale 1.0: 12x6 px glyphs, abc xyz 0123456789", st);
    ui::drawNumber(r, font, 20, 84, "0001234567", 2.0f, ui::Color{1, 1, 1, 1}); // the score scale
    // The ASCII set 0x20..0x7F, 32 per row, in the additive font at scale 1.0.
    for (int row = 0; row < 3; ++row) {
        std::string s;
        for (int c = 0; c < 32; ++c) s += static_cast<char>(0x20 + row * 32 + c);
        st.color = row == 1 ? ui::Color{0.6f, 1.0f, 0.6f, 1} : ui::Color{1, 1, 1, 1};
        ui::drawText(r, font, 20, 150.0f + static_cast<float>(row) * 24.0f, s, st);
    }
    // The alpha font: black text on a grey strip.
    r.rect(14, 232, 520, 24, ui::Color{0.75f, 0.75f, 0.75f, 1}, ui::Blend::Opaque);
    st.kind = ui::FontKind::Alpha;
    st.color = ui::Color{0, 0, 0, 1};
    ui::drawText(r, font, 20, 236, "Alpha font: The quick brown fox 0123", st);
    (void)fm;
}

// ---- phase 3: the quads with expected pixels ----

struct Probe {
    std::string name;
    int x, y; // target pixels, origin top-left
    int r, g, b, tol;
};
std::vector<Probe> g_probes;

void expect(const std::string& name, int x, int y, int r, int g, int b, int tol) {
    g_probes.push_back({name, x, y, r, g, b, tol});
}

// Rectangles in target pixels of the 320x240 target (the mapping is 0.4: virtual = px / 0.4).
void pxRect(ui::Renderer2D& r, float x, float y, float w, float h, ui::Color c, ui::Blend b) {
    const ui::Mapping& m = r.mapping();
    r.rect(m.toVirtX(x), m.toVirtY(y), w / m.scaleX, h / m.scaleY, c, b);
}

struct TestTextures {
    Texture2D t1, t2;
};

void makeTextures(TestTextures& tt) {
    TextureOptions o;
    o.minFilter = o.magFilter = Filter::Nearest;
    o.wrapS = o.wrapT = Wrap::ClampToEdge;
    o.cls = TextureClass::TrueColor;
    Image a;
    a.width = a.height = 8;
    a.hasAlpha = true;
    a.rgba.resize(8 * 8 * 4);
    for (int i = 0; i < 64; ++i) {
        a.rgba[static_cast<size_t>(i) * 4 + 0] = 128;
        a.rgba[static_cast<size_t>(i) * 4 + 1] = 64;
        a.rgba[static_cast<size_t>(i) * 4 + 2] = 0;
        a.rgba[static_cast<size_t>(i) * 4 + 3] = 255;
    }
    tt.t1.create(a, o);
    // Left half (0, 128, 255, a = 255), right half (100, 50, 0, a = 0).
    Image b = a;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            u8* p = &b.rgba[static_cast<size_t>(y * 8 + x) * 4];
            if (x < 4) {
                p[0] = 0; p[1] = 128; p[2] = 255; p[3] = 255;
            } else {
                p[0] = 100; p[1] = 50; p[2] = 0; p[3] = 0;
            }
        }
    tt.t2.create(b, o);
}

void drawQuads(ui::Renderer2D& r, const TestTextures& tt) {
    using ui::Blend;
    using ui::Color;
    g_probes.clear();
    // Row 1 (y = 10, 40 px squares): alpha, add, filter, opaque, solid over base.
    r.begin(320, 240);
    pxRect(r, 10, 10, 40, 40, Color{1, 0, 0, 1}, Blend::Alpha);
    expect("alpha_solid", 30, 30, 255, 0, 0, 3);
    pxRect(r, 60, 10, 40, 40, Color{1, 1, 1, 0.5f}, Blend::Alpha);
    expect("alpha_half", 80, 30, 128, 128, 128, 5);
    pxRect(r, 110, 10, 40, 40, Color{0, 0, 1, 1}, Blend::Opaque);
    pxRect(r, 110, 10, 40, 40, Color{1, 0.5f, 0, 1}, Blend::Add);
    expect("add_over_blue", 130, 30, 255, 128, 255, 5);
    pxRect(r, 160, 10, 40, 40, Color{0.8f, 0.8f, 0.8f, 1}, Blend::Opaque);
    pxRect(r, 160, 10, 40, 40, Color{0.5f, 1, 0.25f, 1}, Blend::Filter);
    expect("filter_over_grey", 180, 30, 102, 204, 51, 6);
    pxRect(r, 210, 10, 40, 40, Color{1, 1, 1, 1}, Blend::Opaque);
    pxRect(r, 210, 10, 40, 40, Color{0, 1, 0, 0.2f}, Blend::Opaque);
    expect("opaque_forces_alpha", 230, 30, 0, 255, 0, 3);
    // Row 2 (y = 60): the five combine modes of the second texture, over black. Each quad shows
    // texture 1 (128, 64, 0) times white; texture 2 is (0, 128, 255, a 255) on its left half,
    // (100, 50, 0, a 0) on its right half.
    const ui::Mapping& m = r.mapping();
    struct Case {
        const char* name;
        int mode;
        int lr, lg, lb; // left half result
        int rr, rg, rb; // right half result
    };
    const Case cases[5] = {
        {"blend", 1, 0, 128, 255, 128, 64, 0},      // mix(previous, tex2, tex2.a)
        {"add", 2, 128, 192, 255, 228, 114, 0},     // previous + tex2
        {"modulate", 3, 0, 32, 0, 0, 0, 0},         // previous * tex2, the right half has alpha 0
        {"replace", 4, 0, 128, 255, 0, 0, 0},       // tex2 (alpha 0 on the right: black)
        {"alpha_only_t1", 0, 128, 64, 0, 128, 64, 0}, // no second texture: texture 1
    };
    for (int k = 0; k < 5; ++k) {
        const float x = 10.0f + 50.0f * static_cast<float>(k), y = 60.0f;
        ui::Quad q;
        q.x = m.toVirtX(x);
        q.y = m.toVirtY(y);
        q.w = 40.0f / m.scaleX;
        q.h = 40.0f / m.scaleY;
        q.texture = &tt.t1;
        q.color = Color{1, 1, 1, 1};
        q.blend = Blend::Alpha;
        if (cases[k].mode != 0) {
            q.texture2 = &tt.t2;
            q.combine2 = cases[k].mode;
        }
        r.add(q);
        expect(std::string("tex2_") + cases[k].name + "_left", static_cast<int>(x) + 10, 80, cases[k].lr, cases[k].lg,
               cases[k].lb, 5);
        expect(std::string("tex2_") + cases[k].name + "_right", static_cast<int>(x) + 30, 80, cases[k].rr, cases[k].rg,
               cases[k].rb, 5);
    }
    // A disc (tessellated): white, radius 12 px, centre (270, 200).
    {
        const float cx = m.toVirtX(270), cy = m.toVirtY(200);
        r.circle(cx, cy, 12.0f / m.scaleY, Color{1, 1, 1, 1});
        expect("disc_centre", 270, 200, 255, 255, 255, 6);
        expect("disc_inside", 277, 200, 255, 255, 255, 12);
        expect("disc_outside", 286, 200, 0, 0, 0, 10);
    }
    // A coloured half-transparent ring (radius 20, thickness 4): hole stays black.
    {
        const float cx = m.toVirtX(200), cy = m.toVirtY(200);
        r.ring(cx, cy, 20.0f / m.scaleY, 4.0f / m.scaleY, Color{1, 0, 0, 1});
        expect("ring_hole", 200, 200, 0, 0, 0, 6);
        expect("ring_body", 218, 200, 255, 0, 0, 40);
    }
    r.flush();
    // A scissored fill: only the rectangle (10..70 x 130..170) turns green.
    r.begin(320, 240);
    setScissor(true, 10, 240 - 170, 60, 40);
    pxRect(r, 0, 0, 320, 240, Color{0, 1, 0, 1}, Blend::Opaque);
    r.flush();
    setScissor(false);
    expect("scissor_inside", 40, 150, 0, 255, 0, 3);
    expect("scissor_right", 100, 150, 0, 0, 0, 3);
    expect("scissor_above", 40, 120, 0, 0, 0, 3);
    expect("scissor_below", 40, 180, 0, 0, 0, 3);
}

// The checks are listed at the end of the first frame of phase 3, so they exist once.
void writeChecks(const std::string& dir) {
    g_checks = std::fopen((dir + "/uitest_checks.txt").c_str(), "w");
    if (!g_checks) return;
    for (const Probe& p : g_probes) std::fprintf(g_checks, "%s %d %d %d %d %d %d\n", p.name.c_str(), p.x, p.y, p.r, p.g, p.b, p.tol);
    std::fclose(g_checks);
    g_checks = nullptr;
}

constexpr int kPhases = 3;
const char* kPhaseNames[kPhases] = {"font", "hud", "quads"};

} // namespace

void run(Vfs& vfs, const DefDatabase& db, const GameProfile& game, const std::string& dir, int level, int botFrames,
         int holdFrames) {
    g_log = std::fopen((dir + "/uitest_log.txt").c_str(), "w");
    const int hold = holdFrames > 0 ? holdFrames : 30;
    const int bot = botFrames > 0 ? botFrames : 120;
    say("2D layer test (M1.7): mission %d, %d bot frames, %d frames per phase\n", level, bot, hold);

    WorldConfig wc;
    wc.rules = &game.rules;
    auto world = std::make_unique<World>();
    world->init(vfs, db, wc);
    std::string err;
    if (!world->loadLevel(std::to_string(level), &err)) {
        say("mission %d: load failed: %s\n", level, err.c_str());
        if (g_log) std::fclose(g_log);
        return;
    }
    for (u32 f = 0; f < static_cast<u32>(bot); ++f) world->step(botInput(f).toPlayerInput());
    const ui::HudState hud = as3d_game::hudStateOf(*world, levelNameOf(*world, db));
    say("hud: health %.0f/%.0f, lives %d, score %lld, weapon %d, missiles %d/%d/%d/%d/%d, level '%s', t %.2f\n",
        static_cast<double>(hud.players[0].health), static_cast<double>(hud.players[0].maxHealth), hud.players[0].lives,
        static_cast<long long>(hud.players[0].score), hud.players[0].weapon, hud.players[0].missiles[0],
        hud.players[0].missiles[1], hud.players[0].missiles[2], hud.players[0].missiles[3], hud.players[0].missiles[4],
        hud.levelName.c_str(), static_cast<double>(hud.levelTime));

    if (void* warm = vramAlloc(0x100)) vramFree(warm);
    C3D_RenderTarget* top = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    C3D_RenderTarget* bottom = C3D_RenderTargetCreate(240, 320, GPU_RB_RGBA8, static_cast<GPU_DEPTHBUF>(-1));
    if (!top || !bottom) {
        say("cannot create the render targets\n");
        if (g_log) std::fclose(g_log);
        return;
    }
    C3D_RenderTargetSetOutput(top, GFX_TOP, GFX_LEFT, UI_TRANSFER_FLAGS);
    C3D_RenderTargetSetOutput(bottom, GFX_BOTTOM, GFX_LEFT, UI_TRANSFER_FLAGS);
    if (!pica::initBackend()) {
        say("pica::initBackend failed\n");
        if (g_log) std::fclose(g_log);
        return;
    }

    auto r2d = std::make_unique<ui::Renderer2D>();
    auto assets = std::make_unique<ui::UiAssets>();
    TestTextures tt;
    ui::Renderer2D& r = *r2d;
    if (!r.init(&err)) say("Renderer2D::init: %s\n", err.c_str());
    assets->hudTarget = ui::HudTarget::Bottom320x240;
    const bool loaded = assets->load(vfs, &err, game.id);
    makeTextures(tt);
    {
        const pica::TextureStats ts = pica::textureStats();
        say("ui assets: loaded %d, missing %d; textures vram %.1f KB (%d), linear %.1f KB (%d), failures %d\n",
            loaded ? 1 : 0, assets->missing, kb(ts.vramBytes), ts.vramCount, kb(ts.linearBytes), ts.linearCount, ts.failures);
        say("font %dx%d %.1f KB, mainbar %dx%d %.1f KB, weapons %.1f KB, missiles %.1f KB, items %.1f KB, life %.1f KB\n",
            assets->font.width(), assets->font.height(), kb(assets->font.bytes()), assets->mainbar.width(),
            assets->mainbar.height(), kb(assets->mainbar.bytes()), kb(assets->weapons.bytes()),
            kb(assets->missiles.bytes()), kb(assets->items.bytes()), kb(assets->life.bytes()));
    }

    TouchMapper touch;
    touch.setScreen(320, 240, TouchLayoutOptions::forBottomScreen320x240(132.0f));

    int phase = -1;
    double gpuSum = 0, gpuMax = 0, cpuSum = 0, cpuMax = 0;
    int n = 0;
    for (int frame = 0; aptMainLoop(); ++frame) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        const int want = std::min(frame / hold, kPhases - 1);
        if (want != phase) {
            if (phase >= 0 && n > 0)
                say("phase %d %s: GPU avg %.3f ms max %.3f ms, CPU avg %.3f ms max %.3f ms over %d frames\n", phase + 1,
                    kPhaseNames[phase], gpuSum / n, gpuMax, cpuSum / n, cpuMax, n);
            gpuSum = gpuMax = cpuSum = cpuMax = 0;
            n = 0;
            phase = want;
        }
        const u64 t0 = svcGetSystemTick();
        ui::ui2dPicaResetStats();
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        pica::frameBegin();

        // Top screen, 400x240: the font block once (phase 1), a caption afterwards.
        pica::drawOn(pica::FrameTarget{top, 400, 240, true});
        clear(Vec4{0, 0, 0, 1}, true, 1.0f);
        r.begin(400, 240);
        assets->hudTarget = ui::HudTarget::Bottom320x240;
        if (phase == 0) {
            drawFontBlock(r, *assets);
        } else {
            ui::TextStyle st;
            st.scale = 1.5f;
            st.color = ui::orange();
            ui::drawText(r, assets->uiFont(), 20, 14, phase == 1 ? "TOP SCREEN: PHASE 2 HUD" : "TOP SCREEN: PHASE 3 QUADS", st);
        }
        r.flush();
        const ui::Ui2dPicaStats topStats = ui::ui2dPicaStats();

        // Bottom screen, 320x240, no depth.
        pica::drawOn(pica::FrameTarget{bottom, 320, 240, false});
        clear(Vec4{0, 0, 0, 1}, false, 1.0f);
        if (phase == 0) {
            r.begin(320, 240);
            drawFontBlock(r, *assets);
            r.flush();
        } else if (phase == 1) {
            r.begin(320, 240);
            ui::drawHud(r, *assets, hud);
            r.flush();
            ui::TouchControlsState ts;
            ts.assets = assets.get();
            ts.player = &hud.players[0];
            ts.rules = &game.rules;
            r.begin(320, 240);
            ui::drawTouchControls(r, touch, ts);
            r.flush();
        } else {
            drawQuads(r, tt);
        }
        const ui::Ui2dPicaStats st = ui::ui2dPicaStats();
        const size_t arenaUsed = pica::frameArena().used();
        pica::frameEnd();
        C3D_FrameEnd(0);
        const double cpu = static_cast<double>(svcGetSystemTick() - t0) / (SYSCLOCK_ARM11 / 1000.0);

        if (frame % hold == 10) {
            dumpTarget(bottom, 320, dir + "/uitest_" + std::to_string(phase + 1) + ".ppm");
            if (phase == 0) dumpTarget(top, 400, dir + "/uitest_top_1.ppm");
            if (phase == 2) writeChecks(dir);
            say("wrote the phase %d dumps\n", phase + 1);
        }
        if (frame % hold == 2) {
            say("phase %d %s: top %d runs %d vertices %.1f KB arena; both screens %d flushes, %d runs (%d two-texture), "
                "%d vertices, %.1f KB vertex data, frame arena used %.1f KB (high water %.1f KB), arena failures %d/%u, "
                "missing textures %d\n",
                phase + 1, kPhaseNames[phase], topStats.runs, topStats.vertices, kb(topStats.arenaBytes), st.flushes,
                st.runs, st.flat2Runs, st.vertices, kb(st.arenaBytes), kb(arenaUsed), kb(pica::frameArena().highWater()),
                st.arenaFailures, pica::frameArena().failures(), st.missingTextures);
        }
        if (frame % hold >= 2) {
            const double g = static_cast<double>(C3D_GetDrawingTime());
            gpuSum += g;
            gpuMax = std::max(gpuMax, g);
            cpuSum += cpu;
            cpuMax = std::max(cpuMax, cpu);
            ++n;
        }
        if (holdFrames > 0 && frame >= (kPhases + 1) * hold) break;
    }
    if (phase >= 0 && n > 0)
        say("phase %d %s: GPU avg %.3f ms max %.3f ms, CPU avg %.3f ms max %.3f ms over %d frames\n", phase + 1,
            kPhaseNames[phase], gpuSum / n, gpuMax, cpuSum / n, cpuMax, n);
    say("uitest: done\n");
    tt.t1 = Texture2D();
    tt.t2 = Texture2D();
    assets.reset();
    r2d.reset();
    pica::shutdownBackend();
    C3D_RenderTargetDelete(top);
    C3D_RenderTargetDelete(bottom);
    world.reset();
    if (g_log) std::fclose(g_log);
    g_log = nullptr;
}

} // namespace uitest
