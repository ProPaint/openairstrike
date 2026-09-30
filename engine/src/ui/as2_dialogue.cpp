// The portrait dialogues at the start and the end of a mission (docs/spec/as2/frontend.md 3.19):
// a panel that opens over 0.25 s, the speaker's portrait, the page typed at one character per
// 0.05 s, 3 s per complete page, a key or tap completes or skips a page, then the panel closes.
// A start dialogue holds the mission paused with the HUD shown and releases it; an end dialogue
// hands over to Mission Complete.
#include <algorithm>

#include "as2_screens.h"

namespace as3d::ui {

namespace {

using namespace as2;

constexpr float kCharSeconds = 0.05f;
constexpr float kPageSeconds = 3.0f;

// Lines of a page (split at LF; issue 240 item 10: without the LF byte).
std::vector<std::string> lines(const std::string& page, size_t chars) {
    std::vector<std::string> out(1);
    for (size_t i = 0; i < page.size() && i < chars; i++) {
        if (page[i] == '\n') out.emplace_back();
        else out.back() += page[i];
    }
    return out;
}

} // namespace

Menu SequelScreens::dialogueScreen(Frontend& f) {
    Menu m = newMenu();
    m.swallowBack = true;
    Frontend::SequelState& s = *f.sq_;
    auto finish = [&f]() {
        Frontend::SequelState& st = *f.sq_;
        const bool end = st.dialogueEnd;
        f.menus_.pop();
        if (end) missionCompleted(f); // the game stays paused
        else f.resumePlay();          // unpaused, p_action cleared
    };
    m.onUpdate = [&f, finish](Menu&, float dt, float) {
        Frontend::SequelState& st = *f.sq_;
        if (st.closing) {
            st.fade = std::max(st.fade - 4.0f * dt, 0.0f);
            if (st.fade <= 0) finish();
            return;
        }
        if (st.fade < 1.0f) {
            st.fade = std::min(st.fade + 4.0f * dt, 1.0f);
            return;
        }
        if (st.page >= st.pages.size()) {
            st.closing = true;
            return;
        }
        const int len = static_cast<int>(st.pages[st.page].text.size());
        if (st.typed < len) {
            // One character per 0.05 s, at most one per frame.
            st.charClock += dt;
            if (st.charClock >= kCharSeconds) {
                st.typed++;
                st.charClock = std::min(st.charClock - kCharSeconds, kCharSeconds);
            }
            return;
        }
        st.pageTimer += dt;
        if (st.pageTimer >= kPageSeconds) {
            st.page++;
            st.typed = 0;
            st.charClock = 0;
            st.pageTimer = 0;
            if (st.page >= st.pages.size()) st.closing = true;
        }
    };
    // Enter, Esc, Space and both buttons complete a page still typing or go to the next; they
    // never close the dialogue at once and do nothing while it closes. Taps are the left button.
    m.onKey = [&f](Menu&, int code) {
        if (code != keys::Enter && code != keys::Escape && code != keys::Space && code != keys::Mouse1 &&
            code != keys::Mouse2)
            return false;
        Frontend::SequelState& st = *f.sq_;
        if (st.closing || st.fade < 1.0f || st.page >= st.pages.size()) return true;
        const int len = static_cast<int>(st.pages[st.page].text.size());
        if (st.typed < len) {
            st.typed = len;
            st.pageTimer = 0;
        } else {
            st.pageTimer = kPageSeconds + 1.0f; // the timer is set past the page's 3 s
        }
        return true;
    };
    m.drawFront = [&f](MenuDrawContext& c) {
        const Frontend::SequelState& st = *f.sq_;
        panel(c.r, c.a, 100, 400, 600, 160, st.fade, "");
        if (st.fade < 1.0f || st.closing || st.page >= st.pages.size()) return;
        const Frontend::SequelState::Page& p = st.pages[st.page];
        if (const Texture2D* t = c.a.texture("gfx\\ui\\portraits2.tga")) {
            const Piece who{p.speaker == 1 ? 80.0f : 0.0f, 8, 80, 120};
            const SpecUv uv = pieceUv(*t, who);
            c.r.quadSpec(120, 420, 80, 120, uv.s0, uv.t0, uv.s1, uv.t1, t, Color{}, Blend::Alpha);
        }
        float y = 430;
        for (const std::string& line : lines(p.text, static_cast<size_t>(std::max(st.typed, 0)))) {
            text(c.r, c.a, 220, y, line, Color{});
            y += 20;
        }
    };
    (void)s;
    return m;
}

} // namespace as3d::ui
