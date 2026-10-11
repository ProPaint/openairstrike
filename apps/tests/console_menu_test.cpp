// The console menu view (as3d/console_menu.h, docs/3ds-ui-design.md "AirStrike 3D menus on the
// console (round 4, R4.5)"): the per-screen layouts of the first game's menus on the 320 x 240
// bottom screen (bars of at least 36 px, no overlaps, labels that fit at their size, the footer
// inside the screen and clear of the bars, hit rectangles equal to the drawn bars), the touch
// path (bars, spinner arrows, slider, list rows, helicopter cells, the Information pages and
// sheets), the Information re-flow and the console key names. Synthetic texts only (the built-in
// defaults and a few set here); the font metrics are the original's table (no font texture).
#include "doctest.h"

#include <cmath>
#include <string>
#include <vector>

#include "as3d/console_menu.h"
#include "as3d/frontend.h"
#include "as3d/game_profile.h"

using namespace as3d;
using namespace as3d::ui;

namespace {

struct Host : GameHost {
    int starts = 0, quits = 0;
    void startMission(const MissionStart&) override { starts++; }
    void loadAttract() override {}
    void setPaused(bool) override {}
    void clearPlayerActions() override {}
    void settingsChanged(const Settings&) override {}
    void saveProfile(const Profile&) override {}
    void quit() override { quits++; }
};

// The console's front end (game_flow.cpp with FlowConfig::consoleOptions and touch).
FrontendContent consoleContent(bool changeGame, GameId game = GameId::AirStrike3D) {
    FrontendContent c;
    if (game != GameId::AirStrike3D) c.game = &gameProfile(game);
    for (int i = 0; i < kMissionCount; i++) c.missionNames[i] = "Mission " + std::to_string(i + 1) + ": Somewhere Far Away";
    c.videoOptions = false;
    c.twoPlayerMode = false;
    c.mouseControlOption = false;
    c.keyBindingsOption = false;
    c.padNavigation = true;
    c.changeGame = changeGame;
    c.version = "v1.70";
    c.copyright = "Copyright 2010 DivoGames Ltd.";
    return c;
}

// Information pages: a long story page (two sheets), a page with icons and headings, credits.
Texts consoleTexts() {
    Texts t;
    t.set("info.1.title", "THE STORY (Page 1 of 2)");
    for (int i = 0; i < 15; ++i)
        if (i != 5 && i != 8)
            t.set("info.1." + std::to_string(i),
                  "Words of a long line of the story that the desktop wraps at eighty characters, more of them.");
    t.set("info.4.title", "PRIMARY WEAPONS (Page 1 of 3)");
    for (int p = 0; p < 4; ++p) {
        t.set("info.4." + std::to_string(4 * p), "{Weapon " + std::to_string(p) + "}");
        t.set("info.4." + std::to_string(4 * p + 1), "Not very powerful, this weapon has a very high rate of fire");
        t.set("info.4." + std::to_string(4 * p + 2), "what makes it very effective against low-armored enemies.");
    }
    t.set("info.10.title", "CREDITS");
    t.set("info.10.4", "PROGRAMMING");
    t.set("info.10.5", "Somebody");
    // The sequels' (R4.8): their own captions with the desktop's padding spaces, credits, a start
    // dialogue for mission 1.
    t.set("button.start_game", "Start Game");
    t.set("button.top_scores", "Top Scores");
    t.set("button.options", "Options");
    t.set("button.information", "Information");
    t.set("button.credits", "Credits");
    t.set("button.quit", "Quit");
    t.set("button.back", "   Back   ");
    t.set("button.next", "   Next   ");
    t.set("button.start", "  Start  ");
    t.set("button.choose_heli", "  Choose Helicopter  ");
    t.set("button.restart", "   Restart   ");
    t.set("button.quit_wide", "   Quit   ");
    t.set("button.next_wide", "   Next   ");
    t.set("button.resume", "Resume");
    t.set("label.difficulty", "Difficulty:");
    t.set("label.game_mode", "Game mode:");
    for (int i = 0; i < 12; ++i) t.set("credits." + std::to_string(i), i % 3 == 0 ? "{SOMETHING DONE}" : "A Person Of Renown");
    t.set("dialog.1.start.0", "Listen to me, pilot. The enemy has taken the island and we need you there.\nGood luck.");
    t.set("dialog.1.start.0.speaker", "0");
    t.set("dialog.1.start.1", "Yes, sir.");
    t.set("dialog.1.start.1.speaker", "1");
    for (int p : {2, 3, 5, 6, 7, 8}) {
        t.set("info." + std::to_string(p) + ".title", "Weapons page");
        t.set("info." + std::to_string(p) + ".0", "{Machine gun}");
        t.set("info." + std::to_string(p) + ".1", "Not very powerful, this weapon has a very high rate of fire.");
    }
    return t;
}

struct Rig {
    Host host;
    Profile profile;
    Frontend fe;
    ConsoleMenuView view;
    explicit Rig(bool changeGame = false, GameId game = GameId::AirStrike3D, bool intro = false)
        : profile(introProfile(intro)), fe(host, profile, consoleContent(changeGame, game), consoleTexts()) {
        fe.setTouchMode(true);
        fe.boot();
        if (intro) return; // the sequels' intro pages
        fe.setState(FrontendState::Attract);
        if (!fe.menuOpen()) fe.open(Screen::MainMenu);
    }
    static Profile introProfile(bool intro) {
        Profile p;
        p.settings.showLogo = intro;
        return p;
    }
    ConsoleLayout layout() { return view.layout(fe); }
    // One frame of a finger: down at (x, y) and up the next frame, through the view.
    void tap(int x, int y) {
        UiInput down;
        view.applyHits(fe);
        view.touchInput(fe, true, false, x, y, down);
        view.filterKeys(fe, down);
        fe.update(0.016f, down);
        view.applyHits(fe);
        UiInput up;
        view.touchInput(fe, false, true, x, y, up);
        fe.update(0.016f, up);
        view.applyHits(fe);
    }
    void key(int k) {
        UiInput in;
        in.key(k);
        view.filterKeys(fe, in);
        fe.update(0.016f, in);
        view.applyHits(fe);
    }
    void tapBox(const ConsoleBox& b) { tap(static_cast<int>(b.r.x + b.r.w * 0.5f), static_cast<int>(b.r.y + b.r.h * 0.5f)); }
    const ConsoleBox* bar(int id) {
        const ConsoleLayout L = layout();
        const Menu* m = fe.menus().top();
        for (const ConsoleBox& b : L.boxes)
            if (b.kind == ConsoleBoxKind::Bar && b.item >= 0 && m->items[static_cast<size_t>(b.item)].id == id) {
                last_ = b;
                return &last_;
            }
        return nullptr;
    }
    const ConsoleBox* box(ConsoleBoxKind k, int arg = -1) {
        const ConsoleLayout L = layout();
        for (const ConsoleBox& b : L.boxes)
            if (b.kind == k && (arg < 0 || b.arg == arg)) {
                last_ = b;
                return &last_;
            }
        return nullptr;
    }
    ConsoleBox last_;
};

bool overlap(const RectF& a, const RectF& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
bool inside(const RectF& in, const RectF& out) {
    return in.x >= out.x && in.y >= out.y && in.x + in.w <= out.x + out.w && in.y + in.h <= out.y + out.h;
}

// The layout rules of docs/3ds-ui-design.md, for the screen on top of `rig`.
void checkLayout(Rig& rig, const char* what) {
    INFO(what);
    const ConsoleLayout L = rig.layout();
    REQUIRE(L.valid);
    const Menu* m = rig.fe.menus().top();
    REQUIRE(m);
    const FontMetrics& fm = FontMetrics::original();
    const bool keyboard = L.screen == Screen::NameEntry;
    for (const ConsoleBox& b : L.boxes) {
        INFO("box " << static_cast<int>(b.kind) << " item " << b.item << " label '" << b.label << "' at " << b.r.x << "," << b.r.y
                    << " " << b.r.w << "x" << b.r.h);
        // On the screen, on whole pixels.
        CHECK(b.r.x >= 0);
        CHECK(b.r.y >= 0);
        CHECK(b.r.x + b.r.w <= kConsoleBottomW);
        CHECK(b.r.y + b.r.h <= kConsoleBottomH);
        CHECK(b.r.x == std::floor(b.r.x));
        CHECK(b.r.y == std::floor(b.r.y));
        // A thumb: 36 px for every target (the keyboard's keys: 20 x 20).
        if (keyboard) {
            CHECK(b.r.w >= kConsoleMinKey);
            CHECK(b.r.h >= kConsoleMinKey);
        } else {
            CHECK(b.r.h >= kConsoleMinBar);
            CHECK(b.r.w >= kConsoleMinBar);
        }
        // Labels fit their box at their size (list rows may step down to S).
        if (!b.label.empty()) {
            const MenuItem* it = b.item >= 0 ? &m->items[static_cast<size_t>(b.item)] : nullptr;
            const bool row = it && (it->type == ItemType::Spinner || it->type == ItemType::Slider ||
                                    (it->type == ItemType::HeliGrid && b.kind == ConsoleBoxKind::Bar));
            if (b.kind == ConsoleBoxKind::ListRow) {
                CHECK(measureTextPx(fm, b.label, FontSize::S) <= b.r.w - 16);
            } else if (row) {
                // The label ends before the row's arrows (or the slider's track).
                float end = b.r.x + b.r.w;
                for (const ConsoleBox& s : L.boxes)
                    if (s.item == b.item && s.kind != ConsoleBoxKind::Bar) end = std::min(end, s.r.x);
                CHECK(b.r.x + 8 + measureTextPx(fm, b.label, b.size) <= end - 4);
            } else {
                CHECK(measureTextPx(fm, b.label, b.size) <= b.r.w - (keyboard ? 4 : 8));
            }
        }
        // A spinner's values fit between its arrows (at L, or S).
        if (b.kind == ConsoleBoxKind::SpinPrev) {
            const MenuItem& it = m->items[static_cast<size_t>(b.item)];
            const ConsoleBox* next = nullptr;
            for (const ConsoleBox& s : L.boxes)
                if (s.item == b.item && s.kind == ConsoleBoxKind::SpinNext) next = &s;
            REQUIRE(next);
            const float zone = next->r.x - (b.r.x + b.r.w);
            for (const std::string& v : it.values) {
                std::string u = v;
                for (char& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                CHECK(measureTextPx(fm, u, FontSize::S) <= zone - 4);
            }
        }
    }
    // No two targets of different items overlap; a sub-target lies inside its item's bar.
    for (size_t i = 0; i < L.boxes.size(); ++i) {
        const ConsoleBox& a = L.boxes[i];
        if (a.kind != ConsoleBoxKind::Bar && a.kind != ConsoleBoxKind::Key) {
            const ConsoleBox* parent = L.barOf(a.item);
            REQUIRE(parent);
            CHECK(inside(a.r, parent->r));
        }
        for (size_t j = i + 1; j < L.boxes.size(); ++j) {
            const ConsoleBox& b = L.boxes[j];
            if (a.item >= 0 && a.item == b.item) {
                // Sub-targets of one item do not overlap each other.
                if (a.kind != ConsoleBoxKind::Bar && b.kind != ConsoleBoxKind::Bar) CHECK(!overlap(a.r, b.r));
                continue;
            }
            INFO("boxes " << i << " and " << j);
            CHECK(!overlap(a.r, b.r));
        }
    }
    // The footer: inside the screen, its room, and clear of every box.
    if (!L.hints.empty()) {
        CHECK(L.hintW == consoleHintsWidth(fm, L.hints));
        CHECK(L.hintX >= 0);
        CHECK(L.hintX + L.hintW <= L.hintRight);
        CHECK(L.hintRight <= kConsoleBottomW);
        CHECK(L.hintY + 15 <= kConsoleBottomH);
        const RectF foot{static_cast<float>(L.hintX), static_cast<float>(L.hintY), static_cast<float>(L.hintW), 15};
        for (const ConsoleBox& b : L.boxes) CHECK(!overlap(foot, b.r));
    }
    // The titles fit the screen.
    if (!L.title.empty()) CHECK(measureTextPx(fm, L.title, L.titleSize) <= kConsoleBottomW - 16);
    if (!L.subtitle.empty()) CHECK(measureTextPx(fm, L.subtitle, FontSize::S) <= kConsoleBottomW - 8);
    // The hit rectangles are the drawn bars (virtual units, 2.5 x the pixels); items without a
    // bar have none (Information's page spinner: the NEXT button).
    rig.view.applyHits(rig.fe);
    for (size_t i = 0; i < m->items.size(); ++i) {
        const MenuItem& it = m->items[i];
        const ConsoleBox* b = L.barOf(static_cast<int>(i));
        if (b) {
            CHECK(it.hit.x == doctest::Approx(b->r.x * 2.5f));
            CHECK(it.hit.y == doctest::Approx(b->r.y * 2.5f));
            CHECK(it.hit.w == doctest::Approx(b->r.w * 2.5f));
            CHECK(it.hit.h == doctest::Approx(b->r.h * 2.5f));
            // The bottom screen's 0.4 mapping takes the hit's centre back into the bar.
            const float cx = (it.hit.x + it.hit.w * 0.5f) * 0.4f, cy = (it.hit.y + it.hit.h * 0.5f) * 0.4f;
            CHECK(b->r.contains(cx, cy));
        } else if (!(L.screen == Screen::Information && it.type == ItemType::Spinner)) {
            CHECK(it.hit.w == 0);
        }
        // Every visible, enabled item of the screen has a bar (or is the Information spinner).
        if (!it.hidden() && !it.disabled() && !(L.screen == Screen::Information && it.type == ItemType::Spinner)) CHECK(b);
    }
}

} // namespace

TEST_CASE("console menus: the sequels' screens keep the rules (R4.8)") {
    for (GameId game : {GameId::AirStrike2, GameId::GulfThunder}) {
        for (bool games : {false, true}) {
            Rig rig(games, game);
            CAPTURE(gameProfile(game).key);
            REQUIRE(rig.fe.sequel());
            REQUIRE(rig.fe.menuOpen());
            REQUIRE(ConsoleMenuView::handles(rig.fe));
            checkLayout(rig, games ? "sequel main menu (with GAMES)" : "sequel main menu");
            // Six buttons of at least 36 px in two columns, GAMES in the corner.
            int bars = 0;
            for (const ConsoleBox& b : rig.layout().boxes) bars += b.kind == ConsoleBoxKind::Bar ? 1 : 0;
            CHECK(bars == (games ? 7 : 6));
            for (Screen s : {Screen::Exit, Screen::StartGame, Screen::HeliSelect, Screen::TopScores, Screen::Options,
                             Screen::Credits, Screen::NameEntry}) {
                rig.fe.open(s);
                REQUIRE(ConsoleMenuView::handles(rig.fe));
                checkLayout(rig, screenName(s));
                rig.fe.menus().pop();
            }
            // Information, every page (the sequels' seven or eight).
            rig.fe.open(Screen::Information);
            for (int page = 0; page < 8; ++page) {
                checkLayout(rig, "sequel information");
                rig.key(keys::PageDown);
            }
            rig.fe.menus().pop();
        }
        Rig rig(false, game);
        rig.fe.setState(FrontendState::Playing);
        for (Screen s : {Screen::InGame, Screen::GameOver, Screen::MissionComplete, Screen::GameComplete}) {
            rig.fe.open(s);
            checkLayout(rig, screenName(s));
            rig.fe.menus().clear();
        }
        rig.fe.showTutorialHint("Press the {Ctrl} key to fire.");
        checkLayout(rig, "sequel hint");
        rig.fe.menus().clear();
        // The start dialogue: one NEXT; a tap completes the page being typed.
        REQUIRE(rig.fe.debugSet("dialogue", "start"));
        REQUIRE(rig.fe.topScreen() == Screen::Dialogue);
        checkLayout(rig, "sequel dialogue");
        const ConsoleBox* next = rig.box(ConsoleBoxKind::Key, keys::Enter);
        REQUIRE(next);
        rig.fe.update(0.3f, UiInput{}); // the panel opens
        rig.tapBox(*next);
        CHECK(ConsoleMenuView::handles(rig.fe));
    }
}

TEST_CASE("console menus: the sequels by touch (R4.8)") {
    Rig rig(false, GameId::AirStrike2);
    // Main menu: Options.
    const ConsoleBox* opt = rig.bar(3);
    REQUIRE(opt);
    rig.tapBox(*opt);
    CHECK(rig.fe.topScreen() == Screen::Options);
    rig.fe.menus().pop();
    // Start Game, Next: the helicopter selection; its arrows turn the helicopter.
    rig.tapBox(*rig.bar(1));
    REQUIRE(rig.fe.topScreen() == Screen::StartGame);
    rig.tapBox(*rig.bar(2));
    REQUIRE(rig.fe.topScreen() == Screen::HeliSelect);
    const int before = rig.fe.helicopters()[0];
    rig.tapBox(*rig.bar(6));
    CHECK(rig.fe.helicopters()[0] != before);
    rig.tapBox(*rig.bar(7));
    CHECK(rig.fe.helicopters()[0] == before);
    // Back twice: the main menu.
    rig.tapBox(*rig.bar(2));
    CHECK(rig.fe.topScreen() == Screen::StartGame);
}

TEST_CASE("console menus: the sequels' intro is the view's: FASTER and SKIP (R4.8)") {
    Rig rig(false, GameId::AirStrike2, true);
    REQUIRE(rig.fe.state() == FrontendState::Intro);
    REQUIRE(ConsoleMenuView::introShown(rig.fe));
    const ConsoleLayout L = rig.layout();
    REQUIRE(L.valid);
    CHECK(L.intro);
    REQUIRE(L.boxes.size() == 2u);
    for (const ConsoleBox& b : L.boxes) {
        CHECK(b.r.w >= kConsoleMinBar);
        CHECK(b.r.h >= kConsoleMinBar);
        CHECK(measureTextPx(FontMetrics::original(), b.label, b.size) <= b.r.w - 8);
    }
    // B is the comic's Skip: the intro ends, the main menu opens.
    rig.key(keys::Escape);
    CHECK(rig.fe.state() != FrontendState::Intro);
    CHECK(rig.fe.menuOpen());
    // The first game has no console intro.
    Rig first(false, GameId::AirStrike3D, true);
    CHECK_FALSE(ConsoleMenuView::introShown(first.fe));
}

TEST_CASE("console menus: every screen's layout keeps the rules") {
    for (bool games : {false, true}) {
        Rig rig(games);
        checkLayout(rig, games ? "main menu (with GAMES)" : "main menu");
        for (Screen s : {Screen::Exit, Screen::StartGame, Screen::TopScores, Screen::Options, Screen::Information,
                         Screen::NameEntry}) {
            rig.fe.open(s);
            checkLayout(rig, screenName(s));
            rig.fe.menus().pop();
        }
    }
    Rig rig;
    rig.fe.setState(FrontendState::Playing);
    rig.fe.open(Screen::InGame);
    checkLayout(rig, "ingame");
    rig.fe.menus().clear();
    MissionReport r;
    r.players[0].score = 1234;
    r.players[0].kills = 3;
    r.totals.enemyTotal = 10;
    rig.fe.onEndLevel(r);
    checkLayout(rig, "mission complete");
    rig.fe.menus().clear();
    rig.fe.onGameOver(r);
    checkLayout(rig, "game over (first seconds)");
    rig.fe.update(2.1f, {});
    checkLayout(rig, "game over menu");
    rig.fe.menus().clear();
    rig.fe.open(Screen::GameComplete);
    checkLayout(rig, "game complete");
    // Every Information page and sheet.
    rig.fe.menus().clear();
    rig.fe.open(Screen::Information);
    for (int p = 0; p < 12; ++p) {
        checkLayout(rig, "information page");
        rig.key(keys::Right);
    }
}

TEST_CASE("console menus: the layouts in pixels") {
    Rig rig(true);
    ConsoleLayout L = rig.layout();
    // Main menu: five bars 240 x 36 at a 40-px pitch from y 4, GAMES in the bottom-left corner.
    REQUIRE(rig.bar(1));
    CHECK(rig.bar(1)->r.x == 40);
    CHECK(rig.bar(1)->r.y == 4);
    CHECK(rig.bar(1)->r.w == 240);
    CHECK(rig.bar(1)->r.h == 36);
    CHECK(rig.bar(5)->r.y == 164);
    REQUIRE(rig.bar(kChangeGameItem));
    CHECK(rig.bar(kChangeGameItem)->r.y == 204);
    CHECK(L.hints.size() == 2);
    // Options: five rows 312 x 36 at a 38-px pitch, BACK 80 x 36.
    rig.fe.open(Screen::Options);
    L = rig.layout();
    int rows = 0;
    for (const ConsoleBox& b : L.boxes)
        if (b.kind == ConsoleBoxKind::Bar && b.r.w == 312) {
            CHECK(b.r.h == 36);
            CHECK(b.r.y == 4 + 38 * rows);
            ++rows;
        }
    CHECK(rows == 5); // brightness, sound, music, camera, show fps
    REQUIRE(rig.bar(1));
    CHECK(rig.bar(1)->r.w == 80);
    rig.fe.menus().pop();
    // Start Game: three list rows, two spinner rows, BACK and START.
    rig.fe.open(Screen::StartGame);
    L = rig.layout();
    int listRows = 0;
    for (const ConsoleBox& b : L.boxes) listRows += b.kind == ConsoleBoxKind::ListRow ? 1 : 0;
    CHECK(listRows == 3);
    REQUIRE(rig.bar(2));
    CHECK(rig.bar(2)->r.x + rig.bar(2)->r.w == 316); // START under the right thumb
}

TEST_CASE("console menus: touch reaches the menu model") {
    Rig rig;
    // A bar: Options opens.
    rig.tapBox(*rig.bar(3));
    REQUIRE(rig.fe.topScreen() == Screen::Options);
    // A spinner's arrows: Show FPS on, then off.
    const bool fps = rig.profile.settings.showFps;
    {
        const ConsoleLayout L = rig.layout();
        const Menu* m = rig.fe.menus().top();
        const ConsoleBox* next = nullptr;
        for (const ConsoleBox& b : L.boxes)
            if (b.kind == ConsoleBoxKind::SpinNext && m->items[static_cast<size_t>(b.item)].id == 43) next = &b;
        REQUIRE(next);
        rig.tapBox(*next);
    }
    CHECK(rig.profile.settings.showFps != fps);
    // The slider: the track's left end is the minimum, the right end the maximum, a drag follows.
    {
        const ConsoleLayout L = rig.layout();
        const Menu* m = rig.fe.menus().top();
        const ConsoleBox* track = nullptr;
        for (const ConsoleBox& b : L.boxes)
            if (b.kind == ConsoleBoxKind::SliderTrack && m->items[static_cast<size_t>(b.item)].id == 25) track = &b;
        REQUIRE(track);
        const int y = static_cast<int>(track->r.y + 10);
        rig.tap(static_cast<int>(track->r.x + 2), y);
        CHECK(rig.profile.settings.sfxVolume == doctest::Approx(0.0f));
        rig.tap(static_cast<int>(track->r.x + track->r.w - 2), y);
        CHECK(rig.profile.settings.sfxVolume == doctest::Approx(1.0f));
        // A drag from the middle to the left quarter, one frame per position.
        UiInput in;
        rig.view.touchInput(rig.fe, true, false, 236, y, in);
        rig.fe.update(0.016f, in);
        CHECK(rig.profile.settings.sfxVolume == doctest::Approx(0.5f));
        for (int x : {220, 200, 202}) {
            UiInput mv;
            rig.view.touchInput(rig.fe, true, true, x, y, mv);
            rig.fe.update(0.016f, mv);
        }
        CHECK(rig.profile.settings.sfxVolume == doctest::Approx(0.2f).epsilon(0.11));
        UiInput up;
        rig.view.touchInput(rig.fe, false, true, 202, y, up);
        rig.fe.update(0.016f, up);
    }
    // BACK closes Options.
    rig.tapBox(*rig.bar(1));
    CHECK(rig.fe.topScreen() == Screen::MainMenu);

    // Start Game: a list row selects that mission (unlocked ones only), the difficulty arrows.
    rig.profile.progress.missionUnlocked[1] = true;
    rig.tapBox(*rig.bar(1));
    REQUIRE(rig.fe.topScreen() == Screen::StartGame);
    rig.tapBox(*rig.box(ConsoleBoxKind::ListRow, 1));
    const MenuItem* list = rig.fe.menus().top()->find(3);
    CHECK(list->selected == 1);
    rig.tapBox(*rig.box(ConsoleBoxKind::ListRow, 2)); // locked
    CHECK(rig.fe.menus().top()->find(3)->selected == 1);
    const int diff = rig.fe.facts().difficulty;
    rig.tapBox(*rig.box(ConsoleBoxKind::SpinPrev));
    CHECK(rig.fe.facts().difficulty == diff - 1);
    // START starts mission 2.
    rig.tapBox(*rig.bar(2));
    CHECK(rig.host.starts == 1);
    CHECK(rig.fe.campaign().mission == 1);
}

TEST_CASE("console menus: Information pages and their sheets") {
    Rig rig;
    rig.tapBox(*rig.bar(4));
    REQUIRE(rig.fe.topScreen() == Screen::Information);
    CHECK(rig.fe.facts().infoPage == 0);
    // Page 1 is longer than the top screen: NEXT shows its second sheet first, then page 2.
    const InfoPage p1 = consoleInfoPage(rig.fe.texts(), FontMetrics::original(), 1, 376, 208);
    REQUIRE(p1.sheets.size() >= 2);
    rig.tapBox(*rig.box(ConsoleBoxKind::Key, keys::Right));
    CHECK(rig.fe.facts().infoPage == 0);
    CHECK(rig.view.infoSheet() == 1);
    for (size_t s = 2; s < p1.sheets.size(); ++s) rig.tapBox(*rig.box(ConsoleBoxKind::Key, keys::Right));
    rig.tapBox(*rig.box(ConsoleBoxKind::Key, keys::Right));
    CHECK(rig.fe.facts().infoPage == 1);
    CHECK(rig.view.infoSheet() == 0);
    // Back (L, PageUp) opens page 1 at its last sheet.
    rig.key(keys::PageUp);
    CHECK(rig.fe.facts().infoPage == 0);
    CHECK(rig.view.infoSheet() == static_cast<int>(p1.sheets.size()) - 1);
    // The ui_taps' target of the page spinner (item 10) is the NEXT button.
    rig.view.applyHits(rig.fe);
    const MenuItem* spin = rig.fe.menus().top()->find(10);
    const ConsoleBox* next = rig.box(ConsoleBoxKind::Key, keys::Right);
    CHECK(next->r.contains((spin->hit.x + spin->hit.w * 0.5f) * 0.4f, (spin->hit.y + spin->hit.h * 0.5f) * 0.4f));
    // B closes it.
    rig.key(keys::Escape);
    CHECK(rig.fe.topScreen() == Screen::MainMenu);
}

TEST_CASE("console menus: the Information re-flow") {
    const Texts t = consoleTexts();
    const FontMetrics& m = FontMetrics::original();
    const InfoPage p = consoleInfoPage(t, m, 1, 376, 208);
    int lines = 0;
    for (const auto& sheet : p.sheets) {
        int used = 0;
        for (const InfoLine& l : sheet) {
            used += l.gapBefore + fontLinePitchPx(FontSize::S);
            CHECK(measureTextPx(m, l.text, FontSize::S, true) <= 376);
            ++lines;
        }
        CHECK(used <= 208);
    }
    CHECK(lines > 13); // 13 desktop lines of 94 characters re-flowed at about 48 a line
    // Paragraphs: the missing line numbers 5 and 8 start new ones (a gap before them).
    int gaps = 0;
    for (const auto& sheet : p.sheets)
        for (const InfoLine& l : sheet) gaps += l.gapBefore > 0 ? 1 : 0;
    CHECK(gaps >= 1);
    // Icon pages: a heading per weapon on its own line with the icon, indented text.
    const InfoPage w = consoleInfoPage(t, m, 4, 376, 208);
    int icons = 0;
    for (const auto& sheet : w.sheets)
        for (const InfoLine& l : sheet)
            if (l.icon >= 0) {
                CHECK(l.text.front() == '{');
                ++icons;
            }
    CHECK(icons == 4);
    // Credits: centred, line by line.
    const InfoPage c = consoleInfoPage(t, m, 10, 376, 208);
    REQUIRE(c.sheets.size() == 1);
    REQUIRE(c.sheets[0].size() == 2);
    CHECK(c.sheets[0][0].centred);
    // Markup stays balanced on every wrapped line.
    const std::vector<std::string> wrapped =
        wrapText(m, "A {very long highlighted phrase that has to wrap over more than one line} after it", FontSize::S, 120);
    REQUIRE(wrapped.size() >= 2);
    for (const std::string& l : wrapped) {
        int depth = 0;
        for (char ch : l) {
            if (ch == '{') ++depth;
            if (ch == '}') --depth;
            CHECK(depth >= 0);
            CHECK(depth <= 1);
        }
        CHECK(depth == 0);
        CHECK(measureTextPx(m, l, FontSize::S, true) <= 120);
    }
}

TEST_CASE("console menus: the hints' key names on the console") {
    CHECK(consoleKeyNames("To fire Primary Weapon, press the {Ctrl} key.") == "To fire Primary Weapon, press the {A} button.");
    CHECK(consoleKeyNames("press the {Shift} key.") == "press the {B} button.");
    CHECK(consoleKeyNames("press the {Space} key.") == "press the {X} button.");
    CHECK(consoleKeyNames("pressing^ {Z} key (by default).") == "pressing^ {Y} button (by default).");
    CHECK(consoleKeyNames("pressing^{X} key (by default).") == "pressing^{R} button (by default).");
    CHECK(consoleKeyNames("{Space} then {X}") == "{X} then {R}");
    CHECK(consoleKeyNames("{Health Bar} and {Missiles}") == "{Health Bar} and {Missiles}");
}

TEST_CASE("console menus: the footers' key widths are the owner's set 4") {
    CHECK(consoleKeyRect(HintKey::A).w == 15);
    CHECK(consoleKeyRect(HintKey::L).w == 19);
    CHECK(consoleKeyRect(HintKey::Start).w == 39);
    CHECK(consoleKeyRect(HintKey::ZL).empty()); // not in the set: the key's name in a frame
    const std::vector<ConsoleHint> h{{HintKey::A, "Select"}, {HintKey::B, "Back"}};
    const FontMetrics& m = FontMetrics::original();
    CHECK(consoleHintsWidth(m, h) == 15 + 4 + measureTextPx(m, "Select", FontSize::S) + 8 + 15 + 4 + measureTextPx(m, "Back", FontSize::S));
}

TEST_CASE("console menus: drawing stays on the screens") {
    Rig rig(true);
    UiAssets assets; // no textures: the quads of the bars, frames and arrows only
    for (Screen s : {Screen::MainMenu, Screen::Options, Screen::StartGame, Screen::Information, Screen::NameEntry,
                     Screen::TopScores, Screen::Exit}) {
        if (s != Screen::MainMenu) rig.fe.open(s);
        rig.view.applyHits(rig.fe);
        Renderer2D bottom;
        bottom.begin(kConsoleBottomW, kConsoleBottomH); // the 3DS list's 0.4 mapping
        rig.view.drawBottom(bottom, assets, rig.fe);
        CHECK(!bottom.quads().empty());
        for (const Quad& q : bottom.quads()) {
            float px[4], py[4];
            bottom.quadCorners(q, px, py);
            for (int k = 0; k < 4; ++k) {
                CHECK(px[k] >= -0.01f);
                CHECK(px[k] <= kConsoleBottomW + 0.01f);
                CHECK(py[k] >= -0.01f);
                CHECK(py[k] <= kConsoleBottomH + 0.01f);
            }
        }
        Renderer2D top;
        top.begin(kConsoleTopW, kConsoleTopH);
        rig.view.drawTop(top, assets, rig.fe);
        for (const Quad& q : top.quads()) {
            float px[4], py[4];
            top.quadCorners(q, px, py);
            for (int k = 0; k < 4; ++k) {
                CHECK(px[k] >= -0.01f);
                CHECK(px[k] <= kConsoleTopW + 0.01f);
            }
        }
        if (s != Screen::MainMenu) rig.fe.menus().pop();
    }
}

// The size rule (docs/3ds-ui-design.md "Owner decisions", R4.7): the rows of one list share one
// size, chosen by the longest entry (not each line on its own), and a label too long even at S is
// cut with "..." instead of shrinking further.
TEST_CASE("console menus: one size per group of labels") {
    const FontMetrics& fm = FontMetrics::original();
    // The helpers.
    CHECK(consoleGroupSize(fm, {"SHORT", "TINY"}, {200}) == FontSize::L);
    const std::string longest = "Mission 2: Leaving the Home";
    REQUIRE(measureTextPx(fm, longest, FontSize::L) > 256);
    REQUIRE(measureTextPx(fm, longest, FontSize::S) <= 256);
    CHECK(consoleGroupSize(fm, {"Mission 1: Tutorial", longest}, {256}) == FontSize::S);
    CHECK(consoleGroupSize(fm, {"Mission 1: Tutorial"}, {256}) == FontSize::L);
    CHECK(fitLabel(fm, "SHORT", FontSize::L, 200) == "SHORT");
    const std::string cut = fitLabel(fm, std::string(60, 'W'), FontSize::S, 100);
    REQUIRE(cut.size() > 3);
    CHECK(cut.substr(cut.size() - 3) == "...");
    CHECK(measureTextPx(fm, cut, FontSize::S) <= 100);
    // A start-game list with one long entry and one too long even at S: every row at one size
    // (the long entry is not visible: the list's every entry decides).
    Host host;
    Profile profile;
    FrontendContent c = consoleContent(false);
    for (int i = 0; i < kMissionCount; i++) c.missionNames[i] = "Mission " + std::to_string(i + 1) + ": Tutorial";
    c.missionNames[5] = longest;
    c.missionNames[kMissionCount - 1] = "Mission 24: " + std::string(40, 'W');
    Frontend fe(host, profile, c, consoleTexts());
    fe.setTouchMode(true);
    fe.boot();
    fe.setState(FrontendState::Attract);
    fe.open(Screen::StartGame);
    ConsoleMenuView view;
    const ConsoleLayout L = view.layout(fe);
    int rows = 0;
    for (const ConsoleBox& b : L.boxes)
        if (b.kind == ConsoleBoxKind::ListRow) {
            ++rows;
            CHECK(b.size == FontSize::S);
            CHECK(measureTextPx(fm, b.label, b.size) <= b.r.w - 16);
        }
    CHECK(rows == 3);
    // The same list without the long entries: L throughout.
    FrontendContent c2 = consoleContent(false);
    for (int i = 0; i < kMissionCount; i++) c2.missionNames[i] = "Mission " + std::to_string(i + 1) + ": Tutorial";
    Frontend fe2(host, profile, c2, consoleTexts());
    fe2.setTouchMode(true);
    fe2.boot();
    fe2.setState(FrontendState::Attract);
    fe2.open(Screen::StartGame);
    for (const ConsoleBox& b : view.layout(fe2).boxes)
        if (b.kind == ConsoleBoxKind::ListRow) CHECK(b.size == FontSize::L);
    // The option rows and the start screen's spinner rows: one size per column.
    Rig rig;
    for (Screen s : {Screen::Options, Screen::StartGame}) {
        rig.fe.open(s);
        std::vector<FontSize> col;
        for (const ConsoleBox& b : rig.layout().boxes)
            if (b.kind == ConsoleBoxKind::Bar && b.r.w == 312 && !b.label.empty()) col.push_back(b.size);
        REQUIRE(!col.empty());
        for (FontSize z : col) CHECK(z == col.front());
        rig.fe.menus().pop();
    }
}

// R4.7: the tutorial hint is a console screen: a 160 x 56 OK on the bottom (no footer: the HUD's
// pad reminder is in the bottom-left corner), a tap on it closes the hint and resumes play.
TEST_CASE("console menus: the tutorial hint's OK") {
    Rig rig;
    rig.fe.menus().clear(); // the main menu of the boot
    rig.fe.setState(FrontendState::Playing);
    rig.fe.showTutorialHint("To launch missile^press the {Shift} key.");
    REQUIRE(rig.fe.menuOpen());
    REQUIRE(rig.fe.topScreen() == Screen::Hint);
    CHECK(ConsoleMenuView::handles(rig.fe));
    for (int k = 0; k < 40; ++k) rig.fe.update(0.016f, {}); // past the opening: the OK is shown
    REQUIRE_FALSE(rig.fe.menus().top()->items[0].hidden());
    checkLayout(rig, "hint");
    const ConsoleLayout L = rig.layout();
    CHECK(L.hints.empty());
    REQUIRE(rig.bar(1));
    CHECK(rig.bar(1)->r.x == 80);
    CHECK(rig.bar(1)->r.y == 100);
    CHECK(rig.bar(1)->r.w == 160);
    CHECK(rig.bar(1)->r.h == 56);
    CHECK(rig.fe.facts().hintText.find("Shift") != std::string::npos);
    rig.tapBox(*rig.bar(1));
    CHECK_FALSE(rig.fe.menuOpen());
}
