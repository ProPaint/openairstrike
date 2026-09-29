// as3d_game: plays the original game in a window, or headless for tests.
//
//   as3d_game [--level N] [--difficulty 0..4] [--seed S] [--data ROOT] [--size WxH]
//             [--input-script FILE] [--record FILE] [--bot] [--frames N] [--no-audio]
//             [--screenshot-every K] [--out-dir DIR] [--dump-state FILE]
//             [--touch] [--perf] [--fullscreen] [--paks DIR]
//             [--profile FILE] [--attract 1..4] [--no-logo]
//   as3d_game --headless --frames N [--input-script FILE] [--bot] [--screenshot-every K]
//             [--out-dir DIR] [--dump-state FILE] [--record FILE] [--quiet] ...
//   as3d_game --headless --ui-script FILE [--frames N] [--size WxH] [--touch] ...
//
// Without --level, --bot or --input-script the window opens on the front end: intro pages,
// main menu over the attract level, and the whole mission flow (docs/spec/frontend.md; the
// profile with progress and settings lives in the platform's user data directory, or in
// --profile FILE). --level N (and --bot, --input-script) start straight into a mission and
// move on by themselves, as before the menus existed.
//
// Game data comes from ROOT/assets_extracted, ROOT from --data or $AS3D_DATA_ROOT, or with
// --paks from the original DIR/pak0.apk, pak1.apk, pak2.apk (as the Android app reads them).
// The front end also reads ROOT/third_party_local/original/data/Settings.xml (and the logo
// beside it) and ROOT/assets_extracted/texts_v170.txt (tools/extract_exe_texts.py).
// The windowed loop is shared with the Android app (game_loop.h). --touch draws the touch
// controls, makes the left mouse button one finger and puts the menus in touch mode
// (docs/android.md); --perf logs frame statistics and the AS3D_* markers every 5 s.
// The simulation runs at a fixed 60 Hz step. Headless mode has no window and no audio
// device (the mixer runs on the null device) and renders through EGL into an offscreen
// target only on screenshot frames; `--dump-state` writes the same JSON as as3d_sim.
// `--ui-script` runs the front end headless, fed by the pointer and key events of the script
// (format in ui_script.h), with its `shot` screenshots; UI time is 1/60 s per frame.
//
// Keys without the front end: arrows move, Ctrl / left mouse fire, Shift / right mouse
// missile, Space / middle mouse power-up, 1 / 2 / 3 cycle missiles / weapons / power-ups,
// Return confirms a hint, P pauses, F12 saves a screenshot, Escape quits. With the front end
// the bindings are the settings' (Options, Configure keys), Esc opens the in-game menu.
#include <GLES3/gl3.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "as3d/gfx.h"
#include "as3d/input.h"
#include "as3d/platform.h"
#include "as3d/world.h"
#include "audio_bridge.h"
#include "game_loop.h"
#include "game_flow.h"
#include "game_session.h"
#include "game_view.h"
#include "touch_overlay.h"
#include "ui_script.h"

using namespace as3d;
using namespace as3d_game;

namespace {

struct Args {
    GameOptions game;
    bool headless = false;
    long frames = -1; // -1: until quit (windowed); required headless
    std::string inputScript, recordPath, outDir = ".", dumpPath;
    bool bot = false;
    long screenshotEvery = 0;
    bool quiet = false;
    bool noAudio = false;
    int width = 800, height = 600;
    bool touch = false, perf = false, fullscreen = false;
    bool levelGiven = false;
    std::string uiScript, profilePath;
    int attract = 0;
    bool noLogo = false;
};

int usage() {
    std::fprintf(stderr,
                 "usage: as3d_game [--level N] [--difficulty 0..4] [--seed S] [--data ROOT] [--size WxH]\n"
                 "                 [--input-script FILE] [--record FILE] [--bot] [--god] [--frames N] [--no-audio]\n"
                 "                 [--touch] [--perf] [--fullscreen] [--paks DIR]\n"
                 "                 [--profile FILE] [--attract 1..4] [--no-logo]\n"
                 "       as3d_game --headless --frames N [--input-script FILE] [--bot] [--screenshot-every K]\n"
                 "                 [--out-dir DIR] [--dump-state FILE] [--record FILE] [--quiet]\n"
                 "       as3d_game --headless --ui-script FILE [--frames N] [--touch] ...\n");
    return 2;
}

bool parseArgs(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        const char* v = nullptr;
        auto next = [&]() {
            if (i + 1 >= argc) return false;
            v = argv[++i];
            return true;
        };
        if (s == "--level" && next()) {
            a.game.mission = std::atoi(v);
            a.levelGiven = true;
        }
        else if (s == "--ui-script" && next()) a.uiScript = v;
        else if (s == "--profile" && next()) a.profilePath = v;
        else if (s == "--attract" && next()) a.attract = std::atoi(v);
        else if (s == "--no-logo") a.noLogo = true;
        else if (s == "--difficulty" && next()) a.game.world.difficulty = std::atoi(v);
        else if (s == "--seed" && next()) a.game.world.seed = static_cast<u32>(std::strtoul(v, nullptr, 10));
        else if (s == "--data" && next()) a.game.dataRoot = v;
        else if (s == "--frames" && next()) a.frames = std::strtol(v, nullptr, 10);
        else if (s == "--input-script" && next()) a.inputScript = v;
        else if (s == "--record" && next()) a.recordPath = v;
        else if (s == "--screenshot-every" && next()) a.screenshotEvery = std::strtol(v, nullptr, 10);
        else if (s == "--out-dir" && next()) a.outDir = v;
        else if (s == "--dump-state" && next()) a.dumpPath = v;
        else if (s == "--size" && next()) {
            if (std::sscanf(v, "%dx%d", &a.width, &a.height) != 2) return false;
        } else if (s == "--headless") a.headless = true;
        else if (s == "--bot") a.bot = true;
        else if (s == "--god") a.game.world.godMode = true;
        else if (s == "--quiet") a.quiet = true;
        else if (s == "--no-audio") a.noAudio = true;
        else if (s == "--touch") a.touch = true;
        else if (s == "--perf") a.perf = true;
        else if (s == "--fullscreen") a.fullscreen = true;
        else if (s == "--paks" && next()) {
            std::string dir = v;
            for (int k = 0; k < 3; ++k) a.game.paks.push_back(dir + "/pak" + std::to_string(k) + ".apk");
        }
        else return false;
    }
    if (a.game.mission < 1 || a.game.mission > kMissionCount) return false;
    if (a.width < 16 || a.height < 16 || a.width > 8192 || a.height > 8192) return false;
    if (a.frames > 100'000'000 || a.screenshotEvery < 0) return false;
    if (a.headless && a.frames < 0 && a.uiScript.empty()) return false;
    if (a.attract < 0 || a.attract > 4) return false;
    if (a.game.dataRoot.empty()) {
        const char* env = std::getenv("AS3D_DATA_ROOT");
        a.game.dataRoot = env && *env ? env : ".";
    }
    return true;
}

bool saveFrame(RenderTarget& target, const std::string& path) {
    Image img;
    if (!target.readPixels(img)) return false;
    return writePng(path.c_str(), img);
}

int runHeadless(const Args& a, GameSession& session, const InputScript* script) {
    std::unique_ptr<GraphicsContext> gl;
    std::unique_ptr<GameView> view;
    RenderTarget target;
    std::string err;
    if (a.screenshotEvery > 0) {
        GraphicsConfig gc;
        gc.headless = true;
        gc.width = a.width;
        gc.height = a.height;
        gl = createGraphicsContext(gc);
        if (!gl) {
            std::fprintf(stderr, "as3d_game: no headless GLES 3.0 context\n");
            return 3;
        }
        gl->makeCurrent();
        view.reset(new GameView());
        if (!view->init(session, &err)) {
            std::fprintf(stderr, "as3d_game: renderer: %s\n", err.c_str());
            return 1;
        }
        if (!target.create(a.width, a.height, 4)) {
            std::fprintf(stderr, "as3d_game: cannot create a %dx%d render target\n", a.width, a.height);
            return 1;
        }
    }
    AudioBridge audio;
    if (!a.noAudio) {
        audio.init(session.vfs(), true);
        audio.startLevel(session.musicPath());
    }
    InputSource source(a.bot, script);
    InputRecorder recorder;
    ConsoleStatus status(a.quiet);
    status.update(session, GameSession::kLevelStarted);
    const int samplesPerStep = 44100 / 60;
    for (long f = 0; f < a.frames; ++f) {
        FrameInput in = source.next(static_cast<u32>(f), FrameInput());
        recorder.record(static_cast<u32>(f), in);
        int ev = session.step(in);
        if (ev & GameSession::kLevelStarted) {
            if (view && !view->beginLevel(session, &err)) std::fprintf(stderr, "as3d_game: renderer: %s\n", err.c_str());
            audio.startLevel(session.musicPath());
        } else if (view) {
            view->step(session);
        }
        audio.drain(session.world());
        audio.pump(samplesPerStep);
        status.update(session, ev);
        if (view && (f + 1) % a.screenshotEvery == 0) {
            target.bind();
            view->draw(session, a.width, a.height);
            char name[64];
            std::snprintf(name, sizeof name, "/frame_%06ld.png", f + 1);
            std::string path = a.outDir + name;
            if (!saveFrame(target, path)) {
                std::fprintf(stderr, "as3d_game: cannot write %s\n", path.c_str());
                return 1;
            }
            if (!a.quiet) {
                const WorldRenderStats& rs = view->renderer().lastStats();
                std::printf("wrote %s: models %d shadows %d sprites %d marks %d lights %d emitters %d particles %d\n",
                            path.c_str(), rs.models, rs.shadows, rs.sprites, rs.marks, rs.lights, rs.emitters,
                            rs.particles);
            }
        }
    }
    if (!a.quiet) {
        const World& w = session.world();
        std::printf("done: %ld frames, mission %d, map_pos %.1f, level complete %d, game over %d, score %lld, "
                    "lives %.0f, sounds %llu (missing %llu)\n",
                    a.frames, session.mission(), static_cast<double>(w.mapPos()), w.levelComplete() ? 1 : 0,
                    w.gameOver() ? 1 : 0, session.displayScore(0), static_cast<double>(w.player(0).lives),
                    audio.stats().played + audio.stats().loopsStarted, audio.stats().missing);
        if (view)
            std::printf("shadow maps: %d (%d generated after the level load)\n", view->renderer().shadowMapCount(),
                        view->renderer().lateShadowMaps());
    }
    if (!a.recordPath.empty() && !recorder.script().save(a.recordPath)) {
        std::fprintf(stderr, "as3d_game: cannot write %s\n", a.recordPath.c_str());
        return 1;
    }
    if (!a.dumpPath.empty() && !writeTextFile(a.dumpPath, session.world().dumpStateJson())) {
        std::fprintf(stderr, "as3d_game: cannot write %s\n", a.dumpPath.c_str());
        return 1;
    }
    return 0;
}

// The front end's files on the desktop: Settings.xml and the menu logo from the install's data
// directory (beside the paks), the texts imported from the exe from assets_extracted/.
FlowConfig desktopFlow(Args& a, bool headless) {
    FlowConfig f;
    f.touch = a.touch;
    f.twoPlayerMode = !a.touch;
    f.mouseControlOption = !a.touch;
    // With --touch the touch overlay's pause button opens the in-game menu (one pause control).
    f.touchMenuButton = false;
    const std::string root = a.game.dataRoot;
    std::string install = root + "/third_party_local/original/data";
    if (!a.game.paks.empty()) {
        const std::string& p = a.game.paks.front();
        size_t slash = p.find_last_of('/');
        install = slash == std::string::npos ? "." : p.substr(0, slash);
    }
    f.settingsXml = install + "/Settings.xml";
    f.textsPath = root + "/assets_extracted/texts_v170.txt";
    a.game.extraFiles.push_back({"gfx\\logo2s.tga", install + "/gfx/logo2s.tga"});
    // Headless runs never touch the user's own profile unless asked to.
    f.profilePath = !a.profilePath.empty() ? a.profilePath : headless ? std::string() : defaultProfilePath();
    // Headless runs are reproducible: attract level 1 unless --attract.
    f.attract = a.attract > 0 ? a.attract : headless ? 1 : 0;
    f.showLogo = !a.noLogo;
    return f;
}

int runHeadlessFlow(const Args& a, const FlowConfig& fc, GameSession& session, const InputScript* script,
                    const UiScript& ui) {
    bool wantShots = a.screenshotEvery > 0;
    for (const UiScript::Command& c : ui.commands()) wantShots = wantShots || c.shot;
    std::unique_ptr<GraphicsContext> gl;
    std::unique_ptr<GameView> view;
    RenderTarget target;
    std::string err;
    if (wantShots) {
        GraphicsConfig gc;
        gc.headless = true;
        gc.width = a.width;
        gc.height = a.height;
        gl = createGraphicsContext(gc);
        if (!gl) {
            std::fprintf(stderr, "as3d_game: no headless GLES 3.0 context\n");
            return 3;
        }
        gl->makeCurrent();
        view.reset(new GameView());
        if (!view->init(session, &err, false)) {
            std::fprintf(stderr, "as3d_game: renderer: %s\n", err.c_str());
            return 1;
        }
        if (!target.create(a.width, a.height, 4)) {
            std::fprintf(stderr, "as3d_game: cannot create a %dx%d render target\n", a.width, a.height);
            return 1;
        }
    }
    AudioBridge audio;
    if (!a.noAudio) audio.init(session.vfs(), true);
    GameFlow flow(session, audio);
    if (!flow.init(fc, &err)) {
        std::fprintf(stderr, "as3d_game: %s\n", err.c_str());
        return 1;
    }
    flow.setView(view.get());
    InputMapper keys; // bindings only: nothing presses keys headless
    flow.setInputMapper(&keys);
    ConsoleStatus status(a.quiet);
    flow.levelLoadedHook = [&]() { status.update(session, GameSession::kLevelStarted); };
    TouchMapper touch;
    touch.setScreen(a.width, a.height);
    InputSource source(a.bot, script);
    flow.boot();
    const long frames = a.frames >= 0 ? a.frames : static_cast<long>(ui.lastFrame()) + 1;
    const int samplesPerStep = 44100 / 60;
    auto shoot = [&](const std::string& name) {
        target.bind();
        flow.draw(a.width, a.height);
        if (a.touch && flow.playing() && view->hudAvailable()) {
            view->overlay().begin(a.width, a.height);
            drawTouchControls(view->overlay(), touch);
            view->overlay().flush();
        }
        std::string path = a.outDir + "/" + name + ".png";
        if (!saveFrame(target, path)) {
            std::fprintf(stderr, "as3d_game: cannot write %s\n", path.c_str());
            return false;
        }
        if (!a.quiet) std::printf("wrote %s\n", path.c_str());
        return true;
    };
    // A shot whose name starts with "loading" is taken during a level load of its frame, with
    // the loading screen at its first progress step.
    std::vector<std::string> shots;
    bool shotFailed = false;
    flow.loadingHook = [&](float progress, bool intermission) {
        if (!view || !view->hudAvailable()) return;
        for (size_t i = 0; i < shots.size(); ++i) {
            if (shots[i].compare(0, 7, "loading") != 0) continue;
            target.bind();
            ui::Renderer2D& r = view->overlay();
            r.begin(a.width, a.height);
            ui::drawLoadingScreen(r, view->assets(), progress, intermission);
            r.flush();
            std::string path = a.outDir + "/" + shots[i] + ".png";
            if (!saveFrame(target, path)) shotFailed = true;
            else if (!a.quiet) std::printf("wrote %s\n", path.c_str());
            shots.erase(shots.begin() + static_cast<long>(i));
            return;
        }
    };
    for (long f = 0; f < frames; ++f) {
        ui::UiInput in;
        shots.clear();
        ui.eventsAt(static_cast<u32>(f), in, &shots);
        flow.uiFrame(1.0f / 60.0f, in);
        FrameInput g = source.next(static_cast<u32>(f), FrameInput());
        const int ev = flow.step(g);
        if (session.hasLevel()) audio.drain(session.world());
        audio.pump(samplesPerStep);
        status.update(session, ev & ~GameSession::kLevelStarted);
        if (shotFailed) return 1;
        for (const std::string& s : shots)
            if (view && !shoot(s)) return 1;
        if (view && a.screenshotEvery > 0 && (f + 1) % a.screenshotEvery == 0) {
            char name[64];
            std::snprintf(name, sizeof name, "frame_%06ld", f + 1);
            if (!shoot(name)) return 1;
        }
        if (flow.quitRequested()) break;
    }
    flow.saveNow();
    if (!a.quiet) {
        const ui::Frontend& fe = flow.frontend();
        std::printf("done: %ld frames, screen %s, mission %d, score %lld, loads %d\n", frames,
                    fe.menuOpen() ? ui::screenName(fe.topScreen()) : "none", session.mission(),
                    session.hasLevel() ? session.displayScore(0) : 0LL, flow.levelLoads());
    }
    if (!a.dumpPath.empty() && session.hasLevel() && !writeTextFile(a.dumpPath, session.world().dumpStateJson())) {
        std::fprintf(stderr, "as3d_game: cannot write %s\n", a.dumpPath.c_str());
        return 1;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    Args a;
    if (!parseArgs(argc, argv, a)) return usage();
    InputScript script;
    bool haveScript = false;
    if (!a.inputScript.empty()) {
        std::string err;
        if (!script.load(a.inputScript, &err)) {
            std::fprintf(stderr, "as3d_game: %s: %s\n", a.inputScript.c_str(), err.c_str());
            return 1;
        }
        haveScript = true;
    }
    const bool frontend = a.headless ? !a.uiScript.empty() : !a.levelGiven && !a.bot && !haveScript;
    FlowConfig flowConfig;
    if (frontend) {
        flowConfig = desktopFlow(a, a.headless);
        a.game.startLevel = false;
        a.game.levelFlow = false;
    }
    if (!a.headless) {
        LoopOptions o;
        o.frontend = frontend;
        o.flow = flowConfig;
        o.game = a.game;
        o.frames = a.frames;
        o.script = haveScript ? &script : nullptr;
        o.recordPath = a.recordPath;
        o.dumpPath = a.dumpPath;
        o.outDir = a.outDir;
        o.bot = a.bot;
        o.screenshotEvery = a.screenshotEvery;
        o.quiet = a.quiet;
        o.noAudio = a.noAudio;
        o.width = a.width;
        o.height = a.height;
        o.fullscreen = a.fullscreen;
        o.resizable = a.touch; // try other aspect ratios with the touch layout
        o.touch = a.touch;
        o.perfLog = a.perf;
        o.markers = a.perf;
        o.frameMarkerEvery = a.perf ? 600 : 0;
        o.logTouches = a.perf;
        return runGameWindow(o);
    }
    GameSession session;
    std::string err;
    if (!session.init(a.game, &err)) {
        std::fprintf(stderr, "as3d_game: %s\n", err.c_str());
        return 1;
    }
    if (frontend) {
        UiScript ui;
        if (!ui.load(a.uiScript, &err)) {
            std::fprintf(stderr, "as3d_game: %s: %s\n", a.uiScript.c_str(), err.c_str());
            return 1;
        }
        return runHeadlessFlow(a, flowConfig, session, haveScript ? &script : nullptr, ui);
    }
    return runHeadless(a, session, haveScript ? &script : nullptr);
}
