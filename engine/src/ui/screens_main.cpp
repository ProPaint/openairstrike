// Main menu (S1), exit confirmation (S2) and Information pages (S8). frontend.md 3.3, 3.5, 3.14.
#include <algorithm>
#include <cstdio>

#include "as3d/frontend.h"

namespace as3d::ui {

namespace {

const Color kGrey = grey(0x80 / 255.0f);

// Main-menu atlas rows (t ranges of menu\mmenu_1.tga).
constexpr SpecUv kStartGame{0, 0.8711f, 1, 1.0f};
constexpr SpecUv kTopScores{0, 0.7344f, 1, 0.8711f};
constexpr SpecUv kOptions{0, 0.5977f, 1, 0.7344f};
constexpr SpecUv kInformation{0, 0.4610f, 1, 0.5977f};
constexpr SpecUv kExit{0, 0.3242f, 1, 0.4610f};

// Main menu frame: bars and the full corner ornament (frontend.md 3.3 step 1). On wide screens
// the bars and rules run to the framebuffer edges (issue 091).
void mainMenuFrame(MenuDrawContext& c) {
    const Mapping& m = c.r.mapping();
    const float l = std::min(m.left(), 0.0f), rr = std::max(m.right(), 800.0f);
    c.r.rect(l, std::min(m.top(), 0.0f), rr - l, 100 - std::min(m.top(), 0.0f), {0, 0, 0, 1}, Blend::Opaque);
    c.r.rect(l, 500, rr - l, std::max(m.bottom(), 600.0f) - 500, {0, 0, 0, 1}, Blend::Opaque);
    const Texture2D* t = c.a.texture("menu\\corner.tga");
    if (!t) return;
    c.r.quadSpec(0, 487, 128, 16, 0, 0, 0.99f, 0.97f, t, Color{}, Blend::Alpha);
    c.r.quadSpec(128, 487, rr - 128, 16, 0.97f, 0, 0.99f, 0.97f, t, Color{}, Blend::Alpha);
    c.r.quadSpec(672, 97, 128, 16, 0.99f, 0.97f, 0, 0, t, Color{}, Blend::Alpha);
    c.r.quadSpec(l, 97, 672 - l, 16, 0.99f, 0.97f, 0.97f, 0, t, Color{}, Blend::Alpha);
    if (rr > 800) // the rotated corner ends at 800; continue its rule to the right edge
        c.r.quadSpec(800, 97, rr - 800, 16, 0.99f, 0.97f, 0.97f, 0, t, Color{}, Blend::Alpha);
    if (l < 0)
        c.r.quadSpec(l, 487, -l, 16, 0.97f, 0, 0.99f, 0.97f, t, Color{}, Blend::Alpha);
}

} // namespace

Menu Frontend::buildMainMenu() {
    Menu m;
    m.swallowBack = true; // the main menu cannot be closed
    const struct { int id; float y, h; SpecUv uv; } buttons[] = {
        {1, 250, 33, kStartGame}, {2, 283, 35, kTopScores}, {3, 318, 35, kOptions},
        {4, 353, 35, kInformation}, {5, 388, 35, kExit},
    };
    for (const auto& b : buttons)
        m.addButton(b.id, 240, b.y, 320, b.h, "menu\\mmenu_1.tga", "menu\\mmenu_2.tga", b.uv);
    m.onItem = [this](Menu&, MenuItem& it, int ev) {
        if (ev != kActivate) return;
        switch (it.id) {
            case 1: open(Screen::StartGame); break;
            case 2: open(Screen::TopScores); break;
            case 3: optionsInGame_ = false; open(Screen::Options); break;
            case 4: open(Screen::Information); break;
            case 5: open(Screen::Exit); break;
            default: break;
        }
    };
    m.drawBack = [this](MenuDrawContext& c) {
        mainMenuFrame(c);
        if (!content_.version.empty()) widgets::text(c, 20, 580, content_.version, kGrey);
        if (!content_.copyright.empty()) widgets::text(c, 780, 580, content_.copyright, kGrey, Align::Right);
        for (const LogoImage& l : content_.logos) {
            const Texture2D* t = c.a.texture(l.path);
            if (!t) continue;
            const float w = static_cast<float>(t->width()), h = static_cast<float>(t->height());
            const float x = l.invertX ? 800 - l.x - w : l.x;
            const float y = l.invertY ? 600 - l.y - h : l.y;
            c.r.pic(x, y, *t, Color{}, Blend::Alpha);
        }
    };
    return m;
}

Menu Frontend::buildExit() {
    Menu m;
    m.addButton(1, 250, 320, 95, 64, "menu\\yesno_1.tga", "menu\\yesno_2.tga", {0, 0, 0.5278f, 1});
    m.addButton(2, 465, 320, 85, 64, "menu\\yesno_1.tga", "menu\\yesno_2.tga", {0.5278f, 0, 1, 1});
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
        widgets::panel(c, 210, 220, 380, 160);
        widgets::text(c, 400, 240, texts_.get("label.exit"), orange(), Align::Center);
    };
    return m;
}

// ---------------------------------------------------------------------------
// Information
// ---------------------------------------------------------------------------
Menu Frontend::buildInformation() {
    Menu m;
    std::vector<std::string> pages;
    char key[32];
    for (int n = 1; n <= 10; n++) {
        std::snprintf(key, sizeof key, "info.pages.%d", n);
        pages.push_back(texts_.get(key));
    }
    m.addSpinner(10, 400, 440, texts_.get("info.page"), pages, infoPage_);
    m.addButton(1, 50, 450, 128, 64, "menu\\back_1.tga", "menu\\back_2.tga");
    m.onItem = [this](Menu& menu, MenuItem& it, int ev) {
        if (ev != kActivate) return;
        if (it.id == 1) menus_.pop();
        else if (it.id == 10) infoPage_ = it.index;
        (void)menu;
    };
    m.onKey = [this](Menu& menu, int code) {
        int dir = 0;
        if (code == keys::PageUp || code == keys::Left) dir = -1;
        else if (code == keys::PageDown || code == keys::Right) dir = 1;
        if (dir == 0) return false;
        infoPage_ = (infoPage_ + dir + 10) % 10;
        if (MenuItem* s = menu.find(10)) s->index = infoPage_;
        return true;
    };
    m.drawBack = [this](MenuDrawContext& c) {
        widgets::letterbox(c);
        const Color white{1, 1, 1, 1};
        if (!texts_.installed()) {
            widgets::text(c, 60, 80, texts_.get("info.missing.title"), white);
            for (int i = 0; i < 3; i++) {
                char k[32];
                std::snprintf(k, sizeof k, "info.missing.%d", i);
                widgets::text(c, 60, 144 + 18.0f * static_cast<float>(i), texts_.get(k), orange(), Align::Left, true);
            }
        } else {
            const int page = infoPage_ + 1;
            char k[32];
            std::snprintf(k, sizeof k, "info.%d.title", page);
            const std::string title = texts_.get(k);
            const bool credits = page == 10;
            // Pages 4 to 9 have one icon per paragraph (frontend.md 3.14; which icon: issue 092).
            struct Icon { const Texture2D* atlas; SpecUv uv; };
            std::vector<Icon> icons;
            if (page == 4) for (int w : {0, 1, 2, 3}) icons.push_back({&c.a.weapons, weaponIconUv(w)});
            if (page == 5) for (int w : {5, 6, 4, 8}) icons.push_back({&c.a.weapons, weaponIconUv(w)});
            if (page == 6) for (int w : {7, 9}) icons.push_back({&c.a.weapons, weaponIconUv(w)});
            if (page == 7) for (int t : {0, 1, 2, 3}) icons.push_back({&c.a.missiles, missileIconUv(t)});
            if (page == 8) icons.push_back({&c.a.missiles, missileIconUv(4)});
            if (page == 9) for (int t : {3, 1, 2, 0}) icons.push_back({&c.a.items, powerupIconUv(t)});
            if (credits) widgets::text(c, 400, 144, title, white, Align::Center);
            else widgets::text(c, 60, 80, title, white);
            const float x = icons.empty() ? 60.0f : 140.0f;
            for (int line = 0; line < 32; line++) {
                std::snprintf(k, sizeof k, "info.%d.%d", page, line);
                if (!texts_.loaded(k)) continue;
                const float y = 144 + 18.0f * static_cast<float>(line);
                if (credits) widgets::text(c, 400, y, texts_.get(k), orange(), Align::Center, true);
                else widgets::text(c, x, y, texts_.get(k), orange(), Align::Left, true);
            }
            for (size_t i = 0; i < icons.size(); i++)
                if (icons[i].atlas->valid())
                    c.r.quadSpec(60, 154 + 72.0f * static_cast<float>(i), 66, 35, icons[i].uv.s0, icons[i].uv.t0,
                                 icons[i].uv.s1, icons[i].uv.t1, icons[i].atlas, Color{}, Blend::Add);
        }
        if (!touch_) { // key hints; on touch the page spinner shows its arrows instead
            widgets::text(c, 570, 460, texts_.get("info.hint.prev"), kGrey);
            widgets::text(c, 570, 480, texts_.get("info.hint.next"), kGrey);
        }
    };
    return m;
}

} // namespace as3d::ui
