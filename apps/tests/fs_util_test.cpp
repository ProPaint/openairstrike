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

namespace {
int g_hookOpen = 0, g_hookClose = 0;
void countHook(bool writing) { (writing ? g_hookOpen : g_hookClose)++; }
} // namespace

TEST_CASE("writeFileAtomic: new file, replace, missing directory, no leftovers") {
    TempDir d;
    CHECK(writeFileAtomic(d.path + "/a", "one", 3));
    CHECK(get(d.path + "/a") == "one");
    setReplaceFileTestFailFirstRename(true); // FAT: rename over an existing file fails
    CHECK(writeFileAtomic(d.path + "/a", "two2", 4));
    setReplaceFileTestFailFirstRename(false);
    CHECK(get(d.path + "/a") == "two2");
    CHECK_FALSE(exists(d.path + "/a.tmp"));
    CHECK(writeFileAtomic(d.path + "/sub/deeper/b", "x", 1)); // directory created on demand
    CHECK(get(d.path + "/sub/deeper/b") == "x");
    CHECK(writeFileAtomic(d.path + "/empty", nullptr, 0));
    CHECK(get(d.path + "/empty").empty());
}

TEST_CASE("writeFileAtomic: a failed replace leaves the destination and no tmp") {
    TempDir d;
    REQUIRE(makeDirectories(d.path + "/a")); // a directory with the target's name: neither rename nor remove works
    put(d.path + "/a/keep", "k");
    std::string why;
    CHECK_FALSE(writeFileAtomic(d.path + "/a", "data", 4, &why));
    CHECK(why.find("cannot replace") == 0);
    CHECK_FALSE(exists(d.path + "/a.tmp"));
    CHECK(get(d.path + "/a/keep") == "k");
    // A destination below a regular file: no directory can be made, no tmp appears.
    put(d.path + "/f", "1");
    CHECK_FALSE(writeFileAtomic(d.path + "/f/x", "d", 1, &why));
    CHECK(why.find("cannot create") == 0);
}

TEST_CASE("FileWriteScope: the hook fires once for the outermost scope, writeFileAtomic is a scope") {
    TempDir d;
    g_hookOpen = g_hookClose = 0;
    setFileWriteHook(countHook);
    {
        FileWriteScope outer;
        FileWriteScope inner;
        CHECK(g_hookOpen == 1);
        CHECK(writeFileAtomic(d.path + "/a", "1", 1));
        CHECK(g_hookClose == 0);
    }
    CHECK(g_hookOpen == 1);
    CHECK(g_hookClose == 1);
    CHECK(writeFileAtomic(d.path + "/a", "2", 1));
    CHECK(g_hookOpen == 2);
    CHECK(g_hookClose == 2);
    setFileWriteHook(nullptr);
}

TEST_CASE("makeDirectories: a device prefix is never created as a directory") {
    CHECK_FALSE(makeDirectories("nodev:/x"));
    CHECK_FALSE(exists("nodev:"));
}
