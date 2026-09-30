// The sequels' screens over a mission: in-game menu (S13), tutorial hint (S12), game over
// (S14), mission complete (S15), game complete (S16). docs/spec/as2/frontend.md 3.8 to 3.10,
// 3.13, 3.15, and what each button does to the campaign (5.4).
#include <algorithm>
#include <cmath>

#include "as2_screens.h"

namespace as3d::ui {

namespace {

using namespace as2;

constexpr u32 kCentre = itemflag::AlignCenter;

// A comic tile of gfx\ui\comix at its natural size.
void comic(Renderer2D& r, const UiAssets& a, const std::string& name, float x, float y, Color c, Blend b) {
    if (const Texture2D* t = a.texture("gfx\\ui\\comix\\" + name + ".tga")) pic(r, *t, x, y, c, b);
}

// Game Complete: 11 lines, typed from mt = 4 at 8 characters per second, one after the other.
std::vector<std::string> congratulations(const Texts& t) {
    std::vector<std::string> lines;
    for (int i = 0; i <= 10; i++) lines.push_back(t.loaded("congrats." + std::to_string(i)) ? t.get("congrats." + std::to_string(i)) : " ");
    if (!t.loaded("congrats.2")) {
        lines[0] = t.get("congrats.0");
        lines[2] = t.get("congrats.missing");
    }
    return lines;
}

int typedCount(const std::vector<std::string>& lines, size_t i, float mt) {
    float start = 4.0f;
    for (size_t j = 0; j < i; j++) start += static_cast<float>(lines[j].size()) / 8.0f;
    const float t = mt - start;
    if (t < 0) return 0;
    return std::min(static_cast<int>(8.0f * t) + 1, static_cast<int>(lines[i].size()));
}

} // namespace

// ---------------------------------------------------------------------------
// In-game menu
// ---------------------------------------------------------------------------
Menu SequelScreens::inGame(Frontend& f) {
    Menu m;
    m.addSequelButton(1, 400, 250, tr(f, "button.resume"), kCentre, 160);
    m.addSequelButton(2, 400, 295, tr(f, "button.options"), kCentre, 160);
    m.addSequelButton(4, 400, 340, tr(f, "button.restart"), kCentre, 160);
    m.addSequelButton(3, 400, 385, tr(f, "button.quit"), kCentre, 160);
    auto resume = [&f]() {
        f.menus_.pop();
        f.resumePlay();
    };
    m.onItem = [&f, resume](Menu&, MenuItem& it, int ev) {
        if (ev != kActivate) return;
        switch (it.id) {
            case 1: resume(); break;
            case 2: f.optionsInGame_ = true; f.open(Screen::Options); break;
            case 4: f.menus_.clear(); f.startLevel(true); break;  // the mission's loadout again
            case 3: f.quitToMainMenu(false, false); break;        // no banking, no high-score check
            default: break;
        }
    };
    m.onKey = [resume](Menu&, int code) {
        if (code != keys::Escape && code != keys::Mouse2) return false;
        resume();
        return true;
    };
    m.drawFront = [&f](MenuDrawContext& c) { titleLogo(c.r, c.a, f.sq_->logoClock); };
    return m;
}

// ---------------------------------------------------------------------------
// Tutorial hint box
// ---------------------------------------------------------------------------
Menu SequelScreens::hint(Frontend& f) {
    Menu m;
    const HintLayout layout = layoutHint(FontMetrics::original(), f.hintText_, HintStyle::SequelPanel);
    m.addSequelButton(1, 400, 520, tr(f, "button.ok"), kCentre);
    auto close = [&f]() {
        f.menus_.pop();
        f.resumePlay();
    };
    m.onItem = [close](Menu&, MenuItem& it, int ev) {
        if (ev == kActivate && it.id == 1) close();
    };
    // Enter, Esc and Space close it; right click too, and resumes (spec README deviation).
    m.onKey = [close](Menu&, int code) {
        if (code == keys::Escape || code == keys::Mouse2 || code == keys::Space || code == keys::Enter) {
            close();
            return true;
        }
        return false;
    };
    m.drawFront = [&f, layout](MenuDrawContext& c) {
        panel(c.r, c.a, layout.box.x, layout.box.y, layout.box.w, layout.box.h, c.menu.open, tr(f, "title.hint"));
        if (c.menu.open < 1) return;
        float y = layout.textTop;
        for (const std::string& line : layout.lines) {
            text(c.r, c.a, 400, y, line, orange(), Align::Center, true);
            y += 18.0f;
        }
    };
    return m;
}

// ---------------------------------------------------------------------------
// Game over
// ---------------------------------------------------------------------------
Menu SequelScreens::gameOver(Frontend& f) {
    Menu m;
    m.swallowBack = true;
    // Usable at once (the first game hid them for 2 s).
    m.addSequelButton(1, 550, 520, tr(f, "button.restart"), kCentre);
    m.addSequelButton(2, 250, 520, tr(f, "button.quit_wide"), kCentre);
    m.onItem = [&f](Menu&, MenuItem& it, int ev) {
        if (ev != kActivate) return;
        if (it.id == 1) {
            f.menus_.clear();
            f.startLevel(true); // the lives it began with, the mission's loadout
        } else if (it.id == 2) {
            f.quitToMainMenu(true, true); // banked, then the high-score check
        }
    };
    m.drawBack = [](MenuDrawContext& c) {
        const float mt = c.mt;
        const float fade = std::min(mt, 1.0f);
        const Color col{1, 1, 1, fade};
        const Blend b = mt < 1.0f ? Blend::Alpha : Blend::Opaque;
        for (int k = 1; k <= 3; k++) {
            const float x = 16.0f + 256.0f * static_cast<float>(k - 1);
            comic(c.r, c.a, "gameover_" + std::to_string(k) + "_1", x, 108, col, b);
            comic(c.r, c.a, "gameover_" + std::to_string(k) + "_2", x, 364, col, b);
        }
        comic(c.r, c.a, "gamov", 144, 260, grey(fade), Blend::Add);
    };
    m.drawFront = [&f](MenuDrawContext& c) { panel(c.r, c.a, 16, 108, 768, 384, c.mt, tr(f, "title.game_over")); };
    return m;
}

// ---------------------------------------------------------------------------
// Mission complete
// ---------------------------------------------------------------------------
Menu SequelScreens::missionComplete(Frontend& f) {
    Menu m;
    m.swallowBack = true;
    f.refreshLocks();
    m.addSequelButton(4, 400, 440, tr(f, "button.choose_heli"), kCentre);
    m.addSequelButton(2, 120, 520, tr(f, "button.restart"), kCentre);
    m.addSequelButton(1, 400, 520, tr(f, "button.quit_wide"), kCentre);
    m.addSequelButton(3, 680, 520, tr(f, "button.next_wide"), kCentre);
    m.onItem = [&f](Menu&, MenuItem& it, int ev) {
        if (ev != kActivate) return;
        switch (it.id) {
            case 3: f.continueCampaign(); break;                       // bank, next mission, upgrades kept
            case 2: f.menus_.clear(); f.startLevel(true); break;       // no banking, the loadout again
            case 1: f.quitToMainMenu(false, false); break;             // no banking; the checkpoint stays
            case 4: f.sq_->heliAccept = true; f.open(Screen::HeliSelect); break;
            default: break;
        }
    };
    const int mission = std::clamp(f.campaign_.mission, 0, f.rules().missionCount - 1);
    const int heli = f.content_.enableHelic[mission];
    const bool newHeli = heli >= 0 && heli < f.rules().helicopterCount; // even when already unlocked (issue 240 item 11)
    m.drawFront = [&f, newHeli](MenuDrawContext& c) {
        // The statistics, one-player mode only, rows appearing with the menu time.
        if (f.campaign_.players == 1) {
            const LevelPlayerResult& p = f.report_.players[0];
            const LevelTotals& t = f.report_.totals;
            if (c.mt > 1.0f) {
                text(c.r, c.a, 240, 220, tr(f, "stat.enemies"), orange());
                if (t.enemyTotal > 0) {
                    const int kills = std::min(p.kills, t.enemyTotal);
                    text(c.r, c.a, 560, 220, std::to_string(kills * 100 / t.enemyTotal) + "%", orange(), Align::Right);
                }
            }
            if (c.mt > 1.4f) {
                text(c.r, c.a, 240, 250, tr(f, "stat.stars"), orange());
                text(c.r, c.a, 560, 250, std::to_string(static_cast<int>(p.stars)) + "/" + std::to_string(t.starTotal),
                     orange(), Align::Right);
            }
            if (c.mt > 1.8f) {
                text(c.r, c.a, 240, 280, tr(f, "stat.rank"), orange());
                const int rank = rankIndex(f.campaign_.missionRankValue(p, t), f.report_.cheatUsed);
                text(c.r, c.a, 560, 280, tr(f, "rank." + std::to_string(rank)), orange(), Align::Right);
            }
        }
        if (newHeli) text(c.r, c.a, 400, 400, tr(f, "msg.new_heli"), Color{}, Align::Center);
        panel(c.r, c.a, 180, 160, 440, 200, c.menu.open, tr(f, "title.mission_complete"));
        titleLogo(c.r, c.a, f.sq_->logoClock);
    };
    return m;
}

// ---------------------------------------------------------------------------
// Game complete
// ---------------------------------------------------------------------------
Menu SequelScreens::gameComplete(Frontend& f) {
    Menu m;
    m.swallowBack = true;
    // Drawn from mt = 4 and usable only then (issue 240 item 7).
    m.addSequelButton(1, 400, 520, tr(f, "button.continue"), kCentre | itemflag::Disabled | itemflag::Hidden);
    f.sq_->congratsTyped = 0;
    m.onItem = [&f](Menu&, MenuItem& it, int ev) {
        if (ev == kActivate && it.id == 1) f.quitToMainMenu(true, true); // banked, then the high-score check
    };
    m.onUpdate = [&f](Menu& menu, float, float mt) {
        if (mt >= 4.0f)
            for (MenuItem& it : menu.items)
                if (it.hidden()) it.setShown(true);
        // sounds\type.wav for each newly shown character but spaces and a line's first.
        const std::vector<std::string> lines = congratulations(f.texts_);
        int n = 0;
        for (size_t i = 0; i < lines.size(); i++) {
            const int k = typedCount(lines, i, mt);
            for (int j = 0; j < k; j++)
                if (++n > f.sq_->congratsTyped && j > 0 && lines[i][static_cast<size_t>(j)] != ' ')
                    f.menus_.playSound("sounds\\type.wav");
        }
        f.sq_->congratsTyped = std::max(f.sq_->congratsTyped, n);
    };
    m.drawBack = [&f](MenuDrawContext& c) {
        const float mt = c.mt;
        if (mt < 2.0f) {
            c.r.fullscreen({0, 0, 0, mt / 2.0f}, Blend::Alpha); // the frozen mission fades to black
            return;
        }
        c.r.fullscreen({0, 0, 0, 1}, Blend::Opaque);
        const float alpha = mt < 4.0f ? (mt - 2.0f) / 2.0f : 1.0f;
        const Blend b = mt < 4.0f ? Blend::Alpha : Blend::Opaque;
        for (int k = 1; k <= 3; k++) {
            const float x = 267.0f * static_cast<float>(k - 1);
            for (int row = 1; row <= 2; row++)
                if (const Texture2D* t = c.a.texture("gfx\\ui\\comix\\gamedone_" + std::to_string(k) + "_" + std::to_string(row) + ".tga"))
                    picStretched(c.r, *t, x, row == 1 ? 80.0f : 347.0f, 267, row == 1 ? 267.0f : 133.0f, {1, 1, 1, alpha}, b);
        }
        if (mt < 4.0f) return;
        const std::vector<std::string> lines = congratulations(f.texts_);
        const FontMetrics& fm = FontMetrics::original();
        for (size_t i = 0; i < lines.size(); i++) {
            const int k = typedCount(lines, i, mt);
            if (k <= 0) continue;
            // Centred with the full line's width: each line types out from its final left edge.
            const float x = static_cast<float>(static_cast<int>(400 - measureText(fm, lines[i], 1.0f, true) * 0.5f));
            TextStyle st;
            st.color = Color{};
            drawTextShadowed(c.r, c.a.uiFont(), x, 160 + 18.0f * static_cast<float>(i), lines[i].substr(0, static_cast<size_t>(k)), st);
        }
    };
    return m;
}

} // namespace as3d::ui
