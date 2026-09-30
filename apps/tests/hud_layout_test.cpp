// HUD layouts as per-game data (as3d/hud_layout.h): the first game's layout reproduces the
// drawing code it replaced (kept below as the reference, quad for quad), every rectangle lies
// inside its atlas, the AirStrike 2 elements land where as2/frontend.md 4 puts them, and
// headless renders of the AirStrike 2 HUD and hint panel light the right places. The games are
// selected explicitly (gameProfile + locateGameData); tests skip loudly without a game's data
// or a GLES context.
#include "doctest.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "as3d/game_data.h"
#include "as3d/gfx.h"
#include "as3d/hud_layout.h"
#include "as3d/image.h"
#include "as3d/ui.h"
#include "as3d/vfs.h"
#include "test_data.h"
#include "ui_test_util.h"

using namespace as3d;
using namespace as3d::ui;

namespace {

// ---------------------------------------------------------------------------
// The first game's HUD drawing code as it was before the layouts (engine/src/ui/hud.cpp at
// b2d0fda), kept as the expectation.
// ---------------------------------------------------------------------------
namespace old {

const Color kFrameGrey = grey(0x80 / 255.0f);
const Color kLifeGrey = grey(0xA0 / 255.0f);
const Color kScoreColor{0.753f, 0.188f, 0.0f, 1};
const Color kCountSelected{0.816f, 0.251f, 0.0f, 1};
const Color kCountOther{0.502f, 0.031f, 0.0f, 1};
constexpr SpecUv kBarFrame{0.0f, 0.8359f, 0.7031f, 1.0f};
constexpr SpecUv kBoxFrame{0.0f, 0.375f, 0.2734f, 0.6719f};
constexpr float kFillS0 = 0.0078f, kFillT0 = 0.6719f, kFillT1 = 0.8359f;

const SpecUv kWeapons[10] = {
    {0.0f, 0.727f, 0.258f, 1.0f},     {0.258f, 0.727f, 0.516f, 1.0f},   {0.516f, 0.727f, 0.774f, 1.0f},
    {0.774f, 0.727f, 0.998f, 1.0f},   {0.0f, 0.18f, 0.258f, 0.453f},    {0.516f, 0.453f, 0.774f, 0.727f},
    {0.774f, 0.453f, 0.998f, 0.727f}, {0.0f, 0.453f, 0.258f, 0.727f},   {0.258f, 0.453f, 0.516f, 0.727f},
    {0.258f, 0.18f, 0.516f, 0.453f},
};
const SpecUv kMissiles[5] = {
    {0.0f, 0.727f, 0.258f, 1.0f},   {0.516f, 0.727f, 0.774f, 1.0f}, {0.258f, 0.727f, 0.516f, 1.0f},
    {0.774f, 0.727f, 0.998f, 1.0f}, {0.0f, 0.501f, 0.258f, 0.727f},
};
const SpecUv kPowerups[4] = {
    {0.0f, 0.453f, 0.258f, 0.727f},
    {0.774f, 0.727f, 0.998f, 1.0f},
    {0.258f, 0.727f, 0.516f, 1.0f},
    {0.516f, 0.727f, 0.774f, 1.0f},
};
SpecUv weaponIconUv(int w) { return w >= 0 && w < 10 ? kWeapons[w] : SpecUv{}; }

int ftol(float v) { return static_cast<int>(v); }

struct Hud {
    Renderer2D& r;
    const UiAssets& a;
    void piece(const Texture2D& tex, float x, float y, float w, float h, SpecUv uv, bool mirror, Color c, Blend b) const {
        if (!tex.valid()) return;
        if (mirror) std::swap(uv.s0, uv.s1);
        r.quadSpec(x, y, w, h, uv.s0, uv.t0, uv.s1, uv.t1, &tex, c, b);
    }
    void barFrame(float x, float y, bool mirror) const { piece(a.mainbar, x, y, 180, 21, kBarFrame, mirror, kFrameGrey, Blend::Add); }
    void boxFrame(float x, float y, bool mirror) const { piece(a.mainbar, x, y, 70, 39, kBoxFrame, mirror, kFrameGrey, Blend::Add); }
    void fill(float y, float health, bool fromRight) const {
        const float f = std::clamp(health / kFullHealth, 0.0f, 1.0f);
        if (f <= 0) return;
        const float w = 172.0f * f;
        SpecUv uv{kFillS0, kFillT0, f * 174.0f / 256.0f, kFillT1};
        piece(a.mainbar, fromRight ? 788.0f - w : 12.0f, y, w, 21, uv, fromRight, Color{}, Blend::Add);
    }
    void weapon(float boxX, float boxY, bool mirrorBox, int w) const {
        boxFrame(boxX, boxY, mirrorBox);
        SpecUv uv = weaponIconUv(w);
        if (!uv.empty()) piece(a.weapons, boxX + 1, boxY + 3, 66, 35, uv, false, Color{}, Blend::Add);
    }
    void count(float right, float y, int n, bool selected) const {
        std::string s = std::to_string(n);
        float x = static_cast<float>(ftol(right - 10.5f * static_cast<float>(s.size())));
        drawNumber(r, a.uiFont(), x, y, s, 0.75f, selected ? kCountSelected : kCountOther);
    }
    void missiles(float frameX, float y0, bool mirror, float countRight, const HudPlayer& p) const {
        int n = 0;
        for (int t = 0; t < kMissileTypes; t++) {
            if (p.missiles[t] == 0) continue;
            const float y = y0 + 41.0f * static_cast<float>(n++);
            const bool sel = t == p.missileSelected;
            boxFrame(frameX, y, mirror);
            if (sel) boxFrame(frameX, y, mirror);
            piece(a.missiles, frameX + 1, y + 3, 66, 35, kMissiles[t], false, Color{}, Blend::Alpha);
            count(countRight, y + 3, p.missiles[t], sel);
        }
    }
    void powerups(float frameX, float frameY0, float iconX, bool mirror, float countRight, const HudPlayer& p) const {
        int n = 0;
        for (int k = 0; k < kPowerupSlots; k++) {
            if (p.powerups[k] == 0) continue;
            const float y = frameY0 + 41.0f * static_cast<float>(n++);
            const bool sel = k == p.powerupSelected;
            boxFrame(frameX, y, mirror);
            if (sel) boxFrame(frameX, y, mirror);
            if (k < 4) piece(a.items, iconX, y + 3, 66, 35, kPowerups[k], false, Color{}, k == 0 ? Blend::Add : Blend::Alpha);
            if (p.powerups[k] > 1) count(countRight, y + 3, p.powerups[k], sel);
        }
    }
    void lives(int n, bool fromRight) const {
        if (!a.life.valid()) return;
        for (int i = 0; i < std::min(n, 5); i++) {
            const float x = fromRight ? 753.0f - 32.0f * static_cast<float>(i) : 15.0f + 32.0f * static_cast<float>(i);
            r.quad(x, 555, 32, 32, 0, 0, 1, 1, &a.life, kLifeGrey, Blend::Add);
        }
    }
};

void drawOnePlayer(const Hud& h, const HudPlayer& p) {
    h.barFrame(10, 10, false);
    h.fill(10, p.health, false);
    h.weapon(10, 32, false, p.weapon);
    h.missiles(10, 73, false, 76, p);
    h.barFrame(610, 10, true);
    drawNumber(h.r, h.a.uiFont(), 630, 12, std::to_string(p.score), 1.0f, kScoreColor);
    h.powerups(720, 32, 721, true, 786, p);
    h.lives(p.lives, false);
}

void drawTwoPlayers(const Hud& h, const HudPlayer& p1, const HudPlayer& p2) {
    h.lives(p1.lives, false);
    h.barFrame(10, 10, false);
    h.fill(10, p1.health, false);
    h.barFrame(10, 32, false);
    {
        std::string s = std::to_string(p1.score);
        drawNumber(h.r, h.a.uiFont(), 170.0f - 14.0f * static_cast<float>(s.size()), 34, s, 1.0f, kScoreColor);
    }
    h.weapon(10, 54, true, p1.weapon);
    h.missiles(10, 95, true, 76, p1);
    h.powerups(82, 54, 86, true, 148, p1);
    h.lives(p2.lives, true);
    h.barFrame(610, 10, true);
    h.fill(10, p2.health, true);
    h.barFrame(610, 32, true);
    drawNumber(h.r, h.a.uiFont(), 630, 34, std::to_string(p2.score), 1.0f, kScoreColor);
    h.weapon(720, 54, false, p2.weapon);
    h.missiles(720, 95, false, 786, p2);
    h.powerups(648, 54, 649, true, 714, p2);
}

// Without the typewriter, message and cursor, which did not change.
void drawHud(Renderer2D& r, const UiAssets& a, const HudState& s) {
    Hud h{r, a};
    if (s.playerCount >= 2) drawTwoPlayers(h, s.players[0], s.players[1]);
    else drawOnePlayer(h, s.players[0]);
}

} // namespace old

// A game's data on a Vfs, selected explicitly.
struct GameVfs {
    GameData data;
    Vfs vfs;
    bool ok = false;
    explicit GameVfs(GameId id) {
        data = locateGameData(testdata::root(), gameProfile(id));
        if (!data.present()) return;
        if (data.hasExtracted) {
            vfs.mount(makeDirSource(data.extractedDir));
        } else {
            for (const std::string& p : data.paks) {
                auto src = makePakSource(openFileStream(p));
                if (!src) return;
                vfs.mount(std::move(src));
            }
        }
        ok = true;
    }
};

#define REQUIRE_GAME_VFS(var, id)                                                                  \
    GameVfs var(id);                                                                               \
    if (!var.ok) {                                                                                 \
        std::fprintf(stderr, "SKIPPED (no data for %s): %s\n", gameProfile(id).key, __FILE__);     \
        return;                                                                                    \
    }

bool sameColor(const Color& a, const Color& b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }
bool sameUv(const SpecUv& a, const SpecUv& b) { return a.s0 == b.s0 && a.t0 == b.t0 && a.s1 == b.s1 && a.t1 == b.t1; }

bool sameQuad(const Quad& a, const Quad& b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h && a.s0 == b.s0 && a.t0 == b.t0 && a.s1 == b.s1 &&
           a.t1 == b.t1 && a.texture == b.texture && sameColor(a.color, b.color) && a.blend == b.blend;
}

// The quads of a texture, in drawing order.
std::vector<Quad> quadsOf(const Renderer2D& r, const Texture2D* t) {
    std::vector<Quad> out;
    for (const Quad& q : r.quads())
        if (q.texture == t) out.push_back(q);
    return out;
}
bool hasQuadAt(const std::vector<Quad>& qs, float x, float y, float w, float h) {
    for (const Quad& q : qs)
        if (std::fabs(q.x - x) < 0.01f && std::fabs(q.y - y) < 0.01f && std::fabs(q.w - w) < 0.01f && std::fabs(q.h - h) < 0.01f)
            return true;
    return false;
}
const Quad* quadAt(const std::vector<Quad>& qs, float x, float y) {
    for (const Quad& q : qs)
        if (std::fabs(q.x - x) < 0.01f && std::fabs(q.y - y) < 0.01f) return &q;
    return nullptr;
}

HudPlayer richPlayer() {
    HudPlayer p;
    p.health = 250;
    p.lives = 7;
    p.score = 43210;
    p.weapon = 2;
    for (int k = 0; k < 9; k++) p.upgrades[k] = k % 4;
    const int m[kMissileTypes] = {12, 0, 5, 3, 1};
    for (int t = 0; t < kMissileTypes; t++) p.missiles[t] = m[t];
    p.missileSelected = 2;
    const int pu[10] = {2, 1, 0, 4, 1, 0, 15, 0, 3, 20};
    for (int k = 0; k < 10; k++) p.powerups[k] = pu[k];
    p.powerupSelected = 3;
    return p;
}

} // namespace

// ---------------------------------------------------------------------------
// CPU-only
// ---------------------------------------------------------------------------

TEST_CASE("hud layout: the first game's tables equal the old constants") {
    const HudLayout& L = hudLayout(GameId::AirStrike3D);
    CHECK(std::string(L.barAtlas) == "gfx\\ui\\mainbar.tga");
    CHECK(sameUv(L.barFrame.uv, old::kBarFrame));
    CHECK(L.barFrame.w == 180);
    CHECK(L.barFrame.h == 21);
    CHECK(sameColor(L.barFrame.color, old::kFrameGrey));
    CHECK(L.barFrame.blend == Blend::Add);
    CHECK(sameUv(L.box.uv, old::kBoxFrame));
    CHECK(L.box.w == 70);
    CHECK(L.box.h == 39);
    CHECK(L.fill.uv.s0 == old::kFillS0);
    CHECK(L.fill.uv.t0 == old::kFillT0);
    CHECK(L.fill.uv.t1 == old::kFillT1);
    CHECK(L.fillWidth == 172);
    CHECK(L.fillSpanTexels == 174);
    CHECK(L.lifeIconsMax == 5);
    CHECK(sameColor(L.life.color, old::kLifeGrey));
    CHECK(sameColor(L.scoreColor, old::kScoreColor));
    CHECK(sameColor(L.countSelected, old::kCountSelected));
    CHECK(sameColor(L.countOther, old::kCountOther));
    CHECK(L.powerupCountMin == 2);
    CHECK(L.mouseCursor != nullptr);
    CHECK(L.hint == HintStyle::V170Box);
    for (int w = -1; w < 22; w++) CHECK(sameUv(weaponIconUv(w), old::weaponIconUv(w)));
    for (int t = 0; t < kMissileTypes; t++) CHECK(sameUv(missileIconUv(t), old::kMissiles[t]));
    for (int k = 0; k < 4; k++) CHECK(sameUv(powerupIconUv(k), old::kPowerups[k]));
    for (int k = 4; k < kPowerupSlots; k++) CHECK(powerupIconUv(k).empty());
    // The icon functions default to the first game.
    CHECK(sameUv(weaponIconUv(3), weaponIconUv(3, GameId::AirStrike3D)));
}

TEST_CASE("hud layout: AirStrike 2 and Gulf Thunder tables (as2/frontend.md 4)") {
    const HudLayout& L = hudLayout(GameId::AirStrike2);
    CHECK(std::string(L.barAtlas) == "gfx\\ui\\mainbar2.tga");
    CHECK(L.lifeIconsMax == 10);
    CHECK(L.lifeIconsMax == gameProfile(GameId::AirStrike2).rules.lifeIconsMax);
    CHECK(L.weapons.size() == static_cast<size_t>(gameProfile(GameId::AirStrike2).rules.weaponSlots));
    CHECK(L.weaponLevelMax == std::vector<int>{4, 5, 7, 8, 5, 5, 4, 5, 3});
    CHECK(L.mouseCursor == nullptr);
    CHECK(L.hint == HintStyle::SequelPanel);
    CHECK(L.powerupCountMin == 1);
    // Texels of 4.1 in spec UVs.
    CHECK(L.box.uv.s1 == doctest::Approx(87.0f / 256.0f));
    CHECK(L.box.uv.t1 == doctest::Approx(1.0f - 102.0f / 256.0f));
    CHECK(L.box.uv.t0 == doctest::Approx(1.0f - 162.0f / 256.0f));
    // Weapon table 4.4 (the Plasma Cannon's narrow cell), power-ups by texel.
    CHECK(weaponIconUv(2, GameId::AirStrike2).s0 == doctest::Approx(0.774f));
    CHECK(weaponIconUv(2, GameId::AirStrike2).t0 == doctest::Approx(0.453f));
    CHECK(weaponIconUv(9, GameId::AirStrike2).empty());
    CHECK(powerupIconUv(2, GameId::AirStrike2).s0 == doctest::Approx(66.0f / 256.0f));
    CHECK(powerupIconUv(9, GameId::AirStrike2).s0 == doctest::Approx(66.0f / 256.0f));
    CHECK(powerupIconUv(9, GameId::AirStrike2).t1 == doctest::Approx(1.0f - 70.0f / 128.0f));
    CHECK(powerupIconUv(10, GameId::AirStrike2).empty());
    // Timer slots: never dimmed, and exactly the slots power-up cycling skips.
    const u32 skip = gameProfile(GameId::AirStrike2).rules.powerUpCycleSkip;
    for (int k = 0; k < static_cast<int>(L.powerups.size()); k++)
        CHECK(L.powerups[static_cast<size_t>(k)].alwaysFull == (((skip >> k) & 1u) != 0));
    CHECK(L.powerups[0].blend == Blend::Add);
    // Gulf Thunder: AirStrike 2's HUD with its own weapon table and caps
    // (apps/tests/gulf_rules_test.cpp).
    const HudLayout& G = hudLayout(GameId::GulfThunder);
    CHECK(std::string(G.barAtlas) == "gfx\\ui\\mainbar2.tga");
    CHECK(G.weapons.size() == L.weapons.size());
    CHECK(G.onePlayer.missiles.firstY == L.onePlayer.missiles.firstY);
    CHECK_FALSE(L.levelOverrunsCap);
    CHECK(L.panelSkin == HudPanelSkin::As2);
}

// ---------------------------------------------------------------------------
// With data and a GLES context
// ---------------------------------------------------------------------------

TEST_CASE("hud layout: the first game draws the same quads as the old code") {
    AS3D_REQUIRE_GLES();
    REQUIRE_GAME_VFS(gv, GameId::AirStrike3D);
    UiAssets a;
    std::string err;
    REQUIRE_MESSAGE(a.load(gv.vfs, &err, GameId::AirStrike3D), err);
    CHECK(a.missing == 0);
    HudPlayer p = richPlayer();
    p.weapon = 9;
    p.powerupSelected = 1;
    HudPlayer q = p;
    q.health = 500; // clamped
    q.lives = 9;
    q.weapon = 13; // no picture
    q.missileSelected = 0;
    HudPlayer z;
    z.health = -20;
    z.lives = 0;
    for (int players : {1, 2})
        for (const HudPlayer* p1 : {&p, &q, &z}) {
            HudState st;
            st.playerCount = players;
            st.players[0] = *p1;
            st.players[1] = p1 == &p ? q : p;
            Renderer2D mine, ref;
            mine.begin(800, 600);
            ref.begin(800, 600);
            drawHud(mine, a, st);
            old::drawHud(ref, a, st);
            REQUIRE(mine.quads().size() == ref.quads().size());
            for (size_t i = 0; i < ref.quads().size(); i++) {
                INFO("players " << players << " quad " << i);
                CHECK(sameQuad(mine.quads()[i], ref.quads()[i]));
            }
        }
}

TEST_CASE("hud layout: every rectangle lies inside its atlas") {
    AS3D_REQUIRE_GLES();
    for (GameId id : {GameId::AirStrike3D, GameId::AirStrike2, GameId::GulfThunder}) {
        GameVfs gv(id);
        if (!gv.ok) {
            std::fprintf(stderr, "SKIPPED (no data for %s): %s\n", gameProfile(id).key, __FILE__);
            continue;
        }
        UiAssets a;
        std::string err;
        REQUIRE_MESSAGE(a.load(gv.vfs, &err, id), err);
        INFO("game " << std::string(gameProfile(id).key) << " " << err);
        CHECK(a.missing == 0); // no missing mainbar.tga for the sequels any more
        const HudLayout& L = a.hudLayout();
        REQUIRE(a.mainbar.valid());
        CHECK(static_cast<float>(a.mainbar.width()) == L.barAtlasW);
        CHECK(static_cast<float>(a.mainbar.height()) == L.barAtlasH);
        auto inside = [](const Texture2D& t, const SpecUv& uv, const char* what) {
            INFO(what << " uv " << uv.s0 << " " << uv.t0 << " " << uv.s1 << " " << uv.t1);
            const float W = static_cast<float>(t.width()), H = static_cast<float>(t.height());
            const float x0 = std::min(uv.s0, uv.s1) * W, x1 = std::max(uv.s0, uv.s1) * W;
            const float y0 = (1.0f - std::max(uv.t0, uv.t1)) * H, y1 = (1.0f - std::min(uv.t0, uv.t1)) * H;
            CHECK(x0 >= -0.01f);
            CHECK(y0 >= -0.01f);
            CHECK(x1 <= W + 0.01f);
            CHECK(y1 <= H + 0.51f); // 0.727 etc. are rounded table values: 35/128 = 0.2734
            CHECK(x1 > x0);
            CHECK(y1 > y0);
        };
        for (const HudPiece* p : {&L.barFrame, &L.scoreFrame, &L.box, &L.fill}) inside(a.mainbar, p->uv, "bar piece");
        if (L.levelOn.w > 0) {
            // The widest run of pips.
            const int mx = *std::max_element(L.weaponLevelMax.begin(), L.weaponLevelMax.end());
            for (const HudPiece* p : {&L.levelOn, &L.levelMax}) {
                SpecUv uv = p->uv;
                uv.s1 = uv.s0 + (p->uv.s1 - p->uv.s0) * static_cast<float>(mx);
                inside(a.mainbar, uv, "pips");
            }
            inside(a.mainbar, L.selLine.uv, "selection line");
        }
        REQUIRE(a.life.valid());
        inside(a.life, L.life.uv, "life");
        for (const HudIcon& ic : L.weapons) if (!ic.uv.empty()) inside(a.weapons, ic.uv, "weapon");
        for (const HudIcon& ic : L.missiles) if (!ic.uv.empty()) inside(a.missiles, ic.uv, "missile");
        for (const HudIcon& ic : L.powerups) if (!ic.uv.empty()) inside(a.items, ic.uv, "power-up");
        if (L.hint == HintStyle::SequelPanel) {
            CHECK(a.panel.valid());
            CHECK(a.panelNoise.valid());
            CHECK(a.panel.width() == 256);
            CHECK(a.panel.height() == 256);
        }
    }
}

TEST_CASE("hud layout: AirStrike 2 elements land where as2/frontend.md 4.2 and 4.3 put them") {
    AS3D_REQUIRE_GLES();
    REQUIRE_GAME_VFS(gv, GameId::AirStrike2);
    UiAssets a;
    std::string err;
    REQUIRE_MESSAGE(a.load(gv.vfs, &err, GameId::AirStrike2), err);
    const Texture2D* bar = &a.mainbar;

    SUBCASE("one player") {
        HudState st;
        st.players[0] = richPlayer();
        st.players[0].maxHealth = 500; // 250 / 500 -> 21 segments
        st.players[0].lives = 14;
        Renderer2D r;
        r.begin(800, 600);
        drawHud(r, a, st);
        const std::vector<Quad> b = quadsOf(r, bar);
        CHECK(hasQuadAt(b, 0, 6, 232, 34));                // health frame
        CHECK(hasQuadAt(b, 0, 6, 17 + 5 * 21, 34));        // fill
        CHECK(hasQuadAt(b, 568, 6, 232, 34));              // score frame, mirrored
        CHECK(quadAt(b, 568, 6)->s0 > quadAt(b, 568, 6)->s1);
        CHECK(hasQuadAt(b, 0, 40, 87, 60));                // weapon box
        CHECK(hasQuadAt(b, 18, 51, 7 * 7, 6));             // pips: plasma cannon maximum 7
        CHECK(hasQuadAt(b, 18, 51, 7 * 2, 6));             // level 2
        // Missiles 0, 2, 3, 4 packed from F = 100, step 60; type 2 selected: its line.
        for (float F : {100.0f, 160.0f, 220.0f, 280.0f}) CHECK(hasQuadAt(b, 0, F, 87, 60));
        CHECK_FALSE(hasQuadAt(b, 0, 340, 87, 60));
        CHECK(hasQuadAt(b, 22, 165, 53, 1));
        CHECK(quadAt(b, 22, 165)->blend == Blend::Add);
        const std::vector<Quad> m = quadsOf(r, &a.missiles);
        CHECK(m.size() == 4);
        CHECK(hasQuadAt(m, 15, 117, 66, 35));
        CHECK(quadAt(m, 15, 117)->color.a == doctest::Approx(0x40 / 255.0f)); // not selected: dimmed
        CHECK(quadAt(m, 15, 177)->color.a == doctest::Approx(1.0f));          // selected
        // Weapon icon (15, 57), ADD.
        const std::vector<Quad> w = quadsOf(r, &a.weapons);
        REQUIRE(w.size() == 1);
        CHECK(hasQuadAt(w, 15, 57, 66, 35));
        CHECK(w[0].blend == Blend::Add);
        // Power-ups 0, 1, 3, 4, 6, 8, 9 packed from F = 44: icons at (721, F + 17); no frames and
        // no line in the column (issue 280); slot 0 dimmed grey (ADD), timers 6 and 9 opaque.
        const std::vector<Quad> it = quadsOf(r, &a.items);
        CHECK(it.size() == 7);
        for (int n = 0; n < 7; n++) CHECK(hasQuadAt(it, 721, 44 + 60.0f * static_cast<float>(n) + 17, 66, 35));
        CHECK(quadAt(it, 721, 61)->blend == Blend::Add);
        CHECK(quadAt(it, 721, 61)->color.r == doctest::Approx(0x40 / 255.0f));
        CHECK(quadAt(it, 721, 61 + 120)->color.a == doctest::Approx(1.0f)); // slot 3 selected
        CHECK(quadAt(it, 721, 61 + 240)->color.a == doctest::Approx(1.0f)); // slot 6 timer
        CHECK(quadAt(it, 721, 61 + 60)->color.a == doctest::Approx(0x40 / 255.0f));
        for (const Quad& q : b) CHECK(q.x < 700); // nothing of mainbar2 in the power-up column
        // Lives: 10 icons at most.
        const std::vector<Quad> l = quadsOf(r, &a.life);
        CHECK(l.size() == 10);
        CHECK(hasQuadAt(l, 15 + 32 * 9, 555, 32, 32));
    }

    SUBCASE("health fill against the maximum") {
        auto fillWidth = [&](float health, float maxHealth) {
            HudState st;
            st.players[0].health = health;
            st.players[0].maxHealth = maxHealth;
            Renderer2D r;
            r.begin(800, 600);
            drawHud(r, a, st);
            for (const Quad& q : quadsOf(r, bar))
                if (q.x == 0 && q.y == 6 && q.w != 232) return q.w;
            return -1.0f;
        };
        CHECK(fillWidth(600, 600) == 227);
        CHECK(fillWidth(900, 600) == 227);  // above the maximum: full
        CHECK(fillWidth(300, 600) == 122);  // 21 segments
        CHECK(fillWidth(299, 600) == 117);  // 20
        CHECK(fillWidth(400, 800) == 122);
        CHECK(fillWidth(0, 500) == 17);     // the cap alone
        CHECK(fillWidth(-50, 500) == 17);   // clamped to 0 segments
    }

    SUBCASE("two players") {
        HudState st;
        st.playerCount = 2;
        st.players[0] = richPlayer();
        st.players[1] = richPlayer();
        st.players[1].health = 400;
        st.players[1].maxHealth = 400;
        st.players[1].weapon = 5; // lightning gun, max 5, level 1
        st.players[1].lives = 2;
        Renderer2D r;
        r.begin(800, 600);
        drawHud(r, a, st);
        const std::vector<Quad> b = quadsOf(r, bar);
        CHECK(hasQuadAt(b, 0, 40, 232, 34));      // p1 score frame, not mirrored
        CHECK(quadAt(b, 0, 40)->s0 < quadAt(b, 0, 40)->s1);
        CHECK(hasQuadAt(b, 568, 40, 232, 34));    // p2 score frame, mirrored
        CHECK(hasQuadAt(b, 573, 6, 227, 34));     // p2 full fill from x 800 leftwards
        CHECK(quadAt(b, 573, 6)->s0 > quadAt(b, 573, 6)->s1);
        CHECK(hasQuadAt(b, 0, 74, 87, 60));       // p1 weapon box
        CHECK(hasQuadAt(b, 713, 74, 87, 60));     // p2 weapon box, mirrored
        CHECK(hasQuadAt(b, 18, 85, 49, 6));       // p1 pips
        CHECK(hasQuadAt(b, 782 - 35, 85, 35, 6)); // p2 pips end at 782
        CHECK(hasQuadAt(b, 782 - 7, 85, 7, 6));
        // Missiles from 134; power-ups follow in the same column (4 missiles -> F' = 374).
        CHECK(hasQuadAt(b, 0, 134, 87, 60));
        CHECK(hasQuadAt(b, 713, 134, 87, 60));
        CHECK(hasQuadAt(b, 0, 374, 87, 60));
        CHECK(hasQuadAt(b, 713, 374, 87, 60));
        const std::vector<Quad> m = quadsOf(r, &a.missiles);
        CHECK(hasQuadAt(m, 15, 147, 66, 35));
        CHECK(hasQuadAt(m, 719, 147, 66, 35));
        const std::vector<Quad> it = quadsOf(r, &a.items);
        CHECK(hasQuadAt(it, 15, 391, 66, 35));
        CHECK(hasQuadAt(it, 721, 391, 66, 35));
        const std::vector<Quad> w = quadsOf(r, &a.weapons);
        CHECK(hasQuadAt(w, 15, 91, 66, 35));
        CHECK(hasQuadAt(w, 719, 91, 66, 35));
        const std::vector<Quad> l = quadsOf(r, &a.life);
        CHECK(hasQuadAt(l, 753, 555, 32, 32));
        CHECK(hasQuadAt(l, 721, 555, 32, 32));
    }

    SUBCASE("hint panel layout (3.15)") {
        HintLayout L = layoutHint(FontMetrics::original(), "one^two^three", HintStyle::SequelPanel);
        CHECK(L.box.h == 160);
        CHECK(L.box.w == 360);
        CHECK(L.box.x == 220);
        CHECK(L.box.y == 220);
        CHECK(L.textTop == doctest::Approx(220 + (160 - 54) / 2.0f));
        CHECK(L.okButton.y == 520);
        CHECK(L.okButton.x + L.okButton.w / 2 == doctest::Approx(400));
        std::string many;
        for (int i = 0; i < 9; i++) many += "line^";
        CHECK(layoutHint(FontMetrics::original(), many, HintStyle::SequelPanel).box.h == 18 * 10 + 60);
    }
}

TEST_CASE("hud layout: AirStrike 2 HUD and hint panel render headless, 4:3 and wide") {
    AS3D_REQUIRE_GLES();
    REQUIRE_GAME_VFS(gv, GameId::AirStrike2);
    UiAssets a;
    std::string err;
    REQUIRE_MESSAGE(a.load(gv.vfs, &err, GameId::AirStrike2), err);
    Renderer2D r;
    REQUIRE_MESSAGE(r.init(&err), err);
    const u8 bg[3] = {90, 100, 64};
    for (int mode = 0; mode < 2; mode++) {
        const int W = mode == 0 ? 800 : 2400, H = mode == 0 ? 600 : 1080;
        INFO("size " << W << "x" << H);
        RenderTarget target;
        REQUIRE(target.create(W, H, 0));
        const Mapping map = computeMapping(W, H);
        auto lit = [&](const Image& img, float x, float y, float w, float h) {
            return uitest::litIn(img, bg, static_cast<int>(map.toFbX(x)), static_cast<int>(map.toFbY(y)),
                                 static_cast<int>(map.toFbX(x + w)), static_cast<int>(map.toFbY(y + h)));
        };
        auto area = [&](float w, float h) { return static_cast<long>(w * map.scaleX * h * map.scaleY); };
        HudState st;
        st.players[0] = richPlayer();
        st.players[0].maxHealth = 500;
        target.bind();
        clear({bg[0] / 255.0f, bg[1] / 255.0f, bg[2] / 255.0f, 1}, true);
        r.begin(W, H);
        drawHud(r, a, st);
        CHECK(r.dropped() == 0);
        r.flush();
        Image img;
        REQUIRE(target.readPixels(img));
        // Health bar: the left half (filled) is lit throughout; the frame's empty right part less.
        CHECK(lit(img, 20, 12, 100, 20) > area(100, 20) * 9 / 10);
        CHECK(lit(img, 0, 40, 87, 60) > area(87, 60) / 3);    // weapon box
        CHECK(lit(img, 0, 280, 87, 60) > area(87, 60) / 3);   // fourth missile box
        CHECK(lit(img, 0, 340, 87, 60) == 0);                 // no fifth
        CHECK(lit(img, 568, 6, 232, 34) > area(232, 34) / 3); // score frame
        CHECK(lit(img, 721, 61, 66, 35) > 0);                 // first power-up icon
        CHECK(lit(img, 15, 555, 7 * 32, 32) > area(7 * 32, 32) / 10); // seven lives
        CHECK(lit(img, 15 + 7 * 32, 555, 3 * 32, 32) == 0);
        CHECK(lit(img, 200, 150, 400, 300) == 0);             // the middle stays clear
        if (mode == 1) {
            // Nothing outside the centred 4:3 field.
            const int fx0 = static_cast<int>(map.toFbX(0)), fx1 = static_cast<int>(map.toFbX(800));
            CHECK(uitest::litIn(img, bg, 0, 0, fx0 - 1, H) == 0);
            CHECK(uitest::litIn(img, bg, fx1 + 1, 0, W, H) == 0);
        }

        // The hint panel: titled panel darkens the box, the text and the Ok button light.
        target.bind();
        clear({bg[0] / 255.0f, bg[1] / 255.0f, bg[2] / 255.0f, 1}, true);
        r.begin(W, H);
        drawHint(r, a, "Hint text^second line");
        r.flush();
        REQUIRE(target.readPixels(img));
        const HintLayout L = layoutHint(FontMetrics::original(), "Hint text^second line", HintStyle::SequelPanel);
        CHECK(lit(img, L.box.x + 10, L.box.y + 10, L.box.w - 20, L.box.h - 20) > area(L.box.w - 20, L.box.h - 20) / 2);
        CHECK(lit(img, L.box.x - 4, L.box.y - 15, 60, 28) > area(60, 28) / 3);  // top bar, left end
        CHECK(lit(img, L.box.x, L.box.y - 47, 60, 30) > area(60, 30) / 3);      // title tab
        CHECK(lit(img, L.okButton.x, L.okButton.y, L.okButton.w, L.okButton.h) > area(L.okButton.w, L.okButton.h) / 3);
        CHECK(lit(img, 0, 400, 150, 100) == 0);
    }
}
