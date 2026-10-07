// as3d::userDataDir and gameDataDir for the 3DS: sdmc:/3ds/openairstrike/ (also where the app
// reads run.ini and writes log.txt).
#include <sys/stat.h>

#include "as3d/core.h"
#include "as3d/fs_util.h"
#include "as3d/platform.h"

namespace as3d {
namespace {

// mkdir -p from "sdmc:/3ds" on: the device root "sdmc:" itself is not a directory to create.
bool ensureDir(const std::string& dir) {
    for (size_t i = 8; i <= dir.size(); ++i) {
        if (i < dir.size() && dir[i] != '/') continue;
        ::mkdir(dir.substr(0, i).c_str(), 0777);
    }
    struct stat st;
    return ::stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

} // namespace

std::string userDataDir() {
    const std::string dir = "sdmc:/3ds/openairstrike";
    if (!ensureDir(dir)) {
        AS3D_ERROR("userDataDir: cannot create '%s'", dir.c_str());
        return std::string();
    }
    return dir + '/';
}

std::string gameDataDir(const char* gameKey) {
    std::string dir = userDataDir();
    if (dir.empty() || !gameKey || !*gameKey) return std::string();
    dir += gameKey;
    if (!ensureDir(dir)) {
        AS3D_ERROR("gameDataDir: cannot create '%s'", dir.c_str());
        return std::string();
    }
    return dir + '/';
}

} // namespace as3d
