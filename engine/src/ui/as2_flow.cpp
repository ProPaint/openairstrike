// The sequels' mission flow and progression (docs/spec/as2/frontend.md 1.3, 5): the screen
// dispatcher, the comic pages at boot, a new game with the campaign checkpoint ("Continue"),
// the start dialogue, the end of a mission (checkpoint, end dialogue, unlocks, Mission or Game
// Complete).
#include <algorithm>
#include <cstdlib>

#include "as2_screens.h"

namespace as3d::ui {

using namespace as2;

namespace {

// Gulf Thunder's captions where the texts file has none: what differs from AirStrike 2's
// built-in defaults (gulf/frontend.delta.md 7: the main and in-game menu captions carry no
// padding, the others other padding; the padding sets the button widths).
const char* gulfDefault(const std::string& key) {
    static const std::pair<const char*, const char*> table[] = {
        {"title.options", "Options"}, {"button.start_game", "Start Game"}, {"button.top_scores", "Top Scores"},
        {"button.options", "Options"}, {"button.information", "Information"}, {"button.credits", "Credits"},
        {"button.quit", "Quit"}, {"button.resume", "Resume"}, {"button.quit_wide", "   Quit   "},
        {"button.back", "   Back   "}, {"button.back_wide", "   Back   "}, {"button.next", "   Next   "},
        {"button.next_wide", "   Next   "}, {"button.start", "  Start  "}, {"button.continue", "  Continue  "},
        {"button.accept", "  Accept  "}, {"button.restart", "   Restart   "},
        {"button.choose_heli", "  Choose Helicopter  "}, {"button.configure_controls", "  Configure Controls  "},
        {"button.apply", "   Apply   "}, {"button.ok", "   Ok   "},
    };
    for (const auto& e : table)
        if (key == e.first) return e.second;
    return nullptr;
}

} // namespace

std::string SequelScreens::tr(const Frontend& f, const std::string& key) {
    if (gulfLook() && !f.texts_.loaded(key))
        if (const char* d = gulfDefault(key)) return d;
    return f.texts_.get(key);
}

namespace {

std::vector<InfoIcon> as2InfoIcons(int page) {
    switch (page) {
        case 2: return {{0, 0, 194}, {0, 1, 286}, {0, 2, 358}, {0, 3, 430}};
        case 3: return {{0, 4, 194}, {0, 5, 295}, {0, 6, 365}};
        case 4: return {{0, 7, 194}, {0, 8, 260}};
        case 5: return {{1, 0, 194}, {1, 1, 260}, {1, 2, 328}, {1, 3, 418}};
        case 6: return {{1, 4, 194}};
        case 7: return {{2, 4, 194}, {2, 1, 256}, {2, 2, 310}, {2, 0, 378}};
        case 8: return {{2, 5, 194}, {2, 8, 256}};
        default: return {};
    }
}

std::vector<InfoIcon> gulfInfoIcons(int page) {
    (void)page;
    return {};
}

} // namespace

const Layout& layout() {
    static const Layout as2{{230, 275, 320, 365, 410, 455}, 200, {250, 295, 340, 385}, 160, true, 8,
                            {1, 2, 3, 4, 5, 6, 7, 8}, as2InfoIcons};
    // gulf/frontend.delta.md 3.3, 3.13, 3.14: positions 250 .. 475, in-game 300 .. 435, no comic,
    // seven Information pages (AirStrike 2's page 4 is gone, the later ones keep their numbers).
    static const Layout gulf{{250, 295, 340, 385, 430, 475}, 180, {300, 345, 390, 435}, 160, false, 7,
                             {1, 2, 3, 5, 6, 7, 8}, gulfInfoIcons};
    return gulfLook() ? gulf : as2;
}

std::string SequelScreens::heliName(const Frontend& f, int heli) {
    const std::string key = "heli." + std::to_string(heli);
    // The names are the executable's (as2/frontend.md 7); without the file, a generic label.
    return f.texts_.loaded(key) ? f.texts_.get(key) : f.texts_.get("heli.generic") + " " + std::to_string(heli + 1);
}

std::vector<Frontend::SequelState::Page> SequelScreens::dialogue(const Frontend& f, int m, bool end) {
    std::vector<Frontend::SequelState::Page> pages;
    const std::string base = "dialog." + std::to_string(m + 1) + (end ? ".end." : ".start.");
    for (int i = 0; i < 16; i++) {
        const std::string key = base + std::to_string(i);
        if (!f.texts_.loaded(key)) break;
        Frontend::SequelState::Page p;
        p.text = f.texts_.get(key);
        p.speaker = std::atoi(f.texts_.get(key + ".speaker").c_str()) == 1 ? 1 : 0;
        pages.push_back(std::move(p));
    }
    return pages;
}

bool SequelScreens::heliLocked(const Frontend& f, int heli) {
    return heli < 0 || heli >= f.rules().helicopterCount || !f.profile_.progress.helicopterUnlocked[heli];
}

bool SequelScreens::startWithContinue(const Frontend& f, int mission) {
    // The start button reads Continue when the chosen mission is the checkpoint's; never for
    // the tutorial (as2/frontend.md 3.18, 5.1).
    return mission > 0 && mission == f.profile_.progress.checkpoint.mission;
}

Menu SequelScreens::build(Frontend& f, Screen s) {
    Menu m = buildScreen(f, s);
    // Gulf Thunder's title bar is drawn under the screen's items and panel (the panel's static
    // fill and the buttons lie over it, gulf/frontend.delta.md 3.1; the original's screens).
    const bool bar = s == Screen::MainMenu || s == Screen::Exit || s == Screen::StartGame || s == Screen::HeliSelect ||
                     s == Screen::TopScores || s == Screen::Options || s == Screen::Controls ||
                     s == Screen::Information || s == Screen::Credits || s == Screen::InGame ||
                     s == Screen::MissionComplete;
    if (gulfLook() && bar) {
        auto back = std::move(m.drawBack);
        m.drawBack = [&f, back](MenuDrawContext& c) {
            gulfTitleBar(c.r, c.a, f.sq_->logoClock, f.sq_->tint, true);
            if (back) back(c);
        };
    }
    return m;
}

Menu SequelScreens::buildScreen(Frontend& f, Screen s) {
    switch (s) {
        case Screen::MainMenu: return mainMenu(f);
        case Screen::Exit: return exit(f);
        case Screen::StartGame: return startGame(f);
        case Screen::HeliSelect: return heliSelect(f);
        case Screen::TopScores: return topScores(f);
        case Screen::NameEntry: return nameEntry(f);
        case Screen::Options: return options(f);
        case Screen::Controls: return controls(f);
        case Screen::Information: return information(f);
        case Screen::Credits: return credits(f);
        case Screen::InGame: return inGame(f);
        case Screen::Hint: return hint(f);
        case Screen::Dialogue: return dialogueScreen(f);
        case Screen::GameOver: return gameOver(f);
        case Screen::MissionComplete: return missionComplete(f);
        case Screen::GameComplete: return gameComplete(f);
    }
    return Menu{};
}

void SequelScreens::boot(Frontend& f) {
    // The logo pages of Settings.xml, then the four comic pages, always (as2/frontend.md 3.2);
    // Gulf Thunder has no comic (gulf/frontend.delta.md 3.2).
    for (int page = 1; page <= 4 && layout().introComic; page++) {
        IntroPage p;
        p.divoGames = false;
        p.comic = page;
        f.intro_->pages.push_back(p);
    }
    if (!f.intro_->pages.empty() && f.intro_->pages.front().comic) f.intro_->clock = 0;
    f.sq_->comicMusic = false;
}

void SequelScreens::applyLook(const Frontend& f) {
    setGulfLook(f.content_.game && f.content_.game->id == GameId::GulfThunder);
}

std::string SequelScreens::loadingName(const Frontend& f) {
    const int m = std::clamp(f.campaign_.mission, 0, kMaxMissions - 1);
    const std::string& name = f.content_.missionNames[m];
    return name.empty() ? f.missionLabel(m) : name;
}

void SequelScreens::header(MenuDrawContext& c, Frontend& f) {
    if (!gulfLook()) titleLogo(c.r, c.a, f.sq_->logoClock); // Gulf Thunder's bar is drawn by build()
}

void SequelScreens::tick(Frontend& f, float dt) {
    f.sq_->logoClock += 0.5f * dt;
    if (gulfLook()) {
        // The emblem's tint moves towards the colour the screen asks for, 3 x dt a step
        // (gulf/frontend.delta.md 3.1): white on the main and in-game menus, a dimmed grey on
        // the others, dimmer on the credits.
        float target[4] = {1, 1, 1, 1};
        if (!f.menus_.empty()) {
            const Screen s = f.topScreen();
            if (s == Screen::Credits) {
                target[0] = target[1] = target[2] = 0x80 / 255.0f;
                target[3] = 0x60 / 255.0f;
            } else if (s != Screen::MainMenu && s != Screen::InGame) {
                target[0] = target[1] = target[2] = 0x80 / 255.0f;
                target[3] = 0xB0 / 255.0f;
            }
        }
        for (int k = 0; k < 4; k++) {
            float& v = f.sq_->tint[k];
            const float step = 3.0f * dt;
            v = v < target[k] ? std::min(v + step, target[k]) : std::max(v - step, target[k]);
        }
    }
    if (!f.menus_.empty() && f.topScreen() == Screen::HeliSelect) f.sq_->heliSpin += 60.0f * dt;
}

void SequelScreens::newGame(Frontend& f) {
    // G_NewGame (as2/frontend.md 5.1): a start of any other mission than the checkpoint's, or
    // of the tutorial, forgets the checkpoint; then the campaign starts from it.
    CampaignCheckpoint& cp = f.profile_.progress.checkpoint;
    const int mission = f.campaign_.mission;
    if (mission == 0 || mission != cp.mission) {
        cp = CampaignCheckpoint{};
        for (int& l : cp.lives) l = f.rules().startLives;
        f.save();
    }
    f.campaign_.start(mission, f.campaign_.difficulty, f.campaign_.players);
    for (int i = 0; i < 2; i++) {
        f.campaign_.p[i].livesAtStart = cp.lives[i];
        f.campaign_.p[i].banked = cp.score[i];
        f.campaign_.p[i].rankAccumulator = cp.rank[i];
    }
    f.twoPlayers_ = f.campaign_.players == 2;
    f.menus_.clear();
    f.startLevel(false); // the mission's loadout (not carried)
}

void SequelScreens::afterLevelStart(Frontend& f) {
    // G_StartLevel ends with the start dialogue: paused, HUD shown (as2/frontend.md 3.19).
    openDialogue(f, f.campaign_.mission, false);
}

void SequelScreens::openDialogue(Frontend& f, int mission, bool end) {
    Frontend::SequelState& s = *f.sq_;
    s.pages = dialogue(f, mission, end);
    if (s.pages.empty()) return; // a start slot without pages: the mission runs at once
    s.dialogueMission = mission;
    s.dialogueEnd = end;
    s.page = 0;
    s.fade = 0;
    s.closing = false;
    s.typed = 0;
    s.charClock = 0;
    s.pageTimer = 0;
    f.setPausedFlag(true);
    f.open(Screen::Dialogue);
}

void SequelScreens::onEndLevel(Frontend& f, const MissionReport& report) {
    f.report_ = report;
    f.haveCarried_ = report.hasUpgrades;
    for (int i = 0; i < 2; i++) {
        f.carriedWeapon_[i] = report.weapon[i];
        for (int k = 0; k < kMaxWeaponSlots; k++) f.carriedUpgrades_[i][k] = report.upgrades[i][k];
    }
    // EndLevel (as2/frontend.md 5.3): the checkpoint for the next mission, before the dialogue;
    // saved as "none" when a cheat was used (6.1). Ours: written at once (issue 240 item 8).
    if (report.hasCheckpoint) {
        CampaignCheckpoint& cp = f.profile_.progress.checkpoint;
        cp = CampaignCheckpoint{};
        if (!report.cheatUsed) {
            cp.mission = report.checkpointMission;
            for (int i = 0; i < 2; i++) {
                cp.lives[i] = report.checkpointLives[i];
                cp.score[i] = report.checkpointScore[i];
                cp.rank[i] = report.checkpointRank[i];
            }
        }
        f.save();
    }
    f.hudHidden_ = true;
    f.setPausedFlag(true);
    const int mission = std::clamp(f.campaign_.mission, 0, f.rules().missionCount - 1);
    if (!dialogue(f, mission, true).empty()) {
        openDialogue(f, mission, true);
        return;
    }
    missionCompleted(f);
}

void SequelScreens::missionCompleted(Frontend& f) {
    // G_MissionComplete after the end dialogue: the helicopter of `enableHelic` and the next
    // mission are unlocked, then Game Complete after the last mission, else Mission Complete.
    const int mission = std::clamp(f.campaign_.mission, 0, f.rules().missionCount - 1);
    f.profile_.progress.unlockAfterMission(mission, f.content_.enableHelic[mission]);
    f.refreshLocks();
    f.save();
    f.typedChars_ = 0;
    f.sq_->congratsTyped = 0;
    f.hudHidden_ = true;
    f.setPausedFlag(true);
    f.open(mission == f.rules().missionCount - 1 ? Screen::GameComplete : Screen::MissionComplete);
}

} // namespace as3d::ui
