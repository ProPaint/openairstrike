// The web version's entry point (docs/web.md, docs/spec/issues/150): the shared game loop
// (apps/game/game_loop.h) in the browser, opening on the front end like the Android app.
//
// The page (site/app.js) prepares everything before it calls main: the game files in /data
// (pak0..2.apk, and when available Settings.xml, logo2s.tga, texts_v170.txt), browser storage
// mounted at /persist (the profile), the canvas sized to the window. It passes its URL
// parameters as arguments:
//   --touch / --auto-touch      touch mode now / at the first finger (hybrid devices)
//   --dpi N                     framebuffer pixels per inch at start (then as3d_web_set_dpi)
//   --level N, --bot            straight into a mission, no menus (tests); --bot --menus: the
//                               menus, and the bot plays the missions started from them
//   --god, --no-audio, --frames N, --difficulty D, --fps
//
// Calls from the page (exported, see site/app.js):
//   as3d_web_set_insets(l, t, r, b)  safe-area insets in framebuffer pixels
//   as3d_web_set_dpi(dpi)            the scale of the canvas changed
//   as3d_web_background(hidden)      the tab was hidden or shown again
//   as3d_web_pause(reason)           1: full screen was left, 2: turned to portrait
//   as3d_web_back()                  the browser's back navigation (the game's Back key)
//   as3d_web_context_lost(), as3d_web_context_restored()   WebGL context loss
// Calls to the page: window.as3dPage.{onScreen, onLayout, onTouchMode, onFirstFrame,
// onFinished, onProfileSaved}.
#include <SDL.h>
#include <emscripten.h>
#include <emscripten/html5.h>

#include <cstdlib>
#include <cstring>

#include "as3d/core.h"
#include "game_loop.h"

namespace {

as3d::SafeInsets g_insets;
float g_dpi = 0;

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void as3d_web_set_insets(int left, int top, int right, int bottom) {
    g_insets.left = left;
    g_insets.top = top;
    g_insets.right = right;
    g_insets.bottom = bottom;
}

EMSCRIPTEN_KEEPALIVE void as3d_web_set_dpi(float dpi) { g_dpi = dpi; }

EMSCRIPTEN_KEEPALIVE void as3d_web_background(int hidden) { as3d_game::hostSetBackground(hidden != 0); }

EMSCRIPTEN_KEEPALIVE void as3d_web_pause(int reason) {
    as3d_game::hostRequestPause(reason == 1 ? "fullscreen_left" : reason == 2 ? "portrait" : "page");
}

// The browser's back navigation: the game's Back key, as on Android (in play the in-game
// menu, in a menu the previous screen, on the main menu the exit question).
EMSCRIPTEN_KEEPALIVE void as3d_web_back() {
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_KEYDOWN;
    e.key.state = SDL_PRESSED;
    e.key.keysym.scancode = SDL_SCANCODE_AC_BACK;
    e.key.keysym.sym = SDLK_AC_BACK;
    SDL_PushEvent(&e);
    e.type = SDL_KEYUP;
    e.key.state = SDL_RELEASED;
    SDL_PushEvent(&e);
}

EMSCRIPTEN_KEEPALIVE void as3d_web_context_lost(){ as3d_game::hostGlContextLost(); }

EMSCRIPTEN_KEEPALIVE void as3d_web_context_restored() {
    // Emscripten enables the WebGL extensions once per context; a restored context has lost them.
    EM_ASM({
        var c = GL.currentContext;
        if (c) {
            c.initExtensionsDone = false;
            GL.initExtensions(c);
        }
    });
    as3d_game::hostGlContextRestored();
}

} // extern "C"

namespace {

void callPage(const char* what) {
    EM_ASM({ if (window.as3dPage && window.as3dPage[UTF8ToString($0)]) window.as3dPage[UTF8ToString($0)](); }, what);
}

} // namespace

int main(int argc, char* argv[]) {
    using namespace as3d_game;
    LoopOptions o;
    bool direct = false, menus = false, level = false;
    o.mobile = true;   // Esc pauses (never quits) without the menus
    o.markers = true;  // the AS3D_* lines reach the console (tests read them)
    o.perfLog = true;
    o.frameMarkerEvery = 600;
    o.quiet = true;
    o.logTouches = true;
    o.game.paks = {"/data/pak0.apk", "/data/pak1.apk", "/data/pak2.apk"};
    for (int i = 1; i < argc; ++i) {
        const char* s = argv[i];
        const char* v = i + 1 < argc ? argv[i + 1] : nullptr;
        if (!std::strcmp(s, "--bot")) {
            o.bot = true;
            direct = true;
        } else if (!std::strcmp(s, "--menus")) menus = true;
        else if (!std::strcmp(s, "--no-audio")) o.noAudio = true;
        else if (!std::strcmp(s, "--touch")) o.touch = true;
        else if (!std::strcmp(s, "--auto-touch")) o.autoTouch = true;
        else if (!std::strcmp(s, "--god")) o.game.world.godMode = true;
        else if (!std::strcmp(s, "--fps")) o.fps = true;
        else if (!std::strcmp(s, "--dpi") && v) {
            g_dpi = static_cast<float>(std::atof(v));
            ++i;
        } else if (!std::strcmp(s, "--level") && v) {
            int m = std::atoi(v);
            if (m >= 1 && m <= o.game.rules().missionCount) o.game.mission = m;
            direct = level = true;
            ++i;
        } else if (!std::strcmp(s, "--difficulty") && v) {
            int d = std::atoi(v);
            if (d >= 0 && d < o.game.rules().difficultyCount) o.game.world.difficulty = d;
            ++i;
        } else if (!std::strcmp(s, "--frames") && v) {
            long f = std::strtol(v, nullptr, 10);
            if (f > 0) o.frames = f;
            ++i;
        } else {
            AS3D_WARN("unknown argument '%s' ignored", s);
        }
    }
    if (menus && !level) direct = false;
    o.safeInsets = [] { return g_insets; };
    o.dpiQuery = [] { return g_dpi; };
    o.screenChanged = [](const char* name) {
        EM_ASM({ if (window.as3dPage) window.as3dPage.onScreen(UTF8ToString($0)); }, name);
    };
    o.layoutChanged = [](const as3d::TouchLayout& L, int w, int h) {
        const int p = static_cast<int>(as3d::TouchButton::Pause);
        const as3d::TouchCircle& c = L.circles[p];
        EM_ASM({ if (window.as3dPage) window.as3dPage.onLayout($0, $1, $2, $3, $4, $5); }, c.x, c.y, c.r, L.hit[p].r, w, h);
    };
    o.touchModeChanged = [](bool on) { EM_ASM({ if (window.as3dPage) window.as3dPage.onTouchMode(!!$0); }, on ? 1 : 0); };
    o.firstFrame = [] { callPage("onFirstFrame"); };
    o.finished = [](int) { callPage("onFinished"); };
    if (!direct) {
        // The front end as on Android (docs/spec/issues/090, 130, 150): no two-player mode
        // (one keyboard, and F is the full-screen key), no video options; the touch overlay's
        // pause button opens the in-game menu. The optional files come from /data when the
        // page has them.
        o.frontend = true;
        o.game.startLevel = false;
        o.game.levelFlow = false;
        o.game.extraFiles.push_back({"gfx\\logo2s.tga", "/data/logo2s.tga"});
        o.flow.touch = o.touch;
        o.flow.twoPlayerMode = false;
        o.flow.mouseControlOption = !o.touch;
        o.flow.touchMenuButton = false;
        o.flow.settingsXml = "/data/Settings.xml";
        o.flow.textsPath = "/data/texts_v170.txt";
        o.flow.screenOptionAlways = true;
        o.flow.webKeys = true;
        o.flow.deferLoads = true;
        o.flow.profileSaved = [] { callPage("onProfileSaved"); };
        o.flow.profilePath = defaultProfilePath();
    }
    AS3D_INFO("AS3D_ARGS bot=%d mission=%d frames=%ld audio=%d touch=%d auto_touch=%d menus=%d dpi=%.0f", o.bot ? 1 : 0,
              o.game.mission, o.frames, o.noAudio ? 0 : 1, o.touch ? 1 : 0, o.autoTouch ? 1 : 0, o.frontend ? 1 : 0,
              g_dpi);
    // Fingers are fingers: no synthetic mouse events from them (the mouse still works, as a
    // finger in touch mode). Keys only while the canvas has the focus, so the page's own
    // controls and the browser's shortcuts keep working elsewhere.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        AS3D_ERROR("FATAL: SDL_Init: %s", SDL_GetError());
        return 1;
    }
    // The canvas' CSS size at start; the page sizes its drawing buffer from then on.
    double cw = 800, ch = 600;
    emscripten_get_element_css_size("#canvas", &cw, &ch);
    o.width = cw > 0 ? static_cast<int>(cw) : 800;
    o.height = ch > 0 ? static_cast<int>(ch) : 600;
    return runGameWindow(o);
}
