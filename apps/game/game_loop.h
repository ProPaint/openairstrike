// The windowed game loop shared by the desktop executable (main.cpp) and the Android app
// (android_main.cpp): window and GL context, loading screen, session, renderer, audio,
// keyboard and touch input, the fixed 60 Hz step with bounded catch-up, the app lifecycle
// (background, foreground, GL context loss), pause overlay and frame statistics.
//
// With `frontend` the game runs behind the menus (game_flow.h): intro pages, attract level,
// main menu and the whole mission flow; keys, mouse and fingers go to the menus first.
// Without it (`--level N`) it starts straight into a mission and moves on by itself.
//
// Log markers (through AS3D_INFO, so they reach logcat on Android; the desktop only prints
// them with the options that enable them):
//   AS3D_GAME_START size=WxH ...      once the game is up (the first level, or the front end)
//   AS3D_SCREEN name=S                 with the front end: the top menu screen changed (the
//                                      names of Frontend::screenName, or intro / playing /
//                                      paused)
//   AS3D_VIEW scale=S x=X y=Y          virtual 800x600 to framebuffer pixels (fb = v*S + X/Y)
//   AS3D_LAYOUT size=WxH ... missile=X,Y,R ...   the touch buttons' centres and radii in
//                                      framebuffer pixels (tools/android_smoke.sh taps them)
//   AS3D_GAME_FRAME n=N mission=M ... every `frameMarkerEvery` simulation frames
//   AS3D_LEVEL_LOADED mission=M ms=T  after every level load
//   AS3D_PERF avg_ms=... max_ms=... sim_steps=...   every 5 s with `perfLog`
//   AS3D_PAUSED reason=... / AS3D_RESUMED, AS3D_BACKGROUND / AS3D_FOREGROUND,
//   AS3D_GL_REBUILD ms=T, AS3D_TOUCH ... (with `logTouches`)
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "as3d/input.h"
#include "game_flow.h"
#include "game_session.h"

namespace as3d_game {

struct LoopOptions {
    GameOptions game;
    long frames = -1;                        // stop after this many simulation frames; -1: never
    const as3d::InputScript* script = nullptr;
    std::string recordPath, dumpPath, outDir = ".";
    bool bot = false;
    long screenshotEvery = 0;
    bool quiet = false;
    bool noAudio = false;
    int width = 800, height = 600;           // window size (ignored on Android)
    bool fullscreen = false;
    bool resizable = false;
    // Touch controls: the on-screen buttons are drawn and SDL finger events drive a
    // TouchMapper. On desktop the left mouse button then acts as one finger (it no longer
    // fires through the key mapper).
    bool touch = false;
    bool mobile = false;                     // Android: back button and Escape pause instead of quitting
    bool markers = false;                    // the AS3D_* log markers above (except AS3D_PERF)
    bool perfLog = false;                    // AS3D_PERF every 5 s
    long frameMarkerEvery = 0;               // AS3D_GAME_FRAME every N frames (0: off)
    bool logTouches = false;                 // AS3D_TOUCH on finger down and up
    bool rebuildOnResume = false;            // test hook: rebuild every GL resource on resume
    // Framebuffer pixels kept free of controls (display cutouts); queried every frame.
    std::function<as3d::SafeInsets()> safeInsets;
    // Display density (dots per inch) for the size of the touch buttons; 0 = unknown (the
    // window is taken for a phone screen, the desktop --touch case).
    float dpi = 0;
    // Settings::screenMode for this session (--screen), -1 = the profile's setting (with the
    // front end) or Wide (without it). Settings::leftHanded without the front end.
    int screenMode = -1;
    bool leftHanded = false;
    int touchSpeed = as3d::kDefaultTouchSpeed; // Settings::touchSpeed without the front end
    bool fps = false;                        // the frame counter regardless of Settings::showFps (--fps)
    // The front end (menus); game.startLevel must then be false.
    bool frontend = false;
    FlowConfig flow;

    // Hooks of the web version (apps/web, docs/spec/issues/150); unset elsewhere.
    std::function<float()> dpiQuery;         // replaces `dpi`, queried with the layout (the page's scale changes)
    bool autoTouch = false;                  // switch to touch mode at the first finger (hybrid devices)
    std::function<void(bool touch)> touchModeChanged;
    std::function<void(const char* screen)> screenChanged; // the names of AS3D_SCREEN
    // The touch layout changed (size, insets, hand, screen mode), framebuffer pixels.
    std::function<void(const as3d::TouchLayout& layout, int width, int height)> layoutChanged;
    std::function<void()> firstFrame;        // after the first frame was presented
    std::function<void(int code)> finished;  // the browser loop ended (the front end's Exit)
};

// Runs until the window is closed, Escape, or `frames`. Returns the process exit code.
int runGameWindow(const LoopOptions& options);

// Lifecycle events a host delivers outside SDL's event queue (the web page, apps/web): they
// act on the window runGameWindow is running and do nothing without one. Call them from the
// thread that runs the loop, between frames.
void hostSetBackground(bool background);   // hidden (pause, silence, save the profile) or shown again
void hostRequestPause(const char* reason); // during play, the in-game menu (full screen left, portrait)
void hostGlContextLost();                  // nothing is drawn until restored; play pauses
void hostGlContextRestored();              // every GL object is gone: rebuild them all

// Where the input of each frame comes from besides the local controls.
class InputSource {
public:
    InputSource(bool bot, const as3d::InputScript* script);
    bool external() const { return bot_ || player_; }
    // The external input of the frame (bot or script), or the local input.
    as3d::FrameInput next(as3d::u32 frame, const as3d::FrameInput& local);

private:
    bool bot_;
    std::unique_ptr<as3d::InputScriptPlayer> player_;
};

// Console status until the HUD exists: one line when score, lives or mission change (at
// most once a second), plus level events and hint texts.
class ConsoleStatus {
public:
    explicit ConsoleStatus(bool quiet) : quiet_(quiet) {}
    void update(const GameSession& s, int events);

private:
    bool quiet_;
    long long score_ = -1;
    int lives_ = -100;
    int mission_ = -1;
    as3d::u32 lastPrint_ = 0;
};

bool writeTextFile(const std::string& path, const std::string& text);

// The world is paused by the player (P, the pause button, back, the app going to the
// background), as opposed to a hint box or a level-end state holding the pause.
bool pausedByPlayer(const as3d::World& w);

} // namespace as3d_game
