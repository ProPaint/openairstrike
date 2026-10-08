#include "ctr_install.h"

#include <cstdio>

namespace ctrapp {

namespace {
bool fileExists(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}
} // namespace

std::vector<GameInstall> detectInstalls(const std::string& root) {
    std::vector<GameInstall> out;
    for (int i = 0; i < as3d::kGameCount; ++i) {
        GameInstall g;
        g.game = &as3d::gameProfile(static_cast<as3d::GameId>(i));
        const std::string dir = root + "/" + g.game->key + "/";
        int paksMissing = 0;
        auto check = [&](const char* name, bool pak) {
            if (fileExists(dir + name)) {
                ++g.present;
            } else {
                g.missing.push_back(name);
                if (pak) ++paksMissing;
            }
        };
        for (const char* const* p = g.game->paks; *p; ++p) check(*p, true);
        check("Settings.xml", false);
        if (g.game->textsFile && *g.game->textsFile) check(g.game->textsFile, false);
        g.paksPresent = paksMissing == 0;
        out.push_back(std::move(g));
    }
    return out;
}

std::string missingList(const GameInstall& g) {
    std::string s;
    for (const std::string& m : g.missing) s += (s.empty() ? "" : ", ") + m;
    return s;
}

} // namespace ctrapp
