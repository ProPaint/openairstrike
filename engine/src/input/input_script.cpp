// Input scripts: parse, serialize, playback and recording (as3d/input.h).
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "as3d/input.h"

namespace as3d {

namespace {
constexpr size_t kMaxScriptEvents = 4u * 1024u * 1024u;
constexpr size_t kMaxScriptBytes = 256u * 1024u * 1024u;

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

// Splits a line (comment stripped) into at most 4 tokens.
int tokens(const std::string& line, std::string out[4]) {
    int n = 0;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && isSpace(line[i])) ++i;
        if (i >= line.size()) break;
        size_t j = i;
        while (j < line.size() && !isSpace(line[j])) ++j;
        if (n >= 4) return 5;
        out[n++] = line.substr(i, j - i);
        i = j;
    }
    return n;
}

bool parseU32(const std::string& s, u32& out) {
    if (s.empty() || s.size() > 10) return false;
    unsigned long long v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<unsigned>(c - '0');
    }
    if (v > 0xFFFFFFFFull) return false;
    out = static_cast<u32>(v);
    return true;
}

bool perPlayer(InputAction a) { return a != InputAction::Confirm && a != InputAction::Pause; }
} // namespace

bool InputScript::parse(const std::string& text, std::string* error) {
    events_.clear();
    if (text.size() > kMaxScriptBytes) {
        if (error) *error = "input script too large";
        return false;
    }
    size_t pos = 0;
    int lineNo = 0;
    u32 lastFrame = 0;
    auto fail = [&](const char* what) {
        if (error) *error = "line " + std::to_string(lineNo) + ": " + what;
        events_.clear();
        return false;
    };
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        ++lineNo;
        size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        std::string tok[4];
        int n = tokens(line, tok);
        if (n == 0) continue;
        if (n != 3) return fail("expected <frame> <action> <value>");
        InputScriptEvent e;
        if (!parseU32(tok[0], e.frame)) return fail("bad frame number");
        std::string name = tok[1];
        if (name.compare(0, 3, "p2_") == 0) {
            e.player = 1;
            name = name.substr(3);
        } else if (name.compare(0, 3, "p1_") == 0) {
            name = name.substr(3);
        }
        if (!inputActionFromName(name, e.action)) return fail("unknown action");
        if (e.player == 1 && !perPlayer(e.action)) return fail("action has no player 2 form");
        if (tok[2] == "0") e.value = 0;
        else if (tok[2] == "1") e.value = 1;
        else return fail("value must be 0 or 1");
        if (!events_.empty() && e.frame < lastFrame) return fail("frames must not decrease");
        if (events_.size() >= kMaxScriptEvents) return fail("too many events");
        lastFrame = e.frame;
        events_.push_back(e);
    }
    return true;
}

bool InputScript::load(const std::string& filePath, std::string* error) {
    std::FILE* f = std::fopen(filePath.c_str(), "rb");
    if (!f) {
        if (error) *error = "cannot open " + filePath;
        return false;
    }
    std::string text;
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
        text.append(buf, n);
        if (text.size() > kMaxScriptBytes) break;
    }
    std::fclose(f);
    return parse(text, error);
}

std::string InputScript::serialize() const {
    std::string out = "# as3d input script: <frame> <action> <value>\n";
    char line[96];
    for (const InputScriptEvent& e : events_) {
        std::snprintf(line, sizeof line, "%u %s%s %d\n", e.frame, e.player == 1 ? "p2_" : "",
                      inputActionName(e.action), e.value);
        out += line;
    }
    return out;
}

bool InputScript::save(const std::string& filePath) const {
    std::string text = serialize();
    std::FILE* f = std::fopen(filePath.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

FrameInput InputScriptPlayer::frame(u32 frame) {
    held_.pausePressed = false;
    const std::vector<InputScriptEvent>& ev = script_->events();
    // Events for frames already passed (a caller that skipped frames) still apply.
    while (next_ < ev.size() && ev[next_].frame <= frame) {
        const InputScriptEvent& e = ev[next_++];
        if (e.action == InputAction::Pause) {
            if (e.value) held_.pausePressed = e.frame == frame;
        } else if (e.action == InputAction::Confirm) {
            held_.confirm = e.value != 0;
        } else {
            u32 bit = inputActionBit(e.action);
            int p = e.player == 1 ? 1 : 0;
            if (e.value) held_.held[p] |= bit;
            else held_.held[p] &= ~bit;
        }
    }
    return held_;
}

void InputRecorder::record(u32 frame, const FrameInput& in) {
    for (int p = 0; p < kMaxPlayers; ++p) {
        u32 changed = in.held[p] ^ last_.held[p];
        for (int a = 0; a < kInputActionCount; ++a) {
            u32 bit = inputActionBit(static_cast<InputAction>(a));
            if (!(changed & bit)) continue;
            InputScriptEvent e;
            e.frame = frame;
            e.player = p;
            e.action = static_cast<InputAction>(a);
            e.value = (in.held[p] & bit) ? 1 : 0;
            script_.add(e);
        }
    }
    if (in.confirm != last_.confirm) {
        InputScriptEvent e;
        e.frame = frame;
        e.action = InputAction::Confirm;
        e.value = in.confirm ? 1 : 0;
        script_.add(e);
    }
    if (in.pausePressed) {
        InputScriptEvent e;
        e.frame = frame;
        e.action = InputAction::Pause;
        e.value = 1;
        script_.add(e);
    }
    last_ = in;
    last_.pausePressed = false;
}

} // namespace as3d
