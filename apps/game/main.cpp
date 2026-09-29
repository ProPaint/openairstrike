// as3d_game: plays the original game's missions in a window, or headless for tests.
//
//   as3d_game [--level N] [--difficulty 0..4] [--seed S] [--data ROOT] [--size WxH]
//             [--input-script FILE] [--record FILE] [--bot] [--frames N] [--no-audio]
//             [--screenshot-every K] [--out-dir DIR] [--dump-state FILE]
//   as3d_game --headless --frames N [--input-script FILE] [--bot] [--screenshot-every K]
//             [--out-dir DIR] [--dump-state FILE] [--record FILE] [--quiet] ...
//
// Game data comes from ROOT/assets_extracted, ROOT from --data or $AS3D_DATA_ROOT.
// The simulation runs at a fixed 60 Hz step. Headless mode has no window and no audio
// device (the mixer runs on the null device) and renders through EGL into an offscreen
// target only on screenshot frames; `--dump-state` writes the same JSON as as3d_sim.
//
// Keys: arrows move, Ctrl / left mouse fire, Shift / right mouse missile, Space / middle
// mouse power-up, 1 / 2 / 3 cycle missiles / weapons / power-ups, Return confirms a hint,
// P pauses, F12 saves a screenshot, Escape quits.
#include <SDL.h>
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
};

int usage() {
    std::fprintf(stderr,
                 "usage: as3d_game [--level N] [--difficulty 0..4] [--seed S] [--data ROOT] [--size WxH]\n"
                 "                 [--input-script FILE] [--record FILE] [--bot] [--frames N] [--no-audio]\n"
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

bool writeFile(const std::string& path, const std::string& text) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

// Where the input of each frame comes from.
class InputSource {
public:
    InputSource(const Args& a, const InputScript* script) : bot_(a.bot) {
        if (script) player_.reset(new InputScriptPlayer(*script));
    }
    bool external() const { return bot_ || player_; }
    FrameInput next(u32 frame, InputMapper* mapper) {
        if (bot_) return botInput(frame);
        if (player_) return player_->frame(frame);
        return mapper ? mapper->takeFrame() : FrameInput();
    }

private:
    bool bot_;
    std::unique_ptr<InputScriptPlayer> player_;
};

// Console status until the HUD exists: one line when score, lives or mission change (at
// most once a second), plus level events and hint texts.
class ConsoleStatus {
public:
    explicit ConsoleStatus(bool quiet) : quiet_(quiet) {}
    void update(const GameSession& s, int events) {
        if (quiet_) return;
        const World& w = s.world();
        if (events & GameSession::kLevelStarted) std::printf("mission %d: %s\n", s.mission(), s.levelName().c_str());
        if (events & GameSession::kHintShown) std::printf("hint: %s\n", w.hintText().c_str());
        if (events & GameSession::kLevelComplete) std::printf("mission %d complete at frame %u\n", s.mission(), s.totalFrames());
        if (events & GameSession::kGameOver) std::printf("game over at frame %u\n", s.totalFrames());
        long long score = s.displayScore(0);
        int lives = static_cast<int>(w.player(0).lives);
        bool changed = score != score_ || lives != lives_ || s.mission() != mission_;
        if (changed && s.totalFrames() >= lastPrint_ + 60) {
            std::printf("frame %u  mission %d  score %lld  lives %d\n", s.totalFrames(), s.mission(), score, lives);
            std::fflush(stdout);
            score_ = score;
            lives_ = lives;
            mission_ = s.mission();
            lastPrint_ = s.totalFrames();
        }
    }

private:
    bool quiet_;
    long long score_ = -1;
    int lives_ = -100;
    int mission_ = -1;
    u32 lastPrint_ = 0;
};

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
    InputSource source(a, script);
    InputRecorder recorder;
    ConsoleStatus status(a.quiet);
    status.update(session, GameSession::kLevelStarted);
    const int samplesPerStep = 44100 / 60;
    for (long f = 0; f < a.frames; ++f) {
        FrameInput in = source.next(static_cast<u32>(f), nullptr);
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
    if (!a.dumpPath.empty() && !writeFile(a.dumpPath, session.world().dumpStateJson())) {
        std::fprintf(stderr, "as3d_game: cannot write %s\n", a.dumpPath.c_str());
        return 1;
    }
    return 0;
}

int runWindowed(const Args& a, GameSession& session, const InputScript* script) {
    GraphicsConfig gc;
    gc.headless = false;
    gc.width = a.width;
    gc.height = a.height;
    gc.vsync = true;
    gc.title = "AirStrike 3D";
    std::unique_ptr<GraphicsContext> gl = createGraphicsContext(gc);
    if (!gl) {
        std::fprintf(stderr, "as3d_game: cannot open a window (try --headless)\n");
        return 3;
    }
    gl->makeCurrent();
    std::string err;
    GameView view;
    if (!view.init(session, &err)) {
        std::fprintf(stderr, "as3d_game: renderer: %s\n", err.c_str());
        return 1;
    }
    AudioBridge audio;
    if (!a.noAudio) {
        audio.init(session.vfs(), false);
        audio.startLevel(session.musicPath());
    }
    InputMapper mapper;
    InputSource source(a, script);
    InputRecorder recorder;
    ConsoleStatus status(a.quiet);
    status.update(session, GameSession::kLevelStarted);
    const double dt = session.world().config().dt;
    const Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 last = SDL_GetPerformanceCounter();
    double acc = 0.0;
    long frame = 0;
    int shots = 0;
    long rendered = 0;
    bool running = true;
    bool redraw = true;
    while (running) {
        bool screenshot = false;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT: running = false; break;
                case SDL_KEYDOWN:
                case SDL_KEYUP: {
                    Hotkey h = mapper.keyEvent(e.key.keysym.scancode, e.type == SDL_KEYDOWN, e.key.repeat != 0);
                    if (h == Hotkey::Quit) running = false;
                    if (h == Hotkey::Screenshot) screenshot = true;
                    break;
                }
                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEBUTTONUP: mapper.mouseButtonEvent(e.button.button, e.type == SDL_MOUSEBUTTONDOWN); break;
                case SDL_WINDOWEVENT:
                    if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) mapper.releaseAll();
                    if (e.window.event == SDL_WINDOWEVENT_CLOSE) running = false;
                    redraw = true; // exposed, resized, ...
                    break;
                default: break;
            }
        }
        Uint64 now = SDL_GetPerformanceCounter();
        acc += std::min(0.25, static_cast<double>(now - last) / static_cast<double>(freq));
        last = now;
        // Wall-clock time only decides how many fixed steps to run; the simulation itself
        // never sees it.
        int steps = 0;
        while (acc >= dt && steps < 8 && running) {
            acc -= dt;
            ++steps;
            FrameInput in = source.next(static_cast<u32>(frame), &mapper);
            if (source.external()) mapper.takeFrame(); // keep the mapper's edges from piling up
            recorder.record(static_cast<u32>(frame), in);
            int ev = session.step(in);
            if (ev & GameSession::kLevelStarted) {
                if (!view.beginLevel(session, &err)) std::fprintf(stderr, "as3d_game: renderer: %s\n", err.c_str());
                audio.startLevel(session.musicPath());
            } else {
                view.step(session);
            }
            audio.drain(session.world());
            status.update(session, ev);
            ++frame;
            if (a.screenshotEvery > 0 && frame % a.screenshotEvery == 0) screenshot = true;
            if (a.frames >= 0 && frame >= a.frames) running = false;
        }
        // Without interpolation a frame only changes when the simulation stepped.
        if (steps == 0 && !redraw && !screenshot) {
            SDL_Delay(1);
            continue;
        }
        redraw = false;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        view.draw(session, gl->width(), gl->height());
        if (screenshot) {
            // Reads the back buffer before the swap.
            Image img;
            img.width = gl->width();
            img.height = gl->height();
            img.hasAlpha = false;
            img.rgba.resize(static_cast<size_t>(img.width) * img.height * 4);
            glReadPixels(0, 0, img.width, img.height, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
            // Flip to top-down rows.
            size_t row = static_cast<size_t>(img.width) * 4;
            std::vector<u8> tmp(row);
            for (int y = 0; y < img.height / 2; ++y) {
                u8* a0 = &img.rgba[static_cast<size_t>(y) * row];
                u8* b0 = &img.rgba[static_cast<size_t>(img.height - 1 - y) * row];
                std::memcpy(tmp.data(), a0, row);
                std::memcpy(a0, b0, row);
                std::memcpy(b0, tmp.data(), row);
            }
            char name[64];
            std::snprintf(name, sizeof name, "/screenshot_%03d.png", shots++);
            std::string path = a.outDir + name;
            if (writePng(path.c_str(), img)) std::printf("wrote %s\n", path.c_str());
        }
        gl->swapBuffers();
        ++rendered;
    }
    if (!a.recordPath.empty() && !recorder.script().save(a.recordPath))
        std::fprintf(stderr, "as3d_game: cannot write %s\n", a.recordPath.c_str());
    if (!a.dumpPath.empty() && !writeFile(a.dumpPath, session.world().dumpStateJson()))
        std::fprintf(stderr, "as3d_game: cannot write %s\n", a.dumpPath.c_str());
    std::printf("quit after %ld frames (%ld rendered): mission %d, score %lld\n", frame, rendered, session.mission(),
                session.displayScore(0));
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
    GameSession session;
    std::string err;
    if (!session.init(a.game, &err)) {
        std::fprintf(stderr, "as3d_game: %s\n", err.c_str());
        return 1;
    }
    return a.headless ? runHeadless(a, session, haveScript ? &script : nullptr)
                      : runWindowed(a, session, haveScript ? &script : nullptr);
}
