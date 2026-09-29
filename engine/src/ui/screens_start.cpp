// Start Game (S3), frontend.md 3.4.
#include <cstdio>

#include "as3d/frontend.h"

namespace as3d::ui {

Menu Frontend::buildStartGame() {
    Menu m;
    refreshLocks();
    std::vector<ListEntry> missions;
    for (int i = 0; i < kMissionCount; i++) {
        std::string name = content_.missionNames[i];
        if (name.empty()) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "Mission %d", i + 1);
            name = buf;
        }
        missions.push_back({name, profile_.progress.missionUnlocked[i]});
    }
    // The selection starts at mission 1 each time.
    m.addList(3, 295, 160, 420, 106, missions);
    std::vector<std::string> diffs;
    char key[32];
    for (int i = 0; i < kDifficultyCount; i++) {
        std::snprintf(key, sizeof key, "difficulty.%d", i);
        diffs.push_back(texts_.get(key));
    }
    difficultyChoice_ = kDefaultDifficulty; // reset to Normal on every opening
    m.addSpinner(4, 160, 184, texts_.get("label.difficulty"), diffs, difficultyChoice_);
    m.addSpinner(5, 160, 224, texts_.get("label.game_mode"), {texts_.get("label.players.1"), texts_.get("label.players.2")},
                 twoPlayers_ ? 1 : 0);
    m.addHeliGrid(6, 224, 304, {heli_, &heliAlternator_, heliLocked_, &twoPlayers_});
    m.addButton(1, 50, 450, 128, 64, "menu\\back_1.tga", "menu\\back_2.tga");
    m.addButton(2, 605, 450, 150, 64, "menu\\start_1.tga", "menu\\start_2.tga");
    m.onItem = [this](Menu& menu, MenuItem& it, int ev) {
        if (ev != kActivate) return;
        switch (it.id) {
            case 1: menus_.pop(); break;
            case 4: difficultyChoice_ = it.index; break;
            case 5: twoPlayers_ = it.index == 1; break; // the grid shows P2 at once
            case 2: {
                const MenuItem* list = menu.find(3);
                const int mission = list ? list->selected : 0;
                startCampaign(mission, difficultyChoice_, twoPlayers_ ? 2 : 1);
                break;
            }
            default: break;
        }
    };
    m.drawBack = [this](MenuDrawContext& c) {
        widgets::letterbox(c);
        widgets::header(c, "menu\\starth", 210, 63, 380, 64);
        widgets::text(c, 300, 140, texts_.get("label.choose_mission"), orange());
    };
    return m;
}

} // namespace as3d::ui
