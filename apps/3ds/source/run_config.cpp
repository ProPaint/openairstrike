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
        case Mode::Soak: return "soak";
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
    applyRunConfig(c, text);
    return c;
}

void applyRunConfig(RunConfig& c, const std::string& text) {
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
            else if (val == "soak") c.mode = Mode::Soak;
            else c.mode = Mode::Spike; // spike, and the test modes (picatest, meshtest, ...): main.cpp
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
        } else if (key == "log_lazy") {
            if (toInt(val, &n)) c.logLazy = n != 0;
        } else if (key == "bot_weapons") {
            if (toInt(val, &n)) c.botWeapons = n != 0;
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
        } else if (key == "max_parallax_px") {
            if (toFloat(val, &f) && f >= 0.0f && f <= 100.0f) c.maxParallaxPx = f;
        } else if (key == "max_far_parallax_px") {
            if (toFloat(val, &f) && f >= 0.0f && f <= 100.0f) c.maxFarParallaxPx = f;
        } else if (key == "stereo_comfort") {
            if (toInt(val, &n)) c.stereoComfort = n != 0;
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
        } else if (key == "name_entry") {
            if (val == "applet") c.nameApplet = true;
            else if (val == "touch") c.nameApplet = false;
        } else if (key == "name_script") {
            c.nameScript = val;
        } else if (key == "pad_script") {
            c.padScript = val;
        } else if (key == "dump_every") {
            if (toInt(val, &n) && n >= 0) c.dumpEvery = n;
        } else if (key == "dump_screens") {
            if (toInt(val, &n)) c.dumpScreens = n != 0;
        } else if (key == "dump_from") {
            if (toInt(val, &n) && n >= 0) c.dumpFrom = n;
        } else if (key == "dump_prefix") {
            c.dumpPrefix = val;
        } else if (key == "depthmap") {
            if (val == "z") c.depthMapZ = true;
            else if (val == "w") c.depthMapZ = false;
        } else if (key == "fog") {
            if (toInt(val, &n)) c.fog = n != 0;
        } else if (key == "fixed_steps") {
            if (toInt(val, &n) && n >= 0 && n <= 8) c.fixedSteps = n;
        } else if (key == "mesh_merge") {
            if (toInt(val, &n)) c.meshMerge = n != 0;
        } else if (key == "mesh_sort") {
            if (toInt(val, &n)) c.meshSort = n != 0;
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
        } else if (key == "shadow_cache") {
            if (toInt(val, &n)) c.shadowCache = n != 0;
        } else if (key == "terrain_lod") {
            if (val == "auto") c.terrainLod = -1;
            else if (toInt(val, &n) && n >= 0 && n <= 2) c.terrainLod = n;
        } else if (key == "terrain_lod_dist") {
            if (toFloat(val, &f) && f >= 0.0f) c.terrainLodDistance = f;
        } else if (key == "terrain_lod_band") {
            if (toFloat(val, &f) && f >= 0.0f) c.terrainLodBand = f;
        } else if (key == "touch_segments") {
            if (toInt(val, &n) && n >= 8 && n <= 96) c.touchSegments = n;
        } else if (key == "shadow_atlas") {
            if (toInt(val, &n)) c.shadowAtlas = n != 0 ? 1 : 0;
        } else if (key == "top_format") {
            if (val == "rgb565") c.top565 = true;
            else if (val == "rgba8") c.top565 = false;
        } else if (key == "prefetch") {
            if (toInt(val, &n)) c.prefetch = n != 0;
        } else if (key == "prefetch_thread") { // R4.10
            if (toInt(val, &n)) c.prefetchThread = n != 0;
        } else if (key == "pak_cache_kb") {
            if (toInt(val, &n) && n >= 0 && n <= 8192) c.pakCacheKb = n;
        } else if (key == "pace") {
            if (val == "auto") c.pace = 0;
            else if (val == "60") c.pace = 60;
            else if (val == "30") c.pace = 30;
        } else if (key == "pace_rule") {
            PaceRule r;
            if (r.parse(val)) c.paceRule = r;
        } else if (key == "catchup") { // R4.2
            c.catchUp.parseMode(val);
        } else if (key == "catchup_lag") {
            c.catchUp.parseLag(val);
        } else if (key == "catchup_margin_ms") {
            c.catchUp.parseMargin(val);
        } else if (key == "ui_text") { // R4.4
            if (toInt(val, &n)) c.uiText = n != 0;
        } else if (key == "hint_hold") {
            if (toInt(val, &n) && n >= 0) c.hintHold = n;
        } else if (key == "dump_loading") {
            if (toInt(val, &n)) c.dumpLoading = n != 0;
        } else if (key == "ui_taps") {
            // F:@ID or F:@SCREEN/ID, comma-separated; a malformed entry is skipped.
            c.uiTaps.clear();
            size_t pos = 0;
            while (pos <= val.size()) {
                size_t end = val.find(',', pos);
                if (end == std::string::npos) end = val.size();
                const std::string e = val.substr(pos, end - pos);
                pos = end + 1;
                const size_t colon = e.find(":@");
                int f = 0, id = 0;
                if (colon == std::string::npos || !toInt(e.substr(0, colon), &f) || f < 0) continue;
                std::string rest = e.substr(colon + 2), screen;
                const size_t slash = rest.find('/');
                if (slash != std::string::npos) {
                    screen = rest.substr(0, slash);
                    rest = rest.substr(slash + 1);
                }
                if (!toInt(rest, &id)) continue;
                RunConfig::UiTap t;
                t.frame = f;
                t.item = id;
                t.screen = screen;
                c.uiTaps.push_back(t);
            }
        } else if (key == "ui_menus") { // R4.5
            if (toInt(val, &n)) c.uiMenus = n != 0;
        // R4.7
        } else if (key == "hud_stock") {
            if (toInt(val, &n)) c.hudStock = n > 0 ? n : 0;
        // R4.7 end
        } else if (key == "clear") {
            if (val == "split") c.clearMode = 0;
            else if (val == "joint") c.clearMode = 1;
            else if (val == "quad") c.clearMode = 2;
        } else if (key == "gpu_kick") {
            if (toInt(val, &n)) c.gpuKick = n != 0;
        } else if (key == "arena_poison") {
            if (toInt(val, &n)) c.arenaPoison = n != 0;
        } else if (key == "state_check") {
            if (toInt(val, &n) && n >= 0 && n <= 2) c.stateCheck = n;
        } else if (key == "probe_every") {
            if (toInt(val, &n) && n >= 0) c.probeEvery = n;
        } else if (key == "wash_check") {
            if (toInt(val, &n)) c.washCheck = n != 0 ? 1 : 0;
        } else if (key == "wash_inject") {
            if (toInt(val, &n) && n >= 0) c.washInject = n;
        } else if (key == "wash_log") {
            if (toInt(val, &n)) c.washLog = n != 0;
        } else if (key == "wash_selftest") {
            if (toInt(val, &n)) c.washSelftest = n != 0;
        } else if (key == "eye_replay") {
            if (toInt(val, &n)) c.eyeReplay = n != 0;
        } else if (key == "terrain_prime") {
            if (toInt(val, &n)) c.terrainPrime = n != 0;
        // R4.1 (docs/3ds-port.md "Stereo GPU budget (round 4, R4.1)")
        } else if (key == "gpu_passes") {
            if (toInt(val, &n) && n >= 0) c.gpuPasses = n;
        } else if (key == "gpu_kick_at") {
            if (val == "terrain") c.gpuKickAt = 1;
            else if (val == "eye") c.gpuKickAt = 0;
        } else if (key == "bottom_hz") {
            if (toInt(val, &n) && (n == 60 || n == 30)) c.bottomHz = n;
        } else if (key == "terrain_far_tev") {
            if (toInt(val, &n)) c.terrainFarTev = n != 0 ? 1 : 0;
        } else if (key == "brightness_skip") {
            if (toInt(val, &n)) c.brightnessSkip = n != 0 ? 1 : 0;
        } else if (key == "right_eye_rate") {
            if (val == "full") c.rightEyeRate = 0;
            else if (val == "half") c.rightEyeRate = 1;
            else if (val == "alternate") c.rightEyeRate = 2;
        } else if (key == "depth16") {
            if (toInt(val, &n)) c.depth16 = n != 0;
        } else if (key == "gpu_lean") {
            if (toInt(val, &n)) c.gpuLean = n != 0;
        // R4.1 end
        // R4.6 (docs/3ds-port.md "GPU cuts and the level start (round 4, R4.6)")
        } else if (key == "brightness_mode") {
            if (val == "fold") c.brightnessMode = 0;
            else if (val == "quad") c.brightnessMode = 1;
        } else if (key == "water_cover") {
            if (toInt(val, &n)) c.waterCover = n != 0;
        } else if (key == "shadow_fast_hit") {
            if (toInt(val, &n)) c.shadowFastHit = n != 0;
        } else if (key == "tex_fast") {
            if (toInt(val, &n)) c.texFast = n != 0;
        } else if (key == "terrain_light_cache") {
            if (toInt(val, &n)) c.terrainLightCache = n != 0;
        } else if (key == "terrain_base_cache") {
            if (toInt(val, &n)) c.terrainBaseCache = n != 0;
        } else if (key == "base_int") {
            if (toInt(val, &n)) c.baseInt = n != 0;
        // R4.6 end
        // R4.11 (docs/3ds-port.md "The sequels on the console (round 4, R4.11)")
        } else if (key == "skin_half") {
            if (val == "auto") c.skinHalf = -1;
            else if (toInt(val, &n)) c.skinHalf = n != 0 ? 1 : 0;
        } else if (key == "skin_half_min") {
            if (toInt(val, &n) && n >= 8) c.skinHalfMin = n;
        } else if (key == "particle_cull") {
            if (toInt(val, &n)) c.particleCull = n != 0;
        } else if (key == "ui_merge") {
            if (toInt(val, &n)) c.uiMerge = n != 0;
        // R4.11 end
        } else if (key == "terrain_force_state") {
            if (toInt(val, &n)) c.terrainForceState = n != 0;
        } else if (key == "probe_inject") {
            if (toInt(val, &n) && n >= 0) c.probeInject = n;
        } else if (key == "soak") {
            std::vector<SoakStage> stages;
            if (parseSoakList(val, &stages)) c.soak = stages; // a malformed list keeps the default stages
        } else if (key == "soak_frames") {
            if (toInt(val, &n) && n >= 0) c.soakFrames = n;
        } else if (key == "report_to") {
            std::string host;
            int port = 0;
            if (val == "none" || val.empty()) c.reportTo.clear();
            else if (parseHostPort(val, &host, &port)) c.reportTo = val;
        } else if (key == "end_flush") {
            if (val == "cmdlist") c.cmdlistFlush = true;
            else if (val == "all") c.cmdlistFlush = false;
        }
    }
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
