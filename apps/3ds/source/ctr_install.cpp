#include "ctr_install.h"

#include <dirent.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "as3d/core.h"
#include "as3d/exe_texts.h"
#include "as3d/sha256.h"

namespace ctrapp {

namespace {
bool fileExists(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

bool readWhole(const std::string& path, std::vector<std::uint8_t>* out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out->clear();
    std::uint8_t buf[16384];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
        out->insert(out->end(), buf, buf + n);
        if (out->size() > (16u << 20)) break; // no game executable is this large
    }
    const bool ok = !std::ferror(f) && out->size() <= (16u << 20);
    std::fclose(f);
    return ok;
}

bool writeWhole(const std::string& path, const std::string& text) {
    const std::string tmp = path + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = (std::fclose(f) == 0) && ok;
    if (ok) {
        std::remove(path.c_str());
        ok = std::rename(tmp.c_str(), path.c_str()) == 0;
    }
    if (!ok) std::remove(tmp.c_str());
    return ok;
}

// The *.exe files of `dir` (any case), sorted.
std::vector<std::string> exeFiles(const std::string& dir) {
    std::vector<std::string> out;
    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    while (dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name.size() < 5) continue;
        std::string ext = name.substr(name.size() - 4);
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".exe") out.push_back(name);
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}
} // namespace

int importExeTexts(const std::string& root) {
    int written = 0;
    for (int i = 0; i < as3d::kGameCount; ++i) {
        const as3d::GameProfile& g = as3d::gameProfile(static_cast<as3d::GameId>(i));
        if (!g.textsFile || !*g.textsFile) continue;
        const std::string dir = root + "/" + g.key;
        const std::string target = dir + "/" + g.textsFile;
        if (fileExists(target)) continue;
        const std::vector<std::string> exes = exeFiles(dir);
        if (exes.empty()) continue;
        bool done = false;
        std::vector<std::uint8_t> exe;
        for (const std::string& name : exes) {
            const std::string path = dir + "/" + name;
            if (!readWhole(path, &exe)) {
                AS3D_WARN("install: %s: cannot read %s", g.key, name.c_str());
                continue;
            }
            const std::string sha = as3d::sha256Hex(exe.data(), exe.size());
            const as3d::GameProfile* found = nullptr;
            std::string text, error;
            if (!as3d::extractExeTexts(exe, sha, &found, &text, &error)) {
                AS3D_INFO("install: %s: %s is not a game executable (%s)", g.key, name.c_str(), error.c_str());
                continue;
            }
            if (found != &g) {
                AS3D_WARN("install: %s: %s is %s's executable, not this game's", g.key, name.c_str(),
                          found ? found->key : "another game");
                continue;
            }
            if (!writeWhole(target, text)) {
                AS3D_WARN("install: %s: cannot write %s", g.key, g.textsFile);
                break;
            }
            AS3D_INFO("install: %s: %s made from %s (%u bytes)", g.key, g.textsFile, name.c_str(),
                      static_cast<unsigned>(text.size()));
            ++written;
            done = true;
            break;
        }
        if (!done) AS3D_WARN("install: %s: no executable of this game gave %s", g.key, g.textsFile);
    }
    return written;
}

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
        for (const char* const* p = g.game->paks; *p; ++p)
            if (!as3d::pakIsOptional(*g.game, *p)) check(*p, true); // optional paks (as3d's pak3.apk) mount when there
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
