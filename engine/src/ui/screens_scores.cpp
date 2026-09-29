// Top Scores (S4) and name entry (S5), frontend.md 3.11 and 3.12.
#include <cstdio>

#include "as3d/frontend.h"
#include "plain_layout.h"

namespace as3d::ui {

namespace {

constexpr int kEditId = 2, kOkId = 1, kDelId = 60, kKeyBase = 200;
// Touch keyboard (ours, docs/spec/issues/090): four rows of ten keys under the name panel.
constexpr const char* kKeyRows[4] = {"ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ0123", "456789.-_ "};
constexpr float kKeyW = 38, kKeyH = 24, kKeyGap = 2, kKeyX = 201, kKeyY = 390;
constexpr RectF kDelButton{532, 282, 52, 22};

} // namespace

Menu Frontend::buildTopScores() {
    Menu m;
    if (plain()) m.addTextButton(1, kPlainLeft, texts_.get("plain.back")).textScale = kPlainButtonScale;
    else m.addButton(1, 50, 450, 128, 64, "menu\\back_1.tga", "menu\\back_2.tga");
    m.onItem = [this](Menu&, MenuItem& it, int ev) {
        if (ev == kActivate && it.id == 1) menus_.pop();
    };
    m.drawBack = [this](MenuDrawContext& c) {
        widgets::letterbox(c);
        if (c.plain) widgets::plainTitle(c, texts_.get("plain.title.scores"));
        else widgets::header(c, "menu\\topscores", 225, 63, 350, 64);
        widgets::panel(c, 130, 140, 540, 298);
        c.r.rect(130, 140, 540, 20, packed(0x80000060u), Blend::Alpha);
        widgets::text(c, 138, 142, texts_.get("scores.number"), orange());
        widgets::text(c, 168, 142, texts_.get("scores.name"), orange());
        widgets::text(c, 393, 142, texts_.get("scores.score"), orange());
        widgets::text(c, 533, 142, texts_.get("scores.rank"), orange());
        for (int i = 0; i < kHighScoreCount; i++) {
            const HighScore& h = profile_.progress.scores[i];
            const float y = 164 + 18.0f * static_cast<float>(i);
            widgets::text(c, 138, y, std::to_string(i + 1), orange());
            widgets::text(c, 168, y, h.name, rust());
            drawNumber(c.r, c.a.uiFont(), 393, y, std::to_string(h.score), 1.0f, rust());
            widgets::text(c, 533, y, texts_.get("rank." + std::to_string(h.rank)), rust());
        }
    };
    return m;
}

Menu Frontend::buildNameEntry() {
    Menu m;
    m.swallowBack = true; // there is no way to skip; an empty name is accepted
    m.addEdit(kEditId, 275, 285, 250);
    if (plain()) m.addTextButton(kOkId, {350, 322, 100, 36}, texts_.get("plain.ok")).textScale = kPlainButtonScale;
    else m.addButton(kOkId, 350, 320, 100, 64, "menu\\apply_ok_1.tga", "menu\\apply_ok_2.tga", {0.6094f, 0, 1, 1});
    if (touch_) {
        m.addTextButton(kDelId, kDelButton, texts_.get("touch.del"), itemflag::NoHoverSound);
        for (int row = 0; row < 4; row++)
            for (int col = 0; col < 10; col++) {
                const char ch = kKeyRows[row][col];
                const RectF hit{kKeyX + (kKeyW + kKeyGap) * static_cast<float>(col),
                                kKeyY + (kKeyH + kKeyGap) * static_cast<float>(row), kKeyW, kKeyH};
                m.addTextButton(kKeyBase + ch, hit, ch == ' ' ? texts_.get("touch.space") : std::string(1, ch),
                                itemflag::NoHoverSound);
            }
    }
    auto commit = [this](Menu& menu) {
        const MenuItem* e = menu.find(kEditId);
        const std::string name = e ? e->text : std::string();
        menus_.pop();
        const int rank = rankIndex(campaign_.highScoreRankValue(), report_.cheatUsed);
        profile_.progress.insert(name, campaign_.p[0].banked, rank);
        save();
        open(Screen::TopScores);
    };
    m.onItem = [this, commit](Menu& menu, MenuItem& it, int ev) {
        if (ev != kActivate) return;
        if (it.id == kOkId || it.id == kEditId) {
            commit(menu);
            return;
        }
        MenuItem* e = menu.find(kEditId);
        if (!e) return;
        if (it.id == kDelId) {
            if (e->cursor > 0 && !e->text.empty()) {
                e->text.erase(static_cast<size_t>(e->cursor - 1), 1);
                e->cursor--;
            }
        } else if (it.id >= kKeyBase) {
            const char ch = static_cast<char>(it.id - kKeyBase);
            if (measureText(FontMetrics::original(), e->text) < e->w - 20 && e->text.size() < 63) {
                e->text.insert(e->text.begin() + e->cursor, ch);
                e->cursor++;
            }
        }
        // Give the focus back to the field so its cursor keeps blinking.
        for (size_t i = 0; i < menu.items.size(); i++)
            if (menu.items[i].id == kEditId) menus_.focus(menu, static_cast<int>(i));
    };
    m.drawBack = [this](MenuDrawContext& c) {
        widgets::letterbox(c);
        if (c.plain) widgets::plainTitle(c, texts_.get("plain.title.name"));
        widgets::panel(c, 210, 220, 380, 160);
        widgets::text(c, 400, 240, texts_.get("label.enter_name"), orange(), Align::Center);
    };
    return m;
}

} // namespace as3d::ui
