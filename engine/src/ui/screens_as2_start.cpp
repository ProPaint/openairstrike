// The sequels' Start Game (S3) and helicopter selection (S3b). docs/spec/as2/frontend.md 3.4,
// 3.18, 5.1, 5.9.
#include <algorithm>
#include <cmath>

#include "as2_screens.h"

namespace as3d::ui {

namespace {

using namespace as2;

enum StartId { kBack = 1, kNext = 2, kDifficulty = 3, kGameMode = 4, kMissionList = 5 };
enum HeliId { kHeliStart = 1, kHeliBack = 2, kPlayerSpinner = 5, kArrowNext = 6, kArrowPrev = 7, kAccept = 8 };

SpecUv atlasUv(const Piece& p) { return texelUv(p.x, p.y, p.w, p.h, 256, 256); }

} // namespace

// ---------------------------------------------------------------------------
// Start Game: mission, difficulty, game mode
// ---------------------------------------------------------------------------
Menu SequelScreens::startGame(Frontend& f) {
    Menu m;
    f.refreshLocks();
    std::vector<ListEntry> missions;
    for (int i = 0; i < f.rules().missionCount; i++) {
        // The level's own name ("Mission 1: Tutorial"); locked missions are disabled.
        const std::string& name = f.content_.missionNames[i];
        missions.push_back({name.empty() ? f.missionLabel(i) : name, f.profile_.progress.missionUnlocked[i]});
    }
    // The checkpoint's mission is preselected when it is 1..17 (0-based), else the last choice.
    const int cp = f.profile_.progress.checkpoint.mission;
    MenuItem& list = m.addList(kMissionList, 190, 200, 420, 144, {});
    list.entries = std::move(missions);
    list.selected = cp >= 1 && cp < f.rules().missionCount ? cp : f.sq_->missionChoice;
    widgets::listFixSelection(list);
    list.flags |= itemflag::NoHoverSound;
    std::vector<std::string> diffs;
    for (int i = 0; i < f.rules().difficultyCount; i++) diffs.push_back(tr(f, "difficulty." + std::to_string(i)));
    f.difficultyChoice_ = f.rules().defaultDifficulty; // Normal at every opening
    m.addSpinner(kDifficulty, 400, 375, tr(f, "label.difficulty"), diffs, f.difficultyChoice_).flags |= itemflag::NoHoverSound;
    // "Cooperative" only where the host offers two players.
    std::vector<std::string> modes{tr(f, "mode.0")};
    if (f.content_.twoPlayerMode) modes.push_back(tr(f, "mode.1"));
    else f.twoPlayers_ = false;
    m.addSpinner(kGameMode, 400, 405, tr(f, "label.game_mode"), modes, f.twoPlayers_ ? 1 : 0).flags |= itemflag::NoHoverSound;
    m.addSequelButton(kBack, 40, 520, tr(f, "button.back"));
    m.addSequelButton(kNext, 645, 520, tr(f, "button.next"));
    m.onItem = [&f](Menu& menu, MenuItem& it, int ev) {
        if (ev != kActivate) return;
        switch (it.id) {
            case kBack: f.menus_.pop(); break;
            case kMissionList: f.sq_->missionChoice = it.selected; break;
            case kDifficulty: f.difficultyChoice_ = it.index; break;
            case kGameMode: f.twoPlayers_ = it.index == 1; break;
            case kNext: {
                // Nothing is loaded yet: the choices, then the selection in start mode.
                const MenuItem* l = menu.find(kMissionList);
                f.sq_->missionChoice = l ? l->selected : 0;
                f.campaign_.mission = f.sq_->missionChoice;
                f.campaign_.difficulty = f.difficultyChoice_;
                f.campaign_.players = f.twoPlayers_ ? 2 : 1;
                f.sq_->heliAccept = false;
                f.open(Screen::HeliSelect);
                break;
            }
            default: break;
        }
    };
    m.drawFront = [&f](MenuDrawContext& c) {
        panel(c.r, c.a, 150, 180, 500, 270, c.menu.open, tr(f, "title.start_game"));
        titleLogo(c.r, c.a, f.sq_->logoClock);
    };
    return m;
}

// ---------------------------------------------------------------------------
// Helicopter selection
// ---------------------------------------------------------------------------
Menu SequelScreens::heliSelect(Frontend& f) {
    Menu m;
    Frontend::SequelState& s = *f.sq_;
    f.refreshLocks();
    const bool accept = s.heliAccept;
    const bool two = f.twoPlayers_;
    s.heliShown = 0; // issue 240 item 5: always player 1 first
    s.heliOpened[0] = f.heli_[0];
    s.heliOpened[1] = f.heli_[1];
    m.addPicture(kArrowNext, 600, 278, 16, 40, "gfx\\ui\\interface.tga", atlasUv(kArrowRight));
    m.addPicture(kArrowPrev, 180, 278, 20, 44, "gfx\\ui\\interface.tga", atlasUv(kArrowLeft));
    if (f.touch_)
        for (MenuItem& it : m.items) it.hit = {it.x - 22, it.y - 20, it.w + 44, it.h + 40}; // ours: finger-sized
    std::string caption;
    if (accept) caption = tr(f, "button.accept");
    else if (startWithContinue(f, f.campaign_.mission)) caption = tr(f, "button.continue");
    else caption = tr(f, "button.start");
    m.addSequelButton(accept ? kAccept : kHeliStart, 760, 520, caption, itemflag::AlignRight);
    if (!accept) m.addSequelButton(kHeliBack, 40, 520, tr(f, "button.back"));
    if (two)
        m.addSpinner(kPlayerSpinner, 400, 435, tr(f, "label.player"), {tr(f, "ctl.player.1"), tr(f, "ctl.player.2")}, 0)
            .flags |= itemflag::NoHoverSound;
    // Leaving keeps no locked choice (issue 240 item 4).
    auto leave = [&f]() {
        for (int p = 0; p < 2; p++)
            if (heliLocked(f, f.heli_[p])) f.heli_[p] = f.sq_->heliOpened[p];
        f.menus_.pop();
    };
    auto refresh = [&f, two](Menu& menu) {
        const bool ok = !heliLocked(f, f.heli_[0]) && (!two || !heliLocked(f, f.heli_[1]));
        for (MenuItem& it : menu.items)
            if (it.id == kHeliStart || it.id == kAccept) it.setEnabled(ok);
    };
    refresh(m);
    m.onUpdate = [refresh](Menu& menu, float, float) { refresh(menu); };
    m.onItem = [&f, leave](Menu&, MenuItem& it, int ev) {
        if (ev != kActivate) return;
        const int count = std::max(f.rules().helicopterCount, 1);
        int& h = f.heli_[std::clamp(f.sq_->heliShown, 0, 1)];
        switch (it.id) {
            // The arrows write the shown player's choice at once; locked ones can be browsed.
            case kArrowNext: h = (h + 1) % count; break;
            case kArrowPrev: h = (h - 1 + count) % count; break;
            case kPlayerSpinner: f.sq_->heliShown = it.index; break;
            case kHeliBack:
            case kAccept: leave(); break;
            case kHeliStart: newGame(f); break;
            default: break;
        }
    };
    m.onKey = [leave](Menu&, int code) {
        if (code != keys::Escape && code != keys::Mouse2) return false;
        leave();
        return true;
    };
    m.drawBack = [](MenuDrawContext& c) {
        if (c.menu.open < 1) return;
        // The dot grid behind the preview, 120-px tiles of texels 0..120 (issue 240 item 6).
        const Texture2D* t = c.a.texture("gfx\\ui\\grid.tga");
        if (!t) return;
        const float x0 = 210, y0 = 190, w = 381, h = 231;
        for (float y = 0; y < h; y += 120)
            for (float x = 0; x < w; x += 120) {
                const float tw = std::min(120.0f, w - x), th = std::min(120.0f, h - y);
                c.r.quad(x0 + x, y0 + y, tw, th, 0, 0, tw / 128.0f, th / 128.0f, t, {0, 0.376f, 0, 1}, Blend::Add);
            }
    };
    m.drawFront = [&f, two](MenuDrawContext& c) {
        const Frontend::SequelState& st = *f.sq_;
        if (c.menu.open >= 1) {
            const int h = std::clamp(f.heli_[std::clamp(st.heliShown, 0, 1)], 0, f.rules().helicopterCount - 1);
            if (heliLocked(f, h)) {
                if (const Texture2D* t = c.a.texture("gfx\\ui\\helicna.tga")) pic(c.r, *t, 272, 241, {0, 0.376f, 0, 1}, Blend::Alpha);
                text(c.r, c.a, 400, 300, tr(f, "heli.na"), red(), Align::Center);
            }
            text(c.r, c.a, 220, 200, heliName(f, h), green());
            text(c.r, c.a, 220, 378, tr(f, "heli.speed"), green());
            text(c.r, c.a, 220, 398, tr(f, "heli.armor"), green());
            const Color bar = packed(0x6000FF00u);
            const FrontendContent::HeliInfo& info = f.content_.heli[h];
            const float speed = info.known && info.hasSpeed ? info.speed : 1.0f;
            const float armor = info.known ? static_cast<float>(info.health) : 0.0f;
            c.r.outline(300, 381, 280, 10, bar, Blend::Alpha);
            c.r.outline(300, 401, 280, 10, bar, Blend::Alpha);
            c.r.rect(300, 381, std::clamp(speed * 280.0f / 1.5f, 0.0f, 280.0f), 10, bar, Blend::Alpha);
            c.r.rect(300, 401, std::clamp(armor * 280.0f / 800.0f, 0.0f, 280.0f), 10, bar, Blend::Alpha);
        }
        panel(c.r, c.a, 160, 160, 480, two ? 310.0f : 290.0f, c.menu.open, tr(f, "title.heli"));
        titleLogo(c.r, c.a, st.logoClock);
    };
    return m;
}

} // namespace as3d::ui
