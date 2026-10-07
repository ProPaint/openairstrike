// as3d::replaceFile and as3d::makeDirectories (as3d/fs_util.h).
#include "as3d/fs_util.h"

#include <sys/stat.h>

#include <cerrno>
#include <cstdio>

namespace as3d {

namespace {
bool g_failFirstRename = false;
}

void setReplaceFileTestFailFirstRename(bool fail) { g_failFirstRename = fail; }

bool replaceFile(const std::string& tmp, const std::string& dst) {
    struct stat st;
    if (::stat(tmp.c_str(), &st) != 0) return false; // nothing to move
    if (!g_failFirstRename) {
        if (std::rename(tmp.c_str(), dst.c_str()) == 0) return true;
        if (errno == EXDEV) return false; // another file system: removing dst would not help
    }
    if (::stat(dst.c_str(), &st) == 0 && std::remove(dst.c_str()) != 0) return false;
    return std::rename(tmp.c_str(), dst.c_str()) == 0;
}

bool makeDirectories(const std::string& dir) {
    for (size_t i = 1; i <= dir.size(); ++i) {
        if (i < dir.size() && dir[i] != '/') continue;
        std::string part = dir.substr(0, i);
        if (::mkdir(part.c_str(), 0700) != 0 && errno != EEXIST) return false;
    }
    struct stat st;
    return ::stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

} // namespace as3d
