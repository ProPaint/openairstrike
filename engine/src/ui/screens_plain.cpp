// The plain front end of a FrontendStyle::PlainList game (docs/spec/as2/issues/260): the
// screens that need the first game's menu pictures, rebuilt from the menu widgets with text
// captions in the game font and our own rectangles and lines. Temporary by design: the
// sequels' real menus come later. Nothing here asks the asset cache for a texture; the
// screens shared with the first game (top scores, name entry, options, controls) are the same
// builders with plain widgets (see Frontend::plain()).
//
// Layout follows the first game's: black bars above y = 100 and below y = 500 with a title in
// the top bar, panels of 0x50000000 black, orange and rust text, buttons in the bottom bar.
#include <algorithm>
#include <cstdio>

#include "as3d/frontend.h"
#include "plain_layout.h"

namespace as3d::ui {

namespace {

constexpr int kHeliBase = 100;
const Color kGrey = grey(0x80 / 255.0f);
constexpr float kMainButtonScale = 1.5f;

bool isMember(const int* list, int mission1) {
    for (int i = 0; i < kMaxSpecialMissions && list[i] != 0; i++)
        if (list[i] == mission1) return true;
    return false;
}

std::string formatSpeed(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", static_cast<double>(v));
    return buf;
}

// A pulsing key-hint line in the bottom bar (desktop only).
void keyHint(MenuDrawContext& c, const Texts& texts) {
    if (c.touchMode) return;
    widgets::text(c, 400, 574, texts.get("plain.keys"), kGrey, Align::Center);
}

} // namespace

std::string Frontend::missionLabel(int mission) const {
    const int m = std::clamp(mission, 0, kMaxMissions - 1);
    const std::string& name = content_.missionNames[m];
    const std::string word = texts_.get("plain.mission");
    // The sequels' level names already read "Mission 7: Gold Isle"; others get their number.
    if (name.empty()) return word + " " + std::to_string(m + 1);
    if (name.compare(0, word.size(), word) == 0) return name;
    return std::to_string(m + 1) + ". " + name;
}

void Frontend::addPlainHeliRows(Menu& m, float x, float y, float w, float rowH) {
    const int count = std::clamp(rules().helicopterCount, 0, 8);
    for (int i = 0; i < count; i++) {
        const RectF hit{x, y + rowH * static_cast<float>(i), w, rowH - 4};
        const FrontendContent::HeliInfo info = content_.heli[i];
        const std::string health = texts_.get("plain.health") + " " + (info.known ? std::to_string(info.health) : "-");
        const std::string speed =
            texts_.get("plain.speed") + " " + (info.known && info.hasSpeed ? formatSpeed(info.speed) : "-");
        MenuItem& it = m.addCustom(kHeliBase + i, hit, [this, i, health, speed](MenuDrawContext& c, MenuItem& self, bool focused) {
            const bool locked = self.disabled();
            const bool chosen = heli_[0] == i;
            Color col = locked ? kGrey : (chosen ? orange() : rust());
            if (!locked && (chosen || focused)) {
                const Color box = chosen ? Color{0.376f, 0, 0, 0.5f} : packed(0x80000060u);
                c.r.rect(self.hit.x, self.hit.y, self.hit.w, self.hit.h, box, Blend::Alpha);
            }
            if (focused && !locked) {
                const Color p = pulse(2, 0, c.mt);
                c.r.outline(self.hit.x, self.hit.y, self.hit.w, self.hit.h, {p.r, p.g * 0.63f, 0, 1}, Blend::Add);
            }
            const float ty = self.hit.y + std::floor((self.hit.h - 15) * 0.5f);
            widgets::text(c, self.hit.x + 10, ty, std::to_string(i + 1), col);
            widgets::text(c, self.hit.x + 44, ty, health, col);
            widgets::text(c, self.hit.x + 176, ty, speed, col);
        });
        it.setEnabled(profile_.progress.helicopterUnlocked[i]);
    }
}

// ---------------------------------------------------------------------------
// Main menu and exit
// ---------------------------------------------------------------------------
Menu Frontend::buildPlainMain() {
    Menu m;
    m.swallowBack = true; // the main menu cannot be closed
    const char* labels[4] = {"plain.main.start", "plain.main.scores", "plain.main.options", "plain.main.exit"};
    for (int i = 0; i < 4; i++)
        m.addTextButton(i + 1, {240, 238 + 56.0f * static_cast<float>(i), 320, 42}, texts_.get(labels[i])).textScale =
            kMainButtonScale;
    // With more than one game (docs/spec/issues/163): the left slot of the bottom bar.
    if (content_.changeGame)
        m.addTextButton(kChangeGameItem, {kPlainLeft.x, kPlainLeft.y, 190, kPlainLeft.h}, texts_.get("menu.change_game"));
    m.onItem = [this](Menu&, MenuItem& it, int ev) {
        if (ev != kActivate) return;
        switch (it.id) {
            case 1: open(Screen::StartGame); break;
            case 2: open(Screen::TopScores); break;
            case 3: optionsInGame_ = false; open(Screen::Options); break;
            case 4: open(Screen::Exit); break;
            case kChangeGameItem:
                save();
                host_.changeGame();
                break;
            default: break;
        }
    };
    m.drawBack = [this](MenuDrawContext& c) {
        widgets::letterbox(c);
        const std::string title = content_.game ? content_.game->title : "";
        const float full = measureText(FontMetrics::original(), title);
        const float scale = full > 0 ? std::min(3.0f, 700.0f / full) : 3.0f;
        widgets::plainTitle(c, title, scale);
    };
    return m;
}

Menu Frontend::buildPlainExit() {
    Menu m;
    m.addTextButton(1, {250, 322, 130, 38}, texts_.get("plain.yes")).textScale = kPlainButtonScale;
    m.addTextButton(2, {420, 322, 130, 38}, texts_.get("plain.no")).textScale = kPlainButtonScale;
    m.onItem = [this](Menu&, MenuItem& it, int ev) {
        if (ev != kActivate) return;
        if (it.id == 1) {
            save();
            host_.quit();
        } else {
            menus_.pop();
        }
    };
    m.drawBack = [this](MenuDrawContext& c) {
        widgets::letterbox(c);
        widgets::panel(c, 190, 220, 420, 160);
        widgets::text(c, 400, 266, texts_.get("label.exit"), orange(), Align::Center);
    };
    return m;
}

// ---------------------------------------------------------------------------
// Start Game
// ---------------------------------------------------------------------------
Menu Frontend::buildPlainStartGame() {
    Menu m;
    refreshLocks();
    std::vector<ListEntry> missions;
    for (int i = 0; i < rules().missionCount; i++) {
        std::string label = missionLabel(i);
        if (isMember(rules().bonusMissions, i + 1)) label += " [" + texts_.get("plain.bonus") + "]";
        if (isMember(rules().bossMissions, i + 1)) label += " [" + texts_.get("plain.boss") + "]";
        missions.push_back({label, profile_.progress.missionUnlocked[i]});
    }
    m.addList(3, 30, 134, 440, 324, missions);
    std::vector<std::string> diffs;
    for (int i = 0; i < rules().difficultyCount; i++) diffs.push_back(texts_.get("difficulty." + std::to_string(i)));
    difficultyChoice_ = rules().defaultDifficulty; // Normal on every opening, as the first game's
    twoPlayers_ = false;                           // single player only until the co-op mode exists
    m.addSpinner(4, 630, 360, texts_.get("label.difficulty"), diffs, difficultyChoice_);
    addPlainHeliRows(m, 490, 134, 290, 30);
    m.addTextButton(1, kPlainLeft, texts_.get("plain.back")).textScale = kPlainButtonScale;
    m.addTextButton(2, kPlainRight, texts_.get("plain.start")).textScale = kPlainButtonScale;
    auto start = [this](Menu& menu) {
        const MenuItem* list = menu.find(3);
        startCampaign(list ? list->selected : 0, difficultyChoice_, 1);
    };
    m.onItem = [this, start](Menu& menu, MenuItem& it, int ev) {
        if (ev != kActivate) return;
        if (it.id == 1) menus_.pop();
        else if (it.id == 4) difficultyChoice_ = it.index;
        else if (it.id == 2) start(menu);
        else if (it.id >= kHeliBase) heli_[0] = it.id - kHeliBase;
    };
    // Enter on the mission list starts that mission (the list itself has no activate).
    m.onKey = [start](Menu& menu, int code) {
        if (code != keys::Enter || menu.focused < 0) return false;
        if (menu.items[static_cast<size_t>(menu.focused)].type != ItemType::List) return false;
        start(menu);
        return true;
    };
    m.drawBack = [this](MenuDrawContext& c) {
        widgets::letterbox(c);
        widgets::plainTitle(c, texts_.get("plain.title.start"));
        widgets::text(c, 30, 112, texts_.get("label.choose_mission"), orange());
        widgets::text(c, 490, 112, texts_.get("plain.heli"), orange());
        keyHint(c, texts_);
    };
    return m;
}

// ---------------------------------------------------------------------------
// Screens over a mission
// ---------------------------------------------------------------------------
Menu Frontend::buildPlainInGame() {
    Menu m;
    const char* labels[3] = {"plain.resume", "plain.main.options", "plain.quit"};
    for (int i = 0; i < 3; i++)
        m.addTextButton(i + 1, {240, 236 + 56.0f * static_cast<float>(i), 320, 42}, texts_.get(labels[i])).textScale =
            kMainButtonScale;
    auto resume = [this]() {
        menus_.pop();
        resumePlay();
    };
    m.onItem = [this, resume](Menu&, MenuItem& it, int ev) {
        if (ev != kActivate) return;
        if (it.id == 1) resume();
        else if (it.id == 2) { optionsInGame_ = true; open(Screen::Options); }
        else if (it.id == 3) quitToMainMenu(false, false); // no banking, no high-score check
    };
    m.onKey = [resume](Menu&, int code) {
        if (code != keys::Escape && code != keys::Mouse2) return false;
        resume();
        return true;
    };
    m.drawBack = [this](MenuDrawContext& c) {
        widgets::letterbox(c);
        widgets::plainTitle(c, texts_.get("plain.title.paused"));
    };
    return m;
}

Menu Frontend::buildPlainHint() {
    Menu m;
    const HintLayout layout = layoutHint(FontMetrics::original(), hintText_);
    const RectF ok{350, layout.box.y + layout.box.h - 46, 100, 32};
    m.addTextButton(1, ok, texts_.get("plain.ok")).setShown(false); // hidden until the opening animation ends
    auto close = [this]() {
        menus_.pop();
        resumePlay();
    };
    m.onItem = [close](Menu&, MenuItem& it, int ev) {
        if (ev == kActivate && it.id == 1) close();
    };
    // Enter, Esc and Space close the box; right click too (as the first game's box here).
    m.onKey = [close](Menu&, int code) {
        if (code == keys::Escape || code == keys::Mouse2 || code == keys::Space || code == keys::Enter) {
            close();
            return true;
        }
        return false;
    };
    m.onUpdate = [](Menu& menu, float, float mt) {
        if (mt >= kHintOpenSeconds && !menu.items.empty() && menu.items[0].hidden()) menu.items[0].setShown(true);
    };
    m.drawBack = [layout](MenuDrawContext& c) { drawHintPanel(c.r, c.a, layout, c.mt); };
    return m;
}

Menu Frontend::buildPlainGameOver() {
    Menu m;
    m.swallowBack = true;
    m.addTextButton(1, {240, 316, 150, 40}, texts_.get("plain.restart"), itemflag::Disabled | itemflag::Hidden).textScale =
        kPlainButtonScale;
    m.addTextButton(2, {410, 316, 150, 40}, texts_.get("plain.quit"), itemflag::Disabled | itemflag::Hidden).textScale =
        kPlainButtonScale;
    m.onUpdate = [](Menu& menu, float, float mt) {
        if (mt >= 2.0f)
            for (MenuItem& it : menu.items)
                if (it.hidden()) it.setShown(true);
    };
    m.onItem = [this](Menu&, MenuItem& it, int ev) {
        if (ev != kActivate) return;
        if (it.id == 1) {
            menus_.clear();
            startLevel(true); // same mission, lives as at its start
        } else if (it.id == 2) {
            quitToMainMenu(true, true);
        }
    };
    m.drawBack = [this](MenuDrawContext& c) {
        const float f = std::min(c.mt / 2.0f, 1.0f);
        c.r.fullscreen({0.25f, 0, 0, 0.6f * f}, Blend::Alpha);
        c.r.rect(150, 190, 500, 90, {0, 0, 0, 0.55f * f}, Blend::Alpha);
        if (c.mt > 0.3f) {
            const Color col = c.mt < 2.0f ? orange() : Color{1.0f, 0.63f, 0.0f, 1};
            widgets::shadowedText(c, 400, 213, texts_.get("plain.title.gameover"), col, Align::Center, 3.0f);
        }
    };
    return m;
}

Menu Frontend::buildPlainMissionComplete() {
    Menu m;
    m.swallowBack = true;
    refreshLocks();
    const int mission = std::clamp(campaign_.mission, 0, rules().missionCount - 1);
    const bool newHeli = content_.enableHelic[mission] >= 0 && content_.enableHelic[mission] < rules().helicopterCount;
    if (rules().helicopterCount > 1) addPlainHeliRows(m, 210, 320, 380, 26);
    m.addTextButton(2, kPlainLeft, texts_.get("plain.restart")).textScale = kPlainButtonScale;
    m.addTextButton(1, kPlainMiddle, texts_.get("plain.quit")).textScale = kPlainButtonScale;
    m.addTextButton(3, kPlainRight, texts_.get("plain.next")).textScale = kPlainButtonScale;
    m.onItem = [this](Menu&, MenuItem& it, int ev) {
        if (ev != kActivate) return;
        if (it.id == 3) continueCampaign();
        else if (it.id == 2) { menus_.clear(); startLevel(true); } // no banking
        else if (it.id == 1) quitToMainMenu(false, false);          // no banking, no check
        else if (it.id >= kHeliBase) heli_[0] = it.id - kHeliBase;
    };
    m.drawBack = [this, mission, newHeli](MenuDrawContext& c) {
        widgets::letterbox(c);
        widgets::plainTitle(c, texts_.get("plain.title.complete"));
        widgets::text(c, 400, 76, missionLabel(mission), rust(), Align::Center);
        drawStats(c, 120);
        if (newHeli && c.mt > 2.2f) widgets::text(c, 400, 252, texts_.get("plain.new_heli"), Color{}, Align::Center);
        if (rules().helicopterCount > 1) widgets::text(c, 210, 296, texts_.get("plain.heli"), orange());
    };
    return m;
}

Menu Frontend::buildPlainGameComplete() {
    Menu m;
    m.swallowBack = true;
    m.addTextButton(1, kPlainMiddle, texts_.get("plain.continue")).textScale = kPlainButtonScale;
    m.onItem = [this](Menu&, MenuItem& it, int ev) {
        if (ev == kActivate && it.id == 1) quitToMainMenu(true, true);
    };
    m.drawBack = [this](MenuDrawContext& c) {
        widgets::letterbox(c);
        widgets::plainTitle(c, texts_.get("plain.title.gamecomplete"));
        widgets::shadowedText(c, 400, 150, texts_.get("plain.congrats"), orange(), Align::Center, 1.5f);
        widgets::text(c, 400, 190, texts_.get("plain.congrats.2"), rust(), Align::Center);
        drawStats(c, 240);
    };
    return m;
}

} // namespace as3d::ui
