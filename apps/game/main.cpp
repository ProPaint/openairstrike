// as3d_game: plays the original game in a window, or headless for tests.
//
//   as3d_game [--level N] [--difficulty 0..4] [--seed S] [--data ROOT] [--size WxH]
//             [--input-script FILE] [--record FILE] [--bot] [--frames N] [--no-audio]
//             [--screenshot-every K] [--out-dir DIR] [--dump-state FILE]
//             [--touch] [--perf] [--fullscreen] [--paks DIR] [--game as3d|as2|gulf] [--list-games]
//             [--profile FILE] [--attract 1..4] [--no-logo]
//             [--screen wide|4x3] [--left-handed] [--dpi N] [--fps] [--allow-unfinished]
//   as3d_game --headless --frames N [--input-script FILE] [--bot] [--screenshot-every K]
//             [--out-dir DIR] [--dump-state FILE] [--record FILE] [--quiet] ...
//   as3d_game --headless --ui-script FILE [--frames N] [--size WxH] [--touch] ...
//   as3d_game --headless --selector-shot FILE.png [--selector-games as3d,as2] [--size WxH] [--touch]
//
// With the front end, no --game, no $AS3D_GAME and no --paks, the window opens on the game
// selector when the data of more than one playable game is present (docs/spec/issues/163;
// --allow-unfinished lists the games that are not playable yet too); each game's main menu
// then offers "Change game". The last choice is kept in <user data dir>/launcher.bin and only
// preselected. Headless runs and --level never show it: they take the game as before.
// --selector-shot draws the selector once into a PNG (the games present, or those named).
//
// Without --level, --bot or --input-script the window opens on the front end: intro pages,
// main menu over the attract level, and the whole mission flow (docs/spec/frontend.md; the
// profile with progress and settings lives in the platform's user data directory, or in
// --profile FILE). --level N (and --bot, --input-script) start straight into a mission and
// move on by themselves, as before the menus existed.
//
// Game data comes from ROOT/assets_extracted, ROOT from --data or $AS3D_DATA_ROOT, or with
// --paks from the original DIR/pak0.apk, pak1.apk, ... (the game's own list, as the Android
// app reads them). --game as3d|as2|gulf (default $AS3D_GAME, then as3d if present, then the
// first game found; --paks alone identifies the game from the paks) picks the game, whose
// files are under ROOT/assets_extracted_games/<key> and ROOT/third_party_local/games/<key>
// (as3d/game_data.h); --list-games prints the games found.
// The front end also reads the install's data/Settings.xml (and the logo beside it) and the
// game's texts file in the extracted directory (tools/extract_exe_texts.py).
// The windowed loop is shared with the Android app (game_loop.h). --touch draws the touch
// controls, makes the left mouse button one finger and puts the menus in touch mode
// (docs/android.md); --perf logs frame statistics and the AS3D_* markers every 5 s.
// --screen overrides the Screen setting for the session (4x3: the world only in the centred
// 4:3 area), --left-handed mirrors the touch buttons when there is no front end (with it, the
// Options "Controls" row decides), --dpi sizes the touch buttons for that display density
// (default: the window is taken for a phone screen). --fps shows the frame counter whatever the
// Show FPS setting says; headless screenshots show it only with --fps.
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
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "as3d/gfx.h"
#include "as3d/input.h"
#include "as3d/platform.h"
#include "as3d/world.h"
#include "audio_bridge.h"
#include "as3d/game_data.h"
#include "as3d/launcher.h"
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
    int screen = -1;
    bool leftHanded = false;
    float dpi = 0;
    bool fps = false;
    std::string gameKey, paksDir;
    bool listGames = false, errorShown = false;
    bool allowUnfinished = false;
    std::string selectorShot, selectorGames;
    int selectorFocus = -1;
    float selectorTime = 0.25f; // seconds of selector clock before the shot
    std::string marqueeDir;     // --selector-marquees: the web page's marquee frames go here
    SafeInsets insets; // --insets, for the selector shot
    GameData data; // where the chosen game's files are
};

int usage() {
    std::fprintf(stderr,
                 "usage: as3d_game [--level N] [--difficulty 0..4] [--seed S] [--data ROOT] [--size WxH]\n"
                 "                 [--input-script FILE] [--record FILE] [--bot] [--god] [--frames N] [--no-audio]\n"
                 "                 [--touch] [--perf] [--fullscreen] [--paks DIR] [--game as3d|as2|gulf] [--list-games]\n"
                 "                 [--profile FILE] [--attract 1..4] [--no-logo]\n"
                 "                 [--screen wide|4x3] [--left-handed] [--dpi N] [--fps]\n"
                 "       as3d_game --headless --frames N [--input-script FILE] [--bot] [--screenshot-every K]\n"
                 "                 [--out-dir DIR] [--dump-state FILE] [--record FILE] [--quiet]\n"
                 "       as3d_game --headless --ui-script FILE [--frames N] [--touch] ...\n"
                 "       as3d_game --headless --selector-shot FILE.png [--selector-games K1,K2] [--size WxH] [--touch]\n"
                 "                      [--screen wide|4x3] [--insets L,T,R,B] [--selector-focus N] [--selector-time SECONDS]\n");
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
        else if (s == "--screen" && next()) {
            if (!std::strcmp(v, "wide")) a.screen = kScreenWide;
            else if (!std::strcmp(v, "4x3")) a.screen = kScreen4x3;
            else return false;
        }
        else if (s == "--left-handed") a.leftHanded = true;
        else if (s == "--fps") a.fps = true;
        else if (s == "--dpi" && next()) a.dpi = static_cast<float>(std::atof(v));
        else if (s == "--difficulty" && next()) a.game.world.difficulty = std::atoi(v);
        else if (s == "--seed" && next()) a.game.world.seed = static_cast<u32>(std::strtoul(v, nullptr, 10));
        else if (s == "--data" && next()) a.game.dataRoot = v;
        else if (s == "--game" && next()) a.gameKey = v;
        else if (s == "--list-games") a.listGames = true;
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
        else if (s == "--paks" && next()) a.paksDir = v;
        else if (s == "--allow-unfinished") a.allowUnfinished = true;
        else if (s == "--selector-shot" && next()) a.selectorShot = v;
        else if (s == "--selector-games" && next()) a.selectorGames = v;
        else if (s == "--selector-focus" && next()) a.selectorFocus = std::atoi(v);
        else if (s == "--selector-marquees" && next()) {
            a.marqueeDir = v;
            a.selectorShot = "(marquees)";
        } else if (s == "--selector-time" && next()) a.selectorTime = static_cast<float>(std::atof(v));
        else if (s == "--insets" && next()) {
            if (std::sscanf(v, "%d,%d,%d,%d", &a.insets.left, &a.insets.top, &a.insets.right, &a.insets.bottom) != 4)
                return false;
        }
        else return false;
    }
    if (a.game.dataRoot.empty()) {
        const char* env = std::getenv("AS3D_DATA_ROOT");
        a.game.dataRoot = env && *env ? env : ".";
    }
    if (a.listGames || !a.selectorShot.empty()) return true;
    std::string gerr;
    if (!chooseGameData(a.game.dataRoot, a.gameKey, a.paksDir, &a.data, &gerr)) {
        std::fprintf(stderr, "as3d_game: %s\n", gerr.c_str());
        a.errorShown = true;
        return false;
    }
    a.game.game = a.data.game;
    a.game.extractedDir = a.data.extractedDir;
    // Extracted files unless --paks was given (or there are none): what the tests and the
    // regression baseline read.
    if (!a.paksDir.empty() || !a.data.hasExtracted) a.game.paks = a.data.paks;
    if (a.game.mission < 1 || a.game.mission > a.game.rules().missionCount) return false;
    if (a.width < 16 || a.height < 16 || a.width > 8192 || a.height > 8192) return false;
    if (a.frames > 100'000'000 || a.screenshotEvery < 0) return false;
    if (a.headless && a.frames < 0 && a.uiScript.empty()) return false;
    if (a.attract < 0 || a.attract > a.game.rules().attractCount) return false;
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
        view->screenMode = a.screen == kScreen4x3 ? kScreen4x3 : kScreenWide;
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
    const std::string install = a.data.dataDir;
    f.settingsXml = install + "/Settings.xml";
    f.textsPath = a.data.extractedDir + "/" + a.data.game->textsFile;
    a.game.extraFiles.push_back({"gfx\\logo2s.tga", install + "/gfx/logo2s.tga"});
    // Headless runs never touch the user's own profile unless asked to.
    f.profilePath = !a.profilePath.empty() ? a.profilePath : headless ? std::string() : defaultProfilePath();
    // Headless runs are reproducible: attract level 1 unless --attract.
    f.attract = a.attract > 0 ? a.attract : headless ? 1 : 0;
    f.showLogo = !a.noLogo;
    f.screenOverride = a.screen;
    return f;
}

// A game's files and front-end files on the desktop, as desktopFlow sets them for the game
// chosen at start: extracted files unless --paks was given or there are none, the install's
// Settings.xml and menu logo, the texts file beside the extracted files.
void configureDesktopGame(const GameData& d, bool usePaks, GameOptions& g, FlowConfig& f) {
    g.game = d.game;
    g.extractedDir = d.extractedDir;
    g.paks.clear();
    if (usePaks || !d.hasExtracted) g.paks = d.paks;
    g.extraFiles.clear();
    g.extraFiles.push_back({"gfx\\logo2s.tga", d.dataDir + "/gfx/logo2s.tga"});
    f.settingsXml = d.dataDir + "/Settings.xml";
    f.textsPath = d.extractedDir + "/" + d.game->textsFile;
}

// The selector's entries on this machine (docs/spec/issues/163): `games` with their files
// under the data root and their saves in the user data directory.
std::vector<LauncherEntry> desktopLauncherEntries(const Args& a, const std::vector<GameData>& found,
                                                  const std::vector<const GameProfile*>& games) {
    const std::string dir = userDataDir();
    std::vector<LauncherEntry> out;
    for (const GameProfile* g : games)
        for (const GameData& d : found) {
            if (d.game != g) continue;
            LauncherEntry e;
            e.game = g;
            e.files.dataRoot = a.game.dataRoot;
            FlowConfig unused;
            configureDesktopGame(d, false, e.files, unused);
            if (!dir.empty()) {
                e.savePath = dir + g->key + "/profile.bin";
                if (g->id == GameId::AirStrike3D) e.legacySavePath = dir + "profile.bin";
            }
            out.push_back(e);
        }
    return out;
}

// Whether the interactive start shows the selector, and with which games: the playable games
// found under the data root (with --allow-unfinished all of them) when there are more than one
// and none is forced.
void planDesktopLauncher(const Args& a, LoopOptions& o) {
    const char* env = std::getenv("AS3D_GAME");
    if (!a.gameKey.empty() || (env && *env) || !a.paksDir.empty()) return; // a game is forced
    const std::vector<GameData> found = detectGames(a.game.dataRoot);
    std::vector<const GameProfile*> present;
    for (const GameData& d : found) present.push_back(d.game);
    const std::string dir = userDataDir();
    const std::string choice = dir.empty() ? std::string() : dir + "launcher.bin";
    std::string last;
    readLauncherChoice(choice, &last);
    const LaunchPlan plan = planLaunch(present, "", last, a.allowUnfinished);
    if (!plan.showSelector) return;
    o.launcher.games = desktopLauncherEntries(a, found, plan.offered);
    o.launcher.preselected = plan.preselected;
    o.launcher.atStart = true;
    o.launcher.choicePath = choice;
    o.launcher.configure = [found](const GameProfile& g, GameOptions& go, FlowConfig& f) {
        for (const GameData& d : found)
            if (d.game == &g) configureDesktopGame(d, false, go, f);
    };
}

// --selector-marquees DIR: the frames of every listed game's marquee for the web page's cards
// (docs/web.md), one PNG per frame (DIR/<key>_NNN.png, 352x162: a marquee box of a 380 wide
// card) and DIR/index.txt with one line per game, "key frames ms" (the frame time). The loop
// is exact (GameSelector::setLoopFit): the banner's 2 pi seconds in 75 frames, the logos'
// 4 pi in 100.
int writeMarqueeFrames(const Args& a, LauncherScreen& screen, ui::Renderer2D& r, RenderTarget& target,
                       const std::vector<const GameProfile*>& games) {
    constexpr int kW = 352, kH = 162;
    if (a.width != 800 || a.height != 600) {
        std::fprintf(stderr, "as3d_game: --selector-marquees needs --size 800x600\n");
        return 2;
    }
    std::error_code ec;
    std::filesystem::create_directories(a.marqueeDir, ec);
    screen.setLoopFit(true);
    const ui::RectF box{(800 - kW) / 2.0f, (600 - kH) / 2.0f, static_cast<float>(kW), static_cast<float>(kH)};
    std::string index;
    for (size_t g = 0; g < games.size(); ++g) {
        const bool banner = games[g]->id == GameId::AirStrike3D;
        const int frames = banner ? 75 : 100;
        const float loop = banner ? 6.28318531f : 12.5663706f;
        for (int f = 0; f < frames; ++f) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            target.bind();
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            screen.drawMarquee(r, a.width, a.height, static_cast<int>(g), box, loop * static_cast<float>(f) / static_cast<float>(frames));
            Image full, crop;
            if (!target.readPixels(full)) return 1;
            crop.width = kW;
            crop.height = kH;
            crop.rgba.resize(static_cast<size_t>(kW) * kH * 4);
            for (int y = 0; y < kH; ++y)
                for (int x = 0; x < kW; ++x) {
                    const size_t from = (static_cast<size_t>(static_cast<int>(box.y) + y) * 800 + static_cast<size_t>(box.x) + x) * 4;
                    const size_t to = (static_cast<size_t>(y) * kW + x) * 4;
                    for (int c = 0; c < 3; ++c) crop.rgba[to + c] = full.rgba[from + c];
                    crop.rgba[to + 3] = 255;
                }
            char name[64];
            std::snprintf(name, sizeof name, "%s_%03d.png", games[g]->key, f);
            if (!writePng((a.marqueeDir + "/" + name).c_str(), crop)) {
                std::fprintf(stderr, "as3d_game: cannot write %s\n", name);
                return 1;
            }
        }
        char line[96];
        std::snprintf(line, sizeof line, "%s %d %d\n", games[g]->key, frames, static_cast<int>(std::lround(1000.0f * loop / static_cast<float>(frames))));
        index += line;
    }
    std::FILE* fp = std::fopen((a.marqueeDir + "/index.txt").c_str(), "wb");
    if (!fp) return 1;
    std::fwrite(index.data(), 1, index.size(), fp);
    std::fclose(fp);
    return 0;
}

// --selector-shot: the selector drawn once, headless (the games present, or --selector-games).
int runSelectorShot(const Args& a) {
    const std::vector<GameData> found = detectGames(a.game.dataRoot);
    std::vector<const GameProfile*> games;
    if (!a.selectorGames.empty()) {
        const std::string list = a.selectorGames + ",";
        for (size_t p = 0, q; (q = list.find(',', p)) != std::string::npos; p = q + 1) {
            const GameProfile* g = findGameProfile(list.substr(p, q - p));
            if (!g) return usage();
            games.push_back(g);
        }
    } else {
        std::vector<const GameProfile*> present;
        for (const GameData& d : found) present.push_back(d.game);
        games = planLaunch(present, "", "", a.allowUnfinished).offered;
        if (games.empty())
            for (const GameData& d : found) games.push_back(d.game);
    }
    GraphicsConfig gc;
    gc.headless = true;
    gc.width = a.width;
    gc.height = a.height;
    std::unique_ptr<GraphicsContext> gl = createGraphicsContext(gc);
    if (!gl) {
        std::fprintf(stderr, "as3d_game: no headless GLES 3.0 context\n");
        return 3;
    }
    gl->makeCurrent();
    std::string err;
    ui::Renderer2D r;
    RenderTarget target;
    if (!r.init(&err) || !target.create(a.width, a.height, 4)) {
        std::fprintf(stderr, "as3d_game: renderer: %s\n", err.c_str());
        return 1;
    }
    LauncherScreen screen;
    if (!screen.init(desktopLauncherEntries(a, found, games), std::max(0, a.selectorFocus), a.touch, &err)) {
        std::fprintf(stderr, "as3d_game: %s\n", err.c_str());
        return 1;
    }
    screen.setScreen(a.width, a.height, a.insets, a.screen);
    // The clock advances in steps of 1/30 s, as the window's frames do.
    for (float t = 0; t < a.selectorTime - 1e-4f; t += 1.0f / 30.0f)
        screen.update(std::min(1.0f / 30.0f, a.selectorTime - t), ui::UiInput());
    target.bind();
    if (!a.marqueeDir.empty()) return writeMarqueeFrames(a, screen, r, target, games);
    screen.draw(r, a.width, a.height);
    // The window shows colour only: the picture is written opaque (the 2D layer leaves alpha
    // as its blending makes it).
    Image img;
    bool ok = target.readPixels(img);
    for (size_t i = 3; ok && i < img.rgba.size(); i += 4) img.rgba[i] = 255;
    if (!ok || !writePng(a.selectorShot.c_str(), img)) {
        std::fprintf(stderr, "as3d_game: cannot write %s\n", a.selectorShot.c_str());
        return 1;
    }
    if (!a.quiet)
        std::printf("wrote %s (%s; font of %s)\n", a.selectorShot.c_str(), screen.layoutMarker().c_str(),
                    screen.fontGame().c_str());
    return 0;
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
    TouchFade fade;
    FpsCounter fps; // the headless loop's own frame rate (wall clock), shown with --fps
    auto lastFrame = std::chrono::steady_clock::now();
    int layoutKey = -1;
    // The touch layout follows the settings (Screen, Controls), as in the windowed loop.
    auto layout = [&]() {
        const Settings& st = flow.profile().settings;
        applyTouchSpeed(touch.settings(), st.touchSpeed);
        const int key = flow.screenMode() * 2 + (st.leftHanded ? 1 : 0);
        if (key == layoutKey) return;
        layoutKey = key;
        TouchLayoutOptions lo;
        lo.dpi = a.dpi;
        lo.leftHanded = st.leftHanded;
        lo.screen4x3 = flow.screenMode() == kScreen4x3;
        touch.setScreen(a.width, a.height, lo);
    };
    flow.setScreenSize(a.width, a.height);
    InputSource source(a.bot, script);
    flow.boot();
    const long frames = a.frames >= 0 ? a.frames : static_cast<long>(ui.lastFrame()) + 1;
    const int samplesPerStep = 44100 / 60;
    auto shoot = [&](const std::string& name) {
        target.bind();
        flow.draw(a.width, a.height);
        if (a.touch && flow.playing() && view->hudAvailable()) {
            TouchOverlayState ts;
            ui::HudPlayer hud = hudStateOf(session).players[0];
            ts.player = &hud;
            ts.assets = &view->assets();
            ts.alpha = fade.alpha(touch.layout().alpha);
            ts.rules = &session.rules();
            view->overlay().begin(a.width, a.height);
            drawTouchControls(view->overlay(), touch, ts);
            view->overlay().flush();
        }
        if (a.fps && view->hudAvailable()) {
            view->overlay().begin(a.width, a.height);
            drawFpsCounter(view->overlay(), view->assets(), fps, a.touch ? &touch.layout() : nullptr, SafeInsets());
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
        layout();
        {
            const auto now = std::chrono::steady_clock::now();
            fps.frame(std::chrono::duration<double>(now - lastFrame).count());
            lastFrame = now;
        }
        // Fingers on the touch controls (the script's `finger` commands), during play only.
        std::vector<UiScript::Command> fingers;
        ui.fingersAt(static_cast<u32>(f), fingers);
        for (const UiScript::Command& c : fingers) {
            if (!flow.playing() && c.phase == 0) continue;
            float nx = c.x, ny = c.y;
            if (c.button >= 0) {
                const TouchCircle& b = touch.layout().circles[c.button];
                nx = b.x / static_cast<float>(a.width);
                ny = b.y / static_cast<float>(a.height);
            }
            touch.touchEvent(c.id, c.phase == 0 ? TouchPhase::Down : c.phase == 1 ? TouchPhase::Move : TouchPhase::Up, nx, ny);
        }
        FrameInput g = source.next(static_cast<u32>(f), FrameInput());
        if (flow.playing()) {
            float hx = 0, hy = 0;
            const bool valid = session.hasLevel() && playerScreenCentre(session.world(), 0, hx, hy);
            touch.setPlayerScreen(valid, hx, hy);
            g = mergeFrameInput(g, touch.takeFrame());
            bool held = false;
            for (int b = 0; b < kTouchButtonCount; ++b) held |= touch.buttonHeld(static_cast<TouchButton>(b));
            fade.update(1.0f / 60.0f, held);
        } else {
            fade.reset();
        }
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
    if (!parseArgs(argc, argv, a)) return a.errorShown ? 2 : usage();
    if (a.listGames) {
        std::fputs(describeGames(a.game.dataRoot).c_str(), stdout);
        return 0;
    }
    if (!a.selectorShot.empty()) return runSelectorShot(a);
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
        o.dpi = a.dpi;
        o.screenMode = a.screen;
        o.leftHanded = a.leftHanded;
        o.fps = a.fps;
        if (frontend) planDesktopLauncher(a, o);
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
