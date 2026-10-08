#include "run_config.h"

#include <cstdio>
#include <cstdlib>

namespace ctrapp {

const char* modeName(Mode m) {
    switch (m) {
        case Mode::Spike: return "spike";
        case Mode::Probe: return "probe";
        case Mode::Sim: return "sim";
        case Mode::Bot: return "bot";
        case Mode::Play: return "play";
        case Mode::Menu: return "menu";
    }
    return "?";
}

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

bool toInt(const std::string& v, int* out) {
    char* end = nullptr;
    long n = std::strtol(v.c_str(), &end, 10);
    if (v.empty() || *end != '\0') return false;
    *out = static_cast<int>(n);
    return true;
}

bool toFloat(const std::string& v, float* out) {
    char* end = nullptr;
    float f = std::strtof(v.c_str(), &end);
    if (v.empty() || *end != '\0') return false;
    *out = f;
    return true;
}

} // namespace

RunConfig parseRunConfig(const std::string& text) {
    RunConfig c;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string line = trim(text.substr(pos, nl - pos));
        pos = nl + 1;
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq)), val = trim(line.substr(eq + 1));
        int n = 0;
        float f = 0.0f;
        if (key == "mode") {
            if (val == "play") c.mode = Mode::Play;
            else if (val == "menu") c.mode = Mode::Menu;
            else if (val == "bot") c.mode = Mode::Bot;
            else if (val == "sim") c.mode = Mode::Sim;
            else if (val == "probe") c.mode = Mode::Probe;
            else if (val == "spike") c.mode = Mode::Spike;
        } else if (key == "game") {
            if (val == "select") {
                c.gameGiven = false; // the selector (ctr_selector.h)
            } else if (!val.empty()) {
                c.game = val;
                c.gameGiven = true;
            }
        } else if (key == "level") {
            if (toInt(val, &n) && n >= 1) {
                c.level = n;
                c.levelGiven = true;
            }
        } else if (key == "frames") {
            if (toInt(val, &n) && n >= 0) c.frames = n;
        } else if (key == "perf") {
            if (toInt(val, &n)) c.perf = n != 0;
        } else if (key == "log_input") {
            if (toInt(val, &n)) c.logInput = n != 0;
        } else if (key.compare(0, 5, "bind_") == 0) {
            as3d::applyBindingOption(c.bindings, key, val);
        } else if (key == "difficulty") {
            if (toInt(val, &n) && n >= 0) c.difficulty = n;
        } else if (key == "dump_at") {
            if (toInt(val, &n) && n >= 0) c.dumpAt = n;
        } else if (key == "brightness") {
            if (toFloat(val, &f) && f >= 0.0f && f <= 1.0f) c.brightness = f;
        } else if (key == "slider") {
            if (toFloat(val, &f) && f >= 0.0f && f <= 1.0f) c.slider = f;
        } else if (key == "eye_offset") {
            if (toFloat(val, &f) && f >= 0.0f && f <= 50.0f) c.eyeOffset = f;
        } else if (key == "min_focal") {
            if (toFloat(val, &f) && f >= 1.0f) c.minFocal = f;
        } else if (key == "precache") {
            if (toInt(val, &n)) c.precache = n != 0;
        } else if (key == "audio") {
            if (toInt(val, &n)) c.audio = n != 0;
        } else if (key == "music") {
            if (toInt(val, &n)) c.music = n != 0;
        } else if (key == "music_taps") {
            if (toInt(val, &n) && (n == 0 || n == 1 || n == 2 || n == 4 || n == 8)) c.musicTaps = n;
        } else if (key == "attract") {
            if (toInt(val, &n) && n >= 0) c.attract = n;
        } else if (key == "logo") {
            if (toInt(val, &n)) c.logo = n != 0;
        } else if (key == "quality") {
            if (val == "auto") c.quality = QualityMode::Auto;
            else if (val == "new") c.quality = QualityMode::New;
            else if (val == "old") c.quality = QualityMode::Old;
        } else if (key == "menu_bot") {
            if (toInt(val, &n)) c.menuBot = n != 0;
        } else if (key == "menu_bot_steps") {
            if (toInt(val, &n) && n >= 0) c.menuBotSteps = n;
        } else if (key == "end_at") {
            if (toInt(val, &n) && n >= 0) c.endAt = n;
        } else if (key == "end_kind") {
            if (val == "complete") c.endGameOver = false;
            else if (val == "gameover") c.endGameOver = true;
        } else if (key == "end_score") {
            if (toInt(val, &n) && n >= 0) c.endScore = n;
        } else if (key == "unlock_heli") {
            if (toInt(val, &n) && n >= 0) c.unlockHeli = n;
        } else if (key == "ui_fixed_dt") {
            if (toInt(val, &n)) c.uiFixedDt = n != 0;
        } else if (key == "cycle") {
            // cycle=1,6,1,6: the memory growth check (M5.4), see run_config.h
            c.cycle.clear();
            size_t at = 0;
            while (at < val.size()) {
                size_t comma = val.find(',', at);
                if (comma == std::string::npos) comma = val.size();
                if (toInt(trim(val.substr(at, comma - at)), &n) && n >= 1) c.cycle.push_back(n);
                at = comma + 1;
            }
        } else if (key == "report_at") {
            if (toInt(val, &n) && n >= 0) c.reportAt = n;
        } else if (key == "cycle_frames") {
            if (toInt(val, &n) && n >= 1) c.cycleFrames = n;
        } else if (key == "pad_script") {
            c.padScript = val;
        } else if (key == "dump_every") {
            if (toInt(val, &n) && n >= 0) c.dumpEvery = n;
        } else if (key == "dump_screens") {
            if (toInt(val, &n)) c.dumpScreens = n != 0;
        } else if (key == "dump_prefix") {
            c.dumpPrefix = val;
        } else if (key == "depthmap") {
            if (val == "z") c.depthMapZ = true;
            else if (val == "w") c.depthMapZ = false;
        } else if (key == "water") {
            if (toInt(val, &n)) c.water = n != 0;
        } else if (key == "water_tile") {
            if (toFloat(val, &f) && f >= 0.0f) c.waterTile = f;
        } else if (key == "passes") {
            c.passes = val;
        } else if (key == "hud") {
            if (toInt(val, &n)) c.hud = n != 0;
        } else if (key == "touch") {
            if (toInt(val, &n)) c.touch = n != 0;
        }
    }
    return c;
}

RunConfig readRunConfig(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return RunConfig();
    std::string text;
    char buf[512];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    std::fclose(f);
    return parseRunConfig(text);
}

} // namespace ctrapp
