// `as3d_viewer screen <name> [--touch] [--size WxH] [--state k=v,...] [--pointer x,y]
//  [--no-texts] --out f.png` and `as3d_viewer screen --list`: renders one front-end screen
// (docs/spec/frontend.md 3) headless over a plain background. The 3D banner of the main menu
// and the level behind the menus are not drawn. --state takes the keys of Frontend::debugSet
// (mt=seconds of menu time, players=2, unlock=1, page=N, capture=row, name=S, hint=S, kills,
// enemies, stars, startotal, score, maxscore, cheat, heli, heli2, mission, ingame=1).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "as3d/defs.h"
#include "as3d/frontend.h"
#include "as3d/image.h"
#include "as3d/ui.h"
#include "common.h"
#include "registry.h"

namespace viewer {
bool renderUiHeadless(int w, int h, as3d::ui::Color bg,
                      const std::function<void(as3d::ui::Renderer2D&, const as3d::ui::UiAssets&)>& draw,
                      as3d::Image& out);
}

namespace {

using namespace as3d::ui;

// A game that does nothing: the viewer only shows screens.
struct NullHost : GameHost {
    void startMission(const MissionStart&) override {}
    void loadAttract() override {}
    void setPaused(bool) override {}
    void clearPlayerActions() override {}
    void settingsChanged(const as3d::Settings&) override {}
    void saveProfile(const as3d::Profile&) override {}
    void quit() override {}
};

bool readFile(const std::string& path, std::string& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
        out.append(buf, n);
        if (out.size() > (8u << 20)) break;
    }
    std::fclose(f);
    return true;
}

// Mission names and helicopter unlocks from levels.txt, Settings.xml for the main menu.
FrontendContent loadContent() {
    FrontendContent c;
    as3d::Vfs vfs;
    viewer::mountGameData(vfs);
    as3d::DefDatabase db;
    db.load(vfs);
    // Levels with a `name` are the missions, in table order (levels-txt.md).
    int i = 0;
    for (const as3d::LevelDef& d : db.levels()) {
        if (d.name.empty() || i >= as3d::defaultGameRules().missionCount) continue;
        c.missionNames[i] = d.name;
        c.enableHelic[i] = d.enableHelic;
        i++;
    }
    std::string xml;
    if (readFile(viewer::dataRoot() + "/third_party_local/original/data/Settings.xml", xml)) {
        parseSettingsXml(xml, c);
        removeRereleaseBranding(c);
    }
    return c;
}

int run(int argc, char** argv) {
    std::string name, out, stateArg;
    int w = 800, h = 600;
    bool touch = false, list = false, noTexts = false;
    float px = -50, py = -50;
    for (int i = 0; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        const char* v = nullptr;
        if (a == "--out" && (v = val())) out = v;
        else if (a == "--size" && (v = val())) {
            if (std::sscanf(v, "%dx%d", &w, &h) != 2 || w < 16 || h < 16 || w > 8192 || h > 8192) return 1;
        } else if (a == "--state" && (v = val())) stateArg = v;
        else if (a == "--pointer" && (v = val())) { if (std::sscanf(v, "%f,%f", &px, &py) != 2) return 1; }
        else if (a == "--touch") touch = true;
        else if (a == "--list") list = true;
        else if (a == "--no-texts") noTexts = true;
        else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 1; }
        else name = a;
    }
    if (list) {
        for (Screen s : kAllScreens) std::printf("%s\n", screenName(s));
        std::printf("intro\nloading\nplaying\n");
        return 0;
    }
    Screen screen = Screen::MainMenu;
    const bool special = name == "intro" || name == "loading" || name == "playing";
    if (out.empty() || (!special && !screenFromName(name, screen))) {
        std::fprintf(stderr, "usage: as3d_viewer screen <name> [--touch] [--size WxH] [--state k=v,...] [--pointer x,y] "
                             "[--no-texts] --out f.png\n       as3d_viewer screen --list\n");
        return 1;
    }

    Texts texts;
    std::string textFile;
    if (!noTexts && readFile(viewer::dataRoot() + "/assets_extracted/texts_v170.txt", textFile)) texts.parse(textFile);
    NullHost host;
    as3d::Profile profile;
    FrontendContent content = loadContent();
    content.videoOptions = !touch; // a touch device has no video modes to pick
    Frontend fe(host, profile, content, texts);
    fe.setTouchMode(touch);
    fe.menus().setPointer(px, py);

    // Split --state into keys applied before the screen is built and keys that need it open.
    std::vector<std::pair<std::string, std::string>> before, after;
    for (size_t i = 0; i < stateArg.size();) {
        size_t j = stateArg.find(',', i);
        if (j == std::string::npos) j = stateArg.size();
        std::string kv = stateArg.substr(i, j - i);
        i = j + 1;
        const size_t eq = kv.find('=');
        if (eq == std::string::npos) continue;
        std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
        (k == "mt" || k == "name" || k == "capture" ? after : before).emplace_back(k, v);
    }
    for (auto& [k, v] : before)
        if (!fe.debugSet(k, v)) { std::fprintf(stderr, "unknown state key %s\n", k.c_str()); return 1; }

    bool loading = false;
    if (name == "intro") {
        fe.boot();
        fe.update(3.0f, {}); // a moment into the DivoGames page
    } else if (name == "loading") {
        loading = true;
    } else if (name == "playing") {
        fe.setState(FrontendState::Playing);
    } else {
        const bool inMission = screen == Screen::InGame || screen == Screen::Hint || screen == Screen::GameOver ||
                               screen == Screen::MissionComplete || screen == Screen::GameComplete;
        fe.setState(inMission ? FrontendState::Playing : FrontendState::Attract);
        if (screen != Screen::MainMenu && !inMission) fe.open(Screen::MainMenu);
        fe.open(screen);
    }
    for (auto& [k, v] : after)
        if (!fe.debugSet(k, v)) { std::fprintf(stderr, "unknown state key %s\n", k.c_str()); return 1; }
    fe.update(0.0f, UiInput().move(px, py));

    as3d::Image img;
    const bool ok = viewer::renderUiHeadless(w, h, {0.18f, 0.24f, 0.30f, 1}, [&](Renderer2D& r, const UiAssets& a) {
        if (loading) {
            drawLoadingScreen(r, a, 0.6f, false);
            return;
        }
        fe.drawUnder(r, a);
        fe.drawOver(r, a);
    }, img);
    if (!ok) return 1;
    if (!as3d::writePng(out.c_str(), img)) { std::fprintf(stderr, "error: cannot write %s\n", out.c_str()); return 1; }
    std::printf("wrote %s (%dx%d)\n", out.c_str(), img.width, img.height);
    return 0;
}

} // namespace

AS3D_VIEWER_COMMAND("screen", "render a front-end screen headless (--list, --touch, --state k=v,...)", run);
