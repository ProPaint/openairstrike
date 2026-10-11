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
#include <cstdlib>
#include <string>
#include <vector>

#include "as3d/game_data.h"
#include "as3d/gfx.h"
#include "as3d/hud_layout.h"
#include "as3d/image.h"
#include "as3d/input.h"
#include "as3d/player_select.h"
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

// ---------------------------------------------------------------------------
// The 3DS bottom screen (HudTarget::Bottom320x240, M0.8)
// ---------------------------------------------------------------------------

namespace {
bool rectsOverlap(const HudElementRect& a, const HudElementRect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
} // namespace

TEST_CASE("hud layout: Main is unchanged and each game has its own bottom table") {
    CHECK(&hudLayout(GameId::AirStrike3D, HudTarget::Main) == &hudLayout(GameId::AirStrike3D));
    CHECK(&hudLayout(GameId::AirStrike2, HudTarget::Main) == &hudLayout(GameId::AirStrike2));
    CHECK(&hudLayout(GameId::GulfThunder, HudTarget::Main) == &hudLayout(GameId::GulfThunder));
    for (GameId g : {GameId::AirStrike2, GameId::GulfThunder}) {
        const HudLayout& m = hudLayout(g);
        const HudLayout& b = hudLayout(g, HudTarget::Bottom320x240);
        CHECK(&b != &m);
        CHECK(b.game == g);
        // The same art and icon tables, only places and sizes differ; Main keeps its values.
        CHECK(std::string(b.barAtlas) == std::string(m.barAtlas));
        CHECK(sameUv(b.barFrame.uv, m.barFrame.uv));
        CHECK(sameUv(b.box.uv, m.box.uv));
        CHECK(b.weapons.size() == m.weapons.size());
        CHECK(b.weaponLevelMax == m.weaponLevelMax);
        CHECK(b.hint == HintStyle::SequelPanel);
        CHECK(m.scoreScale == 1.0f);
        CHECK(m.fillDrawScale == 1.0f);
        CHECK(m.levelNameY == 500);
        CHECK(m.onePlayer.barY == 6);
        CHECK(b.onePlayer.livesY < kHudBottomBandTop);
    }
    CHECK(hudLayout(GameId::AirStrike2, HudTarget::Bottom320x240).weapons.size() == 9);
    const HudLayout& m = hudLayout(GameId::AirStrike3D);
    CHECK(m.scoreScale == 1.0f);
    CHECK(m.levelNameY == 500);
    CHECK(m.messageY == 555);
    CHECK(m.hintScale == 1.0f);
    const HudLayout& b = hudLayout(GameId::AirStrike3D, HudTarget::Bottom320x240);
    CHECK(&b != &m);
    // The same atlases and icon tables as Main: only places and sizes differ.
    CHECK(std::string(b.barAtlas) == std::string(m.barAtlas));
    CHECK(sameUv(b.barFrame.uv, m.barFrame.uv));
    CHECK(sameUv(b.box.uv, m.box.uv));
    CHECK(b.weapons.size() == m.weapons.size());
    CHECK(b.hint == HintStyle::V170Box);
}

TEST_CASE("hud layout: the sequels' hint panel on the bottom screen keeps its Ok button above the touch band") {
    for (GameId game : {GameId::AirStrike2, GameId::GulfThunder}) {
        const HudLayout& L = hudLayout(game, HudTarget::Bottom320x240);
        CAPTURE(std::string(L.name));
        CHECK(hudLayout(game).hintScale == 1.0f);
        CHECK(L.hintScale >= 1.5f);
        for (int n : {1, 3, 6, 10}) {
            CAPTURE(n);
            std::string text = "Destroy the enemy convoy before it reaches the base";
            for (int i = 1; i < n; i++) text += "^Use the missiles on the armoured targets";
            // The desktop: the original's layout, button at y 520 whatever the box.
            const HintLayout m = layoutHint(FontMetrics::original(), text, HintStyle::SequelPanel, hudLayout(game).hintScale);
            CHECK(m.scale == 1.0f);
            CHECK(m.okButton.y == 520.0f);
            // The 3DS bottom screen: bigger text, the button above the band, the panel (its bottom
            // frame 18 below the box, its title tab 47 above) above the button and inside the screen.
            const HintLayout b = layoutHint(FontMetrics::original(), text, HintStyle::SequelPanel, L.hintScale);
            CHECK(b.scale > 1.0f);
            CHECK(b.okButton.y + b.okButton.h <= kHudBottomBandTop);
            CHECK(b.okButton.x >= 0.0f);
            CHECK(b.okButton.x + b.okButton.w <= 800.0f);
            CHECK(b.box.y - 47.0f >= 0.0f);
            CHECK(b.box.y + b.box.h + 18.0f <= b.okButton.y);
            CHECK(b.box.x >= 0.0f);
            CHECK(b.box.x + b.box.w <= 800.0f);
            CHECK(b.textTop >= b.box.y);
            CHECK(b.textTop + 18.0f * b.scale * static_cast<float>(b.lines.size()) <= b.box.y + b.box.h + 0.5f);
            // On the 320x240 screen (scale 0.4): inside, and the button above the touch buttons.
            CHECK((b.okButton.y + b.okButton.h) * 0.4f <= 240.0f - 36.0f);
        }
    }
}

TEST_CASE("hud layout: the Bottom320x240 hint box is bigger and fits the screen") {
    const FontMetrics& fm = FontMetrics::original();
    const HintLayout one = layoutHint(fm, "one^two^three");
    CHECK(one.scale == 1.0f);
    CHECK(one.box.w == 360);
    CHECK(one.okButton.x == 350);
    const HintLayout big = layoutHint(fm, "one^two^three", HintStyle::V170Box, 1.5f);
    CHECK(big.scale == 1.5f);
    CHECK(big.box.w == 540);
    CHECK(big.box.h > one.box.h * 1.4f);
    CHECK(big.okButton.w == 150);
    CHECK(big.okButton.x + big.okButton.w * 0.5f == 400);
    // A wide line gives the scale way: the box stays inside the 800 pixels.
    const HintLayout wide = layoutHint(fm, std::string(60, 'W'), HintStyle::V170Box, 1.5f);
    CHECK(wide.scale == 1.0f); // already wider than the screen at the original size
    const HintLayout mid = layoutHint(fm, std::string(25, 'W'), HintStyle::V170Box, 1.5f);
    CHECK(mid.scale >= 1.0f);
    CHECK(mid.scale <= 1.5f);
    CHECK(mid.box.x >= 0);
    CHECK(mid.box.x + mid.box.w <= 800.0f);
}

TEST_CASE("touch layout: the bottom-screen preset keeps its buttons in the lower band") {
    for (float scale : {1.0f, 2.0f}) {
        CAPTURE(scale);
        const int W = static_cast<int>(320 * scale), H = static_cast<int>(240 * scale);
        const TouchLayoutOptions o = TouchLayoutOptions::forBottomScreen320x240(132.0f * scale);
        const TouchLayout L = computeTouchLayout(W, H, o);
        const float bandTop = static_cast<float>(H) * (1.0f - 36.0f / 240.0f);
        CHECK(bandTop == doctest::Approx(204.0f * scale));
        for (int b = 0; b < kTouchButtonCount; b++) {
            CAPTURE(touchButtonName(static_cast<TouchButton>(b)));
            const TouchCircle& c = L.circles[b];
            CHECK(c.r > 0);
            CHECK(c.x - c.r >= 0);
            CHECK(c.x + c.r <= static_cast<float>(W));
            CHECK(c.y - c.r >= bandTop - 0.01f);
            CHECK(c.y + c.r <= static_cast<float>(H) + 0.01f);
            // The 9 mm touch area is honoured by the hit circles.
            CHECK(L.hit[b].r >= 9.0f * L.pixelsPerMm * 0.5f - 0.01f);
            for (int k = b + 1; k < kTouchButtonCount; k++) {
                const TouchCircle& d = L.circles[k];
                const float dist = std::sqrt((c.x - d.x) * (c.x - d.x) + (c.y - d.y) * (c.y - d.y));
                CHECK(dist >= c.r + d.r);
            }
        }
        CHECK_FALSE(L.outside);
        CHECK(L.pixelsPerMm == doctest::Approx(132.0f * scale / 25.4f));
        // Missile and power-up are the big ones; the order is pause, weapon, missile+, missile,
        // power-up, power-up+.
        auto cx = [&](TouchButton b) { return L.circles[static_cast<int>(b)].x; };
        CHECK(cx(TouchButton::Pause) < cx(TouchButton::NextWeapon));
        CHECK(cx(TouchButton::NextWeapon) < cx(TouchButton::NextMissile));
        CHECK(cx(TouchButton::NextMissile) < cx(TouchButton::Missile));
        CHECK(cx(TouchButton::Missile) < cx(TouchButton::PowerUp));
        CHECK(cx(TouchButton::PowerUp) < cx(TouchButton::NextPowerUp));
        CHECK(L.circles[static_cast<int>(TouchButton::Missile)].r > L.circles[static_cast<int>(TouchButton::NextMissile)].r);
        // A tap on a button's centre hits it; one above the band hits nothing.
        for (int b = 0; b < kTouchButtonCount; b++)
            CHECK(L.hitTest(L.circles[b].x / static_cast<float>(W), L.circles[b].y / static_cast<float>(H)) == b);
        CHECK(L.hitTest(0.5f, 0.3f) == -1);
        // Left-handed: the mirror image.
        TouchLayoutOptions lo = o;
        lo.leftHanded = true;
        const TouchLayout M = computeTouchLayout(W, H, lo);
        for (int b = 0; b < kTouchButtonCount; b++) CHECK(M.circles[b].x == doctest::Approx(static_cast<float>(W) - L.circles[b].x));
    }
}

// ---------------------------------------------------------------------------
// The first game's console HUD (docs/3ds-ui-design.md "In play on the console (round 4, R4.7)"):
// the bottom screen is the HUD in whole pixels and its tiles are the touch controls.
// ---------------------------------------------------------------------------

namespace {
const HudElementRect* rectNamed(const std::vector<HudElementRect>& v, const std::string& n) {
    for (const HudElementRect& e : v)
        if (n == e.name) return &e;
    return nullptr;
}
} // namespace

TEST_CASE("console hud: the layout in pixels, the score rule, no overlaps") {
  // The first game (R4.7) and the sequels (R4.8: their art, the same grid, five power-up rows).
  for (GameId game : {GameId::AirStrike3D, GameId::AirStrike2, GameId::GulfThunder}) {
    const HudLayout& L = hudLayout(game, HudTarget::Bottom320x240);
    CAPTURE(std::string(L.name));
    const bool sequel = game != GameId::AirStrike3D;
    const std::vector<HudElementRect> r = hudElementRects(L);
    // Every element on the 320 x 240 screen, on whole pixels (virtual = pixels x 2.5).
    for (const HudElementRect& e : r) {
        CAPTURE(e.name);
        const float x = e.x * 0.4f, y = e.y * 0.4f, w = e.w * 0.4f, h = e.h * 0.4f;
        CHECK(x >= 0);
        CHECK(y >= 0);
        CHECK(x + w <= 320.0f + 0.01f);
        CHECK(y + h <= 240.0f + 0.01f);
        CHECK(std::fabs(x - std::round(x)) < 0.01f);
        CHECK(std::fabs(y - std::round(y)) < 0.01f);
        CHECK(std::fabs(w - std::round(w)) < 0.01f);
        CHECK(std::fabs(h - std::round(h)) < 0.01f);
    }
    for (size_t i = 0; i < r.size(); i++)
        for (size_t j = i + 1; j < r.size(); j++) {
            CAPTURE(r[i].name);
            CAPTURE(r[j].name);
            CHECK_FALSE(rectsOverlap(r[i], r[j]));
        }
    // The score rule (Owner decisions, 2026-10-11): the score right-aligned in a field of 10
    // digits at L (14 px each), the field ending 8 px from the right edge; the health bar takes
    // what is left between the pause tile and the field, 8 px from either.
    const HudElementRect* score = rectNamed(r, "score");
    const HudElementRect* health = rectNamed(r, "health");
    const HudElementRect* pause = rectNamed(r, "pause");
    REQUIRE(score);
    REQUIRE(health);
    REQUIRE(pause);
    CHECK(score->w * 0.4f == doctest::Approx(10 * numberAdvancePx(FontSize::L)));
    CHECK((score->x + score->w) * 0.4f == doctest::Approx(312));
    CHECK(L.scoreScale * 0.4f == doctest::Approx(1.0f)); // L: one texel per pixel
    CHECK_FALSE(rectsOverlap(*score, *health));
    CHECK(score->x - (health->x + health->w) >= 8 * 2.5f - 0.01f);
    CHECK(health->x - (pause->x + pause->w) >= 4 * 2.5f - 0.01f);
    CHECK(health->w * 0.4f >= 100);
    // The widest score (10 digits) drawn right-aligned stays in the field.
    const float tenDigits = 14.0f * L.scoreScale * 10;
    CHECK(L.onePlayer.scoreX - tenDigits >= score->x - 0.01f);
    // The tiles: the pause tile 40 x 36, the weapon box, five missile and four power-up tiles,
    // each at least 36 px (a thumb) and the art's 70 x 38 at 1:1.
    CHECK(pause->w * 0.4f >= 36);
    CHECK(pause->h * 0.4f >= 36);
    std::vector<const char*> tiles = {"weapon", "missile0", "missile1", "missile2", "missile3", "missile4", "powerup0", "powerup1",
                                      "powerup2", "powerup3"};
    if (sequel) tiles.push_back("powerup4");
    CHECK((rectNamed(r, "powerup4") != nullptr) == sequel);
    for (const char* n : tiles) {
        CAPTURE(n);
        const HudElementRect* t = rectNamed(r, n);
        REQUIRE(t);
        CHECK(t->w * 0.4f == doctest::Approx(70));
        CHECK(t->h * 0.4f == doctest::Approx(38));
    }
    CHECK(rectNamed(r, "keys"));
    CHECK(rectNamed(r, "weapon_level"));
    // The level name and the message are on the top screen: no rectangle here.
    CHECK_FALSE(rectNamed(r, "level_name"));
    CHECK_FALSE(rectNamed(r, "message"));
    CHECK_FALSE(rectNamed(r, "missiles"));
    // The art 1:1: the boxes (the first game's whole; the sequels' in nine pieces whose corners
    // are 1:1 and fit the box) and icons at one texel per pixel.
    if (!sequel) {
        CHECK(L.box.w * 0.4f == doctest::Approx((L.box.uv.s1 - L.box.uv.s0) * L.barAtlasW).epsilon(0.01));
    } else {
        CHECK(L.boxSlice.w > 0);
        CHECK(L.sliceL + L.sliceR < L.box.w * 0.4f);
        CHECK(L.sliceT + L.sliceB < L.box.h * 0.4f);
        CHECK(L.boxSlice.x + L.boxSlice.w <= L.barAtlasW);
        CHECK(L.boxSlice.y + L.boxSlice.h <= L.barAtlasH);
        // The bar and its fill at exactly 1/2 of mainbar2's 232 x 34.
        CHECK(L.barFrame.w * 0.4f == doctest::Approx(116));
        CHECK(L.barFrame.h * 0.4f == doctest::Approx(17));
        CHECK(L.fillDrawScale * 0.4f == doctest::Approx(0.5f));
        // The longest pip run fits its rectangle; the level name and message are the top screen's.
        const HudElementRect* pips = rectNamed(r, "weapon_pips");
        REQUIRE(pips);
        CHECK(L.levelOn.w * 0.4f == doctest::Approx(7));
        CHECK(L.onePlayer.powerups.packedRows);
        CHECK(L.onePlayer.powerups.slots == 5);
        CHECK(L.lifeIconsMax == 5);
    }
    CHECK(L.iconW * 0.4f == doctest::Approx(66));
    CHECK(L.iconH * 0.4f == doctest::Approx(35));
    // Main is untouched.
    const HudLayout& M = hudLayout(game);
    CHECK(M.pauseTile.w == 0);
    CHECK_FALSE(M.onePlayer.missiles.fixedSlots);
    CHECK_FALSE(M.onePlayer.powerups.packedRows);
    CHECK(M.boxSlice.w == 0);
    CHECK(M.levelNameY == 500);
  }
}

TEST_CASE("console hud: a sequel's power-up row selects the kind it shows, then uses it") {
    TouchMapper m;
    m.setScreen(320, 240, TouchLayoutOptions::forHudTiles320x240(GameId::AirStrike2));
    m.settings().autoFire = false;
    const TouchLayout& T = m.layout();
    REQUIRE(T.tileCount == 12);
    int row1 = -1, row3 = -1;
    for (int i = 0; i < T.tileCount; ++i)
        if (T.tiles[i].kind == TouchTileKind::PowerUp && T.tiles[i].index == 1) row1 = i;
        else if (T.tiles[i].kind == TouchTileKind::PowerUp && T.tiles[i].index == 3) row3 = i;
    REQUIRE(row1 >= 0);
    REQUIRE(row3 >= 0);
    TouchInventory inv;
    inv.valid = true;
    inv.powerups[2] = 1; // row 0
    inv.powerups[5] = 2; // row 1
    inv.powerupSelected = 2;
    m.setInventory(inv);
    auto tap = [&](int tile, long long id) {
        const float nx = (T.tiles[tile].x + T.tiles[tile].w * 0.5f) / 320.0f, ny = (T.tiles[tile].y + T.tiles[tile].h * 0.5f) / 240.0f;
        m.touchEvent(id, TouchPhase::Down, nx, ny);
        m.touchEvent(id, TouchPhase::Up, nx, ny);
    };
    // An empty row: nothing.
    tap(row3, 1);
    CHECK(m.takeFrame().held[0] == 0);
    // Row 1 shows kind 5: next-power-up presses until it is selected, nothing used.
    tap(row1, 2);
    int presses = 0;
    bool last = false;
    for (int step = 0; step < 12; ++step) {
        const FrameInput f = m.takeFrame();
        CHECK((f.held[0] & ACT_POWERUP) == 0);
        const bool now = (f.held[0] & ACT_NEXT_POWERUP) != 0;
        if (now && !last) {
            ++presses;
            inv.powerupSelected = inv.powerupSelected == 2 ? 5 : 2;
            m.setInventory(inv);
        }
        last = now;
    }
    CHECK(inv.powerupSelected == 5);
    CHECK(presses == 1);
    // The same row again: kind 5 is used while the finger is down.
    const float nx = (T.tiles[row1].x + T.tiles[row1].w * 0.5f) / 320.0f, ny = (T.tiles[row1].y + T.tiles[row1].h * 0.5f) / 240.0f;
    m.touchEvent(3, TouchPhase::Down, nx, ny);
    CHECK((m.takeFrame().held[0] & ACT_POWERUP) != 0);
    m.touchEvent(3, TouchPhase::Up, nx, ny);
}

TEST_CASE("console hud: the sequels' power-up rows show the kinds owned, the selected one in view") {
    int c[16] = {};
    // Nothing owned: every row empty.
    for (int row = 0; row < 5; ++row) CHECK(hudPackedKind(c, 16, -1, 5, row) == -1);
    // Three kinds: in kind order from the first row, the rest empty.
    c[1] = 2;
    c[4] = 1;
    c[9] = 3;
    CHECK(hudPackedFirst(c, 16, 4, 5) == 0);
    CHECK(hudPackedKind(c, 16, 4, 5, 0) == 1);
    CHECK(hudPackedKind(c, 16, 4, 5, 1) == 4);
    CHECK(hudPackedKind(c, 16, 4, 5, 2) == 9);
    CHECK(hudPackedKind(c, 16, 4, 5, 3) == -1);
    CHECK(hudPackedKind(c, 16, 4, 5, 5) == -1);
    // Eight kinds, five rows: the window keeps the selected kind in view (in the middle row where it
    // can) and never runs past either end.
    for (int k : {0, 2, 3, 5, 6}) c[k] = 1;
    const int owned[8] = {0, 1, 2, 3, 4, 5, 6, 9};
    for (int si = 0; si < 8; ++si) {
        CAPTURE(si);
        const int sel = owned[si];
        const int first = hudPackedFirst(c, 16, sel, 5);
        CHECK(first >= 0);
        CHECK(first <= 3);
        bool seen = false;
        for (int row = 0; row < 5; ++row) {
            const int k = hudPackedKind(c, 16, sel, 5, row);
            CHECK(k == owned[first + row]);
            seen = seen || k == sel;
        }
        CHECK(seen);
    }
    CHECK(hudPackedFirst(c, 16, 0, 5) == 0);
    CHECK(hudPackedFirst(c, 16, 3, 5) == 1);
    CHECK(hudPackedFirst(c, 16, 9, 5) == 3);
}

TEST_CASE("console hud: the tiles are the touch controls") {
    const TouchLayout T = computeTouchLayout(320, 240, TouchLayoutOptions::forHudTiles320x240(GameId::AirStrike3D));
    REQUIRE(T.tileCount == 11); // pause, weapon, 5 missiles, 4 power-ups
    for (int b = 0; b < kTouchButtonCount; ++b) CHECK(T.circles[b].r == 0);
    // The tiles are the HUD's rectangles at 0.4.
    const std::vector<HudElementRect> r = hudElementRects(hudLayout(GameId::AirStrike3D, HudTarget::Bottom320x240));
    int seen = 0;
    for (int i = 0; i < T.tileCount; ++i) {
        const TouchTile& t = T.tiles[i];
        CHECK(t.w >= 36);
        CHECK(t.h >= 36);
        CHECK(T.tileAt((t.x + t.w * 0.5f) / 320.0f, (t.y + t.h * 0.5f) / 240.0f) == i);
        for (const HudElementRect& e : r)
            if (t.x == doctest::Approx(e.x * 0.4f) && t.y == doctest::Approx(e.y * 0.4f) && t.w == doctest::Approx(e.w * 0.4f)) ++seen;
    }
    CHECK(seen == T.tileCount);
    // The health bar and the reminder are not tiles.
    CHECK(T.tileAt(100.0f / 320, 18.0f / 240) == -1);
    CHECK(T.tileAt(40.0f / 320, 200.0f / 240) == -1);
    // The sequels (R4.8): their tiles too, five packed power-up rows.
    for (GameId g : {GameId::AirStrike2, GameId::GulfThunder}) {
        const TouchLayout B = computeTouchLayout(320, 240, TouchLayoutOptions::forHudTiles320x240(g));
        CHECK(B.tileCount == 12);
        CHECK(B.circles[static_cast<int>(TouchButton::Missile)].r == 0);
        int packed = 0;
        for (int i = 0; i < B.tileCount; ++i) {
            CHECK(B.tiles[i].w >= 36);
            CHECK(B.tiles[i].h >= 36);
            if (B.tiles[i].kind == TouchTileKind::PowerUp) {
                CHECK(B.tiles[i].packed);
                CHECK(B.tiles[i].rows == 5);
                ++packed;
            }
        }
        CHECK(packed == 5);
    }

    auto tileOf = [&](TouchTileKind k, int index) {
        for (int i = 0; i < T.tileCount; ++i)
            if (T.tiles[i].kind == k && T.tiles[i].index == index) return i;
        return -1;
    };
    auto centre = [&](int i, float& nx, float& ny) {
        nx = (T.tiles[i].x + T.tiles[i].w * 0.5f) / 320.0f;
        ny = (T.tiles[i].y + T.tiles[i].h * 0.5f) / 240.0f;
    };
    TouchMapper m;
    m.setScreen(320, 240, TouchLayoutOptions::forHudTiles320x240(GameId::AirStrike3D));
    m.settings().autoFire = false;
    TouchInventory inv;
    inv.valid = true;
    inv.missiles[0] = 3;
    inv.missiles[1] = 6;
    inv.missiles[3] = 2;
    inv.missileSelected = 0;
    inv.powerups[0] = 2;
    inv.powerupSelected = 0;
    m.setInventory(inv);
    float nx = 0, ny = 0;
    // Pause.
    centre(tileOf(TouchTileKind::Pause, 0), nx, ny);
    m.touchEvent(1, TouchPhase::Down, nx, ny);
    CHECK(m.takeFrame().pausePressed);
    m.touchEvent(1, TouchPhase::Up, nx, ny);
    CHECK_FALSE(m.takeFrame().pausePressed);
    // The weapon box: one next-weapon press.
    centre(tileOf(TouchTileKind::Weapon, 0), nx, ny);
    m.touchEvent(2, TouchPhase::Down, nx, ny);
    CHECK((m.takeFrame().held[0] & ACT_NEXT_WEAPON) != 0);
    CHECK((m.takeFrame().held[0] & ACT_NEXT_WEAPON) == 0);
    m.touchEvent(2, TouchPhase::Up, nx, ny);
    // An empty slot: nothing.
    centre(tileOf(TouchTileKind::Missile, 2), nx, ny);
    m.touchEvent(3, TouchPhase::Down, nx, ny);
    CHECK(m.takeFrame().held[0] == 0);
    m.touchEvent(3, TouchPhase::Up, nx, ny);
    CHECK(m.takeFrame().held[0] == 0);
    // Missile 3 (selected: 0): next-missile presses on alternate steps until the world's
    // selection is 3 (the world here: nextOwnedIndex on the presses), no missile fired.
    centre(tileOf(TouchTileKind::Missile, 3), nx, ny);
    m.touchEvent(4, TouchPhase::Down, nx, ny);
    CHECK(m.pressedTile() == tileOf(TouchTileKind::Missile, 3));
    m.touchEvent(4, TouchPhase::Up, nx, ny);
    CHECK(m.pressedTile() == -1);
    int presses = 0;
    bool last = false;
    for (int step = 0; step < 20; ++step) {
        const FrameInput f = m.takeFrame();
        CHECK((f.held[0] & ACT_MISSILE) == 0);
        const bool now = (f.held[0] & ACT_NEXT_MISSILE) != 0;
        if (now && !last) { // the world acts on the press
            ++presses;
            inv.missileSelected = nextOwnedIndex(inv.missiles, 5, inv.missileSelected);
            m.setInventory(inv);
        }
        CHECK_FALSE((now && last)); // released between presses
        last = now;
    }
    CHECK(inv.missileSelected == 3);
    CHECK(presses == 2); // 0 -> 1 -> 3
    // A tap on the selected kind uses it while the finger is down.
    m.touchEvent(5, TouchPhase::Down, nx, ny);
    CHECK((m.takeFrame().held[0] & ACT_MISSILE) != 0);
    CHECK((m.takeFrame().held[0] & ACT_MISSILE) != 0);
    m.touchEvent(5, TouchPhase::Up, nx, ny);
    CHECK((m.takeFrame().held[0] & ACT_MISSILE) == 0);
    // The selected power-up: used.
    centre(tileOf(TouchTileKind::PowerUp, 0), nx, ny);
    m.touchEvent(6, TouchPhase::Down, nx, ny);
    CHECK((m.takeFrame().held[0] & ACT_POWERUP) != 0);
    m.touchEvent(6, TouchPhase::Up, nx, ny);
    // Beside the tiles: nothing (no drag, no fire).
    m.touchEvent(7, TouchPhase::Down, 100.0f / 320, 18.0f / 240);
    CHECK((m.takeFrame().held[0] & ~0u) == 0);
    m.touchEvent(7, TouchPhase::Up, 100.0f / 320, 18.0f / 240);
}

// The console HUD drawn with the console text (as the 3DS draws it): every quad inside an element
// rectangle (or the pad reminder's room), on whole pixels, the score's ten digits inside its field.
// AS3D_HUD_DUMP=<dir> writes the 320x240 picture (as3d-bottom-console.png).
TEST_CASE("console hud: the fullest HUD draws inside its rectangles, on whole pixels") {
  AS3D_REQUIRE_GLES();
  for (GameId game : {GameId::AirStrike3D, GameId::AirStrike2, GameId::GulfThunder}) {
    REQUIRE_GAME_VFS(gv, game);
    struct ConsoleText {
        ConsoleText() { UiAssets::setConsoleTextDefault(true); }
        ~ConsoleText() { UiAssets::setConsoleTextDefault(false); }
    } consoleText;
    UiAssets a;
    a.hudTarget = HudTarget::Bottom320x240;
    std::string err;
    REQUIRE_MESSAGE(a.load(gv.vfs, &err, game), err);
    REQUIRE(a.consoleText);
    const HudLayout& L = a.hudLayout();
    HudState st;
    HudPlayer& p = st.players[0];
    p.lives = 5;
    p.score = 9876543210LL; // ten digits
    p.health = 300;
    const int m[kMissileTypes] = {3, 6, 0, 2, 0};
    for (int t = 0; t < kMissileTypes; t++) p.missiles[t] = m[t];
    p.missileSelected = 1;
    p.powerups[0] = 2;
    p.powerups[1] = 1;
    p.powerupSelected = 0;
    if (game != GameId::AirStrike3D) { // eight kinds of ten: the window of five rows
        for (int k : {3, 4, 6, 7, 8, 9}) p.powerups[k] = 1;
        p.powerupSelected = 7;
    }
    p.weapon = 1;
    p.upgrades[1] = 2;
    st.levelName = "Mission 1: Tutorial";
    st.levelTime = 3.0f; // not drawn here: the top screen's
    const std::vector<HudElementRect> rects = hudElementRects(L);
    Renderer2D r;
    REQUIRE_MESSAGE(r.init(&err), err);
    RenderTarget target;
    REQUIRE(target.create(320, 240, 0));
    target.bind();
    clear({0, 0, 0, 1}, true);
    r.begin(320, 240);
    drawHud(r, a, st);
    CHECK(r.dropped() == 0);
    REQUIRE(r.quads().size() > 30u);
    for (const Quad& q : r.quads()) {
        INFO("quad " << q.x << "," << q.y << " " << q.w << "x" << q.h);
        bool inside = false;
        for (const HudElementRect& e : rects)
            inside = inside || (q.x >= e.x - 0.01f && q.y >= e.y - 2.5f && q.x + q.w <= e.x + e.w + 0.01f &&
                                q.y + q.h <= e.y + e.h + 2.5f);
        // A glyph's quad covers its whole cell (wider than its advance): its left edge counts.
        const bool glyph = q.texture == &a.font || q.texture == &a.fontSmall || q.texture == &a.fontAlpha;
        if (glyph) {
            inside = false;
            for (const HudElementRect& e : rects)
                inside = inside || (q.x >= e.x - 0.01f && q.x < e.x + e.w && q.y >= e.y - 2.5f && q.y < e.y + e.h);
        }
        CHECK(inside);
        // Whole pixels.
        CHECK(std::fabs(q.x * 0.4f - std::round(q.x * 0.4f)) < 0.01f);
        CHECK(std::fabs(q.y * 0.4f - std::round(q.y * 0.4f)) < 0.01f);
    }
    r.flush();
    if (const char* dir = std::getenv("AS3D_HUD_DUMP")) {
        Image img;
        REQUIRE(target.readPixels(img));
        writePng((std::string(dir) + "/" + L.name + "-console.png").c_str(), img);
    }
  }
}
