// Campaign rules, unlocks, high scores and settings (frontend.md 5 and 6).
#include <algorithm>
#include <cmath>

#include "as3d/profile.h"

namespace as3d {

// ---------------------------------------------------------------------------
// Difficulty and rank
// ---------------------------------------------------------------------------
const DifficultyFactors& difficultyFactors(int index) {
    static const DifficultyFactors t[kDifficultyCount] = {
        {0.3f, 0.5f, 0.6f, 0.7f},  {0.5f, 0.7f, 0.8f, 0.85f}, {0.75f, 0.8f, 1.0f, 1.07f},
        {1.5f, 1.25f, 1.2f, 1.2f}, {2.0f, 1.4f, 1.4f, 1.3f},
    };
    return t[std::clamp(index, 0, kDifficultyCount - 1)];
}

int rankIndex(double v, bool cheatUsed) {
    if (cheatUsed) return 0;
    static const double threshold[6] = {0, 3, 7.5, 14, 22, 30};
    for (int i = 1; i <= 5; i++)
        if (v < threshold[i]) return i;
    return 6;
}

// ---------------------------------------------------------------------------
// Campaign
// ---------------------------------------------------------------------------
void Campaign::start(int missionIndex, int difficultyIndex, int playerCount) {
    active = true;
    mission = std::clamp(missionIndex, 0, kMissionCount - 1);
    difficulty = std::clamp(difficultyIndex, 0, kDifficultyCount - 1);
    players = playerCount >= 2 ? 2 : 1;
    for (CampaignPlayer& cp : p) cp = CampaignPlayer{};
}

static double ratio(double a, double b) { return b != 0 ? a / b : 0.0; }

void Campaign::bank(const LevelPlayerResult results[2], const LevelTotals& totals) {
    for (int i = 0; i < players; i++) {
        const LevelPlayerResult& r = results[i];
        CampaignPlayer& cp = p[i];
        cp.banked = static_cast<std::int64_t>(static_cast<double>(cp.banked) + r.score);
        cp.rankAccumulator += ratio(r.stars, totals.starTotal) + 0.5 * ratio(r.score, totals.maxScore);
        cp.livesAtStart = r.lives;
    }
}

double Campaign::missionRankValue(const LevelPlayerResult& r, const LevelTotals& totals) const {
    const double v = ratio(r.stars, totals.starTotal) + 0.5 * ratio(r.score, totals.maxScore) + p[0].rankAccumulator;
    return v * difficultyFactors(difficulty).rank;
}

double Campaign::highScoreRankValue() const { return p[0].rankAccumulator * difficultyFactors(difficulty).rank; }

// ---------------------------------------------------------------------------
// Progress
// ---------------------------------------------------------------------------
Progress Progress::defaults() {
    Progress p;
    // Fresh-install table (frontend.md 6.1).
    static const struct { const char* name; std::int64_t score; int rank; } t[kHighScoreCount] = {
        {"Divo Master", 1000000, 6}, {"Dennis", 900000, 5},   {"Terminator", 800000, 5},
        {"Walter", 700000, 4},       {"Phil", 600000, 4},     {"James", 500000, 3},
        {"Marianne", 400000, 3},     {"Chris", 300000, 3},    {"Smasher", 250000, 2},
        {"Greg Gizmo", 200000, 2},   {"Alex. B. Blom", 150000, 2}, {"Sam", 100000, 1},
        {"Jennifer", 75000, 1},      {"Turner", 50000, 1},    {"Linda", 30000, 0},
    };
    for (int i = 0; i < kHighScoreCount; i++) p.scores[i] = {t[i].name, t[i].score, t[i].rank};
    p.helicopterUnlocked[0] = p.helicopterUnlocked[1] = true;
    p.missionUnlocked[0] = p.missionUnlocked[1] = true;
    return p;
}

int Progress::qualifyingSlot(std::int64_t score) const {
    for (int i = 0; i < kHighScoreCount; i++)
        if (scores[i].score <= score) return i;
    return -1;
}

int Progress::insert(const std::string& name, std::int64_t score, int rank) {
    const int slot = qualifyingSlot(score);
    if (slot < 0) return -1;
    for (int i = kHighScoreCount - 1; i > slot; i--) scores[i] = scores[i - 1];
    scores[slot] = {name.substr(0, 31), score, std::clamp(rank, 0, kRankCount - 1)};
    return slot;
}

void Progress::unlockAfterMission(int missionIndex, int enableHelic) {
    if (enableHelic >= 0 && enableHelic < kHelicopterCount) helicopterUnlocked[enableHelic] = true;
    const int next = ((missionIndex + 1) % kMissionCount + kMissionCount) % kMissionCount;
    missionUnlocked[next] = true;
    missionUnlocked[0] = true; // mission 1 is always unlocked
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
u32 actionBit(Action a) {
    static const u32 bits[kActionCount] = {0x001, 0x400, 0x002, 0x100, 0x004, 0x200, 0x010, 0x020, 0x040, 0x080};
    const int i = static_cast<int>(a);
    return i >= 0 && i < kActionCount ? bits[i] : 0;
}

Settings Settings::defaults() {
    Settings s;
    // Built-in bindings (frontend.md 6.2), both players alike, in Action order.
    static const int def[kActionCount][2] = {
        {17, 203}, {50, 206}, {16, 204}, {49, 211}, {32, 205}, {50, 212},
        {38, 243}, {40, 244}, {37, 241}, {39, 242},
    };
    for (int p = 0; p < 2; p++)
        for (int a = 0; a < kActionCount; a++) {
            s.keys[p][a][0] = def[a][0];
            s.keys[p][a][1] = def[a][1];
        }
    return s;
}

void Settings::clampToRanges() {
    const Settings d = defaults();
    auto clampF = [](float v, float lo, float hi, float def) { return std::isfinite(v) ? std::clamp(v, lo, hi) : def; };
    if (camera < 0 || camera > 3) camera = d.camera;
    if (videoMode < 0 || videoMode > 8) videoMode = 1;
    if (refreshRate < 0 || refreshRate > 1000) refreshRate = 0;
    if (colorDepth != 0 && colorDepth != 16 && colorDepth != 32) colorDepth = 0;
    if (textureFilter < 0 || textureFilter > 1) textureFilter = 0;
    brightness = clampF(brightness, 0.2f, 1.0f, d.brightness);
    sfxVolume = clampF(sfxVolume, 0.0f, 1.0f, d.sfxVolume);
    musicVolume = clampF(musicVolume, 0.0f, 1.0f, d.musicVolume);
    for (auto& player : keys)
        for (auto& action : player)
            for (int& k : action)
                if (k < 0 || k > 255) k = 0;
}

static void removeCode(int (&slot)[2], int code) {
    if (code <= 0) return;
    if (slot[0] == code) { slot[0] = slot[1]; slot[1] = 0; }
    if (slot[1] == code) slot[1] = 0;
}

void Settings::applyMouseControlBindings() {
    const Action acts[3] = {Action::PrimaryAttack, Action::MissileAttack, Action::UseItem};
    for (int i = 0; i < 3; i++) {
        const int code = (mouseControl ? 200 : 203) + i;
        for (int p = 0; p < 2; p++)
            for (int a = 0; a < kActionCount; a++) removeCode(keys[p][a], code);
        int (&slot)[2] = keys[0][static_cast<int>(acts[i])];
        if (slot[0] <= 0) slot[0] = code;
        else slot[1] = code;
    }
}

void Settings::bindKey(int player, Action a, int code) {
    if (player < 0 || player > 1 || code <= 0 || code == 27) return; // Esc can never be bound
    for (int i = 0; i < kActionCount; i++) removeCode(keys[player][i], code);
    int (&slot)[2] = keys[player][static_cast<int>(a)];
    slot[1] = slot[0];
    slot[0] = code;
}

void Settings::unbindKey(int player, Action a) {
    if (player < 0 || player > 1) return;
    int (&slot)[2] = keys[player][static_cast<int>(a)];
    slot[1] = slot[0];
    slot[0] = 0;
}

} // namespace as3d
