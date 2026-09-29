// Touch controls (WP-48): the TouchMapper turns finger events into the same FrameInput as
// the keyboard mapper. Relative drag steering, multi-touch with buttons, fingers lifted and
// replaced, auto-fire, pause, and the button layout at 4:3, 16:9 and 20:9 with cutouts.
// CPU only; the last test steers the real helicopter of mission 1 (skipped without data).
#include "doctest.h"

#include <algorithm>
#include <cmath>

#include "as3d/defs.h"
#include "as3d/input.h"
#include "as3d/vfs.h"
#include "as3d/world.h"
#include "test_data.h"

using namespace as3d;

namespace {

float cx(const TouchRect& r) { return r.x + r.w * 0.5f; }
float cy(const TouchRect& r) { return r.y + r.h * 0.5f; }

[[maybe_unused]] bool overlaps(const TouchRect& a, const TouchRect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

bool insideScreen(const TouchRect& r) {
    return r.x >= 0 && r.y >= 0 && r.x + r.w <= 1.0001f && r.y + r.h <= 1.0001f;
}

const u32 kDirs = ACT_FORWARD | ACT_BACKWARD | ACT_LEFT | ACT_RIGHT;

// A toy helicopter in virtual screen pixels, driven by the direction bits like the player
// script's model (accelerate while held, decay otherwise, clamped speed).
struct ToyHeli {
    float x = 400, y = 450, vx = 0, vy = 0;
    void step(u32 bits) {
        auto axis = [](float& v, bool plus, bool minus) {
            if (plus) v += 0.45f;
            else if (minus) v -= 0.45f;
            else if (v > 0) v = std::max(0.0f, v - 0.15f);
            else v = std::min(0.0f, v + 0.15f);
            v = std::min(std::max(v, -4.0f), 4.0f);
        };
        axis(vx, bits & ACT_RIGHT, bits & ACT_LEFT);
        axis(vy, bits & ACT_BACKWARD, bits & ACT_FORWARD);
        x += vx;
        y += vy;
    }
};

// Runs frames: the mapper sees the heli's position, the heli follows the bits.
u32 runFrames(TouchMapper& t, ToyHeli& h, int frames) {
    u32 last = 0;
    for (int i = 0; i < frames; ++i) {
        t.setPlayerScreen(true, h.x, h.y);
        FrameInput in = t.takeFrame();
        last = in.held[0];
        h.step(last);
    }
    return last;
}

void tap(TouchMapper& t, long long id, const TouchRect& r) {
    t.touchEvent(id, TouchPhase::Down, cx(r), cy(r));
}

} // namespace

TEST_CASE("touch layout: buttons at 4:3 sit inside the play-field, translucent") {
    TouchLayout L = computeTouchLayout(800, 600);
    CHECK_FALSE(L.outside);
    CHECK(L.alpha < 1.0f);
    for (int b = 0; b < kTouchButtonCount; ++b) {
        CHECK(insideScreen(L.buttons[b]));
        CHECK(L.hitTest(cx(L.buttons[b]), cy(L.buttons[b])) == b);
        for (int c = b + 1; c < kTouchButtonCount; ++c)
            CHECK(std::hypot(L.circles[b].x - L.circles[c].x, L.circles[b].y - L.circles[c].y) >=
                  L.circles[b].r + L.circles[c].r);
    }
    // Clear of the HUD's top bars (health at the left, score at the right, virtual y < 31)
    // and of the power-up column (x >= 720, y < 200): only the pause button is at the top,
    // centred.
    const TouchRect& p = L.buttons[static_cast<int>(TouchButton::Pause)];
    CHECK(std::fabs(cx(p) - 0.5f) < 0.01f);
    CHECK(p.x > 200.0f / 800.0f);
    CHECK(p.x + p.w < 600.0f / 800.0f);
    for (int b = 0; b < kTouchButtonCount; ++b) {
        if (b == static_cast<int>(TouchButton::Pause)) continue;
        CHECK(L.buttons[b].y > 200.0f / 600.0f);
    }
}

TEST_CASE("touch layout: 16:9 and 20:9 put the buttons in the side bars") {
    for (int w : {1920, 2400}) {
        TouchLayout L = computeTouchLayout(w, 1080);
        CAPTURE(w);
        CHECK(L.outside);
        const TouchRect& pf = L.playField;
        CHECK(std::fabs(pf.w * w - 1440.0f) < 1.0f);
        for (int b = 0; b < kTouchButtonCount; ++b) {
            CAPTURE(b);
            const TouchRect& r = L.buttons[b];
            CHECK(insideScreen(r));
            // Every button is outside the 4:3 field: the action cluster on the right, pause
            // on the left.
            bool right = r.x >= pf.x + pf.w - 1e-4f;
            bool left = r.x + r.w <= pf.x + 1e-4f;
            CHECK((right || left));
            if (b == static_cast<int>(TouchButton::Pause)) CHECK(left);
            else CHECK(right);
        }
        // Missile is the lowest (thumb) button, power-up above it.
        const TouchRect& m = L.buttons[static_cast<int>(TouchButton::Missile)];
        const TouchRect& pu = L.buttons[static_cast<int>(TouchButton::PowerUp)];
        CHECK(m.y > pu.y);
        CHECK(m.y + m.h <= 1.0f);
    }
}

TEST_CASE("touch layout: display cutouts keep controls out of the insets") {
    // 20:9 phone in landscape with a hole-punch camera on the left edge.
    SafeInsets left;
    left.left = 130;
    TouchLayout L = computeTouchLayout(2400, 1080, left);
    CHECK(L.outside);
    for (int b = 0; b < kTouchButtonCount; ++b) CHECK(L.buttons[b].x * 2400.0f >= 130.0f);
    // Rotated the other way: the cutout is on the right, where the action cluster is.
    SafeInsets right;
    right.right = 130;
    L = computeTouchLayout(2400, 1080, right);
    CHECK(L.outside);
    for (int b = 0; b < kTouchButtonCount; ++b) CHECK((L.buttons[b].x + L.buttons[b].w) * 2400.0f <= 2400.0f - 130.0f);
    // A 4:3 tablet with a top inset: the pause button moves down below it.
    SafeInsets top;
    top.top = 60;
    L = computeTouchLayout(1600, 1200, top);
    CHECK(L.buttons[static_cast<int>(TouchButton::Pause)].y * 1200.0f >= 60.0f);
}

TEST_CASE("touch layout: button hit areas at 4:3, 16:9 and 20:9 map to their actions") {
    const int sizes[3][2] = {{1024, 768}, {1920, 1080}, {2400, 1080}};
    const u32 bits[kTouchButtonCount] = {ACT_MISSILE, ACT_POWERUP, ACT_NEXT_MISSILE, ACT_NEXT_WEAPON,
                                         ACT_NEXT_POWERUP, 0};
    for (const auto& sz : sizes) {
        CAPTURE(sz[0]);
        const float fw = static_cast<float>(sz[0]), fh = static_cast<float>(sz[1]);
        for (int b = 0; b < kTouchButtonCount; ++b) {
            CAPTURE(b);
            TouchMapper probe;
            probe.setScreen(sz[0], sz[1]);
            const TouchCircle& h = probe.layout().hit[b];
            // Near the edge of the round hit area, on a side away from the other buttons.
            int found = 0;
            for (int k = 0; k < 16; ++k) {
                const float a = 6.2831853f * static_cast<float>(k) / 16.0f;
                const float nx = (h.x + std::cos(a) * h.r * 0.97f) / fw, ny = (h.y + std::sin(a) * h.r * 0.97f) / fh;
                if (nx < 0 || nx > 1 || ny < 0 || ny > 1) continue;
                bool other = false;
                for (int c = 0; c < kTouchButtonCount; ++c)
                    if (c != b && probe.layout().hit[c].contains(nx * fw, ny * fh)) other = true;
                if (other) continue;
                ++found;
                TouchMapper t;
                t.setScreen(sz[0], sz[1]);
                t.touchEvent(1, TouchPhase::Down, nx, ny);
                CHECK(t.buttonHeld(static_cast<TouchButton>(b)));
                FrameInput in = t.takeFrame();
                if (b == static_cast<int>(TouchButton::Pause)) {
                    CHECK(in.pausePressed);
                    CHECK(in.held[0] == 0u); // the pause button does not fire
                } else {
                    CHECK((in.held[0] & bits[b]) == bits[b]);
                    CHECK((in.held[0] & ACT_FIRE) != 0u);
                    CHECK_FALSE(in.pausePressed);
                }
                CHECK((in.held[0] & kDirs) == 0u); // a button press never steers
                // Just outside the hit area, the same direction is not this button.
                TouchMapper u;
                u.setScreen(sz[0], sz[1]);
                const float ox = (h.x + std::cos(a) * (h.r + 3.0f)) / fw, oy = (h.y + std::sin(a) * (h.r + 3.0f)) / fh;
                u.touchEvent(2, TouchPhase::Down, ox, oy);
                CHECK_FALSE(u.buttonHeld(static_cast<TouchButton>(b)));
            }
            CHECK(found >= 3);
        }
    }
}

TEST_CASE("touch: relative drag moves the helicopter by the finger's displacement") {
    TouchMapper t;
    t.setScreen(800, 600); // 1 normalised unit = 800 x 600 virtual pixels
    ToyHeli h;
    h.x = 400;
    h.y = 450;
    // The finger goes down far from the helicopter: nothing moves (no jump to the finger).
    t.touchEvent(7, TouchPhase::Down, 0.1f, 0.2f);
    u32 bits = runFrames(t, h, 30);
    CHECK((bits & kDirs) == 0u);
    CHECK(std::fabs(h.x - 400) < 0.01f);
    CHECK(std::fabs(h.y - 450) < 0.01f);
    CHECK(t.targetValid());
    CHECK(t.targetX() == doctest::Approx(400));
    // Drag 50 virtual pixels right: the target moves 50 x gain.
    t.touchEvent(7, TouchPhase::Move, 0.1f + 50.0f / 800.0f, 0.2f);
    t.setPlayerScreen(true, h.x, h.y);
    FrameInput in = t.takeFrame();
    CHECK((in.held[0] & ACT_RIGHT) != 0u);
    CHECK((in.held[0] & ACT_LEFT) == 0u);
    CHECK(t.targetX() == doctest::Approx(400 + 50 * t.settings().gain));
    h.step(in.held[0]);
    // It settles near the target without oscillating.
    bits = runFrames(t, h, 240);
    CHECK((bits & kDirs) == 0u);
    CHECK(std::fabs(h.x - t.targetX()) <= t.settings().deadZone + 5.0f);
    CHECK(std::fabs(h.vx) < 0.01f);
    // Up the screen is forward.
    float y0 = h.y;
    t.touchEvent(7, TouchPhase::Move, 0.1f + 50.0f / 800.0f, 0.2f - 40.0f / 600.0f);
    t.setPlayerScreen(true, h.x, h.y);
    in = t.takeFrame();
    CHECK((in.held[0] & ACT_FORWARD) != 0u);
    h.step(in.held[0]);
    runFrames(t, h, 240);
    CHECK(h.y < y0 - 30);
    // A tiny finger movement within the dead zone does not move it.
    float x1 = h.x;
    t.touchEvent(7, TouchPhase::Move, 0.1f + 55.0f / 800.0f, 0.2f - 40.0f / 600.0f);
    bits = runFrames(t, h, 60);
    CHECK((bits & kDirs) == 0u);
    CHECK(std::fabs(h.x - x1) < 1.0f);
}

TEST_CASE("touch: the target stays close to a helicopter that cannot follow") {
    TouchMapper t;
    t.setScreen(800, 600);
    t.touchEvent(1, TouchPhase::Down, 0.5f, 0.5f);
    t.setPlayerScreen(true, 780, 300); // against the right edge, not moving
    t.takeFrame();
    t.touchEvent(1, TouchPhase::Move, 0.95f, 0.5f); // far right
    t.setPlayerScreen(true, 780, 300);
    t.takeFrame();
    CHECK(t.targetX() <= 800.0f);
    CHECK(t.targetX() - 780.0f <= t.settings().maxLead);
    // Reversing the finger by a little steers left at once.
    t.touchEvent(1, TouchPhase::Move, 0.85f, 0.5f);
    t.setPlayerScreen(true, 780, 300);
    FrameInput in = t.takeFrame();
    CHECK((in.held[0] & ACT_LEFT) != 0u);
}

TEST_CASE("touch: multi-touch presses buttons while dragging") {
    TouchMapper t;
    t.setScreen(1920, 1080);
    const TouchLayout& L = t.layout();
    ToyHeli h;
    t.touchEvent(10, TouchPhase::Down, 0.3f, 0.6f); // drag finger in the field
    runFrames(t, h, 2);
    t.touchEvent(10, TouchPhase::Move, 0.3f + 0.05f, 0.6f);
    tap(t, 11, L.buttons[static_cast<int>(TouchButton::Missile)]);
    t.setPlayerScreen(true, h.x, h.y);
    FrameInput in = t.takeFrame();
    CHECK((in.held[0] & ACT_MISSILE) != 0u);
    CHECK((in.held[0] & ACT_RIGHT) != 0u);
    CHECK((in.held[0] & ACT_FIRE) != 0u);
    CHECK(t.dragging());
    // Moving the button finger does not steer.
    float tx = t.targetX();
    t.touchEvent(11, TouchPhase::Move, 0.99f, 0.99f);
    t.setPlayerScreen(true, h.x, h.y);
    t.takeFrame();
    CHECK(t.targetX() == doctest::Approx(tx));
    // A third finger on power-up, then the button fingers lift; the drag goes on.
    tap(t, 12, L.buttons[static_cast<int>(TouchButton::PowerUp)]);
    t.setPlayerScreen(true, h.x, h.y);
    in = t.takeFrame();
    CHECK((in.held[0] & (ACT_MISSILE | ACT_POWERUP)) == (ACT_MISSILE | ACT_POWERUP));
    t.touchEvent(11, TouchPhase::Up, 0.99f, 0.99f);
    t.touchEvent(12, TouchPhase::Up, 0.9f, 0.5f);
    t.setPlayerScreen(true, h.x, h.y);
    in = t.takeFrame();
    CHECK((in.held[0] & (ACT_MISSILE | ACT_POWERUP)) == 0u);
    CHECK((in.held[0] & ACT_FIRE) != 0u);
    CHECK(t.dragging());
    // The drag finger lifting while a button is still held: no steering, no drag.
    tap(t, 13, L.buttons[static_cast<int>(TouchButton::NextWeapon)]);
    t.touchEvent(10, TouchPhase::Up, 0.35f, 0.6f);
    t.setPlayerScreen(true, h.x, h.y);
    in = t.takeFrame();
    CHECK_FALSE(t.dragging());
    CHECK((in.held[0] & kDirs) == 0u);
    CHECK((in.held[0] & ACT_NEXT_WEAPON) != 0u);
}

TEST_CASE("touch: a finger lifted and replaced starts again from the helicopter") {
    TouchMapper t;
    t.setScreen(800, 600);
    ToyHeli h;
    t.touchEvent(1, TouchPhase::Down, 0.5f, 0.5f);
    runFrames(t, h, 1);
    t.touchEvent(1, TouchPhase::Move, 0.6f, 0.5f); // 80 px right
    runFrames(t, h, 10);
    CHECK(h.vx > 0);
    t.touchEvent(1, TouchPhase::Up, 0.6f, 0.5f);
    t.setPlayerScreen(true, h.x, h.y);
    FrameInput in = t.takeFrame();
    CHECK((in.held[0] & kDirs) == 0u);
    CHECK((in.held[0] & ACT_FIRE) == 0u);
    CHECK_FALSE(t.dragging());
    runFrames(t, h, 60); // coasts to a stop
    // A new finger (new id) somewhere else: the target is the helicopter, no jump.
    t.touchEvent(2, TouchPhase::Down, 0.9f, 0.1f);
    t.setPlayerScreen(true, h.x, h.y);
    in = t.takeFrame();
    CHECK((in.held[0] & kDirs) == 0u);
    CHECK(t.targetX() == doctest::Approx(h.x));
    CHECK(t.targetY() == doctest::Approx(h.y));
    t.touchEvent(2, TouchPhase::Move, 0.8f, 0.1f);
    t.setPlayerScreen(true, h.x, h.y);
    in = t.takeFrame();
    CHECK((in.held[0] & ACT_LEFT) != 0u);
    // The same id going down twice (lost up event) is treated as a move, not a new finger.
    t.touchEvent(2, TouchPhase::Down, 0.8f, 0.1f);
    CHECK(t.fingersDown() == 1);
    // While the helicopter is gone (respawn), no steering; it restarts from where it appears.
    t.setPlayerScreen(false, 0, 0);
    in = t.takeFrame();
    CHECK((in.held[0] & kDirs) == 0u);
    t.setPlayerScreen(true, 100, 500);
    t.takeFrame();
    CHECK(t.targetX() == doctest::Approx(100));
}

TEST_CASE("touch: auto-fire while a finger is down, including taps shorter than a frame") {
    TouchMapper t;
    t.setScreen(1280, 720);
    FrameInput in = t.takeFrame();
    CHECK(in.held[0] == 0u);
    t.touchEvent(1, TouchPhase::Down, 0.5f, 0.5f);
    for (int i = 0; i < 3; ++i) {
        t.setPlayerScreen(true, 400, 400);
        in = t.takeFrame();
        CHECK((in.held[0] & ACT_FIRE) != 0u);
    }
    CHECK(in.confirm == false); // the tap's confirm lasted one frame
    t.touchEvent(1, TouchPhase::Up, 0.5f, 0.5f);
    in = t.takeFrame();
    CHECK((in.held[0] & ACT_FIRE) == 0u);
    // Down and up between two frames: fire (and a hint's OK) still reach one frame.
    t.touchEvent(2, TouchPhase::Down, 0.4f, 0.4f);
    t.touchEvent(2, TouchPhase::Up, 0.4f, 0.4f);
    in = t.takeFrame();
    CHECK((in.held[0] & ACT_FIRE) != 0u);
    CHECK(in.confirm);
    in = t.takeFrame();
    CHECK(in.held[0] == 0u);
    CHECK_FALSE(in.confirm);
    // Auto-fire can be switched off.
    t.settings().autoFire = false;
    t.touchEvent(3, TouchPhase::Down, 0.4f, 0.4f);
    in = t.takeFrame();
    CHECK((in.held[0] & ACT_FIRE) == 0u);
}

TEST_CASE("touch: pause button, and a tap continues a paused game") {
    TouchMapper t;
    t.setScreen(1920, 1080);
    const TouchLayout& L = t.layout();
    tap(t, 1, L.buttons[static_cast<int>(TouchButton::Pause)]);
    FrameInput in = t.takeFrame();
    CHECK(in.pausePressed);
    in = t.takeFrame();
    CHECK_FALSE(in.pausePressed); // an edge
    t.touchEvent(1, TouchPhase::Up, 0, 0);
    // Paused (by the button, back or the app lifecycle): a tap anywhere only unpauses.
    t.setPaused(true);
    t.touchEvent(2, TouchPhase::Down, 0.5f, 0.5f);
    in = t.takeFrame();
    CHECK(in.pausePressed);
    CHECK(in.held[0] == 0u);
    t.setPaused(false);
    // The finger that continued the game has no effect until it is lifted.
    t.touchEvent(2, TouchPhase::Move, 0.7f, 0.5f);
    t.setPlayerScreen(true, 400, 400);
    in = t.takeFrame();
    CHECK(in.held[0] == 0u);
    CHECK_FALSE(t.dragging());
    t.touchEvent(2, TouchPhase::Up, 0.7f, 0.5f);
    t.touchEvent(3, TouchPhase::Down, 0.5f, 0.5f);
    in = t.takeFrame();
    CHECK((in.held[0] & ACT_FIRE) != 0u);
    // releaseAll (focus lost, background) lifts everything.
    t.releaseAll();
    in = t.takeFrame();
    CHECK(in.held[0] == 0u);
    CHECK(t.fingersDown() == 0);
}

TEST_CASE("touch: merged with the keyboard mapper's frame") {
    FrameInput a, b;
    a.held[0] = ACT_LEFT;
    b.held[0] = ACT_FIRE | ACT_MISSILE;
    b.confirm = true;
    a.pausePressed = true;
    FrameInput m = mergeFrameInput(a, b);
    CHECK(m.held[0] == (ACT_LEFT | ACT_FIRE | ACT_MISSILE));
    CHECK(m.held[1] == 0u);
    CHECK(m.confirm);
    CHECK(m.pausePressed);
}

namespace {
bool heliCentre(const World& w, float& x, float& y) {
    int pi = w.playerEntityIndex(0);
    if (pi < 0) return false;
    const ScreenRect& r = w.entity(pi).rect;
    if (!(r.max[0] > r.min[0])) return false;
    x = 0.5f * (r.min[0] + r.max[0]);
    y = 600.0f - 0.5f * (r.min[1] + r.max[1]);
    return true;
}
} // namespace

TEST_CASE("touch: a drag steers the real helicopter of mission 1 and it settles") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    Vfs vfs;
    vfs.mount(makeDirSource(testdata::extractedDir()));
    DefDatabase db;
    REQUIRE(db.load(vfs));
    World world;
    world.init(vfs, db, WorldConfig());
    std::string err;
    REQUIRE(world.loadLevel("1", &err));
    TouchMapper t;
    t.setScreen(800, 600);
    auto step = [&]() {
        float x = 0, y = 0;
        bool ok = heliCentre(world, x, y);
        t.setPlayerScreen(ok, x, y);
        FrameInput in = t.takeFrame();
        in.confirm = true; // close the tutorial's hint boxes
        world.step(in.toPlayerInput());
        return in.held[0];
    };
    // Past the fly-in.
    for (int f = 0; f < 420; ++f) step();
    float x0 = 0, y0 = 0;
    REQUIRE(heliCentre(world, x0, y0));
    t.touchEvent(1, TouchPhase::Down, 0.5f, 0.7f);
    for (int f = 0; f < 30; ++f) step();
    float xs = 0, ys = 0;
    REQUIRE(heliCentre(world, xs, ys));
    CHECK(std::fabs(xs - x0) < 3.0f); // touching does not move it
    // 60 virtual pixels right: the helicopter goes 60 x gain = 90 pixels right.
    t.touchEvent(1, TouchPhase::Move, 0.5f + 60.0f / 800.0f, 0.7f);
    int flips = 0;
    u32 lastDir = 0;
    u32 bits = 0;
    for (int f = 0; f < 240; ++f) {
        bits = step();
        u32 dir = bits & (ACT_LEFT | ACT_RIGHT);
        if (dir && lastDir && dir != lastDir) ++flips;
        if (dir) lastDir = dir;
    }
    float x1 = 0, y1 = 0;
    REQUIRE(heliCentre(world, x1, y1));
    MESSAGE("helicopter x ", xs, " -> ", x1, " target ", t.targetX(), ", direction flips ", flips);
    CHECK(x1 - xs > 60.0f);
    CHECK(std::fabs(x1 - t.targetX()) < 35.0f);
    CHECK(flips <= 2);
    CHECK((bits & (ACT_LEFT | ACT_RIGHT)) == 0u);
    // Up the screen by 60: forward.
    t.touchEvent(1, TouchPhase::Move, 0.5f + 60.0f / 800.0f, 0.7f - 40.0f / 600.0f);
    for (int f = 0; f < 240; ++f) step();
    float x2 = 0, y2 = 0;
    REQUIRE(heliCentre(world, x2, y2));
    MESSAGE("helicopter y ", y1, " -> ", y2, " target ", t.targetY());
    CHECK(y1 - y2 > 40.0f);
    CHECK(std::fabs(y2 - t.targetY()) < 35.0f);
}
