#include "game_session.h"

#include <algorithm>
#include <cmath>

#include "as3d/defs.h"
#include "as3d/platform.h"

namespace as3d_game {

using namespace as3d;

GameSession::GameSession() = default;
GameSession::~GameSession() {
    world_.reset(); // before the definitions it points into
}

bool GameSession::init(const GameOptions& options, std::string* error) {
    options_ = options;
    std::string root = options.dataRoot.empty() ? "." : options.dataRoot;
    std::string where = root + "/assets_extracted";
    if (options.paks.empty()) {
        vfs_.mount(makeDirSource(where));
    } else {
        where = "the pak archives";
        for (const std::string& pak : options.paks) {
            std::unique_ptr<IStream> stream = openPlatformStream(pak);
            std::unique_ptr<IFileSource> source = stream ? makePakSource(std::move(stream)) : nullptr;
            if (!source) {
                if (error) *error = "cannot mount " + pak;
                return false;
            }
            vfs_.mount(std::move(source));
        }
    }
    db_.reset(new DefDatabase());
    if (!db_->load(vfs_)) {
        if (error) *error = "cannot load definitions from " + where;
        return false;
    }
    world_.reset(new World());
    world_->init(vfs_, *db_, options.world);
    return startMission(std::min(std::max(options.mission, 1), kMissionCount), error);
}

bool GameSession::startMission(int mission, std::string* error) {
    mission_ = mission;
    flowTimer_ = 0;
    wasComplete_ = wasGameOver_ = false;
    hintsSeen_ = world_->hintsShown();
    return world_->loadLevel(std::to_string(mission), error);
}

const LevelDef* GameSession::levelDef() const {
    const Terrain* t = world_ ? world_->terrain() : nullptr;
    if (!t || !db_) return nullptr;
    for (const LevelDef& d : db_->levels()) {
        if (d.id == t->style().id) return &d;
    }
    return nullptr;
}

std::string GameSession::musicPath() const {
    const LevelDef* d = levelDef();
    return d ? d->music : std::string();
}

std::string GameSession::levelName() const {
    const LevelDef* d = levelDef();
    return d ? (d->name.empty() ? d->id : d->name) : std::string();
}

long long GameSession::displayScore(int player) const {
    const PlayerRecord& pr = world_->player(player);
    float s = pr.scores;
    long long v = (s > -2.0e9f && s < 2.0e9f) ? static_cast<long long>(s) : 0;
    return v + pr.banked;
}

void GameSession::bankScores() {
    // "Continue" of the mission-complete screen (engine-behaviour.md 10.4).
    World& w = *world_;
    for (int p = 0; p < w.numPlayers(); ++p) {
        PlayerRecord& pr = w.player(p);
        double banked = static_cast<double>(pr.banked) + static_cast<double>(pr.scores);
        pr.banked = static_cast<int>(std::max(-2.0e9, std::min(2.0e9, banked)));
        float stars = w.starTotal() > 0 ? pr.stars / static_cast<float>(w.starTotal()) : 0.0f; // guarded
        float score = w.maxLevelScore() > 0.0f ? 0.5f * pr.scores / w.maxLevelScore() : 0.0f;
        pr.rankAccumulator += stars + score;
        pr.livesAtStart = static_cast<int>(std::max(0.0f, std::min(pr.lives, 99.0f)));
    }
}

int GameSession::step(const FrameInput& input) {
    World& w = *world_;
    int events = kNone;
    // The P / Pause key (1.3 state 6). Ignored while a hint box or a level-end state owns
    // the pause.
    if (input.pausePressed && !w.hintShowing() && !w.levelComplete() && !w.gameOver()) {
        bool nowPaused = !w.paused();
        w.setPaused(nowPaused);
        if (!nowPaused) {
            for (int p = 0; p < kMaxPlayers; ++p) w.player(p).action = 0.0f;
        }
    }
    w.step(input.toPlayerInput());
    ++totalFrames_;
    if (w.hintsShown() != hintsSeen_) {
        hintsSeen_ = w.hintsShown();
        events |= kHintShown;
    }
    if (w.levelComplete() && !wasComplete_) {
        wasComplete_ = true;
        events |= kLevelComplete;
    }
    if (w.gameOver() && !wasGameOver_) {
        wasGameOver_ = true;
        events |= kGameOver;
    }
    if (options_.levelFlow && (wasComplete_ || wasGameOver_)) {
        if (++flowTimer_ >= options_.flowDelayFrames) {
            std::string err;
            int next = mission_;
            if (wasComplete_) {
                // Continue: bank the score, next mission. After the last mission the
                // campaign starts over (the congratulations screen is not implemented).
                bankScores();
                next = mission_ >= kMissionCount ? 1 : mission_ + 1;
            }
            // Game over: Restart reloads the mission with the lives it started with.
            if (!startMission(next, &err)) {
                AS3D_ERROR("cannot start mission %d: %s", next, err.c_str());
            } else {
                events |= kLevelStarted;
            }
        }
    }
    return events;
}

} // namespace as3d_game
