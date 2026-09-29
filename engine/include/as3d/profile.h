// Progression and persistence: campaign rules (carry-over, banking, lives, rank), unlocks, the
// high-score table and settings. Spec: docs/spec/frontend.md 5 and 6.
//
// The original's game.bin / config.ini are not used: our own small versioned file holds both
// the progress and the settings (format below). Online high scores and the CD check are dropped.
// Nothing here depends on the game world; the game loop hands in plain numbers.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "as3d/core.h"

namespace as3d {

constexpr int kMissionCount = 20;
constexpr int kHelicopterCount = 10;
constexpr int kHighScoreCount = 15;
constexpr int kRankCount = 7;
constexpr int kDifficultyCount = 5;
constexpr int kDefaultDifficulty = 2; // Normal
constexpr int kCampaignStartLives = 2; // the current helicopter plus two spares

// ---------------------------------------------------------------------------
// Difficulty (engine-behaviour.md 6.3) and rank (frontend.md 5.11)
// ---------------------------------------------------------------------------
struct DifficultyFactors {
    float health, damage, score, rank;
};
const DifficultyFactors& difficultyFactors(int index); // clamped to 0..4

// Rank index from a value v: 0 "Cheater" if a cheat was used, else the first i in 1..5 with
// v < threshold[i] (3, 7.5, 14, 22, 30), else 6.
int rankIndex(double v, bool cheatUsed);

// ---------------------------------------------------------------------------
// Campaign (runtime only, never saved: frontend.md 6.1)
// ---------------------------------------------------------------------------
struct CampaignPlayer {
    std::int64_t banked = 0;   // banked score (+0x165)
    double rankAccumulator = 0; // (+0x169)
    int livesAtStart = kCampaignStartLives; // lives-at-level-start (+0x94)
};

// What the game reports about one player at the end of a level (or at Game Over -> Quit).
struct LevelPlayerResult {
    double score = 0;  // p_scores of the level
    int lives = 0;     // p_lives
    double stars = 0;  // p_stars
    int kills = 0;     // kills this level
};

// Level-wide totals (frontend.md 5.5).
struct LevelTotals {
    int starTotal = 0;
    double maxScore = 0;
    int enemyTotal = 0;
};

struct Campaign {
    bool active = false;
    int mission = 0;       // 0..19
    int difficulty = kDefaultDifficulty;
    int players = 1;       // 1 or 2
    CampaignPlayer p[2];

    // New campaign (0x408c40): banked score and rank accumulator cleared, lives-at-start 2.
    void start(int missionIndex, int difficultyIndex, int playerCount);
    // G_BankScore (frontend.md 5.4), for the `players` records. Zero denominators count as 0.
    void bank(const LevelPlayerResult results[2], const LevelTotals& totals);
    // Rank value shown on Mission / Game Complete: the mission's ratios plus the accumulator,
    // before banking, times the rank factor (player 1).
    double missionRankValue(const LevelPlayerResult& r, const LevelTotals& totals) const;
    // Rank value stored with a high score: accumulator (after banking) times the rank factor.
    double highScoreRankValue() const;
    // Next mission after Continue (mission index + 1, mod 20).
    int nextMission() const { return (mission + 1) % kMissionCount; }
};

// ---------------------------------------------------------------------------
// Persistent progress (frontend.md 6.1)
// ---------------------------------------------------------------------------
struct HighScore {
    std::string name; // up to 31 characters
    std::int64_t score = 0;
    int rank = 0;     // rank index 0..6
};

struct Progress {
    HighScore scores[kHighScoreCount];
    bool helicopterUnlocked[kHelicopterCount] = {};
    bool missionUnlocked[kMissionCount] = {};

    static Progress defaults(); // the compiled-in table, helicopters 0-1 and missions 1-2

    // Slot the score would take (first entry whose score <= score), or -1 if it does not
    // qualify (frontend.md 3.12).
    int qualifyingSlot(std::int64_t score) const;
    // Inserts at the qualifying slot, shifting the rest down (the last is dropped). Returns the
    // slot or -1.
    int insert(const std::string& name, std::int64_t score, int rank);
    // EndLevel unlocks (frontend.md 5.3): helicopter `enableHelic` if 0..9, and the next mission.
    void unlockAfterMission(int missionIndex, int enableHelic);
};

// ---------------------------------------------------------------------------
// Settings (frontend.md 6.2), with the ranges of the menus and fresh-install defaults
// ---------------------------------------------------------------------------
enum class Action {
    PrimaryAttack, SwitchWeapon, MissileAttack, SwitchMissiles, UseItem, SwitchItem,
    MoveForward, MoveBackward, MoveLeft, MoveRight, Count
};
constexpr int kActionCount = static_cast<int>(Action::Count);
u32 actionBit(Action a); // p_action bit (engine-behaviour.md 7.2)

// Settings::screenMode: Wide = the 3D world fills the whole screen; 4:3 = everything is drawn
// in the centred 4:3 area with black bars, as the original's aspect.
constexpr int kScreenWide = 0;
constexpr int kScreen4x3 = 1;

// Settings::touchSpeed: steps of the touch drag gain (as3d/input.h touchSpeedFactor); step 0 is
// the original feel of WP-48, the default a tad faster.
constexpr int kTouchSpeedSteps = 5;
constexpr int kDefaultTouchSpeed = 1;

struct Settings {
    bool showHints = false;
    bool showLogo = true;
    bool useSystemMouse = false;
    int camera = 1;            // 0..3: Low Pitch, Default, High Pitch, Top-Down
    bool mouseControl = false;
    int videoMode = 1;         // 0..8 (desktop only)
    int refreshRate = 0;       // 0 = default, else Hz (desktop only)
    int colorDepth = 0;        // 0, 16, 32 (desktop only)
    bool fullscreen = true;    // desktop only
    bool vsync = false;
    float brightness = 0.6f;   // 0.2..1.0
    float sfxVolume = 0.5f;    // 0..1
    float musicVolume = 0.5f;  // 0..1
    bool sound3D = false;
    int textureFilter = 0;     // 0 bilinear, 1 trilinear
    bool showFps = false;
    // Ours, not in the original (docs/spec/issues/140): how the world fills a screen wider than
    // 4:3, and which thumb the touch buttons are for.
    int screenMode = kScreenWide;  // kScreenWide or kScreen4x3
    bool leftHanded = false;       // touch buttons mirrored to the left side
    int touchSpeed = kDefaultTouchSpeed; // 0..kTouchSpeedSteps-1, the touch drag gain step
    // Two key codes per action and player; 0 = unbound.
    int keys[2][kActionCount][2] = {};

    static Settings defaults();
    // Brings every value into its range (invalid ones take their default, like the original).
    void clampToRanges();
    // Mouse Control rebinding (frontend.md 3.6): On binds mouse 1-3 to player 1's fire,
    // missile and power-up, Off joy 1-3; the code is removed from every other slot.
    void applyMouseControlBindings();
    // Binds `code` to (player, action) as the controls screen does: removed from every other
    // slot of both players (ours: from the stored bindings too), key 2 = old key 1.
    void bindKey(int player, Action a, int code);
    // Backspace/Delete on a row (frontend.md 3.7): key 1 = unbound, key 2 = the old key 1.
    void unbindKey(int player, Action a);
};

// Fresh-profile key bindings of the web version (docs/spec/issues/150): browsers keep Ctrl+W,
// Ctrl+T and Ctrl+N for themselves, so the built-in Ctrl fire key would close the tab. Player 1
// and player 2 fire with Space, launch missiles with X and use items with C; the second slots
// (joystick codes) and every other key stay as in defaults(). Rebinding works as usual.
void applyWebKeyBindings(Settings& s);

constexpr const char* kVideoModeNames[9] = {"640 x 480", "800 x 600", "1024 x 768", "1152 x 864", "1280 x 960",
                                            "1280 x 1024", "1600 x 1200", "1920 x 1440", "2048 x 1536"};

// ---------------------------------------------------------------------------
// Profile file
// ---------------------------------------------------------------------------
struct Profile {
    Progress progress = Progress::defaults();
    Settings settings = Settings::defaults();
};

// File format (little-endian), version 1:
//   "AS3DPROF" | u32 version | u32 payload size | u32 CRC-32 of the payload | payload
//   payload = chunks { char tag[4] | u32 size | data }, unknown tags skipped:
//     "PROG": u8 count (15) of { u8 name length (<= 31), name, i64 score, u8 rank };
//             u8 count (10) of u8 helicopter flags; u8 count (20) of u8 mission flags
//     "SETT": u16 count of { u8 key length, key, i32 value }  (floats stored x 1000)
// Every length and count is checked against what remains before anything is allocated; a bad
// header or CRC gives the defaults, a bad chunk gives that chunk's defaults.
std::vector<u8> serializeProfile(const Profile& p);
// Returns false (and leaves `out` = defaults for whatever could not be read) on any problem;
// `why` says what.
bool deserializeProfile(const u8* data, size_t size, Profile& out, std::string* why = nullptr);

// File helpers. The path is supplied by the platform layer (the user data directory). Saving
// writes `<path>.tmp` and renames it over `path`, so a crash never leaves a half-written file.
bool loadProfileFile(const std::string& path, Profile& out, std::string* why = nullptr);
bool saveProfileFile(const std::string& path, const Profile& p, std::string* why = nullptr);

} // namespace as3d
