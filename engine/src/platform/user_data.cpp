// as3d::readPlatformFile and as3d::userDataDir (as3d/platform.h).
#include <SDL.h>
#include <sys/stat.h>

#include <cerrno>
#include <cstdlib>
#include <string>

#include "as3d/core.h"
#include "as3d/platform.h"

namespace as3d {

bool readPlatformFile(const std::string& path, Blob& out) {
    out.clear();
    SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb");
    if (!rw) return false;
    Sint64 size = SDL_RWsize(rw);
    bool ok = size >= 0 && size <= (64 << 20);
    if (ok) {
        out.resize(static_cast<size_t>(size));
        size_t got = 0;
        while (got < out.size()) {
            size_t n = SDL_RWread(rw, out.data() + got, 1, out.size() - got);
            if (n == 0) break;
            got += n;
        }
        ok = got == out.size();
    }
    SDL_RWclose(rw);
    if (!ok) out.clear();
    return ok;
}

// mkdir -p for an absolute or relative path; true if the directory exists afterwards.
bool makeDirectories(const std::string& dir) {
    for (size_t i = 1; i <= dir.size(); ++i) {
        if (i < dir.size() && dir[i] != '/') continue;
        std::string part = dir.substr(0, i);
        if (::mkdir(part.c_str(), 0700) != 0 && errno != EEXIST) return false;
    }
    struct stat st;
    return ::stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string userDataDir() {
#ifdef __ANDROID__
    const char* internal = SDL_AndroidGetInternalStoragePath();
    if (!internal || !*internal) {
        AS3D_ERROR("userDataDir: no internal storage path: %s", SDL_GetError());
        return std::string();
    }
    std::string dir = internal;
#elif defined(__EMSCRIPTEN__)
    // The web page mounts browser storage (IDBFS) here before main() and syncs it after
    // every profile save (apps/web).
    std::string dir = "/persist";
#else
    std::string dir;
    const char* over = std::getenv("AS3D_USER_DATA_DIR"); // tests and portable installs
    const char* xdg = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    if (over && *over) dir = over;
    else if (xdg && *xdg == '/') dir = std::string(xdg) + "/airstrike3d";
    else if (home && *home) dir = std::string(home) + "/.local/share/airstrike3d";
    else dir = ".";
#endif
    if (!makeDirectories(dir)) {
        AS3D_ERROR("userDataDir: cannot create '%s'", dir.c_str());
        return std::string();
    }
    if (dir.back() != '/') dir += '/';
    return dir;
}

std::string gameDataDir(const char* gameKey) {
    std::string dir = userDataDir();
    if (dir.empty() || !gameKey || !*gameKey) return std::string();
    dir += gameKey;
    if (!makeDirectories(dir)) {
        AS3D_ERROR("gameDataDir: cannot create '%s'", dir.c_str());
        return std::string();
    }
    return dir + '/';
}

} // namespace as3d
