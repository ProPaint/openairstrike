// The sequels' mission flow and progression (docs/spec/as2/frontend.md 1.3, 5): the screen
// dispatcher, the comic pages at boot, a new game with the campaign checkpoint ("Continue"),
// the start dialogue, the end of a mission (checkpoint, end dialogue, unlocks, Mission or Game
// Complete).
#include <algorithm>
#include <cstdlib>

#include "as2_screens.h"

namespace as3d::ui {

std::string SequelScreens::tr(const Frontend& f, const std::string& key) { return f.texts_.get(key); }

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
    // The logo pages of Settings.xml, then the four comic pages, always (as2/frontend.md 3.2).
    for (int page = 1; page <= 4; page++) {
        IntroPage p;
        p.divoGames = false;
        p.comic = page;
        f.intro_->pages.push_back(p);
    }
    if (!f.intro_->pages.empty() && f.intro_->pages.front().comic) f.intro_->clock = 0;
    f.sq_->comicMusic = false;
}

void SequelScreens::tick(Frontend& f, float dt) {
    f.sq_->logoClock += 0.5f * dt;
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
