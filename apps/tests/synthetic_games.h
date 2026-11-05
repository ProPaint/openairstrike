// Three synthetic games for the identification and import tests (docs/spec/issues/170): real
// pak files (as3d/pak_file.h) with made-up entry names, their fingerprints and name lists put
// in place of the built-in ones, no head signatures and no known hashes, so that everything
// goes through the entry names. Never any game data.
#pragma once

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "as3d/game_data.h"
#include "as3d/game_import.h"
#include "as3d/pak_file.h"
#include "as3d/sha256.h"

namespace synth {

inline std::vector<std::string> refNames(const char* key, const char* pak) {
    std::vector<std::string> names;
    const std::string p = pak;
    if (p == "pak0.apk") {
        for (int i = 0; i < 200; ++i) {
            const char* dir = i % 3 == 0 ? "models" : i % 3 == 1 ? "textures" : "gfx";
            names.push_back(std::string(dir) + "\\" + key + "\\file" + std::to_string(i) + ".bin");
        }
        names.push_back(std::string("maps\\") + key + "\\levels.txt");
    } else if (p == "pak1.apk") {
        for (int i = 0; i < 20; ++i) names.push_back(std::string("maps\\") + key + "\\level" + std::to_string(i) + ".hsc");
    } else if (p == "pak2.apk") {
        // The sequels' music: the same names in both (as the real as2 and gulf pak2).
        const char* who = std::string(key) == "as3d" ? "as3d" : "sequel";
        for (int i = 0; i < 10; ++i) names.push_back(std::string("music\\") + who + "\\track" + std::to_string(i) + ".mo3");
    } else if (p == "pak4.apk") {
        names.push_back(std::string("models\\") + key + "\\patched.tga");
    }
    return names;
}

inline std::string joined(std::vector<std::string> names) {
    std::sort(names.begin(), names.end());
    std::string s;
    for (size_t i = 0; i < names.size(); ++i) s += (i ? "\n" : "") + names[i];
    return s;
}

// Writes a pak whose entries are `names` (each body: its name).
inline bool writeNamedPak(const std::filesystem::path& path, const std::vector<std::string>& names, std::string* err = nullptr) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::vector<as3d::PakWriteEntry> entries;
    for (const std::string& n : names) {
        as3d::PakWriteEntry e;
        e.name = n;
        e.data.assign(n.begin(), n.end());
        entries.push_back(e);
    }
    std::string e;
    return as3d::writePak(path.string(), entries, err ? err : &e);
}

// Installs the synthetic tables; the destructor restores the built-in ones.
struct Games {
    std::vector<as3d::PakFingerprint> fingerprints;
    std::vector<std::string> shas; // storage for the fingerprint strings
    as3d::PakSignature noSignature[1] = {{as3d::GameId::AirStrike3D, 0, 0}};
    as3d::KnownPakHash noHash[1] = {{as3d::GameId::AirStrike3D, "", ""}};

    Games() {
        using namespace as3d;
        const char* paks[4] = {"pak0.apk", "pak1.apk", "pak2.apk", "pak4.apk"};
        for (int g = 0; g < kGameCount; ++g) {
            const GameProfile& p = gameProfile(static_cast<GameId>(g));
            for (const char* pak : paks) {
                bool listed = false;
                for (const char* const* q = p.paks; *q; ++q)
                    if (std::string(*q) == pak) listed = true;
                if (!listed) continue;
                const std::vector<std::string> names = refNames(p.key, pak);
                const std::string j = joined(names);
                shas.push_back(sha256Hex(j.data(), j.size()));
                setPakEntryNames(p.id, pak, j + "\n");
            }
        }
        size_t k = 0;
        for (int g = 0; g < kGameCount; ++g) {
            const GameProfile& p = gameProfile(static_cast<GameId>(g));
            for (const char* pak : paks) {
                bool listed = false;
                for (const char* const* q = p.paks; *q; ++q)
                    if (std::string(*q) == pak) listed = true;
                if (!listed) continue;
                fingerprints.push_back({p.id, pak, static_cast<std::uint32_t>(refNames(p.key, pak).size()), shas[k++].c_str()});
            }
        }
        setPakFingerprintsForTest(fingerprints.data(), static_cast<int>(fingerprints.size()));
        setPakSignaturesForTest(noSignature, 0);
        setKnownPakHashesForTest(noHash, 0);
    }
    ~Games() {
        as3d::setPakFingerprintsForTest(nullptr, 0);
        as3d::setPakSignaturesForTest(nullptr, 0);
        as3d::setKnownPakHashesForTest(nullptr, 0);
        as3d::resetPakEntryNamesForTest();
    }
    // The pak of a game with its reference names.
    bool pak(const std::filesystem::path& path, const char* key, const char* pakName) const {
        return writeNamedPak(path, refNames(key, pakName));
    }
};

} // namespace synth
