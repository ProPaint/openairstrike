// Web spike, Stage B (docs/web-spike.md): the real game loop (apps/game/game_loop.cpp) in the
// browser, straight into a mission, no menus. The page passes its query string as arguments
// (web/index.html): --level N, --bot, --touch, --no-audio, --frames N, --difficulty D, --god.
// The paks come from the file packager's /data directory. The browser starts audio only after
// the first key press or tap (autoplay rule); the mixer runs silently until then.
#include <SDL.h>

#include <cstdlib>
#include <cstring>

#include "as3d/core.h"
#include "game_loop.h"

int main(int argc, char* argv[]) {
    using namespace as3d_game;
    LoopOptions o;
    o.markers = true;
    o.perfLog = true;
    o.frameMarkerEvery = 600;
    o.quiet = true;
    o.logTouches = true;
    o.game.paks = {"/data/pak0.apk", "/data/pak1.apk", "/data/pak2.apk"};
    for (int i = 1; i < argc; ++i) {
        const char* s = argv[i];
        const char* v = i + 1 < argc ? argv[i + 1] : nullptr;
        if (!std::strcmp(s, "--bot")) o.bot = true;
        else if (!std::strcmp(s, "--no-audio")) o.noAudio = true;
        else if (!std::strcmp(s, "--touch")) o.touch = true;
        else if (!std::strcmp(s, "--god")) o.game.world.godMode = true;
        else if (!std::strcmp(s, "--level") && v) {
            int m = std::atoi(v);
            if (m >= 1 && m <= as3d::kMissionCount) o.game.mission = m;
            ++i;
        } else if (!std::strcmp(s, "--difficulty") && v) {
            int d = std::atoi(v);
            if (d >= 0 && d <= 4) o.game.world.difficulty = d;
            ++i;
        } else if (!std::strcmp(s, "--frames") && v) {
            long f = std::strtol(v, nullptr, 10);
            if (f > 0) o.frames = f;
            ++i;
        } else {
            AS3D_WARN("unknown argument '%s' ignored", s);
        }
    }
    AS3D_INFO("AS3D_ARGS bot=%d mission=%d frames=%ld audio=%d touch=%d", o.bot ? 1 : 0, o.game.mission, o.frames,
              o.noAudio ? 0 : 1, o.touch ? 1 : 0);
    if (o.touch) SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        AS3D_ERROR("FATAL: SDL_Init: %s", SDL_GetError());
        return 1;
    }
    return runGameWindow(o);
}
