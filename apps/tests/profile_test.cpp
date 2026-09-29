// Progression and persistence tests (frontend.md 5 and 6): campaign banking, rank, unlocks,
// high scores, settings ranges and the profile file (round trip, corrupt and truncated files).
#include "doctest.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "as3d/profile.h"

using namespace as3d;

namespace {

u32 crc32(const u8* d, size_t n) {
    u32 c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return c ^ 0xFFFFFFFFu;
}

// Version 2 file of the first game: 20 bytes of header, the key length, "as3d", the payload.
constexpr size_t kPayloadAt = 20 + 1 + 4;

// Rewrites the header's size and CRC (of key and payload) after the payload was edited.
void fixHeader(std::vector<u8>& f) {
    const u32 n = static_cast<u32>(f.size() - kPayloadAt);
    const u32 c = crc32(f.data() + 21, 4 + n);
    std::memcpy(&f[12], &n, 4);
    std::memcpy(&f[16], &c, 4);
}

bool sameProgress(const Progress& a, const Progress& b) {
    for (int i = 0; i < kHighScoreCount; i++)
        if (a.scores[i].name != b.scores[i].name || a.scores[i].score != b.scores[i].score ||
            a.scores[i].rank != b.scores[i].rank)
            return false;
    return std::memcmp(a.helicopterUnlocked, b.helicopterUnlocked, sizeof a.helicopterUnlocked) == 0 &&
           std::memcmp(a.missionUnlocked, b.missionUnlocked, sizeof a.missionUnlocked) == 0;
}

} // namespace

TEST_CASE("profile: fresh defaults") {
    Progress p = Progress::defaults();
    CHECK(p.scores[0].name == "Divo Master");
    CHECK(p.scores[0].score == 1000000);
    CHECK(p.scores[0].rank == 6);
    CHECK(p.scores[14].score == 30000);
    CHECK(p.scores[14].rank == 0);
    CHECK(p.helicopterUnlocked[0]);
    CHECK(p.helicopterUnlocked[1]);
    CHECK_FALSE(p.helicopterUnlocked[2]);
    CHECK(p.missionUnlocked[0]);
    CHECK(p.missionUnlocked[1]);
    CHECK_FALSE(p.missionUnlocked[2]);
    Settings s = Settings::defaults();
    CHECK(s.camera == 1);
    CHECK(s.brightness == doctest::Approx(0.6f));
    CHECK(s.sfxVolume == doctest::Approx(0.5f));
    CHECK(s.fullscreen);
    CHECK_FALSE(s.showHints);
    CHECK(s.keys[0][static_cast<int>(Action::MoveForward)][0] == 38);
    CHECK(s.keys[1][static_cast<int>(Action::SwitchItem)][1] == 212);
    CHECK(actionBit(Action::SwitchWeapon) == 0x400);
}

TEST_CASE("profile: rank index and difficulty factors") {
    CHECK(rankIndex(100, true) == 0);
    CHECK(rankIndex(0, false) == 1);
    CHECK(rankIndex(2.99, false) == 1);
    CHECK(rankIndex(3, false) == 2);
    CHECK(rankIndex(7.5, false) == 3);
    CHECK(rankIndex(21.9, false) == 4);
    CHECK(rankIndex(22, false) == 5);
    CHECK(rankIndex(30, false) == 6);
    CHECK(difficultyFactors(2).rank == doctest::Approx(1.07f));
    CHECK(difficultyFactors(9).rank == doctest::Approx(1.3f));
    CHECK(difficultyFactors(-1).score == doctest::Approx(0.6f));
}

TEST_CASE("profile: campaign start, banking, lives carry-over, rank values") {
    Campaign c;
    c.p[0].banked = 5;
    c.start(4, 3, 1);
    CHECK(c.active);
    CHECK(c.mission == 4);
    CHECK(c.p[0].banked == 0);
    CHECK(c.p[0].livesAtStart == 2);
    LevelPlayerResult r[2];
    r[0].score = 1500.7;
    r[0].lives = 1;
    r[0].stars = 3;
    LevelTotals t;
    t.starTotal = 6;
    t.maxScore = 3001.4;
    // Rank shown before banking: (0.5 + 0.5 * 0.5 + 0) * 1.2.
    CHECK(c.missionRankValue(r[0], t) == doctest::Approx((0.5 + 0.25) * 1.2));
    c.bank(r, t);
    CHECK(c.p[0].banked == 1500);
    CHECK(c.p[0].rankAccumulator == doctest::Approx(0.75));
    CHECK(c.p[0].livesAtStart == 1);
    CHECK(c.highScoreRankValue() == doctest::Approx(0.75 * 1.2));
    // Player 2 untouched in one-player mode.
    CHECK(c.p[1].banked == 0);
    // Zero denominators count as zero (no NaN).
    LevelTotals zero;
    c.bank(r, zero);
    CHECK(c.p[0].banked == 3000); // ftol(1500 + 1500.7)
    CHECK(c.p[0].rankAccumulator == doctest::Approx(0.75));
    CHECK(c.nextMission() == 5);
    c.mission = 19;
    CHECK(c.nextMission() == 0);
    // Two players bank both.
    Campaign c2;
    c2.start(0, 2, 2);
    r[1].score = 99;
    r[1].lives = 3;
    c2.bank(r, t);
    CHECK(c2.p[1].banked == 99);
    CHECK(c2.p[1].livesAtStart == 3);
}

TEST_CASE("profile: high-score qualification and insertion") {
    Progress p = Progress::defaults();
    CHECK(p.qualifyingSlot(29999) == -1);
    CHECK(p.qualifyingSlot(30000) == 14);
    CHECK(p.qualifyingSlot(950000) == 1);
    CHECK(p.insert("Ace", 950000, 5) == 1);
    CHECK(p.scores[1].name == "Ace");
    CHECK(p.scores[2].name == "Dennis");
    CHECK(p.scores[14].name == "Turner"); // Linda dropped
    CHECK(p.insert(std::string(40, 'x'), 2000000, 9) == 0);
    CHECK(p.scores[0].name.size() == 31);
    CHECK(p.scores[0].rank == 6);
    CHECK(p.insert("low", 1, 1) == -1);
}

TEST_CASE("profile: unlocking after missions") {
    Progress p = Progress::defaults();
    p.unlockAfterMission(2, 7); // mission 3 unlocks helicopter 7 and mission 4
    CHECK(p.helicopterUnlocked[7]);
    CHECK(p.missionUnlocked[3]);
    p.unlockAfterMission(19, -1);
    CHECK(p.missionUnlocked[0]);
    p.unlockAfterMission(5, 42); // out of range: nothing
    CHECK(p.missionUnlocked[6]);
}

TEST_CASE("profile: settings ranges and bindings") {
    Settings s = Settings::defaults();
    s.camera = 7;
    s.videoMode = 12;
    s.colorDepth = 24;
    s.brightness = 5.0f;
    s.sfxVolume = -1.0f;
    s.musicVolume = 1.5f;
    s.keys[0][0][0] = 999;
    s.clampToRanges();
    CHECK(s.camera == 1);
    CHECK(s.videoMode == 1);
    CHECK(s.colorDepth == 0);
    CHECK(s.brightness == doctest::Approx(1.0f));
    CHECK(s.sfxVolume == 0.0f);
    CHECK(s.musicVolume == 1.0f);
    CHECK(s.keys[0][0][0] == 0);

    Settings b = Settings::defaults();
    const int fire = static_cast<int>(Action::PrimaryAttack), up = static_cast<int>(Action::MoveForward);
    b.bindKey(0, Action::PrimaryAttack, 38); // Up moves from Move Forward to Primary Attack
    CHECK(b.keys[0][fire][0] == 38);
    CHECK(b.keys[0][fire][1] == 17);
    CHECK(b.keys[0][up][0] == 243);
    CHECK(b.keys[0][up][1] == 0);
    CHECK(b.keys[1][up][0] == 38); // the other player keeps it
    b.bindKey(0, Action::MoveLeft, 27); // Esc can never be bound
    CHECK(b.keys[0][static_cast<int>(Action::MoveLeft)][0] == 37);
    b.unbindKey(0, Action::PrimaryAttack);
    CHECK(b.keys[0][fire][0] == 0);
    CHECK(b.keys[0][fire][1] == 38);

    Settings m = Settings::defaults();
    m.mouseControl = true;
    m.applyMouseControlBindings();
    CHECK(m.keys[0][fire][1] == 200);
    CHECK(m.keys[0][static_cast<int>(Action::MissileAttack)][1] == 201);
    CHECK(m.keys[0][static_cast<int>(Action::UseItem)][1] == 202);
    m.mouseControl = false;
    m.applyMouseControlBindings();
    CHECK(m.keys[0][fire][1] == 203);
    CHECK(m.keys[1][fire][1] == 0); // joy1 moved to player 1
}

TEST_CASE("profile file: round trip") {
    Profile p;
    p.progress.insert("Round Trip", 123456789, 4);
    p.progress.unlockAfterMission(10, 9);
    p.settings.camera = 3;
    p.settings.brightness = 0.9f;
    p.settings.musicVolume = 0.1f;
    p.settings.showHints = true;
    p.settings.keys[1][4][1] = 77;
    std::vector<u8> bytes = serializeProfile(p);
    Profile q;
    std::string why;
    REQUIRE_MESSAGE(deserializeProfile(bytes.data(), bytes.size(), q, &why), why);
    CHECK(sameProgress(p.progress, q.progress));
    CHECK(q.settings.camera == 3);
    CHECK(q.settings.brightness == doctest::Approx(0.9f));
    CHECK(q.settings.musicVolume == doctest::Approx(0.1f));
    CHECK(q.settings.showHints);
    CHECK(q.settings.keys[1][4][1] == 77);

    const std::string path = std::string(AS3D_REPO_ROOT) + "/build/profile_test.bin";
    REQUIRE(saveProfileFile(path, p, &why));
    Profile r;
    CHECK(loadProfileFile(path, r, &why));
    CHECK(sameProgress(p.progress, r.progress));
    std::FILE* tmp = std::fopen((path + ".tmp").c_str(), "rb");
    CHECK(tmp == nullptr); // renamed away
    if (tmp) std::fclose(tmp);
    std::remove(path.c_str());
    Profile missing;
    CHECK_FALSE(loadProfileFile(path, missing, &why));
    CHECK(sameProgress(missing.progress, Progress::defaults()));
}

TEST_CASE("profile file: truncated, corrupt and hostile files fall back to defaults") {
    Profile p;
    p.progress.insert("Someone", 5000000, 6);
    const std::vector<u8> good = serializeProfile(p);
    const Progress def = Progress::defaults();
    // Every truncation.
    for (size_t n = 0; n < good.size(); n++) {
        Profile q;
        CHECK_FALSE(deserializeProfile(good.data(), n, q));
        CHECK(sameProgress(q.progress, def));
    }
    // Every single-byte corruption is caught (header check or CRC).
    for (size_t i = 0; i < good.size(); i++) {
        std::vector<u8> bad = good;
        bad[i] ^= 0x5A;
        Profile q;
        std::string why;
        const bool ok = deserializeProfile(bad.data(), bad.size(), q, &why);
        CHECK_FALSE(ok);
    }
    // Hostile counts with a valid CRC: huge chunk length, huge string length, wrong counts.
    {
        std::vector<u8> bad = good;
        const u32 huge = 0xFFFFFFF0u;
        std::memcpy(&bad[kPayloadAt + 4], &huge, 4); // PROG chunk length
        fixHeader(bad);
        Profile q;
        std::string why;
        CHECK_FALSE(deserializeProfile(bad.data(), bad.size(), q, &why));
        CHECK(why == "chunk longer than the file");
        CHECK(sameProgress(q.progress, def));
    }
    {
        std::vector<u8> bad = good;
        bad[kPayloadAt + 8] = 200; // high-score count
        fixHeader(bad);
        Profile q;
        std::string why;
        CHECK_FALSE(deserializeProfile(bad.data(), bad.size(), q, &why));
        CHECK(sameProgress(q.progress, def));
        // The settings chunk is still read.
        CHECK(q.settings.camera == p.settings.camera);
    }
    {
        std::vector<u8> bad = good;
        bad[kPayloadAt + 9] = 250; // first name length
        fixHeader(bad);
        Profile q;
        CHECK_FALSE(deserializeProfile(bad.data(), bad.size(), q));
        CHECK(sameProgress(q.progress, def));
    }
    {
        // Settings: count far above what the chunk can hold.
        std::vector<u8> bad = good;
        size_t at = 0;
        for (size_t i = kPayloadAt; i + 4 <= bad.size(); i++)
            if (std::memcmp(&bad[i], "SETT", 4) == 0) { at = i; break; }
        REQUIRE(at > 0);
        bad[at + 8] = 0xFF;
        bad[at + 9] = 0xFF;
        fixHeader(bad);
        Profile q;
        std::string why;
        CHECK_FALSE(deserializeProfile(bad.data(), bad.size(), q, &why));
        CHECK(why == "bad settings count");
        CHECK(q.settings.camera == Settings::defaults().camera);
    }
    // Wrong version, wrong magic, oversized.
    {
        std::vector<u8> bad = good;
        bad[8] = 3;
        Profile q;
        CHECK_FALSE(deserializeProfile(bad.data(), bad.size(), q));
        bad = good;
        bad[0] = 'X';
        CHECK_FALSE(deserializeProfile(bad.data(), bad.size(), q));
        std::vector<u8> big(2u << 20, 0);
        CHECK_FALSE(deserializeProfile(big.data(), big.size(), q));
        CHECK_FALSE(deserializeProfile(nullptr, 0, q));
    }
    // Out-of-range settings values in a well-formed file are clamped.
    {
        Profile odd;
        odd.settings.camera = 3;
        std::vector<u8> bytes = serializeProfile(odd);
        size_t at = 0;
        const char* key = "camera";
        for (size_t i = 0; i + 6 <= bytes.size(); i++)
            if (std::memcmp(&bytes[i], key, 6) == 0) { at = i + 6; break; }
        REQUIRE(at > 0);
        const i32 v = 42;
        std::memcpy(&bytes[at], &v, 4);
        fixHeader(bytes);
        Profile q;
        CHECK(deserializeProfile(bytes.data(), bytes.size(), q));
        CHECK(q.settings.camera == 1);
    }
}

namespace {
// A synthetic game of 18 missions and 6 helicopters, a modified copy of the first game.
GameRules smallRules() {
    GameRules r = defaultGameRules();
    r.missionCount = 18;
    r.helicopterCount = 6;
    r.difficultyCount = 3;
    r.defaultDifficulty = 1;
    r.startLives = 4;
    r.difficulty[2] = {9.0f, 8.0f, 7.0f, 6.0f};
    return r;
}
} // namespace

TEST_CASE("profile: the first game's constants equal its rules") {
    const GameRules& d = defaultGameRules();
    CHECK(kMissionCount == d.missionCount);
    CHECK(kHelicopterCount == d.helicopterCount);
    CHECK(kDifficultyCount == d.difficultyCount);
    CHECK(kDefaultDifficulty == d.defaultDifficulty);
    CHECK(kCampaignStartLives == d.startLives);
    const Progress p = Progress::defaults();
    CHECK(p.missionCount == 20);
    CHECK(p.helicopterCount == 10);
}

TEST_CASE("profile: a game of 18 missions and 6 helicopters stays inside its counts") {
    static const GameRules rules = smallRules();
    Campaign c(rules);
    CHECK(c.difficulty == 1);
    CHECK(c.p[0].livesAtStart == 4);
    c.start(99, 99, 1);
    CHECK(c.mission == 17);
    CHECK(c.difficulty == 2);
    CHECK(c.p[1].livesAtStart == 4);
    CHECK(c.isLastMission());
    CHECK(c.nextMission() == 0);
    c.start(-3, -3, 2);
    CHECK(c.mission == 0);
    CHECK(c.difficulty == 0);
    CHECK(difficultyFactors(9, rules).rank == doctest::Approx(6.0f));
    CHECK(difficultyFactors(9).rank == doctest::Approx(1.3f)); // default rules: the first game's

    Progress p = Progress::defaults(rules);
    CHECK(p.missionCount == 18);
    CHECK(p.helicopterCount == 6);
    p.unlockAfterMission(16, 5);
    CHECK(p.missionUnlocked[17]);
    CHECK(p.helicopterUnlocked[5]);
    p.unlockAfterMission(17, 6); // helicopter 6 does not exist: nothing; next wraps to mission 1
    CHECK_FALSE(p.helicopterUnlocked[6]);
    CHECK_FALSE(p.missionUnlocked[18]);
    CHECK(p.missionUnlocked[0]);
    p.unlockAfterMission(-1, 10);
    CHECK_FALSE(p.missionUnlocked[18]);
    CHECK_FALSE(p.helicopterUnlocked[10]);
    for (int i = 6; i < kMaxHelicopters; i++) CHECK_FALSE(p.helicopterUnlocked[i]);
    for (int i = 18; i < kMaxMissions; i++) CHECK_FALSE(p.missionUnlocked[i]);
}

TEST_CASE("profile file: counts are data; a file of another game is rejected") {
    static const GameRules rules = smallRules();
    Profile small;
    small.progress = Progress::defaults(rules);
    small.progress.missionUnlocked[17] = true;
    small.progress.helicopterUnlocked[5] = true;
    const std::vector<u8> bytes = serializeProfile(small);
    const std::vector<u8> first = serializeProfile(Profile{});
    CHECK(bytes.size() == first.size() - 2 - 4); // 2 missions and 4 helicopters fewer
    Profile back;
    back.progress = Progress::defaults(rules);
    CHECK(deserializeProfile(bytes.data(), bytes.size(), back));
    CHECK(back.progress.missionCount == 18);
    CHECK(back.progress.missionUnlocked[17]);
    CHECK(back.progress.helicopterUnlocked[5]);
    // Read by the first game (20 missions, 10 helicopters): the file lacks two missions and
    // four helicopters, which take the defaults; what it has is kept.
    Profile wide;
    std::string why;
    CHECK_MESSAGE(deserializeProfile(bytes.data(), bytes.size(), wide, &why), why);
    CHECK(wide.progress.missionCount == 20);
    CHECK(wide.progress.missionUnlocked[17]);
    CHECK_FALSE(wide.progress.missionUnlocked[19]);
    CHECK(wide.progress.helicopterUnlocked[5]);
    // The other way round: the entries beyond the game's counts are ignored.
    Profile narrow;
    narrow.progress = Progress::defaults(rules);
    CHECK(deserializeProfile(first.data(), first.size(), narrow));
    CHECK(narrow.progress.missionCount == 18);
    CHECK_FALSE(narrow.progress.missionUnlocked[19]);
}
