// Screens over a mission: in-game menu (S13), tutorial hint (S12), game over (S14), mission
// complete (S15), game complete (S16). frontend.md 3.8 to 3.10, 3.13, 3.15.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "as3d/frontend.h"

namespace as3d::ui {

namespace {

constexpr float kPi = 3.14159265f;

void missionCompleteTitle(MenuDrawContext& c) {
    for (int layer = 0; layer < 2; layer++) {
        const Texture2D* t = c.a.texture(layer == 0 ? "menu\\miscompl_1.tga" : "menu\\miscompl_2.tga");
        if (!t) continue;
        const Color col = layer == 0 ? Color{} : pulse(0.25f, 0, c.mt);
        const Blend b = layer == 0 ? Blend::Alpha : Blend::Add;
        c.r.quadSpec(124, 64, 245, 64, 0, 0.5f, 0.765625f, 1, t, col, b);
        c.r.quadSpec(369, 64, 320, 64, 0, 0, 1, 0.5f, t, col, b);
    }
}

// Lines of the Game Complete text: "Congratulations!", a blank line, three lines from the
// executable (issue 080).
std::vector<std::string> congratulationLines(const Texts& texts) {
    std::vector<std::string> lines;
    for (int i = 0; i < 5; i++) lines.push_back(texts.get("congrats." + std::to_string(i)));
    if (!texts.loaded("congrats.2")) lines[2] = texts.get("congrats.missing");
    return lines;
}

// Characters of line i shown at menu time mt: lines type one after the other from mt = 1 at
// 8 characters per second.
int typedCount(const std::vector<std::string>& lines, size_t i, float mt) {
    float start = 1.0f;
    for (size_t j = 0; j < i; j++) start += static_cast<float>(lines[j].size()) / 8.0f;
    const float t = mt - start;
    if (t < 0) return 0;
    return std::min(static_cast<int>(8.0f * t) + 1, static_cast<int>(lines[i].size()));
}

} // namespace

void Frontend::drawStats(MenuDrawContext& c, float boxY) {
    if (campaign_.players != 1) return; // no tally in two-player mode
    const float mt = c.mt;
    c.r.rect(210, boxY, 380, 120, {0, 0, 0, std::min(80.0f * mt, 80.0f) / 255.0f}, Blend::Alpha);
    const LevelPlayerResult& p = report_.players[0];
    const LevelTotals& t = report_.totals;
    if (mt > 1.0f) {
        widgets::text(c, 240, boxY + 20, texts_.get("stat.enemies"), orange());
        if (t.enemyTotal > 0)
            widgets::text(c, 560, boxY + 20, std::to_string(p.kills * 100 / t.enemyTotal) + "%", orange(), Align::Right);
    }
    if (mt > 1.4f) {
        widgets::text(c, 240, boxY + 50, texts_.get("stat.stars"), orange());
        widgets::text(c, 560, boxY + 50,
                      std::to_string(static_cast<int>(p.stars)) + "/" + std::to_string(t.starTotal), orange(), Align::Right);
    }
    if (mt > 1.8f) {
        widgets::text(c, 240, boxY + 80, texts_.get("stat.rank"), orange());
        const int rank = rankIndex(campaign_.missionRankValue(p, t), report_.cheatUsed);
        widgets::text(c, 560, boxY + 80, texts_.get("rank." + std::to_string(rank)), orange(), Align::Right);
    }
}

Menu Frontend::buildInGame() {
    Menu m;
    m.addButton(1, 240, 270, 320, 35, "menu\\mmenu_1.tga", "menu\\mmenu_2.tga", {0, 0.1875f, 1, 0.3242f});
    m.addButton(2, 240, 305, 320, 35, "menu\\mmenu_1.tga", "menu\\mmenu_2.tga", {0, 0.5977f, 1, 0.7344f});
    m.addButton(3, 240, 340, 320, 35, "menu\\mmenu_1.tga", "menu\\mmenu_2.tga", {0, 0.0506f, 1, 0.1875f});
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
    m.drawBack = [](MenuDrawContext& c) { widgets::letterbox(c); };
    return m;
}

Menu Frontend::buildHint() {
    Menu m;
    const HintLayout layout = layoutHint(FontMetrics::original(), hintText_);
    m.addCustom(1, layout.okButton, [layout](MenuDrawContext& c, MenuItem&, bool focused) {
        drawHintOk(c.r, c.a, layout, focused, c.mt);
    });
    m.items.back().setShown(false); // hidden until the opening animation ends
    auto close = [this]() {
        menus_.pop();
        resumePlay();
    };
    m.onItem = [close](Menu&, MenuItem& it, int ev) {
        if (ev == kActivate && it.id == 1) close();
    };
    // Enter, Esc and Space close the box; right click too (ours: the original leaves the game
    // paused, see the deviation table of docs/spec/README.md).
    m.onKey = [close](Menu& menu, int code) {
        if (code == keys::Escape || code == keys::Mouse2 || code == keys::Space) { close(); return true; }
        if (code == keys::Enter) {
            // Enter before the OK button exists still closes the box.
            close();
            return true;
        }
        (void)menu;
        return false;
    };
    m.onUpdate = [](Menu& menu, float, float mt) {
        if (mt >= kHintOpenSeconds && !menu.items.empty() && menu.items[0].hidden()) menu.items[0].setShown(true);
    };
    m.drawBack = [layout](MenuDrawContext& c) { drawHintPanel(c.r, c.a, layout, c.mt); };
    return m;
}

Menu Frontend::buildGameOver() {
    Menu m;
    m.swallowBack = true;
    m.addButton(1, 130, 450, 210, 64, "menu\\restart_1.tga", "menu\\restart_2.tga", {0, 0, 0.8203f, 1},
                itemflag::Disabled | itemflag::Hidden);
    m.addButton(2, 542, 450, 128, 64, "menu\\quit_1.tga", "menu\\quit_2.tga", {0, 0, 1, 1},
                itemflag::Disabled | itemflag::Hidden);
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
    m.drawBack = [](MenuDrawContext& c) {
        const float mt = c.mt;
        const float g = mt < 2.0f ? (255.0f - 64.0f * mt) / 255.0f : 0.5f;
        c.r.fullscreen({1, g, g, 1}, Blend::Filter);
        const float alpha = mt < 2.0f ? 128.0f * mt / 255.0f : 1.0f;
        if (const Texture2D* t = c.a.texture("menu\\gameover_0.tga"))
            c.r.quadSpec(230, 200, 340, 64, 0, 0, 1, 1, t, {1, 1, 1, std::min(alpha, 1.0f)}, Blend::Alpha);
        if (const Texture2D* t = c.a.texture("menu\\gameover_3.tga"))
            c.r.quadSpec(230, 200, 340, 64, 0, 0, 1, 1, t, pulse(0.25f, kPi / 2, mt), Blend::Add);
    };
    return m;
}

Menu Frontend::buildMissionComplete() {
    Menu m;
    m.swallowBack = true;
    refreshLocks();
    m.addHeliGrid(7, 224, 304, {heli_, &heliAlternator_, heliLocked_, &twoPlayers_});
    m.addButton(2, 64, 470, 210, 64, "menu\\restart_1.tga", "menu\\restart_2.tga", {0, 0, 0.8203f, 1});
    m.addButton(1, 332, 470, 128, 64, "menu\\quit_1.tga", "menu\\quit_2.tga");
    m.addButton(3, 482, 470, 256, 64, "menu\\continue_1.tga", "menu\\continue_2.tga");
    m.onItem = [this](Menu&, MenuItem& it, int ev) {
        if (ev != kActivate) return;
        if (it.id == 3) continueCampaign();
        else if (it.id == 2) { menus_.clear(); startLevel(true); } // no banking
        else if (it.id == 1) quitToMainMenu(false, false);          // no banking, no check
    };
    m.drawBack = [this](MenuDrawContext& c) {
        missionCompleteTitle(c);
        drawStats(c, 156);
    };
    return m;
}

Menu Frontend::buildGameComplete() {
    Menu m;
    m.swallowBack = true;
    m.addButton(1, 272, 470, 256, 64, "menu\\continue_1.tga", "menu\\continue_2.tga");
    typedChars_ = 0;
    m.onItem = [this](Menu&, MenuItem& it, int ev) {
        if (ev == kActivate && it.id == 1) quitToMainMenu(true, true);
    };
    m.onUpdate = [this](Menu&, float, float mt) {
        // sounds\type.wav per new non-space character.
        const std::vector<std::string> lines = congratulationLines(texts_);
        int n = 0;
        for (size_t i = 0; i < lines.size(); i++) {
            const int k = typedCount(lines, i, mt);
            for (int j = 0; j < k; j++) {
                if (++n > typedChars_ && lines[i][static_cast<size_t>(j)] != ' ') menus_.playSound("sounds\\type.wav");
            }
        }
        typedChars_ = std::max(typedChars_, n);
    };
    m.drawBack = [this](MenuDrawContext& c) {
        missionCompleteTitle(c);
        const std::vector<std::string> lines = congratulationLines(texts_);
        const FontMetrics& fm = FontMetrics::original();
        for (size_t i = 0; i < lines.size(); i++) {
            const int k = typedCount(lines, i, c.mt);
            if (k <= 0) continue;
            const float full = measureText(fm, lines[i]);
            const float x = static_cast<float>(static_cast<int>(400 - full * 0.5f));
            TextStyle st;
            st.color = Color{};
            drawTextShadowed(c.r, c.a.uiFont(), x, 160 + 18.0f * static_cast<float>(i), lines[i].substr(0, static_cast<size_t>(k)), st);
        }
        drawStats(c, 296);
    };
    return m;
}

} // namespace as3d::ui
