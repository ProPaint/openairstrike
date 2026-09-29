// as3d_game: plays the original game's missions in a window, or headless for tests.
//
//   as3d_game [--level N] [--difficulty 0..4] [--seed S] [--data ROOT] [--size WxH]
//             [--input-script FILE] [--record FILE] [--bot] [--frames N] [--no-audio]
//             [--screenshot-every K] [--out-dir DIR] [--dump-state FILE]
//             [--touch] [--perf] [--fullscreen] [--paks DIR]
//   as3d_game --headless --frames N [--input-script FILE] [--bot] [--screenshot-every K]
//             [--out-dir DIR] [--dump-state FILE] [--record FILE] [--quiet] ...
//
// Game data comes from ROOT/assets_extracted, ROOT from --data or $AS3D_DATA_ROOT, or with
// --paks from the original DIR/pak0.apk, pak1.apk, pak2.apk (as the Android app reads them).
// The windowed loop is shared with the Android app (game_loop.h). --touch draws the touch
// controls and makes the left mouse button one finger (docs/android.md); --perf logs frame
// statistics and the AS3D_* markers every 5 s.
// The simulation runs at a fixed 60 Hz step. Headless mode has no window and no audio
// device (the mixer runs on the null device) and renders through EGL into an offscreen
// target only on screenshot frames; `--dump-state` writes the same JSON as as3d_sim.
//
// Keys: arrows move, Ctrl / left mouse fire, Shift / right mouse missile, Space / middle
// mouse power-up, 1 / 2 / 3 cycle missiles / weapons / power-ups, Return confirms a hint,
// P pauses, F12 saves a screenshot, Escape quits.
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
#include "game_session.h"
#include "game_view.h"

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
};

int usage() {
    std::fprintf(stderr,
                 "usage: as3d_game [--level N] [--difficulty 0..4] [--seed S] [--data ROOT] [--size WxH]\n"
                 "                 [--input-script FILE] [--record FILE] [--bot] [--frames N] [--no-audio]\n"
                 "                 [--touch] [--perf] [--fullscreen] [--paks DIR]\n"
                 "       as3d_game --headless --frames N [--input-script FILE] [--bot] [--screenshot-every K]\n"
                 "                 [--out-dir DIR] [--dump-state FILE] [--record FILE] [--quiet]\n");
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
        if (s == "--level" && next()) a.game.mission = std::atoi(v);
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
    if (a.headless && a.frames < 0) return false;
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
    if (!a.headless) {
        LoopOptions o;
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
    return runHeadless(a, session, haveScript ? &script : nullptr);
}
