// The games the engine runs (as3d/game_profile.h).
//
// AirStrike 3D's values are those of the base specs. The sequels' entries hold what is known
// from their data (counts, pak lists, helicopter objects); their rule constants equal the
// first game's until the delta specs in docs/spec/as2 and docs/spec/gulf say otherwise.
#include "as3d/game_profile.h"

namespace as3d {

namespace {

const char* const kHeliAs3d[] = {
    "p_apache",        "p_comanche",      "p_apache_white",  "p_apache_impala", "p_comanche_white",
    "p_comanche_lava", "p_apache_blue",   "p_comanche_sand", "p_comanche_blue", "p_comanche_green",
};
// The executable's table order (as2/engine-behaviour.delta.md 7.6): `enableHelic n` of
// levels.txt unlocks entry n.
const char* const kHeliAs2[] = {"player_1", "player_2", "player_4", "player_6", "player_5", "player_3"};

// as2/engine-behaviour.delta.md 8.2: upgrade levels per mission. Slots: 0 machine gun,
// 1 impulse gun, 2 plasma gun, 3 laser, 4 big laser, 5 lightning gun, 6 wave gun,
// 7 missile gun, 8 flamethrower.
const int kLoadoutAs2[18][kMaxWeaponSlots] = {
    {1},                         // 1
    {4, 3},                      // 2
    {4, 5},                      // 3
    {4, 5, 3},                   // 4
    {0, 5, 7},                   // 5
    {0, 5, 7, 3},                // 6
    {0, 0, 7, 3, 0, 0, 0, 5},    // 7
    {0, 0, 7, 3, 0, 0, 0, 5},    // 8
    {0, 0, 7, 5, 0, 0, 0, 5},    // 9
    {0, 0, 7, 8, 0, 0, 3, 5},    // 10
    {0, 0, 7, 8, 0, 0, 4, 5},    // 11
    {0, 0, 7, 8, 0, 5, 4, 5},    // 12
    {0, 0, 7, 8, 3, 5, 4, 5},    // 13
    {0, 0, 7, 8, 5, 5, 4, 5},    // 14
    {0, 0, 7, 8, 5, 5, 4, 5, 3}, // 15
    {0, 0, 7, 8, 5, 5, 4, 5, 3}, // 16
    {0, 0, 7, 8, 5, 5, 4, 5, 3}, // 17
    {0, 0, 7, 8, 5, 5, 4, 5, 3}, // 18
};
const char* const kHeliGulf[] = {"player_1", "player_2", "player_3"};

GameRules rulesAs3d() {
    GameRules r;
    r.missionCount = 20;
    r.attractCount = 4;
    r.helicopterCount = 10;
    r.heliObjects = kHeliAs3d;
    r.difficultyCount = 5;
    r.defaultDifficulty = 2;
    r.difficulty[0] = {0.3f, 0.5f, 0.6f, 0.7f};
    r.difficulty[1] = {0.5f, 0.7f, 0.8f, 0.85f};
    r.difficulty[2] = {0.75f, 0.8f, 1.0f, 1.07f};
    r.difficulty[3] = {1.5f, 1.25f, 1.2f, 1.2f};
    r.difficulty[4] = {2.0f, 1.4f, 1.4f, 1.3f};
    r.startLives = 2;
    r.scrollSpeed = 42.0f;
    r.startMapPos = 32.0f;
    r.startCameraX = 640.0f;
    r.cameraMinX = kGameCameraMinX;
    r.cameraMaxX = kGameCameraMaxX;
    r.cameraFollow = 48.0f;
    r.playerClampMargin = 10.0f;
    r.cameraModeCount = 4;
    r.defaultCameraMode = 1;
    for (int i = 0; i < 4; ++i) r.cameraModes[i] = kGameCameraPresets[i];
    r.scoreDigitObject = "score_num";
    r.starItemObject = "item_star";
    r.healthBarEmptyObject = "hbar_empty";
    r.healthBarFullObject = "hbar_full";
    r.weaponSlots = 20;
    r.deathLosesWeaponLevel = true;
    r.respawnChecksLives = true;
    r.lifeIconsMax = 5;
    return r;
}

// docs/spec/as2/engine-behaviour.delta.md, "Values for GameRules". What that delta found
// unchanged (difficulty table, scroll, camera, clamps, native object names) is inherited.
GameRules rulesAs2() {
    GameRules r = rulesAs3d();
    r.missionCount = 18;
    r.attractCount = 2;
    r.helicopterCount = 6;
    r.heliObjects = kHeliAs2;
    r.bonusMissions[0] = 7;
    r.bonusMissions[1] = 13;
    r.bossMissions[0] = 6;
    r.bossMissions[1] = 12;
    r.bossMissions[2] = 18;
    r.terraMorph = true;
    r.civilians = true;
    r.skidMarks = true;
    r.waterFlags = true;
    r.coop = true;
    r.weaponSlots = 9;
    r.missionLoadout = kLoadoutAs2;
    r.upgradesCarryToNextMission = true;
    r.deathLosesWeaponLevel = false;
    r.powerUpCycleSkip = (1u << 6) | (1u << 7) | (1u << 9);
    r.accelInput = true;
    r.mouseAccel = 2.0f;
    r.mouseControlDefault = true;
    r.respawnChecksLives = false;
    r.clampPlayerHealthToMax = true;
    r.healthBarScaleFromMax = true;
    r.lifeIconsMax = 10;
    r.deadShootersBlocked = true;
    r.touchModeBits = true;
    r.killCapAtEnemyTotal = true;
    r.statsOnlyOnePlayer = true;
    r.campaignCheckpoint = true;
    r.lightningEffectObject = "wavegun_hit";
    r.lightningEffectInterval = 0.2f;
    r.lightningTimerCap = 5.0f;
    r.spawnAllOnIntermission = true;
    r.spawnDuringLoad = true;
    r.waterFollowsWaves = true;
    r.skidTrailPool = 64;
    r.skidNodeInterval = 5.0f / 12.0f;
    r.skidMaxNodes = 23;
    r.skidLife = 10.0f;
    r.skidFadeStart = 5.0f;
    r.skidHeightOffset = 2.0f;
    return r;
}

// Until docs/spec/gulf exists: AirStrike 2's rules with Gulf Thunder's counts. Its mission
// loadout table is not known yet, so none is set.
GameRules rulesGulf() {
    GameRules r = rulesAs2();
    r.missionCount = 24;
    r.helicopterCount = 3;
    r.heliObjects = kHeliGulf;
    r.missionLoadout = nullptr;
    for (int& m : r.bonusMissions) m = 0;
    for (int& m : r.bossMissions) m = 0;
    r.bonusMissions[0] = 11;
    r.bonusMissions[1] = 18;
    r.bossMissions[0] = 10;
    r.bossMissions[1] = 24;
    return r;
}

const GameProfile kProfiles[kGameCount] = {
    {GameId::AirStrike3D, "as3d", "AirStrike 3D", "1.70", "AirStrike3D.exe",
     {"pak0.apk", "pak1.apk", "pak2.apk", nullptr}, "texts_v170.txt", "v170", 24,
     FrontendStyle::V170Menus, rulesAs3d()},
    {GameId::AirStrike2, "as2", "AirStrike 2", "2.51", "AirStrike3D II.exe",
     {"pak0.apk", "pak1.apk", "pak2.apk", nullptr}, "texts_as2.txt", "v251", 28,
     FrontendStyle::SequelMenus, rulesAs2()},
    {GameId::GulfThunder, "gulf", "AirStrike II: Gulf Thunder", "2.71", "AirStrike3D II - Gulf.exe",
     {"pak0.apk", "pak1.apk", "pak2.apk", "pak4.apk", nullptr}, "texts_gulf.txt", "v271", 28,
     FrontendStyle::PlainList, rulesGulf()},
};

} // namespace

const GameProfile& gameProfile(GameId id) {
    int i = static_cast<int>(id);
    return kProfiles[i >= 0 && i < kGameCount ? i : 0];
}

const GameProfile* findGameProfile(std::string_view key) {
    for (const GameProfile& p : kProfiles)
        if (key == p.key) return &p;
    return nullptr;
}

const GameRules& defaultGameRules() { return kProfiles[0].rules; }

} // namespace as3d
