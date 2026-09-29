// Which game the engine is running, and what differs between the games of the family.
//
// One binary runs AirStrike 3D v1.70, AirStrike 2 and AirStrike II: Gulf Thunder; the game is
// chosen once at start from the data present. Everything that used to be a literal of the
// first game (counts, object names, rule constants, pak names) lives here. Specs and their
// precedence per game: docs/spec/README.md, "Games and which spec applies".
//
// Orchestrator-owned header. tools/games.json carries the same keys, titles and pak lists for
// the shell and Python tools; apps/tests/game_profile_test.cpp checks that both agree.
#pragma once

#include <string_view>

#include "as3d/core.h"
#include "as3d/game_camera.h"

namespace as3d {

enum class GameId : u8 { AirStrike3D = 0, AirStrike2 = 1, GulfThunder = 2 };
constexpr int kGameCount = 3;

// Upper bounds over all games, for fixed-size arrays that carry a runtime count.
constexpr int kMaxMissions = 32;
constexpr int kMaxHelicopters = 16;
constexpr int kMaxDifficulties = 8;
constexpr int kMaxCameraModes = 8;
constexpr int kMaxPaks = 8;
constexpr int kMaxSpecialMissions = 8;
constexpr int kMaxWeaponSlots = 20;

// engine-behaviour.md 6.3: factors on enemy health, enemy damage, score and rank.
struct DifficultyRow {
    float health, damage, score, rank;
};

// Native rules that differ, or may differ, between the games. Values of a sequel come from
// its delta specs; a value that a delta does not list as changed equals the first game's.
struct GameRules {
    // Campaign.
    int missionCount = 0;
    int attractCount = 0;              // intro maps behind the menus
    int helicopterCount = 0;
    const char* const* heliObjects = nullptr; // object definition of helicopter 0..count-1
    int difficultyCount = 0;
    int defaultDifficulty = 0;
    DifficultyRow difficulty[kMaxDifficulties] = {};
    int startLives = 0;
    int bonusMissions[kMaxSpecialMissions] = {}; // 1-based, 0-terminated
    int bossMissions[kMaxSpecialMissions] = {};  // 1-based, 0-terminated

    // Level runtime (engine-behaviour.md 3 and 7).
    float scrollSpeed = 0;      // map units per second at scroll factor 1
    float startMapPos = 0;
    float startCameraX = 0;
    float cameraMinX = 0, cameraMaxX = 0;
    float cameraFollow = 0;     // the camera trails the player's x by at most this
    float playerClampMargin = 0;
    int cameraModeCount = 0;
    int defaultCameraMode = 0;
    CameraPreset cameraModes[kMaxCameraModes] = {};

    // Objects the native code refers to by name; nullptr = the game has none.
    const char* scoreDigitObject = nullptr;
    const char* starItemObject = nullptr;
    const char* healthBarEmptyObject = nullptr;
    const char* healthBarFullObject = nullptr;

    // Player and weapons (engine-behaviour.md 7 and 8; as2/engine-behaviour.delta.md 7, 8).
    int weaponSlots = 0;                 // upgrade slots G_GetUpgrade / G_SetUpgrade accept
    // Upgrade levels given per mission, missionCount rows of weaponSlots values, or nullptr:
    // then a level start clears the upgrades and gives slot 0 level 1.
    const int (*missionLoadout)[kMaxWeaponSlots] = nullptr;
    bool upgradesCarryToNextMission = false; // the loadout applies on new game and restart only
    bool deathLosesWeaponLevel = false;
    u32 powerUpCycleSkip = 0;            // bit n set: "next power-up" skips slot n
    bool accelInput = false;             // the engine builds the vector GetPlayerAccel returns
    float mouseAccel = 0;                // length of that vector under mouse control
    bool mouseControlDefault = false;
    bool respawnChecksLives = false;     // the native respawn refuses without lives
    bool clampPlayerHealthToMax = false; // health is cut to the definition's every frame
    bool healthBarScaleFromMax = false;  // HUD fill = health / maximum; else health / 400
    int lifeIconsMax = 0;

    // Combat and statistics.
    bool deadShootersBlocked = false;    // Shoot does nothing for a dead shooter
    bool touchModeBits = false;          // touch modes are a bit set, dead candidates skipped
    bool killCapAtEnemyTotal = false;
    bool statsOnlyOnePlayer = false;
    bool campaignCheckpoint = false;     // EndLevel stores lives, score and rank for "Continue"
    const char* lightningEffectObject = nullptr; // spawned on a struck target
    float lightningEffectInterval = 0;   // seconds between two of them on one target
    float lightningTimerCap = 0;

    // Level start.
    bool spawnAllOnIntermission = false; // attract levels spawn every placed object at once
    bool spawnDuringLoad = false;        // one spawner call and entity pass inside level start,
                                         // before the counters are reset (as2 issue 211)

    // Water and tyre tracks.
    bool waterFollowsWaves = false;      // FL_ONWATER uses the animated surface
    int skidTrailPool = 0;
    float skidNodeInterval = 0;          // seconds between two fixed nodes of a trail
    int skidMaxNodes = 0;
    float skidLife = 0, skidFadeStart = 0;
    float skidHeightOffset = 0;

    // Features only some games have.
    bool terraMorph = false; // run-time terrain deformation
    bool civilians = false;
    bool skidMarks = false;
    bool waterFlags = false; // FL_ONWATER_NORMAL / FL_ONWATER_FLAT
    bool coop = false;       // "Cooperative" in place of "2 Players"
};

enum class FrontendStyle : u8 {
    V170Menus,   // the first game's menus (frontend.md)
    PlainList,   // ours: a plain mission list, until a sequel's own menus exist
    SequelMenus, // the sequels' menus and comic panels
};

struct GameProfile {
    GameId id = GameId::AirStrike3D;
    const char* key = "";       // "as3d", "as2", "gulf": directories, AS3D_GAME, --game, ?game=
    const char* title = "";
    const char* version = "";   // of the executable the specs were taken from
    const char* exeName = "";
    const char* paks[kMaxPaks + 1] = {}; // mount order, later ones override; nullptr-terminated
    const char* textsFile = ""; // texts extracted from the user's own executable
    const char* builtinSet = ""; // name of the builtin table and arity list
    int globalCount = 0;        // script globals of the executable
    FrontendStyle frontend = FrontendStyle::V170Menus;
    GameRules rules;
};

const GameProfile& gameProfile(GameId id);
// nullptr if the key names no game.
const GameProfile* findGameProfile(std::string_view key);
// The first game's rules: the default wherever none is given, so that code and tests written
// before there were several games behave as they did.
const GameRules& defaultGameRules();

} // namespace as3d
