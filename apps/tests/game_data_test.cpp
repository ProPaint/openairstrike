// as3d/game_data.h: finding a game's data, identifying paks by content, and mounting a game's
// paks in the profile's order through GameSession.
#include <doctest.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <random>
#include <unistd.h>
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
        // Unique per process and per call (an unseeded std::rand() gave concurrent test
        // processes the same name; see game_import_test.cpp).
        static int counter = 0;
        std::random_device rd;
        path = fs::temp_directory_path(ec) / (std::string("as3d_game_data_") + tag + "_" + std::to_string(::getpid()) + "_" +
                                              std::to_string(++counter) + "_" + std::to_string(rd()));
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

// ---- identification by entry names (docs/spec/issues/170, section 2)

#include "as3d/pak_file.h"
#include "synthetic_games.h"

namespace {
std::string readFile(const std::string& path) {
    std::string s;
    if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
        char buf[65536];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
        std::fclose(f);
    }
    return s;
}
} // namespace

TEST_CASE("the built-in pak fingerprints are those of tools/pak_entries (count and SHA-256 of the sorted names)") {
    int count = 0;
    const PakFingerprint* table = pakFingerprints(&count);
    REQUIRE(count == 10);
    const std::string dir = std::string(AS3D_REPO_ROOT) + "/tools/pak_entries/";
    for (int i = 0; i < count; ++i) {
        const PakFingerprint& f = table[i];
        const GameProfile& g = gameProfile(f.game);
        const std::string pak = f.pak;
        const std::string file = dir + g.key + (pak == "pak0.apk" ? "" : "." + pak.substr(0, pak.size() - 4)) + ".txt";
        INFO(file);
        const std::string text = readFile(file);
        REQUIRE_FALSE(text.empty());
        std::vector<std::string> names;
        size_t p = 0;
        while (p < text.size()) {
            size_t e = text.find('\n', p);
            if (e == std::string::npos) e = text.size();
            if (e > p) names.push_back(text.substr(p, e - p));
            p = e + 1;
        }
        // The list is what tools/paktool.py names writes: normalized, sorted, unique, ASCII.
        for (size_t k = 0; k < names.size(); ++k) {
            CHECK(normalizePakEntryName(names[k]) == names[k]);
            if (k) CHECK(names[k - 1] < names[k]);
            for (char c : names[k]) CHECK((c >= 0x20 && c <= 0x7E));
        }
        CHECK(names.size() == f.count);
        std::string joined;
        for (size_t k = 0; k < names.size(); ++k) joined += (k ? "\n" : "") + names[k];
        CHECK(sha256Hex(joined.data(), joined.size()) == f.sha256);
        // And the engine reads the same list from the source tree.
        const std::vector<std::string>* loaded = pakEntryNamesOf(f.game, pak);
        REQUIRE(loaded);
        CHECK(*loaded == names);
    }
    // Every pak of every profile has a reference, except the override pak nobody owns.
    for (int i = 0; i < kGameCount; ++i) {
        const GameProfile& g = gameProfile(static_cast<GameId>(i));
        for (const char* const* p = g.paks; *p; ++p) CHECK(pakHasReference(g, *p) == !pakIsOptional(g, *p));
    }
    CHECK(pakIsOptional(gameProfile(GameId::AirStrike3D), "pak3.apk"));
    CHECK_FALSE(pakIsOptional(gameProfile(GameId::AirStrike3D), "pak0.apk"));
    CHECK(requiredPakCount(gameProfile(GameId::AirStrike3D)) == 3);
    CHECK(requiredPakCount(gameProfile(GameId::GulfThunder)) == 4);
}

TEST_CASE("identifyPakFile: exact fingerprint, another build by overlap, too different, truncated, not a pak, shared names, override pak") {
    synth::Games games;
    TempDir tmp("names");
    const fs::path d = tmp.path;
    const GameProfile& as2 = gameProfile(GameId::AirStrike2);
    const GameProfile& gulf = gameProfile(GameId::GulfThunder);
    const GameProfile& as3d = gameProfile(GameId::AirStrike3D);

    // The reference pak0 of as2, under any name.
    REQUIRE(games.pak(d / "x.bin", "as2", "pak0.apk"));
    PakIdentity id = identifyPakFile((d / "x.bin").string());
    CHECK(id.game == &as2);
    CHECK(id.pak == "pak0.apk");
    CHECK(id.exact);
    CHECK(id.overlap == 1.0);
    CHECK_FALSE(id.ambiguous);
    CHECK(id.entries == 201);
    CHECK(id.names.size() == 201);
    CHECK(identifyPak0File((d / "x.bin").string()) == &as2);
    CHECK(pakMatches(id, as2, "pak0.apk", kPak0MinOverlap, nullptr));
    CHECK_FALSE(pakMatches(id, gulf, "pak0.apk", 0.5, nullptr));
    CHECK_FALSE(pakMatches(id, as2, "pak1.apk", 0.5, nullptr));

    // Another build: two names of 201 replaced -> Jaccard 199/203 = 0.980, still as2's pak0.
    std::vector<std::string> names = synth::refNames("as2", "pak0.apk");
    names[0] = "models\\as2\\extra_a.bin";
    names[1] = "models\\as2\\extra_b.bin";
    REQUIRE(synth::writeNamedPak(d / "near.apk", names));
    id = identifyPakFile((d / "near.apk").string());
    CHECK(id.game == &as2);
    CHECK_FALSE(id.exact);
    CHECK(id.overlap == doctest::Approx(199.0 / 203.0));
    CHECK(identifyPak0File((d / "near.apk").string()) == &as2);
    // Ten replaced -> 0.906: best match as2 but not its pak0; a pak1 of another build is
    // taken at 0.9 (19 of 20 names plus one new one: 19/21).
    for (int i = 0; i < 10; ++i) names[i] = "models\\as2\\other" + std::to_string(i) + ".bin";
    REQUIRE(synth::writeNamedPak(d / "far.apk", names));
    id = identifyPakFile((d / "far.apk").string());
    CHECK(id.game == &as2);
    CHECK(id.overlap < kPak0MinOverlap);
    CHECK(identifyPak0File((d / "far.apk").string()) == nullptr);
    std::vector<std::string> pak1 = synth::refNames("gulf", "pak1.apk");
    pak1[0] = "maps\\gulf\\bonus.hsc";
    REQUIRE(synth::writeNamedPak(d / "pak1.apk", pak1));
    id = identifyPakFile((d / "pak1.apk").string());
    CHECK(id.game == &gulf);
    CHECK(id.pak == "pak1.apk");
    double overlap = 0;
    CHECK(pakMatches(id, gulf, "pak1.apk", kPakMinOverlap, &overlap));
    CHECK(overlap == doctest::Approx(19.0 / 21.0));
    CHECK_FALSE(pakMatches(id, as2, "pak1.apk", kPakMinOverlap, &overlap));
    CHECK(overlap == 0.0);

    // The sequels' pak2 share their names: exact for both, ambiguous alone.
    REQUIRE(games.pak(d / "pak2.apk", "gulf", "pak2.apk"));
    id = identifyPakFile((d / "pak2.apk").string());
    CHECK(id.exact);
    CHECK(id.ambiguous);
    CHECK(id.game == &as2); // the first in GameId order
    CHECK(pakMatches(id, gulf, "pak2.apk", kPakMinOverlap, nullptr));
    CHECK(pakMatches(id, as2, "pak2.apk", kPakMinOverlap, nullptr));
    CHECK_FALSE(pakMatches(id, as3d, "pak2.apk", kPakMinOverlap, nullptr));
    CHECK(identifyPak0File((d / "pak2.apk").string()) == nullptr);

    // Truncated: the reference pak cut by 100 bytes.
    const std::string whole = readFile((d / "x.bin").string());
    writeBytes(d / "cut.apk", whole.substr(0, whole.size() - 100));
    id = identifyPakFile((d / "cut.apk").string());
    CHECK(id.game == nullptr);
    CHECK(id.bytesShort == 100);
    CHECK(id.error == "truncated, 100 bytes short: unpack it again");
    CHECK(identifyPak0File((d / "cut.apk").string()) == nullptr);
    // Not a pak, and a pak of unknown names.
    writeBytes(d / "text.apk", std::string(5000, 'q'));
    id = identifyPakFile((d / "text.apk").string());
    CHECK(id.game == nullptr);
    CHECK(id.error.find("not a pak archive") != std::string::npos);
    REQUIRE(synth::writeNamedPak(d / "alien.apk", {"foo\\bar.txt", "baz.txt"}));
    id = identifyPakFile((d / "alien.apk").string());
    CHECK(id.error.empty());
    CHECK(id.game == nullptr);
    CHECK(id.overlap == 0.0);

    // An override pak: every name one of the game's (of pak0 or pak1); one foreign name spoils it.
    std::vector<std::string> over = {synth::refNames("as3d", "pak0.apk")[3], synth::refNames("as3d", "pak1.apk")[2]};
    REQUIRE(synth::writeNamedPak(d / "pak3.apk", over));
    id = identifyPakFile((d / "pak3.apk").string());
    CHECK(shareOfGameNames(id.names, as3d) == 1.0);
    CHECK(shareOfGameNames(id.names, as2) == 0.0);
    over.push_back("models\\nobody\\x.bin");
    REQUIRE(synth::writeNamedPak(d / "pak3b.apk", over));
    id = identifyPakFile((d / "pak3b.apk").string());
    CHECK(shareOfGameNames(id.names, as3d) == doctest::Approx(2.0 / 3.0));
    CHECK_FALSE(pakHasReference(as3d, "pak3.apk"));
    CHECK(pakHasReference(as3d, "pak2.apk"));

    // Without the synthetic name lists only the exact matches remain: the lists read again are
    // the real ones of the source tree ($AS3D_PAK_ENTRIES without lists is passed over), which
    // the synthetic names overlap by nothing.
    resetPakEntryNamesForTest();
    setenv("AS3D_PAK_ENTRIES", (d / "nowhere").string().c_str(), 1);
    id = identifyPakFile((d / "near.apk").string());
    CHECK(id.game == nullptr);
    CHECK(id.error.empty());
    id = identifyPakFile((d / "x.bin").string());
    CHECK(id.game == &as2);
    CHECK(shareOfGameNames(id.names, as3d) == 0.0);
    REQUIRE(pakEntryNamesOf(GameId::AirStrike2, "pak0.apk"));
    CHECK(pakEntryNamesOf(GameId::AirStrike2, "pak0.apk")->size() == 1609);
    unsetenv("AS3D_PAK_ENTRIES");
    resetPakEntryNamesForTest();
}

TEST_CASE("chooseGameData --paks without pak0.apk: a pak0.apk built from the loose files, kept under the data root") {
    synth::Games games;
    TempDir tmp("loose");
    const fs::path r = tmp.path;
    const GameProfile& gulf = gameProfile(GameId::GulfThunder);
    // A "Desert Hawk" install: pak0's content loose under data/, beside the other paks.
    const fs::path dir = r / "desert/app/data";
    for (const std::string& n : synth::refNames("gulf", "pak0.apk")) {
        std::string rel = n;
        for (char& c : rel) if (c == '\\') c = '/';
        writeBytes(dir / rel, "body of " + n);
    }
    REQUIRE(games.pak(dir / "pak1.apk", "gulf", "pak1.apk"));
    REQUIRE(games.pak(dir / "pak2.apk", "gulf", "pak2.apk"));
    REQUIRE(games.pak(dir / "pak4.apk", "gulf", "pak4.apk"));
    writeBytes(dir / "Settings.xml", "<s/>");

    const std::vector<LooseFile> files = looseFilesUnder(dir.string());
    CHECK(files.size() == 201 + 4);
    double coverage = 0;
    CHECK(gameOfLooseFiles(files, nullptr, &coverage) == &gulf);
    CHECK(coverage == 1.0);
    CHECK(gameOfLooseFiles(files, &gameProfile(GameId::AirStrike2), &coverage) == nullptr);
    CHECK(coverage == 0.0);

    unsetenv("AS3D_GAME");
    GameData d;
    std::string error;
    REQUIRE(chooseGameData(r.string(), "", dir.string(), &d, &error));
    CHECK(d.game == &gulf);
    REQUIRE(d.paks.size() == 4);
    const std::string built = r.string() + "/third_party_local/built/gulf/pak0.apk";
    CHECK(d.paks[0] == built);
    CHECK(d.paks[1] == dir.string() + "/pak1.apk");
    CHECK(d.paks[3] == dir.string() + "/pak4.apk");
    CHECK(d.missing.size() == 1); // the extracted files only
    CHECK(d.settingsXml == dir.string() + "/Settings.xml");
    CHECK(identifyPak0File(built) == &gulf);
    PakTable t;
    REQUIRE(readPakTableFile(built, &t, &error));
    CHECK(t.entries.size() == 201);
    // The built pak serves the files, in the reference order.
    std::unique_ptr<IFileSource> src = makePakSource(openFileStream(built));
    REQUIRE(src);
    Blob b;
    REQUIRE(src->read("maps\\gulf\\levels.txt", b));
    CHECK(bytesOf(b) == "body of maps\\gulf\\levels.txt");
    // A second call reuses it; with the game named it is the same; another game is refused.
    const auto stamp = fs::last_write_time(built);
    REQUIRE(chooseGameData(r.string(), "gulf", dir.string(), &d, &error));
    CHECK(d.paks[0] == built);
    CHECK(fs::last_write_time(built) == stamp);
    CHECK_FALSE(chooseGameData(r.string(), "as2", dir.string(), &d, &error));
    CHECK(error.find("0.0 % of AirStrike 2's pak0.apk entries") != std::string::npos);
    // Too few files: the message gives the share.
    const fs::path part = r / "part";
    int n = 0;
    for (const std::string& name : synth::refNames("gulf", "pak0.apk")) {
        if (++n > 150) break;
        std::string rel = name;
        for (char& c : rel) if (c == '\\') c = '/';
        writeBytes(part / rel, "x");
    }
    CHECK_FALSE(chooseGameData(r.string(), "", part.string(), &d, &error));
    CHECK(error.find("74.6 % of any game's pak0.apk entries") != std::string::npos);
    CHECK(error.find("99 % needed") != std::string::npos);
}
