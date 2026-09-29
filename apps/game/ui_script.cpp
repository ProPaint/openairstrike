#include "ui_script.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace as3d_game {

using namespace as3d;
namespace keys = as3d::ui::keys;

int uiKeyCode(const std::string& name) {
    if (name.empty()) return -1;
    struct Named {
        const char* name;
        int code;
    };
    static const Named kNames[] = {
        {"escape", keys::Escape}, {"esc", keys::Escape}, {"enter", keys::Enter}, {"return", keys::Enter},
        {"space", keys::Space}, {"backspace", keys::Backspace}, {"tab", keys::Tab}, {"delete", keys::Delete},
        {"up", keys::Up}, {"down", keys::Down}, {"left", keys::Left}, {"right", keys::Right},
        {"pageup", keys::PageUp}, {"pagedown", keys::PageDown}, {"home", keys::Home}, {"end", keys::End},
        {"pause", keys::Pause}, {"mouse1", keys::Mouse1}, {"mouse2", keys::Mouse2}, {"mouse3", keys::Mouse3},
    };
    std::string low = name;
    for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const Named& n : kNames)
        if (low == n.name) return n.code;
    if (low.size() >= 2 && low[0] == 'f' && std::isdigit(static_cast<unsigned char>(low[1]))) {
        int n = std::atoi(low.c_str() + 1);
        if (n >= 1 && n <= 12) return 0x70 + n - 1;
        return -1;
    }
    if (name.size() == 1 && std::isalnum(static_cast<unsigned char>(name[0])))
        return std::toupper(static_cast<unsigned char>(name[0]));
    bool digits = std::all_of(name.begin(), name.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
    if (digits && name.size() <= 3) return std::atoi(name.c_str());
    return -1;
}

bool UiScript::parse(const std::string& text, std::string* error) {
    commands_.clear();
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    auto fail = [&](const char* why) {
        if (error) *error = "line " + std::to_string(lineNo) + ": " + why;
        commands_.clear();
        return false;
    };
    while (std::getline(in, line)) {
        ++lineNo;
        size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        std::istringstream ls(line);
        long frame = -1;
        std::string cmd;
        if (!(ls >> frame)) {
            if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
            return fail("expected a frame number");
        }
        if (frame < 0 || frame > 100000000) return fail("bad frame");
        if (!commands_.empty() && static_cast<u32>(frame) < commands_.back().frame) return fail("frames must not decrease");
        if (!(ls >> cmd)) return fail("expected a command");
        Command c;
        c.frame = static_cast<u32>(frame);
        auto push = [&](ui::UiEvent::Type t, int code, float x, float y) {
            c.event = ui::UiEvent{t, code, x, y};
            commands_.push_back(c);
        };
        if (cmd == "tap" || cmd == "move") {
            float x = 0, y = 0;
            if (!(ls >> x >> y)) return fail("expected x y");
            push(ui::UiEvent::Type::PointerMove, 0, x, y);
            if (cmd == "tap") {
                push(ui::UiEvent::Type::Press, keys::Mouse1, 0, 0);
                push(ui::UiEvent::Type::Release, keys::Mouse1, 0, 0);
            }
        } else if (cmd == "press" || cmd == "release" || cmd == "key") {
            std::string k;
            if (!(ls >> k)) return fail("expected a key");
            int code = uiKeyCode(k);
            if (code < 0) return fail("unknown key");
            if (cmd != "release") push(ui::UiEvent::Type::Press, code, 0, 0);
            if (cmd != "press") push(ui::UiEvent::Type::Release, code, 0, 0);
        } else if (cmd == "text") {
            std::string rest;
            std::getline(ls, rest);
            size_t start = rest.find_first_not_of(" \t");
            if (start == std::string::npos) return fail("expected text");
            rest = rest.substr(start);
            while (!rest.empty() && (rest.back() == '\r' || rest.back() == ' ')) rest.pop_back();
            for (char ch : rest) push(ui::UiEvent::Type::Char, static_cast<unsigned char>(ch), 0, 0);
        } else if (cmd == "shot") {
            std::string name;
            if (!(ls >> name)) return fail("expected a name");
            for (char ch : name)
                if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_' && ch != '-') return fail("bad shot name");
            c.shot = true;
            c.name = name;
            commands_.push_back(c);
        } else {
            return fail("unknown command");
        }
        if (commands_.size() > 1000000) return fail("too many commands");
    }
    return true;
}

bool UiScript::load(const std::string& path, std::string* error) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
        text.append(buf, n);
        if (text.size() > (16u << 20)) break;
    }
    std::fclose(f);
    return parse(text, error);
}

void UiScript::eventsAt(u32 frame, ui::UiInput& out, std::vector<std::string>* shots) const {
    auto it = std::lower_bound(commands_.begin(), commands_.end(), frame,
                               [](const Command& c, u32 f) { return c.frame < f; });
    for (; it != commands_.end() && it->frame == frame; ++it) {
        if (it->shot) {
            if (shots) shots->push_back(it->name);
        } else {
            out.events.push_back(it->event);
        }
    }
}

} // namespace as3d_game
