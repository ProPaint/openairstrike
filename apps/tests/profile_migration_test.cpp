// Save file per game (docs/spec/issues/160): the version 2 header with the game key, counts read
// as data, the migration of the first game's version 1 save, and hostile files. Everything runs
// on directories under build/profile_scratch, never on the real user data directory.
#include "doctest.h"

#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <new>
#include <string>

#include "as3d/game_profile.h"
#include "as3d/platform.h"
#include "as3d/profile.h"

using namespace as3d;

namespace {

// Largest single allocation made while g_track is set: a count read from a file must never
// size a buffer.
std::atomic<bool> g_track{false};
std::atomic<size_t> g_maxAlloc{0};

std::string scratch(const char* name) {
    const std::string dir = std::string(AS3D_REPO_ROOT) + "/build/profile_scratch/" + name;
    std::error_code ec;
    std::filesystem::permissions(dir, std::filesystem::perms::owner_all, ec);
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

bool exists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

u32 crc32(const u8* d, size_t n) {
    u32 c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return c ^ 0xFFFFFFFFu;
}

void put32(std::vector<u8>& b, u32 v) {
    for (int i = 0; i < 4; i++) b.push_back(static_cast<u8>(v >> (8 * i)));
}

// A file built from bytes: `scores` high scores, the given flag arrays, an empty settings chunk
// (or the settings of `sett`). version 1 has no key.
std::vector<u8> buildFile(u32 version, const char* key, int scores, const std::vector<u8>& heli,
                          const std::vector<u8>& miss, const std::vector<u8>& sett = {0, 0}) {
    std::vector<u8> prog;
    prog.push_back(static_cast<u8>(scores));
    for (int i = 0; i < scores; i++) {
        const std::string name = "P" + std::to_string(i);
        prog.push_back(static_cast<u8>(name.size()));
        prog.insert(prog.end(), name.begin(), name.end());
        put32(prog, 1000u * (scores - i));
        put32(prog, 0);
        prog.push_back(static_cast<u8>(i % 7));
    }
    prog.push_back(static_cast<u8>(heli.size()));
    prog.insert(prog.end(), heli.begin(), heli.end());
    prog.push_back(static_cast<u8>(miss.size()));
    prog.insert(prog.end(), miss.begin(), miss.end());
    std::vector<u8> payload;
    payload.insert(payload.end(), {'P', 'R', 'O', 'G'});
    put32(payload, static_cast<u32>(prog.size()));
    payload.insert(payload.end(), prog.begin(), prog.end());
    payload.insert(payload.end(), {'S', 'E', 'T', 'T'});
    put32(payload, static_cast<u32>(sett.size()));
    payload.insert(payload.end(), sett.begin(), sett.end());
    std::vector<u8> keyed;
    if (version >= 2) keyed.insert(keyed.end(), key, key + std::strlen(key));
    keyed.insert(keyed.end(), payload.begin(), payload.end());
    std::vector<u8> f = {'A', 'S', '3', 'D', 'P', 'R', 'O', 'F'};
    put32(f, version);
    put32(f, static_cast<u32>(payload.size()));
    put32(f, crc32(keyed.data(), keyed.size()));
    if (version >= 2) f.push_back(static_cast<u8>(std::strlen(key)));
    f.insert(f.end(), keyed.begin(), keyed.end());
    return f;
}

// The version 1 writer of the release before per-game saves: the first game's v2 bytes with the
// key dropped from the header (the payload layout is the same).
std::vector<u8> toVersion1(const std::vector<u8>& v2) {
    const size_t keyLen = v2[20];
    const u32 payloadSize = static_cast<u32>(v2.size() - 21 - keyLen);
    std::vector<u8> f(v2.begin(), v2.begin() + 20);
    const u32 one = 1;
    std::memcpy(&f[8], &one, 4);
    f.insert(f.end(), v2.begin() + 21 + keyLen, v2.end());
    const u32 c = crc32(f.data() + 20, payloadSize);
    std::memcpy(&f[16], &c, 4);
    return f;
}

void writeBytes(const std::string& path, const std::vector<u8>& b) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    REQUIRE(f);
    std::fwrite(b.data(), 1, b.size(), f);
    std::fclose(f);
}

bool sameProgress(const Progress& a, const Progress& b) {
    if (a.missionCount != b.missionCount || a.helicopterCount != b.helicopterCount) return false;
    for (int i = 0; i < kHighScoreCount; i++)
        if (a.scores[i].name != b.scores[i].name || a.scores[i].score != b.scores[i].score ||
            a.scores[i].rank != b.scores[i].rank)
            return false;
    for (int i = 0; i < a.helicopterCount; i++)
        if (a.helicopterUnlocked[i] != b.helicopterUnlocked[i]) return false;
    for (int i = 0; i < a.missionCount; i++)
        if (a.missionUnlocked[i] != b.missionUnlocked[i]) return false;
    return true;
}

Profile profileFor(const GameRules& rules) {
    Profile p;
    p.progress = Progress::defaults(rules);
    return p;
}

// A first-game profile with something of everything.
Profile playedProfile() {
    Profile p;
    p.progress.insert("Migrant", 777777, 4);
    p.progress.unlockAfterMission(6, 4);
    p.progress.unlockAfterMission(7, 5);
    p.settings.camera = 3;
    p.settings.brightness = 0.9f;
    p.settings.sfxVolume = 0.25f;
    p.settings.showFps = true;
    p.settings.keys[0][2][0] = 66;
    return p;
}

void checkPlayed(const Profile& q, const Profile& p) {
    CHECK(sameProgress(q.progress, p.progress));
    CHECK(q.settings.camera == 3);
    CHECK(q.settings.brightness == doctest::Approx(0.9f));
    CHECK(q.settings.sfxVolume == doctest::Approx(0.25f));
    CHECK(q.settings.showFps);
    CHECK(q.settings.keys[0][2][0] == 66);
}

} // namespace

void* operator new(size_t n) {
    if (g_track.load(std::memory_order_relaxed)) {
        size_t m = g_maxAlloc.load();
        while (n > m && !g_maxAlloc.compare_exchange_weak(m, n)) {}
    }
    void* p = std::malloc(n ? n : 1);
    if (!p) std::abort();
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

TEST_CASE("profile v2: round trip for the rules of each game, the header carries the key") {
    for (int g = 0; g < kGameCount; g++) {
        const GameProfile& game = gameProfile(static_cast<GameId>(g));
        Profile p = profileFor(game.rules);
        p.progress.insert("Round", 5555555, 3);
        p.progress.missionUnlocked[game.rules.missionCount - 1] = true;
        p.progress.helicopterUnlocked[game.rules.helicopterCount - 1] = true;
        p.settings.camera = 2;
        const std::vector<u8> bytes = serializeProfile(p, game.key);
        REQUIRE(bytes.size() > 21 + std::strlen(game.key));
        CHECK(bytes[8] == 2);
        CHECK(bytes[20] == std::strlen(game.key));
        CHECK(std::memcmp(&bytes[21], game.key, std::strlen(game.key)) == 0);
        Profile q = profileFor(game.rules);
        std::string why;
        REQUIRE_MESSAGE(deserializeProfile(bytes.data(), bytes.size(), q, &why, game.key), why);
        CHECK(sameProgress(p.progress, q.progress));
        CHECK(q.settings.camera == 2);
        // Through the file helpers too.
        const std::string path = scratch("roundtrip") + "/profile.bin";
        REQUIRE(saveProfileFile(path, p, &why, game.key));
        Profile r = profileFor(game.rules);
        CHECK_MESSAGE(loadProfileFile(path, r, &why, game.key), why);
        CHECK(sameProgress(p.progress, r.progress));
    }
}

TEST_CASE("profile v2: a file of another game is rejected and the defaults are used") {
    const GameProfile& as2 = gameProfile(GameId::AirStrike2);
    const GameProfile& gulf = gameProfile(GameId::GulfThunder);
    Profile p = profileFor(as2.rules);
    p.progress.insert("Sequel", 9999999, 5);
    p.settings.camera = 3;
    const std::vector<u8> bytes = serializeProfile(p, as2.key);
    Profile q = profileFor(gulf.rules);
    std::string why;
    CHECK_FALSE(deserializeProfile(bytes.data(), bytes.size(), q, &why, gulf.key));
    CHECK(why == "profile of another game");
    CHECK(sameProgress(q.progress, profileFor(gulf.rules).progress));
    CHECK(q.settings.camera == Settings::defaults().camera);
    Profile first;
    CHECK_FALSE(deserializeProfile(bytes.data(), bytes.size(), first, &why, "as3d"));
    CHECK(sameProgress(first.progress, Progress::defaults()));
    // A version 1 file is the first game's only.
    const std::vector<u8> v1 = toVersion1(serializeProfile(Profile{}));
    Profile s = profileFor(as2.rules);
    CHECK_FALSE(deserializeProfile(v1.data(), v1.size(), s, &why, as2.key));
    CHECK(why == "profile of another game");
    Profile f;
    CHECK(deserializeProfile(v1.data(), v1.size(), f, &why, "as3d"));
    // A key that does not belong in a header.
    std::vector<u8> bad = buildFile(2, "as3d", 15, std::vector<u8>(10, 1), std::vector<u8>(20, 1));
    bad[20] = 0;
    Profile z;
    CHECK_FALSE(deserializeProfile(bad.data(), bad.size(), z));
    bad[20] = 200;
    CHECK_FALSE(deserializeProfile(bad.data(), bad.size(), z));
}

TEST_CASE("profile v2: counts above the maxima are rejected, fewer than the game's are defaulted") {
    const std::vector<u8> h10(10, 1), m20(20, 1);
    Profile q;
    std::string why;
    auto load = [&](const std::vector<u8>& f, std::string* w = nullptr) {
        q = Profile{};
        return deserializeProfile(f.data(), f.size(), q, w);
    };
    CHECK(load(buildFile(2, "as3d", 15, h10, m20)));
    CHECK_FALSE(load(buildFile(2, "as3d", 16, h10, m20), &why)); // more scores than the table
    CHECK(sameProgress(q.progress, Progress::defaults()));
    CHECK_FALSE(load(buildFile(2, "as3d", 15, std::vector<u8>(kMaxHelicopters + 1, 1), m20)));
    CHECK(sameProgress(q.progress, Progress::defaults()));
    CHECK_FALSE(load(buildFile(2, "as3d", 15, h10, std::vector<u8>(kMaxMissions + 1, 1))));
    CHECK(sameProgress(q.progress, Progress::defaults()));
    CHECK_FALSE(load(buildFile(2, "as3d", 15, h10, std::vector<u8>(255, 1))));
    // The maxima themselves are fine; the entries beyond the game's counts are ignored.
    CHECK(load(buildFile(2, "as3d", 15, std::vector<u8>(kMaxHelicopters, 1), std::vector<u8>(kMaxMissions, 1))));
    CHECK(q.progress.missionCount == 20);
    for (int i = 0; i < 20; i++) CHECK(q.progress.missionUnlocked[i]);
    // A version 1 file keeps the strict check.
    CHECK_FALSE(load(buildFile(1, "", 15, h10, std::vector<u8>(19, 1))));
    CHECK(load(buildFile(1, "", 15, h10, m20)));

    // A save from before the game gained missions: 12 of 20, all open; the rest is defaulted.
    CHECK(load(buildFile(2, "as3d", 15, std::vector<u8>(6, 1), std::vector<u8>(12, 1))));
    for (int i = 0; i < 12; i++) CHECK(q.progress.missionUnlocked[i]);
    for (int i = 12; i < 20; i++) CHECK_FALSE(q.progress.missionUnlocked[i]);
    for (int i = 0; i < 6; i++) CHECK(q.progress.helicopterUnlocked[i]);
    for (int i = 6; i < 10; i++) CHECK_FALSE(q.progress.helicopterUnlocked[i]);
    // And fewer high scores: the table's defaults fill the rest.
    CHECK(load(buildFile(2, "as3d", 3, h10, m20)));
    CHECK(q.progress.scores[0].name == "P0");
    CHECK(q.progress.scores[3].name == Progress::defaults().scores[3].name);
    CHECK(q.progress.scores[14].score == Progress::defaults().scores[14].score);
}

TEST_CASE("profile v2: every truncation and every single-byte corruption ends in defaults or a valid profile") {
    const Profile p = playedProfile();
    const std::vector<u8> good = serializeProfile(p);
    const Progress def = Progress::defaults();
    auto check = [&](const std::vector<u8>& f, size_t len, bool mustFail) {
        Profile q;
        g_maxAlloc = 0;
        g_track = true;
        std::string why;
        const bool ok = deserializeProfile(f.data(), len, q, &why);
        g_track = false;
        CHECK(g_maxAlloc.load() < 4096);
        if (mustFail) CHECK_FALSE(ok);
        if (!ok) {
            CHECK(!why.empty());
        } else {
            CHECK(q.progress.missionCount == kMissionCount);
            CHECK(q.progress.helicopterCount == kHelicopterCount);
            for (const HighScore& h : q.progress.scores) {
                CHECK(h.rank >= 0);
                CHECK(h.rank < kRankCount);
                CHECK(h.score >= 0);
                CHECK(h.name.size() <= 31);
            }
            CHECK(q.progress.missionUnlocked[0]);
            CHECK(q.progress.helicopterUnlocked[1]);
        }
        (void)def;
    };
    for (size_t n = 0; n < good.size(); n++) check(good, n, true);
    for (size_t i = 0; i < good.size(); i++) {
        std::vector<u8> bad = good;
        bad[i] ^= 0xA5;
        check(bad, bad.size(), true); // caught by the header or the CRC
    }
    // The same, with the CRC and size repaired so that the parser sees the hostile bytes.
    const size_t payloadAt = 21 + 4;
    for (u8 x : {u8(0xFF), u8(0x80), u8(0x01)}) {
        for (size_t i = payloadAt; i < good.size(); i++) {
            std::vector<u8> bad = good;
            bad[i] ^= x;
            const u32 c = crc32(bad.data() + 21, bad.size() - 21);
            std::memcpy(&bad[16], &c, 4);
            check(bad, bad.size(), false);
        }
    }
    // Truncated at every length with the header repaired to match.
    for (size_t n = payloadAt; n < good.size(); n++) {
        std::vector<u8> cut(good.begin(), good.begin() + n);
        const u32 size = static_cast<u32>(n - payloadAt);
        std::memcpy(&cut[12], &size, 4);
        const u32 c = crc32(cut.data() + 21, n - 21);
        std::memcpy(&cut[16], &c, 4);
        check(cut, cut.size(), false);
    }
}

TEST_CASE("profile migration: the first game's version 1 save moves to <key>/profile.bin") {
    const std::string dir = scratch("migrate");
    const std::string legacy = dir + "/profile.bin";
    const std::string path = dir + "/as3d/profile.bin";
    const Profile p = playedProfile();
    const std::vector<u8> v1 = toVersion1(serializeProfile(p));
    REQUIRE(v1[8] == 1);
    writeBytes(legacy, v1);

    Profile q;
    std::string why;
    CHECK(loadProfileForGame(path, legacy, "as3d", q, &why) == ProfileLoad::Migrated);
    checkPlayed(q, p);
    CHECK(exists(path));
    CHECK_FALSE(exists(legacy));
    CHECK(exists(dir + "/profile.v1.bak"));
    // The new file is version 2, of the first game, and holds the same profile.
    Profile r;
    CHECK_MESSAGE(loadProfileFile(path, r, &why), why);
    checkPlayed(r, p);
    std::FILE* f = std::fopen(path.c_str(), "rb");
    REQUIRE(f);
    u8 head[26] = {};
    CHECK(std::fread(head, 1, sizeof head, f) == sizeof head);
    std::fclose(f);
    CHECK(head[8] == 2);
    CHECK(std::memcmp(head + 21, "as3d", 4) == 0);
    // The backup is byte for byte the old file.
    Profile b;
    CHECK(loadProfileFile(dir + "/profile.v1.bak", b, &why));
    checkPlayed(b, p);

    // A second start reads the version 2 file and migrates nothing; a change made in between
    // is what it reads.
    Profile changed = q;
    changed.settings.camera = 0;
    REQUIRE(saveProfileFile(path, changed, &why));
    Profile again;
    CHECK(loadProfileForGame(path, legacy, "as3d", again, &why) == ProfileLoad::Loaded);
    CHECK(again.settings.camera == 0);
    // Even if an old file shows up again (a restored backup), the new save wins and is not touched.
    writeBytes(legacy, v1);
    Profile third;
    CHECK(loadProfileForGame(path, legacy, "as3d", third, &why) == ProfileLoad::Loaded);
    CHECK(third.settings.camera == 0);
    CHECK(exists(legacy));
}

TEST_CASE("profile migration: an existing backup is never overwritten, a sequel never reads the old file") {
    const std::string dir = scratch("migrate_more");
    const std::string legacy = dir + "/profile.bin";
    const std::vector<u8> v1 = toVersion1(serializeProfile(playedProfile()));
    writeBytes(legacy, v1);
    writeBytes(dir + "/profile.v1.bak", {1, 2, 3});
    // A sequel: fresh, the old file stays where it is.
    Profile s = profileFor(gameProfile(GameId::AirStrike2).rules);
    std::string why;
    CHECK(loadProfileForGame(dir + "/as2/profile.bin", legacy, "as2", s, &why) == ProfileLoad::Fresh);
    CHECK(sameProgress(s.progress, profileFor(gameProfile(GameId::AirStrike2).rules).progress));
    CHECK(exists(legacy));
    CHECK_FALSE(exists(dir + "/as2/profile.bin"));
    // The first game: migrates, and keeps the older backup.
    Profile q;
    CHECK(loadProfileForGame(dir + "/as3d/profile.bin", legacy, "as3d", q, &why) == ProfileLoad::Migrated);
    CHECK_FALSE(exists(legacy));
    CHECK(exists(dir + "/profile.v1.bak"));
    CHECK(exists(dir + "/profile.v1.bak.1"));
    Profile older;
    CHECK_FALSE(loadProfileFile(dir + "/profile.v1.bak", older)); // still the 3 bytes
    // No file anywhere: fresh. A bad old file: defaults, and it is left alone.
    const std::string d2 = scratch("migrate_bad");
    Profile n;
    CHECK(loadProfileForGame(d2 + "/as3d/profile.bin", d2 + "/profile.bin", "as3d", n, &why) == ProfileLoad::Fresh);
    writeBytes(d2 + "/profile.bin", {'j', 'u', 'n', 'k'});
    CHECK(loadProfileForGame(d2 + "/as3d/profile.bin", d2 + "/profile.bin", "as3d", n, &why) == ProfileLoad::Unusable);
    CHECK(sameProgress(n.progress, Progress::defaults()));
    CHECK(exists(d2 + "/profile.bin"));
    CHECK_FALSE(exists(d2 + "/as3d/profile.bin"));
}

TEST_CASE("profile migration: when the new file cannot be written the old one stays and is used") {
    if (::geteuid() == 0) return; // a read-only directory does not stop root
    const std::string dir = scratch("migrate_ro");
    const std::string legacy = dir + "/profile.bin";
    const Profile p = playedProfile();
    writeBytes(legacy, toVersion1(serializeProfile(p)));
    const std::vector<u8> before = toVersion1(serializeProfile(p));
    REQUIRE(::chmod(dir.c_str(), 0500) == 0);
    Profile q;
    std::string why;
    const ProfileLoad r = loadProfileForGame(dir + "/as3d/profile.bin", legacy, "as3d", q, &why);
    ::chmod(dir.c_str(), 0700);
    CHECK(r == ProfileLoad::LoadedLegacy);
    checkPlayed(q, p);
    CHECK(exists(legacy));
    CHECK_FALSE(exists(dir + "/as3d"));
    CHECK_FALSE(exists(dir + "/profile.v1.bak"));
    Profile still;
    CHECK(loadProfileFile(legacy, still, &why));
    checkPlayed(still, p);
    // Once the directory is writable again the next start migrates.
    CHECK(loadProfileForGame(dir + "/as3d/profile.bin", legacy, "as3d", q, &why) == ProfileLoad::Migrated);
    CHECK(exists(dir + "/profile.v1.bak"));
}

TEST_CASE("profile directories: one per game under the user data directory") {
    const std::string dir = scratch("userdata");
    ::setenv("AS3D_USER_DATA_DIR", dir.c_str(), 1);
    const std::string base = userDataDir();
    const std::string as2 = gameDataDir("as2");
    ::unsetenv("AS3D_USER_DATA_DIR");
    CHECK(base == dir + "/");
    CHECK(as2 == dir + "/as2/");
    CHECK(exists(dir + "/as2"));
    CHECK(gameDataDir("").empty());
    CHECK(profileGameKey(nullptr) == std::string("as3d"));
    CHECK(profileGameKey(&gameProfile(GameId::GulfThunder)) == std::string("gulf"));
}
