#include "game_loop.h"

#include <SDL.h>
#include <GLES3/gl3.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "as3d/gfx.h"
#include "as3d/platform.h"
#include "as3d/ui.h"
#include "as3d/world.h"
#include "audio_bridge.h"
#include "game_view.h"
#include "touch_overlay.h"

namespace as3d_game {

using namespace as3d;

// ---------------------------------------------------------------------------------------
// Helpers shared with the headless path.
// ---------------------------------------------------------------------------------------

InputSource::InputSource(bool bot, const InputScript* script) : bot_(bot) {
    if (script) player_.reset(new InputScriptPlayer(*script));
}

FrameInput InputSource::next(u32 frame, const FrameInput& local) {
    if (bot_) return botInput(frame);
    if (player_) return player_->frame(frame);
    return local;
}

void ConsoleStatus::update(const GameSession& s, int events) {
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

bool writeTextFile(const std::string& path, const std::string& text) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

bool playerScreenCentre(const World& w, int player, float& vx, float& vy) {
    int pi = w.playerEntityIndex(player);
    if (pi < 0) return false;
    const ScreenRect& r = w.entity(pi).rect;
    if (!(r.max[0] > r.min[0]) || !(r.max[1] > r.min[1])) return false;
    vx = 0.5f * (r.min[0] + r.max[0]);
    vy = ui::kVirtualHeight - 0.5f * (r.min[1] + r.max[1]); // window y up -> screen y down
    return vx > -200.0f && vx < 1000.0f && vy > -200.0f && vy < 800.0f;
}

bool pausedByPlayer(const World& w) {
    return w.paused() && !w.hintShowing() && !w.levelComplete() && !w.gameOver();
}

namespace {

double nowSeconds() {
    return static_cast<double>(SDL_GetPerformanceCounter()) / static_cast<double>(SDL_GetPerformanceFrequency());
}

// Frame-time statistics over a 5-second window (AS3D_PERF).
class PerfStats {
public:
    void presented(double interval, double work) {
        ++frames_;
        sum_ += interval;
        max_ = std::max(max_, interval);
        work_ += work;
    }
    void stepped(int n) { steps_ += n; }
    void dropped(int n) { dropped_ += n; }
    void maybeLog(double now, bool enabled) {
        if (start_ < 0) start_ = now;
        if (now - start_ < 5.0) return;
        if (enabled && frames_ > 0) {
            AS3D_INFO("AS3D_PERF avg_ms=%.2f max_ms=%.2f fps=%.1f work_ms=%.2f sim_steps=%d dropped_steps=%d",
                      1000.0 * sum_ / frames_, 1000.0 * max_, frames_ / (now - start_), 1000.0 * work_ / frames_,
                      steps_, dropped_);
        }
        reset(now);
    }
    void reset(double now) {
        start_ = now;
        frames_ = steps_ = dropped_ = 0;
        sum_ = max_ = work_ = 0;
    }

private:
    double start_ = -1;
    int frames_ = 0, steps_ = 0, dropped_ = 0;
    double sum_ = 0, max_ = 0, work_ = 0;
};

class GameWindow {
public:
    explicit GameWindow(const LoopOptions& o) : o_(o), source_(o.bot, o.script), status_(o.quiet) {}
    int run();

private:
    bool initGl(std::string* err);
    void rebuildGl(const char* why);
    void presentLoading(float progress);
    bool loadLevelView();
    void handleEvent(const SDL_Event& e);
    void pauseGame(const char* reason);
    void setBackground(bool bg);
    void updatePauseState();
    void simulate(int steps);
    void draw();
    void saveScreenshot();
    int fbWidth() const { return gl_ ? gl_->width() : o_.width; }
    int fbHeight() const { return gl_ ? gl_->height() : o_.height; }

    const LoopOptions& o_;
    std::unique_ptr<GraphicsContext> gl_;
    GameSession session_;
    std::unique_ptr<GameView> view_;
    std::unique_ptr<ui::Renderer2D> overlay_;
    AudioBridge audio_;
    InputMapper keys_;
    TouchMapper touch_;
    InputSource source_;
    InputRecorder recorder_;
    ConsoleStatus status_;
    PerfStats perf_;
    bool running_ = true;
    bool background_ = false;
    bool redraw_ = true;
    bool screenshot_ = false;
    bool mouseFinger_ = false;
    bool lastPaused_ = false;
    long frame_ = 0;
    long rendered_ = 0;
    int shots_ = 0;
    double lastPresent_ = -1;
};

bool GameWindow::initGl(std::string* err) {
    view_.reset();
    overlay_.reset(new ui::Renderer2D());
    if (!overlay_->init(err)) return false;
    return true;
}

void GameWindow::presentLoading(float progress) {
    if (!overlay_) return;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    overlay_->begin(fbWidth(), fbHeight());
    drawLoadingScreen(*overlay_, progress);
    overlay_->flush();
    gl_->swapBuffers();
}

bool GameWindow::loadLevelView() {
    double t0 = nowSeconds();
    presentLoading(0.6f);
    std::string err;
    bool ok = view_->beginLevel(session_, &err);
    if (!ok) std::fprintf(stderr, "as3d_game: renderer: %s\n", err.c_str());
    if (o_.markers) AS3D_INFO("AS3D_LEVEL_LOADED mission=%d ms=%.0f shadow_maps=%d", session_.mission(), 1000.0 * (nowSeconds() - t0),
              view_->renderer().shadowMapCount());
    lastPresent_ = -1; // the load is not a frame time
    return ok;
}

void GameWindow::rebuildGl(const char* why) {
    // Old objects first: after a context loss their names mean nothing to the new context,
    // and deleting them after new objects exist could hit the new ones.
    double t0 = nowSeconds();
    view_.reset();
    overlay_.reset();
    std::string err;
    if (!initGl(&err)) {
        AS3D_ERROR("FATAL: cannot rebuild the 2D layer after %s: %s", why, err.c_str());
        running_ = false;
        return;
    }
    presentLoading(0.3f);
    view_.reset(new GameView());
    if (!view_->init(session_, &err)) {
        AS3D_ERROR("FATAL: cannot rebuild the renderer after %s: %s", why, err.c_str());
        running_ = false;
        return;
    }
    if (o_.markers) AS3D_INFO("AS3D_GL_REBUILD reason=%s ms=%.0f", why, 1000.0 * (nowSeconds() - t0));
    lastPresent_ = -1;
    redraw_ = true;
}

void GameWindow::pauseGame(const char* reason) {
    World& w = session_.world();
    if (w.paused() || w.hintShowing() || w.levelComplete() || w.gameOver()) return;
    w.setPaused(true);
    if (o_.markers) AS3D_INFO("AS3D_PAUSED reason=%s frame=%ld", reason, frame_);
    lastPaused_ = true;
    audio_.setPaused(true);
    redraw_ = true;
}

void GameWindow::setBackground(bool bg) {
    if (bg == background_) return;
    background_ = bg;
    if (bg) {
        if (o_.markers) AS3D_INFO("AS3D_BACKGROUND frame=%ld", frame_);
        pauseGame("background");
        keys_.releaseAll();
        touch_.releaseAll();
        mouseFinger_ = false;
        audio_.setPaused(true);
    } else {
        if (o_.markers) AS3D_INFO("AS3D_FOREGROUND frame=%ld", frame_);
        // Stays paused until a tap (or P); no catch-up for the time spent away.
        redraw_ = true;
        lastPresent_ = -1;
        if (o_.rebuildOnResume) rebuildGl("resume");
    }
}

void GameWindow::updatePauseState() {
    bool p = pausedByPlayer(session_.world());
    if (p != lastPaused_) {
        lastPaused_ = p;
        if (o_.markers) AS3D_INFO(p ? "AS3D_PAUSED reason=input frame=%ld" : "AS3D_RESUMED frame=%ld", frame_);
    }
    audio_.setPaused(p || background_);
}

void GameWindow::handleEvent(const SDL_Event& e) {
    switch (e.type) {
        case SDL_QUIT:
        case SDL_APP_TERMINATING: running_ = false; break;
        case SDL_APP_WILLENTERBACKGROUND:
        case SDL_APP_DIDENTERBACKGROUND: setBackground(true); break;
        case SDL_APP_DIDENTERFOREGROUND: setBackground(false); break;
        case SDL_RENDER_DEVICE_RESET:
            // SDL on Android created a new EGL context: every GL object is gone.
            gl_->adoptCurrentContext();
            rebuildGl("context_lost");
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            if (e.key.keysym.scancode == SDL_SCANCODE_AC_BACK) {
                // Android back: pauses (never quits in the middle of a mission).
                if (e.type == SDL_KEYDOWN && !e.key.repeat) pauseGame("back");
                break;
            }
            Hotkey h = keys_.keyEvent(e.key.keysym.scancode, e.type == SDL_KEYDOWN, e.key.repeat != 0);
            if (h == Hotkey::Quit && !o_.mobile) running_ = false;
            if (h == Hotkey::Quit && o_.mobile) pauseGame("back");
            if (h == Hotkey::Screenshot) screenshot_ = true;
            break;
        }
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            if (e.button.which == SDL_TOUCH_MOUSEID) break; // synthesized from a finger
            bool down = e.type == SDL_MOUSEBUTTONDOWN;
            if (o_.touch && e.button.button == SDL_BUTTON_LEFT) {
                float x = static_cast<float>(e.button.x), y = static_cast<float>(e.button.y);
                int ww = 1, wh = 1;
                SDL_GetWindowSize(SDL_GetWindowFromID(e.button.windowID), &ww, &wh);
                mouseFinger_ = down;
                touch_.touchEvent(-1, down ? TouchPhase::Down : TouchPhase::Up, x / std::max(ww, 1),
                                  y / std::max(wh, 1));
                if (o_.logTouches)
                    if (o_.markers) AS3D_INFO("AS3D_TOUCH %s id=mouse x=%.3f y=%.3f", down ? "down" : "up", x / std::max(ww, 1),
                              y / std::max(wh, 1));
                break;
            }
            keys_.mouseButtonEvent(e.button.button, down);
            break;
        }
        case SDL_MOUSEMOTION: {
            if (e.motion.which == SDL_TOUCH_MOUSEID || !o_.touch || !mouseFinger_) break;
            int ww = 1, wh = 1;
            SDL_GetWindowSize(SDL_GetWindowFromID(e.motion.windowID), &ww, &wh);
            touch_.touchEvent(-1, TouchPhase::Move, static_cast<float>(e.motion.x) / std::max(ww, 1),
                              static_cast<float>(e.motion.y) / std::max(wh, 1));
            break;
        }
        case SDL_FINGERDOWN:
        case SDL_FINGERUP:
        case SDL_FINGERMOTION: {
            TouchPhase ph = e.type == SDL_FINGERDOWN ? TouchPhase::Down
                            : e.type == SDL_FINGERUP ? TouchPhase::Up
                                                     : TouchPhase::Move;
            touch_.touchEvent(static_cast<long long>(e.tfinger.fingerId), ph, e.tfinger.x, e.tfinger.y);
            if (o_.logTouches && ph != TouchPhase::Move) {
                const TouchLayout& L = touch_.layout();
                const char* on = "field";
                for (int b = 0; b < kTouchButtonCount; ++b) {
                    if (L.hit[b].contains(e.tfinger.x, e.tfinger.y)) on = touchButtonName(static_cast<TouchButton>(b));
                }
                if (o_.markers) AS3D_INFO("AS3D_TOUCH %s id=%lld x=%.3f y=%.3f on=%s", ph == TouchPhase::Down ? "down" : "up",
                          static_cast<long long>(e.tfinger.fingerId), e.tfinger.x, e.tfinger.y, on);
            }
            redraw_ = true;
            break;
        }
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                keys_.releaseAll();
                touch_.releaseAll();
                mouseFinger_ = false;
            }
            if (e.window.event == SDL_WINDOWEVENT_CLOSE) running_ = false;
            redraw_ = true; // exposed, resized, ...
            break;
        default: break;
    }
}

void GameWindow::simulate(int steps) {
    const World& cw = session_.world();
    for (int s = 0; s < steps && running_; ++s) {
        float px = 0, py = 0;
        bool valid = playerScreenCentre(cw, 0, px, py);
        touch_.setPlayerScreen(valid, px, py);
        touch_.setPaused(pausedByPlayer(cw));
        FrameInput local = keys_.takeFrame();
        FrameInput fromTouch = touch_.takeFrame();
        FrameInput in = source_.next(static_cast<u32>(frame_), local);
        in = mergeFrameInput(in, fromTouch);
        recorder_.record(static_cast<u32>(frame_), in);
        int ev = session_.step(in);
        if (ev & GameSession::kLevelStarted) {
            loadLevelView();
            if (!o_.noAudio) audio_.startLevel(session_.musicPath());
        } else {
            view_->step(session_);
        }
        audio_.drain(session_.world());
        status_.update(session_, ev);
        updatePauseState();
        ++frame_;
        if (o_.frameMarkerEvery > 0 && frame_ % o_.frameMarkerEvery == 0) {
            const World& w = session_.world();
            if (o_.markers) AS3D_INFO("AS3D_GAME_FRAME n=%ld mission=%d score=%lld lives=%.0f map_pos=%.1f paused=%d", frame_,
                      session_.mission(), session_.displayScore(0), static_cast<double>(w.player(0).lives),
                      static_cast<double>(w.mapPos()), w.paused() ? 1 : 0);
        }
        if (o_.screenshotEvery > 0 && frame_ % o_.screenshotEvery == 0) screenshot_ = true;
        if (o_.frames >= 0 && frame_ >= o_.frames) running_ = false;
    }
}

void GameWindow::saveScreenshot() {
    // Reads the back buffer before the swap.
    Image img;
    img.width = fbWidth();
    img.height = fbHeight();
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
    std::snprintf(name, sizeof name, "/screenshot_%03d.png", shots_++);
    std::string path = o_.outDir + name;
    if (writePng(path.c_str(), img)) std::printf("wrote %s\n", path.c_str());
}

void GameWindow::draw() {
    int w = fbWidth(), h = fbHeight();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    view_->draw(session_, w, h);
    bool paused = pausedByPlayer(session_.world());
    if (o_.touch || paused) {
        overlay_->begin(w, h);
        if (paused) drawPauseOverlay(*overlay_, o_.touch);
        if (o_.touch) drawTouchControls(*overlay_, touch_);
        overlay_->flush();
    }
}

int GameWindow::run() {
    GraphicsConfig gc;
    gc.headless = false;
    gc.width = o_.width;
    gc.height = o_.height;
    gc.vsync = true;
    gc.fullscreen = o_.fullscreen;
    gc.resizable = o_.resizable;
    gc.title = "AirStrike 3D";
    gl_ = createGraphicsContext(gc);
    if (!gl_) {
        std::fprintf(stderr, "as3d_game: cannot open a window (try --headless)\n");
        AS3D_ERROR("FATAL: cannot create the window and GLES 3.0 context");
        return 3;
    }
    gl_->makeCurrent();
    std::string err;
    double t0 = nowSeconds();
    if (!initGl(&err)) {
        std::fprintf(stderr, "as3d_game: renderer: %s\n", err.c_str());
        AS3D_ERROR("FATAL: 2D layer: %s", err.c_str());
        return 1;
    }
    presentLoading(0.05f);
    if (!session_.init(o_.game, &err)) {
        std::fprintf(stderr, "as3d_game: %s\n", err.c_str());
        AS3D_ERROR("FATAL: %s", err.c_str());
        return 1;
    }
    presentLoading(0.3f);
    view_.reset(new GameView());
    if (!view_->init(session_, &err)) {
        std::fprintf(stderr, "as3d_game: renderer: %s\n", err.c_str());
        AS3D_ERROR("FATAL: renderer: %s", err.c_str());
        return 1;
    }
    if (!o_.noAudio) {
        audio_.init(session_.vfs(), false);
        audio_.startLevel(session_.musicPath());
    }
    touch_.setScreen(fbWidth(), fbHeight(), o_.safeInsets ? o_.safeInsets() : SafeInsets());
    {
        const TouchLayout& L = touch_.layout();
        if (o_.markers) AS3D_INFO("AS3D_GAME_START size=%dx%d mission=%d load_ms=%.0f touch=%d buttons=%s gl=\"%s\"", fbWidth(),
                  fbHeight(), session_.mission(), 1000.0 * (nowSeconds() - t0), o_.touch ? 1 : 0,
                  L.outside ? "outside" : "inside", gl_->description().c_str());
    }
    status_.update(session_, GameSession::kLevelStarted);

    const double dt = session_.world().config().dt;
    const int kMaxCatchUp = 5; // steps per displayed frame; the rest of a long stall is dropped
    double last = nowSeconds();
    double acc = 0.0;
    SafeInsets insets;
    while (running_) {
        screenshot_ = false;
        SDL_Event e;
        while (SDL_PollEvent(&e)) handleEvent(e);
        if (!running_) break;
        if (background_) {
            // SDL blocks inside SDL_PollEvent while the activity is paused; this only runs
            // between the background event and that point.
            SDL_Delay(10);
            last = nowSeconds();
            acc = 0;
            continue;
        }
        if (o_.safeInsets) insets = o_.safeInsets();
        touch_.setScreen(fbWidth(), fbHeight(), insets);

        double now = nowSeconds();
        acc += std::min(0.25, now - last);
        last = now;
        // Wall-clock time only decides how many fixed steps to run; the simulation itself
        // never sees it.
        int steps = 0;
        while (acc >= dt && steps < kMaxCatchUp) {
            acc -= dt;
            ++steps;
        }
        if (acc >= dt) {
            perf_.dropped(static_cast<int>(acc / dt));
            acc = 0; // bounded catch-up: a long stall is not replayed
        }
        double work0 = nowSeconds();
        simulate(steps);
        perf_.stepped(steps);
        if (!running_) break;
        // Without interpolation a frame only changes when the simulation stepped.
        if (steps == 0 && !redraw_ && !screenshot_) {
            SDL_Delay(1);
            perf_.maybeLog(nowSeconds(), o_.perfLog);
            continue;
        }
        redraw_ = false;
        draw();
        if (screenshot_) saveScreenshot();
        double work = nowSeconds() - work0;
        gl_->swapBuffers();
        ++rendered_;
        double t = nowSeconds();
        if (lastPresent_ >= 0) perf_.presented(t - lastPresent_, work);
        lastPresent_ = t;
        perf_.maybeLog(t, o_.perfLog);
    }
    if (!o_.recordPath.empty() && !recorder_.script().save(o_.recordPath))
        std::fprintf(stderr, "as3d_game: cannot write %s\n", o_.recordPath.c_str());
    if (!o_.dumpPath.empty() && !writeTextFile(o_.dumpPath, session_.world().dumpStateJson()))
        std::fprintf(stderr, "as3d_game: cannot write %s\n", o_.dumpPath.c_str());
    std::printf("quit after %ld frames (%ld rendered): mission %d, score %lld\n", frame_, rendered_, session_.mission(),
                session_.displayScore(0));
    if (o_.markers) AS3D_INFO("AS3D_GAME_END frames=%ld rendered=%ld mission=%d", frame_, rendered_, session_.mission());
    audio_.shutdown();
    view_.reset();
    overlay_.reset();
    return 0;
}

} // namespace

int runGameWindow(const LoopOptions& options) {
    std::unique_ptr<GameWindow> w(new GameWindow(options));
    return w->run();
}

} // namespace as3d_game
