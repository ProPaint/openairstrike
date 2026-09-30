// The game selector (docs/spec/issues/163): which games it lists and when it is skipped, the
// remembered choice in launcher.bin (a corrupt file is ignored), the save summary read without
// touching the save, the selector screen operated by pointer only, by keyboard, inside the
// display cutouts, the "Change game" entry of both main menus (the first game's menu unchanged
// without it), and switching between the two games five times with the GL object counts back
// where they were (run it under AddressSanitizer too: docs/spec/issues/163). Never uses the
// user data directory: files go under build/launcher_test_tmp.
//
// The game stack and the selector screen (apps/game) are compiled in here.
#include "doctest.h"

#include <GLES3/gl3.h>

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../game/game_stack.cpp"
#include "../game/launcher_screen.cpp"
#include "as3d/frontend.h"
#include "as3d/game_data.h"
#include "as3d/launcher.h"
#include "as3d/profile.h"
#include "test_data.h"
#include "ui_test_util.h"

using namespace as3d;
using namespace as3d_game;
namespace fs = std::filesystem;
namespace keys = as3d::ui::keys;

namespace {

const GameProfile& kAs3d = gameProfile(GameId::AirStrike3D);
const GameProfile& kAs2 = gameProfile(GameId::AirStrike2);
const GameProfile& kGulf = gameProfile(GameId::GulfThunder);

std::string tmpDir(const std::string& name) {
    const std::string d = std::string(AS3D_REPO_ROOT) + "/build/launcher_test_tmp/" + name;
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return d;
}

std::string readBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void writeBytes(const std::string& path, const std::string& bytes) {
    std::ofstream f(path, std::ios::binary);
    f << bytes;
}

// FNV-1a 64 of a file's bytes, "" when it does not exist.
std::string hashOf(const std::string& path) {
    if (!fs::exists(path)) return std::string();
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char c : readBytes(path)) h = (h ^ c) * 0x100000001b3ull;
    return std::to_string(h) + ":" + std::to_string(fs::file_size(path));
}

std::vector<std::string> listing(const std::string& dir) {
    std::vector<std::string> out;
    for (const auto& e : fs::recursive_directory_iterator(dir)) out.push_back(e.path().string());
    std::sort(out.begin(), out.end());
    return out;
}

// The fake game behind a bare front end.
struct Host : ui::GameHost {
    int changes = 0, saves = 0;
    void startMission(const ui::MissionStart&) override {}
    void loadAttract() override {}
    void setPaused(bool) override {}
    void clearPlayerActions() override {}
    void settingsChanged(const Settings&) override {}
    void saveProfile(const Profile&) override { ++saves; }
    void quit() override {}
    void changeGame() override { ++changes; }
};

struct MenuRig {
    Host host;
    Profile profile;
    ui::Frontend fe;
    MenuRig(const GameProfile* game, bool changeGame, bool touch = false)
        : fe(host, profile, [&] {
              ui::FrontendContent c;
              c.game = game;
              c.changeGame = changeGame;
              return c;
          }(), ui::Texts{}) {
        fe.setTouchMode(touch);
        fe.boot();
    }
    const ui::MenuItem* item(int id) {
        ui::Menu* m = fe.menus().top();
        if (!m) return nullptr;
        for (const ui::MenuItem& it : m->items)
            if (it.id == id && !it.hidden()) return &it;
        return nullptr;
    }
};

std::vector<ui::GameCard> cards(int n) {
    const GameProfile* g[3] = {&kAs3d, &kAs2, &kGulf};
    std::vector<ui::GameCard> out;
    for (int i = 0; i < n; ++i) {
        ui::GameCard c;
        c.key = g[i]->key;
        c.title = g[i]->title;
        c.version = g[i]->version;
        c.saveLines = {"No save yet"};
        c.marquee = gameMarquee(g[i]->id);
        out.push_back(c);
    }
    return out;
}

void tapRect(ui::GameSelector& s, const ui::RectF& r) {
    s.update(0.016f, ui::UiInput().tap(r.x + r.w * 0.5f, r.y + r.h * 0.5f));
}

} // namespace

// ---------------------------------------------------------------------------------------
// Which games, and whether the selector shows at all.
// ---------------------------------------------------------------------------------------
TEST_CASE("launcher plan: the playable games present, skipped with one, forced wins, last choice preselects") {
    // Two playable games: the selector, in GameId order whatever the order found.
    LaunchPlan p = planLaunch({&kAs2, &kAs3d}, "", "", false);
    CHECK(p.showSelector);
    REQUIRE(p.offered.size() == 2);
    CHECK(p.offered[0] == &kAs3d);
    CHECK(p.offered[1] == &kAs2);
    CHECK(p.preselected == 0);
    // The last choice preselects, never starts.
    p = planLaunch({&kAs3d, &kAs2}, "", "as2", false);
    CHECK(p.showSelector);
    CHECK(p.preselected == 1);
    CHECK(p.startKey.empty());
    // An unknown or absent last choice: the first card.
    CHECK(planLaunch({&kAs3d, &kAs2}, "", "gulf", false).preselected == 0);
    // Gulf Thunder plays too (package F2): the three games are offered, in GameId order.
    p = planLaunch({&kGulf, &kAs3d}, "", "", false);
    CHECK(p.showSelector);
    REQUIRE(p.offered.size() == 2);
    CHECK(p.offered[0] == &kAs3d);
    CHECK(p.offered[1] == &kGulf);
    p = planLaunch({&kAs3d, &kAs2, &kGulf}, "", "gulf", true);
    CHECK(p.showSelector);
    CHECK(p.offered.size() == 3);
    CHECK(p.preselected == 2);
    CHECK(planLaunch({&kAs3d, &kAs2, &kGulf}, "", "", false).offered.size() == 3);
    // One game: it starts, whatever the last choice says.
    p = planLaunch({&kAs2}, "", "as3d", false);
    CHECK_FALSE(p.showSelector);
    CHECK(p.startKey == "as2");
    p = planLaunch({&kAs3d}, "", "", false);
    CHECK(p.startKey == "as3d");
    // A forced game (--game, AS3D_GAME, ?game=, the Android extra): no selector.
    p = planLaunch({&kAs3d, &kAs2}, "as2", "as3d", false);
    CHECK_FALSE(p.showSelector);
    CHECK(p.startKey == "as2");
    // No game present: no selector and nothing to start.
    p = planLaunch({}, "", "", false);
    CHECK_FALSE(p.showSelector);
    CHECK(p.startKey.empty());
    p = planLaunch({&kGulf}, "", "", false);
    CHECK_FALSE(p.showSelector);
    CHECK(p.startKey == "gulf");
    CHECK(gameIsPlayable(kAs3d));
    CHECK(gameIsPlayable(kAs2));
    CHECK(gameIsPlayable(kGulf));
    CHECK(playableGameKeys() == std::vector<std::string>{"as3d", "as2", "gulf"});
}

// ---------------------------------------------------------------------------------------
// launcher.bin
// ---------------------------------------------------------------------------------------
TEST_CASE("launcher.bin: round trip, atomic write, every kind of corrupt file ignored") {
    const std::string dir = tmpDir("choice");
    const std::string path = dir + "/launcher.bin";
    std::string key = "x";
    CHECK_FALSE(readLauncherChoice(path, &key)); // absent
    CHECK(key.empty());
    REQUIRE(writeLauncherChoice(path, "as2"));
    CHECK_FALSE(fs::exists(path + ".tmp"));
    REQUIRE(readLauncherChoice(path, &key));
    CHECK(key == "as2");
    REQUIRE(writeLauncherChoice(path, "as3d"));
    REQUIRE(readLauncherChoice(path, &key));
    CHECK(key == "as3d");
    CHECK_FALSE(writeLauncherChoice(path, "AS2"));      // not a key
    CHECK_FALSE(writeLauncherChoice(path, ""));
    CHECK_FALSE(writeLauncherChoice(dir + "/no/such/dir/launcher.bin", "as2"));
    REQUIRE(readLauncherChoice(path, &key));
    CHECK(key == "as3d");                              // unchanged by the failed writes

    const std::vector<u8> good = serializeLauncherChoice("as2");
    auto bad = [&](std::vector<u8> b, const char* what) {
        INFO(what);
        writeBytes(path, std::string(b.begin(), b.end()));
        std::string k = "x";
        CHECK_FALSE(readLauncherChoice(path, &k));
        CHECK(k.empty());
    };
    std::vector<u8> b = good;
    b[0] = 'X';
    bad(b, "magic");
    b = good;
    b[8] = 2;
    bad(b, "version");
    b = good;
    b[13] = 'b'; // "bs2": CRC mismatch
    bad(b, "crc");
    b = good;
    b.pop_back();
    bad(b, "truncated");
    b = good;
    b.push_back(0);
    bad(b, "trailing byte");
    b = good;
    b[12] = 40;
    bad(b, "key length");
    bad(std::vector<u8>(), "empty");
    bad(std::vector<u8>(4000, 0x41), "large");
    bad(serializeLauncherChoice("zz9"), "a key that names no game");
    b = serializeLauncherChoice("As2");
    bad(b, "characters");
    // And a good file reads again.
    writeBytes(path, std::string(good.begin(), good.end()));
    REQUIRE(readLauncherChoice(path, &key));
    CHECK(key == "as2");
}

// ---------------------------------------------------------------------------------------
// Save summaries: read, never written.
// ---------------------------------------------------------------------------------------
TEST_CASE("save summary: read through the profile code, the files untouched") {
    const std::string dir = tmpDir("saves");
    // No save.
    SaveSummary s = readSaveSummary(dir + "/as2/profile.bin", "", kAs2);
    CHECK_FALSE(s.found);
    CHECK(describeSave(s) == "No save yet");
    CHECK(listing(dir).empty()); // no directory made

    // A save with progress and a high score of the player's.
    fs::create_directories(dir + "/as3d");
    Profile p;
    p.progress = Progress::defaults(kAs3d.rules);
    for (int i = 0; i < 7; ++i) p.progress.missionUnlocked[i] = true;
    p.progress.insert("Pilot", 123450, 3);
    REQUIRE(saveProfileFile(dir + "/as3d/profile.bin", p, nullptr, "as3d"));
    const std::vector<std::string> before = listing(dir);
    const std::string hash = hashOf(dir + "/as3d/profile.bin");
    const auto mtime = fs::last_write_time(dir + "/as3d/profile.bin");
    s = readSaveSummary(dir + "/as3d/profile.bin", dir + "/profile.bin", kAs3d);
    CHECK(s.found);
    CHECK(s.readable);
    CHECK(s.missionsUnlocked == 7);
    CHECK(s.missionCount == 20);
    CHECK(s.hasScore);
    CHECK(s.bestScore == 123450);
    CHECK(describeSave(s) == "7 of 20 missions open, best 123 450");
    CHECK(hashOf(dir + "/as3d/profile.bin") == hash);
    CHECK(fs::last_write_time(dir + "/as3d/profile.bin") == mtime);
    CHECK(listing(dir) == before);

    // A fresh table only: no best score of the player's.
    Profile fresh;
    fresh.progress = Progress::defaults(kAs2.rules, 1);
    fs::create_directories(dir + "/as2");
    REQUIRE(saveProfileFile(dir + "/as2/profile.bin", fresh, nullptr, "as2"));
    s = readSaveSummary(dir + "/as2/profile.bin", "", kAs2);
    CHECK(s.readable);
    CHECK_FALSE(s.hasScore);
    CHECK(s.missionsUnlocked == 2);
    CHECK(describeSave(s) == "2 of 18 missions open");

    // Another game's file in the place: found, not readable, left alone.
    const std::string h2 = hashOf(dir + "/as2/profile.bin");
    s = readSaveSummary(dir + "/as2/profile.bin", "", kAs3d);
    CHECK(s.found);
    CHECK_FALSE(s.readable);
    CHECK(hashOf(dir + "/as2/profile.bin") == h2);

    // The first game's save from before issue 160 (version 1 at the old place): read where it
    // is, not migrated, no new file.
    const std::string legacyDir = tmpDir("legacy");
    std::vector<u8> v2 = serializeProfile(p, "as3d");
    // Version 1: the same header without the key (issue 160); CRC over the payload.
    const size_t keyLen = v2[20];
    std::vector<u8> payload(v2.begin() + 21 + static_cast<long>(keyLen), v2.end());
    std::vector<u8> v1(v2.begin(), v2.begin() + 20);
    v1[8] = 1;
    u32 c = 0xFFFFFFFFu;
    for (u8 x : payload) {
        c ^= x;
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    c = ~c;
    for (int k = 0; k < 4; ++k) v1[16 + k] = static_cast<u8>(c >> (8 * k));
    v1.insert(v1.end(), payload.begin(), payload.end());
    writeBytes(legacyDir + "/profile.bin", std::string(v1.begin(), v1.end()));
    const std::vector<std::string> legacyBefore = listing(legacyDir);
    const std::string lh = hashOf(legacyDir + "/profile.bin");
    s = readSaveSummary(legacyDir + "/as3d/profile.bin", legacyDir + "/profile.bin", kAs3d);
    CHECK(s.found);
    CHECK(s.readable);
    CHECK(s.missionsUnlocked == 7);
    CHECK(hashOf(legacyDir + "/profile.bin") == lh);
    CHECK(listing(legacyDir) == legacyBefore);
    // A sequel never looks at the old place.
    CHECK_FALSE(readSaveSummary(legacyDir + "/as2/profile.bin", legacyDir + "/profile.bin", kAs2).found);
}

// ---------------------------------------------------------------------------------------
// The selector screen.
// ---------------------------------------------------------------------------------------
TEST_CASE("selector: operable by pointer only (touch), one tap on a card plays it") {
    for (int n = 1; n <= 3; ++n) {
        for (int pick = 0; pick < n; ++pick) {
            ui::GameSelector s(cards(n), 0);
            s.setTouchMode(true);
            CHECK(s.chosen() == -1);
            tapRect(s, s.cardRect(pick));
            CHECK(s.chosen() == pick);
        }
    }
    // Play starts the card last touched; Exit exits.
    {
        ui::GameSelector s(cards(2), 1);
        s.setTouchMode(true);
        CHECK(s.current() == 1);
        tapRect(s, s.playRect());
        CHECK(s.chosen() == 1);
    }
    {
        ui::GameSelector s(cards(2), 0);
        tapRect(s, s.exitRect());
        CHECK(s.exitRequested());
        CHECK(s.chosen() == -1);
    }
    // A tap outside everything does nothing.
    {
        ui::GameSelector s(cards(2), 0);
        s.update(0.016f, ui::UiInput().tap(400, 50));
        s.update(0.016f, ui::UiInput().tap(400, 505));
        CHECK(s.chosen() == -1);
        CHECK_FALSE(s.exitRequested());
    }
}

TEST_CASE("selector: keyboard and mouse on the desktop") {
    ui::GameSelector s(cards(3), 0);
    CHECK(s.current() == 0);
    s.update(0.016f, ui::UiInput().key(keys::Right));
    CHECK(s.current() == 1);
    s.update(0.016f, ui::UiInput().key(keys::Right).key(keys::Right)); // stops at the last card
    CHECK(s.current() == 2);
    s.update(0.016f, ui::UiInput().key(keys::Left));
    CHECK(s.current() == 1);
    s.update(0.016f, ui::UiInput().key(keys::Enter));
    CHECK(s.chosen() == 1);

    ui::GameSelector m(cards(2), 0);
    const ui::RectF r = m.cardRect(1);
    m.update(0.016f, ui::UiInput().move(r.x + 10, r.y + 10)); // hover focuses
    CHECK(m.current() == 1);
    m.update(0.016f, ui::UiInput().press(keys::Mouse1).release(keys::Mouse1));
    CHECK(m.chosen() == 1);

    ui::GameSelector e(cards(2), 0);
    e.update(0.016f, ui::UiInput().key(keys::Escape));
    CHECK(e.exitRequested());
}

TEST_CASE("selector: cards and buttons stay inside the screen and clear of cutouts") {
    for (int n = 1; n <= 3; ++n) {
        ui::GameSelector s(cards(n), 0);
        struct Area { float l, t, r, b; } areas[] = {{0, 0, 800, 600}, {90, 0, 800, 600}, {0, 0, 700, 600}, {60, 0, 740, 600}};
        for (const Area& a : areas) {
            s.setSafeArea(a.l, a.t, a.r, a.b);
            INFO("cards " << n << ", safe " << a.l << ".." << a.r);
            float lastRight = -1;
            for (int i = 0; i < n; ++i) {
                const ui::RectF c = s.cardRect(i);
                CHECK(c.x >= a.l);
                CHECK(c.x + c.w <= a.r);
                CHECK(c.x >= 0);
                CHECK(c.x + c.w <= 800);
                CHECK(c.w >= 150);
                CHECK(c.x > lastRight); // no overlap
                lastRight = c.x + c.w;
            }
            CHECK(s.exitRect().x >= a.l);
            CHECK(s.playRect().x + s.playRect().w <= a.r);
            CHECK(s.exitRect().x + s.exitRect().w < s.playRect().x);
            // Still playable by a tap after the layout moved.
            tapRect(s, s.cardRect(n - 1));
            CHECK(s.chosen() == n - 1);
        }
    }
}

// ---------------------------------------------------------------------------------------
// The selector's look (docs/spec/issues/164): the layout at every aspect and screen mode.
// ---------------------------------------------------------------------------------------
TEST_CASE("selector look: the marquee kind of each game") {
    CHECK(gameMarquee(GameId::AirStrike3D) == ui::Marquee::Banner);      // the 3D banner of its main menu
    CHECK(gameMarquee(GameId::AirStrike2) == ui::Marquee::TitleLogo);    // the rusty logo, then the "2" emblem
    CHECK(gameMarquee(GameId::GulfThunder) == ui::Marquee::Emblem);      // its title logo
    CHECK(std::string(gameLogoPath(GameId::AirStrike2)) == "gfx\\logo\\logo.tga");
    CHECK(std::string(gameLogoPath(GameId::GulfThunder)) == "gfx\\logo\\logo_gulf.tga");
    const std::vector<ui::GameCard> c = cards(3);
    CHECK(c[0].marquee == ui::Marquee::Banner);
    CHECK(c[1].marquee == ui::Marquee::TitleLogo);
    CHECK(c[2].marquee == ui::Marquee::Emblem);
}

TEST_CASE("selector look: layout at every aspect and screen mode, for 1, 2 and 3 cards") {
    struct Size { int w, h; } sizes[] = {{800, 600}, {1024, 768}, {1600, 720}, {2400, 1080}, {1280, 800}, {1600, 1200}, {640, 480}};
    for (const Size& sz : sizes) {
        const ui::Mapping m = ui::computeMapping(sz.w, sz.h);
        for (bool fourThree : {false, true}) {
            for (int n = 1; n <= 3; ++n) {
                ui::GameSelector s(cards(n), 0);
                s.setView(m.left(), m.right(), fourThree);
                INFO(sz.w << "x" << sz.h << (fourThree ? " 4:3 " : " wide ") << n << " cards");
                const float vl = fourThree ? 0.0f : std::min(m.left(), 0.0f), vr = fourThree ? 800.0f : std::max(m.right(), 800.0f);
                const ui::RectF first = s.cardRect(0), last = s.cardRect(n - 1);
                float left = first.x, right = last.x + last.w;
                // Centred on the screen, inside it with a margin, inside the field in 4:3.
                CHECK(std::fabs((left + right) * 0.5f - 400.0f) <= 1.5f);
                CHECK(left >= vl + 20);
                CHECK(right <= vr - 20);
                CHECK(right - left <= 1100.5f);
                // One row: same size and top, equal gaps, the row centred in the band between
                // the bars' rules (no empty band above or below).
                for (int i = 0; i < n; ++i) {
                    const ui::RectF c = s.cardRect(i), mq = s.marqueeRect(i);
                    CHECK(c.y == first.y);
                    CHECK(c.w == first.w);
                    CHECK(c.h == first.h);
                    CHECK(c.w >= 200);
                    CHECK(c.w <= 380);
                    CHECK(c.y >= 102);
                    CHECK(c.y + c.h <= 498);
                    if (i > 0) CHECK(c.x - (s.cardRect(i - 1).x + s.cardRect(i - 1).w) == 18);
                    // The marquees share one size, inside their card.
                    CHECK(mq.w == s.marqueeRect(0).w);
                    CHECK(mq.h == s.marqueeRect(0).h);
                    CHECK(mq.x >= c.x);
                    CHECK(mq.x + mq.w <= c.x + c.w);
                    CHECK(mq.y >= c.y);
                    CHECK(mq.y + mq.h < c.y + c.h);
                    CHECK(mq.w / mq.h > 1.9f); // boxes at least as wide as the widest (2:1) emblem
                    CHECK(mq.w / mq.h < 2.4f);
                }
                CHECK(std::fabs((first.y - 102) - (498 - (first.y + first.h))) <= 1.5f);
                CHECK(first.h >= 250); // a card is not a sliver on any screen
                // Buttons in the bottom bar, at the row's edges.
                CHECK(s.exitRect().y >= 502);
                CHECK(s.playRect().y + s.playRect().h <= 600);
                CHECK(s.exitRect().x + s.exitRect().w < s.playRect().x);
                CHECK(s.exitRect().x >= vl + 20);
                CHECK(s.playRect().x + s.playRect().w <= vr - 20);
            }
        }
    }
    // A wide screen gives the cards at least the room the 4:3 field does, and a lone card the
    // same on both.
    {
        const ui::Mapping m = ui::computeMapping(2400, 1080);
        for (int n = 1; n <= 3; ++n) {
            ui::GameSelector a(cards(n), 0), b(cards(n), 0);
            a.setView(m.left(), m.right(), false);
            b.setView(m.left(), m.right(), true);
            CHECK(a.cardRect(0).w >= b.cardRect(0).w);
            if (n == 1) CHECK(a.cardRect(0).w == b.cardRect(0).w);
        }
    }
}

TEST_CASE("selector look: cutouts move the row, never a card past them") {
    const ui::Mapping m = ui::computeMapping(2400, 1080);
    for (int n = 1; n <= 3; ++n) {
        ui::GameSelector s(cards(n), 0);
        s.setView(m.left(), m.right(), false);
        s.setSafeArea(m.left() + 300, 0, m.right() - 60, 600); // a big notch on the left
        for (int i = 0; i < n; ++i) {
            CHECK(s.cardRect(i).x >= m.left() + 300);
            CHECK(s.cardRect(i).x + s.cardRect(i).w <= m.right() - 60);
        }
        CHECK(s.exitRect().x >= m.left() + 300);
        CHECK(s.playRect().x + s.playRect().w <= m.right() - 60);
        tapRect(s, s.marqueeRect(n - 1)); // a tap on the marquee is a tap on the card
        CHECK(s.chosen() == n - 1);
    }
}

// ---------------------------------------------------------------------------------------
// The marquees, drawn: what the window shows, pixel by pixel, over time.
// ---------------------------------------------------------------------------------------
namespace {

StackConfig stackFor(const GameProfile& g); // below

struct ShotRig {
    ui::Renderer2D r;
    RenderTarget target;
    LauncherScreen screen;
    bool ok = false;
    std::vector<const GameProfile*> games;

    bool init(const std::vector<const GameProfile*>& want, std::string* err) {
        if (!r.init(err) || !target.create(800, 600, 0)) return false;
        std::vector<LauncherEntry> entries;
        for (const GameProfile* g : want) {
            const GameData d = locateGameData(testdata::root(), *g);
            if (!d.hasExtracted) continue;
            LauncherEntry e;
            e.game = g;
            e.files = stackFor(*g).game;
            entries.push_back(e);
            games.push_back(g);
        }
        if (entries.empty()) return false;
        if (!screen.init(entries, 0, false, err)) return false;
        screen.setScreen(800, 600, SafeInsets());
        return ok = true;
    }
    void advance(float seconds) { screen.update(seconds, ui::UiInput()); }
    void shot(Image& img) {
        target.bind();
        screen.draw(r, 800, 600);
        REQUIRE(target.readPixels(img));
    }
};

// Pixels of `img` inside `box` that are not black, and the number that differ from `other`.
long lit(const Image& img, const ui::RectF& box) {
    long n = 0;
    for (int y = static_cast<int>(box.y); y < static_cast<int>(box.y + box.h); ++y)
        for (int x = static_cast<int>(box.x); x < static_cast<int>(box.x + box.w); ++x) {
            const size_t i = (static_cast<size_t>(y) * 800 + x) * 4;
            if (img.rgba[i] + img.rgba[i + 1] + img.rgba[i + 2] > 60) ++n;
        }
    return n;
}
long differ(const Image& a, const Image& b, const ui::RectF& box, int tolerance = 8) {
    long n = 0;
    for (int y = static_cast<int>(box.y); y < static_cast<int>(box.y + box.h); ++y)
        for (int x = static_cast<int>(box.x); x < static_cast<int>(box.x + box.w); ++x) {
            const size_t i = (static_cast<size_t>(y) * 800 + x) * 4;
            int d = 0;
            for (int c = 0; c < 3; ++c) d = std::max(d, std::abs(int(a.rgba[i + c]) - int(b.rgba[i + c])));
            if (d > tolerance) ++n;
        }
    return n;
}

} // namespace

TEST_CASE("selector look: each marquee draws the game's own title and moves as the title screen's does") {
    AS3D_REQUIRE_DATA();
    AS3D_REQUIRE_GLES();
    ShotRig rig;
    std::string err;
    if (!rig.init({&kAs3d, &kAs2, &kGulf}, &err)) {
        std::fprintf(stderr, "SKIPPED (no game data for the selector's marquees: %s): %s\n", err.c_str(), __FILE__);
        return;
    }
    Image t0, t1, t2;
    rig.advance(0.1f);
    rig.shot(t0);
    rig.advance(1.3f);
    rig.shot(t1);
    rig.advance(1.7f);
    rig.shot(t2);
    for (size_t i = 0; i < rig.games.size(); ++i) {
        const GameProfile& g = *rig.games[i];
        const ui::RectF box = rig.screen.selector().marqueeRect(static_cast<int>(i));
        INFO(g.key << ", marquee " << box.x << "," << box.y << " " << box.w << "x" << box.h);
        // Something of the title is on the stage (the marquee is not the text fallback: the text
        // fallback would be 2.6 scale glyphs, far more lit pixels than a logo's).
        const long l0 = lit(t0, box);
        CHECK(l0 > 400);
        CHECK(l0 < static_cast<long>(box.w * box.h * 0.6));
        // It moves: the clouds and the emblem (AirStrike 2, Gulf Thunder), the mesh's pitch
        // and yaw (AirStrike 3D).
        CHECK(differ(t0, t1, box) > 60);
        CHECK(differ(t1, t2, box) > 60);
        // The stage is the marquee's own: nothing of it spills past its frame (4 pixels of stage
        // and the frame line), whatever the mesh or the emblem's swell do.
        const ui::RectF wide{box.x - 8, box.y - 8, box.w + 16, box.h + 16};
        const ui::RectF ring[4] = {{wide.x, wide.y, wide.w, 3}, {wide.x, wide.y + wide.h - 3, wide.w, 3},
                                   {wide.x, wide.y, 3, wide.h}, {wide.x + wide.w - 3, wide.y, 3, wide.h}};
        for (const ui::RectF& edge : ring) CHECK(differ(t0, t2, edge, 2) == 0);
    }
    // The pulsing focus: the current card's frame is brighter at one moment than another, the
    // card beside it does not pulse.
    {
        const ui::RectF c0 = rig.screen.selector().cardRect(0);
        long lo = 1 << 30, hi = 0;
        for (int k = 0; k < 12; ++k) {
            rig.advance(0.09f);
            Image im;
            rig.shot(im);
            long sum = 0;
            for (int x = static_cast<int>(c0.x); x < static_cast<int>(c0.x + c0.w); ++x) {
                const size_t i = (static_cast<size_t>(c0.y) * 800 + x) * 4;
                sum += im.rgba[i] + im.rgba[i + 1];
            }
            lo = std::min(lo, sum);
            hi = std::max(hi, sum);
        }
        CHECK(hi > lo * 1.12);
    }
}

TEST_CASE("selector look: the web page's marquee loops are exact (loop fit)") {
    AS3D_REQUIRE_DATA();
    AS3D_REQUIRE_GLES();
    ShotRig rig;
    std::string err;
    if (!rig.init({&kAs3d, &kAs2, &kGulf}, &err)) {
        std::fprintf(stderr, "SKIPPED (no game data for the selector's marquees: %s): %s\n", err.c_str(), __FILE__);
        return;
    }
    rig.screen.setLoopFit(true);
    // The banner loops after 2 pi seconds of the selector's clock, the logos after 4 pi.
    Image a, b;
    rig.advance(0.5f);
    rig.shot(a);
    rig.advance(6.28318531f);
    rig.shot(b);
    for (size_t i = 0; i < rig.games.size(); ++i) {
        if (rig.games[i]->id != GameId::AirStrike3D) continue;
        INFO("the banner");
        CHECK(differ(a, b, rig.screen.selector().marqueeRect(static_cast<int>(i)), 6) < 20);
    }
    rig.advance(6.28318531f);
    Image c;
    rig.shot(c);
    for (size_t i = 0; i < rig.games.size(); ++i) {
        if (rig.games[i]->id == GameId::AirStrike3D) continue;
        INFO(rig.games[i]->key);
        CHECK(differ(a, c, rig.screen.selector().marqueeRect(static_cast<int>(i)), 6) < 20);
    }
}

// ---------------------------------------------------------------------------------------
// "Change game" in the main menus.
// ---------------------------------------------------------------------------------------
TEST_CASE("main menus: \"Change game\" only with several games; the first game's menu otherwise unchanged") {
    for (bool touch : {false, true}) {
        MenuRig one(nullptr, false, touch), two(nullptr, true, touch);
        REQUIRE(one.fe.topScreen() == ui::Screen::MainMenu);
        const ui::Menu* a = one.fe.menus().top();
        const ui::Menu* b = two.fe.menus().top();
        REQUIRE(a);
        REQUIRE(b);
        CHECK(one.item(ui::kChangeGameItem) == nullptr);
        REQUIRE(two.item(ui::kChangeGameItem) != nullptr);
        // The original items are the same, in the same places.
        REQUIRE(a->items.size() == 5);
        REQUIRE(b->items.size() == 6);
        for (size_t i = 0; i < a->items.size(); ++i) {
            CHECK(a->items[i].id == b->items[i].id);
            CHECK(a->items[i].hit.x == b->items[i].hit.x);
            CHECK(a->items[i].hit.y == b->items[i].hit.y);
            CHECK(a->items[i].hit.w == b->items[i].hit.w);
            CHECK(a->items[i].hit.h == b->items[i].hit.h);
            // The new entry overlaps none of them.
            const ui::RectF& h = a->items[i].hit;
            const ui::RectF& c = ui::kChangeGameRect;
            CHECK((c.y >= h.y + h.h || c.y + c.h <= h.y || c.x >= h.x + h.w || c.x + c.w <= h.x));
        }
        // A tap on it saves the profile and asks the host.
        const ui::MenuItem* it = two.item(ui::kChangeGameItem);
        const int saves = two.host.saves;
        two.fe.update(0.016f, ui::UiInput().tap(it->hit.x + it->hit.w / 2, it->hit.y + it->hit.h / 2));
        CHECK(two.host.changes == 1);
        CHECK(two.host.saves == saves + 1);
    }
    // The plain front end: in the bottom bar's left slot.
    MenuRig plainOne(&kGulf, false, true), plainTwo(&kGulf, true, true); // AirStrike 2: as2_frontend_test.cpp
    CHECK(plainOne.item(ui::kChangeGameItem) == nullptr);
    const ui::MenuItem* it = plainTwo.item(ui::kChangeGameItem);
    REQUIRE(it != nullptr);
    CHECK(it->hit.y >= 500);
    plainTwo.fe.update(0.016f, ui::UiInput().tap(it->hit.x + 5, it->hit.y + 5));
    CHECK(plainTwo.host.changes == 1);
}

TEST_CASE("main menu headless: the first game's pixels unchanged except where \"Change game\" is") {
    AS3D_REQUIRE_DATA();
    AS3D_REQUIRE_FIRST_GAME("menu pictures");
    AS3D_REQUIRE_GLES();
    Vfs vfs;
    REQUIRE(uitest::mountPaks(vfs));
    ui::UiAssets assets;
    std::string err;
    REQUIRE_MESSAGE(assets.load(vfs, &err), err);
    ui::Renderer2D r;
    REQUIRE_MESSAGE(r.init(&err), err);
    RenderTarget target;
    REQUIRE(target.create(800, 600, 0));
    auto render = [&](bool changeGame, Image& img) {
        MenuRig rig(nullptr, changeGame);
        rig.fe.menus().setPointer(-50, -50);
        rig.fe.update(0.5f, {});
        target.bind();
        clear({40 / 255.0f, 60 / 255.0f, 80 / 255.0f, 1}, true);
        r.begin(800, 600);
        rig.fe.draw(r, assets);
        r.flush();
        REQUIRE(target.readPixels(img));
    };
    Image one, two;
    render(false, one);
    render(true, two);
    REQUIRE(one.rgba.size() == two.rgba.size());
    const ui::RectF c = ui::kChangeGameRect;
    long outside = 0, inside = 0;
    for (int y = 0; y < 600; ++y)
        for (int x = 0; x < 800; ++x) {
            const size_t i = (static_cast<size_t>(y) * 800 + x) * 4;
            const bool diff = std::memcmp(&one.rgba[i], &two.rgba[i], 3) != 0;
            const bool in = x >= c.x - 2 && x < c.x + c.w + 2 && y >= c.y - 2 && y < c.y + c.h + 2;
            (in ? inside : outside) += diff ? 1 : 0;
        }
    CHECK(outside == 0);
    CHECK(inside > 300); // the box and its caption
}

// ---------------------------------------------------------------------------------------
// Switching games: the whole game torn down and the other built, five times each way.
// ---------------------------------------------------------------------------------------
namespace {

struct GlCounts {
    int textures = 0, buffers = 0, arrays = 0, programs = 0, framebuffers = 0;
    bool operator==(const GlCounts& o) const {
        return textures == o.textures && buffers == o.buffers && arrays == o.arrays && programs == o.programs &&
               framebuffers == o.framebuffers;
    }
};

// Live GL objects of the current context, by probing names (the engine counts none itself).
GlCounts countGl() {
    GlCounts c;
    for (GLuint n = 1; n < 40000; ++n) {
        c.textures += glIsTexture(n) ? 1 : 0;
        c.buffers += glIsBuffer(n) ? 1 : 0;
        c.arrays += glIsVertexArray(n) ? 1 : 0;
        c.programs += glIsProgram(n) ? 1 : 0;
        c.framebuffers += glIsFramebuffer(n) ? 1 : 0;
    }
    return c;
}

GraphicsContext* switchContext() {
    static std::unique_ptr<GraphicsContext> ctx = [] {
        GraphicsConfig cfg;
        cfg.width = 64;
        cfg.height = 64;
        cfg.headless = true;
        cfg.title = "as3d_tests_launcher";
        return createGraphicsContext(cfg);
    }();
    if (ctx) ctx->makeCurrent();
    return ctx.get();
}

StackConfig stackFor(const GameProfile& g) {
    const GameData d = locateGameData(testdata::root(), g);
    StackConfig sc;
    sc.game.dataRoot = testdata::root();
    sc.game.game = &g;
    sc.game.extractedDir = d.extractedDir;
    sc.game.startLevel = false;
    sc.game.levelFlow = false;
    sc.frontend = true;
    sc.flow.attract = 1;
    sc.flow.showLogo = false;
    sc.flow.touch = true;
    sc.flow.changeGame = true;
    sc.flow.settingsXml = d.dataDir + "/Settings.xml";
    sc.flow.textsPath = d.extractedDir + "/" + g.textsFile;
    // profilePath stays empty: nothing is saved.
    sc.nullAudio = true;
    return sc;
}

// A few seconds of the game: the front end boots onto its attract level, a frame is drawn.
void play(GameStack& s, RenderTarget& target) {
    s.flow->boot();
    for (int f = 0; f < 90; ++f) {
        s.flow->uiFrame(1.0f / 60.0f, ui::UiInput());
        s.flow->step(FrameInput());
        if (s.session->hasLevel()) s.audio.drain(s.session->world());
        s.audio.pump(735);
    }
    target.bind();
    s.flow->draw(320, 240);
}

} // namespace

TEST_CASE("switching games: five times each way, the GL objects back to the same counts") {
    const GameData a = locateGameData(testdata::root(), kAs3d), b = locateGameData(testdata::root(), kAs2);
    if (!a.hasExtracted || !b.hasExtracted) {
        std::fprintf(stderr, "SKIPPED (the switch test needs the extracted data of as3d and as2): %s\n", __FILE__);
        return;
    }
    if (testdata::gameKey() != "as3d") {
        std::fprintf(stderr, "SKIPPED (the switch test runs in the as3d pass): %s\n", __FILE__);
        return;
    }
    if (!switchContext()) {
        std::fprintf(stderr, "SKIPPED (no headless GLES context available): %s\n", __FILE__);
        return;
    }
    RenderTarget target;
    REQUIRE(target.create(320, 240, 4));
    ui::Renderer2D overlay;
    std::string err;
    REQUIRE_MESSAGE(overlay.init(&err), err);
    std::vector<LauncherEntry> entries;
    for (const GameProfile* g : {&kAs3d, &kAs2}) {
        LauncherEntry e;
        e.game = g;
        e.files = stackFor(*g).game;
        entries.push_back(e);
    }

    GameStack stack;
    GlCounts baseline;
    const GameProfile* order[2] = {&kAs3d, &kAs2};
    for (int round = 0; round < 10; ++round) {
        const GameProfile& g = *order[round % 2];
        INFO("round " << round << ", " << std::string(g.key));
        // The selector between games, as the window shows it.
        {
            LauncherScreen screen;
            REQUIRE_MESSAGE(screen.init(entries, round % 2, true, &err), err);
            target.bind();
            screen.draw(overlay, 320, 240);
        }
        REQUIRE_MESSAGE(stack.build(stackFor(g), &err), err);
        CHECK(stack.session->game() == &g);
        CHECK(stack.flow->frontend().sequel() == (g.id == GameId::AirStrike2));
        play(stack, target);
        CHECK(stack.session->hasLevel());
        if (round == 3) {
            // A lost context in the middle: the rebuild path of the window (every GL object of
            // the view anew), then play goes on.
            stack.view.reset();
            stack.view.reset(new GameView());
            REQUIRE_MESSAGE(stack.view->init(*stack.session, &err, stack.session->hasLevel()), err);
            stack.flow->setView(stack.view.get());
            target.bind();
            stack.flow->draw(320, 240);
        }
        stack.teardown();
        CHECK_FALSE(stack.active());
        glFinish();
        const GlCounts now = countGl();
        INFO("textures " << now.textures << ", buffers " << now.buffers << ", arrays " << now.arrays << ", programs "
                         << now.programs << ", framebuffers " << now.framebuffers);
        if (round == 1) baseline = now; // after both games have run once (first-use caches)
        if (round > 1) CHECK(now == baseline);
        if (round == 9)
            MESSAGE("GL objects after each switch: textures " << now.textures << ", buffers " << now.buffers
                                                            << ", vertex arrays " << now.arrays << ", programs "
                                                            << now.programs << ", framebuffers " << now.framebuffers);
    }
}
