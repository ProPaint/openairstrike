// The sequels' main menu (S1), exit confirmation (S2), Top Scores (S4), name entry (S5),
// Information (S8) and Credits (S8b). docs/spec/as2/frontend.md 3.3, 3.5, 3.11, 3.12, 3.14, 3.20.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "as2_screens.h"

namespace as3d::ui {

namespace {

using namespace as2;

constexpr u32 kCentre = itemflag::AlignCenter;

// Touch keyboard of the name entry (ours, docs/spec/issues/090), in the screen's style.
constexpr const char* kKeyRows[4] = {"ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ0123", "456789.-_ "};
constexpr float kKeyW = 38, kKeyH = 24, kKeyGap = 2, kKeyX = 201, kKeyY = 400;
constexpr RectF kDelButton{532, 284, 52, 18};
constexpr int kEditId = 2, kOkId = 1, kDelId = 60, kKeyBase = 200;

void drawKey(MenuDrawContext& c, const MenuItem& it, bool focused) {
    const RectF& b = it.hit;
    c.r.rect(b.x, b.y, b.w, b.h, darkGreenBox(), Blend::Alpha);
    c.r.outline(b.x, b.y, b.w, b.h, focused ? orange() : greenOutline(), Blend::Alpha);
    text(c.r, c.a, b.x + b.w * 0.5f, b.y + std::floor((b.h - 15) * 0.5f), it.label, focused ? orange() : green(),
         Align::Center);
}

} // namespace

// ---------------------------------------------------------------------------
// Main menu
// ---------------------------------------------------------------------------
Menu SequelScreens::mainMenu(Frontend& f) {
    Menu m;
    m.swallowBack = true; // Esc and right click are swallowed (as2@0x42b890)
    const struct { int id; const char* key; float y; } buttons[] = {
        {1, "button.start_game", 230}, {2, "button.top_scores", 275}, {3, "button.options", 320},
        {4, "button.information", 365}, {7, "button.credits", 410}, {5, "button.quit", 455},
    };
    for (const auto& b : buttons) m.addSequelButton(b.id, 400, b.y, tr(f, b.key), kCentre, 200);
    // Ours (docs/spec/issues/163): with more than one game, a button in the free band below.
    if (f.content_.changeGame) m.addSequelButton(kChangeGameItem, 400, 510, tr(f, "button.change_game"), kCentre, 200);
    m.onItem = [&f](Menu&, MenuItem& it, int ev) {
        if (ev != kActivate) return;
        switch (it.id) {
            case 1: f.open(Screen::StartGame); break;
            case 2: f.open(Screen::TopScores); break;
            case 3: f.optionsInGame_ = false; f.open(Screen::Options); break;
            case 4: f.open(Screen::Information); break;
            case 7: f.open(Screen::Credits); break;
            case 5: f.open(Screen::Exit); break;
            case kChangeGameItem:
                f.save();
                f.host_.changeGame();
                break;
            default: break;
        }
    };
    m.drawBack = [&f](MenuDrawContext& c) {
        // Settings.xml <Logotypes> (the base's rules), then the title logo.
        for (const LogoImage& l : f.content_.logos) {
            const Texture2D* t = c.a.texture(l.path);
            if (!t) continue;
            const float w = static_cast<float>(t->width()), h = static_cast<float>(t->height());
            c.r.pic(l.invertX ? 800 - l.x - w : l.x, l.invertY ? 600 - l.y - h : l.y, *t, Color{}, Blend::Alpha);
        }
        titleLogo(c.r, c.a, f.sq_->logoClock);
    };
    m.drawFront = [&f](MenuDrawContext& c) {
        if (!f.content_.version.empty()) text(c.r, c.a, 20, 580, f.content_.version, listGrey());
        if (!f.content_.copyright.empty()) text(c.r, c.a, 780, 580, f.content_.copyright, listGrey(), Align::Right);
    };
    return m;
}

// ---------------------------------------------------------------------------
// Exit confirmation
// ---------------------------------------------------------------------------
Menu SequelScreens::exit(Frontend& f) {
    Menu m;
    // YES and NO are plain text labels, the only items that play the hover sound.
    m.addText(1, 300, 330, tr(f, "button.yes"), kCentre);
    m.addText(2, 500, 330, tr(f, "button.no"), kCentre);
    if (f.touch_)
        for (MenuItem& it : m.items) it.hit = {it.x - 50, it.y - 12, 100, 40}; // ours: finger-sized
    m.onItem = [&f](Menu&, MenuItem& it, int ev) {
        if (ev != kActivate) return;
        if (it.id == 1) {
            f.save();
            f.host_.quit();
        } else {
            f.menus_.pop();
        }
    };
    m.drawFront = [&f](MenuDrawContext& c) {
        if (c.menu.open >= 1) text(c.r, c.a, 400, 250, tr(f, "label.exit"), green(), Align::Center);
        panel(c.r, c.a, 210, 230, 380, 160, c.menu.open, tr(f, "title.exit"));
        titleLogo(c.r, c.a, f.sq_->logoClock);
    };
    return m;
}

// ---------------------------------------------------------------------------
// Top Scores and name entry
// ---------------------------------------------------------------------------
Menu SequelScreens::topScores(Frontend& f) {
    Menu m;
    m.addSequelButton(1, 30, 520, tr(f, "button.back"));
    m.onItem = [&f](Menu&, MenuItem& it, int ev) {
        if (ev == kActivate && it.id == 1) f.menus_.pop();
    };
    m.drawFront = [&f](MenuDrawContext& c) {
        if (c.menu.open >= 1) {
            c.r.rect(130, 180, 540, 20, darkGreenBox(), Blend::Alpha);
            text(c.r, c.a, 138, 182, tr(f, "scores.number"), orange());
            text(c.r, c.a, 168, 182, tr(f, "scores.name"), orange());
            text(c.r, c.a, 393, 182, tr(f, "scores.score"), orange());
            text(c.r, c.a, 533, 182, tr(f, "scores.rank"), orange());
            for (int i = 0; i < kHighScoreCount; i++) {
                const HighScore& h = f.profile_.progress.scores[i];
                const float y = 204 + 18.0f * static_cast<float>(i);
                text(c.r, c.a, 138, y, std::to_string(i + 1), orange());
                text(c.r, c.a, 168, y, h.name, green());
                drawNumber(c.r, c.a.uiFont(), 393, y, std::to_string(h.score), 1.0f, green());
                text(c.r, c.a, 533, y, tr(f, "rank." + std::to_string(h.rank)), green());
            }
        }
        panel(c.r, c.a, 120, 170, 560, 318, c.menu.open, tr(f, "title.top_scores"));
        titleLogo(c.r, c.a, f.sq_->logoClock);
    };
    return m;
}

Menu SequelScreens::nameEntry(Frontend& f) {
    Menu m;
    m.swallowBack = true; // no way to skip; an empty name is accepted
    m.addEdit(kEditId, 275, 285, 250).flags |= itemflag::NoHoverSound;
    m.addSequelButton(kOkId, 400, 520, tr(f, "button.ok"), kCentre);
    if (f.touch_) {
        m.addCustom(kDelId, kDelButton, drawKey).label = tr(f, "touch.del");
        for (int row = 0; row < 4; row++)
            for (int col = 0; col < 10; col++) {
                const char ch = kKeyRows[row][col];
                const RectF hit{kKeyX + (kKeyW + kKeyGap) * static_cast<float>(col),
                                kKeyY + (kKeyH + kKeyGap) * static_cast<float>(row), kKeyW, kKeyH};
                MenuItem& k = m.addCustom(kKeyBase + ch, hit, drawKey);
                k.label = ch == ' ' ? tr(f, "touch.space") : std::string(1, ch);
                k.flags |= itemflag::NoHoverSound;
            }
    }
    auto commit = [&f](Menu& menu) {
        const MenuItem* e = menu.find(kEditId);
        const std::string name = e ? e->text : std::string();
        f.menus_.pop();
        const int rank = rankIndex(f.campaign_.highScoreRankValue(), f.report_.cheatUsed);
        f.profile_.progress.insert(name, f.campaign_.p[0].banked, rank);
        f.save();
        f.open(Screen::TopScores);
    };
    m.onItem = [&f, commit](Menu& menu, MenuItem& it, int ev) {
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
        for (size_t i = 0; i < menu.items.size(); i++)
            if (menu.items[i].id == kEditId) f.menus_.focus(menu, static_cast<int>(i));
    };
    m.drawFront = [&f](MenuDrawContext& c) { panel(c.r, c.a, 210, 220, 380, 160, c.menu.open, tr(f, "title.enter_name")); };
    return m;
}

// ---------------------------------------------------------------------------
// Information (8 pages) and Credits
// ---------------------------------------------------------------------------
namespace {

constexpr int kInfoPages = 8;

struct InfoIcon {
    int kind; // 0 weapon, 1 missile, 2 power-up
    int slot;
    float y;
};

// The icon of each paragraph, placed by the page builders (as2/frontend.md 3.14).
std::vector<InfoIcon> infoIcons(int page) {
    switch (page) {
        case 2: return {{0, 0, 194}, {0, 1, 286}, {0, 2, 358}, {0, 3, 430}};
        case 3: return {{0, 4, 194}, {0, 5, 295}, {0, 6, 365}};
        case 4: return {{0, 7, 194}, {0, 8, 260}};
        case 5: return {{1, 0, 194}, {1, 1, 260}, {1, 2, 328}, {1, 3, 418}};
        case 6: return {{1, 4, 194}};
        case 7: return {{2, 4, 194}, {2, 1, 256}, {2, 2, 310}, {2, 0, 378}};
        case 8: return {{2, 5, 194}, {2, 8, 256}};
        default: return {};
    }
}

} // namespace

Menu SequelScreens::information(Frontend& f) {
    Menu m;
    f.infoPage_ = std::clamp(f.infoPage_, 0, kInfoPages - 1);
    std::vector<std::string> pages;
    for (int n = 1; n <= kInfoPages; n++) {
        const std::string key = "info.pages." + std::to_string(n);
        pages.push_back(f.texts_.loaded(key) ? f.texts_.get(key) : std::to_string(n) + " of " + std::to_string(kInfoPages));
    }
    m.addSpinner(10, 400, 520, tr(f, "info.page"), pages, f.infoPage_).flags |= itemflag::NoHoverSound;
    m.addSequelButton(1, 50, 520, tr(f, "button.back_wide"));
    m.onItem = [&f](Menu&, MenuItem& it, int ev) {
        if (ev != kActivate) return;
        if (it.id == 1) f.menus_.pop();
        else if (it.id == 10) f.infoPage_ = it.index;
    };
    m.onKey = [&f](Menu& menu, int code) {
        int dir = 0;
        if (code == keys::PageUp || code == keys::Left) dir = -1;
        else if (code == keys::PageDown || code == keys::Right) dir = 1;
        if (dir == 0) return false;
        f.infoPage_ = (f.infoPage_ + dir + kInfoPages) % kInfoPages;
        if (MenuItem* s = menu.find(10)) s->index = f.infoPage_;
        return true;
    };
    m.drawBack = [&f](MenuDrawContext& c) {
        if (c.menu.open < 1) return;
        const Color white{1, 1, 1, 1};
        if (!f.texts_.installed()) {
            text(c.r, c.a, 60, 120, tr(f, "info.missing.title"), white);
            text(c.r, c.a, 60, 184, tr(f, "info.missing.0"), orange(), Align::Left, true);
            text(c.r, c.a, 60, 202, tr(f, "info.missing.1"), orange(), Align::Left, true);
            const std::string game = f.content_.game ? std::string(f.content_.game->title) + " v" + f.content_.game->version : "";
            text(c.r, c.a, 60, 220, game + tr(f, "info.missing.2s"), orange(), Align::Left, true);
        } else {
            const int page = f.infoPage_ + 1;
            const std::vector<InfoIcon> icons = infoIcons(page);
            text(c.r, c.a, 60, 120, f.texts_.get("info." + std::to_string(page) + ".title"), white);
            const float x = icons.empty() ? 60.0f : 140.0f;
            for (int line = 0; line < 32; line++) {
                const std::string k = "info." + std::to_string(page) + "." + std::to_string(line);
                if (!f.texts_.loaded(k)) continue;
                text(c.r, c.a, x, 184 + 18.0f * static_cast<float>(line), f.texts_.get(k), orange(), Align::Left, !icons.empty());
            }
            // One icon per paragraph, with the HUD's UVs and blend (as2/frontend.md 4.4).
            const HudLayout& L = hudLayout(f.content_.game ? f.content_.game->id : GameId::AirStrike2);
            for (const InfoIcon& ic : icons) {
                const std::vector<HudIcon>& table = ic.kind == 0 ? L.weapons : ic.kind == 1 ? L.missiles : L.powerups;
                const Texture2D& atlas = ic.kind == 0 ? c.a.weapons : ic.kind == 1 ? c.a.missiles : c.a.items;
                if (ic.slot >= static_cast<int>(table.size()) || !atlas.valid()) continue;
                const HudIcon& h = table[static_cast<size_t>(ic.slot)];
                if (h.uv.empty()) continue;
                c.r.quadSpec(60, ic.y, 66, 35, h.uv.s0, h.uv.t0, h.uv.s1, h.uv.t1, &atlas, Color{}, h.blend);
            }
        }
        // The key hints (not on touch, issue 240 item 17: the spinner shows its arrows there).
        if (!c.touchMode) {
            text(c.r, c.a, 570, 520, tr(f, "info.hint.prev"), listGrey());
            text(c.r, c.a, 570, 540, tr(f, "info.hint.next"), listGrey());
        }
    };
    return m;
}

Menu SequelScreens::credits(Frontend& f) {
    Menu m;
    m.addSequelButton(1, 50, 520, tr(f, "button.back_wide"));
    m.onItem = [&f](Menu&, MenuItem& it, int ev) {
        if (ev == kActivate && it.id == 1) f.menus_.pop();
    };
    m.drawBack = [&f](MenuDrawContext& c) {
        titleLogo(c.r, c.a, f.sq_->logoClock);
        if (c.menu.open < 1) return;
        bool any = false;
        for (int line = 0; line <= 20; line++) {
            const std::string k = "credits." + std::to_string(line);
            if (!f.texts_.loaded(k)) continue;
            any = true;
            text(c.r, c.a, 400, 120 + 18.0f * static_cast<float>(line), f.texts_.get(k), orange(), Align::Center, true);
        }
        if (!any) {
            text(c.r, c.a, 400, 184, tr(f, "info.missing.0"), orange(), Align::Center, true);
            text(c.r, c.a, 400, 202, tr(f, "info.missing.1"), orange(), Align::Center, true);
        }
    };
    return m;
}

} // namespace as3d::ui
