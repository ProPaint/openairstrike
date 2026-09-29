// WP-51 (docs/spec/issues/140): the round touch buttons at 4:3, 16:9 and 20:9 in both Screen
// modes and both hands, display cutouts, the buttons' fade, the new settings in the profile
// (and a profile written before them), the circle primitive of the 2D layer, and the
// simulation not depending on the Screen setting. The GL parts skip loudly without a
// headless GLES context, the game parts without game data.
#include "doctest.h"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../game/game_session.h"
#include "../game/fps_counter.h"
#include "../game/game_view.h"
#include "as3d/defs.h"
#include "as3d/gfx.h"
#include "as3d/input.h"
#include "as3d/platform.h"
#include "as3d/profile.h"
#include "as3d/ui.h"
#include "as3d/vfs.h"
#include "as3d/world.h"
#include "test_data.h"

using namespace as3d;

namespace {

const int kSizes[][2] = {{1024, 768}, {800, 600}, {1920, 1080}, {2400, 1080}, {1600, 720}};

float dist(const TouchCircle& a, const TouchCircle& b) { return std::hypot(a.x - b.x, a.y - b.y); }

std::unique_ptr<GraphicsContext>& polishContext() {
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

#define POLISH_REQUIRE_GL()                                                                     \
    if (!polishContext()) {                                                                     \
        std::fprintf(stderr, "SKIPPED (no headless GLES context available): %s\n", __FILE__);  \
        return;                                                                                 \
    }

u32 crc32(const u8* data, size_t n) {
    u32 c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= data[i];
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return c ^ 0xFFFFFFFFu;
}

void put32(std::vector<u8>& b, u32 v) {
    for (int i = 0; i < 4; i++) b.push_back(static_cast<u8>(v >> (8 * i)));
}

} // namespace

TEST_CASE("polish layout: round buttons at 4:3, 16:9 and 20:9, both screen modes and hands") {
    for (const auto& sz : kSizes) {
        for (int screen = 0; screen < 2; ++screen) {
            for (int hand = 0; hand < 2; ++hand) {
                CAPTURE(sz[0]);
                CAPTURE(sz[1]);
                CAPTURE(screen);
                CAPTURE(hand);
                TouchLayoutOptions o;
                o.screen4x3 = screen == 1;
                o.leftHanded = hand == 1;
                const TouchLayout L = computeTouchLayout(sz[0], sz[1], o);
                const float fw = static_cast<float>(sz[0]), fh = static_cast<float>(sz[1]);
                const bool wide = sz[0] * 3 > sz[1] * 4;
                // 16:9 and wider put the buttons outside the field; 4:3 cannot.
                CHECK(L.outside == (sz[0] * 9 >= sz[1] * 16));
                const float pfx0 = L.playField.x * fw, pfx1 = (L.playField.x + L.playField.w) * fw;
                for (int b = 0; b < kTouchButtonCount; ++b) {
                    CAPTURE(b);
                    const TouchCircle& c = L.circles[b];
                    const TouchCircle& h = L.hit[b];
                    CHECK(c.r > 0);
                    CHECK(c.x - c.r >= -0.5f);
                    CHECK(c.x + c.r <= fw + 0.5f);
                    CHECK(c.y - c.r >= -0.5f);
                    CHECK(c.y + c.r <= fh + 0.5f);
                    // At least 9 mm across to touch (the density is unknown here: the screen
                    // is taken for a 68 mm phone), and at least the drawn button.
                    CHECK(2 * h.r >= 9.0f * L.pixelsPerMm - 0.01f);
                    CHECK(h.r >= c.r);
                    // Round: a point on the drawn circle is on the button, the box corner is not.
                    CHECK(L.hitTest((c.x) / fw, (c.y) / fh) == b);
                    const float corner = h.r * 0.98f; // inside the bounding square, outside the circle
                    const int atCorner = L.hitTest((h.x + corner) / fw, (h.y + corner) / fh);
                    CHECK(atCorner != b);
                    // Drawn buttons never overlap.
                    for (int k = b + 1; k < kTouchButtonCount; ++k) CHECK(dist(c, L.circles[k]) >= c.r + L.circles[k].r - 0.5f);
                    if (!L.outside && b != static_cast<int>(TouchButton::Pause)) {
                        // Inside the field: below the HUD's power-up (right) or missile (left)
                        // column, and above the lives (left).
                        const float pfy = L.playField.y * fh, pfh = L.playField.h * fh;
                        CHECK(c.y - c.r >= pfy + pfh * (hand ? 285.0f : 210.0f) / 600.0f - 0.5f);
                        if (hand) CHECK(c.y + c.r <= pfy + pfh * 548.0f / 600.0f + 0.5f);
                    }
                    if (L.outside && wide) {
                        // In the bars (4:3 mode: over black; Wide: over the extra world).
                        const bool inLeftBar = c.x + c.r <= pfx0 + 0.5f;
                        const bool inRightBar = c.x - c.r >= pfx1 - 0.5f;
                        CHECK((inLeftBar || inRightBar));
                        const bool actionSide = hand == 0 ? inRightBar : inLeftBar;
                        CHECK((b == static_cast<int>(TouchButton::Pause)) != actionSide);
                    }
                }
                // Missile is the lowest big button, power-up above it; the satellites are smaller.
                const TouchCircle& m = L.circles[static_cast<int>(TouchButton::Missile)];
                const TouchCircle& p = L.circles[static_cast<int>(TouchButton::PowerUp)];
                CHECK(m.y > p.y);
                for (TouchButton s : {TouchButton::NextMissile, TouchButton::NextWeapon, TouchButton::NextPowerUp})
                    CHECK(L.circles[static_cast<int>(s)].r < m.r);
                // The pause button is in a top corner (outside) or top centre (inside).
                const TouchCircle& pz = L.circles[static_cast<int>(TouchButton::Pause)];
                CHECK(pz.y < fh * 0.2f);
                // Outside the field the left-handed layout is the mirror image of the
                // right-handed one (inside, the left cluster stays clear of the lives and the
                // missile column of the HUD, so it sits higher).
                TouchLayoutOptions r = o;
                r.leftHanded = !o.leftHanded;
                const TouchLayout R = computeTouchLayout(sz[0], sz[1], r);
                for (int b = 0; b < kTouchButtonCount && L.outside; ++b) {
                    CHECK(std::fabs(R.circles[b].x - (fw - L.circles[b].x)) < 0.01f);
                    CHECK(std::fabs(R.circles[b].y - L.circles[b].y) < 0.01f);
                }
            }
        }
    }
}

TEST_CASE("polish layout: sizes follow the display density") {
    // 2400 x 1080 at 400 dpi (a typical phone): 12.5 mm main buttons are about 197 px.
    TouchLayoutOptions o;
    o.dpi = 400;
    TouchLayout L = computeTouchLayout(2400, 1080, o);
    CHECK(L.pixelsPerMm == doctest::Approx(400.0f / 25.4f));
    CHECK(2 * L.circles[static_cast<int>(TouchButton::Missile)].r == doctest::Approx(12.5f * 400.0f / 25.4f).epsilon(0.01));
    CHECK(L.outside);
    // An implausible density falls back to the phone assumption.
    o.dpi = 5;
    L = computeTouchLayout(2400, 1080, o);
    CHECK(L.pixelsPerMm == doctest::Approx(1080.0f / 68.0f));
}

TEST_CASE("polish layout: cutouts keep every button out of the insets, both hands") {
    for (int hand = 0; hand < 2; ++hand) {
        for (int side = 0; side < 2; ++side) {
            CAPTURE(hand);
            CAPTURE(side);
            TouchLayoutOptions o;
            o.leftHanded = hand == 1;
            if (side == 0) o.insets.left = 130;
            else o.insets.right = 130;
            o.insets.top = 40;
            const TouchLayout L = computeTouchLayout(2400, 1080, o);
            for (int b = 0; b < kTouchButtonCount; ++b) {
                const TouchCircle& c = L.circles[b];
                CHECK(c.x - c.r >= static_cast<float>(o.insets.left) - 0.5f);
                CHECK(c.x + c.r <= 2400.0f - static_cast<float>(o.insets.right) + 0.5f);
                CHECK(c.y - c.r >= static_cast<float>(o.insets.top) - 0.5f);
            }
        }
    }
}

TEST_CASE("polish: a finger on a button's round area presses it, a drag in the bars steers") {
    TouchMapper t;
    TouchLayoutOptions o;
    o.screen4x3 = true;
    t.setScreen(2400, 1080, o);
    const TouchLayout& L = t.layout();
    const TouchCircle& m = L.circles[static_cast<int>(TouchButton::Missile)];
    t.touchEvent(1, TouchPhase::Down, (m.x + m.r * 0.9f) / 2400.0f, m.y / 1080.0f);
    CHECK(t.buttonHeld(TouchButton::Missile));
    FrameInput in = t.takeFrame();
    CHECK((in.held[0] & ACT_MISSILE) != 0u);
    t.touchEvent(1, TouchPhase::Up, 0, 0);
    // A drag that starts in the right bar, above the buttons, steers like one in the field.
    t.setPlayerScreen(true, 400, 450);
    t.takeFrame();
    t.touchEvent(2, TouchPhase::Down, 2300.0f / 2400.0f, 0.1f);
    CHECK(t.dragging());
    t.takeFrame();
    t.touchEvent(2, TouchPhase::Move, 2200.0f / 2400.0f, 0.1f);
    t.setPlayerScreen(true, 400, 450);
    in = t.takeFrame();
    CHECK((in.held[0] & ACT_LEFT) != 0u);
}

TEST_CASE("polish: the buttons fade 2 s after the last use and come back at a touch") {
    CHECK(touchFadeAlpha(0.0f, 0.5f) == 1.0f);
    CHECK(touchFadeAlpha(1.99f, 0.5f) == 1.0f);
    CHECK(touchFadeAlpha(2.0f, 0.5f) == 1.0f);
    const float mid = touchFadeAlpha(2.25f, 0.5f);
    CHECK(mid < 1.0f);
    CHECK(mid > 0.5f);
    CHECK(touchFadeAlpha(2.5f, 0.5f) == doctest::Approx(0.5f));
    CHECK(touchFadeAlpha(100.0f, 0.35f) == doctest::Approx(0.35f));
    TouchFade f;
    CHECK(f.alpha(0.5f) == 1.0f); // full opacity when play starts
    for (int i = 0; i < 180; ++i) f.update(1.0f / 60.0f, false);
    CHECK(f.alpha(0.5f) == doctest::Approx(0.5f));
    f.update(1.0f / 60.0f, true); // a button pressed
    CHECK(f.alpha(0.5f) == 1.0f);
    for (int i = 0; i < 119; ++i) f.update(1.0f / 60.0f, false);
    CHECK(f.alpha(0.5f) == 1.0f); // still within the 2 s
    for (int i = 0; i < 40; ++i) f.update(1.0f / 60.0f, false);
    CHECK(f.alpha(0.5f) == doctest::Approx(0.5f));
}

TEST_CASE("polish profile: the new settings round-trip; a profile written before them loads") {
    Profile p;
    p.settings.screenMode = kScreen4x3;
    p.settings.leftHanded = true;
    p.settings.touchSpeed = 3;
    p.settings.brightness = 0.8f;
    const std::vector<u8> bytes = serializeProfile(p);
    Profile q;
    std::string why;
    REQUIRE_MESSAGE(deserializeProfile(bytes.data(), bytes.size(), q, &why), why);
    CHECK(q.settings.screenMode == kScreen4x3);
    CHECK(q.settings.leftHanded);
    CHECK(q.settings.touchSpeed == 3);
    CHECK(q.settings.brightness == doctest::Approx(0.8f));

    // The version 1 layout as written before WP-51: PROG, then SETT without the new keys.
    std::vector<u8> prog;
    const Progress d = Progress::defaults();
    prog.push_back(kHighScoreCount);
    for (const HighScore& h : d.scores) {
        prog.push_back(static_cast<u8>(h.name.size()));
        prog.insert(prog.end(), h.name.begin(), h.name.end());
        put32(prog, static_cast<u32>(h.score));
        put32(prog, 0);
        prog.push_back(static_cast<u8>(h.rank));
    }
    prog.push_back(kHelicopterCount);
    for (int i = 0; i < kHelicopterCount; ++i) prog.push_back(i < 3 ? 1 : 0);
    prog.push_back(kMissionCount);
    for (int i = 0; i < kMissionCount; ++i) prog.push_back(i < 5 ? 1 : 0);
    std::vector<u8> sett;
    const std::pair<const char*, int> kv[] = {{"camera", 2}, {"brightness", 700}, {"showFps", 1}};
    sett.push_back(3);
    sett.push_back(0);
    for (const auto& e : kv) {
        sett.push_back(static_cast<u8>(std::strlen(e.first)));
        sett.insert(sett.end(), e.first, e.first + std::strlen(e.first));
        put32(sett, static_cast<u32>(e.second));
    }
    std::vector<u8> payload;
    for (auto chunk : {std::make_pair("PROG", &prog), std::make_pair("SETT", &sett)}) {
        payload.insert(payload.end(), chunk.first, chunk.first + 4);
        put32(payload, static_cast<u32>(chunk.second->size()));
        payload.insert(payload.end(), chunk.second->begin(), chunk.second->end());
    }
    std::vector<u8> file = {'A', 'S', '3', 'D', 'P', 'R', 'O', 'F'};
    put32(file, 1);
    put32(file, static_cast<u32>(payload.size()));
    put32(file, crc32(payload.data(), payload.size()));
    file.insert(file.end(), payload.begin(), payload.end());
    Profile old;
    REQUIRE_MESSAGE(deserializeProfile(file.data(), file.size(), old, &why), why);
    CHECK(old.settings.camera == 2);
    CHECK(old.settings.brightness == doctest::Approx(0.7f));
    CHECK(old.settings.showFps);
    CHECK(old.settings.screenMode == kScreenWide); // the defaults for what the file lacks
    CHECK_FALSE(old.settings.leftHanded);
    CHECK(old.settings.touchSpeed == kDefaultTouchSpeed);
    CHECK(old.progress.missionUnlocked[4]);
    // An out-of-range value falls back to the default.
    Settings s;
    s.screenMode = 7;
    s.touchSpeed = 5;
    s.clampToRanges();
    CHECK(s.screenMode == kScreenWide);
    CHECK(s.touchSpeed == kDefaultTouchSpeed);
}

TEST_CASE("polish: the 2D layer's circle and ring are round, anti-aliased and hollow") {
    POLISH_REQUIRE_GL();
    ui::Renderer2D r;
    std::string err;
    REQUIRE_MESSAGE(r.init(&err), err);
    RenderTarget target;
    REQUIRE(target.create(800, 600, 0)); // no multisampling: the edge is the shader's own
    target.bind();
    glViewport(0, 0, 800, 600);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    r.begin(800, 600);
    r.circle(200, 300, 100, ui::Color{1, 0, 0, 1});
    r.ring(600, 300, 100, 20, ui::Color{0, 1, 0, 1});
    r.flush();
    Image img;
    REQUIRE(target.readPixels(img));
    auto px = [&](int x, int y, int c) { return static_cast<int>(img.rgba[(static_cast<size_t>(y) * img.width + x) * 4 + c]); };
    // readPixels rows are top-down like the virtual screen (as3d/gfx.h).
    CHECK(px(200, 300, 0) == 255);   // centre
    CHECK(px(200, 205, 0) == 255);   // inside, near the top
    CHECK(px(200, 300 - 110, 0) == 0); // outside
    CHECK(px(200 + 75, 300 + 75, 0) == 0); // the square's corner is not drawn: round
    CHECK(px(200 + 65, 300 + 65, 0) == 255); // inside at 45 degrees (radius 92)
    // One-pixel anti-aliased edge: an intermediate value right on the circle.
    int edge = -1;
    for (int x = 295; x <= 302; ++x)
        if (px(x, 300, 0) > 20 && px(x, 300, 0) < 235) edge = x;
    CHECK(edge >= 298);
    // The ring: green on its band, empty in the hole and outside.
    CHECK(px(600 + 90, 300, 1) == 255);
    CHECK(px(600, 300, 1) == 0);
    CHECK(px(600 + 70, 300, 1) == 0);
    CHECK(px(600 + 110, 300, 1) == 0);
    // Round on a stretched mapping too (5:4 to 4:3 is stretched): 900 x 600 is wider than
    // 4:3 so scaled uniformly, 750 x 600 is 5:4, stretched.
    r.begin(750, 600);
    const float rx = 50.0f * r.mapping().scaleY / r.mapping().scaleX;
    r.circle(400, 300, 50, ui::Color{1, 1, 1, 1});
    REQUIRE(r.quads().size() == 1u);
    CHECK(r.quads()[0].w * r.mapping().scaleX == doctest::Approx(r.quads()[0].h * r.mapping().scaleY));
    CHECK(r.quads()[0].w == doctest::Approx(2 * rx));
}

TEST_CASE("polish: the simulation does not depend on the Screen setting") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    POLISH_REQUIRE_GL();
    std::string dumps[2];
    int barPixels[2] = {0, 0};
    for (int mode = 0; mode < 2; ++mode) {
        as3d_game::GameSession s;
        as3d_game::GameOptions o;
        o.dataRoot = testdata::root();
        o.mission = 1;
        o.levelFlow = false;
        std::string err;
        REQUIRE_MESSAGE(s.init(o, &err), err);
        as3d_game::GameView view;
        view.screenMode = mode == 0 ? kScreenWide : kScreen4x3;
        REQUIRE_MESSAGE(view.init(s, &err), err);
        RenderTarget target;
        REQUIRE(target.create(960, 432, 4)); // 20:9
        for (u32 f = 0; f < 600; ++f) {
            s.step(botInput(f));
            view.step(s);
            if (f % 5 != 0) continue;
            target.bind();
            view.draw(s, 960, 432); // every 5th frame drawn, as a loaded device might
        }
        Image img;
        REQUIRE(target.readPixels(img));
        // The left bar (x < 192): world in Wide, black in 4:3.
        for (int y = 0; y < img.height; y += 4)
            for (int x = 0; x < 180; x += 4) {
                const u8* p = &img.rgba[(static_cast<size_t>(y) * img.width + x) * 4];
                if (p[0] + p[1] + p[2] > 30) ++barPixels[mode];
            }
        dumps[mode] = s.world().dumpStateJson();
    }
    CHECK(barPixels[0] > 1000);
    CHECK(barPixels[1] == 0);
    CHECK(dumps[0].size() > 1000);
    CHECK(dumps[0] == dumps[1]);
}

namespace {

bool heliAt(const World& w, float& x, float& y) {
    int pi = w.playerEntityIndex(0);
    if (pi < 0) return false;
    const ScreenRect& r = w.entity(pi).rect;
    if (!(r.max[0] > r.min[0])) return false;
    x = 0.5f * (r.min[0] + r.max[0]);
    y = 600.0f - 0.5f * (r.min[1] + r.max[1]);
    return true;
}

struct SwipeResult {
    float moved = 0;        // virtual px the helicopter travelled
    int framesTo90 = -1;    // frames until 90 % of the final displacement
    float topSpeed = 0;     // px per frame
    int framesAtTop = 0;    // frames at >= 95 % of the top speed before arriving
    int framesTo150 = -1;   // frames until the helicopter is 150 px to the right
};

// Mission 1 past the fly-in: the helicopter is brought to x ~ 250, then a fast swipe (the
// finger moves `fingerPx` virtual pixels right in 6 frames, 0.1 s) with the mapper's `gain`
// and `lead`; returns how the helicopter follows.
SwipeResult swipe(float gain, float lead, float fingerPx) {
    Vfs vfs;
    vfs.mount(makeDirSource(testdata::extractedDir()));
    DefDatabase db;
    db.load(vfs);
    World world;
    world.init(vfs, db, WorldConfig());
    std::string err;
    world.loadLevel("1", &err);
    TouchMapper t;
    t.setScreen(800, 600);
    std::vector<float> xs;
    auto step = [&](bool record) {
        float x = 0, y = 0;
        bool ok = heliAt(world, x, y);
        t.setPlayerScreen(ok, x, y);
        FrameInput in = t.takeFrame();
        in.confirm = true;
        world.step(in.toPlayerInput());
        if (record && heliAt(world, x, y)) xs.push_back(x);
    };
    for (int f = 0; f < 420; ++f) step(false);
    // Bring it to the left with the default mapper, then let it settle.
    t.touchEvent(1, TouchPhase::Down, 0.5f, 0.7f);
    step(false);
    t.touchEvent(1, TouchPhase::Move, 0.5f - 100.0f / 800.0f, 0.7f);
    for (int f = 0; f < 240; ++f) step(false);
    t.touchEvent(1, TouchPhase::Up, 0.5f, 0.7f);
    for (int f = 0; f < 60; ++f) step(false);
    t.settings().gain = gain;
    t.settings().maxLead = lead;
    const float fx0 = 0.3f;
    t.touchEvent(2, TouchPhase::Down, fx0, 0.7f);
    step(true);
    for (int k = 1; k <= 6; ++k) {
        t.touchEvent(2, TouchPhase::Move, fx0 + fingerPx / 800.0f * static_cast<float>(k) / 6.0f, 0.7f);
        step(true);
    }
    for (int f = 0; f < 300; ++f) step(true);
    SwipeResult r;
    if (xs.size() < 10) return r;
    const float x0 = xs.front(), x1 = xs.back();
    r.moved = x1 - x0;
    for (size_t i = 1; i < xs.size(); ++i) r.topSpeed = std::max(r.topSpeed, xs[i] - xs[i - 1]);
    for (size_t i = 0; i < xs.size(); ++i)
        if (xs[i] - x0 >= 150.0f) {
            r.framesTo150 = static_cast<int>(i);
            break;
        }
    for (size_t i = 0; i < xs.size(); ++i) {
        if (xs[i] - x0 >= 0.9f * r.moved) {
            r.framesTo90 = static_cast<int>(i);
            break;
        }
        if (i > 0 && xs[i] - xs[i - 1] >= 0.95f * r.topSpeed) ++r.framesAtTop;
    }
    return r;
}

} // namespace

TEST_CASE("touch speed: the setting scales the drag gain and the lead") {
    CHECK(touchSpeedFactor(0) == 1.0f);
    CHECK(touchSpeedFactor(1) == 1.25f);
    CHECK(touchSpeedFactor(4) == 2.0f);
    CHECK(touchSpeedFactor(-3) == 1.0f);
    CHECK(touchSpeedFactor(9) == 2.0f);
    TouchSettings s;
    applyTouchSpeed(s, 0); // the original feel (WP-48)
    CHECK(s.gain == 1.5f);
    CHECK(s.maxLead == 160.0f);
    applyTouchSpeed(s, kDefaultTouchSpeed);
    CHECK(s.gain == doctest::Approx(1.875f));
    CHECK(s.maxLead == doctest::Approx(200.0f));
    applyTouchSpeed(s, 4);
    CHECK(s.gain == doctest::Approx(3.0f));
    CHECK(s.deadZone == TouchSettings().deadZone); // only gain and lead change
    CHECK(s.lookAhead == TouchSettings().lookAhead);
}

TEST_CASE("touch speed: a fast swipe on the real helicopter goes further and arrives sooner") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    // The finger moves 200 virtual px in 0.1 s; the helicopter's own top speed (150 units/s,
    // about 8.2 px a frame) is the script's and is not changed.
    SwipeResult r[kTouchSpeedSteps];
    for (int step = 0; step < kTouchSpeedSteps; ++step) {
        TouchSettings ts;
        applyTouchSpeed(ts, step);
        r[step] = swipe(ts.gain, ts.maxLead, 200.0f);
        MESSAGE("step ", step, " (gain ", ts.gain, ", lead ", ts.maxLead, "): moved ", r[step].moved,
                " px, 150 px after ", r[step].framesTo150, " frames, 90% after ", r[step].framesTo90,
                " frames, top speed ", r[step].topSpeed, " px/frame, frames at top speed ", r[step].framesAtTop);
    }
    // Step 0 is the old feel: the 300 px the finger asks for are cut to the 160 px lead.
    CHECK(r[0].moved < 180.0f);
    for (int step = 1; step < kTouchSpeedSteps; ++step) {
        CHECK(r[step].moved > r[step - 1].moved + 20.0f);
        CHECK(r[step].framesTo150 >= 0);
        CHECK(r[step].framesTo150 <= r[step - 1].framesTo150);
    }
    CHECK(r[kTouchSpeedSteps - 1].framesTo150 < r[0].framesTo150);
    for (const SwipeResult& x : r) CHECK(x.topSpeed < 9.0f);
}

TEST_CASE("fps counter: frames per second over each second, worst frame, dropped steps") {
    as3d_game::FpsCounter c;
    CHECK_FALSE(c.valid());
    // 59 frames of 1/60 s: not a full second yet.
    for (int i = 0; i < 59; ++i) c.frame(1.0 / 60.0);
    CHECK_FALSE(c.valid());
    c.frame(1.0 / 60.0 + 1e-9);
    REQUIRE(c.valid());
    CHECK(c.fps() == doctest::Approx(60.0).epsilon(0.001));
    CHECK(c.worstMs() == doctest::Approx(1000.0 / 60.0).epsilon(0.001));
    CHECK(c.dropped() == 0);
    // The next second: 27 frames of 1/30 s, then a stall of 110 ms that dropped 3 steps.
    for (int i = 0; i < 27; ++i) c.frame(1.0 / 30.0);
    CHECK(c.fps() == doctest::Approx(60.0).epsilon(0.001)); // shown values change once a second
    c.frame(0.11, 3);
    CHECK(c.fps() == doctest::Approx(28.0 / (27.0 / 30.0 + 0.11)).epsilon(0.001));
    CHECK(c.worstMs() == doctest::Approx(110.0));
    CHECK(c.dropped() == 3);
    // A long gap (a level load, the background) is not a frame: it starts a new second.
    for (int i = 0; i < 30; ++i) c.frame(1.0 / 60.0);
    c.frame(12.0);
    for (int i = 0; i < 59; ++i) c.frame(1.0 / 60.0);
    CHECK(c.worstMs() == doctest::Approx(110.0)); // unchanged: no full second since the gap
    c.frame(1.0 / 60.0 + 1e-9);
    CHECK(c.worstMs() < 17.0);
    CHECK(c.dropped() == 0);
}

TEST_CASE("fps counter: Show FPS round-trips in the profile") {
    Profile p;
    CHECK_FALSE(p.settings.showFps); // off by default
    p.settings.showFps = true;
    const std::vector<u8> bytes = serializeProfile(p);
    Profile q;
    REQUIRE(deserializeProfile(bytes.data(), bytes.size(), q));
    CHECK(q.settings.showFps);
}
