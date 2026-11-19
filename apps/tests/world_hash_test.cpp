// World::stateHash / stateHashFull (online desync detection, docs/spec/issues/180-online-coop.md
// 4.4) and the simulation data fingerprint (5.3).
#include "doctest.h"

#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "as3d/defs.h"
#include "as3d/input.h"
#include "as3d/sim_fingerprint.h"
#include "as3d/vfs.h"
#include "as3d/world.h"
#include "test_data.h"

using namespace as3d;

namespace {

// Mission 1 under the scripted pilot (the bot regression's setup); the world keeps the files
// and definitions it points into.
struct HashRun {
    Vfs vfs;
    DefDatabase db;
    World w;
    std::vector<u64> hashes; // every 20 frames

    void play(u32 frames) {
        vfs.mount(makeDirSource(testdata::extractedDir()));
        REQUIRE(db.load(vfs));
        WorldConfig cfg;
        cfg.rules = &testdata::game().rules;
        cfg.seed = 1;
        cfg.difficulty = 2;
        w.init(vfs, db, cfg);
        std::string err;
        REQUIRE(w.loadLevel("1", &err));
        for (u32 f = 0; f < frames; ++f) {
            w.step(botInput(w, f).toPlayerInput());
            if (w.frame() % 20 == 0) hashes.push_back(w.stateHash());
        }
    }
};

class FingerprintSource : public IFileSource {
public:
    void add(const std::string& path, const std::string& text) { files_[normalizePath(path)] = text; }
    bool exists(const std::string& path) override { return files_.count(path) != 0; }
    bool read(const std::string& path, Blob& out) override {
        auto it = files_.find(path);
        if (it == files_.end()) return false;
        out.assign(it->second.begin(), it->second.end());
        return true;
    }
    void list(std::vector<std::string>& out) override {
        for (const auto& kv : files_) out.push_back(kv.first);
    }

private:
    std::map<std::string, std::string> files_;
};

using FileList = std::vector<std::pair<std::string, std::string>>;

std::string fingerprintOf(const std::vector<FileList>& sources) {
    Vfs vfs;
    for (const FileList& files : sources) {
        auto src = std::make_unique<FingerprintSource>();
        for (const auto& f : files) src->add(f.first, f.second);
        vfs.mount(std::move(src));
    }
    return simDataFingerprint(vfs);
}

} // namespace

TEST_CASE("world hash: two runs agree frame by frame, and the hash moves with the state") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    HashRun a, b;
    a.play(600);
    b.play(600);
    REQUIRE(a.hashes.size() == 30);
    CHECK(a.hashes == b.hashes);
    const u64 full = a.w.stateHashFull();
    CHECK(full == b.w.stateHashFull());
    CHECK(full != 0);
    int changes = 0;
    for (size_t i = 1; i < a.hashes.size(); ++i) changes += a.hashes[i] != a.hashes[i - 1] ? 1 : 0;
    CHECK(changes == static_cast<int>(a.hashes.size()) - 1); // the frame number alone moves it

    World& w = a.w;
    const u64 h0 = w.stateHash();
    CHECK(h0 == w.stateHash());
    int slot = -1;
    for (int i = 0; i < kMaxEntitySlots && slot < 0; ++i)
        if (w.entity(i).inUse) slot = i;
    REQUIRE(slot >= 0);
    Entity& e = w.entity(slot);
    const u32 saved = e.fields[F_ORIGIN];
    e.setF(F_ORIGIN, e.f(F_ORIGIN) + 1.0f);
    CHECK(w.stateHash() != h0);
    // Every NaN is the same NaN to the hash (x86, ARM and wasm make different ones).
    e.fields[F_ORIGIN] = 0x7fc00000u;
    const u64 hNaN = w.stateHash();
    e.fields[F_ORIGIN] = 0xffc00000u;
    CHECK(w.stateHash() == hNaN);
    e.fields[F_ORIGIN] = 0x7f800123u;
    CHECK(w.stateHash() == hNaN);
    e.fields[F_ORIGIN] = 0x7f800000u; // +inf is not a NaN
    CHECK(w.stateHash() != hNaN);
    e.fields[F_ORIGIN] = saved;
    CHECK(w.stateHash() == h0);
}

TEST_CASE("sim fingerprint: media files are left out, the pak layout does not matter") {
    const FileList all = {
        {"scripts/a.scr", "script a"}, {"objects/heli.obj", "object"}, {"maps/m1.hsc", "map"},
        {"models/heli.mdl", "model"},  {"textures/heli.tga", "pixels"}, {"sounds/boom.wav", "samples"},
        {"music/theme.mo3", "music"},
    };
    const std::string fp = fingerprintOf({all});
    REQUIRE(fp.size() == 64);
    CHECK(fp.find_first_not_of("0123456789abcdef") == std::string::npos);

    // Split over two sources, in another order: the same.
    CHECK(fingerprintOf({{all[4], all[0], all[2]}, {all[6], all[1], all[3], all[5]}}) == fp);
    // A file in both sources counts once, as the source that wins serves it.
    CHECK(fingerprintOf({all, {all[0]}}) == fp);

    // Pictures, sounds and music do not count; their absence neither.
    FileList changed = all;
    changed[4].second = "other pixels";
    changed[5].second = "other samples";
    CHECK(fingerprintOf({changed}) == fp);
    CHECK(fingerprintOf({{all[0], all[1], all[2], all[3]}}) == fp);

    // Scripts, objects, maps and models do.
    for (size_t k = 0; k < 4; ++k) {
        FileList c = all;
        c[k].second += " ";
        CHECK(fingerprintOf({c}) != fp);
    }
    FileList renamed = all;
    renamed[0].first = "scripts/b.scr";
    CHECK(fingerprintOf({renamed}) != fp);
    FileList extra = all;
    extra.push_back({"readme.txt", ""});
    CHECK(fingerprintOf({extra}) != fp);

    Vfs vfs;
    auto src = std::make_unique<FingerprintSource>();
    for (const auto& f : all) src->add(f.first, f.second);
    vfs.mount(std::move(src));
    u8 bytes[32];
    simDataFingerprintBytes(vfs, bytes);
    char hex[65];
    for (int i = 0; i < 32; ++i) std::snprintf(hex + 2 * i, 3, "%02x", bytes[i]);
    CHECK(std::string(hex) == fp);
}

TEST_CASE("sim fingerprint: the game's own files") {
    AS3D_REQUIRE_DATA();
    Vfs vfs;
    vfs.mount(makeDirSource(testdata::extractedDir()));
    const std::string fp = simDataFingerprint(vfs);
    CHECK(fp.size() == 64);
    CHECK(fp != std::string(64, '0'));
    CHECK(simDataFingerprint(vfs) == fp);
    MESSAGE("sim data fingerprint (" << testdata::gameKey() << "): " << fp);
}
