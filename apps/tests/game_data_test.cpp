// as3d/game_data.h: finding a game's data, identifying paks by content, and mounting a game's
// paks in the profile's order through GameSession.
#include <doctest.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "as3d/game_data.h"
#include "as3d/vfs.h"
#include "../game/game_session.h"
#include "test_data.h"

using namespace as3d;
namespace fs = std::filesystem;

namespace {

// A scratch directory removed at the end of the test.
struct TempDir {
    fs::path path;
    explicit TempDir(const char* tag) {
        std::error_code ec;
        path = fs::temp_directory_path(ec) / (std::string("as3d_game_data_") + tag + "_" + std::to_string(std::rand()));
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    std::string str() const { return path.string(); }
};

void writeBytes(const fs::path& p, const std::string& bytes) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    if (std::FILE* f = std::fopen(p.string().c_str(), "wb")) {
        std::fwrite(bytes.data(), 1, bytes.size(), f);
        std::fclose(f);
    }
}

// Puts the built-in signature table back at the end of a test.
struct SignatureGuard {
    ~SignatureGuard() { setPakSignaturesForTest(nullptr, 0); }
};

// Synthetic pak (docs/spec/pak.md): plain bodies, table encrypted with a fixed key.
void put32(u8* p, u32 v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<u8>((v >> (8 * i)) & 0xFF);
}
std::string buildPak(const std::string& name, const std::string& content) {
    std::array<u8, 1024> key{};
    for (size_t i = 0; i < key.size(); ++i) key[i] = static_cast<u8>((i * 37 + 11) & 0xFF);
    Blob data(0x410, 0);
    const u8 magic[8] = {0x00, 0x00, 0x80, 0x3F, 0x99, 0x99, 0x00, 0x00};
    std::memcpy(data.data(), magic, 8);
    std::memcpy(data.data() + 0x10, key.data(), key.size());
    u32 offset = static_cast<u32>(data.size());
    data.insert(data.end(), content.begin(), content.end());
    put32(data.data() + 8, static_cast<u32>(data.size()));
    put32(data.data() + 12, 1);
    Blob entry(76, 0);
    std::memcpy(entry.data(), name.data(), name.size());
    put32(entry.data() + 64, offset);
    put32(entry.data() + 68, static_cast<u32>(content.size()));
    put32(entry.data() + 72, 0);
    for (size_t i = 0; i < entry.size(); ++i) entry[i] ^= key[i % key.size()];
    data.insert(data.end(), entry.begin(), entry.end());
    return std::string(data.begin(), data.end());
}

std::string bytesOf(const Blob& b) { return std::string(b.begin(), b.end()); }

} // namespace

TEST_CASE("identifyPaks: three synthetic signatures, unknown content, renamed directory") {
    SignatureGuard guard;
    TempDir tmp("ident");
    // Same size for two of them: the hash of the head has to tell them apart.
    const std::string a(70000, 'a'), b = std::string(70000, 'b'), c = std::string(1234, 'c');
    writeBytes(tmp.path / "one/pak0.apk", a);
    writeBytes(tmp.path / "two/pak0.apk", b);
    writeBytes(tmp.path / "three/pak0.apk", c);
    writeBytes(tmp.path / "other/pak0.apk", std::string(70000, 'x'));
    // Differs from `a` only after the first 64 KB: same signature, by design.
    writeBytes(tmp.path / "tail/pak0.apk", a.substr(0, 65536) + std::string(4464, 'z'));

    PakSignature sigs[3];
    const GameId ids[3] = {GameId::AirStrike3D, GameId::AirStrike2, GameId::GulfThunder};
    const char* dirs[3] = {"one", "two", "three"};
    for (int i = 0; i < 3; ++i) {
        std::uint64_t size = 0, hash = 0;
        REQUIRE(readPakSignature((tmp.path / dirs[i] / "pak0.apk").string(), &size, &hash));
        sigs[i] = {ids[i], size, hash};
    }
    setPakSignaturesForTest(sigs, 3);

    CHECK(identifyPaks((tmp.path / "one").string()) == &gameProfile(GameId::AirStrike3D));
    CHECK(identifyPaks((tmp.path / "two").string()) == &gameProfile(GameId::AirStrike2));
    CHECK(identifyPaks((tmp.path / "three").string()) == &gameProfile(GameId::GulfThunder));
    CHECK(identifyPaks((tmp.path / "other").string()) == nullptr);
    CHECK(identifyPaks((tmp.path / "missing").string()) == nullptr);
    CHECK(identifyPaks((tmp.path / "tail").string()) == &gameProfile(GameId::AirStrike3D));

    // Names and paths mean nothing: a directory called "gulf" holding the first game's file.
    std::error_code ec;
    fs::rename(tmp.path / "one", tmp.path / "gulf", ec);
    REQUIRE(!ec);
    CHECK(identifyPaks((tmp.path / "gulf").string()) == &gameProfile(GameId::AirStrike3D));

    // The built-in table knows none of the synthetic files.
    setPakSignaturesForTest(nullptr, 0);
    CHECK(identifyPaks((tmp.path / "gulf").string()) == nullptr);
}

TEST_CASE("locateGameData: the layout of each game, and what is missing") {
    TempDir tmp("locate");
    const fs::path r = tmp.path;
    // First game: both locations.
    for (const char* p : {"pak0.apk", "pak1.apk", "pak2.apk"}) writeBytes(r / "third_party_local/original/data" / p, "x");
    writeBytes(r / "third_party_local/original/data/Settings.xml", "<s/>");
    writeBytes(r / "assets_extracted/maps/levels.txt", "x");
    writeBytes(r / "assets_extracted/texts_v170.txt", "x");
    // Gulf Thunder: no pak1, has pak4; extracted files only under its own key.
    for (const char* p : {"pak0.apk", "pak2.apk", "pak4.apk"}) writeBytes(r / "third_party_local/games/gulf/data" / p, "x");
    writeBytes(r / "assets_extracted_games/gulf/maps/levels.txt", "x");

    GameData a = locateGameData(r.string(), gameProfile(GameId::AirStrike3D));
    CHECK(a.installDir == r.string() + "/third_party_local/original");
    CHECK(a.extractedDir == r.string() + "/assets_extracted");
    REQUIRE(a.paks.size() == 3);
    CHECK(a.paks[0] == a.dataDir + "/pak0.apk");
    CHECK(a.paks[2] == a.dataDir + "/pak2.apk");
    CHECK(a.hasExtracted);
    CHECK(a.missing.empty());
    CHECK(a.settingsXml == a.dataDir + "/Settings.xml");
    CHECK(a.textsFile == a.extractedDir + "/texts_v170.txt");
    CHECK(a.logoFile.empty());
    CHECK(a.present());

    GameData g = locateGameData(r.string(), gameProfile(GameId::GulfThunder));
    CHECK(g.installDir == r.string() + "/third_party_local/games/gulf");
    CHECK(g.dataDir == g.installDir + "/data");
    CHECK(g.extractedDir == r.string() + "/assets_extracted_games/gulf");
    REQUIRE(g.paks.size() == 3);
    CHECK(g.paks[0] == g.dataDir + "/pak0.apk");
    CHECK(g.paks[1] == g.dataDir + "/pak2.apk");
    CHECK(g.paks[2] == g.dataDir + "/pak4.apk"); // last: overrides the others
    CHECK(g.hasExtracted);
    REQUIRE(g.missing.size() == 1);
    CHECK(g.missing[0] == g.dataDir + "/pak1.apk");
    CHECK(g.settingsXml.empty());
    CHECK(g.textsFile.empty());

    GameData s = locateGameData(r.string(), gameProfile(GameId::AirStrike2));
    CHECK(s.extractedDir == r.string() + "/assets_extracted_games/as2");
    CHECK(s.paks.empty());
    CHECK(!s.hasExtracted);
    CHECK(!s.present());
    CHECK(s.missing.size() == 4); // three paks and the extracted files
}

TEST_CASE("chooseGameData: default, key, unknown key, game without data") {
    SignatureGuard guard;
    TempDir tmp("choose");
    const fs::path r = tmp.path;
    writeBytes(r / "assets_extracted_games/gulf/maps/levels.txt", "x");
    std::string error;
    GameData d;

    // Only the sequel is there: it becomes the default; the first game is an error that
    // lists what was found.
    unsetenv("AS3D_GAME");
    REQUIRE(chooseGameData(r.string(), "", "", &d, &error));
    CHECK(d.game == &gameProfile(GameId::GulfThunder));
    CHECK(!chooseGameData(r.string(), "as3d", "", &d, &error));
    CHECK(error.find("as3d") != std::string::npos);
    CHECK(error.find("detected: gulf") != std::string::npos);
    CHECK(!chooseGameData(r.string(), "nosuch", "", &d, &error));
    CHECK(error.find("unknown game 'nosuch'") != std::string::npos);

    writeBytes(r / "assets_extracted/maps/levels.txt", "x");
    REQUIRE(chooseGameData(r.string(), "", "", &d, &error));
    CHECK(d.game == &gameProfile(GameId::AirStrike3D));
    setenv("AS3D_GAME", "gulf", 1);
    REQUIRE(chooseGameData(r.string(), "", "", &d, &error));
    CHECK(d.game == &gameProfile(GameId::GulfThunder));
    REQUIRE(chooseGameData(r.string(), "as3d", "", &d, &error)); // the option wins
    CHECK(d.game == &gameProfile(GameId::AirStrike3D));
    unsetenv("AS3D_GAME");

    // --paks alone: the game comes from the content.
    writeBytes(r / "elsewhere/pak0.apk", std::string(100, 'q'));
    writeBytes(r / "elsewhere/pak2.apk", "x");
    CHECK(!chooseGameData(r.string(), "", (r / "elsewhere").string(), &d, &error));
    std::uint64_t size = 0, hash = 0;
    REQUIRE(readPakSignature((r / "elsewhere/pak0.apk").string(), &size, &hash));
    PakSignature sig = {GameId::AirStrike2, size, hash};
    setPakSignaturesForTest(&sig, 1);
    REQUIRE(chooseGameData(r.string(), "", (r / "elsewhere").string(), &d, &error));
    CHECK(d.game == &gameProfile(GameId::AirStrike2));
    CHECK(d.dataDir == (r / "elsewhere").string());
    CHECK(d.paks.size() == 2);
}

TEST_CASE("GameSession mounts a game's paks in order, the later one overrides") {
    TempDir tmp("mount");
    writeBytes(tmp.path / "pak0.apk", buildPak("models\\thing.txt", "from pak0"));
    writeBytes(tmp.path / "pak1.apk", buildPak("models\\thing.txt", "from pak1"));
    writeBytes(tmp.path / "pak4.apk", buildPak("models\\other.txt", "only pak4"));

    as3d_game::GameSession session;
    as3d_game::GameOptions o;
    o.game = &gameProfile(GameId::GulfThunder);
    o.startLevel = false;
    for (const char* p : {"pak0.apk", "pak1.apk", "pak4.apk"}) o.paks.push_back((tmp.path / p).string());
    std::string error;
    // The synthetic paks carry no definitions, so init stops there; the mounts are done.
    CHECK(!session.init(o, &error));
    CHECK(error.find("cannot load definitions") != std::string::npos);
    Blob out;
    REQUIRE(session.vfs().read("models\\thing.txt", out));
    CHECK(bytesOf(out) == "from pak1");
    REQUIRE(session.vfs().read("models\\other.txt", out));
    CHECK(bytesOf(out) == "only pak4");

    // A pak that does not exist is an error naming it.
    as3d_game::GameSession bad;
    o.paks.push_back((tmp.path / "nope.apk").string());
    CHECK(!bad.init(o, &error));
    CHECK(error.find("cannot mount") != std::string::npos);
}

TEST_CASE("detectGames on the real data, and Gulf Thunder's pak4 override") {
    AS3D_REQUIRE_DATA();
    const std::string root = testdata::root();
    std::vector<GameData> found = detectGames(root);
    REQUIRE(!found.empty());
    CHECK(found[0].game == &gameProfile(GameId::AirStrike3D));
    // Order as3d, as2, gulf, each present exactly when its data is.
    int last = -1;
    for (const GameData& d : found) {
        CHECK(static_cast<int>(d.game->id) > last);
        last = static_cast<int>(d.game->id);
    }
    auto has = [&](GameId id) {
        for (const GameData& d : found)
            if (d.game->id == id) return true;
        return false;
    };
    std::error_code ec;
    bool as2Here = fs::exists(root + "/third_party_local/games/as2/data/pak0.apk", ec);
    bool gulfHere = fs::exists(root + "/third_party_local/games/gulf/data/pak0.apk", ec);
    CHECK(has(GameId::AirStrike2) == (as2Here || fs::exists(root + "/assets_extracted_games/as2/maps/levels.txt", ec)));
    CHECK(has(GameId::GulfThunder) == (gulfHere || fs::exists(root + "/assets_extracted_games/gulf/maps/levels.txt", ec)));
    if (as2Here) CHECK(identifyPaks(root + "/third_party_local/games/as2/data") == &gameProfile(GameId::AirStrike2));
    if (gulfHere) CHECK(identifyPaks(root + "/third_party_local/games/gulf/data") == &gameProfile(GameId::GulfThunder));
    if (fs::exists(root + "/third_party_local/original/data/pak0.apk", ec))
        CHECK(identifyPaks(root + "/third_party_local/original/data") == &gameProfile(GameId::AirStrike3D));

    if (!gulfHere) {
        std::fprintf(stderr, "SKIPPED (no Gulf Thunder paks): %s\n", __FILE__);
        return;
    }
    GameData gulf;
    for (const GameData& d : found)
        if (d.game->id == GameId::GulfThunder) gulf = d;
    REQUIRE(gulf.paks.size() == 4);
    as3d_game::GameSession session;
    as3d_game::GameOptions o;
    o.game = gulf.game;
    o.paks = gulf.paks;
    o.startLevel = false;
    std::string error;
    session.init(o, &error); // the definitions of a sequel may not load yet; the mounts are enough
    Blob tga;
    REQUIRE(session.vfs().read("models\\mapobjects\\elektro\\power_plant.tga", tga));
    REQUIRE(tga.size() > 18);
    CHECK(tga[12] + 256 * tga[13] == 256);
    CHECK(tga[14] + 256 * tga[15] == 256);
}
