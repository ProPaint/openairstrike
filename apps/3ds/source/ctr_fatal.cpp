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
        for (const char* const* p = g.paks; *p; ++p)
            if (!as3d::pakIsOptional(g, *p)) files += std::string(files.empty() ? "" : " ") + *p;
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

std::string takeFatal() {
    if (!g_fatal) return std::string();
    std::string s = g_what + (g_detail.empty() ? "" : ": " + g_detail);
    g_fatal = false;
    g_what.clear();
    g_detail.clear();
    return s;
}

std::string fatalText() {
    std::string s = "OpenAirStrike cannot start\n\n" + g_what + "\n";
    if (!g_detail.empty()) s += g_detail + "\n";
    return s;
}

namespace {
// The console's framebuffer as an upright PPM (run.ini dump_screens=1: the emulator check of the
// screen). The console draws RGB565, single-buffered, into a column-major buffer fbW pixels high.
void dumpConsole(gfxScreen_t screen, const std::string& path) {
    u16 fbW = 0, fbH = 0;
    const u16* fb = reinterpret_cast<const u16*>(gfxGetFramebuffer(screen, GFX_LEFT, &fbW, &fbH));
    if (!fb || gfxGetScreenFormat(screen) != GSP_RGB565_OES) return;
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", static_cast<int>(fbH), static_cast<int>(fbW));
    for (int y = 0; y < fbW; ++y)
        for (int x = 0; x < fbH; ++x) {
            const u16 c = fb[x * fbW + (fbW - 1 - y)];
            const unsigned char rgb[3] = {static_cast<unsigned char>(((c >> 11) & 31) * 255 / 31),
                                          static_cast<unsigned char>(((c >> 5) & 63) * 255 / 63),
                                          static_cast<unsigned char>((c & 31) * 255 / 31)};
            std::fwrite(rgb, 1, 3, f);
        }
    std::fclose(f);
}
} // namespace

void showFatal(bool dump) {
    if (!g_fatal) return;
    if (aptShouldClose()) return; // closed from the HOME menu: no screen to show it on (the log has it)
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
    gfxFlushBuffers();
    if (dump) {
        dumpConsole(GFX_TOP, std::string(kDataDir) + "/fatal_top.ppm");
        dumpConsole(GFX_BOTTOM, std::string(kDataDir) + "/fatal_bottom.ppm");
    }
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
