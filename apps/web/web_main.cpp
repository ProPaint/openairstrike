// The web version's entry point (docs/web.md, docs/spec/issues/150): the shared game loop
// (apps/game/game_loop.h) in the browser, opening on the front end like the Android app.
//
// The page (site/app.js) prepares everything before it calls main: the game files in /data
// (data/<key>/: the game's paks, and when available Settings.xml, logo2s.tga and its texts file), browser storage
// mounted at /persist (the profile), the canvas sized to the window. It passes its URL
// parameters as arguments:
//   --touch / --auto-touch      touch mode now / at the first finger (hybrid devices)
//   --dpi N                     framebuffer pixels per inch at start (then as3d_web_set_dpi)
//   --level N, --bot            straight into a mission, no menus (tests); --bot --menus: the
//                               menus, and the bot plays the missions started from them
//   --god, --no-audio, --frames N, --difficulty D, --fps
//   --game KEY, --unfinished    the game (?game=as3d|as2|gulf, or the one chosen on the page's
//                               start screen; default as3d; its files in /data/<key>/ under the
//                               names of its profile); a game that does not play yet is refused
//                               unless ?unfinished=1
//   --change-game               the page offers more than one game: the main menu has "Change
//                               game", which saves and hands back to the page's start screen
//                               (docs/spec/issues/163: on the web the page, not the engine, chooses
//                               the game, so that only the chosen game's data is downloaded)
//
// Calls from the page (exported, see site/app.js):
//   as3d_web_set_insets(l, t, r, b)  safe-area insets in framebuffer pixels
//   as3d_web_set_dpi(dpi)            the scale of the canvas changed
//   as3d_web_background(hidden)      the tab was hidden or shown again
//   as3d_web_pause(reason)           1: full screen was left, 2: turned to portrait
//   as3d_web_back()                  the browser's back navigation (the game's Back key)
//   as3d_web_context_lost(), as3d_web_context_restored()   WebGL context loss
//   as3d_unpack(inDir)               the player's archive or installer (docs/spec/issues/170
//                                    section 5): see below
//   as3d_banner_begin(canvas, paks), as3d_banner_frame(i), as3d_banner_end()
//                                    the selector card's banner from the player's paks, before
//                                    a game is chosen (docs/web.md): see below
// Calls to the page: window.as3dPage.{onScreen, onLayout, onTouchMode, onFirstFrame,
// onFinished, onProfileSaved, onChangeGame}.
#include <GLES3/gl3.h>
#include <SDL.h>
#include <emscripten.h>
#include <emscripten/html5.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/game_data.h"
#include "as3d/game_import.h"
#include "as3d/gfx.h"
#include "game_loop.h"
#include "game_view.h"
#include "launcher_screen.h"

namespace {

as3d::SafeInsets g_insets;
float g_dpi = 0;

// The banner render of as3d_banner_*: a WebGL 2 context of its own on the page's hidden canvas,
// current only inside the calls (the game's SDL window context, made later or already there,
// is current again on return), the paks mounted, the mesh, an 800x600 target.
struct BannerRender {
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = 0;
    std::unique_ptr<as3d::Vfs> vfs;
    std::unique_ptr<as3d::DefDatabase> defs;
    std::unique_ptr<as3d_game::BannerMesh> mesh;
    std::unique_ptr<as3d::RenderTarget> target;
    std::vector<unsigned char> frame; // kMarqueeW x kMarqueeH RGBA, the last frame
};
std::unique_ptr<BannerRender> g_banner;

// Makes the banner's context current for a scope and the one before current again after.
struct BannerContextScope {
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE previous;
    explicit BannerContextScope(EMSCRIPTEN_WEBGL_CONTEXT_HANDLE c) : previous(emscripten_webgl_get_current_context()) {
        emscripten_webgl_make_context_current(c);
    }
    ~BannerContextScope() { emscripten_webgl_make_context_current(previous); }
};

void endBanner() {
    if (!g_banner) return;
    if (g_banner->context) {
        {
            // The GL objects are deleted in their own context.
            BannerContextScope scope(g_banner->context);
            g_banner->target.reset();
            g_banner->mesh.reset();
            g_banner->defs.reset();
            g_banner->vfs.reset();
        }
        emscripten_webgl_destroy_context(g_banner->context);
    }
    g_banner.reset();
}

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

// The files the page could not take as loose game files (an archive, an installer, a game file
// it does not know) go through the importer of the Android app (as3d/game_import.h, with the
// archive module): the page writes them under `inDir` in MEMFS (/unpack/in/), calls this, and
// reads back what the JSON names, the files the importer wrote under /unpack/out/<key>/ with the
// names the page stores (pak0.apk, Settings.xml, logo2s.tga, the texts file):
//   {"imported": ["as3d"], "files": {"as3d": ["pak0.apk", ...]}, "notes": ["...", ...]}
// The page removes /unpack afterwards. The returned string lives until the next call.
EMSCRIPTEN_KEEPALIVE const char* as3d_unpack(const char* inDir) {
    namespace fs = std::filesystem;
    static std::string json;
    auto quote = [](const std::string& s) {
        std::string q = "\"";
        for (unsigned char c : s) {
            if (c == '"' || c == '\\') q += '\\', q += static_cast<char>(c);
            else if (c < 0x20) {
                char b[8];
                std::snprintf(b, sizeof b, "\\u%04x", c);
                q += b;
            } else q += static_cast<char>(c);
        }
        return q + "\"";
    };
    const std::string out = "/unpack/out";
    std::error_code ec;
    fs::remove_all(out, ec);
    fs::create_directories(out, ec);
    // The reference entry lists are embedded in the module at /pak_entries (apps/web/CMakeLists.txt):
    // the source-tree path the desktop build reads them from does not exist in the browser.
    static bool namesLoaded = false;
    if (!namesLoaded) {
        std::string err;
        namesLoaded = as3d::loadPakEntryNames("/pak_entries", &err);
        if (!namesLoaded) AS3D_WARN("as3d_unpack: %s", err.c_str());
    }
    const as3d::ImportResult r = as3d::importGameFiles(inDir ? inDir : "/unpack/in", out);
    json = "{\"imported\": [";
    for (size_t i = 0; i < r.imported.size(); ++i) json += (i ? ", " : "") + quote(r.imported[i]);
    json += "], \"files\": {";
    bool firstGame = true;
    for (int g = 0; g < as3d::kGameCount; ++g) {
        const as3d::GameProfile& p = as3d::gameProfile(static_cast<as3d::GameId>(g));
        const fs::path dir = fs::path(out) / p.key;
        if (!fs::is_directory(dir, ec)) continue;
        json += std::string(firstGame ? "" : ", ") + quote(p.key) + ": [";
        firstGame = false;
        bool first = true;
        for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
            if (!e.is_regular_file(ec)) continue;
            json += (first ? "" : ", ") + quote(e.path().filename().string());
            first = false;
        }
        json += "]";
    }
    json += "}, \"notes\": [";
    for (size_t i = 0; i < r.notes.size(); ++i) json += (i ? ", " : "") + quote(r.notes[i]);
    json += "]}";
    return json.c_str();
}

// The game selector's banner card from the player's files (docs/web.md, issue 164): the page
// writes the paks that hold the banner into MEMFS and calls
//   as3d_banner_begin("#canvas-selector", "/banner/pak0.apk\n...")
// which makes a WebGL 2 context on that (hidden) canvas, mounts the paks and loads AirStrike 3D's
// banner object, and returns {"frames": N, "ms": M, "width": W, "height": H} or {"error": "..."};
// then as3d_banner_frame(i) for i in 0..N-1 returns frame i, W x H RGBA (alpha 255) in the
// module's memory, valid until the next call: the selector card's marquee box drawn by
// drawBannerInBox as `as3d_game --headless --selector-marquees` draws it for the bundled site
// (the same loop: N frames over 2 pi seconds, M ms each). as3d_banner_end() frees it all, the
// context too, and closes the paks (the page then removes them). Each call leaves the context
// that was current before it current again.
EMSCRIPTEN_KEEPALIVE const char* as3d_banner_begin(const char* canvas, const char* paks) {
    using namespace as3d_game;
    static std::string json;
    endBanner();
    auto fail = [](const std::string& why) {
        AS3D_WARN("as3d_banner: %s", why.c_str());
        endBanner();
        std::string text = why;
        for (char& c : text)
            if (c == '"' || c == '\\' || static_cast<unsigned char>(c) < 0x20) c = '\'';
        json = "{\"error\": \"" + text + "\"}";
        return json.c_str();
    };
    std::unique_ptr<BannerRender> b(new BannerRender());
    EmscriptenWebGLContextAttributes attr;
    emscripten_webgl_init_context_attributes(&attr);
    attr.majorVersion = 2;
    attr.minorVersion = 0;
    attr.alpha = false;
    attr.depth = false;
    attr.stencil = false;
    attr.antialias = false;
    attr.preserveDrawingBuffer = false;
    attr.enableExtensionsByDefault = true;
    b->context = emscripten_webgl_create_context(canvas ? canvas : "", &attr);
    if (b->context <= 0) return fail("no WebGL 2 context");
    g_banner = std::move(b);
    BannerRender& r = *g_banner;
    BannerContextScope scope(r.context);
    GameOptions files;
    files.game = &as3d::gameProfile(as3d::GameId::AirStrike3D);
    for (const char *p = paks ? paks : "", *q; *p; p = *q ? q + 1 : q) {
        q = std::strchr(p, '\n');
        if (!q) q = p + std::strlen(p);
        if (q > p) files.paks.emplace_back(p, static_cast<size_t>(q - p));
    }
    std::string err;
    r.vfs.reset(new as3d::Vfs());
    if (files.paks.empty() || !mountGameFiles(files, *r.vfs, nullptr, &err)) return fail(err.empty() ? "no paks" : err);
    r.defs.reset(new as3d::DefDatabase());
    r.mesh.reset(new BannerMesh());
    if (!r.defs->load(*r.vfs) || !r.mesh->init(*r.vfs, *r.defs, &err)) return fail("no banner: " + err);
    r.target.reset(new as3d::RenderTarget());
    if (!r.target->create(800, 600, 4)) return fail("no render target");
    r.frame.assign(static_cast<size_t>(kMarqueeW) * kMarqueeH * 4, 255);
    char buf[128];
    std::snprintf(buf, sizeof buf, "{\"frames\": %d, \"ms\": %d, \"width\": %d, \"height\": %d}", kBannerLoopFrames,
                  static_cast<int>(std::lround(1000.0f * kBannerLoopSeconds / static_cast<float>(kBannerLoopFrames))),
                  kMarqueeW, kMarqueeH);
    json = buf;
    return json.c_str();
}

EMSCRIPTEN_KEEPALIVE const unsigned char* as3d_banner_frame(int index) {
    using namespace as3d_game;
    if (!g_banner || !g_banner->target || index < 0 || index >= kBannerLoopFrames) return nullptr;
    BannerRender& r = *g_banner;
    BannerContextScope scope(r.context);
    const as3d::ui::RectF box{(800 - kMarqueeW) / 2.0f, (600 - kMarqueeH) / 2.0f, static_cast<float>(kMarqueeW),
                              static_cast<float>(kMarqueeH)};
    r.target->bind();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawBannerInBox(*r.mesh, box, kBannerLoopSeconds * static_cast<float>(index) / static_cast<float>(kBannerLoopFrames),
                    800, 600, true);
    as3d::Image full;
    if (!r.target->readPixels(full)) return nullptr;
    for (int y = 0; y < kMarqueeH; ++y)
        for (int x = 0; x < kMarqueeW; ++x) {
            const size_t from = (static_cast<size_t>(static_cast<int>(box.y) + y) * 800 + static_cast<size_t>(box.x) + x) * 4;
            const size_t to = (static_cast<size_t>(y) * kMarqueeW + x) * 4;
            for (int c = 0; c < 3; ++c) r.frame[to + c] = full.rgba[from + c];
        }
    return r.frame.data();
}

EMSCRIPTEN_KEEPALIVE void as3d_banner_end() { endBanner(); }

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
    const as3d::GameProfile* profile = &as3d::gameProfile(as3d::GameId::AirStrike3D);
    bool allowUnfinished = false, badGame = false, changeGame = false;
    int levelArg = 0, difficulty = -1; // as given; checked against the game's rules once it is known
    for (int i = 1; i < argc; ++i) {
        const char* s = argv[i];
        const char* v = i + 1 < argc ? argv[i + 1] : nullptr;
        if (!std::strcmp(s, "--game") && v) {
            profile = as3d::findGameProfile(v);
            if (!profile) {
                AS3D_ERROR("unknown game '%s' (as3d, as2, gulf)", v);
                badGame = true;
                profile = &as3d::gameProfile(as3d::GameId::AirStrike3D);
            }
            ++i;
        } else if (!std::strcmp(s, "--unfinished")) allowUnfinished = true;
        else if (!std::strcmp(s, "--change-game")) changeGame = true;
        else if (!std::strcmp(s, "--bot")) {
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
            levelArg = std::atoi(v); // range-checked against the chosen game below
            direct = level = true;
            ++i;
        } else if (!std::strcmp(s, "--difficulty") && v) {
            difficulty = std::atoi(v);
            ++i;
        } else if (!std::strcmp(s, "--frames") && v) {
            long f = std::strtol(v, nullptr, 10);
            if (f > 0) o.frames = f;
            ++i;
        } else {
            AS3D_WARN("unknown argument '%s' ignored", s);
        }
    }
    if (badGame) return 1;
    if (!as3d::gameIsPlayable(*profile) && !allowUnfinished) {
        AS3D_ERROR("game '%s' (%s) is not playable yet; add ?unfinished=1 to run it anyway", profile->key, profile->title);
        return 1;
    }
    o.game.game = profile;
    const std::string dir = std::string("/data/") + profile->key + "/"; // this game's files, put there by the page
    for (const char* const* p = profile->paks; *p; ++p) o.game.paks.push_back(dir + *p);
    // The level and difficulty ranges are the game's own.
    if (levelArg >= 1 && levelArg <= profile->rules.missionCount) o.game.mission = levelArg;
    else if (levelArg != 0) AS3D_WARN("--level %d is outside 1..%d, ignored", levelArg, profile->rules.missionCount);
    if (difficulty >= 0 && difficulty < profile->rules.difficultyCount) o.game.world.difficulty = difficulty;
    else if (difficulty != -1) AS3D_WARN("--difficulty %d is outside 0..%d, ignored", difficulty, profile->rules.difficultyCount - 1);
    if (o.game.world.difficulty >= profile->rules.difficultyCount) o.game.world.difficulty = profile->rules.defaultDifficulty;
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
        o.game.extraFiles.push_back({"gfx\\logo2s.tga", dir + "logo2s.tga"});
        o.flow.touch = o.touch;
        o.flow.twoPlayerMode = false;
        o.flow.mouseControlOption = !o.touch;
        o.flow.touchMenuButton = false;
        o.flow.settingsXml = dir + "Settings.xml";
        o.flow.textsPath = dir + profile->textsFile;
        o.flow.screenOptionAlways = true;
        o.flow.webKeys = true;
        o.flow.deferLoads = true;
        o.flow.profileSaved = [] { callPage("onProfileSaved"); };
        o.flow.profilePath = defaultProfilePath();
        if (changeGame) o.changeGame = [] { callPage("onChangeGame"); };
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
