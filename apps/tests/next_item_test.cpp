// WP-53 (docs/spec/issues/141-next-item-preview.md): the touch overlay's "next" buttons show
// the item a press selects. The prediction is player_select.h, the same functions the player
// frame calls; these tests cover the rule, the simulation against the prediction, and the
// pixels of the buttons.
#include "doctest.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../game/touch_overlay.cpp"
#include "as3d/defs.h"
#include "as3d/gfx.h"
#include "as3d/image.h"
#include "as3d/input.h"
#include "as3d/player_select.h"
#include "as3d/vfs.h"
#include "as3d/world.h"
#include "test_data.h"
#include "ui_test_util.h"

using namespace as3d;

namespace {

// Reference: the loop as the player frame had it before the shared functions.
int refNext(const int* counts, int n, int cur) {
    for (int k = 1; k <= n; ++k) {
        int t = ((cur < 0 ? -1 : cur) + k + n) % n;
        if (counts[t] > 0) return t;
    }
    return cur;
}

} // namespace

TEST_CASE("next item: the rule over every combination of owned missiles") {
    for (int mask = 0; mask < 32; ++mask) {
        int m[5];
        for (int t = 0; t < 5; ++t) m[t] = (mask >> t & 1) ? 1 + t : 0;
        for (int cur = -1; cur < 5; ++cur) {
            const int got = nextMissileType(m, cur);
            CHECK(got == refNext(m, 5, cur));
            if (mask == 0) CHECK(got == cur); // nothing owned: unchanged
            if (mask != 0) CHECK(m[got] > 0); // otherwise always an owned type
            if (cur >= 0 && m[cur] > 0) {
                // Walking the cycle visits every owned type once before coming back.
                int seen = 1 << cur, c = cur;
                for (int i = 0; i < 5; ++i) {
                    c = nextMissileType(m, c);
                    if (c == cur) break;
                    seen |= 1 << c;
                }
                CHECK(c == cur);
                CHECK(seen == mask);
            }
        }
    }
}

TEST_CASE("next item: wrap-around, single and empty cases for missiles, power-ups and weapons") {
    int m[5] = {0, 3, 0, 0, 2};
    CHECK(nextMissileType(m, 1) == 4);
    CHECK(nextMissileType(m, 4) == 1); // wraps
    CHECK(nextMissileType(m, 0) == 1);
    CHECK(nextMissileType(m, -1) == 1);
    int one[5] = {0, 0, 5, 0, 0};
    CHECK(nextMissileType(one, 2) == 2); // one type: nothing changes
    CHECK(nextMissileType(one, 0) == 2); // current type used up: moves to the owned one
    int none[5] = {};
    CHECK(nextMissileType(none, 3) == 3);
    CHECK(nextMissileType(none, -1) == -1);

    int p[16] = {};
    p[1] = 2;
    p[15] = 1;
    CHECK(nextPowerupSlot(p, 1) == 15);
    CHECK(nextPowerupSlot(p, 15) == 1);
    CHECK(nextPowerupSlot(p, -1) == 1);
    int p0[16] = {};
    CHECK(nextPowerupSlot(p0, -1) == -1);

    int u[20] = {};
    u[0] = 1;
    CHECK(nextWeaponIndex(u, 0) == 0); // only the base weapon
    u[7] = 1;
    u[19] = 3;
    CHECK(nextWeaponIndex(u, 0) == 7);
    CHECK(nextWeaponIndex(u, 7) == 19);
    CHECK(nextWeaponIndex(u, 19) == 0);
    int u0[20] = {};
    CHECK(nextWeaponIndex(u0, 4) == 4);

    PlayerRecord pr;
    pr.upgrades[0] = 1;
    pr.upgrades[2] = 1;
    pr.weapon = 0.0f;
    CHECK(nextWeaponIndex(pr) == 2);
    pr.missiles[3] = 1;
    pr.currentMissile = 3;
    CHECK(nextMissileType(pr) == 3);
    pr.powerups[0] = 1;
    CHECK(nextPowerupSlot(pr) == 0);
}

TEST_CASE("next item: the simulation selects what the prediction says, in a series of states") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    Vfs vfs;
    vfs.mount(makeDirSource(testdata::extractedDir()));
    DefDatabase db;
    REQUIRE(db.load(vfs));
    World w;
    WorldConfig cfg;
    cfg.godMode = true;
    w.init(vfs, db, cfg);
    std::string err;
    REQUIRE_MESSAGE(w.loadLevel("1", &err), err);
    PlayerInput in;
    in.confirm = true;
    for (int f = 0; f < 30; ++f) w.step(in);

    struct State {
        int missiles[5];
        int cm;
        int powerups[16];
        int cp;
        int upgrades[20];
        int weapon;
    };
    const State states[] = {
        {{0, 0, 0, 0, 0}, -1, {}, -1, {1}, 0},
        {{4, 0, 0, 0, 0}, 0, {0, 2}, 1, {1}, 0},
        {{1, 2, 3, 4, 5}, 0, {1, 1, 1, 1}, 0, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, 0},
        {{0, 2, 0, 0, 7}, 4, {0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, 15, {1, 0, 0, 0, 0, 0, 0, 2, 0, 0}, 7},
        {{0, 0, 3, 0, 0}, -1, {}, 3, {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5}, 19},
        {{0, 0, 0, 9, 0}, 0, {0, 0, 5}, -1, {0, 0, 1}, 0},
    };
    for (const State& s : states) {
        PlayerRecord& pr = w.player(0);
        std::memcpy(pr.missiles, s.missiles, sizeof pr.missiles);
        std::memcpy(pr.powerups, s.powerups, sizeof pr.powerups);
        std::memcpy(pr.upgrades, s.upgrades, sizeof pr.upgrades);
        pr.currentMissile = s.cm;
        pr.currentPowerup = s.cp;
        pr.weapon = static_cast<float>(s.weapon);
        for (int press = 0; press < 6; ++press) { // several presses: walks the cycle and wraps
            const int wantM = nextMissileType(w.player(0));
            const int wantP = nextPowerupSlot(w.player(0));
            const int wantW = nextWeaponIndex(w.player(0));
            in.action[0] = ACT_NEXT_MISSILE | ACT_NEXT_POWERUP | ACT_NEXT_WEAPON;
            w.step(in);
            CHECK(w.player(0).currentMissile == wantM);
            CHECK(w.player(0).currentPowerup == wantP);
            CHECK(static_cast<int>(w.player(0).weapon) == wantW);
            in.action[0] = 0;
            w.step(in);
        }
    }
}

namespace {

ui::HudPlayer richPlayer() {
    ui::HudPlayer p;
    p.weapon = 1;
    p.upgrades[0] = 1;
    p.upgrades[1] = 1;
    p.upgrades[2] = 1;
    p.upgrades[4] = 1;
    p.missiles[0] = 8;
    p.missiles[1] = 5;
    p.missiles[3] = 12;
    p.missileSelected = 0;
    p.powerups[0] = 3;
    p.powerups[1] = 2;
    p.powerups[3] = 1;
    p.powerupSelected = 1;
    return p;
}

struct Shot {
    Image img;
    TouchLayout layout;
};

// Renders the overlay of `p` over a flat ground colour into a 2400x1080 target.
bool renderOverlay(const ui::UiAssets& assets, ui::Renderer2D& r, const ui::HudPlayer* p, bool mode4x3, bool left,
                   bool pressed, Shot& out) {
    RenderTarget target;
    if (!target.create(2400, 1080, 0)) return false;
    target.bind();
    clear({0.16f, 0.24f, 0.16f, 1}, true);
    TouchMapper touch;
    TouchLayoutOptions o;
    o.dpi = 400;
    o.leftHanded = left;
    o.screen4x3 = mode4x3;
    touch.setScreen(2400, 1080, o);
    if (pressed) {
        const TouchLayout& L = touch.layout();
        int id = 1;
        for (TouchButton b : {TouchButton::Missile, TouchButton::NextPowerUp}) {
            const TouchCircle& c = L.circles[static_cast<int>(b)];
            touch.touchEvent(id++, TouchPhase::Down, c.x / 2400.0f, c.y / 1080.0f);
        }
    }
    as3d_game::TouchOverlayState st;
    st.assets = &assets;
    st.player = p;
    r.begin(2400, 1080);
    as3d_game::drawTouchControls(r, touch, st);
    r.flush();
    out.layout = touch.layout();
    return target.readPixels(out.img);
}

long diffIn(const Image& a, const Image& b, const TouchCircle& c) {
    long n = 0;
    const int x0 = static_cast<int>(c.x - c.r), x1 = static_cast<int>(c.x + c.r);
    const int y0 = static_cast<int>(c.y - c.r), y1 = static_cast<int>(c.y + c.r);
    for (int y = std::max(y0, 0); y < std::min(y1, a.height); ++y)
        for (int x = std::max(x0, 0); x < std::min(x1, a.width); ++x) {
            const size_t i = (static_cast<size_t>(y) * a.width + x) * 4;
            n += std::abs(a.rgba[i] - b.rgba[i]) + std::abs(a.rgba[i + 1] - b.rgba[i + 1]) +
                     std::abs(a.rgba[i + 2] - b.rgba[i + 2]) >
                 30;
        }
    return n;
}

} // namespace

TEST_CASE("next item: the next buttons show different pixels when the next item differs; disabled is dim") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    AS3D_REQUIRE_GLES();
    Vfs vfs;
    REQUIRE(uitest::mountPaks(vfs));
    ui::UiAssets assets;
    std::string err;
    REQUIRE_MESSAGE(assets.load(vfs, &err), err);
    ui::Renderer2D r;
    REQUIRE_MESSAGE(r.init(&err), err);

    const ui::HudPlayer base = richPlayer();
    Shot a;
    REQUIRE(renderOverlay(assets, r, &base, false, false, false, a));
    const auto idx = [](TouchButton b) { return static_cast<int>(b); };

    // Next missile: current 0, the next owned is 1; with the current at 1 it is 3.
    ui::HudPlayer m = base;
    m.missileSelected = 1;
    Shot b;
    REQUIRE(renderOverlay(assets, r, &m, false, false, false, b));
    CHECK(diffIn(a.img, b.img, a.layout.circles[idx(TouchButton::NextMissile)]) > 300);
    // A different count of the next item changes the button as well.
    ui::HudPlayer c = base;
    c.missiles[1] = 9;
    REQUIRE(renderOverlay(assets, r, &c, false, false, false, b));
    CHECK(diffIn(a.img, b.img, a.layout.circles[idx(TouchButton::NextMissile)]) > 30);
    // Next power-up.
    ui::HudPlayer k = base;
    k.powerupSelected = 0;
    REQUIRE(renderOverlay(assets, r, &k, false, false, false, b));
    CHECK(diffIn(a.img, b.img, a.layout.circles[idx(TouchButton::NextPowerUp)]) > 300);
    // Next weapon: current 1 -> 2; current 2 -> 4.
    ui::HudPlayer wpn = base;
    wpn.weapon = 2;
    REQUIRE(renderOverlay(assets, r, &wpn, false, false, false, b));
    CHECK(diffIn(a.img, b.img, a.layout.circles[idx(TouchButton::NextWeapon)]) > 300);
    // The next-missile button does not depend on the weapon.
    CHECK(diffIn(a.img, b.img, a.layout.circles[idx(TouchButton::NextMissile)]) == 0);

    // Disabled (one missile type owned): only a dim arrow, clearly fewer bright pixels.
    ui::HudPlayer one = base;
    for (int t = 1; t < 5; ++t) one.missiles[t] = 0;
    one.missileSelected = 0;
    Shot d;
    REQUIRE(renderOverlay(assets, r, &one, false, false, false, d));
    const TouchCircle& nm = a.layout.circles[idx(TouchButton::NextMissile)];
    const auto bright = [&](const Image& img) {
        long n = 0;
        for (int y = static_cast<int>(nm.y - nm.r * 0.8f); y < static_cast<int>(nm.y + nm.r * 0.8f); ++y)
            for (int x = static_cast<int>(nm.x - nm.r * 0.8f); x < static_cast<int>(nm.x + nm.r * 0.8f); ++x) {
                const u8* q = &img.rgba[(static_cast<size_t>(y) * img.width + x) * 4];
                n += q[0] + q[1] + q[2] > 300;
            }
        return n;
    };
    CHECK(bright(d.img) * 2 < bright(a.img));
    CHECK(diffIn(a.img, d.img, nm) > 300);

    // Screenshots for a look (never committed): AS3D_SHOT_DIR=<dir>.
    if (const char* dir = std::getenv("AS3D_SHOT_DIR")) {
        for (int mode = 0; mode < 2; ++mode)
            for (int left = 0; left < 2; ++left)
                for (int pressed = 0; pressed < 2; ++pressed) {
                    Shot s;
                    REQUIRE(renderOverlay(assets, r, &base, mode != 0, left != 0, pressed != 0, s));
                    const std::string name = std::string(dir) + "/next_" + (mode ? "4x3" : "wide") +
                                             (left ? "_left" : "_right") + (pressed ? "_pressed" : "_idle") + ".png";
                    CHECK(writePng(name.c_str(), s.img));
                }
        ui::HudPlayer dis = base;
        for (int t = 1; t < 5; ++t) dis.missiles[t] = 0;
        dis.missileSelected = 0;
        for (int i = 0; i < 16; ++i) dis.powerups[i] = 0;
        dis.powerups[0] = 2;
        dis.powerupSelected = 0;
        for (int i = 1; i < 20; ++i) dis.upgrades[i] = 0;
        dis.weapon = 0;
        Shot s;
        REQUIRE(renderOverlay(assets, r, &dis, false, false, false, s));
        CHECK(writePng((std::string(dir) + "/next_wide_right_disabled.png").c_str(), s.img));
    }
}
