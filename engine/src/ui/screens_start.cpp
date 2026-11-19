// Start Game (S3), frontend.md 3.4.
#include <cstdio>

#include "as3d/frontend.h"

namespace as3d::ui {

Menu Frontend::buildStartGame() {
    Menu m;
    refreshLocks();
    std::vector<ListEntry> missions;
    for (int i = 0; i < rules().missionCount; i++) {
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
    for (int i = 0; i < rules().difficultyCount; i++) {
        std::snprintf(key, sizeof key, "difficulty.%d", i);
        diffs.push_back(texts_.get(key));
    }
    difficultyChoice_ = rules().defaultDifficulty; // reset to Normal on every opening
    m.addSpinner(4, 160, 184, texts_.get("label.difficulty"), diffs, difficultyChoice_);
    if (!content_.twoPlayerMode) twoPlayers_ = false;
    m.addSpinner(5, 160, 224, texts_.get("label.game_mode"), {texts_.get("label.players.1"), texts_.get("label.players.2")},
                 twoPlayers_ ? 1 : 0);
    if (!content_.twoPlayerMode) m.items.back().setShown(false);
    m.addHeliGrid(6, 224, 304, {heli_, &heliAlternator_, heliLocked_, &twoPlayers_, rules().helicopterCount});
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
                if (twoPlayers_ && pads_) {
                    // Ours: who flies with what first (the host has game controllers).
                    playersMission_ = mission;
                    open(Screen::Players);
                    break;
                }
                startCampaign(mission, difficultyChoice_, twoPlayers_ ? 2 : 1);
                break;
            }
            default: break;
        }
    };
    // Ours: the "2 Players" choice comes and goes with the second player's devices (a pad
    // plugged in or out while the screen is up, setPlayerPads).
    m.onUpdate = [this](Menu& menu, float, float) {
        MenuItem* mode = menu.find(5);
        const bool offer = twoPlayerChoice();
        if (!mode || mode->hidden() == !offer) return;
        mode->setShown(offer);
        if (!offer && twoPlayers_) {
            twoPlayers_ = false;
            mode->index = 0;
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
