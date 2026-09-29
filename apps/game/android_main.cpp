// The Android app's native entry point (SDL_main, run by SDLActivity on its own thread).
// Opens on the front end (intro pages, main menu over the attract level, the whole mission
// flow) in touch mode, with touch controls during play, the original pak archives read in
// place from the APK's assets, and the shared game loop (game_loop.h). With --level N or
// --bot it starts straight into a mission as before the menus existed. docs/android.md
// describes the controls, the intent extras and the log markers the smoke test waits for.
//
// Arguments come from GameActivity.getArguments(), built from the launching intent's
// extras: --bot, --level N, --frames N, --difficulty D, --no-audio, --rebuild-on-resume,
// --game KEY (extra `game`: as3d, as2 or gulf; default as3d), --allow-unfinished (extra
// `allow_unfinished`). A game that does not play yet is refused without the latter, so that
// nothing half-working reaches a phone by accident. The paks are read from the APK's assets
// under assets/<key>/ (tools/android_build.sh, AS3D_ANDROID_GAMES), with the names of the game's
// profile.
#include <SDL.h>
#include <jni.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>

#include "as3d/core.h"
#include "as3d/game_data.h"
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
    bool direct = false;
    o.touch = true;
    o.mobile = true;
    o.markers = true;
    o.perfLog = true;
    o.frameMarkerEvery = 600;
    o.logTouches = true;
    o.quiet = true;
    o.safeInsets = currentInsets;
    const as3d::GameProfile* profile = &as3d::gameProfile(as3d::GameId::AirStrike3D);
    bool allowUnfinished = false, badGame = false;
    int level = 0, difficulty = -1; // as given; checked against the game's rules once it is known
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
        } else if (!std::strcmp(s, "--allow-unfinished")) allowUnfinished = true;
        else if (!std::strcmp(s, "--bot")) {
            o.bot = true;
            direct = true;
        }
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
    if (badGame) return 1;
    if (!as3d::gameIsPlayable(*profile) && !allowUnfinished) {
        AS3D_ERROR("game '%s' (%s) is not playable yet; pass the extra allow_unfinished to run it anyway", profile->key,
                   profile->title);
        return 1;
    }
    o.game.game = profile;
    const std::string dir = std::string(profile->key) + "/"; // this game's directory in the APK's assets
    for (const char* const* p = profile->paks; *p; ++p) o.game.paks.push_back(dir + *p);
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
        o.game.extraFiles.push_back({"gfx\\logo2s.tga", dir + "logo2s.tga"});
        o.flow.touch = true;
        o.flow.twoPlayerMode = false;
        o.flow.mouseControlOption = false;
        o.flow.touchMenuButton = false;
        o.flow.settingsXml = dir + "Settings.xml";
        o.flow.textsPath = dir + profile->textsFile;
        o.flow.screenOptionAlways = true; // Options offers Screen (Wide / 4:3) on every device
    }
    AS3D_INFO("AS3D_ARGS bot=%d mission=%d frames=%ld audio=%d rebuild_on_resume=%d menus=%d", o.bot ? 1 : 0,
              o.game.mission, o.frames, o.noAudio ? 0 : 1, o.rebuildOnResume ? 1 : 0, o.frontend ? 1 : 0);

    // Back is a game key (it pauses), not "finish the activity". Touches must not also
    // arrive as synthetic mouse clicks. Landscape only, either way round. Keep the screen on.
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        AS3D_ERROR("FATAL: SDL_Init: %s", SDL_GetError());
        return 1;
    }
    SDL_DisableScreenSaver();
    // The display density sizes the touch buttons in millimetres (issue 140); densityDpi is
    // the stable, bucketed value (xdpi / ydpi are wrong on some devices).
    float ddpi = 0, hdpi = 0, vdpi = 0;
    if (SDL_GetDisplayDPI(0, &ddpi, &hdpi, &vdpi) == 0) o.dpi = ddpi;
    AS3D_INFO("AS3D_DPI ddpi=%.0f hdpi=%.0f vdpi=%.0f", ddpi, hdpi, vdpi);
    // The profile lives in the app's internal files directory (needs SDL's Android glue).
    if (o.frontend) o.flow.profilePath = defaultProfilePath();
    int rc = runGameWindow(o);
    SDL_Quit();
    // SDLActivity finishes the activity when main returns; the process may be reused, and
    // nothing here keeps state between runs except the cutout insets.
    return rc;
}
