// GameView's layer methods (M0.6 of the 3DS port): draw() is exactly the world, HUD, 2D-layer
// and brightness calls in order; the HUD in a 320x240 rectangle lies at 0.4 scale; one
// prepareWorld serves several drawWorld calls. Needs the first game's data and a headless GLES
// context; skips loudly without them.
#include "doctest.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "../game/game_session.h"
#include "../game/game_view.h"
#include "../game/touch_overlay.h"
#include "as3d/hud_layout.h"
#include "as3d/image.h"
#include "as3d/defs.h"
#include "as3d/gfx.h"
#include "as3d/input.h"
#include "as3d/platform.h"
#include "as3d/render_state.h"
#include "as3d/ui.h"
#include "as3d/world.h"
#include "test_data.h"

using namespace as3d;

namespace {

std::unique_ptr<GraphicsContext>& viewContext() {
    static std::unique_ptr<GraphicsContext> ctx = [] {
        GraphicsConfig cfg;
        cfg.width = 64;
        cfg.height = 64;
        cfg.headless = true;
        return createGraphicsContext(cfg);
    }();
    if (ctx) ctx->makeCurrent();
    return ctx;
}

#define GAMEVIEW_REQUIRE_GL()                                                                   \
    if (!viewContext()) {                                                                       \
        std::fprintf(stderr, "SKIPPED (no headless GLES context available): %s\n", __FILE__); \
        return;                                                                                 \
    }

struct Level1 {
    as3d_game::GameSession session;
    as3d_game::GameView view;
    bool init(int frames) {
        as3d_game::GameOptions o;
        o.dataRoot = testdata::root();
        o.mission = 1;
        o.levelFlow = false;
        std::string err;
        if (!session.init(o, &err)) return false;
        if (!view.init(session, &err)) return false;
        for (int f = 0; f < frames; ++f) {
            session.step(botInput(static_cast<u32>(f)));
            view.step(session);
        }
        return true;
    }
};

} // namespace

TEST_CASE("game view: draw() equals the layer methods called in order") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    GAMEVIEW_REQUIRE_GL();
    for (int mode = 0; mode < 2; ++mode) {
        CAPTURE(mode);
        Level1 g;
        REQUIRE(g.init(120));
        g.view.screenMode = mode == 0 ? kScreenWide : kScreen4x3;
        const int W = 960, H = 432;
        RenderTarget target;
        REQUIRE(target.create(W, H, 0));
        Image composed, assembled;
        target.bind();
        g.view.draw(g.session, W, H);
        REQUIRE(target.readPixels(composed));

        target.bind();
        const as3d_game::ViewRect frame{0, 0, W, H};
        const as3d_game::ViewRect wr = g.view.worldRect(frame);
        g.view.prepareWorld(g.session, wr);
        g.view.drawWorld(g.session, wr, EyeView{});
        g.view.drawHud(g.session, frame, as3d_game::HudTarget::Main);
        g.view.drawFront(g.session, frame, nullptr, true);
        g.view.drawBrightness(frame);
        g.view.clearBars(frame);
        REQUIRE(target.readPixels(assembled));

        REQUIRE(composed.rgba.size() == assembled.rgba.size());
        CHECK(composed.rgba == assembled.rgba);
        // Not a blank frame.
        long lit = 0;
        for (size_t i = 0; i < composed.rgba.size(); i += 4) lit += composed.rgba[i] + composed.rgba[i + 1] + composed.rgba[i + 2] > 30;
        CHECK(lit > 10000);
    }
}

TEST_CASE("game view: the HUD in a 320x240 rectangle is the 0.4 scale layout") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    GAMEVIEW_REQUIRE_GL();
    Level1 g;
    REQUIRE(g.init(120));
    REQUIRE(g.view.hudAvailable());
    // The quads of the HUD in 800x600 and in 320x240, on CPU-only renderers.
    const ui::HudState state = as3d_game::hudStateOf(g.session);
    ui::Renderer2D big, small;
    big.begin(800, 600);
    small.begin(320, 240);
    ui::drawHud(big, g.view.assets(), state);
    ui::drawHud(small, g.view.assets(), state);
    REQUIRE(big.quads().size() > 4u);
    REQUIRE(big.quads().size() == small.quads().size());
    const ui::Mapping& mb = big.mapping();
    const ui::Mapping& ms = small.mapping();
    CHECK(ms.scaleX == doctest::Approx(0.4f));
    CHECK(ms.scaleY == doctest::Approx(0.4f));
    CHECK(ms.offsetX == doctest::Approx(0.0f));
    CHECK(ms.offsetY == doctest::Approx(0.0f));
    // Two known elements: the first quad drawn and the last one.
    for (size_t i : {size_t(0), big.quads().size() - 1}) {
        const ui::Quad& a = big.quads()[i];
        const ui::Quad& b = small.quads()[i];
        CHECK(a.x == doctest::Approx(b.x));
        CHECK(a.y == doctest::Approx(b.y));
        CHECK(ms.toFbX(b.x) == doctest::Approx(0.4f * mb.toFbX(a.x)));
        CHECK(ms.toFbY(b.y) == doctest::Approx(0.4f * mb.toFbY(a.y)));
        CHECK(ms.toFbX(b.x + b.w) - ms.toFbX(b.x) == doctest::Approx(0.4f * (mb.toFbX(a.x + a.w) - mb.toFbX(a.x))));
    }
    // The GL path: the HUD drawn into a 320x240 rectangle of a larger target touches only it.
    RenderTarget t;
    REQUIRE(t.create(640, 480, 0));
    t.bind();
    clear(Vec4{0.25f, 0.0f, 0.25f, 1.0f});
    const as3d_game::ViewRect rect{200, 100, 320, 240};
    g.view.drawHud(g.session, rect, as3d_game::HudTarget::Bottom320x240);
    Image img;
    REQUIRE(t.readPixels(img));
    Image ref;
    t.bind();
    clear(Vec4{0.25f, 0.0f, 0.25f, 1.0f});
    REQUIRE(t.readPixels(ref));
    long inside = 0, outside = 0;
    const int rowStart = img.height - (rect.y + rect.height), rowEnd = img.height - rect.y; // either row order
    for (int y = 0; y < img.height; ++y)
        for (int x = 0; x < img.width; ++x) {
            const size_t o = (static_cast<size_t>(y) * img.width + x) * 4;
            const bool changed = img.rgba[o] != ref.rgba[o] || img.rgba[o + 1] != ref.rgba[o + 1] || img.rgba[o + 2] != ref.rgba[o + 2];
            const bool in = x >= rect.x && x < rect.x + rect.width && y >= rowStart && y < rowEnd;
            if (changed) (in ? inside : outside)++;
        }
    CHECK(inside > 100);
    CHECK(outside == 0);
}

TEST_CASE("game view: one prepareWorld serves several drawWorld calls") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    GAMEVIEW_REQUIRE_GL();
    Level1 g;
    REQUIRE(g.init(120));
    const int W = 640, H = 480;
    RenderTarget t;
    REQUIRE(t.create(W, H, 0));
    const as3d_game::ViewRect rect{0, 0, W, H};
    g.view.prepareWorld(g.session, rect);
    Image a, b;
    t.bind();
    g.view.drawWorld(g.session, rect, EyeView{});
    REQUIRE(t.readPixels(a));
    t.bind();
    clear(Vec4{1, 0, 1, 1});
    g.view.drawWorld(g.session, rect, EyeView{});
    REQUIRE(t.readPixels(b));
    CHECK(a.rgba == b.rgba);
    // A stereo pair from one prepare: the two eyes differ, each is repeatable.
    g.view.prepareWorld(g.session, rect, 4.0f, 300.0f);
    Image l1, r1, l2;
    t.bind();
    g.view.drawWorld(g.session, rect, EyeView{-4.0f, 300.0f});
    REQUIRE(t.readPixels(l1));
    t.bind();
    g.view.drawWorld(g.session, rect, EyeView{4.0f, 300.0f});
    REQUIRE(t.readPixels(r1));
    t.bind();
    g.view.drawWorld(g.session, rect, EyeView{-4.0f, 300.0f});
    REQUIRE(t.readPixels(l2));
    CHECK(l1.rgba == l2.rgba);
    CHECK(l1.rgba != r1.rgba);
}

TEST_CASE("game view: the Bottom320x240 HUD draws inside its element rectangles and above the touch band") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    GAMEVIEW_REQUIRE_GL();
    Level1 g;
    REQUIRE(g.init(60));
    REQUIRE(g.view.hudAvailable());
    // The fullest content: every missile type, every power-up kind, five lives, a long score,
    // the level name typed out and a message.
    ui::HudState state = as3d_game::hudStateOf(g.session);
    ui::HudPlayer& p = state.players[0];
    p.lives = 5;
    p.score = 1234567890;
    for (int t = 0; t < ui::kMissileTypes; ++t) p.missiles[t] = 12;
    for (int k = 0; k < ui::kPowerupIconKinds; ++k) p.powerups[k] = 3;
    state.levelName = "Operation Thunderstrike Alpha";
    state.levelTime = 3.0f;
    state.message = "Cheat enabled";
    state.messageAge = 0.5f;
    const ui::HudLayout& L = ui::hudLayout(GameId::AirStrike3D, ui::HudTarget::Bottom320x240);
    const std::vector<ui::HudElementRect> rects = ui::hudElementRects(L);
    ui::UiAssets& assets = const_cast<ui::UiAssets&>(g.view.assets());
    assets.hudTarget = ui::HudTarget::Bottom320x240;
    ui::Renderer2D r;
    r.begin(320, 240);
    ui::drawHud(r, assets, state);
    assets.hudTarget = ui::HudTarget::Main;
    REQUIRE(r.quads().size() > 10u);
    for (const ui::Quad& q : r.quads()) {
        // The quad (virtual pixels) lies inside some element's rectangle, never in the band.
        bool inside = false;
        // A number's glyph cell is 32 wide for a 14 pixel advance (the extra is transparent).
        const float qw = (q.h == 16.0f * L.countScale && q.w == 32.0f * L.countScale) ? 14.0f * L.countScale : q.w;
        for (const ui::HudElementRect& e : rects)
            inside = inside || (q.x >= e.x - 1.0f && q.y >= e.y - 1.0f && q.x + qw <= e.x + e.w + 1.0f && q.y + q.h <= e.y + e.h + 1.0f);
        INFO("quad " << q.x << "," << q.y << " " << q.w << "x" << q.h);
        CHECK(inside);
        CHECK(q.y + q.h <= ui::kHudBottomBandTop + 1.0f);
    }
}

TEST_CASE("game view: dual-screen composite of level 1 after 120 bot frames") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    GAMEVIEW_REQUIRE_GL();
    Level1 g;
    REQUIRE(g.init(120));
    g.view.dualScreen = true;
    const int W = as3d_game::kDualFrameWidth, H = as3d_game::kDualFrameHeight;
    const as3d_game::DualScreenRects d = as3d_game::dualScreenRects(W, H);
    CHECK(d.top.x == 0);
    CHECK(d.top.width == 400);
    CHECK(d.top.height == 240);
    CHECK(d.bottom.width == 320);
    CHECK(d.bottom.height == 240);
    CHECK(d.bottom.x == 40);
    CHECK(d.top.y == 240); // GL convention: the top screen is the upper half
    CHECK(d.bottom.y == 0);
    RenderTarget target;
    REQUIRE(target.create(W, H, 0));
    target.bind();
    g.view.draw(g.session, W, H);
    // The touch buttons of the preset over the bottom screen, as the game does.
    TouchMapper touch;
    touch.setScreen(d.bottom.width, d.bottom.height, TouchLayoutOptions::forBottomScreen320x240(132.0f * d.scale));
    as3d_game::TouchOverlayState ts;
    ui::HudPlayer hud = as3d_game::hudStateOf(g.session).players[0];
    ts.player = &hud;
    ts.assets = &g.view.assets();
    ts.rules = &g.session.rules();
    as3d_game::drawTouchControlsIn(g.view.overlay(), d.bottom.x, d.bottom.y, d.bottom.width, d.bottom.height, touch, ts);
    Image img;
    REQUIRE(target.readPixels(img));
    REQUIRE(img.width == W);
    REQUIRE(img.height == H);
    std::filesystem::create_directories(std::string(AS3D_REPO_ROOT) + "/out");
    CHECK(writePng((std::string(AS3D_REPO_ROOT) + "/out/dual_screen_level1.png").c_str(), img));

    auto px = [&](int x, int yFromTop) {
        const size_t o = (static_cast<size_t>(yFromTop) * img.width + x) * 4;
        return std::array<int, 3>{img.rgba[o], img.rgba[o + 1], img.rgba[o + 2]};
    };
    long topLit = 0, bottomLit = 0, sideBars = 0, hudRed = 0, bandLit = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const auto c = px(x, y);
            const bool lit = c[0] + c[1] + c[2] > 30;
            if (y < 240) topLit += lit;
            else if (x >= 40 && x < 360) {
                bottomLit += lit;
                // The HUD's atlas colours: the score's red-orange (0.75, 0.19, 0).
                if (y < 240 + 204 && c[0] > 120 && c[1] < 100 && c[2] < 40) hudRed++;
                if (y >= 240 + 204) bandLit += lit;
            } else {
                sideBars += lit; // beside the bottom screen: black
            }
        }
    CHECK(topLit > 400 * 240 / 4); // the world fills the top screen
    CHECK(bottomLit > 500);        // the HUD
    CHECK(hudRed > 20);            // score digits / count colours
    CHECK(bandLit > 100);          // the touch buttons
    CHECK(sideBars == 0);          // nothing outside the two screens
}
