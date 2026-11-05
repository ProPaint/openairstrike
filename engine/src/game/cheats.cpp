// The typed cheat codes (as3d/cheats.h; docs/spec/engine-behaviour.md 14, as2 delta 14).
#include "as3d/cheats.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "as3d/world.h"

namespace as3d {

namespace {

// AirStrike 3D (VERIFIED-CODE 0x4014c0).
const CheatCode kCodesAs3d[] = {
    {"iwannabe", Cheat::GodMode},     {"endisnear", Cheat::Lives},       {"armorychamber", Cheat::Weapons},
    {"launchmenow", Cheat::Missiles}, {"iamstronger", Cheat::PowerUps},  {"messwiththebest", Cheat::EndLevel},
    {"dieliketherest", Cheat::GameOver}, {"givemecredit", Cheat::Credits}, {nullptr, Cheat::None},
};

// AirStrike 2 (as2@0x407110) and Gulf Thunder (gulf@0x407070, the same words). No credits.
const CheatCode kCodesSequels[] = {
    {"invulnerability", Cheat::GodMode},  {"igonnaliveforever", Cheat::Lives},  {"showmetheweapons", Cheat::Weapons},
    {"moremoreweapons", Cheat::Missiles}, {"glitteringprizes", Cheat::PowerUps}, {"deadlineisnear", Cheat::EndLevel},
    {"diediediemydarling", Cheat::GameOver}, {nullptr, Cheat::None},
};

bool endsWith(const std::string& s, const char* word) {
    const size_t n = std::strlen(word);
    return s.size() >= n && s.compare(s.size() - n, n, word) == 0;
}

} // namespace

const CheatCode* cheatCodes(GameId game) { return game == GameId::AirStrike3D ? kCodesAs3d : kCodesSequels; }

Cheat CheatInput::feed(char c) {
    buffer_.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (buffer_.size() > kCheatBufferSize) buffer_.erase(0, buffer_.size() - kCheatBufferSize);
    for (const CheatCode* code = cheatCodes(game_); code->word; ++code) {
        if (!endsWith(buffer_, code->word)) continue;
        buffer_.clear();
        return code->cheat;
    }
    return Cheat::None;
}

const char* applyCheat(World& w, Cheat cheat) {
    const bool first = w.game() == GameId::AirStrike3D;
    const GameRules& rules = w.rules();
    switch (cheat) {
        case Cheat::GodMode:
            w.setGodMode(!w.godMode());
            return w.godMode() ? "cheat.god_on" : "cheat.god_off";
        case Cheat::Lives:
            for (int p = 0; p < kMaxPlayers; ++p) w.player(p).lives = 99.0f;
            return "cheat.lives";
        case Cheat::Weapons: {
            // Every slot the game's G_SetUpgrade accepts (20 in the first game, 9 in the sequels).
            const int slots = std::clamp(rules.weaponSlots > 0 ? rules.weaponSlots : kMaxWeaponSlots, 1, kMaxWeaponSlots);
            for (int p = 0; p < kMaxPlayers; ++p)
                for (int k = 0; k < slots; ++k)
                    if (w.player(p).upgrades[k] == 0) w.player(p).upgrades[k] = 1;
            return "cheat.weapons";
        }
        case Cheat::Missiles:
            for (int p = 0; p < kMaxPlayers; ++p) {
                PlayerRecord& pr = w.player(p);
                for (int& n : pr.missiles) n = std::min(n + 99, 99);
                if (pr.currentMissile < 0) pr.currentMissile = 0;
            }
            return "cheat.missiles";
        case Cheat::PowerUps: {
            // The first game: slots 0..3. The sequels: slots 0..5 and 8, and slot 0 is selected
            // when none is.
            const u32 mask = first ? 0x00Fu : 0x13Fu;
            for (int p = 0; p < kMaxPlayers; ++p) {
                PlayerRecord& pr = w.player(p);
                for (int k = 0; k < 16; ++k)
                    if (mask & (1u << k)) pr.powerups[k] = std::min(pr.powerups[k] + 99, 99);
                if (!first && pr.currentPowerup < 0) pr.currentPowerup = 0;
            }
            return "cheat.powerups";
        }
        case Cheat::EndLevel:
            w.endLevel();
            return "";
        case Cheat::GameOver:
            if (first) w.setGameOver();
            else w.gameOverBuiltin();
            return "";
        case Cheat::Credits:
        case Cheat::None:
            break;
    }
    return "";
}

} // namespace as3d
