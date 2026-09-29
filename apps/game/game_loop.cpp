#include "game_loop.h"

#include <SDL.h>
#include <GLES3/gl3.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include "as3d/frontend.h"
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
    void presentLoading(float progress, bool intermission = false);
    void warmUp();
    bool loadLevelView();
    void handleEvent(const SDL_Event& e);
    bool handleFlowEvent(const SDL_Event& e);
    void uiFrame();
    void logScreen();
    float virtX(float fbX) const;
    float virtY(float fbY) const;
    void windowToFb(SDL_Window* win, float& x, float& y) const;
    void pauseGame(const char* reason);
    void setBackground(bool bg);
    void updatePauseState();
    void simulate(int steps);
    void draw();
    void saveScreenshot();
    void updateLayout();
    int screenMode() const;
    bool leftHanded() const;
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
    int layoutW_ = -1, layoutH_ = -1;
    int layoutScreen_ = -1, layoutHand_ = -1;
    TouchFade fade_;
    double lastDraw_ = -1;
    double stepAvg_ = -1, drawAvg_ = -1; // running averages (seconds) for AS3D_HITCH
    // A step or draw over 50 ms and over 3 times its running average: a stall, not a device
    // that is merely slow everywhere.
    bool isHitch(double t, double& avg) const {
        bool hitch = o_.markers && avg >= 0 && t > 0.05 && t > 3.0 * avg;
        avg = avg < 0 ? t : avg * 0.95 + t * 0.05;
        return hitch;
    }
    SafeInsets layoutInsets_;

    // With the front end.
    std::unique_ptr<GameFlow> flow_;
    ui::UiInput uiIn_;
    struct PendingInput {
        bool mouse;  // mouse button (else a scancode)
        int code;
        bool down;
    };
    std::vector<PendingInput> pending_;     // keys and buttons for the mapper, after the UI had them
    std::map<long long, bool> uiFingers_;   // fingers that went down on the UI (not the controls)
    double lastUi_ = -1;
    bool textInput_ = false;
    int loadsSeen_ = 0;
    std::string screen_;                    // last AS3D_SCREEN name
};

int GameWindow::screenMode() const {
    if (flow_) return flow_->screenMode();
    return o_.screenMode == kScreen4x3 ? kScreen4x3 : kScreenWide;
}

bool GameWindow::leftHanded() const { return flow_ ? flow_->profile().settings.leftHanded : o_.leftHanded; }

void GameWindow::updateLayout() {
    SafeInsets in = o_.safeInsets ? o_.safeInsets() : SafeInsets();
    int w = fbWidth(), h = fbHeight();
    const int screen = screenMode(), hand = leftHanded() ? 1 : 0;
    if (flow_) flow_->setScreenSize(w, h);
    if (view_) view_->screenMode = screen;
    if (w == layoutW_ && h == layoutH_ && in.left == layoutInsets_.left && in.top == layoutInsets_.top &&
        in.right == layoutInsets_.right && in.bottom == layoutInsets_.bottom && screen == layoutScreen_ &&
        hand == layoutHand_)
        return;
    layoutW_ = w;
    layoutH_ = h;
    layoutInsets_ = in;
    layoutScreen_ = screen;
    layoutHand_ = hand;
    TouchLayoutOptions lo;
    lo.insets = in;
    lo.dpi = o_.dpi;
    lo.leftHanded = hand != 0;
    lo.screen4x3 = screen == kScreen4x3;
    touch_.setScreen(w, h, lo);
    redraw_ = true;
    if (!o_.markers) return;
    const ui::Mapping m = ui::computeMapping(w, h);
    AS3D_INFO("AS3D_VIEW scale=%.4f x=%.1f y=%.1f", m.scaleX, m.offsetX, m.offsetY);
    // Button centres and radii in framebuffer pixels, for tools/android_smoke.sh to tap.
    const TouchLayout& L = touch_.layout();
    std::string line;
    char buf[96];
    for (int b = 0; b < kTouchButtonCount; ++b) {
        std::snprintf(buf, sizeof buf, " %s=%d,%d,%d", touchButtonName(static_cast<TouchButton>(b)),
                      static_cast<int>(L.circles[b].x), static_cast<int>(L.circles[b].y),
                      static_cast<int>(L.circles[b].r));
        line += buf;
    }
    std::snprintf(buf, sizeof buf, " field=%d,%d,%d,%d", static_cast<int>(L.playField.x * w),
                  static_cast<int>(L.playField.y * h), static_cast<int>((L.playField.x + L.playField.w) * w),
                  static_cast<int>((L.playField.y + L.playField.h) * h));
    line += buf;
    AS3D_INFO("AS3D_LAYOUT size=%dx%d insets=%d,%d,%d,%d buttons=%s screen=%s hand=%s px_per_mm=%.2f%s", w, h, in.left,
              in.top, in.right, in.bottom, L.outside ? "outside" : "inside", screen == kScreen4x3 ? "4x3" : "wide",
              hand ? "left" : "right", L.pixelsPerMm, line.c_str());
}

bool GameWindow::initGl(std::string* err) {
    view_.reset();
    overlay_.reset(new ui::Renderer2D());
    if (!overlay_->init(err)) return false;
    return true;
}

void GameWindow::presentLoading(float progress, bool intermission) {
    if (!overlay_) return;
    // Both buffers of the swap chain, so the bar stays up however long the next step takes.
    for (int i = 0; i < 2; ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        overlay_->begin(fbWidth(), fbHeight());
        // Behind the front end: the original's loading screen (frontend.md 3.16) once its
        // pictures are loaded; before that (and without the front end) a plain bar.
        if (flow_ && view_ && view_->hudAvailable()) ui::drawLoadingScreen(*overlay_, view_->assets(), progress, intermission);
        else drawLoadingScreen(*overlay_, progress);
        overlay_->flush();
        gl_->swapBuffers();
    }
}

// Draws the world once, unseen, behind the loading screen: the renderer loads the models
// and textures of everything in view on first draw, which would otherwise stall the first
// frames of play (seconds on a software GPU).
void GameWindow::warmUp() {
    if (!view_) return;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (session_.hasLevel()) view_->draw(session_, fbWidth(), fbHeight());
    glFinish();
    presentLoading(1.0f, session_.hasLevel() && session_.world().intermission());
}

bool GameWindow::loadLevelView() {
    double t0 = nowSeconds();
    presentLoading(0.6f);
    std::string err;
    bool ok = view_->beginLevel(session_, &err);
    if (!ok) std::fprintf(stderr, "as3d_game: renderer: %s\n", err.c_str());
    else warmUp();
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
    if (!view_->init(session_, &err, session_.hasLevel())) {
        AS3D_ERROR("FATAL: cannot rebuild the renderer after %s: %s", why, err.c_str());
        running_ = false;
        return;
    }
    if (flow_) flow_->setView(view_.get());
    warmUp();
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
        if (flow_) {
            // The in-game menu opens (the game comes back paused); the profile is saved, the
            // process may be killed in the background.
            flow_->onBackground();
            uiFingers_.clear();
            logScreen();
        } else {
            pauseGame("background");
        }
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
    if (flow_) {
        // The front end owns the pause; music and sounds go on under the menus (frontend.md
        // 1.2), only the background silences them.
        audio_.setPaused(background_);
        return;
    }
    bool p = pausedByPlayer(session_.world());
    if (p != lastPaused_) {
        lastPaused_ = p;
        if (o_.markers) AS3D_INFO(p ? "AS3D_PAUSED reason=input frame=%ld" : "AS3D_RESUMED frame=%ld", frame_);
    }
    audio_.setPaused(p || background_);
}

float GameWindow::virtX(float fbX) const { return ui::computeMapping(fbWidth(), fbHeight()).toVirtX(fbX); }
float GameWindow::virtY(float fbY) const { return ui::computeMapping(fbWidth(), fbHeight()).toVirtY(fbY); }

void GameWindow::windowToFb(SDL_Window* win, float& x, float& y) const {
    int ww = 1, wh = 1;
    if (win) SDL_GetWindowSize(win, &ww, &wh);
    x = x * static_cast<float>(fbWidth()) / static_cast<float>(std::max(ww, 1));
    y = y * static_cast<float>(fbHeight()) / static_cast<float>(std::max(wh, 1));
}

// Input with the front end: everything goes to the menus first (frontend.md 1.4); keys and
// buttons reach the mapper after the UI frame, and only while playing (see uiFrame). Fingers
// go to the menus when a menu (or the intro) is up, to the touch controls during play; the
// touch pause button stands for Esc. Returns true when the event was handled.
bool GameWindow::handleFlowEvent(const SDL_Event& e) {
    using ui::UiEvent;
    switch (e.type) {
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            const bool down = e.type == SDL_KEYDOWN;
            const int sc = e.key.keysym.scancode;
            if (sc == SDL_SCANCODE_AC_BACK) {
                if (down && !e.key.repeat) flow_->back();
                logScreen();
                return true;
            }
            if (down && sc == SDL_SCANCODE_F12 && !e.key.repeat) screenshot_ = true;
            const int vk = scancodeToVk(sc);
            if (vk) {
                // Autorepeat reaches the menus only (list scrolling, Backspace in a name).
                if (down && (!e.key.repeat || flow_->frontend().menuOpen())) uiIn_.press(vk);
                if (!down) uiIn_.release(vk);
            }
            if (!e.key.repeat) pending_.push_back({false, sc, down});
            return true;
        }
        case SDL_TEXTINPUT:
            uiIn_.text(e.text.text);
            return true;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            if (e.button.which == SDL_TOUCH_MOUSEID) return true; // synthesized from a finger
            if (o_.touch) return false; // the left button is a finger (handled below)
            const bool down = e.type == SDL_MOUSEBUTTONDOWN;
            float x = static_cast<float>(e.button.x), y = static_cast<float>(e.button.y);
            windowToFb(SDL_GetWindowFromID(e.button.windowID), x, y);
            uiIn_.move(virtX(x), virtY(y));
            if (int vk = mouseButtonToVk(e.button.button)) {
                if (down) uiIn_.press(vk);
                else uiIn_.release(vk);
            }
            pending_.push_back({true, e.button.button, down});
            return true;
        }
        case SDL_MOUSEMOTION: {
            if (e.motion.which == SDL_TOUCH_MOUSEID || o_.touch) return false;
            float x = static_cast<float>(e.motion.x), y = static_cast<float>(e.motion.y);
            windowToFb(SDL_GetWindowFromID(e.motion.windowID), x, y);
            uiIn_.move(virtX(x), virtY(y));
            return true;
        }
        case SDL_MOUSEWHEEL:
            if (e.wheel.y > 0) uiIn_.key(ui::keys::WheelUp);
            if (e.wheel.y < 0) uiIn_.key(ui::keys::WheelDown);
            return true;
        default: break;
    }
    // Fingers (and on desktop with --touch the left mouse button as finger -1).
    long long id = 0;
    TouchPhase ph = TouchPhase::Move;
    float nx = 0, ny = 0;
    if (e.type == SDL_FINGERDOWN || e.type == SDL_FINGERUP || e.type == SDL_FINGERMOTION) {
        id = static_cast<long long>(e.tfinger.fingerId);
        ph = e.type == SDL_FINGERDOWN ? TouchPhase::Down : e.type == SDL_FINGERUP ? TouchPhase::Up : TouchPhase::Move;
        nx = e.tfinger.x;
        ny = e.tfinger.y;
    } else if (o_.touch && (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) &&
               e.button.button == SDL_BUTTON_LEFT) {
        id = -1;
        ph = e.type == SDL_MOUSEBUTTONDOWN ? TouchPhase::Down : TouchPhase::Up;
        int ww = 1, wh = 1;
        SDL_GetWindowSize(SDL_GetWindowFromID(e.button.windowID), &ww, &wh);
        nx = static_cast<float>(e.button.x) / static_cast<float>(std::max(ww, 1));
        ny = static_cast<float>(e.button.y) / static_cast<float>(std::max(wh, 1));
        mouseFinger_ = ph == TouchPhase::Down;
    } else if (o_.touch && e.type == SDL_MOUSEMOTION && mouseFinger_) {
        id = -1;
        int ww = 1, wh = 1;
        SDL_GetWindowSize(SDL_GetWindowFromID(e.motion.windowID), &ww, &wh);
        nx = static_cast<float>(e.motion.x) / static_cast<float>(std::max(ww, 1));
        ny = static_cast<float>(e.motion.y) / static_cast<float>(std::max(wh, 1));
    } else {
        return false;
    }
    const float vx = virtX(nx * static_cast<float>(fbWidth())), vy = virtY(ny * static_cast<float>(fbHeight()));
    const bool onUi = uiFingers_.count(id) != 0;
    const char* on = "field";
    if (ph == TouchPhase::Down) {
        const TouchLayout& L = touch_.layout();
        const int hitB = L.hitTest(nx, ny);
        const bool onPause = hitB == static_cast<int>(TouchButton::Pause);
        if (hitB >= 0) on = touchButtonName(static_cast<TouchButton>(hitB));
        if (!flow_->playing() || onPause) {
            // A menu is up (or the pause button: Esc, the in-game menu).
            uiFingers_[id] = onPause;
            if (onPause) {
                uiIn_.key(ui::keys::Escape);
            } else {
                uiIn_.move(vx, vy);
                uiIn_.press(ui::keys::Mouse1);
                on = "menu";
            }
        } else {
            touch_.touchEvent(id, ph, nx, ny);
        }
    } else if (onUi) {
        const bool pauseFinger = uiFingers_[id];
        if (!pauseFinger) {
            uiIn_.move(vx, vy);
            if (ph == TouchPhase::Up) uiIn_.release(ui::keys::Mouse1);
        }
        if (ph == TouchPhase::Up) uiFingers_.erase(id);
    } else {
        touch_.touchEvent(id, ph, nx, ny);
    }
    if (o_.logTouches && o_.markers && ph != TouchPhase::Move)
        AS3D_INFO("AS3D_TOUCH %s id=%lld x=%.3f y=%.3f on=%s", ph == TouchPhase::Down ? "down" : "up", id, nx, ny,
                  onUi || ph != TouchPhase::Down ? (onUi ? "menu" : "field") : on);
    redraw_ = true;
    return true;
}

void GameWindow::logScreen() {
    if (!flow_ || !o_.markers) return;
    const ui::Frontend& fe = flow_->frontend();
    std::string s;
    if (fe.state() == ui::FrontendState::Intro) s = "intro";
    else if (fe.menuOpen()) s = ui::screenName(fe.topScreen());
    else if (fe.state() == ui::FrontendState::Playing) s = fe.paused() ? "paused" : "playing";
    else s = "none";
    if (s == screen_) return;
    screen_ = s;
    AS3D_INFO("AS3D_SCREEN name=%s frame=%ld mission=%d", s.c_str(), frame_, session_.hasLevel() ? session_.mission() : 0);
}

// One front-end frame with the events gathered since the last one, then the keys and buttons
// the UI did not take go to the mapper (releases always, so nothing sticks).
void GameWindow::uiFrame() {
    const double now = nowSeconds();
    const float dt = lastUi_ < 0 ? 0.0f : static_cast<float>(std::min(0.1, now - lastUi_));
    lastUi_ = now;
    const bool took = flow_->uiFrame(dt, uiIn_);
    uiIn_.events.clear();
    const bool live = flow_->playing() && !took;
    for (const PendingInput& p : pending_) {
        if (p.down && !live) continue;
        if (p.mouse) keys_.mouseButtonEvent(p.code, p.down);
        else keys_.keyEvent(p.code, p.down, false);
    }
    pending_.clear();
    // Desktop: the system text input while a name is typed (touch mode draws its own keys).
    const bool wantText = flow_->frontend().wantsTextInput();
    if (wantText != textInput_) {
        textInput_ = wantText;
        if (wantText) SDL_StartTextInput();
        else SDL_StopTextInput();
    }
    if (flow_->quitRequested()) running_ = false;
    // Menus animate on the menu time: they are redrawn with every simulation step (60 Hz), the
    // intro pages (no world steps) at the same rate by the step count of the loop.
    logScreen();
}

void GameWindow::handleEvent(const SDL_Event& e) {
    if (flow_ && handleFlowEvent(e)) return;
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
                const int hitB = touch_.layout().hitTest(e.tfinger.x, e.tfinger.y);
                const char* on = hitB >= 0 ? touchButtonName(static_cast<TouchButton>(hitB)) : "field";
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
        if (flow_ && !flow_->worldRunning()) break; // intro pages: no level yet
        float px = 0, py = 0;
        bool valid = playerScreenCentre(cw, 0, px, py);
        touch_.setPlayerScreen(valid, px, py);
        touch_.setPaused(flow_ ? false : pausedByPlayer(cw));
        FrameInput local = keys_.takeFrame();
        FrameInput fromTouch = touch_.takeFrame();
        FrameInput in = source_.next(static_cast<u32>(frame_), local);
        in = mergeFrameInput(in, fromTouch);
        recorder_.record(static_cast<u32>(frame_), in);
        double t0 = nowSeconds();
        const int loads = flow_ ? flow_->levelLoads() : 0;
        int ev = flow_ ? flow_->step(in) : session_.step(in);
        double ts = nowSeconds() - t0;
        if (isHitch(ts, stepAvg_)) AS3D_INFO("AS3D_HITCH part=step frame=%ld ms=%.0f", frame_, 1000.0 * ts);
        if (flow_) {
            // A bot or script confirm may close a hint box; the flow loads levels itself.
            if (flow_->levelLoads() != loads) redraw_ = true;
            else view_->step(session_);
            logScreen();
        } else if (ev & GameSession::kLevelStarted) {
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
    // The buttons' fade runs on the wall clock (drawing only).
    const double now = nowSeconds();
    const float dt = lastDraw_ < 0 ? 0.0f : static_cast<float>(std::min(0.25, now - lastDraw_));
    lastDraw_ = now;
    bool held = false;
    for (int b = 0; b < kTouchButtonCount; ++b) held |= touch_.buttonHeld(static_cast<TouchButton>(b));
    fade_.update(dt, held);
    TouchOverlayState ts;
    ui::HudPlayer hud;
    if (session_.hasLevel()) {
        hud = hudStateOf(session_).players[0];
        ts.player = &hud;
    }
    if (view_ && view_->hudAvailable()) ts.assets = &view_->assets();
    ts.alpha = fade_.alpha(touch_.layout().alpha);
    if (flow_) {
        flow_->draw(w, h);
        if (o_.touch && flow_->playing()) {
            overlay_->begin(w, h);
            drawTouchControls(*overlay_, touch_, ts);
            overlay_->flush();
        } else {
            fade_.reset(); // full opacity again when play (re)starts
        }
        return;
    }
    view_->draw(session_, w, h);
    bool paused = pausedByPlayer(session_.world());
    if (o_.touch || paused) {
        overlay_->begin(w, h);
        if (paused) drawPauseOverlay(*overlay_, o_.touch);
        if (o_.touch) drawTouchControls(*overlay_, touch_, ts);
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
    if (!view_->init(session_, &err, session_.hasLevel())) {
        std::fprintf(stderr, "as3d_game: renderer: %s\n", err.c_str());
        AS3D_ERROR("FATAL: renderer: %s", err.c_str());
        return 1;
    }
    if (session_.hasLevel()) warmUp();
    if (!o_.noAudio) {
        audio_.init(session_.vfs(), false);
        audio_.startLevel(session_.musicPath());
    }
    updateLayout();
    if (o_.frontend) {
        flow_.reset(new GameFlow(session_, audio_));
        if (!flow_->init(o_.flow, &err)) {
            std::fprintf(stderr, "as3d_game: %s\n", err.c_str());
            AS3D_ERROR("FATAL: front end: %s", err.c_str());
            return 1;
        }
        flow_->setView(view_.get());
        flow_->setInputMapper(&keys_);
        flow_->loadingHook = [this](float progress, bool intermission) { presentLoading(progress, intermission); };
        flow_->levelLoadedHook = [this]() {
            warmUp();
            status_.update(session_, GameSession::kLevelStarted);
            if (o_.markers)
                AS3D_INFO("AS3D_LEVEL_LOADED mission=%d shadow_maps=%d", session_.mission(), view_->renderer().shadowMapCount());
        };
        // The front end draws its own cursor (frontend.md 2.7) unless UseSystemMouse.
        if (!o_.touch && !flow_->profile().settings.useSystemMouse) SDL_ShowCursor(SDL_DISABLE);
        SDL_StopTextInput();
        flow_->boot();
        logScreen();
    }
    {
        const TouchLayout& L = touch_.layout();
        if (o_.markers) AS3D_INFO("AS3D_GAME_START size=%dx%d mission=%d load_ms=%.0f touch=%d buttons=%s gl=\"%s\"", fbWidth(),
                  fbHeight(), session_.mission(), 1000.0 * (nowSeconds() - t0), o_.touch ? 1 : 0,
                  L.outside ? "outside" : "inside", gl_->description().c_str());
    }
    if (!flow_) status_.update(session_, GameSession::kLevelStarted);

    const double dt = session_.world().config().dt;
    const int kMaxCatchUp = 5; // steps per displayed frame; the rest of a long stall is dropped
    double last = nowSeconds();
    double acc = 0.0;
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
        updateLayout();
        if (flow_) {
            const int loads = flow_->levelLoads();
            uiFrame();
            if (!running_) break;
            if (flow_->levelLoads() != loads) {
                // A level was loaded (seconds, behind the loading screen): not a frame time.
                last = nowSeconds();
                acc = 0.0;
                lastPresent_ = -1;
            }
        }

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
        // Without interpolation a frame only changes when the simulation stepped.
        if (steps == 0 && !redraw_ && !screenshot_) {
            SDL_Delay(1);
            perf_.maybeLog(nowSeconds(), o_.perfLog);
            continue;
        }
        redraw_ = false;
        double td = nowSeconds();
        draw();
        td = nowSeconds() - td;
        if (isHitch(td, drawAvg_)) AS3D_INFO("AS3D_HITCH part=draw frame=%ld ms=%.0f", frame_, 1000.0 * td);
        if (screenshot_) saveScreenshot();
        double work = nowSeconds() - work0;
        gl_->swapBuffers();
        ++rendered_;
        double t = nowSeconds();
        if (lastPresent_ >= 0) perf_.presented(t - lastPresent_, work);
        lastPresent_ = t;
        perf_.maybeLog(t, o_.perfLog);
    }
    if (flow_) flow_->saveNow();
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
