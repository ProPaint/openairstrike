// The game selector (docs/spec/issues/163), in the plain front end's style
// (docs/spec/as2/issues/260): black bars with two rules, a title in the top bar, one card per
// game between them, Exit and Play in the bottom bar. See as3d/frontend.h, GameSelector.
#include <algorithm>
#include <cmath>

#include "as3d/frontend.h"
#include "plain_layout.h"

namespace as3d::ui {

namespace {

constexpr int kExitId = 1, kPlayId = 2, kCardBase = 10;
constexpr float kCardTop = 116, kCardBottom = 482;
constexpr float kGap = 16;
constexpr float kMaxCardW = 340;
const Color kGrey = grey(0x80 / 255.0f);

float fitScale(std::string_view s, float width, float maxScale) {
    const float w = measureText(FontMetrics::original(), s);
    return w > 0 ? std::min(maxScale, width / w) : maxScale;
}

void scaledText(MenuDrawContext& c, float x, float y, std::string_view s, Color col, float scale, bool shadow) {
    TextStyle st;
    st.color = col;
    st.align = Align::Center;
    st.scale = scale;
    if (shadow) drawTextShadowed(c.r, c.a.uiFont(), x, y, s, st);
    else drawText(c.r, c.a.uiFont(), x, y, s, st);
}

} // namespace

GameSelector::GameSelector(std::vector<GameCard> cards, int preselected) : cards_(std::move(cards)) {
    menus_.plain = true;
    menus_.drawCursor = false; // the system pointer (desktop) or none (touch)
    current_ = cards_.empty() ? 0 : std::clamp(preselected, 0, static_cast<int>(cards_.size()) - 1);
    build();
}

void GameSelector::setTouchMode(bool on) { menus_.touchMode = on; }

void GameSelector::setSafeArea(float left, float top, float right, float bottom) {
    if (left == safeL_ && top == safeT_ && right == safeR_ && bottom == safeB_) return;
    safeL_ = left;
    safeT_ = top;
    safeR_ = right;
    safeB_ = bottom;
    build();
}

RectF GameSelector::cardRect(int index) const {
    return index >= 0 && index < static_cast<int>(rects_.size()) ? rects_[static_cast<size_t>(index)] : RectF{};
}

void GameSelector::build() {
    // Content between the safe edges, 20 virtual pixels in from the 800x600 screen at least.
    const float l = std::max(20.0f, safeL_ + 8), r = std::min(780.0f, safeR_ - 8);
    const int n = static_cast<int>(cards_.size());
    rects_.clear();
    if (n > 0) {
        const float avail = std::max(120.0f, r - l);
        const float w = std::min(kMaxCardW, (avail - kGap * static_cast<float>(n - 1)) / static_cast<float>(n));
        const float total = w * static_cast<float>(n) + kGap * static_cast<float>(n - 1);
        float x = std::floor(l + (avail - total) * 0.5f);
        for (int i = 0; i < n; ++i, x += w + kGap) rects_.push_back({x, kCardTop, std::floor(w), kCardBottom - kCardTop});
    }
    exitButton_ = {std::max(kPlainLeft.x, safeL_ + 8), kPlainLeft.y, kPlainLeft.w, kPlainLeft.h};
    play_ = {std::min(kPlainRight.x, safeR_ - 8 - kPlainRight.w), kPlainRight.y, kPlainRight.w, kPlainRight.h};

    Menu m;
    m.swallowBack = true; // Esc is Exit, below
    for (int i = 0; i < n; ++i) {
        m.addCustom(kCardBase + i, rects_[static_cast<size_t>(i)], [this, i](MenuDrawContext& c, MenuItem& self, bool focused) {
            const GameCard& card = cards_[static_cast<size_t>(i)];
            const RectF& b = self.hit;
            const bool cur = i == current_;
            c.r.rect(b.x, b.y, b.w, b.h, cur ? Color{0.376f, 0, 0, 0.55f} : packed(0x50000000u), Blend::Alpha);
            c.r.outline(b.x, b.y, b.w, b.h, cur ? orange() : rust(), Blend::Alpha);
            if (focused) {
                const Color p = pulse(2, 0, c.mt);
                c.r.outline(b.x + 2, b.y + 2, b.w - 4, b.h - 4, {p.r, p.g * 0.63f, 0, 1}, Blend::Add);
            }
            const float cx = b.x + b.w * 0.5f, inner = b.w - 24;
            // The picture area: the game's logo fitted in, else its title in large text.
            const float picTop = b.y + 16, picH = std::min(150.0f, std::max(60.0f, b.w * 0.5f));
            const bool logo = card.logo && card.logo->valid() && card.logo->width() > 0 && card.logo->height() > 0;
            if (logo) {
                const float tw = static_cast<float>(card.logo->width()), th = static_cast<float>(card.logo->height());
                const float s = std::min(inner / tw, picH / th);
                const float dw = std::floor(tw * s), dh = std::floor(th * s);
                c.r.quad(std::floor(cx - dw * 0.5f), std::floor(picTop + (picH - dh) * 0.5f), dw, dh, 0, 0, 1, 1, card.logo,
                         Color{}, Blend::Alpha);
            } else {
                const float s = fitScale(card.title, inner, 2.6f);
                // Over the picture area and the title row below it.
                scaledText(c, cx, std::floor(picTop + (picH + 18 + 15 - 15 * s) * 0.5f), card.title,
                           cur || focused ? orange() : rust(), s, true);
            }
            float y = picTop + picH + 18;
            // Under a logo the title in words (a logo need not name the game exactly); a card
            // without one shows its title above already, the line stays empty.
            const float ts = fitScale(card.title, inner, 1.4f);
            if (logo) scaledText(c, cx, y, card.title, cur || focused ? orange() : rust(), ts, true);
            y += 31; // the same rows on every card, whatever the title's scale
            const std::string version = card.version.empty() ? std::string() : "Version " + card.version;
            scaledText(c, cx, y, version, kGrey, fitScale(version, inner, 1.0f), false);
            y += 34;
            for (const std::string& line : card.saveLines) {
                scaledText(c, cx, y, line, Color{1, 1, 1, 1}, fitScale(line, inner, 1.0f), false);
                y += 22;
            }
            // What a click does, at the card's foot.
            const char* hint = c.touchMode ? "Tap to play" : "Click to play";
            scaledText(c, cx, b.y + b.h - 36, hint, cur || focused ? orange() : kGrey, 1.0f, false);
        });
    }
    m.addTextButton(kExitId, exitButton_, "Exit").textScale = kPlainButtonScale;
    m.addTextButton(kPlayId, play_, "Play").textScale = kPlainButtonScale;
    m.onItem = [this](Menu&, MenuItem& it, int ev) {
        if (it.id >= kCardBase && ev == kFocusGained) current_ = it.id - kCardBase;
        if (ev != kActivate) return;
        if (it.id >= kCardBase) chosen_ = it.id - kCardBase;
        else if (it.id == kPlayId && !cards_.empty()) chosen_ = current_;
        else if (it.id == kExitId) exit_ = true;
    };
    m.onKey = [this, n](Menu& menu, int code) {
        if (code == keys::Escape || code == keys::Mouse2) {
            exit_ = true;
            return true;
        }
        int dir = 0;
        if (code == keys::Left || code == keys::StickLeft) dir = -1;
        else if (code == keys::Right || code == keys::StickRight) dir = 1;
        if (dir == 0 || n == 0) return false;
        // Left and Right move between the cards (from a button: back to the current card).
        const bool onCard = menu.focused >= 0 && menu.items[static_cast<size_t>(menu.focused)].id >= kCardBase;
        const int next = onCard ? std::clamp(current_ + dir, 0, n - 1) : current_;
        menus_.focus(menu, next); // the cards are the first n items
        return true;
    };
    m.drawBack = [](MenuDrawContext& c) {
        widgets::letterbox(c);
        widgets::plainTitle(c, "Choose a game");
    };
    m.drawFront = [this](MenuDrawContext& c) {
        if (!c.touchMode)
            widgets::text(c, 400, 574, "Arrows choose, Enter plays, Esc exits", kGrey, Align::Center);
    };
    const int focus = menus_.top() && menus_.top()->focused >= 0 ? current_ : current_;
    menus_.replaceAll(std::move(m));
    if (Menu* top = menus_.top())
        if (focus >= 0 && focus < n) menus_.focus(*top, focus);
}

void GameSelector::update(float dt, const UiInput& input) {
    menus_.update(dt, input);
    menus_.takeSounds(); // no game audio behind the selector
}

void GameSelector::draw(Renderer2D& r, const UiAssets& fontAssets) {
    r.fullscreen({0, 0, 0, 1}, Blend::Opaque);
    // A dim red wash behind the cards, full width on wide screens.
    const Mapping& m = r.mapping();
    const float l = std::min(m.left(), 0.0f), w = std::max(m.right(), 800.0f) - l;
    r.rect(l, 100, w, 400, {0.10f, 0.02f, 0.02f, 1}, Blend::Opaque);
    menus_.draw(r, fontAssets);
}

} // namespace as3d::ui
