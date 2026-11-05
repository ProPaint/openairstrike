#include "game_session.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "as3d/defs.h"
#include "as3d/game_data.h"
#include "as3d/platform.h"

namespace as3d_game {

using namespace as3d;

namespace {

// Serves single files through as3d::readPlatformFile (GameOptions::extraFiles).
class PlatformFileSource final : public IFileSource {
public:
    explicit PlatformFileSource(const std::vector<std::pair<std::string, std::string>>& files) {
        for (const auto& f : files) files_.emplace_back(normalizePath(f.first), f.second);
    }
    bool exists(const std::string& path) override {
        Blob b;
        return read(path, b);
    }
    bool read(const std::string& path, Blob& out) override {
        for (const auto& f : files_)
            if (f.first == path) return readPlatformFile(f.second, out);
        return false;
    }
    void list(std::vector<std::string>& out) override {
        for (const auto& f : files_) out.push_back(f.first);
    }

private:
    std::vector<std::pair<std::string, std::string>> files_;
};

} // namespace

bool mountGameFiles(const GameOptions& options, Vfs& vfs, std::string* where, std::string* error) {
    std::string root = options.dataRoot.empty() ? "." : options.dataRoot;
    std::string w = options.extractedDir;
    if (w.empty()) w = options.game ? locateGameData(root, *options.game).extractedDir : root + "/assets_extracted";
    if (!options.extraFiles.empty()) vfs.mount(std::unique_ptr<IFileSource>(new PlatformFileSource(options.extraFiles)));
    if (options.paks.empty()) {
        vfs.mount(makeDirSource(w));
    } else {
        w = "the pak archives";
        for (const std::string& pak : options.paks) {
            std::unique_ptr<IStream> stream = openPlatformStream(pak);
            std::unique_ptr<IFileSource> source = stream ? makePakSource(std::move(stream)) : nullptr;
            if (!source) {
                if (error) *error = "cannot mount " + pak;
                return false;
            }
            vfs.mount(std::move(source));
        }
    }
    if (where) *where = w;
    return true;
}

GameSession::GameSession() = default;
GameSession::~GameSession() {
    world_.reset(); // before the definitions it points into
}

bool GameSession::init(const GameOptions& options, std::string* error) {
    options_ = options;
    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    const Clock::time_point t0 = Clock::now();
    std::string where;
    if (!mountGameFiles(options, vfs_, &where, error)) return false;
    if (options.afterMount) options.afterMount(vfs_);
    const Clock::time_point t1 = Clock::now();
    db_.reset(new DefDatabase());
    if (!db_->load(vfs_)) {
        if (error) *error = "cannot load definitions from " + where;
        return false;
    }
    const Clock::time_point t2 = Clock::now();
    world_.reset(new World());
    {
        WorldConfig c = options.world;
        c.rules = &options.rules();
        world_->init(vfs_, *db_, c);
    }
    cheats_.setGame(world_->game());
    cheatUsed_ = false;
    godCheat_ = false;
    message_.clear();
    messageAge_ = -1.0f;
    const Clock::time_point t3 = Clock::now();
    initTimes_.mountMs = ms(t0, t1);
    initTimes_.defsMs = ms(t1, t2);
    initTimes_.worldMs = ms(t2, t3);
    if (!options.startLevel) return true;
    const bool ok = startMission(std::min(std::max(options.mission, 1), options.rules().missionCount), error);
    initTimes_.levelMs = ms(t3, Clock::now());
    return ok;
}

void GameSession::resetFlow() {
    flowTimer_ = 0;
    wasComplete_ = wasGameOver_ = false;
    hintsSeen_ = world_->hintsShown();
}

bool GameSession::startMission(int mission, std::string* error) {
    mission_ = mission;
    resetFlow();
    hasLevel_ = world_->loadLevel(std::to_string(mission), error);
    return hasLevel_;
}

bool GameSession::startMission(const LevelSetup& s, std::string* error) {
    WorldConfig c = options_.world;
    c.difficulty = s.difficulty;
    c.players = s.players;
    c.heli[0] = s.heli[0];
    c.heli[1] = s.heli[1];
    c.cameraMode = s.camera;
    c.rules = &rules();
    c.godMode = c.godMode || godCheat_; // the god-mode cheat outlives the level (a global in the original)
    world_->init(vfs_, *db_, c);
    for (int p = 0; p < kMaxPlayers; ++p) {
        PlayerRecord& pr = world_->player(p);
        pr.livesAtStart = s.lives[p];
        pr.banked = static_cast<int>(std::max(-2000000000LL, std::min(2000000000LL, s.banked[p])));
        pr.rankAccumulator = static_cast<float>(s.rankAccumulator[p]);
    }
    // Weapons (as2 engine-behaviour.delta.md 8.2): the level start applies the mission's
    // loadout (new game, Restart) unless the upgrades of the last mission carry over (Next).
    if (s.carryUpgrades && rules().upgradesCarryToNextMission) {
        for (int p = 0; p < kMaxPlayers; ++p) {
            PlayerRecord& pr = world_->player(p);
            for (int k = 0; k < kMaxWeaponSlots; ++k) pr.upgrades[k] = s.upgrades[p][k];
            pr.weapon = static_cast<float>(s.weapon[p]);
        }
        world_->carryUpgradesToNextLevel();
    }
    mission_ = std::min(std::max(s.mission, 1), rules().missionCount);
    resetFlow();
    hasLevel_ = world_->loadLevel(std::to_string(mission_), error);
    return hasLevel_;
}

bool GameSession::loadAttract(const std::string& id, std::string* error) {
    WorldConfig c = options_.world;
    c.players = 1;
    c.rules = &rules();
    world_->init(vfs_, *db_, c);
    mission_ = 0;
    resetFlow();
    hasLevel_ = world_->loadLevel(id, error);
    return hasLevel_;
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
    if (messageAge_ >= 0.0f) messageAge_ += w.config().dt;
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
                next = mission_ >= rules().missionCount ? 1 : mission_ + 1;
                // "Next" keeps the upgrades collected so far where they carry over (as2 8.2);
                // the campaign's start over after the last mission is a new game.
                if (rules().upgradesCarryToNextMission && next != 1) w.carryUpgradesToNextLevel();
            }
            // Game over: Restart reloads the mission with the lives it started with (and, in
            // the sequels, the mission's loadout).
            if (!startMission(next, &err)) {
                AS3D_ERROR("cannot start mission %d: %s", next, err.c_str());
            } else {
                events |= kLevelStarted;
            }
        }
    }
    return events;
}

ui::HudState hudStateOf(const World& w, const std::string& levelName) {
    const GameRules& rules = w.rules();
    ui::HudState hs;
    hs.playerCount = std::min(std::max(w.numPlayers(), 1), 2);
    for (int p = 0; p < hs.playerCount; ++p) {
        const PlayerRecord& pr = w.player(p);
        ui::HudPlayer& hp = hs.players[p];
        int pi = w.playerEntityIndex(p);
        hp.health = pi >= 0 ? std::max(w.entity(pi).f(F_HEALTH), 0.0f) : 0.0f;
        // The sequels measure the bar against the helicopter's maximum health (entity +0x70,
        // from its definition; as2/engine-behaviour.delta.md 7.4, 11.2); the first game against 400.
        hp.maxHealth = ui::kFullHealth;
        if (rules.healthBarScaleFromMax && pi >= 0 && w.entity(pi).maxHealth > 0.0f) hp.maxHealth = w.entity(pi).maxHealth;
        hp.lives = pr.lives > 0.0f ? static_cast<int>(std::min(pr.lives, 99.0f)) : 0;
        const float s = pr.scores;
        hp.score = ((s > -2.0e9f && s < 2.0e9f) ? static_cast<long long>(s) : 0LL) + pr.banked;
        // The sequels round p_weapon (as2/frontend.md 4.2), the first game truncates.
        if (pr.weapon >= 0.0f && pr.weapon < 64.0f)
            hp.weapon = w.game() != GameId::AirStrike3D ? static_cast<int>(std::lround(pr.weapon)) : static_cast<int>(pr.weapon);
        else
            hp.weapon = 0;
        for (int k = 0; k < ui::kWeaponSlots; ++k) hp.upgrades[k] = std::max(pr.upgrades[k], 0);
        hp.missileSelected = pr.currentMissile;
        for (int t = 0; t < ui::kMissileTypes; ++t) hp.missiles[t] = std::max(pr.missiles[t], 0);
        hp.powerupSelected = pr.currentPowerup;
        for (int k = 0; k < ui::kPowerupSlots; ++k) hp.powerups[k] = std::max(pr.powerups[k], 0);
    }
    // Level-name typewriter (frontend.md 4.5): the world's clock stops while paused, like the
    // original's level clock.
    hs.levelName = levelName;
    hs.levelTime = w.time();
    return hs;
}

ui::HudState hudStateOf(const GameSession& session) {
    ui::HudState hs = hudStateOf(session.world(), session.levelName());
    for (int p = 0; p < hs.playerCount; ++p) hs.players[p].score = session.displayScore(p);
    hs.message = session.message();
    hs.messageAge = session.messageAge();
    return hs;
}

Cheat GameSession::typeCheat(char c, const char** messageKey) {
    if (messageKey) *messageKey = "";
    // Only outside intermission levels (engine-behaviour.md 14).
    if (!hasLevel_ || !world_ || world_->intermission()) return Cheat::None;
    const Cheat cheat = cheats_.feed(c);
    if (cheat == Cheat::None) return cheat;
    cheatUsed_ = true;
    const char* key = applyCheat(*world_, cheat);
    if (cheat == Cheat::GodMode) godCheat_ = world_->godMode();
    if (messageKey) *messageKey = key;
    return cheat;
}

void GameSession::postMessage(std::string text) {
    message_ = std::move(text);
    messageAge_ = 0.0f;
}

} // namespace as3d_game
