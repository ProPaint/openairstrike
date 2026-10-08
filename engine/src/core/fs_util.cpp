// as3d::replaceFile and as3d::makeDirectories (as3d/fs_util.h).
#include "as3d/fs_util.h"

#include <sys/stat.h>

#include <cerrno>
#include <cstdio>
#include <vector>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace as3d {

namespace {
bool g_failFirstRename = false;
void (*g_writeHook)(bool) = nullptr;
int g_writeDepth = 0;
}

void setFileWriteHook(void (*hook)(bool writing)) { g_writeHook = hook; }
FileWriteScope::FileWriteScope() {
    if (g_writeDepth++ == 0 && g_writeHook) g_writeHook(true);
}
FileWriteScope::~FileWriteScope() {
    if (--g_writeDepth == 0 && g_writeHook) g_writeHook(false);
}

bool syncStream(std::FILE* f) {
    if (std::fflush(f) != 0) return false;
#ifndef _WIN32
    if (::fsync(::fileno(f)) != 0 && errno != EINVAL && errno != ENOSYS && errno != EROFS) return false;
#endif
    return true;
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
        if (part.back() == ':') continue; // a device root ("sdmc:")
        if (::mkdir(part.c_str(), 0700) != 0 && errno != EEXIST) return false;
    }
    struct stat st;
    return ::stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool writeFileAtomic(const std::string& dst, const void* data, size_t size, std::string* why) {
    FileWriteScope scope;
    const std::string tmp = dst + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) {
        const size_t slash = dst.find_last_of('/');
        if (slash != std::string::npos && slash > 0 && makeDirectories(dst.substr(0, slash)))
            f = std::fopen(tmp.c_str(), "wb");
    }
    if (!f) {
        if (why) *why = "cannot create " + tmp;
        return false;
    }
    const bool written = size == 0 || std::fwrite(data, 1, size, f) == size;
    const bool synced = written && syncStream(f);
    const bool closed = std::fclose(f) == 0;
    if (!written || !synced || !closed) {
        std::remove(tmp.c_str());
        if (why) *why = "cannot write " + tmp;
        return false;
    }
    if (!replaceFile(tmp, dst)) {
        std::remove(tmp.c_str());
        if (why) *why = "cannot replace " + dst;
        return false;
    }
    return true;
}

} // namespace as3d
