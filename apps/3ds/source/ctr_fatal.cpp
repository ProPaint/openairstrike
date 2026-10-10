#include "ctr_fatal.h"

#include <3ds.h>

#include <cstdio>

#include "as3d/core.h"
#include "as3d/game_profile.h"
#include "run_config.h"

namespace ctrapp {

namespace {
std::string g_what, g_detail;
bool g_fatal = false;

// The expected SD layout, from the game profiles (the paks, Settings.xml, the texts file).
std::string layoutText() {
    std::string s = std::string(kDataDir) + "/\n";
    s += "  openairstrike.3dsx  (HBL)\n";
    s += "  run.ini             (optional)\n";
    for (int i = 0; i < as3d::kGameCount; ++i) {
        const as3d::GameProfile& g = as3d::gameProfile(static_cast<as3d::GameId>(i));
        std::string files;
        for (const char* const* p = g.paks; *p; ++p) files += std::string(files.empty() ? "" : " ") + *p;
        s += std::string("  ") + g.key + "/\n    " + files + "\n    Settings.xml";
        if (g.textsFile && *g.textsFile) s += std::string(" ") + g.textsFile;
        s += "\n    music/*.ogg  (optional)\n";
    }
    s += "  (one game is enough)\n";
    s += "sdmc:/3ds/dspfirm.cdc  (sound)\n";
    return s;
}
} // namespace

void setFatal(const std::string& what, const std::string& detail) {
    AS3D_ERROR("fatal: %s%s%s", what.c_str(), detail.empty() ? "" : ": ", detail.c_str());
    if (g_fatal) return;
    g_fatal = true;
    g_what = what;
    g_detail = detail;
}

bool hasFatal() { return g_fatal; }

std::string fatalText() {
    std::string s = "OpenAirStrike cannot start\n\n" + g_what + "\n";
    if (!g_detail.empty()) s += g_detail + "\n";
    return s;
}

void showFatal() {
    if (!g_fatal) return;
    gfxInitDefault();
    PrintConsole top, bottom;
    consoleInit(GFX_TOP, &top);
    consoleInit(GFX_BOTTOM, &bottom);
    consoleSelect(&top);
    // The top console is 50 columns: the reason, wrapped by the console itself.
    std::printf("\x1b[31;1m%s\x1b[0m", fatalText().c_str());
    std::printf("\nThe game data comes from your own copies of the\ngames: see INSTALL.txt of the package.\n");
    std::printf("\nlog: %s/log.txt\n\nPress A, B or START to quit.\n", kDataDir);
    consoleSelect(&bottom);
    std::printf("Expected on the SD card:\n\n%s", layoutText().c_str());
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & (KEY_A | KEY_B | KEY_START)) break;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    gfxExit();
}

} // namespace ctrapp
