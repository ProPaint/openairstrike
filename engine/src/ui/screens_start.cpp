// Start Game (S3), frontend.md 3.4.
#include <algorithm>
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
    // Ours (online co-op, docs/spec/issues/180-online-coop.md 8.2): a third value "Online" while
    // the host offers it; without a second player's devices it takes the place of "2 Players".
    const std::string onlineLabel = texts_.get("label.players.online");
    const bool onlineOffered = content_.onlineMode;
    auto modes = [this, onlineOffered, onlineLabel](bool two) {
        std::vector<std::string> v{texts_.get("label.players.1")};
        if (two || !onlineOffered) v.push_back(texts_.get("label.players.2"));
        if (onlineOffered) v.push_back(onlineLabel);
        return v;
    };
    m.addSpinner(5, 160, 224, texts_.get("label.game_mode"), modes(content_.twoPlayerMode), twoPlayers_ ? 1 : 0);
    if (!content_.twoPlayerMode && !onlineOffered) m.items.back().setShown(false);
    m.addHeliGrid(6, 224, 304, {heli_, &heliAlternator_, heliLocked_, &twoPlayers_, rules().helicopterCount});
    m.addButton(1, 50, 450, 128, 64, "menu\\back_1.tga", "menu\\back_2.tga");
    m.addButton(2, 605, 450, 150, 64, "menu\\start_1.tga", "menu\\start_2.tga");
    m.onItem = [this, onlineLabel](Menu& menu, MenuItem& it, int ev) {
        if (ev != kActivate) return;
        switch (it.id) {
            case 1: menus_.pop(); break;
            case 4: difficultyChoice_ = it.index; break;
            case 5: // the grid shows P2 at once; online is one player's choices for the lobby
                twoPlayers_ = it.index == 1 && it.values[static_cast<size_t>(it.index)] != onlineLabel;
                break;
            case 2: {
                const MenuItem* list = menu.find(3);
                const int mission = list ? list->selected : 0;
                const MenuItem* mode = menu.find(5);
                if (mode && !mode->hidden() && mode->values[static_cast<size_t>(mode->index)] == onlineLabel) {
                    // Ours: the choices of this screen are the lobby's initial values; helicopters
                    // are chosen in the lobby, the Players screen is bypassed.
                    onlineInitial_ = OnlineLobby{};
                    onlineInitial_.mission = mission;
                    onlineInitial_.difficulty = difficultyChoice_;
                    onlineInitial_.heli[0] = heli_[0];
                    onlineInitial_.heli[1] = heli_[1];
                    open(Screen::Online);
                    break;
                }
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
    m.onUpdate = [this, modes, onlineLabel](Menu& menu, float, float) {
        MenuItem* mode = menu.find(5);
        if (!mode) return;
        const bool two = twoPlayerChoice();
        if (content_.onlineMode) {
            // Online offered: the spinner stays; "2 Players" comes and goes inside it.
            const std::vector<std::string> want = modes(two);
            if (mode->values != want) {
                const bool wasOnline = mode->values[static_cast<size_t>(mode->index)] == onlineLabel;
                mode->values = want;
                mode->index = wasOnline ? static_cast<int>(want.size()) - 1 : std::min(mode->index, static_cast<int>(want.size()) - 1);
                if (!two && twoPlayers_) twoPlayers_ = false;
                if (!two && !wasOnline) mode->index = 0;
            }
            return;
        }
        const bool offer = two;
        if (mode->hidden() == !offer) return;
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
