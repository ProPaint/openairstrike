// Players (ours, two players on one screen; docs/web.md "Two players", docs/android.md): between
// Start Game with "2 Players" and the mission, who flies with what. In the Start Game screen's
// look (letterbox, the START GAME header, Back and Start where Start Game has them) with two
// panels, Player 1 on the left and Player 2 on the right, each listing its devices: the touch
// screen (player 1, touch mode), the keyboard with that player's own keys (both players have a
// key set) and its game controllers (as3d/player_pads.h).
//
// A pad pushed left (D-pad or stick) goes to player 1, pushed right to player 2; a click or a tap
// on a pad's row moves it to the other player. A pad whose button is held lights its row up, so
// "which one is mine" is a button press away. A (Enter) starts (the Start button, also when no
// item has the focus yet), B / Start / Esc go back to Start Game. Start is refused while a
// player has nothing to fly with (the panel says what it needs).
#include <algorithm>
#include <cstdio>

#include "as3d/frontend.h"

namespace as3d::ui {

namespace {

enum PlayersId { kBack = 1, kStart = 2 };
constexpr float kPanelX[2] = {60, 410}, kPanelY = 165, kPanelW = 330, kPanelH = 250;
constexpr float kRowH = 20;

// The panel's lines before the pads (title, touch, keyboard) and the pad rows after them.
float firstPadRowY(bool touch, bool keyboard, int player) {
    float y = kPanelY + 40;
    if (touch && player == 0) y += kRowH + 6;
    if (keyboard) y += 4 * kRowH + 6;
    return y;
}

RectF padRowRect(int player, float y) { return {kPanelX[player] + 10, y - 2, kPanelW - 20, kRowH}; }

// `s` cut with "..." to fit `w` virtual pixels in the menu font.
std::string fit(std::string s, float w) {
    if (measureText(FontMetrics::original(), s) <= w) return s;
    while (!s.empty() && measureText(FontMetrics::original(), s + "...") > w) s.pop_back();
    return s + "...";
}

} // namespace

Menu Frontend::buildPlayers() {
    Menu m;
    if (pads_) pads_->clearMoves(); // pushes made before the screen do not count
    // Start first: the pad's first Up / Down lands on it.
    m.addButton(kStart, 605, 450, 150, 64, "menu\\start_1.tga", "menu\\start_2.tga");
    m.addButton(kBack, 50, 450, 128, 64, "menu\\back_1.tga", "menu\\back_2.tga");

    const bool keyboard = pads_ ? pads_->keyboard() : !touch_;
    // Where each pad's row is now (index into PlayerPads::pads), for clicks and drawing.
    auto rows = [this, keyboard](std::vector<std::pair<int, RectF>>& out) {
        out.clear();
        if (!pads_) return;
        for (int p = 0; p < 2; ++p) {
            float y = firstPadRowY(touch_, keyboard, p);
            for (int i = 0; i < pads_->count(); ++i) {
                if (pads_->pads()[static_cast<size_t>(i)].player != p) continue;
                out.push_back({i, padRowRect(p, y)});
                y += kRowH;
            }
        }
    };
    auto ready = [this]() { return !pads_ || (pads_->hasDevice(0) && pads_->hasDevice(1)); };
    auto start = [this, ready]() {
        if (!ready()) return false;
        startCampaign(playersMission_, difficultyChoice_, 2);
        return true;
    };

    m.onItem = [this, start](Menu&, MenuItem& it, int ev) {
        if (ev != kActivate) return;
        if (it.id == kBack) menus_.pop();
        else if (it.id == kStart) start();
    };
    m.onKey = [this, rows, start](Menu& menu, int code) {
        // Left / Right are each pad's own (onUpdate); the keyboard has nothing to move.
        if (code == keys::Left || code == keys::Right || code == keys::StickLeft || code == keys::StickRight) return true;
        if (code == keys::Enter && menu.focused < 0) {
            if (start()) menus_.playSound("sounds\\menu1.wav");
            return true;
        }
        if (code == keys::Mouse1 && pads_) {
            std::vector<std::pair<int, RectF>> r;
            rows(r);
            for (const auto& [index, rect] : r)
                if (rect.contains(menus_.pointerX(), menus_.pointerY())) {
                    pads_->setPlayer(index, pads_->pads()[static_cast<size_t>(index)].player == 0 ? 1 : 0);
                    menus_.playSound("sounds\\menu1.wav");
                    return true;
                }
        }
        return false;
    };
    m.onUpdate = [this, ready](Menu& menu, float, float) {
        if (pads_)
            for (int i = 0; i < pads_->count(); ++i) {
                const int move = pads_->takeMove(i);
                const int to = move < 0 ? 0 : 1;
                if (move == 0 || pads_->pads()[static_cast<size_t>(i)].player == to) continue;
                pads_->setPlayer(i, to);
                menus_.playSound("sounds\\menu2.wav");
            }
        if (MenuItem* s = menu.find(kStart)) s->setEnabled(ready());
    };
    m.drawBack = [this, keyboard, rows](MenuDrawContext& c) {
        widgets::letterbox(c);
        widgets::header(c, "menu\\starth", 210, 63, 380, 64);
        widgets::text(c, 400, 140, texts_.get("players.title"), orange(), Align::Center);
        const Settings& st = profile_.settings;
        auto key = [&](int p, Action a) {
            const int k = st.keys[p][static_cast<int>(a)][0];
            return k > 0 ? keys::name(k) : texts_.get("ctl.unbound");
        };
        for (int p = 0; p < 2; ++p) {
            const float x0 = kPanelX[p], cx = x0 + kPanelW / 2, tx = x0 + 16;
            widgets::panel(c, x0, kPanelY, kPanelW, kPanelH);
            widgets::text(c, cx, kPanelY + 10, texts_.get(p ? "ctl.player.2" : "ctl.player.1"), orange(), Align::Center);
            float y = kPanelY + 40;
            if (touch_ && p == 0) {
                widgets::text(c, tx, y, texts_.get("players.touch"), rust());
                y += kRowH + 6;
            }
            if (keyboard) {
                widgets::text(c, tx, y, texts_.get("players.keyboard"), rust());
                const int fwd = st.keys[p][static_cast<int>(Action::MoveForward)][0];
                const int bwd = st.keys[p][static_cast<int>(Action::MoveBackward)][0];
                const int lft = st.keys[p][static_cast<int>(Action::MoveLeft)][0];
                const int rgt = st.keys[p][static_cast<int>(Action::MoveRight)][0];
                const std::string move = fwd == keys::Up && bwd == keys::Down && lft == keys::Left && rgt == keys::Right
                                             ? texts_.get("players.arrows")
                                             : key(p, Action::MoveForward) + " " + key(p, Action::MoveLeft) + " " +
                                                   key(p, Action::MoveBackward) + " " + key(p, Action::MoveRight);
                const Color dim = grey(0.8f);
                widgets::text(c, tx + 10, y + kRowH, fit(move + "  " + texts_.get("players.fly"), kPanelW - 42), dim);
                widgets::text(c, tx + 10, y + 2 * kRowH,
                              fit(key(p, Action::PrimaryAttack) + " " + texts_.get("players.fire") + ",  " +
                                      key(p, Action::MissileAttack) + " " + texts_.get("players.missile"),
                                  kPanelW - 42),
                              dim);
                widgets::text(c, tx + 10, y + 3 * kRowH,
                              fit(key(p, Action::UseItem) + " " + texts_.get("players.item"), kPanelW - 42), dim);
            }
            if (pads_ && !pads_->hasDevice(p)) {
                char who[64];
                std::snprintf(who, sizeof who, "%s %s", texts_.get(p ? "ctl.player.2" : "ctl.player.1").c_str(),
                              texts_.get("players.needs").c_str());
                widgets::text(c, cx, kPanelY + kPanelH - 30, who, pulse(2, 0, c.mt).r > 0.5f ? orange() : rust(), Align::Center);
            }
        }
        if (!pads_) return;
        std::vector<std::pair<int, RectF>> r;
        rows(r);
        const bool hover = !c.touchMode;
        for (const auto& [index, rect] : r) {
            const PlayerPads::Pad& pad = pads_->pads()[static_cast<size_t>(index)];
            const bool lit = pad.held != 0;
            const bool over = hover && rect.contains(menus_.pointerX(), menus_.pointerY());
            if (lit) c.r.rect(rect.x, rect.y, rect.w, rect.h, packed(0x8000A0FFu), Blend::Alpha);
            else if (over) c.r.rect(rect.x, rect.y, rect.w, rect.h, packed(0x80000060u), Blend::Alpha);
            // The arrow shows where a push takes it.
            if (pad.player == 1) widgets::text(c, rect.x + 4, rect.y + 2, "<", orange());
            else widgets::text(c, rect.x + rect.w - 4, rect.y + 2, ">", orange(), Align::Right);
            char label[32];
            std::snprintf(label, sizeof label, "%s %d: ", texts_.get("players.gamepad").c_str(), pad.number);
            widgets::text(c, rect.x + 18, rect.y + 2, fit(label + pad.name, rect.w - 40), lit ? grey(1) : orange());
        }
        const std::string second = texts_.get(touch_ ? "players.hint.tap" : "players.hint.click");
        if (pads_->count() > 0) {
            widgets::text(c, 400, 424, texts_.get("players.hint.move"), rust(), Align::Center);
            widgets::text(c, 400, 442, fit(second, 420), rust(), Align::Center);
        } else {
            widgets::text(c, 400, 424, texts_.get("players.hint.none"), rust(), Align::Center);
        }
    };
    return m;
}

} // namespace as3d::ui
