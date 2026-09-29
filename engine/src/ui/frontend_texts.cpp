// Front-end texts (issue 080) and the parts of Settings.xml the front end uses.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "as3d/frontend.h"

namespace as3d::ui {

namespace {

// Built-in English defaults for the short generic labels (frontend.md gives them all). The
// story, overview, weapon descriptions, credits and congratulation lines have no default.
const std::map<std::string, std::string>& defaults() {
    static const std::map<std::string, std::string> d = [] {
        std::map<std::string, std::string> m = {
            {"rank.0", "Cheater"}, {"rank.1", "Rookie"}, {"rank.2", "Junior Pilot"}, {"rank.3", "Pilot"},
            {"rank.4", "Master Pilot"}, {"rank.5", "Berserker"}, {"rank.6", "Elite"},
            {"difficulty.0", "Very Easy"}, {"difficulty.1", "Easy"}, {"difficulty.2", "Normal"},
            {"difficulty.3", "Hard"}, {"difficulty.4", "Nightmare"},
            {"label.choose_mission", "Choose mission:"}, {"label.difficulty", "Difficulty:"},
            {"label.game_mode", "Game mode:"}, {"label.players.1", "1 Player"}, {"label.players.2", "2 Players"},
            {"label.exit", "Are you sure you want to quit?"}, {"label.enter_name", "Please enter your name:"},
            {"stat.enemies", "Enemies destroyed:"}, {"stat.stars", "Stars collected:"},
            {"stat.rank", "Your current rank:"},
            {"scores.number", "#"}, {"scores.name", "Name"}, {"scores.score", "Score"}, {"scores.rank", "Rank"},
            {"opt.resolution", "Resolution:"}, {"opt.refresh", "Refresh rate:"}, {"opt.depth", "Color Depth:"},
            {"opt.fullscreen", "Fullscreen:"}, {"opt.brightness", "Brightness:"}, {"opt.sfx", "Sound Volume:"},
            {"opt.music", "Music Volume:"}, {"opt.sound3d", "3D Sound:"}, {"opt.camera", "Camera:"},
            {"opt.mouse", "Mouse Control:"}, {"opt.refresh.default", "default"}, {"opt.hz", " Hz"},
            {"opt.depth.0", "Default"}, {"opt.depth.16", "16 bit"}, {"opt.depth.32", "32 bit"},
            {"opt.off", "Off"}, {"opt.on", "On"},
            {"opt.screen", "Screen:"}, {"opt.screen.wide", "Wide"}, {"opt.screen.4x3", "4:3"},
            {"opt.controls", "Controls:"}, {"opt.controls.right", "Right"}, {"opt.controls.left", "Left"},
            {"opt.showfps", "Show FPS:"}, {"opt.touchspeed", "Touch speed:"}, {"opt.touchspeed.0", "Original"}, {"opt.touchspeed.1", "x1.25"},
            {"opt.touchspeed.2", "x1.5"}, {"opt.touchspeed.3", "x1.75"}, {"opt.touchspeed.4", "x2"},
            {"opt.tooltip.resolution", "Changes screen resolution."},
            {"camera.0", "Low Pitch"}, {"camera.1", "Default"}, {"camera.2", "High Pitch"}, {"camera.3", "Top-Down"},
            {"ctl.set", "Controls Set:"}, {"ctl.player.1", "Player 1"}, {"ctl.player.2", "Player 2"},
            {"ctl.row.0", "Primary Attack"}, {"ctl.row.1", "Switch Weapon"}, {"ctl.row.2", "Missile Attack"},
            {"ctl.row.3", "Switch Missiles"}, {"ctl.row.4", "Use Item"}, {"ctl.row.5", "Switch Item"},
            {"ctl.row.6", "Move Forward"}, {"ctl.row.7", "Move Backward"}, {"ctl.row.8", "Move Left"},
            {"ctl.row.9", "Move Right"}, {"ctl.or", " or "}, {"ctl.unbound", "???"},
            {"info.page", "Page:"}, {"info.hint.prev", "PgUp - Previous Page"},
            {"info.hint.next", "PgDown - Next Page"},
            {"info.missing.title", "INFORMATION"},
            {"info.missing.0", "The information texts are not installed."},
            {"info.missing.1", "Run {tools/extract_exe_texts.py} with your copy of"},
            {"info.missing.2", "AirStrike 3D v1.70 to import them."},
            {"congrats.0", "Congratulations!"},
            {"congrats.missing", "(The texts of this screen are not installed.)"},
            {"touch.menu", "MENU"}, {"touch.cancel", "Cancel"}, {"touch.clear", "Clear"},
            {"touch.del", "Del"}, {"touch.space", "Sp"},
        };
        char key[32], val[32];
        for (int n = 1; n <= 10; n++) {
            std::snprintf(key, sizeof key, "info.pages.%d", n);
            std::snprintf(val, sizeof val, "%d of 10", n);
            m[key] = val;
        }
        return m;
    }();
    return d;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

} // namespace

int Texts::parse(std::string_view content) {
    int n = 0;
    size_t pos = 0;
    while (pos < content.size()) {
        size_t eol = content.find('\n', pos);
        if (eol == std::string_view::npos) eol = content.size();
        std::string_view line = trim(content.substr(pos, eol - pos));
        pos = eol + 1;
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        std::string_view key = trim(line.substr(0, eq));
        std::string_view rest = trim(line.substr(eq + 1));
        if (key.empty() || key.size() > 64 || rest.size() < 2 || rest.front() != '"') continue;
        std::string value;
        bool closed = false;
        for (size_t i = 1; i < rest.size(); i++) {
            const char c = rest[i];
            if (c == '\\' && i + 1 < rest.size()) {
                value += rest[++i];
            } else if (c == '"') {
                closed = i + 1 == rest.size();
                break;
            } else {
                value += c;
            }
        }
        if (!closed || value.size() > 512) continue;
        values_[std::string(key)] = std::move(value);
        n++;
    }
    return n;
}

std::string Texts::get(std::string_view key) const {
    auto it = values_.find(std::string(key));
    if (it != values_.end()) return it->second;
    const auto& d = defaults();
    auto jt = d.find(std::string(key));
    return jt != d.end() ? jt->second : std::string();
}

// ---------------------------------------------------------------------------
// Settings.xml (a tiny tag scanner: only the elements and attributes named in frontend.md)
// ---------------------------------------------------------------------------
namespace {

struct Tag {
    std::string name;
    bool closing = false;
    std::map<std::string, std::string> attrs;
};

bool nextTag(std::string_view xml, size_t& pos, Tag& tag) {
    while (true) {
        const size_t lt = xml.find('<', pos);
        if (lt == std::string_view::npos) return false;
        if (xml.substr(lt, 4) == "<!--") {
            const size_t end = xml.find("-->", lt + 4);
            if (end == std::string_view::npos) return false;
            pos = end + 3;
            continue;
        }
        const size_t gt = xml.find('>', lt);
        if (gt == std::string_view::npos) return false;
        std::string_view body = xml.substr(lt + 1, gt - lt - 1);
        pos = gt + 1;
        if (!body.empty() && (body[0] == '?' || body[0] == '!')) continue;
        tag = Tag{};
        if (!body.empty() && body[0] == '/') { tag.closing = true; body.remove_prefix(1); }
        if (!body.empty() && body.back() == '/') body.remove_suffix(1);
        size_t i = 0;
        while (i < body.size() && body[i] != ' ' && body[i] != '\t' && body[i] != '\r' && body[i] != '\n') i++;
        tag.name = std::string(body.substr(0, i));
        while (i < body.size()) {
            while (i < body.size() && (body[i] == ' ' || body[i] == '\t' || body[i] == '\r' || body[i] == '\n')) i++;
            const size_t eq = body.find('=', i);
            if (eq == std::string_view::npos) break;
            std::string key(trim(body.substr(i, eq - i)));
            const size_t q1 = body.find('"', eq);
            if (q1 == std::string_view::npos) break;
            const size_t q2 = body.find('"', q1 + 1);
            if (q2 == std::string_view::npos) break;
            tag.attrs[key] = std::string(body.substr(q1 + 1, q2 - q1 - 1));
            i = q2 + 1;
        }
        return true;
    }
}

float num(const Tag& t, const char* k) {
    auto it = t.attrs.find(k);
    return it == t.attrs.end() ? 0.0f : static_cast<float>(std::atof(it->second.c_str()));
}

} // namespace

bool parseSettingsXml(std::string_view xml, FrontendContent& out) {
    size_t pos = 0;
    Tag t;
    enum { None, Intros, Logotypes } section = None;
    bool any = false;
    IntroPage* image = nullptr;
    while (nextTag(xml, pos, t)) {
        if (t.name == "Info" && !t.closing) {
            out.version = t.attrs["version"];
            out.copyright = t.attrs["copyright"];
            any = true;
        } else if (t.name == "Intros") {
            section = t.closing ? None : Intros;
            if (!t.closing) out.intros.clear();
            image = nullptr;
            any = true;
        } else if (t.name == "Logotypes") {
            section = t.closing ? None : Logotypes;
            if (!t.closing) out.logos.clear();
            any = true;
        } else if (section == Intros && !t.closing) {
            if (t.name == "BuiltIn" && t.attrs["name"] == "DivoGames") {
                out.intros.push_back(IntroPage{});
                image = nullptr;
            } else if (t.name == "Image") {
                IntroPage p;
                p.divoGames = false;
                p.image = t.attrs["name"];
                out.intros.push_back(p);
                image = &out.intros.back();
            } else if (t.name == "BackColor" && image) {
                image->back = {num(t, "r") / 255.0f, num(t, "g") / 255.0f, num(t, "b") / 255.0f, 1.0f};
            } else {
                image = nullptr; // other tags (ImageTemp) are ignored, with their children
            }
        } else if (section == Logotypes && !t.closing && t.name == "Image") {
            LogoImage l;
            l.path = t.attrs["name"];
            l.x = num(t, "x");
            l.y = num(t, "y");
            l.invertX = num(t, "invertAxisX") == 1.0f;
            l.invertY = num(t, "invertAxisY") == 1.0f;
            out.logos.push_back(l);
        }
    }
    return any;
}

} // namespace as3d::ui
