#include "ctr_save.h"

#include <3ds.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/fs_util.h"
#include "as3d/profile.h"

namespace ctrsave {
namespace {

int g_opened = 0, g_closed = 0, g_allowedInside = 0, g_deniedInside = 0;

void sleepGuard(bool writing) {
    aptSetSleepAllowed(!writing);
    if (writing) {
        ++g_opened;
        if (aptIsSleepAllowed()) ++g_allowedInside; else ++g_deniedInside;
    } else {
        ++g_closed;
    }
}

std::FILE* g_log = nullptr;
void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void say(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::fputs(buf, stdout);
    if (g_log) {
        std::fputs(buf, g_log);
        std::fflush(g_log);
    }
}

double ms(u64 ticks) { return static_cast<double>(ticks) * 1000.0 / SYSCLOCK_ARM11; }

bool exists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

// Number of names ending in ".tmp" under `dir`, subdirectories included.
int countTmp(const std::string& dir) {
    int n = 0;
    DIR* d = opendir(dir.c_str());
    if (!d) return -1;
    while (dirent* e = readdir(d)) {
        const std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        const std::string full = dir + "/" + name;
        struct stat st;
        if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            const int sub = countTmp(full);
            if (sub > 0) n += sub;
        } else if (name.size() > 4 && name.compare(name.size() - 4, 4, ".tmp") == 0) {
            say("  leftover: %s\n", full.c_str());
            ++n;
        }
    }
    closedir(d);
    return n;
}

int g_fail = 0;
void check(bool ok, const char* what) {
    say("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_fail;
}

} // namespace

void installSleepGuard() { as3d::setFileWriteHook(sleepGuard); }

void runTest(const std::string& dir) {
    using namespace as3d;
    const std::string base = dir + "/savetest";
    g_log = std::fopen((dir + "/savetest_log.txt").c_str(), "w");
    if (!std::freopen((dir + "/savetest_engine_log.txt").c_str(), "w", stderr)) say("(engine log not redirected)\n");
    setLogLevel(LogLevel::Info);
    say("save path test (M2.5)\n");
    installSleepGuard();
    makeDirectories(base);

    // 1. fsync on the SD: does it exist and succeed?
    {
        const std::string fp = base + "/fsync_probe";
        std::FILE* f = std::fopen(fp.c_str(), "wb");
        if (f) {
            std::fwrite("x", 1, 1, f);
            std::fflush(f);
            errno = 0;
            const int r = fsync(fileno(f));
            say("fsync(sdmc file) = %d errno=%d (%s)\n", r, errno, r == 0 ? "FSFILE_Flush ran" : "not supported");
            std::fclose(f);
            std::remove(fp.c_str());
        }
    }

    // 2. 50 saves with changed options, each read back and compared byte for byte.
    const std::string path = base + "/profile.bin";
    std::remove(path.c_str());
    double sum = 0, worst = 0;
    int bad = 0;
    as3d::Profile p;
    for (int i = 0; i < 50; ++i) {
        p.settings.brightness = 0.2f + 0.016f * i;
        p.settings.musicVolume = (i % 11) / 10.0f;
        p.settings.sfxVolume = (i % 7) / 6.0f;
        p.settings.showFps = (i & 1) != 0;
        p.settings.touchSpeed = i % kTouchSpeedSteps;
        p.progress.insert("SAVETEST", 1000 + i, i % 7);
        std::string why;
        const u64 t0 = svcGetSystemTick();
        const bool ok = saveProfileFile(path, p, &why);
        const double t = ms(svcGetSystemTick() - t0);
        sum += t;
        if (t > worst) worst = t;
        as3d::Profile back;
        const bool loaded = ok && loadProfileFile(path, back, &why);
        if (!loaded || serializeProfile(back) != serializeProfile(p)) {
            ++bad;
            say("save %d: ok=%d loaded=%d why=%s\n", i, ok, loaded, why.c_str());
        }
    }
    say("50 saves: avg %.1f ms, worst %.1f ms, mismatches %d\n", sum / 50, worst, bad);
    check(bad == 0, "every save read back identical");
    check(!exists(path + ".tmp"), "no profile.bin.tmp after the saves");
    check(g_opened == 50 && g_closed == 50 && g_deniedInside == 50 && g_allowedInside == 0,
          "sleep refused inside each write, allowed after (hook 50 x)");
    check(aptIsSleepAllowed(), "aptIsSleepAllowed() true after the saves");

    // 3. A failed replace: a directory with the target's name. Neither the rename nor the remove
    // can make a file of it; the error path must leave no temporary and keep the directory.
    {
        const std::string t = base + "/blocked/profile.bin";
        makeDirectories(t);
        if (std::FILE* k = std::fopen((t + "/keep").c_str(), "wb")) std::fclose(k); // non-empty: remove() cannot rmdir it
        std::string why;
        const bool ok = saveProfileFile(t, p, &why);
        say("blocked target: ok=%d why=%s\n", ok, why.c_str());
        check(!ok, "saving over a directory fails");
        check(!exists(t + ".tmp"), "no temporary left by the failed save");
        struct stat st;
        check(stat(t.c_str(), &st) == 0 && S_ISDIR(st.st_mode), "the blocking directory is intact");
    }

    // 4. A missing directory is created.
    {
        const std::string t = base + "/fresh/a/b/profile.bin";
        std::string why;
        check(saveProfileFile(t, p, &why), "saving into a missing directory creates it");
        as3d::Profile back;
        check(loadProfileFile(t, back, &why), "and the file loads");
    }

    // 5. A stale temporary from a write cut short is removed at load.
    {
        const std::string t = base + "/stale/profile.bin";
        std::string why;
        saveProfileFile(t, p, &why);
        if (std::FILE* f = std::fopen((t + ".tmp").c_str(), "wb")) {
            std::fwrite("partial", 1, 7, f);
            std::fclose(f);
        }
        as3d::Profile back;
        const ProfileLoad r = loadProfileForGame(t, "", "as3d", back, &why);
        check(r == ProfileLoad::Loaded, "profile loads beside a stale temporary");
        check(!exists(t + ".tmp"), "the stale temporary is gone after the load");
    }

    // 6. No *.tmp anywhere.
    check(countTmp(base) == 0, "no *.tmp under savetest/");
    say("%s: %d failed checks\n", g_fail ? "FAILED" : "PASSED", g_fail);
    if (g_log) std::fclose(g_log);
    g_log = nullptr;

    // The result stays on the bottom screen until START.
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gspWaitForVBlank();
    }
}

} // namespace ctrsave
