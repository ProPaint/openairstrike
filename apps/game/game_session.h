// One game: data, the world, the fixed-timestep frame with pause, and the level flow
// (docs/spec/engine-behaviour.md 1.3): restart after game over, next mission after a
// mission is complete. No GL, no audio device: the executable (main.cpp) adds the window,
// the renderer and the sound around it, and tests drive it directly.
#pragma once

#include <memory>
#include <string>

#include "as3d/core.h"
#include "as3d/input.h"
#include "as3d/vfs.h"
#include "as3d/script.h"
#include "as3d/world.h"

namespace as3d {
class DefDatabase;
struct LevelDef;
}

namespace as3d_game {

constexpr int kMissionCount = 20;

struct GameOptions {
    std::string dataRoot;          // directory holding assets_extracted/
    int mission = 1;               // 1..20
    as3d::WorldConfig world;       // difficulty, seed, players (same defaults as as3d_sim)
    bool levelFlow = true;         // restart on game over, continue on mission complete
    int flowDelayFrames = 180;     // frames the finished level stays on screen first
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

    GameOptions options_;
    as3d::Vfs vfs_;
    std::unique_ptr<as3d::DefDatabase> db_;
    std::unique_ptr<as3d::World> world_;
    int mission_ = 1;
    int flowTimer_ = 0;
    bool wasComplete_ = false, wasGameOver_ = false;
    as3d::script::u64 hintsSeen_ = 0;
    as3d::u32 totalFrames_ = 0;
};

} // namespace as3d_game
