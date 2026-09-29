// The game selector's logic. See as3d/launcher.h and docs/spec/issues/163.
#include "as3d/launcher.h"

#include <cstdio>
#include <cstring>

#include "as3d/game_data.h"
#include "as3d/profile.h"

namespace as3d {

namespace {

constexpr char kMagic[8] = {'A', 'S', '3', 'D', 'L', 'N', 'C', 'H'};
constexpr u32 kVersion = 1;
constexpr size_t kMaxKey = 15;
constexpr size_t kMaxFile = 64;

u32 crc32(const u8* data, size_t n) {
    u32 c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= data[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

void putU32(std::vector<u8>& out, u32 v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<u8>(v >> (8 * i)));
}

u32 getU32(const u8* p) {
    return static_cast<u32>(p[0]) | static_cast<u32>(p[1]) << 8 | static_cast<u32>(p[2]) << 16 |
           static_cast<u32>(p[3]) << 24;
}

bool validKey(const std::string& key) {
    if (key.empty() || key.size() > kMaxKey) return false;
    for (char c : key)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

bool readSmallFile(const std::string& path, std::vector<u8>& out, size_t limit) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    u8 buf[256];
    size_t n;
    bool ok = true;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
        if (out.size() + n > limit) {
            ok = false;
            break;
        }
        out.insert(out.end(), buf, buf + n);
    }
    std::fclose(f);
    return ok;
}

// "12 345": thousands apart, as the high-score screens are read aloud.
std::string grouped(std::int64_t v) {
    const bool neg = v < 0;
    std::string d = std::to_string(neg ? -v : v), out;
    for (size_t i = 0; i < d.size(); ++i) {
        if (i && (d.size() - i) % 3 == 0) out += ' ';
        out += d[i];
    }
    return neg ? "-" + out : out;
}

} // namespace

LaunchPlan planLaunch(const std::vector<const GameProfile*>& present, const std::string& forcedKey,
                      const std::string& lastChoice, bool allowUnfinished) {
    LaunchPlan plan;
    if (!forcedKey.empty()) {
        plan.startKey = forcedKey;
        return plan;
    }
    for (int i = 0; i < kGameCount; ++i) {
        const GameProfile& g = gameProfile(static_cast<GameId>(i));
        bool here = false;
        for (const GameProfile* p : present) here = here || p == &g;
        if (here && (allowUnfinished || gameIsPlayable(g))) plan.offered.push_back(&g);
    }
    if (plan.offered.size() == 1) {
        plan.startKey = plan.offered.front()->key;
        plan.offered.clear();
        return plan;
    }
    if (plan.offered.empty()) return plan;
    plan.showSelector = true;
    for (size_t i = 0; i < plan.offered.size(); ++i)
        if (lastChoice == plan.offered[i]->key) plan.preselected = static_cast<int>(i);
    return plan;
}

std::vector<u8> serializeLauncherChoice(const std::string& key) {
    std::vector<u8> out(kMagic, kMagic + sizeof kMagic);
    putU32(out, kVersion);
    out.push_back(static_cast<u8>(key.size()));
    out.insert(out.end(), key.begin(), key.end());
    putU32(out, crc32(reinterpret_cast<const u8*>(key.data()), key.size()));
    return out;
}

bool parseLauncherChoice(const u8* data, size_t size, std::string* key) {
    if (key) key->clear();
    if (!data || size < sizeof kMagic + 4 + 1 + 4 || std::memcmp(data, kMagic, sizeof kMagic) != 0) return false;
    if (getU32(data + 8) != kVersion) return false;
    const size_t len = data[12];
    if (len == 0 || len > kMaxKey || size != 13 + len + 4) return false;
    const std::string k(reinterpret_cast<const char*>(data + 13), len);
    if (!validKey(k) || getU32(data + 13 + len) != crc32(data + 13, len)) return false;
    if (!findGameProfile(k)) return false;
    if (key) *key = k;
    return true;
}

bool readLauncherChoice(const std::string& path, std::string* key) {
    if (key) key->clear();
    std::vector<u8> b;
    if (path.empty() || !readSmallFile(path, b, kMaxFile)) return false;
    return parseLauncherChoice(b.data(), b.size(), key);
}

bool writeLauncherChoice(const std::string& path, const std::string& key) {
    if (path.empty() || !validKey(key)) return false;
    const std::vector<u8> bytes = serializeLauncherChoice(key);
    const std::string tmp = path + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const bool written = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    if (std::fclose(f) != 0 || !written) {
        std::remove(tmp.c_str());
        return false;
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

SaveSummary readSaveSummary(const std::string& path, const std::string& legacyPath, const GameProfile& game) {
    SaveSummary s;
    s.missionCount = game.rules.missionCount;
    std::string from = path;
    std::FILE* probe = path.empty() ? nullptr : std::fopen(path.c_str(), "rb");
    if (probe) {
        std::fclose(probe);
    } else if (game.id == GameId::AirStrike3D && !legacyPath.empty()) {
        // The first game's save from before the move (issue 160), read where it is.
        probe = std::fopen(legacyPath.c_str(), "rb");
        if (!probe) return s;
        std::fclose(probe);
        from = legacyPath;
    } else {
        return s;
    }
    s.found = true;
    const Progress fresh = Progress::defaults(game.rules);
    Profile p;
    p.progress = fresh;
    if (!loadProfileFile(from, p, nullptr, game.key)) return s;
    s.readable = true;
    for (int i = 0; i < game.rules.missionCount && i < kMaxMissions; ++i)
        if (p.progress.missionUnlocked[i]) ++s.missionsUnlocked;
    // The player's own entries: those that are not an entry of the fresh table.
    for (const HighScore& h : p.progress.scores) {
        bool stock = false;
        for (const HighScore& f : fresh.scores) stock = stock || (f.name == h.name && f.score == h.score);
        if (stock || h.score <= 0) continue;
        if (!s.hasScore || h.score > s.bestScore) s.bestScore = h.score;
        s.hasScore = true;
    }
    return s;
}

std::string describeSave(const SaveSummary& s) {
    if (!s.found) return "No save yet";
    if (!s.readable) return "Save cannot be read";
    std::string line = std::to_string(s.missionsUnlocked) + " of " + std::to_string(s.missionCount) + " missions open";
    if (s.hasScore) line += ", best " + grouped(s.bestScore);
    return line;
}

} // namespace as3d
