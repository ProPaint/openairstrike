// Acceptance tests for the virtual file system (WP-11). See docs/spec/pak.md.
#include "doctest.h"

#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "as3d/core.h"
#include "as3d/vfs.h"
#include "test_data.h"

using as3d::u32;
using as3d::u8;

// ---------------------------------------------------------------------------
// Minimal self-contained SHA-1 (public-domain algorithm), used only to check
// bytes read through the Vfs against testdata/golden/<game>/pak_manifest.json.
// ---------------------------------------------------------------------------
namespace {

u32 rol(u32 x, u32 c) { return (x << c) | (x >> (32 - c)); }

std::string sha1Hex(const as3d::Blob& data) {
    u32 h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;

    std::vector<u8> msg(data.begin(), data.end());
    const std::uint64_t bitLen = static_cast<std::uint64_t>(data.size()) * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<u8>((bitLen >> (i * 8)) & 0xFF));

    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        u32 w[80];
        for (int i = 0; i < 16; ++i) {
            size_t p = chunk + static_cast<size_t>(i) * 4;
            w[i] = (u32(msg[p]) << 24) | (u32(msg[p + 1]) << 16) | (u32(msg[p + 2]) << 8) |
                   u32(msg[p + 3]);
        }
        for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        u32 a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            u32 f, k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            u32 temp = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = temp;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    char buf[41];
    std::snprintf(buf, sizeof buf, "%08x%08x%08x%08x%08x", h0, h1, h2, h3, h4);
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// Tiny hand-written reader for pak_manifest.json. The file has a fixed,
// regular layout (see tools/paktool.py's `manifest` command): a JSON array of
// objects, each with "name", "pak", "size", "flag", "sha1" in that order.
// ---------------------------------------------------------------------------

struct ManifestEntry {
    std::string name;
    std::string pak;
    size_t size = 0;
    int flag = 0;
    std::string sha1;
};

std::string readWholeFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Finds "key" starting at or after pos.
size_t findKey(const std::string& text, size_t pos, const char* key) {
    std::string pat = std::string("\"") + key + "\"";
    return text.find(pat, pos);
}

// Reads a JSON string value; pos must be at (or before) the key occurrence.
// Advances pos past the closing quote.
std::string readStringValue(const std::string& text, size_t& pos) {
    size_t colon = text.find(':', pos);
    size_t q1 = text.find('"', colon);
    size_t i = q1 + 1;
    std::string out;
    while (i < text.size() && text[i] != '"') {
        if (text[i] == '\\' && i + 1 < text.size()) {
            out.push_back(text[i + 1]);
            i += 2;
        } else {
            out.push_back(text[i]);
            i += 1;
        }
    }
    pos = i + 1;
    return out;
}

long readNumberValue(const std::string& text, size_t& pos) {
    size_t colon = text.find(':', pos);
    size_t i = colon + 1;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) i++;
    size_t start = i;
    while (i < text.size() &&
           (std::isdigit(static_cast<unsigned char>(text[i])) || text[i] == '-'))
        i++;
    pos = i;
    return std::stol(text.substr(start, i - start));
}

std::vector<ManifestEntry> parseManifest(const std::string& path) {
    std::vector<ManifestEntry> entries;
    std::string text = readWholeFile(path);
    size_t pos = 0;
    for (;;) {
        size_t namePos = findKey(text, pos, "name");
        if (namePos == std::string::npos) break;
        ManifestEntry e;
        size_t p = namePos;
        e.name = readStringValue(text, p);
        p = findKey(text, p, "pak");
        e.pak = readStringValue(text, p);
        p = findKey(text, p, "size");
        e.size = static_cast<size_t>(readNumberValue(text, p));
        p = findKey(text, p, "flag");
        e.flag = static_cast<int>(readNumberValue(text, p));
        p = findKey(text, p, "sha1");
        e.sha1 = readStringValue(text, p);
        pos = p;
        entries.push_back(std::move(e));
    }
    return entries;
}

// ---------------------------------------------------------------------------
// Synthetic pak builder, for the override and malformed-input tests. Mirrors
// docs/spec/pak.md / tools/paktool.py exactly.
// ---------------------------------------------------------------------------

constexpr size_t kKeySize = 1024;
constexpr size_t kEntrySize = 76;

void writeLE32(u8* p, u32 v) {
    p[0] = static_cast<u8>(v & 0xFF);
    p[1] = static_cast<u8>((v >> 8) & 0xFF);
    p[2] = static_cast<u8>((v >> 16) & 0xFF);
    p[3] = static_cast<u8>((v >> 24) & 0xFF);
}

std::array<u8, kKeySize> testKey() {
    std::array<u8, kKeySize> key{};
    for (size_t i = 0; i < kKeySize; ++i) key[i] = static_cast<u8>((i * 37 + 11) & 0xFF);
    return key;
}

struct RawEntry {
    std::string name;
    as3d::Blob content;
    bool encrypted = false;
};

as3d::Blob buildPak(const std::vector<RawEntry>& entries) {
    auto key = testKey();
    as3d::Blob data(0x410, 0);
    const u8 magic[8] = {0x00, 0x00, 0x80, 0x3F, 0x99, 0x99, 0x00, 0x00};
    std::memcpy(data.data(), magic, 8);
    std::memcpy(data.data() + 0x10, key.data(), kKeySize);

    struct Located {
        std::string name;
        u32 offset;
        u32 size;
        u32 flag;
    };
    std::vector<Located> located;
    for (const auto& e : entries) {
        u32 offset = static_cast<u32>(data.size());
        as3d::Blob body = e.content;
        if (e.encrypted) {
            for (size_t j = 0; j < body.size(); ++j) body[j] ^= key[j % kKeySize];
        }
        data.insert(data.end(), body.begin(), body.end());
        located.push_back({e.name, offset, static_cast<u32>(e.content.size()),
                            e.encrypted ? 1u : 0u});
    }

    u32 tableOffset = static_cast<u32>(data.size());
    u32 count = static_cast<u32>(located.size());
    writeLE32(data.data() + 8, tableOffset);
    writeLE32(data.data() + 12, count);

    as3d::Blob table(static_cast<size_t>(count) * kEntrySize, 0);
    for (u32 i = 0; i < count; ++i) {
        const Located& l = located[i];
        u8* e = table.data() + static_cast<size_t>(i) * kEntrySize;
        size_t n = l.name.size() < 63 ? l.name.size() : 63;
        std::memcpy(e, l.name.data(), n);
        writeLE32(e + 64, l.offset);
        writeLE32(e + 68, l.size);
        writeLE32(e + 72, l.flag);
    }
    for (size_t i = 0; i < table.size(); ++i) table[i] ^= key[i % kKeySize];
    data.insert(data.end(), table.begin(), table.end());
    return data;
}

} // namespace

// ---------------------------------------------------------------------------
// 1. Mount pak0/pak1/pak2, check counts and every manifest entry.
// ---------------------------------------------------------------------------
TEST_CASE("Vfs merges the paks of the game and matches the golden manifest") {
    AS3D_REQUIRE_DATA();

    as3d::Vfs vfs;
    REQUIRE(testdata::mountGamePaks(vfs));

    auto names = vfs.list();
    const size_t want = static_cast<size_t>(testdata::expectedInt("paks.files"));
    CHECK(names.size() == want);

    auto manifest = parseManifest(testdata::goldenDir() + "/pak_manifest.json");
    REQUIRE(manifest.size() == want);

    for (const auto& e : manifest) {
        as3d::Blob out;
        bool ok = vfs.read(e.name, out);
        CHECK(ok);
        if (!ok) continue;
        CHECK(out.size() == e.size);
        CHECK(sha1Hex(out) == e.sha1);
    }
}

// ---------------------------------------------------------------------------
// 2. Case- and slash-insensitive lookup of a known text file.
// ---------------------------------------------------------------------------
TEST_CASE("Vfs read is case- and slash-insensitive") {
    AS3D_REQUIRE_DATA();

    as3d::Vfs vfs;
    REQUIRE(testdata::mountGamePaks(vfs));

    as3d::Blob out;
    REQUIRE(vfs.read("MAPS/Levels.txt", out));
    std::string text(out.begin(), out.end());
    CHECK(text.find("name") != std::string::npos);
    if (testdata::gameKey() == "as3d") CHECK(text.find("Mission 1") != std::string::npos);
}

// ---------------------------------------------------------------------------
// 3. Override order and encrypted-flag round trip, with synthetic paks.
// ---------------------------------------------------------------------------
TEST_CASE("Vfs: a later mount overrides an earlier one, encrypted bodies decrypt") {
    as3d::Blob lowerContent{'l', 'o', 'w', 'e', 'r'};
    as3d::Blob upperContent{'U', 'P', 'P', 'E', 'R'};

    as3d::Blob pak0 = buildPak({{"scripts\\common.txt", lowerContent, false}});
    as3d::Blob pak1 = buildPak({{"scripts\\common.txt", upperContent, true}});

    auto src0 = as3d::makePakSource(as3d::makeMemoryStream(pak0));
    auto src1 = as3d::makePakSource(as3d::makeMemoryStream(pak1));
    REQUIRE(src0 != nullptr);
    REQUIRE(src1 != nullptr);

    as3d::Vfs vfs;
    vfs.mount(std::move(src0));

    as3d::Blob out;
    REQUIRE(vfs.read("scripts\\common.txt", out));
    CHECK(out == lowerContent);

    vfs.mount(std::move(src1));
    REQUIRE(vfs.read("SCRIPTS/Common.TXT", out));
    CHECK(out == upperContent);
}

// ---------------------------------------------------------------------------
// 4. Malformed input never crashes.
// ---------------------------------------------------------------------------
TEST_CASE("makePakSource rejects malformed archives without crashing") {
    SUBCASE("bad magic") {
        as3d::Blob data(0x410, 0); // zeroed, magic does not match
        auto src = as3d::makePakSource(as3d::makeMemoryStream(data));
        CHECK(src == nullptr);
    }

    SUBCASE("truncated (too short for header and key)") {
        as3d::Blob data(16, 0);
        const u8 magic[8] = {0x00, 0x00, 0x80, 0x3F, 0x99, 0x99, 0x00, 0x00};
        std::memcpy(data.data(), magic, 8);
        auto src = as3d::makePakSource(as3d::makeMemoryStream(data));
        CHECK(src == nullptr);
    }

    SUBCASE("table offset past end of stream") {
        as3d::Blob data = buildPak({{"a.txt", as3d::Blob{'x'}, false}});
        u32 badOffset = static_cast<u32>(data.size()) + 100000;
        writeLE32(data.data() + 8, badOffset);
        auto src = as3d::makePakSource(as3d::makeMemoryStream(data));
        CHECK(src == nullptr);
    }

    SUBCASE("entry body past end of stream") {
        // Hand-build a pak whose single entry claims a body far past EOF.
        // The archive itself is otherwise well-formed.
        auto key = testKey();
        as3d::Blob data(0x410, 0);
        const u8 magic[8] = {0x00, 0x00, 0x80, 0x3F, 0x99, 0x99, 0x00, 0x00};
        std::memcpy(data.data(), magic, 8);
        std::memcpy(data.data() + 0x10, key.data(), kKeySize);

        u32 tableOffset = static_cast<u32>(data.size());
        writeLE32(data.data() + 8, tableOffset);
        writeLE32(data.data() + 12, 1);

        as3d::Blob table(kEntrySize, 0);
        std::memcpy(table.data(), "bad.txt", 7);
        writeLE32(table.data() + 64, tableOffset); // offset within bounds
        writeLE32(table.data() + 68, 999999);      // size way past EOF
        writeLE32(table.data() + 72, 0);
        for (size_t i = 0; i < table.size(); ++i) table[i] ^= key[i % kKeySize];
        data.insert(data.end(), table.begin(), table.end());

        auto src = as3d::makePakSource(as3d::makeMemoryStream(data));
        // Either the whole archive is rejected, or the bad entry is dropped /
        // fails to read. Both are acceptable; a crash is not.
        if (src) {
            as3d::Blob out;
            CHECK_FALSE(src->read("bad.txt", out));
        }
    }
}

// ---------------------------------------------------------------------------
// 5. DirSource over the extracted directory tree.
// ---------------------------------------------------------------------------
TEST_CASE("DirSource reads extracted files case-insensitively") {
    AS3D_REQUIRE_DATA();

    auto src = as3d::makeDirSource(testdata::extractedDir());
    REQUIRE(src != nullptr);

    as3d::Blob out;
    REQUIRE(src->read(as3d::normalizePath("maps\\levels.txt"), out));

    auto manifest = parseManifest(testdata::goldenDir() + "/pak_manifest.json");
    std::string want;
    for (const auto& e : manifest) {
        if (as3d::normalizePath(e.name) == as3d::normalizePath("maps\\levels.txt")) {
            want = e.sha1;
            break;
        }
    }
    REQUIRE(!want.empty());
    CHECK(sha1Hex(out) == want);
}

// ---------------------------------------------------------------------------
// 6. The read-ahead thread (Vfs::prefetchAsync, docs/3ds-port.md "Prefetch overlap (round 4,
// R4.10)") over a slow stream: the runs in the order of first use, reads in and out of order
// while it runs, a miss (a file outside the plan), the synchronous prefetch during a plan,
// destruction and dropPrefetched while the reader runs; the bytes identical throughout.
// ---------------------------------------------------------------------------
namespace {

// What a slow stream records; it outlives the stream (the Vfs owns the stream).
struct SlowState {
    std::mutex mutex;
    std::vector<std::pair<size_t, size_t>> reads; // offset, size
    size_t bodiesEnd = 0;                         // where the pak's table starts
    int delayMs = 15;
    size_t count() {
        std::lock_guard<std::mutex> lock(mutex);
        return reads.size();
    }
    std::vector<std::pair<size_t, size_t>> copy() {
        std::lock_guard<std::mutex> lock(mutex);
        return reads;
    }
};

class SlowStream : public as3d::IStream {
public:
    SlowStream(as3d::Blob data, std::shared_ptr<SlowState> state) : data_(std::move(data)), state_(std::move(state)) {}
    size_t size() override { return data_.size(); }
    bool readAt(size_t offset, void* out, size_t n) override {
        if (n == 0) return true;
        if (offset > data_.size() || n > data_.size() - offset) return false;
        // The pak header, key and table are read at mount: only the bodies are slow and recorded.
        if (offset >= 0x410 && offset < state_->bodiesEnd) {
            std::this_thread::sleep_for(std::chrono::milliseconds(state_->delayMs));
            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->reads.push_back({offset, n});
        }
        std::memcpy(out, data_.data() + offset, n);
        return true;
    }

private:
    as3d::Blob data_;
    std::shared_ptr<SlowState> state_;
};

as3d::Blob pseudoRandom(size_t n, u32 seed) {
    as3d::Blob b(n);
    for (size_t i = 0; i < n; ++i) {
        seed = seed * 1664525u + 1013904223u;
        b[i] = static_cast<u8>(seed >> 24);
    }
    return b;
}

// Three groups of three small files, 70 KB fillers between them (more than the 64 KB gap a run
// reads through, so each group is one run), two more files outside the plan (d1, d2).
struct PrefetchFixture {
    std::vector<RawEntry> entries;
    std::vector<std::string> plan = {"c1", "c2", "c3", "a1", "a2", "a3", "b1", "b2", "b3"};
    std::vector<std::string> misses = {"d1", "d2"};
    std::vector<std::string> all;
    std::shared_ptr<SlowState> state = std::make_shared<SlowState>();

    PrefetchFixture() {
        u32 seed = 7;
        auto add = [&](const std::string& name, size_t n, bool enc) {
            entries.push_back({name, pseudoRandom(n, seed++), enc});
        };
        add("a1", 3000, false);
        add("a2", 4500, true);
        add("a3", 2200, false);
        add("filler1", 70 * 1024, false);
        add("b1", 1500, true);
        add("b2", 6000, false);
        add("b3", 2600, true);
        add("filler2", 70 * 1024, false);
        add("c1", 5000, false);
        add("c2", 1200, true);
        add("c3", 3300, false);
        add("filler3", 70 * 1024, false);
        add("d1", 2000, true);
        add("d2", 2500, false);
        all = plan;
        all.insert(all.end(), misses.begin(), misses.end());
        state->bodiesEnd = 0x410;
        for (const RawEntry& e : entries) state->bodiesEnd += e.content.size();
    }
    size_t offsetOf(const std::string& name) const {
        size_t off = 0x410;
        for (const RawEntry& e : entries) {
            if (e.name == name) return off;
            off += e.content.size();
        }
        return 0;
    }
    const as3d::Blob& content(const std::string& name) const {
        for (const RawEntry& e : entries)
            if (e.name == name) return e.content;
        static const as3d::Blob none;
        return none;
    }
    size_t planBytes() const {
        size_t n = 0;
        for (const std::string& p : plan) n += content(p).size();
        return n;
    }
    void mount(as3d::Vfs& vfs) const {
        auto src = as3d::makePakSource(std::unique_ptr<as3d::IStream>(new SlowStream(buildPak(entries), state)));
        REQUIRE(src != nullptr);
        vfs.mount(std::move(src));
    }
    // Every file of the plan and the misses, read through `vfs`, against its content.
    void checkAll(as3d::Vfs& vfs) const {
        for (const std::string& name : all) {
            as3d::Blob out;
            REQUIRE(vfs.read(name, out));
            CHECK(out == content(name));
        }
    }
};

} // namespace

TEST_CASE("Vfs::prefetchAsync: the runs in the order of first use, reads in and out of order, a miss") {
    PrefetchFixture fx;
    std::unique_ptr<as3d::PrefetchThreading> threading = as3d::makeStdPrefetchThreading();
    REQUIRE(threading != nullptr); // the desktop has std::thread
    as3d::Vfs vfs;
    fx.mount(vfs);
    vfs.setPrefetchThreading(std::move(threading));
    CHECK(std::string(vfs.prefetchThreadDescription()) == "std::thread");

    vfs.prefetchAsync(fx.plan);
    CHECK(vfs.prefetchStats().threaded);
    CHECK(vfs.prefetchStats().steps == 3);
    CHECK(vfs.prefetchStats().files == 9);

    as3d::Blob out;
    REQUIRE(vfs.read("d1", out)); // a miss while the reader runs: from the file, no wait for the plan
    CHECK(out == fx.content("d1"));
    REQUIRE(vfs.read("c1", out)); // the plan's first file: waits for its run (the first one read)
    CHECK(out == fx.content("c1"));
    REQUIRE(vfs.read("b3", out)); // out of order: the last run
    CHECK(out == fx.content("b3"));
    REQUIRE(vfs.read("a2", out));
    CHECK(out == fx.content("a2"));
    REQUIRE(vfs.read("c1", out)); // twice: still in memory
    CHECK(out == fx.content("c1"));
    CHECK_FALSE(vfs.read("nowhere", out));

    vfs.prefetchJoin();
    const as3d::Vfs::PrefetchStats st = vfs.prefetchStats();
    CHECK(st.done);
    CHECK(st.threaded);
    CHECK(st.stepsRun == 3);
    CHECK(st.bytes == fx.planBytes());
    CHECK(st.readMs >= 3 * fx.state->delayMs);
    CHECK(st.waits >= 1); // b3 at least (c1 too unless its run landed first)
    CHECK(st.waits <= 3);
    CHECK(st.waitMs >= 0.0);

    // The stream saw the three runs, in the plan's order of first use (C, A, B), and the miss.
    const auto reads = fx.state->copy();
    REQUIRE(reads.size() == 4);
    std::vector<std::pair<size_t, size_t>> runs;
    for (const auto& r : reads)
        if (r.first != fx.offsetOf("d1")) runs.push_back(r);
    REQUIRE(runs.size() == 3);
    CHECK(runs[0].first == fx.offsetOf("c1"));
    CHECK(runs[0].second == fx.content("c1").size() + fx.content("c2").size() + fx.content("c3").size());
    CHECK(runs[1].first == fx.offsetOf("a1"));
    CHECK(runs[2].first == fx.offsetOf("b1"));

    // Every file, planned or not, in memory or from the file: the same bytes.
    fx.checkAll(vfs);
    CHECK(fx.state->count() == 4 + fx.misses.size()); // the misses read again, the plan's files not
    vfs.dropPrefetched();
    fx.checkAll(vfs); // all from the file now
    CHECK(fx.state->count() == 4 + fx.misses.size() + fx.all.size());
}

TEST_CASE("Vfs::prefetchAsync: without a thread it reads on the caller; the synchronous prefetch during a plan") {
    PrefetchFixture fx;
    fx.state->delayMs = 2;
    SUBCASE("no threading: the same plan, read before prefetchAsync returns") {
        as3d::Vfs vfs;
        fx.mount(vfs);
        vfs.prefetchAsync(fx.plan);
        const as3d::Vfs::PrefetchStats st = vfs.prefetchStats();
        CHECK_FALSE(st.threaded);
        CHECK(st.done);
        CHECK(st.steps == 3);
        CHECK(st.stepsRun == 3);
        CHECK(st.bytes == fx.planBytes());
        CHECK(st.waits == 0);
        CHECK(fx.state->count() == 3);
        const auto reads = fx.state->copy();
        CHECK(reads[0].first == fx.offsetOf("c1"));
        fx.checkAll(vfs);
        CHECK(fx.state->count() == 3 + fx.misses.size()); // the plan's files from memory
    }
    SUBCASE("a plan of unknown files plans nothing") {
        as3d::Vfs vfs;
        fx.mount(vfs);
        vfs.setPrefetchThreading(as3d::makeStdPrefetchThreading());
        vfs.prefetchAsync({"x1", "x2"});
        const as3d::Vfs::PrefetchStats st = vfs.prefetchStats();
        CHECK(st.files == 0);
        CHECK(st.steps == 0);
        CHECK(st.done);
        CHECK_FALSE(st.threaded);
        fx.checkAll(vfs);
    }
    SUBCASE("Vfs::prefetch of pending and new files while the reader runs") {
        fx.state->delayMs = 15;
        as3d::Vfs vfs;
        fx.mount(vfs);
        vfs.setPrefetchThreading(as3d::makeStdPrefetchThreading());
        vfs.prefetchAsync(fx.plan);
        // The definitions' read-ahead (defs.cpp): a1 is pending (skipped), d1 is read now.
        const std::vector<std::string> defs = {"a1", "d1"};
        CHECK(vfs.prefetch(defs) == fx.content("d1").size());
        as3d::Blob out;
        REQUIRE(vfs.read("d1", out));
        CHECK(out == fx.content("d1"));
        REQUIRE(vfs.read("a1", out));
        CHECK(out == fx.content("a1"));
        vfs.dropPrefetched(&defs); // drops both; a1 is read again from the file below
        fx.checkAll(vfs);
        vfs.prefetchJoin();
        CHECK(vfs.prefetchStats().stepsRun == 3);
        CHECK(fx.state->count() == 3 + 1 + 1 + fx.misses.size()); // the runs, the defs' d1, a1 again, the misses
    }
}

TEST_CASE("Vfs::prefetchAsync: destroyed, dropped and restarted while the reader runs") {
    PrefetchFixture fx;
    fx.state->delayMs = 20;
    SUBCASE("the destructor joins the reader; nothing is read after it") {
        {
            as3d::Vfs vfs;
            fx.mount(vfs);
            vfs.setPrefetchThreading(as3d::makeStdPrefetchThreading());
            vfs.prefetchAsync(fx.plan);
            CHECK(vfs.prefetchStats().threaded);
        }
        const size_t atDestruction = fx.state->count();
        CHECK(atDestruction <= 3);
        std::this_thread::sleep_for(std::chrono::milliseconds(3 * fx.state->delayMs));
        CHECK(fx.state->count() == atDestruction);
    }
    SUBCASE("dropPrefetched(nullptr) joins the reader; the files come from the file afterwards") {
        as3d::Vfs vfs;
        fx.mount(vfs);
        vfs.setPrefetchThreading(as3d::makeStdPrefetchThreading());
        vfs.prefetchAsync(fx.plan);
        vfs.dropPrefetched();
        const as3d::Vfs::PrefetchStats st = vfs.prefetchStats();
        CHECK(st.done);
        CHECK(st.stepsRun <= 3);
        const size_t before = fx.state->count();
        fx.checkAll(vfs);
        CHECK(fx.state->count() == before + fx.all.size());
    }
    SUBCASE("a second prefetchAsync joins the first") {
        as3d::Vfs vfs;
        fx.mount(vfs);
        vfs.setPrefetchThreading(as3d::makeStdPrefetchThreading());
        vfs.prefetchAsync(fx.plan);
        const std::vector<std::string> again = {"b1", "d2"};
        vfs.prefetchAsync(again); // b1 may be in memory already (then only d2 is planned)
        const as3d::Vfs::PrefetchStats st = vfs.prefetchStats();
        CHECK(st.files == 2);
        CHECK(st.steps >= 1);
        CHECK(st.steps <= 2);
        fx.checkAll(vfs);
        vfs.prefetchJoin();
        CHECK(vfs.prefetchStats().done);
        fx.checkAll(vfs);
    }
    SUBCASE("setPrefetchThreading(nullptr) joins the reader; the next plan reads on the caller") {
        as3d::Vfs vfs;
        fx.mount(vfs);
        vfs.setPrefetchThreading(as3d::makeStdPrefetchThreading());
        vfs.prefetchAsync(fx.plan);
        vfs.setPrefetchThreading(nullptr);
        CHECK(vfs.prefetchStats().done);
        CHECK(std::string(vfs.prefetchThreadDescription()) == "none");
        vfs.prefetchAsync(fx.plan);
        CHECK_FALSE(vfs.prefetchStats().threaded);
        CHECK(vfs.prefetchStats().done);
        fx.checkAll(vfs);
    }
}
