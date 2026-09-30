// as3d/game_profile.h: the table of games, and its agreement with tools/games.json.
#include <doctest.h>

#include <cstdio>
#include <string>

#include "as3d/game_profile.h"

using namespace as3d;

namespace {
std::string readRepoFile(const char* rel) {
    std::string path = std::string(AS3D_REPO_ROOT) + "/" + rel;
    std::string out;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
        std::fclose(f);
    }
    return out;
}

// The text of the JSON object of one game: from its "key" to the next one.
std::string gameBlock(const std::string& json, const char* key) {
    size_t a = json.find("\"key\": \"" + std::string(key) + "\"");
    if (a == std::string::npos) return {};
    size_t b = json.find("\"key\":", a + 1);
    return json.substr(a, b == std::string::npos ? std::string::npos : b - a);
}
} // namespace

TEST_CASE("game profiles: keys, lookup, the first game's rules") {
    CHECK(findGameProfile("as3d") == &gameProfile(GameId::AirStrike3D));
    CHECK(findGameProfile("as2") == &gameProfile(GameId::AirStrike2));
    CHECK(findGameProfile("gulf") == &gameProfile(GameId::GulfThunder));
    CHECK(findGameProfile("") == nullptr);
    CHECK(findGameProfile("AS3D") == nullptr);

    const GameRules& r = defaultGameRules();
    CHECK(&r == &gameProfile(GameId::AirStrike3D).rules);
    CHECK(r.missionCount == 20);
    CHECK(r.helicopterCount == 10);
    CHECK(std::string(r.heliObjects[0]) == "p_apache");
    CHECK(std::string(r.heliObjects[9]) == "p_comanche_green");
    CHECK(r.difficultyCount == 5);
    CHECK(r.difficulty[2].score == 1.0f);
    CHECK(r.scrollSpeed == 42.0f);
    CHECK(r.cameraModeCount == 4);
    CHECK(r.cameraModes[3].height == 370.0f);
    CHECK_FALSE(r.terraMorph);
    CHECK(r.weaponSlots == 20);
    CHECK(r.missionLoadout == nullptr);
    CHECK(r.respawnChecksLives);
    CHECK_FALSE(r.accelInput);
    CHECK_FALSE(r.touchModeBits);

    const GameRules& a = gameProfile(GameId::AirStrike2).rules;
    CHECK(a.weaponSlots == 9);
    REQUIRE(a.missionLoadout != nullptr);
    CHECK(a.missionLoadout[0][0] == 1);
    CHECK(a.missionLoadout[17][8] == 3);
    CHECK(std::string(a.heliObjects[2]) == "player_4");
    CHECK(a.difficulty[4].health == r.difficulty[4].health);
    CHECK(a.scrollSpeed == r.scrollSpeed);

    const GameRules& g = gameProfile(GameId::GulfThunder).rules;
    REQUIRE(g.missionLoadout != nullptr);
    CHECK(g.missionLoadout[0][0] == 4);
    CHECK(g.missionLoadout[23][2] == 7);
    CHECK(g.missionLoadout[2][4] == 3);
}

TEST_CASE("game profiles: every game fits the maximum sizes") {
    for (int i = 0; i < kGameCount; ++i) {
        const GameProfile& p = gameProfile(static_cast<GameId>(i));
        INFO(p.key);
        CHECK(static_cast<int>(p.id) == i);
        CHECK(p.rules.missionCount > 0);
        CHECK(p.rules.missionCount <= kMaxMissions);
        CHECK(p.rules.helicopterCount <= kMaxHelicopters);
        CHECK(p.rules.difficultyCount <= kMaxDifficulties);
        CHECK(p.rules.cameraModeCount <= kMaxCameraModes);
        REQUIRE(p.rules.heliObjects != nullptr);
        for (int h = 0; h < p.rules.helicopterCount; ++h) CHECK(p.rules.heliObjects[h] != nullptr);
        int paks = 0;
        while (paks <= kMaxPaks && p.paks[paks]) ++paks;
        CHECK(paks >= 1);
        CHECK(paks <= kMaxPaks);
        CHECK(p.rules.weaponSlots >= 1);
        CHECK(p.rules.weaponSlots <= kMaxWeaponSlots);
        if (p.rules.missionLoadout) {
            // Every mission's loadout owns at least one weapon, inside the slots.
            for (int m = 0; m < p.rules.missionCount; ++m) {
                int owned = 0;
                for (int w = 0; w < kMaxWeaponSlots; ++w) {
                    int level = p.rules.missionLoadout[m][w];
                    CHECK(level >= 0);
                    if (w >= p.rules.weaponSlots) CHECK(level == 0);
                    owned += level > 0;
                }
                CHECK(owned >= 1);
            }
        }
        for (int m : p.rules.bonusMissions) CHECK(m <= p.rules.missionCount);
        for (int m : p.rules.bossMissions) CHECK(m <= p.rules.missionCount);
    }
}

TEST_CASE("game profiles agree with tools/games.json") {
    std::string json = readRepoFile("tools/games.json");
    REQUIRE_FALSE(json.empty());
    for (int i = 0; i < kGameCount; ++i) {
        const GameProfile& p = gameProfile(static_cast<GameId>(i));
        INFO(p.key);
        std::string g = gameBlock(json, p.key);
        REQUIRE_FALSE(g.empty());
        CHECK(g.find("\"title\": \"" + std::string(p.title) + "\"") != std::string::npos);
        CHECK(g.find("\"version\": \"" + std::string(p.version) + "\"") != std::string::npos);
        CHECK(g.find("\"exe\": \"" + std::string(p.exeName) + "\"") != std::string::npos);
        CHECK(g.find("\"texts\": \"" + std::string(p.textsFile) + "\"") != std::string::npos);
        CHECK(g.find("\"missions\": " + std::to_string(p.rules.missionCount)) != std::string::npos);
        CHECK(g.find("\"helicopters\": " + std::to_string(p.rules.helicopterCount)) != std::string::npos);
        std::string paks = "\"paks\": [";
        for (int k = 0; p.paks[k]; ++k) paks += std::string(k ? ", " : "") + "\"" + p.paks[k] + "\"";
        paks += "]";
        CHECK(g.find(paks) != std::string::npos);
    }
}
