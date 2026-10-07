// replaceFile (as3d/fs_util.h): the tmp-then-rename save, also where rename over an existing
// file fails (FAT on a console SD card).
#include "doctest.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "as3d/fs_util.h"

using namespace as3d;

namespace {

struct TempDir {
    std::string path;
    TempDir() {
        char tpl[] = "/tmp/as3d_fsutil_XXXXXX";
        path = mkdtemp(tpl);
    }
    ~TempDir() {
        std::string cmd = "rm -rf '" + path + "'";
        if (std::system(cmd.c_str()) != 0) std::fprintf(stderr, "cleanup failed\n");
    }
};

void put(const std::string& p, const std::string& text) {
    std::FILE* f = std::fopen(p.c_str(), "wb");
    REQUIRE(f);
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
}

std::string get(const std::string& p) {
    std::FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return "<missing>";
    char buf[64];
    size_t n = std::fread(buf, 1, sizeof buf, f);
    std::fclose(f);
    return std::string(buf, n);
}

bool exists(const std::string& p) {
    std::FILE* f = std::fopen(p.c_str(), "rb");
    if (f) std::fclose(f);
    return f != nullptr;
}

} // namespace

TEST_CASE("replaceFile: normal replace and fresh destination") {
    TempDir d;
    put(d.path + "/a.tmp", "new");
    CHECK(replaceFile(d.path + "/a.tmp", d.path + "/a"));
    CHECK(get(d.path + "/a") == "new");
    CHECK_FALSE(exists(d.path + "/a.tmp"));

    put(d.path + "/a.tmp", "newer");
    CHECK(replaceFile(d.path + "/a.tmp", d.path + "/a"));
    CHECK(get(d.path + "/a") == "newer");
}

TEST_CASE("replaceFile: survives a rename that fails over an existing file") {
    TempDir d;
    put(d.path + "/a", "old");
    put(d.path + "/a.tmp", "new");
    setReplaceFileTestFailFirstRename(true);
    const bool ok = replaceFile(d.path + "/a.tmp", d.path + "/a");
    setReplaceFileTestFailFirstRename(false);
    CHECK(ok);
    CHECK(get(d.path + "/a") == "new");
    CHECK_FALSE(exists(d.path + "/a.tmp"));
}

TEST_CASE("replaceFile: a missing tmp fails and keeps the destination") {
    TempDir d;
    put(d.path + "/a", "old");
    CHECK_FALSE(replaceFile(d.path + "/none.tmp", d.path + "/a"));
    setReplaceFileTestFailFirstRename(true);
    CHECK_FALSE(replaceFile(d.path + "/none.tmp", d.path + "/a"));
    setReplaceFileTestFailFirstRename(false);
    CHECK(get(d.path + "/a") == "old");
}

TEST_CASE("makeDirectories: creates nested directories") {
    TempDir d;
    CHECK(makeDirectories(d.path + "/x/y/z/"));
    CHECK(makeDirectories(d.path + "/x/y"));
    put(d.path + "/f", "1");
    CHECK_FALSE(makeDirectories(d.path + "/f"));
}
