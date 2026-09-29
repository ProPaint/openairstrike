// One game: data, the world, the fixed-timestep frame with pause, and the level flow
// (docs/spec/engine-behaviour.md 1.3). Two ways to use it:
//   - direct (`as3d_game --level N`, tests, the bot): init() loads a mission and, with
//     `levelFlow`, restarts it after a game over and continues after a mission complete;
//   - behind the front end (game_flow.h): init() with `startLevel = false` loads nothing,
//     the front end's host calls startMission() and loadAttract().
// No GL, no audio device: the executable (main.cpp) adds the window, the renderer and the
// sound around it, and tests drive it directly.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/input.h"
#include "as3d/profile.h"
#include "as3d/vfs.h"
#include "as3d/script.h"
#include "as3d/world.h"

namespace as3d {
class DefDatabase;
struct LevelDef;
}

namespace as3d_game {

using as3d::kMissionCount; // 20

struct GameOptions {
    std::string dataRoot;          // directory holding assets_extracted/
    // When not empty: the original pak archives to mount instead of assets_extracted/, in
    // this order (later paks override earlier ones, docs/spec/pak.md), opened through
    // as3d::openPlatformStream (file paths on desktop, APK assets on Android).
    std::vector<std::string> paks;
    int mission = 1;               // 1..20
    as3d::WorldConfig world;       // difficulty, seed, players (same defaults as as3d_sim)
    bool levelFlow = true;         // restart on game over, continue on mission complete
    int flowDelayFrames = 180;     // frames the finished level stays on screen first
    bool startLevel = true;        // false: init() loads no level (the front end starts them)
    // Single files outside the paks and assets_extracted/ (the main menu's logo lives only in
    // the install's data\gfx): game path -> path for as3d::readPlatformFile. Mounted under
    // everything else.
    std::vector<std::pair<std::string, std::string>> extraFiles;
};

// Everything a level start takes from the front end (frontend.md 5.1, 5.2).
struct LevelSetup {
    int mission = 1;               // 1..20
    int difficulty = 2;
    int players = 1;
    int heli[2] = {1, 0};
    int lives[2] = {2, 2};         // lives at level start
    long long banked[2] = {0, 0};  // banked score (the HUD shows p_scores + banked)
    int camera = 1;
};

class GameSession {
public:
    enum StepEvent : int {
        kNone = 0,
        kLevelStarted = 1,   // a new level was loaded during this step (renderer: beginLevel)
        kLevelComplete = 2,  // the level was completed during this step
        kGameOver = 4,       // the game was lost during this step
        kHintShown = 8,      // a tutorial hint appeared during this step
    };

    GameSession();
    ~GameSession();

    bool init(const GameOptions& options, std::string* error);

    // Behind the front end: frees the level and starts a mission with the level-start resets
    // of frontend.md 5.2 (the world is re-initialised with the setup's difficulty, players and
    // helicopters; lives and banked scores come from the setup). False if it cannot load.
    bool startMission(const LevelSetup& setup, std::string* error);
    // Loads an attract (intermission) level by levels.txt id, e.g. "intro2": no players.
    bool loadAttract(const std::string& id, std::string* error);
    bool hasLevel() const { return hasLevel_; }

    // One fixed simulation step. Applies the pause edge (the P key toggles the world's
    // pause; unpausing clears p_action of both players, 1.3 state 6), steps the world and
    // runs the level flow. Returns StepEvent bits.
    int step(const as3d::FrameInput& input);

    as3d::World& world() { return *world_; }
    const as3d::World& world() const { return *world_; }
    as3d::Vfs& vfs() { return vfs_; }
    const as3d::DefDatabase& db() const { return *db_; }
    int mission() const { return mission_; }
    // The levels.txt entry of the current level, or null.
    const as3d::LevelDef* levelDef() const;
    std::string musicPath() const;
    std::string levelName() const;
    // Frames stepped since init (all levels).
    as3d::u32 totalFrames() const { return totalFrames_; }
    // HUD score: ftol(p_scores) + banked (engine-behaviour.md 10.4).
    long long displayScore(int player = 0) const;

private:
    bool startMission(int mission, std::string* error);
    void bankScores();
    void resetFlow();

    GameOptions options_;
    as3d::Vfs vfs_;
    std::unique_ptr<as3d::DefDatabase> db_;
    std::unique_ptr<as3d::World> world_;
    int mission_ = 1;
    bool hasLevel_ = false;
    int flowTimer_ = 0;
    bool wasComplete_ = false, wasGameOver_ = false;
    as3d::script::u64 hintsSeen_ = 0;
    as3d::u32 totalFrames_ = 0;
};

} // namespace as3d_game
