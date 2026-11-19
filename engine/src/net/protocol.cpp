// The ctl messages and the Inputs packet (as3d/net_protocol.h, docs/spec/issues/180-online-coop.md
// 3). Little-endian integers, `u8 length + bytes` strings, the type byte first. The decoders are
// strict: the type byte, every length, every enum and flag range, and no trailing bytes; a peer
// that sends anything else is a protocol error (the session ends, section 9), never a crash.
#include <cstring>

#include "as3d/net_protocol.h"

namespace as3d::net {

namespace {

// The writer appends to `out` (the encoders clear it first).
struct Writer {
    std::vector<u8>& out;
    void u8v(u8 v) { out.push_back(v); }
    void u16v(u16 v) {
        out.push_back(static_cast<u8>(v));
        out.push_back(static_cast<u8>(v >> 8));
    }
    void u32v(u32 v) {
        for (int i = 0; i < 4; i++) out.push_back(static_cast<u8>(v >> (8 * i)));
    }
    void u64v(u64 v) {
        for (int i = 0; i < 8; i++) out.push_back(static_cast<u8>(v >> (8 * i)));
    }
    void i64v(i64 v) { u64v(static_cast<u64>(v)); }
    void f64v(double v) {
        u64 bits;
        std::memcpy(&bits, &v, sizeof bits);
        u64v(bits);
    }
    void bytes(const u8* p, size_t n) { out.insert(out.end(), p, p + n); }
    // Cut at kMaxWireString (the header's rule: longer strings are cut by the encoder).
    void str(const std::string& s) {
        size_t n = s.size() < kMaxWireString ? s.size() : kMaxWireString;
        u8v(static_cast<u8>(n));
        bytes(reinterpret_cast<const u8*>(s.data()), n);
    }
};

// A bounds-checked reader: every read past the end sets `bad` and yields zero; the decoders check
// `done()` (no failure, nothing left over) at the end.
struct Reader {
    const u8* p;
    size_t n;
    size_t pos = 0;
    bool bad = false;
    bool take(size_t k) {
        if (bad || n - pos < k) {
            bad = true;
            return false;
        }
        return true;
    }
    u8 u8v() {
        if (!take(1)) return 0;
        return p[pos++];
    }
    u16 u16v() {
        if (!take(2)) return 0;
        u16 v = static_cast<u16>(p[pos] | (p[pos + 1] << 8));
        pos += 2;
        return v;
    }
    u32 u32v() {
        if (!take(4)) return 0;
        u32 v = 0;
        for (int i = 0; i < 4; i++) v |= static_cast<u32>(p[pos + i]) << (8 * i);
        pos += 4;
        return v;
    }
    u64 u64v() {
        if (!take(8)) return 0;
        u64 v = 0;
        for (int i = 0; i < 8; i++) v |= static_cast<u64>(p[pos + i]) << (8 * i);
        pos += 8;
        return v;
    }
    i64 i64v() { return static_cast<i64>(u64v()); }
    double f64v() {
        u64 bits = u64v();
        double v;
        std::memcpy(&v, &bits, sizeof v);
        return v;
    }
    void bytes(u8* out, size_t k) {
        if (!take(k)) return;
        std::memcpy(out, p + pos, k);
        pos += k;
    }
    void str(std::string& s) {
        size_t k = u8v();
        if (k > kMaxWireString) bad = true;
        if (!take(k)) return;
        s.assign(reinterpret_cast<const char*>(p + pos), k);
        pos += k;
    }
    // A 0/1 byte.
    u8 flag() {
        u8 v = u8v();
        if (v > 1) bad = true;
        return v;
    }
    // A byte at most `max`.
    u8 upTo(u8 max) {
        u8 v = u8v();
        if (v > max) bad = true;
        return v;
    }
    bool done() const { return !bad && pos == n; }
};

// The reader over a message whose type byte must be `t`; null data or a wrong type fails it.
Reader open(const u8* data, size_t n, MsgType t) {
    Reader r{data, data ? n : 0};
    if (!data || n == 0 || data[0] != static_cast<u8>(t)) r.bad = true;
    else r.pos = 1;
    return r;
}

Writer begin(std::vector<u8>& out, MsgType t) {
    out.clear();
    Writer w{out};
    w.u8v(static_cast<u8>(t));
    return w;
}

void encodePingLike(const PingMsg& m, std::vector<u8>& out, MsgType t) {
    Writer w = begin(out, t);
    w.u32v(m.seq);
    w.u32v(m.sentMs);
}

bool decodePingLike(const u8* data, size_t n, PingMsg& out, MsgType t) {
    Reader r = open(data, n, t);
    PingMsg m;
    m.seq = r.u32v();
    m.sentMs = r.u32v();
    if (!r.done()) return false;
    out = m;
    return true;
}

// InputRecord flag bits a version-1 peer may send.
constexpr u8 kKnownInputFlags = InputRecord::kConfirm | InputRecord::kPause | InputRecord::kSteer;

} // namespace

u8 messageType(const u8* data, size_t n) { return data && n > 0 ? data[0] : 0; }

void encodeHello(const HelloMsg& m, std::vector<u8>& out) {
    Writer w = begin(out, MsgType::Hello);
    w.u16v(m.protocolVersion);
    w.str(m.buildId);
    w.u8v(m.platform);
    w.str(m.gameKey);
    w.bytes(m.simFingerprint, sizeof m.simFingerprint);
    w.u8v(m.role);
}

// The version is read and returned whatever it is (the caller compares it first and says
// "different version"); only the shape of the message is checked. The platform byte is not
// range-checked either (peerPlatformName says "unknown"), the role is.
bool decodeHello(const u8* data, size_t n, HelloMsg& out) {
    Reader r = open(data, n, MsgType::Hello);
    HelloMsg m;
    m.protocolVersion = r.u16v();
    r.str(m.buildId);
    m.platform = r.u8v();
    r.str(m.gameKey);
    r.bytes(m.simFingerprint, sizeof m.simFingerprint);
    m.role = r.upTo(static_cast<u8>(PeerRole::Guest));
    if (!r.done()) return false;
    out = std::move(m);
    return true;
}

void encodeBye(const ByeMsg& m, std::vector<u8>& out) {
    Writer w = begin(out, MsgType::Bye);
    w.u8v(m.reason);
    w.str(m.text);
}

bool decodeBye(const u8* data, size_t n, ByeMsg& out) {
    Reader r = open(data, n, MsgType::Bye);
    ByeMsg m;
    m.reason = r.upTo(static_cast<u8>(ByeReason::Timeout));
    r.str(m.text);
    if (!r.done()) return false;
    out = std::move(m);
    return true;
}

void encodePing(const PingMsg& m, std::vector<u8>& out) { encodePingLike(m, out, MsgType::Ping); }
bool decodePing(const u8* data, size_t n, PingMsg& out) { return decodePingLike(data, n, out, MsgType::Ping); }
void encodePong(const PingMsg& m, std::vector<u8>& out) { encodePingLike(m, out, MsgType::Pong); }
bool decodePong(const u8* data, size_t n, PingMsg& out) { return decodePingLike(data, n, out, MsgType::Pong); }

void encodeLobby(const LobbyMsg& m, std::vector<u8>& out) {
    Writer w = begin(out, MsgType::Lobby);
    w.u8v(m.mission);
    w.u8v(m.difficulty);
    w.u8v(m.hostHeli);
    w.u8v(m.guestHeli);
    w.u8v(m.godMode);
}

// Mission, difficulty and helicopters are game data indices: the game side checks them against
// its tables; the codec checks the booleans.
bool decodeLobby(const u8* data, size_t n, LobbyMsg& out) {
    Reader r = open(data, n, MsgType::Lobby);
    LobbyMsg m;
    m.mission = r.u8v();
    m.difficulty = r.u8v();
    m.hostHeli = r.u8v();
    m.guestHeli = r.u8v();
    m.godMode = r.flag();
    if (!r.done()) return false;
    out = m;
    return true;
}

void encodeLobbyGuest(const LobbyGuestMsg& m, std::vector<u8>& out) {
    Writer w = begin(out, MsgType::LobbyGuest);
    w.u8v(m.heli);
    w.u8v(m.ready);
}

bool decodeLobbyGuest(const u8* data, size_t n, LobbyGuestMsg& out) {
    Reader r = open(data, n, MsgType::LobbyGuest);
    LobbyGuestMsg m;
    m.heli = r.u8v();
    m.ready = r.flag();
    if (!r.done()) return false;
    out = m;
    return true;
}

void encodeStartMission(const StartMissionMsg& m, std::vector<u8>& out) {
    Writer w = begin(out, MsgType::StartMission);
    w.u32v(m.token);
    w.u32v(m.seed);
    w.u8v(m.inputDelay);
    w.u8v(m.camera);
    w.u8v(m.godMode);
    const WireMissionStart& s = m.start;
    w.u8v(s.mission);
    w.u8v(s.difficulty);
    w.u8v(s.players);
    for (u8 h : s.heli) w.u8v(h);
    for (u8 l : s.lives) w.u8v(l);
    for (i64 b : s.banked) w.i64v(b);
    w.u8v(s.restart);
    w.u8v(s.carryUpgrades);
    for (const auto& row : s.upgrades) w.bytes(row, kWireWeaponSlots);
    for (u8 x : s.weapon) w.u8v(x);
    for (double a : s.rankAccumulator) w.f64v(a);
}

// The delay must be one the lockstep accepts (2..10, as3d/net_lockstep.h; written here as
// literals to keep the codec free of the lockstep header), players 1 or 2.
bool decodeStartMission(const u8* data, size_t n, StartMissionMsg& out) {
    Reader r = open(data, n, MsgType::StartMission);
    StartMissionMsg m;
    m.token = r.u32v();
    m.seed = r.u32v();
    m.inputDelay = r.u8v();
    if (m.inputDelay < 2 || m.inputDelay > 10) r.bad = true;
    m.camera = r.u8v();
    m.godMode = r.flag();
    WireMissionStart& s = m.start;
    s.mission = r.u8v();
    s.difficulty = r.u8v();
    s.players = r.u8v();
    if (s.players < 1 || s.players > 2) r.bad = true;
    for (u8& h : s.heli) h = r.u8v();
    for (u8& l : s.lives) l = r.u8v();
    for (i64& b : s.banked) b = r.i64v();
    s.restart = r.flag();
    s.carryUpgrades = r.flag();
    for (auto& row : s.upgrades) r.bytes(row, kWireWeaponSlots);
    for (u8& x : s.weapon) x = r.u8v();
    for (double& a : s.rankAccumulator) a = r.f64v();
    if (!r.done()) return false;
    out = m;
    return true;
}

void encodeReady(const ReadyMsg& m, std::vector<u8>& out) {
    Writer w = begin(out, MsgType::Ready);
    w.u32v(m.token);
}

bool decodeReady(const u8* data, size_t n, ReadyMsg& out) {
    Reader r = open(data, n, MsgType::Ready);
    ReadyMsg m;
    m.token = r.u32v();
    if (!r.done()) return false;
    out = m;
    return true;
}

void encodeFlowChoice(const FlowChoiceMsg& m, std::vector<u8>& out) {
    Writer w = begin(out, MsgType::FlowChoice);
    w.u8v(m.choice);
}

bool decodeFlowChoice(const u8* data, size_t n, FlowChoiceMsg& out) {
    Reader r = open(data, n, MsgType::FlowChoice);
    FlowChoiceMsg m;
    m.choice = r.upTo(static_cast<u8>(FlowCode::StartCampaign));
    if (!r.done()) return false;
    out = m;
    return true;
}

void encodeHash(const HashMsg& m, std::vector<u8>& out) {
    Writer w = begin(out, MsgType::Hash);
    w.u32v(m.token);
    w.u32v(m.frame);
    w.u64v(m.hash);
}

bool decodeHash(const u8* data, size_t n, HashMsg& out) {
    Reader r = open(data, n, MsgType::Hash);
    HashMsg m;
    m.token = r.u32v();
    m.frame = r.u32v();
    m.hash = r.u64v();
    if (!r.done()) return false;
    out = m;
    return true;
}

void encodeDesync(const DesyncMsg& m, std::vector<u8>& out) {
    Writer w = begin(out, MsgType::Desync);
    w.u32v(m.token);
    w.u32v(m.frame);
    w.u64v(m.mine);
    w.u64v(m.yours);
}

bool decodeDesync(const u8* data, size_t n, DesyncMsg& out) {
    Reader r = open(data, n, MsgType::Desync);
    DesyncMsg m;
    m.token = r.u32v();
    m.frame = r.u32v();
    m.mine = r.u64v();
    m.yours = r.u64v();
    if (!r.done()) return false;
    out = m;
    return true;
}

// 14 bytes of header + 4 per record: at most 62 bytes (kMaxInputsMessage = 128). A count outside
// 1..kRedundancy is not encoded (the output stays empty: nothing to send).
void encodeInputs(const InputsPacket& m, std::vector<u8>& out) {
    if (m.count < 1 || m.count > kRedundancy) {
        out.clear();
        return;
    }
    Writer w = begin(out, MsgType::Inputs);
    w.u32v(m.token);
    w.u32v(m.ack);
    w.u32v(m.firstFrame);
    w.u8v(m.count);
    for (int i = 0; i < m.count; i++) {
        const InputRecord& rec = m.records[i];
        w.u16v(rec.held);
        w.u8v(rec.flags);
        w.u8v(rec.steer);
    }
}

// Refused: a count outside 1..kRedundancy, frames that wrap past 0xFFFFFFFF (or reach kNoFrame),
// unknown flag bits, a steer byte without kSteer (the encoder of a version-1 peer never writes
// one, and the merge must not see two encodings of one input).
bool decodeInputs(const u8* data, size_t n, InputsPacket& out) {
    Reader r = open(data, n, MsgType::Inputs);
    InputsPacket m;
    m.token = r.u32v();
    m.ack = r.u32v();
    m.firstFrame = r.u32v();
    m.count = r.u8v();
    if (m.count < 1 || m.count > kRedundancy) r.bad = true;
    if (!r.bad && m.firstFrame > kNoFrame - m.count) r.bad = true;
    for (int i = 0; i < m.count && !r.bad; i++) {
        InputRecord& rec = m.records[i];
        rec.held = r.u16v();
        rec.flags = r.u8v();
        rec.steer = r.u8v();
        if ((rec.flags & ~kKnownInputFlags) != 0) r.bad = true;
        if (!(rec.flags & InputRecord::kSteer) && rec.steer != 0) r.bad = true;
    }
    if (!r.done()) return false;
    out = m;
    return true;
}

} // namespace as3d::net
