// Online co-op in the front end (docs/spec/issues/180-online-coop.md 8 and 9): the Frontend
// members of the online session (setOnlineHost, online, onlineApplyFlow, onlineStartCampaign,
// onlineEnded, onlineHintConfirmed) and the screens Online, OnlineHostCode, OnlineJoinCode,
// OnlineLobby and OnlineNotice in the three front-end styles: the first game's Start Game look
// of screens_players.cpp (letterbox, the START GAME header, a panel, text buttons in the bottom
// bar), the plain list's (letterbox, title in the top bar) and the sequels' (the panel with its
// title tab, their text buttons; AirStrike 2 and Gulf Thunder through as2::skin()). Every string
// goes through texts_.get (defaults in frontend_texts.cpp); every button is a menu item with an
// id, so the web walks tap them (AS3D_MENU).
//
// The screens read OnlineHost::status() every frame and call its actions; they never see the
// transport. Navigation follows the phase: the host's code screen and the guest's join screen give
// way to the lobby when the phase reaches Lobby; a Failed or Ended phase opens the notice over
// whatever screen is up (its OK -> OnlineHost::acknowledgeMessage and back to the Online screen,
// or the guest's join screen for a mistyped code, or the main menu after a session).
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "as2_screens.h"
#include "plain_layout.h"

namespace as3d::ui {

namespace {

enum OnlineId {
    kBack = 1, kHost = 2, kJoin = 3, kCopy = 4, kPaste = 5, kConnect = 6, kNext = 7,
    // kOnlineFieldItem (frontend.h) = 8, the paste field
    kOk = 9, kMission = 10, kDifficulty = 11, kHeli = 12, kStart = 13, kReady = 14,
};

constexpr int kMaxCodeLines = 9;      // the code box: 9 lines of the menu font (a 360-character code needs 8)
constexpr size_t kMaxFieldChars = 2048;

const FontMetrics& fm() { return FontMetrics::original(); }

const OnlineStatus& noStatus() {
    static const OnlineStatus s;
    return s;
}

// "%s" and "%d" of a text replaced in order by `args` (the texts are data: never a printf format).
std::string subst(const std::string& fmt, std::initializer_list<std::string> args) {
    std::string out;
    auto it = args.begin();
    for (size_t i = 0; i < fmt.size(); i++) {
        if (fmt[i] == '%' && i + 1 < fmt.size() && (fmt[i + 1] == 's' || fmt[i + 1] == 'd')) {
            if (it != args.end()) out += *it++;
            i++;
        } else {
            out += fmt[i];
        }
    }
    return out;
}

// `s` cut at word breaks to lines of at most `w` virtual pixels (a word wider than a line is cut).
std::vector<std::string> wrapWords(const std::string& s, float w) {
    std::vector<std::string> lines;
    std::string line;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t sp = s.find(' ', pos);
        if (sp == std::string::npos) sp = s.size();
        const std::string word = s.substr(pos, sp - pos);
        pos = sp + 1;
        const std::string tryLine = line.empty() ? word : line + " " + word;
        if (measureText(fm(), tryLine) <= w || line.empty()) {
            line = tryLine;
        } else {
            lines.push_back(line);
            line = word;
        }
        while (measureText(fm(), line) > w && line.size() > 1) { // one long word
            size_t n = line.size();
            while (n > 1 && measureText(fm(), line.substr(0, n)) > w) n--;
            lines.push_back(line.substr(0, n));
            line = line.substr(n);
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

// A code (no spaces) cut at any character to lines of at most `w` pixels, at most `maxLines`
// (the last one ends with "..." when the code is longer).
std::vector<std::string> wrapChars(const std::string& s, float w, int maxLines) {
    std::vector<std::string> lines;
    std::string line;
    float width = 0;
    for (char ch : s) {
        const float cw = measureText(fm(), std::string(1, ch));
        if (width + cw > w && !line.empty()) {
            lines.push_back(line);
            line.clear();
            width = 0;
        }
        line += ch;
        width += cw;
    }
    if (!line.empty()) lines.push_back(line);
    if (static_cast<int>(lines.size()) > maxLines) {
        lines.resize(static_cast<size_t>(maxLines));
        std::string& last = lines.back();
        while (last.size() > 3 && measureText(fm(), last + "...") > w) last.pop_back();
        last += "...";
    }
    return lines;
}

// A pasted code reduced to its characters: clipboards bring spaces, line breaks and quotes.
std::string cleanCode(const std::string& s) {
    std::string out;
    for (char c : s)
        if (c > 0x20 && c < 0x7F && c != '"' && c != '\'' && out.size() < kMaxFieldChars) out += c;
    return out;
}

// The field's text in `w` pixels: the start of the code and its length when it does not fit.
std::string fieldShown(const std::string& s, float w) {
    if (measureText(fm(), s) <= w) return s;
    const std::string tail = "... (" + std::to_string(s.size()) + ")";
    std::string head = s;
    while (!head.empty() && measureText(fm(), head + tail) > w) head.pop_back();
    return head + tail;
}

} // namespace

struct OnlineScreens {
    enum class Look { First, Plain, Sequel };
    enum Slot { Left, Right, Middle, Free };

    static Look look(const Frontend& f) { return f.sequel() ? Look::Sequel : f.plain() ? Look::Plain : Look::First; }
    static const OnlineStatus& status(const Frontend& f) { return f.online_ ? f.online_->status() : noStatus(); }
    static std::string tx(const Frontend& f, const char* key) { return f.texts_.get(key); }
    static Color hi(const Frontend& f) { return look(f) == Look::Sequel ? as2::accent() : orange(); }
    static Color lo(const Frontend& f) { return look(f) == Look::Sequel ? as2::ink() : rust(); }

    static void put(MenuDrawContext& c, const Frontend& f, float x, float y, std::string_view s, Color col,
                    Align al = Align::Left) {
        if (look(f) == Look::Sequel) as2::text(c.r, c.a, x, y, s, col, al);
        else widgets::text(c, x, y, s, col, al);
    }

    // The top of the content, and the panel behind it (x 100..700).
    static float y0(const Frontend& f) { return look(f) == Look::Sequel ? 182.0f : look(f) == Look::Plain ? 118.0f : 156.0f; }
    static float panelY(const Frontend& f) { return y0(f) - (look(f) == Look::Sequel ? 16.0f : 10.0f); }

    // The chrome under the content: the first game's header and panel, the plain list's title,
    // Gulf Thunder's title bar. `h` is the panel's height.
    static void chromeBack(MenuDrawContext& c, Frontend& f, float h) {
        const std::string title = tx(f, "online.title");
        switch (look(f)) {
            case Look::First:
                widgets::letterbox(c);
                widgets::header(c, "menu\\starth", 210, 63, 380, 64);
                widgets::text(c, 400, 134, title, orange(), Align::Center);
                widgets::panel(c, 100, panelY(f), 600, h);
                break;
            case Look::Plain:
                widgets::letterbox(c);
                widgets::plainTitle(c, title);
                widgets::panel(c, 100, panelY(f), 600, h);
                break;
            case Look::Sequel:
                if (as2::gulfLook()) as2::gulfTitleBar(c.r, c.a, f.sq_->logoClock, f.sq_->tint, true);
                break;
        }
    }
    // The sequels' panel and logo over the items (the panel's static fill lies over them as in
    // Start Game).
    static void chromeFront(MenuDrawContext& c, Frontend& f, float h) {
        if (look(f) != Look::Sequel) return;
        as2::panel(c.r, c.a, 100, panelY(f), 600, h, c.menu.open, tx(f, "online.title"));
        SequelScreens::header(c, f);
    }

    static Menu newMenuFor(const Frontend& f) { return look(f) == Look::Sequel ? newMenu() : Menu{}; }

    // A text button in the style of the game: in the bottom bar (Left, Right, Middle) or at `r`.
    static MenuItem& button(Menu& m, const Frontend& f, int id, Slot slot, RectF r, const std::string& label,
                            float textScale = 1.0f) {
        const Look lk = look(f);
        if (lk == Look::Sequel) {
            const float w = std::max(0.0f, (slot == Free ? r.w : 150.0f) - m.buttonMargin);
            switch (slot) {
                case Left: return m.addSequelButton(id, 40, 520, label, 0, w);
                case Right: return m.addSequelButton(id, 760, 520, label, itemflag::AlignRight, w);
                case Middle: return m.addSequelButton(id, 400, 520, label, itemflag::AlignCenter, w);
                case Free: return m.addSequelButton(id, r.x + r.w * 0.5f, r.y, label, itemflag::AlignCenter, w);
            }
        }
        if (slot == Left) r = lk == Look::Plain ? kPlainLeft : RectF{50, 456, 150, 40};
        else if (slot == Right) r = lk == Look::Plain ? kPlainRight : RectF{600, 456, 150, 40};
        else if (slot == Middle) r = lk == Look::Plain ? kPlainMiddle : RectF{325, 456, 150, 40};
        MenuItem& it = m.addTextButton(id, r, label);
        it.textScale = lk == Look::Plain && slot != Free ? kPlainButtonScale : textScale;
        return it;
    }

    // Esc and the right click do what Back does.
    static void backKeys(Menu& m, std::function<void()> back) {
        m.onKey = [back](Menu&, int code) {
            if (code != keys::Escape && code != keys::Mouse2) return false;
            back();
            return true;
        };
    }

    // A Failed or Ended phase: the notice over this screen (8.3).
    static bool watchEnd(Frontend& f) {
        const OnlinePhase p = status(f).phase;
        if (!f.online_ || (p != OnlinePhase::Failed && p != OnlinePhase::Ended)) return false;
        f.onlineMessage_ = status(f).message;
        f.open(Screen::OnlineNotice);
        return true;
    }
    // The lobby replaces the code screens when the session reaches it.
    static bool watchLobby(Frontend& f) {
        if (status(f).phase != OnlinePhase::Lobby) return false;
        f.menus_.pop();
        f.open(Screen::OnlineLobby);
        return true;
    }

    static void cancelAndPop(Frontend& f) {
        if (f.online_ && status(f).phase != OnlinePhase::Idle) f.online_->cancel();
        f.menus_.pop();
    }

    // A line under the code: the host's status message, or the phase's own text.
    static std::string statusLine(const Frontend& f) {
        const OnlineStatus& s = status(f);
        switch (s.phase) {
            case OnlinePhase::Checking: return tx(f, "online.checking");
            case OnlinePhase::Gathering: return tx(f, "online.gathering");
            case OnlinePhase::InviteReady:
            case OnlinePhase::WaitingReply: return s.message.empty() ? tx(f, "online.waiting_reply") : s.message;
            case OnlinePhase::Connecting:
            case OnlinePhase::Handshake: return tx(f, "online.connecting");
            default: return s.message;
        }
    }

    // The code box: a dark box with the code wrapped over its lines (a read-only field, copied
    // by the Copy button only).
    static void drawCodeBox(MenuDrawContext& c, const Frontend& f, float x, float y, float w, const std::string& code) {
        const float h = 15.0f * static_cast<float>(kMaxCodeLines) + 8;
        c.r.rect(x, y, w, h, packed(0x80000060u), Blend::Alpha);
        float ly = y + 4;
        for (const std::string& line : wrapChars(code, w - 16, kMaxCodeLines)) {
            put(c, f, x + 8, ly, line, Color{});
            ly += 15;
        }
    }
    static void drawLines(MenuDrawContext& c, const Frontend& f, float x, float y, float w, const std::string& s,
                          Color col, Align al = Align::Left) {
        const float ax = al == Align::Center ? x + w * 0.5f : x;
        for (const std::string& line : wrapWords(s, w)) {
            put(c, f, ax, y, line, col, al);
            y += 15;
        }
    }

    // The paste field (a custom item: the text lives in MenuItem::text, any length).
    static void addField(Menu& m, const Frontend& f, RectF r) {
        m.addCustom(
            kOnlineFieldItem, r,
            [](MenuDrawContext& c, MenuItem& self, bool focused) {
                c.r.rect(self.hit.x, self.hit.y, self.hit.w, self.hit.h, packed(0x80000060u), Blend::Alpha);
                if (focused) {
                    const Color p = pulse(2, 0, c.mt);
                    c.r.outline(self.hit.x, self.hit.y, self.hit.w, self.hit.h, {p.r, p.g * 0.63f, 0, 1}, Blend::Add);
                }
                const float ty = self.hit.y + std::floor((self.hit.h - 15) * 0.5f);
                const std::string shown = fieldShown(self.text, self.hit.w - 20);
                widgets::text(c, self.hit.x + 6, ty, shown, Color{});
                if (focused && ((c.ms / 250) % 2) == 1 && shown == self.text)
                    widgets::text(c, self.hit.x + 6 + measureText(fm(), shown), ty, "_", Color{});
            },
            [](MenuItem& self, int code) {
                if (code == keys::Backspace) {
                    if (!self.text.empty()) self.text.pop_back();
                    return true;
                }
                if (code == keys::Delete) {
                    self.text.clear();
                    return true;
                }
                return code == keys::Mouse1; // a click focuses it, it does not submit
            });
        (void)f;
    }
    // Typed characters (and a Ctrl+V the loop turned into some) go to the field while it has the focus.
    static void fieldChars(Menu& m) {
        m.onChar = [](Menu& menu, char ch) {
            if (menu.focused < 0) return;
            MenuItem& it = menu.items[static_cast<size_t>(menu.focused)];
            if (it.id != kOnlineFieldItem || it.disabled()) return;
            const unsigned char u = static_cast<unsigned char>(ch);
            if (u > 0x20 && u < 0x7F && it.text.size() < kMaxFieldChars) it.text += ch;
        };
    }

    // ---- Online: Host a game / Join a game ----------------------------------------------------
    static Menu online(Frontend& f) {
        Menu m = newMenuFor(f);
        const float y = y0(f);
        button(m, f, kHost, Free, {240, y + 70, 320, 42}, tx(f, "online.host"), 1.5f);
        button(m, f, kJoin, Free, {240, y + 126, 320, 42}, tx(f, "online.join"), 1.5f);
        button(m, f, kBack, Left, {}, tx(f, "online.back"));
        auto back = [&f]() { cancelAndPop(f); };
        m.onItem = [&f, back](Menu&, MenuItem& it, int ev) {
            if (ev != kActivate) return;
            if (it.id == kBack) back();
            else if (it.id == kHost && f.online_) {
                f.online_->hostGame(f.onlineInitial_);
                f.open(Screen::OnlineHostCode);
            } else if (it.id == kJoin) {
                f.open(Screen::OnlineJoinCode);
            }
        };
        backKeys(m, back);
        m.onUpdate = [&f](Menu& menu, float, float) {
            if (watchEnd(f)) return;
            // "Checking game files..." replaces the buttons until the check is through.
            const bool checking = status(f).phase == OnlinePhase::Checking;
            for (MenuItem& it : menu.items)
                if (it.id == kHost || it.id == kJoin) {
                    it.setShown(!checking);
                    if (!f.online_) it.setEnabled(false);
                }
        };
        m.drawBack = [&f, y](MenuDrawContext& c) {
            chromeBack(c, f, 250);
            drawLines(c, f, 140, y + 8, 520, tx(f, "online.intro"), hi(f), Align::Center);
            if (status(f).phase == OnlinePhase::Checking)
                put(c, f, 400, y + 96, tx(f, "online.checking"), pulse(2, 0, c.mt).r > 0.5f ? hi(f) : lo(f), Align::Center);
        };
        m.drawFront = [&f](MenuDrawContext& c) { chromeFront(c, f, 250); };
        return m;
    }

    // ---- The host's invite code ---------------------------------------------------------------
    static Menu hostCode(Frontend& f) {
        Menu m = newMenuFor(f);
        const float y = y0(f);
        const bool page = f.online_ && f.online_->pageDialog();
        button(m, f, kCopy, Free, {110, y + 169, 90, 28}, tx(f, "online.copy"));
        addField(m, f, {110, y + 241, 390, 24});
        button(m, f, kPaste, Free, {506, y + 239, 90, 28}, tx(f, "online.paste"));
        button(m, f, kConnect, Free, {602, y + 239, 96, 28}, tx(f, "online.connect"));
        button(m, f, kBack, Left, {}, tx(f, "online.back"));
        if (page)
            for (MenuItem& it : m.items)
                if (it.id != kBack) it.setShown(false);
        fieldChars(m);
        auto back = [&f]() { cancelAndPop(f); };
        auto connect = [&f](Menu& menu) {
            MenuItem* field = menu.find(kOnlineFieldItem);
            if (!f.online_ || !field || field->text.empty()) return;
            const OnlinePhase p = status(f).phase;
            if (p != OnlinePhase::InviteReady && p != OnlinePhase::WaitingReply) return;
            f.online_->acceptReply(field->text);
            field->text.clear();
        };
        m.onItem = [&f, back, connect](Menu& menu, MenuItem& it, int ev) {
            if (ev != kActivate) return;
            switch (it.id) {
                case kBack: back(); break;
                case kCopy: if (f.online_) f.online_->setClipboardText(status(f).code); break;
                case kPaste:
                    if (MenuItem* field = menu.find(kOnlineFieldItem))
                        if (f.online_) field->text = cleanCode(f.online_->clipboardText());
                    break;
                case kConnect:
                case kOnlineFieldItem: connect(menu); break;
                default: break;
            }
        };
        backKeys(m, back);
        m.onUpdate = [&f, page](Menu& menu, float, float) {
            if (watchEnd(f) || watchLobby(f)) return;
            if (page) return;
            const OnlinePhase p = status(f).phase;
            const bool ready = p == OnlinePhase::InviteReady || p == OnlinePhase::WaitingReply;
            const MenuItem* field = menu.find(kOnlineFieldItem);
            if (MenuItem* c = menu.find(kConnect)) c->setEnabled(ready && field && !field->text.empty());
            if (MenuItem* c = menu.find(kCopy)) c->setEnabled(!status(f).code.empty());
        };
        m.drawBack = [&f, y, page](MenuDrawContext& c) {
            chromeBack(c, f, 306);
            put(c, f, 110, y, tx(f, "online.invite_label"), hi(f));
            if (page) {
                put(c, f, 110, y + 24, tx(f, "online.page_code"), hi(f));
            } else {
                drawCodeBox(c, f, 110, y + 20, 580, status(f).code);
                drawLines(c, f, 212, y + 169, 478, tx(f, "online.code_warning"), lo(f));
                drawLines(c, f, 110, y + 207, 580, tx(f, "online.paste_reply"), hi(f));
            }
            put(c, f, 110, y + 273, statusLine(f), hi(f));
        };
        m.drawFront = [&f](MenuDrawContext& c) { chromeFront(c, f, 306); };
        return m;
    }

    // ---- The guest: paste the invite, then show the reply code --------------------------------
    static Menu joinCode(Frontend& f) {
        Menu m = newMenuFor(f);
        const float y = y0(f);
        const bool page = f.online_ && f.online_->pageDialog();
        addField(m, f, {110, y + 22, 390, 24});
        button(m, f, kPaste, Free, {506, y + 20, 90, 28}, tx(f, "online.paste"));
        button(m, f, kNext, Free, {602, y + 20, 96, 28}, tx(f, "online.next"));
        button(m, f, kCopy, Free, {110, y + 169, 90, 28}, tx(f, "online.copy"));
        button(m, f, kBack, Left, {}, tx(f, "online.back"));
        fieldChars(m);
        auto back = [&f]() { cancelAndPop(f); };
        auto next = [&f](Menu& menu) {
            MenuItem* field = menu.find(kOnlineFieldItem);
            if (!f.online_ || !field || field->text.empty() || status(f).phase != OnlinePhase::Idle) return;
            f.online_->joinGame(field->text);
            field->text.clear();
        };
        m.onItem = [&f, back, next](Menu& menu, MenuItem& it, int ev) {
            if (ev != kActivate) return;
            switch (it.id) {
                case kBack: back(); break;
                case kCopy: if (f.online_) f.online_->setClipboardText(status(f).code); break;
                case kPaste:
                    if (MenuItem* field = menu.find(kOnlineFieldItem))
                        if (f.online_) field->text = cleanCode(f.online_->clipboardText());
                    break;
                case kNext:
                case kOnlineFieldItem: next(menu); break;
                default: break;
            }
        };
        backKeys(m, back);
        m.onUpdate = [&f, page](Menu& menu, float, float) {
            if (watchEnd(f) || watchLobby(f)) return;
            // Idle: the paste step; any other phase: the reply code step.
            const bool paste = status(f).phase == OnlinePhase::Idle;
            const MenuItem* field = menu.find(kOnlineFieldItem);
            for (MenuItem& it : menu.items) {
                if (it.id == kBack) continue;
                const bool pasteItem = it.id == kOnlineFieldItem || it.id == kPaste || it.id == kNext;
                it.setShown(pasteItem == paste && !page);
            }
            if (MenuItem* n = menu.find(kNext)) n->setEnabled(paste && !page && field && !field->text.empty());
            if (MenuItem* c = menu.find(kCopy)) c->setEnabled(!status(f).code.empty());
        };
        m.drawBack = [&f, y, page](MenuDrawContext& c) {
            chromeBack(c, f, 306);
            if (status(f).phase == OnlinePhase::Idle) {
                put(c, f, 110, y, tx(f, "online.paste_invite"), hi(f));
                if (page) put(c, f, 110, y + 24, tx(f, "online.page_code"), hi(f));
                return;
            }
            put(c, f, 110, y, tx(f, "online.reply_label"), hi(f));
            if (page) {
                put(c, f, 110, y + 24, tx(f, "online.page_code"), hi(f));
            } else {
                drawCodeBox(c, f, 110, y + 20, 580, status(f).code);
                drawLines(c, f, 212, y + 169, 478, tx(f, "online.code_warning"), lo(f));
            }
            drawLines(c, f, 110, y + 207, 580, tx(f, "online.reply_sent"), hi(f));
            put(c, f, 110, y + 247, statusLine(f), hi(f));
        };
        m.drawFront = [&f](MenuDrawContext& c) { chromeFront(c, f, 306); };
        return m;
    }

    // ---- The lobby -----------------------------------------------------------------------------
    static Menu lobby(Frontend& f) {
        Menu m = newMenuFor(f);
        const float y = y0(f);
        const OnlineStatus& st = status(f);
        const bool host = st.host;
        const int helis = std::clamp(f.rules().helicopterCount, 1, 10);
        f.refreshLocks();

        // The host's unlocked missions; the guest has the host's choice as text.
        auto missions = std::make_shared<std::vector<int>>();
        std::vector<std::string> missionNames;
        for (int i = 0; i < f.rules().missionCount; i++)
            if (f.profile_.progress.missionUnlocked[i]) {
                missions->push_back(i);
                missionNames.push_back(f.missionLabel(i));
            }
        if (missions->empty()) {
            missions->push_back(0);
            missionNames.push_back(f.missionLabel(0));
        }
        std::vector<std::string> diffs;
        for (int i = 0; i < f.rules().difficultyCount; i++) diffs.push_back(f.texts_.get("difficulty." + std::to_string(i)));
        auto heliList = std::make_shared<std::vector<int>>();
        std::vector<std::string> heliNames;
        for (int i = 0; i < helis; i++)
            if (f.profile_.progress.helicopterUnlocked[i]) {
                heliList->push_back(i);
                heliNames.push_back(heliName(f, i));
            }
        if (heliList->empty()) {
            heliList->push_back(0);
            heliNames.push_back(heliName(f, 0));
        }
        auto indexOf = [](const std::vector<int>& v, int value) {
            const auto it = std::find(v.begin(), v.end(), value);
            return it == v.end() ? 0 : static_cast<int>(it - v.begin());
        };

        const float x = 430;
        const int mine = host ? 0 : 1;
        if (host) {
            m.addSpinner(kMission, x, y + 34, tx(f, "online.lobby.mission"), missionNames, indexOf(*missions, st.lobby.mission));
            m.addSpinner(kDifficulty, x, y + 64, tx(f, "online.lobby.difficulty"), diffs,
                         std::clamp(st.lobby.difficulty, 0, std::max(0, static_cast<int>(diffs.size()) - 1)));
        }
        m.addSpinner(kHeli, x, y + 94, tx(f, "online.lobby.your_heli"), heliNames, indexOf(*heliList, st.lobby.heli[mine]));
        button(m, f, kBack, Left, {}, tx(f, "online.back"));
        if (host) button(m, f, kStart, Right, {}, tx(f, "online.start")).setEnabled(false);
        else button(m, f, kReady, Right, {}, tx(f, st.lobby.guestReady ? "online.not_ready" : "online.ready"));

        auto back = [&f]() { cancelAndPop(f); };
        m.onItem = [&f, back, missions, heliList, host, mine](Menu&, MenuItem& it, int ev) {
            if (ev != kActivate || !f.online_) return;
            OnlineLobby l = status(f).lobby;
            switch (it.id) {
                case kBack: back(); break;
                case kMission: l.mission = (*missions)[static_cast<size_t>(std::clamp(it.index, 0, static_cast<int>(missions->size()) - 1))]; f.online_->setLobby(l); break;
                case kDifficulty: l.difficulty = it.index; f.online_->setLobby(l); break;
                case kHeli: {
                    const int h = (*heliList)[static_cast<size_t>(std::clamp(it.index, 0, static_cast<int>(heliList->size()) - 1))];
                    if (host) {
                        l.heli[mine] = h;
                        f.online_->setLobby(l);
                    } else {
                        f.online_->setMyHelicopter(h);
                    }
                    break;
                }
                case kReady: f.online_->setReady(!l.guestReady); break;
                case kStart:
                    if (host && l.guestReady) {
                        f.online_->startGame();
                        f.onlineStartCampaign(l); // the host's Start (8.1): both sides start the campaign
                    }
                    break;
                default: break;
            }
        };
        backKeys(m, back);
        m.onUpdate = [&f](Menu& menu, float, float) {
            if (watchEnd(f)) return;
            const OnlineStatus& s = status(f);
            if (MenuItem* b = menu.find(kStart)) b->setEnabled(s.lobby.guestReady && s.phase == OnlinePhase::Lobby);
            if (MenuItem* b = menu.find(kReady)) {
                b->label = f.texts_.get(s.lobby.guestReady ? "online.not_ready" : "online.ready");
                b->setEnabled(s.phase == OnlinePhase::Lobby);
            }
        };
        m.drawBack = [&f, y, host, mine, heliList](MenuDrawContext& c) {
            chromeBack(c, f, 306);
            const OnlineStatus& s = status(f);
            const OnlineLobby& l = s.lobby;
            const std::string role = tx(f, s.host ? "online.role.guest" : "online.role.host");
            put(c, f, 400, y, subst(tx(f, "online.connected"), {role, s.lobby.peerPlatform.empty() ? "?" : s.lobby.peerPlatform,
                                                                std::to_string(std::max(0, l.pingMs))}),
                hi(f), Align::Center);
            const Color col = hi(f);
            const float lx = 420, vx = 440;
            if (!host) {
                put(c, f, lx, y + 34, tx(f, "online.lobby.mission"), col, Align::Right);
                put(c, f, vx, y + 34, f.missionLabel(std::clamp(l.mission, 0, kMaxMissions - 1)), col);
                put(c, f, lx, y + 64, tx(f, "online.lobby.difficulty"), col, Align::Right);
                put(c, f, vx, y + 64, f.texts_.get("difficulty." + std::to_string(std::clamp(l.difficulty, 0, 7))), col);
            }
            put(c, f, lx, y + 124, tx(f, "online.lobby.other_heli"), col, Align::Right);
            put(c, f, vx, y + 124, heliName(f, std::clamp(l.heli[1 - mine], 0, 9)), col);
            const int frames = l.inputDelay;
            put(c, f, 400, y + 160, subst(tx(f, "online.lobby.delay"), {std::to_string(frames), std::to_string(frames * 1000 / 60)}),
                lo(f), Align::Center);
            if (!host) put(c, f, 400, y + 196, tx(f, "online.waiting_start"), col, Align::Center);
            (void)heliList;
        };
        m.drawFront = [&f](MenuDrawContext& c) { chromeFront(c, f, 306); };
        return m;
    }

    static std::string heliName(const Frontend& f, int heli) {
        if (f.sequel()) return SequelScreens::heliName(f, heli);
        return f.texts_.get("heli.generic") + " " + std::to_string(heli + 1);
    }

    // ---- The notice: why the session ended or failed -------------------------------------------
    static Menu notice(Frontend& f) {
        Menu m = newMenuFor(f);
        const float y = y0(f);
        const std::string msg = f.onlineMessage_;
        button(m, f, kOk, Middle, {}, tx(f, "online.ok"));
        auto ok = [&f]() {
            if (f.online_) f.online_->acknowledgeMessage();
            f.menus_.pop();
            // Back to the Online screen, or the join screen to try another code; the host's and
            // the lobby screens of the failed session go.
            while (!f.menus_.empty() && (f.topScreen() == Screen::OnlineHostCode || f.topScreen() == Screen::OnlineLobby))
                f.menus_.pop();
        };
        m.onItem = [ok](Menu&, MenuItem& it, int ev) {
            if (ev == kActivate && it.id == kOk) ok();
        };
        m.onKey = [ok](Menu&, int code) {
            if (code != keys::Escape && code != keys::Mouse2 && code != keys::Enter) return false;
            ok();
            return true;
        };
        m.drawBack = [&f, y, msg](MenuDrawContext& c) {
            chromeBack(c, f, 240);
            drawLines(c, f, 130, y + 20, 540, msg, hi(f), Align::Center);
        };
        m.drawFront = [&f](MenuDrawContext& c) { chromeFront(c, f, 240); };
        return m;
    }

    static Menu build(Frontend& f, Screen s) {
        switch (s) {
            case Screen::Online: return online(f);
            case Screen::OnlineHostCode: return hostCode(f);
            case Screen::OnlineJoinCode: return joinCode(f);
            case Screen::OnlineLobby: return lobby(f);
            case Screen::OnlineNotice: return notice(f);
            default: return Menu{};
        }
    }
};

// ---------------------------------------------------------------------------
// Frontend members
// ---------------------------------------------------------------------------
void Frontend::setOnlineHost(OnlineHost* h) {
    online_ = h;
    content_.onlineMode = h != nullptr;
}

bool Frontend::online() const {
    if (!online_) return false;
    const OnlinePhase p = online_->status().phase;
    return p >= OnlinePhase::Lobby && p <= OnlinePhase::Playing;
}

void Frontend::onlineApplyFlow(OnlineFlowChoice c) {
    switch (c) {
        case OnlineFlowChoice::Next: continueCampaign(); break;
        case OnlineFlowChoice::Restart: menus_.clear(); startLevel(true); break;
        // The checkpoint start is the sequels' (their Continue); the first game's "Continue" is its Next.
        case OnlineFlowChoice::Continue:
            if (sequel()) SequelScreens::newGame(*this);
            else continueCampaign();
            break;
        case OnlineFlowChoice::Quit: quitToMainMenu(false, false); break;
        case OnlineFlowChoice::StartCampaign:
            if (online_) onlineStartCampaign(online_->status().lobby);
            break;
    }
}

void Frontend::onlineStartCampaign(const OnlineLobby& l) {
    heli_[0] = l.heli[0];
    heli_[1] = l.heli[1];
    difficultyChoice_ = l.difficulty;
    startCampaign(l.mission, l.difficulty, 2);
}

void Frontend::onlineEnded(std::string message) {
    quitToMainMenu(false, false);
    onlineMessage_ = std::move(message);
    open(Screen::OnlineNotice);
}

void Frontend::onlineHintConfirmed() {
    if (menus_.empty() || topScreen() != Screen::Hint) return;
    menus_.pop();
    resumePlay();
}

void Frontend::updateOnlineNote() {
    onlineNote_.clear();
    if (!online_) return;
    const OnlineStatus& s = online_->status();
    if (s.phase != OnlinePhase::Playing) return;
    if (s.losing) onlineNote_ = texts_.get("online.note.losing");
    else if (s.stalled) onlineNote_ = texts_.get("online.note.stalled");
    else if (s.remotePaused) onlineNote_ = texts_.get("online.note.remote_paused");
    else if (s.lobby.pingMs >= 0) onlineNote_ = subst(texts_.get("online.note.ping"), {std::to_string(s.lobby.pingMs)});
}

namespace {
// The viewer's host: a status to look at, no session behind it.
struct DebugOnlineHost : OnlineHost {
    OnlineStatus st;
    const OnlineStatus& status() const override { return st; }
    void hostGame(const OnlineLobby&) override {}
    void joinGame(const std::string&) override {}
    void acceptReply(const std::string&) override {}
    void cancel() override {}
    void setLobby(const OnlineLobby& l) override { st.lobby = l; }
    void setMyHelicopter(int h) override { st.lobby.heli[1] = h; }
    void setReady(bool r) override { st.lobby.guestReady = r; }
    void startGame() override {}
    void chooseFlow(OnlineFlowChoice) override {}
    void requestConfirm() override {}
    std::string clipboardText() override { return "ABCD-EFGH"; }
    void setClipboardText(const std::string&) override {}
    void acknowledgeMessage() override {}
};
} // namespace

bool Frontend::debugOnline(const std::string& state) {
    auto host = std::make_unique<DebugOnlineHost>();
    OnlineStatus& st = host->st;
    st.lobby.pingMs = 38;
    st.lobby.inputDelay = 4;
    st.lobby.peerPlatform = "desktop";
    std::string code;
    for (int i = 0; code.size() < 360; i++) { // 360 characters with the dashes
        if (i && i % 5 == 0) code += '-';
        code += "0123456789ABCDEFGHJKMNPQRSTVWXYZ"[(i * 7 + i / 3) % 32];
    }
    if (state == "checking") st.phase = OnlinePhase::Checking;
    else if (state == "gathering") { st.phase = OnlinePhase::Gathering; st.host = true; }
    else if (state == "invite") { st.phase = OnlinePhase::InviteReady; st.host = true; st.code = code; }
    else if (state == "reply") { st.phase = OnlinePhase::ReplyReady; st.code = code; }
    else if (state == "lobbyhost") { st.phase = OnlinePhase::Lobby; st.host = true; st.lobby.guestReady = true; }
    else if (state == "lobbyguest") { st.phase = OnlinePhase::Lobby; st.lobby.mission = 2; st.lobby.heli[0] = 1; }
    else if (state == "failed") {
        st.phase = OnlinePhase::Failed;
        st.message = texts_.get("online.err.cant_connect");
        onlineMessage_ = st.message;
    } else if (state == "playing") st.phase = OnlinePhase::Playing;
    else if (state != "idle") return false;
    debugOnline_ = std::move(host);
    setOnlineHost(debugOnline_.get());
    return true;
}

Menu Frontend::buildOnlineScreen(Screen s) { return OnlineScreens::build(*this, s); }

} // namespace as3d::ui
