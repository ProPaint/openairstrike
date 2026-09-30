// The game selector (docs/spec/issues/163, 164), in the plain front end's style
// (docs/spec/as2/issues/260): black bars with two rules, a title in the top bar, one card per
// game between them, Exit and Play in the bottom bar. A card: its game's own animated title as
// the marquee (launcher_marquee.h), the title, the version, the save summary, what a click
// does. See as3d/frontend.h, GameSelector.
#include <algorithm>
#include <cmath>

#include "as3d/frontend.h"
#include "launcher_marquee.h"
#include "plain_layout.h"

namespace as3d::ui {

namespace {

constexpr int kExitId = 1, kPlayId = 2, kCardBase = 10;
constexpr float kBandTop = 102, kBandBottom = 498; // between the bars' rules
constexpr float kGap = 18;
constexpr float kMaxCardW = 380;
constexpr float kMaxSpan = 1100; // the cards' row on the widest screens
constexpr float kEdge = 28;      // from the screen's (or field's) edge
constexpr float kPad = 14;       // inside a card
const Color kGrey = grey(0x80 / 255.0f);
const Color kSoft = packed(0xFF1F5FE0u); // the card's rust, brighter than the menus' on the dark fill

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

// The marquee box's height for a card's inner width: 0.46 of it.
float marqueeHeight(float inner) { return std::clamp(std::floor(inner * 0.46f), 88.0f, 170.0f); }

// Rows of a card, top to bottom: the marquee, the title, the version, two lines of save
// summary, a rule and what a click does.
constexpr float kTitleRow = 30, kVersionRow = 26, kSaveRow = 50, kRuleGap = 12, kHintRow = 16;

float cardHeight(float marquee) {
    return kPad + 4 + marquee + 4 + 12 + kTitleRow + kVersionRow + kSaveRow + kRuleGap + 1 + 12 + kHintRow + kPad + 4;
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

void GameSelector::setView(float left, float right, bool fourByThree) {
    if (left == viewL_ && right == viewR_ && fourByThree == fourByThree_) return;
    viewL_ = left;
    viewR_ = right;
    fourByThree_ = fourByThree;
    build();
}

void GameSelector::setBannerDrawer(BannerDrawer fn) { banner_ = std::move(fn); }

RectF GameSelector::cardRect(int index) const {
    return index >= 0 && index < static_cast<int>(rects_.size()) ? rects_[static_cast<size_t>(index)] : RectF{};
}

RectF GameSelector::marqueeRect(int index) const {
    return index >= 0 && index < static_cast<int>(marquees_.size()) ? marquees_[static_cast<size_t>(index)] : RectF{};
}

void GameSelector::build() {
    // The row of cards: inside the screen (the 800x600 field in the 4:3 mode), at most 1100
    // wide, clear of the cutouts.
    float l = fourByThree_ ? kEdge : std::max(viewL_ + kEdge, 400 - kMaxSpan * 0.5f);
    float r = fourByThree_ ? 800 - kEdge : std::min(viewR_ - kEdge, 400 + kMaxSpan * 0.5f);
    l = std::max(l, safeL_ + 8);
    r = std::min(r, safeR_ - 8);
    if (r - l < 120) r = l + 120;
    const int n = static_cast<int>(cards_.size());
    rects_.clear();
    marquees_.clear();
    if (n > 0) {
        const float avail = r - l;
        const float w = std::floor(std::min(kMaxCardW, (avail - kGap * static_cast<float>(n - 1)) / static_cast<float>(n)));
        const float total = w * static_cast<float>(n) + kGap * static_cast<float>(n - 1);
        const float inner = w - 2 * kPad;
        const float mh = marqueeHeight(inner), h = std::floor(cardHeight(mh));
        const float top = std::floor(kBandTop + (kBandBottom - kBandTop - h) * 0.5f);
        float x = std::floor(l + (avail - total) * 0.5f);
        for (int i = 0; i < n; ++i, x += w + kGap) {
            rects_.push_back({x, top, w, h});
            marquees_.push_back({x + kPad, top + kPad + 4, inner, mh});
        }
    }
    // The buttons on the row's edges, in the bottom bar.
    exitButton_ = {std::floor(l), kPlainLeft.y, kPlainLeft.w, kPlainLeft.h};
    play_ = {std::floor(r - kPlainRight.w), kPlainRight.y, kPlainRight.w, kPlainRight.h};

    Menu m;
    m.swallowBack = true; // Esc is Exit, below
    for (int i = 0; i < n; ++i) {
        m.addCustom(kCardBase + i, rects_[static_cast<size_t>(i)], [this, i](MenuDrawContext& c, MenuItem& self, bool focused) {
            const GameCard& card = cards_[static_cast<size_t>(i)];
            const RectF& b = self.hit;
            const bool cur = i == current_;
            // 0..1 pulse of the focus, about once a second.
            const float pl = 0.5f + 0.5f * std::sin(c.mt * 6.0f);
            c.r.rect(b.x, b.y, b.w, b.h, cur ? Color{0.30f, 0.02f, 0.02f, 0.80f} : Color{0.03f, 0.01f, 0.01f, 0.72f},
                     Blend::Alpha);
            if (cur) {
                // The focus: a bright frame that breathes, and a soft glow spreading outwards.
                for (int k = 1; k <= 4; ++k) {
                    const float a = (0.30f + 0.30f * pl) / static_cast<float>(k * k);
                    c.r.outline(b.x - k, b.y - k, b.w + 2 * k, b.h + 2 * k, {1.0f * a, 0.50f * a, 0, 1}, Blend::Add);
                }
                const float a = 0.72f + 0.28f * pl;
                c.r.outline(b.x, b.y, b.w, b.h, {1.0f * a, 0.63f * a, 0, 1}, Blend::Alpha);
                c.r.outline(b.x + 1, b.y + 1, b.w - 2, b.h - 2, {0.75f * a, 0.35f * a, 0, 0.9f}, Blend::Alpha);
            } else {
                c.r.outline(b.x, b.y, b.w, b.h, packed(0xFF002A70u), Blend::Alpha);
            }
            const float cx = b.x + b.w * 0.5f, inner = b.w - 2 * kPad;
            // The marquee, on a dark stage of its own: the game's title as its title screen shows
            // it, else (no pictures, no banner drawer) the title in large text.
            const RectF mq = marquees_[static_cast<size_t>(i)];
            c.r.rect(mq.x - 4, mq.y - 4, mq.w + 8, mq.h + 8, {0, 0, 0, cur ? 0.78f : 0.6f}, Blend::Alpha);
            c.r.outline(mq.x - 4, mq.y - 4, mq.w + 8, mq.h + 8, cur ? packed(0x90003C90u) : packed(0x70002050u), Blend::Alpha);
            const bool bannerDrawn = card.marquee == Marquee::Banner && banner_;
            if (!bannerDrawn && !drawMarquee(c.r, card, mq, c.mt, loopFit_)) {
                const float s = fitScale(card.title, mq.w, 2.6f);
                scaledText(c, cx, std::floor(mq.y + (mq.h - 15 * s) * 0.5f), card.title, cur ? orange() : kSoft, s, true);
            }
            float y = mq.y + mq.h + 4 + 12;
            // The title in words under the picture (a logo need not name the game exactly).
            const float ts = fitScale(card.title, inner, 1.5f);
            scaledText(c, cx, std::floor(y + (22 - 15 * ts) * 0.5f), card.title, cur ? orange() : kSoft, ts, true);
            y += kTitleRow;
            const std::string version = card.version.empty() ? std::string() : "Version " + card.version;
            scaledText(c, cx, y, version, kGrey, fitScale(version, inner, 1.0f), false);
            y += kVersionRow;
            // One or two lines, centred in the two-line slot.
            const float lines = static_cast<float>(card.saveLines.size());
            float sy = y + (kSaveRow - 22 * std::max(lines, 1.0f) + 7) * 0.5f;
            for (const std::string& line : card.saveLines) {
                scaledText(c, cx, std::floor(sy), line, Color{1, 1, 1, 1}, fitScale(line, inner, 1.0f), false);
                sy += 22;
            }
            y += kSaveRow + kRuleGap;
            c.r.rect(b.x + kPad, y, inner, 1, cur ? packed(0x80003C90u) : packed(0x60002050u), Blend::Alpha);
            // What a click does, at the card's foot; it breathes with the frame on the current card.
            const char* hint = c.touchMode ? "Tap to play" : "Click to play";
            const float ha = 0.75f + 0.25f * pl;
            scaledText(c, cx, y + 13, hint, cur ? Color{1.0f * ha, 0.63f * ha, 0, 1} : kGrey, 1.0f, false);
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
    m.drawBack = [this](MenuDrawContext& c) {
        // The bars and their rules; in the 4:3 mode only over the 800x600 field.
        const Mapping& mp = c.r.mapping();
        const float l = fourByThree_ ? 0.0f : std::min(mp.left(), 0.0f);
        const float w = (fourByThree_ ? 800.0f : std::max(mp.right(), 800.0f)) - l;
        c.r.rect(l, std::min(mp.top(), 0.0f), w, 100 - std::min(mp.top(), 0.0f), {0, 0, 0, 1}, Blend::Opaque);
        c.r.rect(l, 500, w, std::max(mp.bottom(), 600.0f) - 500, {0, 0, 0, 1}, Blend::Opaque);
        const Color rule = packed(0xFF0030C0u);
        c.r.rect(l, 98, w, 2, rule, Blend::Opaque);
        c.r.rect(l, 500, w, 2, rule, Blend::Opaque);
        widgets::plainTitle(c, "Choose a game", 1.7f);
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
    // A dim red wash behind the cards, the whole width of the screen (the field in the 4:3 mode).
    const Mapping& m = r.mapping();
    const float l = fourByThree_ ? 0.0f : std::min(m.left(), 0.0f);
    const float w = (fourByThree_ ? 800.0f : std::max(m.right(), 800.0f)) - l;
    r.rect(l, 100, w, 400, {0.085f, 0.02f, 0.02f, 1}, Blend::Opaque);
    menus_.draw(r, fontAssets);
    // The first game's banner mesh, over its card's stage (the mesh renderer draws its own pass).
    if (banner_) {
        bool any = false;
        for (size_t i = 0; i < cards_.size(); ++i) any = any || cards_[i].marquee == Marquee::Banner;
        if (any) {
            r.flush();
            for (size_t i = 0; i < cards_.size(); ++i)
                if (cards_[i].marquee == Marquee::Banner) banner_(marquees_[i], menus_.menuTime());
            r.begin(m.fbWidth, m.fbHeight); // the list is empty again; the caller flushes nothing
        }
    }
}

} // namespace as3d::ui
