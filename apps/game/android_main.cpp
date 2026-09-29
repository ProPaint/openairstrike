// The Android app's native entry point (SDL_main, run by SDLActivity on its own thread).
// Opens on the front end (intro pages, main menu over the attract level, the whole mission
// flow) in touch mode, with touch controls during play, the original pak archives read in
// place from the APK's assets, and the shared game loop (game_loop.h). With --level N or
// --bot it starts straight into a mission as before the menus existed. docs/android.md
// describes the controls, the intent extras and the log markers the smoke test waits for.
//
// Arguments come from GameActivity.getArguments(), built from the launching intent's
// extras: --bot, --menus, --level N, --frames N, --difficulty D, --no-audio,
// --rebuild-on-resume, --game KEY (extra `game`: as3d, as2 or gulf), --allow-unfinished (extra
// `allow_unfinished`). The paks are read from the APK's assets under assets/<key>/
// (tools/android_build.sh, AS3D_ANDROID_GAMES), with the names of the game's profile.
//
// Which game (docs/spec/issues/163): --game names it; else, with the front end, the game
// selector when the APK holds more than one playable game (the last choice preselected from
// files/launcher.bin), the one game when it holds one. A game that does not play yet
// (as3d::gameIsPlayable) is refused, and not listed, without --allow-unfinished, so that
// nothing half-working reaches a phone by accident.
#include <SDL.h>
#include <jni.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>

#include "as3d/core.h"
#include "as3d/game_data.h"
#include "as3d/launcher.h"
#include "as3d/platform.h"
#include "game_loop.h"

namespace {

// Display cutout insets in window pixels, set from the UI thread by GameActivity.
std::atomic<int> g_insets[4] = {{0}, {0}, {0}, {0}};

as3d::SafeInsets currentInsets() {
    as3d::SafeInsets s;
    s.left = g_insets[0].load();
    s.top = g_insets[1].load();
    s.right = g_insets[2].load();
    s.bottom = g_insets[3].load();
    return s;
}

// This game's directory in the APK's assets.
std::string assetDir(const as3d::GameProfile& g) { return std::string(g.key) + "/"; }

// The games whose paks the APK holds.
std::vector<const as3d::GameProfile*> gamesInApk() {
    std::vector<const as3d::GameProfile*> out;
    for (int i = 0; i < as3d::kGameCount; ++i) {
        const as3d::GameProfile& g = as3d::gameProfile(static_cast<as3d::GameId>(i));
        if (as3d::openPlatformStream(assetDir(g) + g.paks[0])) out.push_back(&g);
    }
    return out;
}

// A game's files (paks, logo) and its front end's (Settings.xml, texts) in the assets.
void configureGame(const as3d::GameProfile& g, as3d_game::GameOptions& o, as3d_game::FlowConfig& f) {
    const std::string dir = assetDir(g);
    o.game = &g;
    o.paks.clear();
    for (const char* const* p = g.paks; *p; ++p) o.paks.push_back(dir + *p);
    o.extraFiles.clear();
    o.extraFiles.push_back({"gfx\\logo2s.tga", dir + "logo2s.tga"});
    f.settingsXml = dir + "Settings.xml";
    f.textsPath = dir + g.textsFile;
}

} // namespace

extern "C" JNIEXPORT void JNICALL Java_org_as3dport_game_GameActivity_nativeSetSafeInsets(JNIEnv*, jclass, jint left,
                                                                                           jint top, jint right,
                                                                                           jint bottom) {
    g_insets[0] = left;
    g_insets[1] = top;
    g_insets[2] = right;
    g_insets[3] = bottom;
}

int main(int argc, char* argv[]) {
    using namespace as3d_game;
    LoopOptions o;
    bool direct = false, menus = false;
    o.touch = true;
    o.mobile = true;
    o.markers = true;
    o.perfLog = true;
    o.frameMarkerEvery = 600;
    o.logTouches = true;
    o.quiet = true;
    o.safeInsets = currentInsets;
    std::string forced;
    bool allowUnfinished = false;
    int level = 0, difficulty = -1; // as given; checked against the game's rules once it is known
    for (int i = 1; i < argc; ++i) {
        const char* s = argv[i];
        const char* v = i + 1 < argc ? argv[i + 1] : nullptr;
        if (!std::strcmp(s, "--game") && v) {
            forced = v;
            ++i;
        } else if (!std::strcmp(s, "--allow-unfinished")) allowUnfinished = true;
        else if (!std::strcmp(s, "--bot")) {
            o.bot = true;
            direct = true;
        } else if (!std::strcmp(s, "--menus")) menus = true;
        else if (!std::strcmp(s, "--no-audio")) o.noAudio = true;
        else if (!std::strcmp(s, "--rebuild-on-resume")) o.rebuildOnResume = true;
        else if (!std::strcmp(s, "--level") && v) {
            level = std::atoi(v); // range-checked against the chosen game below
            direct = true;
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
    if (menus && level == 0) direct = false; // --bot --menus: the menus, the pilot plays

    // Back is a game key (it pauses), not "finish the activity". Touches must not also
    // arrive as synthetic mouse clicks. Landscape only, either way round. Keep the screen on.
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        AS3D_ERROR("FATAL: SDL_Init: %s", SDL_GetError());
        return 1;
    }

    // The game: forced, or planned from what the APK holds (the selector with several).
    const std::vector<const as3d::GameProfile*> present = gamesInApk();
    const std::string dataDir = as3d::userDataDir();
    const std::string choicePath = dataDir.empty() ? std::string() : dataDir + "launcher.bin";
    std::string last;
    as3d::readLauncherChoice(choicePath, &last);
    as3d::LaunchPlan plan = as3d::planLaunch(present, forced, last, allowUnfinished);
    if (direct && plan.showSelector) {
        // Straight into a mission (tests): the first game offered, as before the selector.
        plan.showSelector = false;
        plan.startKey = plan.offered.front()->key;
    }
    const as3d::GameProfile* profile = nullptr;
    if (plan.showSelector) {
        profile = plan.offered[static_cast<size_t>(plan.preselected)];
    } else {
        const std::string key = plan.startKey.empty() ? std::string("as3d") : plan.startKey;
        profile = as3d::findGameProfile(key);
        if (!profile) {
            AS3D_ERROR("unknown game '%s' (as3d, as2, gulf)", key.c_str());
            SDL_Quit();
            return 1;
        }
        if (!as3d::gameIsPlayable(*profile) && !allowUnfinished) {
            AS3D_ERROR("game '%s' (%s) is not playable yet; pass the extra allow_unfinished to run it anyway",
                       profile->key, profile->title);
            SDL_Quit();
            return 1;
        }
    }
    AS3D_INFO("AS3D_GAMES present=%d selector=%d game=%s", static_cast<int>(present.size()), plan.showSelector ? 1 : 0,
              profile->key);
    configureGame(*profile, o.game, o.flow);
    // The level and difficulty ranges are the game's own.
    if (level >= 1 && level <= profile->rules.missionCount) o.game.mission = level;
    else if (level != 0) AS3D_WARN("--level %d is outside 1..%d, ignored", level, profile->rules.missionCount);
    if (difficulty >= 0 && difficulty < profile->rules.difficultyCount) o.game.world.difficulty = difficulty;
    else if (difficulty != -1) AS3D_WARN("--difficulty %d is outside 0..%d, ignored", difficulty, profile->rules.difficultyCount - 1);
    if (o.game.world.difficulty >= profile->rules.difficultyCount) o.game.world.difficulty = profile->rules.defaultDifficulty;
    if (!direct) {
        // The front end in touch mode (docs/spec/issues/090, 130): no two-player mode, no
        // mouse control, no video options; the touch overlay's pause button opens the
        // in-game menu. Settings.xml, the menu logo and the texts imported from the exe are
        // copied into the APK's assets by tools/android_build.sh when the data has them.
        o.frontend = true;
        o.game.startLevel = false;
        o.game.levelFlow = false;
        o.flow.touch = true;
        o.flow.twoPlayerMode = false;
        o.flow.mouseControlOption = false;
        o.flow.touchMenuButton = false;
        o.flow.screenOptionAlways = true; // Options offers Screen (Wide / 4:3) on every device
        // The profile lives in the app's internal files directory (needs SDL's Android glue).
        o.flow.profilePath = defaultProfilePath();
        if (plan.showSelector) {
            for (const as3d::GameProfile* g : plan.offered) {
                LauncherEntry e;
                e.game = g;
                FlowConfig unused;
                configureGame(*g, e.files, unused);
                if (!dataDir.empty()) {
                    e.savePath = dataDir + g->key + "/profile.bin";
                    if (g->id == as3d::GameId::AirStrike3D) e.legacySavePath = dataDir + "profile.bin";
                }
                o.launcher.games.push_back(e);
            }
            o.launcher.preselected = plan.preselected;
            o.launcher.atStart = true;
            o.launcher.choicePath = choicePath;
            o.launcher.configure = [](const as3d::GameProfile& g, GameOptions& go, FlowConfig& f) { configureGame(g, go, f); };
        }
    }
    AS3D_INFO("AS3D_ARGS bot=%d mission=%d frames=%ld audio=%d rebuild_on_resume=%d menus=%d", o.bot ? 1 : 0,
              o.game.mission, o.frames, o.noAudio ? 0 : 1, o.rebuildOnResume ? 1 : 0, o.frontend ? 1 : 0);

    SDL_DisableScreenSaver();
    // The display density sizes the touch buttons in millimetres (issue 140); densityDpi is
    // the stable, bucketed value (xdpi / ydpi are wrong on some devices).
    float ddpi = 0, hdpi = 0, vdpi = 0;
    if (SDL_GetDisplayDPI(0, &ddpi, &hdpi, &vdpi) == 0) o.dpi = ddpi;
    AS3D_INFO("AS3D_DPI ddpi=%.0f hdpi=%.0f vdpi=%.0f", ddpi, hdpi, vdpi);
    int rc = runGameWindow(o);
    SDL_Quit();
    // SDLActivity finishes the activity when main returns; the process may be reused, and
    // nothing here keeps state between runs except the cutout insets.
    return rc;
}
