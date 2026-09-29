// Profile file: our own small versioned format (see as3d/profile.h), written atomically.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "as3d/profile.h"

namespace as3d {

namespace {

constexpr char kMagic[8] = {'A', 'S', '3', 'D', 'P', 'R', 'O', 'F'};
constexpr u32 kVersion = 1;
constexpr size_t kHeaderSize = 8 + 4 + 4 + 4;
constexpr size_t kMaxFileSize = 1 << 20; // far above any real profile

u32 crc32(const u8* data, size_t n) {
    static u32 table[256];
    static bool init = false;
    if (!init) {
        for (u32 i = 0; i < 256; i++) {
            u32 c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    u32 c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

struct Writer {
    std::vector<u8> b;
    void u8v(u32 v) { b.push_back(static_cast<u8>(v)); }
    void u16v(u32 v) { u8v(v & 0xFF); u8v((v >> 8) & 0xFF); }
    void u32v(u32 v) { u16v(v & 0xFFFF); u16v(v >> 16); }
    void i32v(i32 v) { u32v(static_cast<u32>(v)); }
    void i64v(std::int64_t v) { u32v(static_cast<u32>(static_cast<std::uint64_t>(v) & 0xFFFFFFFFu)); u32v(static_cast<u32>(static_cast<std::uint64_t>(v) >> 32)); }
    void bytes(const void* p, size_t n) { const u8* c = static_cast<const u8*>(p); b.insert(b.end(), c, c + n); }
    void str8(const std::string& s) {
        const size_t n = std::min<size_t>(s.size(), 255);
        u8v(static_cast<u32>(n));
        bytes(s.data(), n);
    }
};

std::int64_t readI64(ByteReader& r) {
    const std::uint64_t lo = r.readU32(), hi = r.readU32();
    return static_cast<std::int64_t>(lo | (hi << 32));
}

// Reads a length-prefixed string; the length is checked against what remains first.
bool readStr8(ByteReader& r, std::string& out, size_t maxLen) {
    const size_t n = r.readU8();
    if (r.failed() || n > maxLen || n > r.remaining()) return false;
    out.assign(n, '\0');
    return r.readBytes(out.data(), n);
}

void writeProgress(Writer& w, const Progress& p) {
    w.u8v(kHighScoreCount);
    for (const HighScore& h : p.scores) {
        w.str8(h.name.substr(0, 31));
        w.i64v(h.score);
        w.u8v(static_cast<u32>(h.rank));
    }
    w.u8v(static_cast<u32>(p.helicopterCount));
    for (int i = 0; i < p.helicopterCount; i++) w.u8v(p.helicopterUnlocked[i] ? 1 : 0);
    w.u8v(static_cast<u32>(p.missionCount));
    for (int i = 0; i < p.missionCount; i++) w.u8v(p.missionUnlocked[i] ? 1 : 0);
}

bool readProgress(ByteReader& r, Progress& p, std::string& why) {
    // The counts are those of the Progress being replaced (its game); a file must match them.
    Progress out = Progress::defaults();
    out.helicopterCount = p.helicopterCount;
    out.missionCount = p.missionCount;
    const size_t n = r.readU8();
    if (r.failed() || n != kHighScoreCount) { why = "bad high-score count"; return false; }
    for (size_t i = 0; i < n; i++) {
        HighScore& h = out.scores[i];
        if (!readStr8(r, h.name, 31)) { why = "bad high-score name"; return false; }
        for (char& c : h.name)
            if (static_cast<unsigned char>(c) < 0x20) c = ' ';
        h.score = readI64(r);
        h.rank = r.readU8();
        if (r.failed() || h.rank >= kRankCount || h.score < 0) { why = "bad high-score entry"; return false; }
    }
    const size_t nh = r.readU8();
    if (r.failed() || nh != static_cast<size_t>(out.helicopterCount) || nh > r.remaining()) { why = "bad helicopter count"; return false; }
    for (size_t i = 0; i < nh; i++) out.helicopterUnlocked[i] = r.readU8() != 0;
    const size_t nm = r.readU8();
    if (r.failed() || nm != static_cast<size_t>(out.missionCount) || nm > r.remaining()) { why = "bad mission count"; return false; }
    for (size_t i = 0; i < nm; i++) out.missionUnlocked[i] = r.readU8() != 0;
    if (r.failed()) { why = "truncated progress"; return false; }
    // Never lock what a fresh install has.
    out.helicopterUnlocked[0] = out.helicopterUnlocked[1] = true;
    out.missionUnlocked[0] = out.missionUnlocked[1] = true;
    p = out;
    return true;
}

// Settings as named integers, so fields can be added without a version bump.
template <typename F>
void forEachSetting(Settings& s, F&& f) {
    auto flag = [&](const char* k, bool& v) { int i = v; f(k, i); v = i != 0; };
    auto num = [&](const char* k, int& v) { f(k, v); };
    auto milli = [&](const char* k, float& v) {
        int i = static_cast<int>(std::lround(v * 1000.0f));
        f(k, i);
        v = static_cast<float>(i) / 1000.0f;
    };
    flag("showHints", s.showHints);
    flag("showLogo", s.showLogo);
    flag("useSystemMouse", s.useSystemMouse);
    num("camera", s.camera);
    flag("mouseControl", s.mouseControl);
    num("videoMode", s.videoMode);
    num("refreshRate", s.refreshRate);
    num("colorDepth", s.colorDepth);
    flag("fullscreen", s.fullscreen);
    flag("vsync", s.vsync);
    milli("brightness", s.brightness);
    milli("sfxVolume", s.sfxVolume);
    milli("musicVolume", s.musicVolume);
    flag("sound3D", s.sound3D);
    num("textureFilter", s.textureFilter);
    flag("showFps", s.showFps);
    // Added after the first release of the format (WP-51): older files simply lack them and
    // get the defaults; older readers skip them.
    num("screenMode", s.screenMode);
    flag("leftHanded", s.leftHanded);
    num("touchSpeed", s.touchSpeed);
    char key[16];
    for (int p = 0; p < 2; p++)
        for (int a = 0; a < kActionCount; a++)
            for (int k = 0; k < 2; k++) {
                std::snprintf(key, sizeof key, "key%d.%d.%d", p, a, k);
                num(key, s.keys[p][a][k]);
            }
}

void writeSettings(Writer& w, const Settings& s0) {
    Settings s = s0;
    std::vector<std::pair<std::string, int>> kv;
    forEachSetting(s, [&](const char* k, int& v) { kv.emplace_back(k, v); });
    w.u16v(static_cast<u32>(kv.size()));
    for (auto& [k, v] : kv) {
        w.str8(k);
        w.i32v(v);
    }
}

bool readSettings(ByteReader& r, Settings& s, std::string& why) {
    Settings out = Settings::defaults();
    const size_t n = r.readU16();
    // Each entry takes at least 1 + 4 bytes.
    if (r.failed() || n > 1024 || n * 5 > r.remaining()) { why = "bad settings count"; return false; }
    for (size_t i = 0; i < n; i++) {
        std::string key;
        if (!readStr8(r, key, 32)) { why = "bad settings key"; return false; }
        const int value = r.readI32();
        if (r.failed()) { why = "truncated settings"; return false; }
        forEachSetting(out, [&](const char* k, int& v) {
            if (key == k) v = value;
        });
    }
    out.clampToRanges();
    s = out;
    return true;
}

// The defaults for whatever could not be read, sized like the Progress the caller passed in.
void resetKeepingCounts(Profile& out) {
    const int mc = out.progress.missionCount, hc = out.progress.helicopterCount;
    out = Profile{};
    out.progress.missionCount = mc;
    out.progress.helicopterCount = hc;
}

} // namespace

std::vector<u8> serializeProfile(const Profile& p) {
    Writer payload;
    auto chunk = [&](const char tag[4], const Writer& data) {
        payload.bytes(tag, 4);
        payload.u32v(static_cast<u32>(data.b.size()));
        payload.bytes(data.b.data(), data.b.size());
    };
    Writer prog, sett;
    writeProgress(prog, p.progress);
    writeSettings(sett, p.settings);
    chunk("PROG", prog);
    chunk("SETT", sett);
    Writer out;
    out.bytes(kMagic, 8);
    out.u32v(kVersion);
    out.u32v(static_cast<u32>(payload.b.size()));
    out.u32v(crc32(payload.b.data(), payload.b.size()));
    out.bytes(payload.b.data(), payload.b.size());
    return out.b;
}

bool deserializeProfile(const u8* data, size_t size, Profile& out, std::string* why) {
    resetKeepingCounts(out);
    std::string err;
    auto fail = [&](const char* msg) {
        if (why) *why = msg;
        return false;
    };
    if (!data || size < kHeaderSize) return fail("file too short");
    if (size > kMaxFileSize) return fail("file too large");
    if (std::memcmp(data, kMagic, 8) != 0) return fail("not a profile file");
    ByteReader h(data + 8, 12);
    const u32 version = h.readU32();
    const u32 payloadSize = h.readU32();
    const u32 crc = h.readU32();
    if (version != kVersion) return fail("unsupported profile version");
    if (payloadSize != size - kHeaderSize) return fail("truncated or padded file");
    const u8* payload = data + kHeaderSize;
    if (crc32(payload, payloadSize) != crc) return fail("checksum mismatch");

    ByteReader r(payload, payloadSize);
    bool ok = true;
    bool haveProg = false, haveSett = false;
    while (r.remaining() > 0) {
        if (r.remaining() < 8) { err = "truncated chunk header"; ok = false; break; }
        char tag[4];
        r.readBytes(tag, 4);
        const u32 len = r.readU32();
        if (len > r.remaining()) { err = "chunk longer than the file"; ok = false; break; }
        ByteReader c(payload + r.pos(), len);
        r.skip(len);
        std::string cerr;
        if (std::memcmp(tag, "PROG", 4) == 0 && !haveProg) {
            haveProg = true;
            if (!readProgress(c, out.progress, cerr)) { err = cerr; ok = false; }
        } else if (std::memcmp(tag, "SETT", 4) == 0 && !haveSett) {
            haveSett = true;
            if (!readSettings(c, out.settings, cerr)) { err = cerr; ok = false; }
        }
    }
    if (!haveProg || !haveSett) {
        if (err.empty()) err = "missing chunk";
        ok = false;
    }
    if (!ok && why) *why = err;
    return ok;
}

bool loadProfileFile(const std::string& path, Profile& out, std::string* why) {
    resetKeepingCounts(out);
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (why) *why = "no profile file";
        return false;
    }
    std::vector<u8> buf;
    u8 chunk[4096];
    size_t n;
    while ((n = std::fread(chunk, 1, sizeof chunk, f)) > 0) {
        if (buf.size() + n > kMaxFileSize) {
            std::fclose(f);
            if (why) *why = "file too large";
            return false;
        }
        buf.insert(buf.end(), chunk, chunk + n);
    }
    std::fclose(f);
    return deserializeProfile(buf.data(), buf.size(), out, why);
}

bool saveProfileFile(const std::string& path, const Profile& p, std::string* why) {
    const std::vector<u8> bytes = serializeProfile(p);
    const std::string tmp = path + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) {
        if (why) *why = "cannot create " + tmp;
        return false;
    }
    const bool written = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    const bool flushed = std::fflush(f) == 0;
    const bool closed = std::fclose(f) == 0;
    if (!written || !flushed || !closed) {
        std::remove(tmp.c_str());
        if (why) *why = "cannot write " + tmp;
        return false;
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        if (why) *why = "cannot replace " + path;
        return false;
    }
    return true;
}

} // namespace as3d
