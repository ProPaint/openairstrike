// The online protocol's messages (as3d/net_protocol.h, docs/spec/issues/180-online-coop.md 3 and
// 13 T1): every message round-trips with edge values, every decoder refuses truncations, a
// wrong type byte, trailing bytes and out-of-range fields, and survives 10,000 random and mutated
// buffers.
#include <cstring>
#include <functional>
#include <limits>
#include <memory>

#include "doctest.h"

#include "as3d/core.h"
#include "as3d/net_protocol.h"

using namespace as3d;
using namespace as3d::net;

namespace {

bool sameStart(const WireMissionStart& a, const WireMissionStart& b) {
    return a.mission == b.mission && a.difficulty == b.difficulty && a.players == b.players &&
           std::memcmp(a.heli, b.heli, 2) == 0 && std::memcmp(a.lives, b.lives, 2) == 0 && a.banked[0] == b.banked[0] &&
           a.banked[1] == b.banked[1] && a.restart == b.restart && a.carryUpgrades == b.carryUpgrades &&
           std::memcmp(a.upgrades, b.upgrades, sizeof a.upgrades) == 0 && std::memcmp(a.weapon, b.weapon, 2) == 0 &&
           std::memcmp(a.rankAccumulator, b.rankAccumulator, sizeof a.rankAccumulator) == 0;
}

// One decoder over a buffer, for the fuzz loops: true when it decoded, and then `reencoded`
// holds the message encoded again.
using Decoder = std::function<bool(const u8*, size_t, std::vector<u8>& reencoded)>;

template <class M, bool (*Dec)(const u8*, size_t, M&), void (*Enc)(const M&, std::vector<u8>&)>
Decoder decoder() {
    return [](const u8* d, size_t n, std::vector<u8>& re) {
        M m;
        if (!Dec(d, n, m)) return false;
        Enc(m, re);
        return true;
    };
}

std::vector<std::pair<const char*, Decoder>> allDecoders() {
    return {
        {"Hello", decoder<HelloMsg, decodeHello, encodeHello>()},
        {"Bye", decoder<ByeMsg, decodeBye, encodeBye>()},
        {"Ping", decoder<PingMsg, decodePing, encodePing>()},
        {"Pong", decoder<PingMsg, decodePong, encodePong>()},
        {"Lobby", decoder<LobbyMsg, decodeLobby, encodeLobby>()},
        {"LobbyGuest", decoder<LobbyGuestMsg, decodeLobbyGuest, encodeLobbyGuest>()},
        {"StartMission", decoder<StartMissionMsg, decodeStartMission, encodeStartMission>()},
        {"Ready", decoder<ReadyMsg, decodeReady, encodeReady>()},
        {"FlowChoice", decoder<FlowChoiceMsg, decodeFlowChoice, encodeFlowChoice>()},
        {"Hash", decoder<HashMsg, decodeHash, encodeHash>()},
        {"Desync", decoder<DesyncMsg, decodeDesync, encodeDesync>()},
        {"Inputs", decoder<InputsPacket, decodeInputs, encodeInputs>()},
    };
}

// A valid encoding of every message (the fuzz seeds), with edge values.
std::vector<std::vector<u8>> validMessages() {
    std::vector<std::vector<u8>> out;
    std::vector<u8> b;
    HelloMsg h;
    h.buildId = std::string(kMaxWireString, 'x');
    h.gameKey = "as3d";
    for (int i = 0; i < 32; i++) h.simFingerprint[i] = static_cast<u8>(i * 7);
    h.role = 1;
    encodeHello(h, b);
    out.push_back(b);
    encodeBye(ByeMsg{4, "bye"}, b);
    out.push_back(b);
    encodePing(PingMsg{0xFFFFFFFFu, 0}, b);
    out.push_back(b);
    encodePong(PingMsg{1, 0xFFFFFFFFu}, b);
    out.push_back(b);
    encodeLobby(LobbyMsg{255, 3, 1, 2, 1}, b);
    out.push_back(b);
    encodeLobbyGuest(LobbyGuestMsg{2, 1}, b);
    out.push_back(b);
    StartMissionMsg sm;
    sm.inputDelay = 10;
    sm.start.banked[0] = std::numeric_limits<i64>::min();
    encodeStartMission(sm, b);
    out.push_back(b);
    encodeReady(ReadyMsg{7}, b);
    out.push_back(b);
    encodeFlowChoice(FlowChoiceMsg{4}, b);
    out.push_back(b);
    encodeHash(HashMsg{1, 20, 0x0123456789ABCDEFull}, b);
    out.push_back(b);
    encodeDesync(DesyncMsg{1, 20, 1, 2}, b);
    out.push_back(b);
    InputsPacket p;
    p.count = kRedundancy;
    for (int i = 0; i < kRedundancy; i++) p.records[i] = InputRecord{static_cast<u16>(i), InputRecord::kSteer, static_cast<u8>(i)};
    encodeInputs(p, b);
    out.push_back(b);
    return out;
}

} // namespace

TEST_CASE("net protocol: Hello round-trips, strings are cut at 64 bytes by the encoder") {
    HelloMsg m;
    m.protocolVersion = 0xFFFF;
    m.buildId = "v0.4.0-12-gabcdef0-dirty";
    m.platform = static_cast<u8>(PeerPlatform::Android);
    m.gameKey = std::string(100, 'k');
    for (int i = 0; i < 32; i++) m.simFingerprint[i] = static_cast<u8>(255 - i);
    m.role = static_cast<u8>(PeerRole::Guest);
    std::vector<u8> b;
    encodeHello(m, b);
    CHECK(messageType(b.data(), b.size()) == static_cast<u8>(MsgType::Hello));
    HelloMsg d;
    REQUIRE(decodeHello(b.data(), b.size(), d));
    CHECK(d.protocolVersion == 0xFFFF);
    CHECK(d.buildId == m.buildId);
    CHECK(d.platform == m.platform);
    CHECK(d.gameKey == std::string(kMaxWireString, 'k'));
    CHECK(std::memcmp(d.simFingerprint, m.simFingerprint, 32) == 0);
    CHECK(d.role == 1);

    // Empty strings, and a role out of range is refused.
    m = HelloMsg();
    encodeHello(m, b);
    REQUIRE(decodeHello(b.data(), b.size(), d));
    CHECK(d.buildId.empty());
    CHECK(d.protocolVersion == kNetProtocolVersion);
    b.back() = 2;
    CHECK(!decodeHello(b.data(), b.size(), d));
    // A string length above 64 is refused even when the bytes are there.
    std::vector<u8> bad = {static_cast<u8>(MsgType::Hello), 1, 0, 65};
    bad.resize(bad.size() + 65 + 1 + 1 + 32 + 1, 'a');
    bad[4 + 65] = 0;     // platform
    bad[4 + 65 + 1] = 0; // empty game key
    bad.back() = 0;      // role
    CHECK(!decodeHello(bad.data(), bad.size(), d));
}

TEST_CASE("net protocol: Bye, Ping, Pong, Lobby, LobbyGuest, Ready, FlowChoice, Hash, Desync round-trip") {
    std::vector<u8> b;
    {
        ByeMsg d;
        encodeBye(ByeMsg{static_cast<u8>(ByeReason::Desync), "desync at 900"}, b);
        REQUIRE(decodeBye(b.data(), b.size(), d));
        CHECK(d.reason == 3);
        CHECK(d.text == "desync at 900");
        encodeBye(ByeMsg{5, ""}, b);
        CHECK(!decodeBye(b.data(), b.size(), d));
    }
    {
        PingMsg d;
        encodePing(PingMsg{0xFFFFFFFFu, 0x80000000u}, b);
        REQUIRE(decodePing(b.data(), b.size(), d));
        CHECK(d.seq == 0xFFFFFFFFu);
        CHECK(d.sentMs == 0x80000000u);
        CHECK(!decodePong(b.data(), b.size(), d)); // the type byte tells Ping from Pong
        encodePong(PingMsg{3, 4}, b);
        REQUIRE(decodePong(b.data(), b.size(), d));
        CHECK(d.seq == 3);
        CHECK(d.sentMs == 4);
        CHECK(messageType(b.data(), b.size()) == static_cast<u8>(MsgType::Pong));
    }
    {
        LobbyMsg d;
        encodeLobby(LobbyMsg{255, 254, 253, 252, 1}, b);
        REQUIRE(decodeLobby(b.data(), b.size(), d));
        CHECK(d.mission == 255);
        CHECK(d.difficulty == 254);
        CHECK(d.hostHeli == 253);
        CHECK(d.guestHeli == 252);
        CHECK(d.godMode == 1);
        b.back() = 2;
        CHECK(!decodeLobby(b.data(), b.size(), d));
    }
    {
        LobbyGuestMsg d;
        encodeLobbyGuest(LobbyGuestMsg{9, 1}, b);
        REQUIRE(decodeLobbyGuest(b.data(), b.size(), d));
        CHECK(d.heli == 9);
        CHECK(d.ready == 1);
    }
    {
        ReadyMsg d;
        encodeReady(ReadyMsg{0xDEADBEEFu}, b);
        REQUIRE(decodeReady(b.data(), b.size(), d));
        CHECK(d.token == 0xDEADBEEFu);
    }
    {
        FlowChoiceMsg d;
        for (u8 c = 0; c <= 4; c++) {
            encodeFlowChoice(FlowChoiceMsg{c}, b);
            REQUIRE(decodeFlowChoice(b.data(), b.size(), d));
            CHECK(d.choice == c);
        }
        encodeFlowChoice(FlowChoiceMsg{5}, b);
        CHECK(!decodeFlowChoice(b.data(), b.size(), d));
    }
    {
        HashMsg d;
        encodeHash(HashMsg{1, 0xFFFFFFFFu, 0xFFFFFFFFFFFFFFFFull}, b);
        REQUIRE(decodeHash(b.data(), b.size(), d));
        CHECK(d.token == 1);
        CHECK(d.frame == 0xFFFFFFFFu);
        CHECK(d.hash == 0xFFFFFFFFFFFFFFFFull);
    }
    {
        DesyncMsg d;
        encodeDesync(DesyncMsg{2, 900, 0x8000000000000001ull, 1}, b);
        REQUIRE(decodeDesync(b.data(), b.size(), d));
        CHECK(d.token == 2);
        CHECK(d.frame == 900);
        CHECK(d.mine == 0x8000000000000001ull);
        CHECK(d.yours == 1);
    }
}

TEST_CASE("net protocol: StartMission round-trips every MissionStart field") {
    StartMissionMsg m;
    m.token = 0xFFFFFFFEu;
    m.seed = 0x12345678u;
    m.inputDelay = 10;
    m.camera = 255;
    m.godMode = 1;
    WireMissionStart& s = m.start;
    s.mission = 13;
    s.difficulty = 2;
    s.players = 2;
    s.heli[0] = 1;
    s.heli[1] = 3;
    s.lives[0] = 0;
    s.lives[1] = 255;
    s.banked[0] = std::numeric_limits<i64>::max();
    s.banked[1] = -1;
    s.restart = 1;
    s.carryUpgrades = 1;
    for (int p = 0; p < 2; p++)
        for (int i = 0; i < kWireWeaponSlots; i++) s.upgrades[p][i] = static_cast<u8>(p * 100 + i);
    s.weapon[0] = 4;
    s.weapon[1] = 19;
    s.rankAccumulator[0] = -0.0;
    s.rankAccumulator[1] = 1e300;
    std::vector<u8> b;
    encodeStartMission(m, b);
    CHECK(b.size() == 1 + 4 + 4 + 3 + 3 + 2 + 2 + 16 + 2 + 2 * kWireWeaponSlots + 2 + 16);
    StartMissionMsg d;
    REQUIRE(decodeStartMission(b.data(), b.size(), d));
    CHECK(d.token == m.token);
    CHECK(d.seed == m.seed);
    CHECK(d.inputDelay == 10);
    CHECK(d.camera == 255);
    CHECK(d.godMode == 1);
    CHECK(sameStart(d.start, s));

    // Delay outside 2..10 and a player count outside 1..2 are refused.
    m.inputDelay = 1;
    encodeStartMission(m, b);
    CHECK(!decodeStartMission(b.data(), b.size(), d));
    m.inputDelay = 11;
    encodeStartMission(m, b);
    CHECK(!decodeStartMission(b.data(), b.size(), d));
    m.inputDelay = 2;
    m.start.players = 0;
    encodeStartMission(m, b);
    CHECK(!decodeStartMission(b.data(), b.size(), d));
    m.start.players = 1;
    encodeStartMission(m, b);
    CHECK(decodeStartMission(b.data(), b.size(), d));
}

TEST_CASE("net protocol: Inputs round-trip, 62 bytes at full redundancy, ranges refused") {
    InputsPacket p;
    p.token = 5;
    p.ack = kNoFrame;
    p.firstFrame = 1000;
    p.count = kRedundancy;
    for (int i = 0; i < kRedundancy; i++) {
        p.records[i].held = static_cast<u16>(0x3FF - i);
        p.records[i].flags = static_cast<u8>(i % 8);
        p.records[i].steer = (p.records[i].flags & InputRecord::kSteer) ? static_cast<u8>(200 + i) : 0;
    }
    std::vector<u8> b;
    encodeInputs(p, b);
    CHECK(b.size() == 62);
    CHECK(b.size() <= 128);
    InputsPacket d;
    REQUIRE(decodeInputs(b.data(), b.size(), d));
    CHECK(d.token == 5);
    CHECK(d.ack == kNoFrame);
    CHECK(d.firstFrame == 1000);
    CHECK(d.count == kRedundancy);
    for (int i = 0; i < kRedundancy; i++) CHECK(d.records[i] == p.records[i]);

    // One record at the top of the frame range.
    p.count = 1;
    p.firstFrame = kNoFrame - 1;
    encodeInputs(p, b);
    CHECK(b.size() == 18);
    REQUIRE(decodeInputs(b.data(), b.size(), d));
    CHECK(d.firstFrame == kNoFrame - 1);
    // Frames reaching kNoFrame (or wrapping) are refused.
    p.count = 2;
    encodeInputs(p, b);
    CHECK(!decodeInputs(b.data(), b.size(), d));
    // Count 0 or above kRedundancy is not encoded, and refused when hand-made.
    p.count = 0;
    encodeInputs(p, b);
    CHECK(b.empty());
    p.count = kRedundancy + 1;
    encodeInputs(p, b);
    CHECK(b.empty());
    p.firstFrame = 0;
    p.count = 1;
    p.records[0] = InputRecord();
    encodeInputs(p, b);
    REQUIRE(b.size() == 18);
    b[13] = 0;
    CHECK(!decodeInputs(b.data(), b.size(), d));
    // Unknown flag bits, and a steer byte without kSteer.
    encodeInputs(p, b);
    b[16] = 8;
    CHECK(!decodeInputs(b.data(), b.size(), d));
    b[16] = 0;
    b[17] = 1;
    CHECK(!decodeInputs(b.data(), b.size(), d));
    b[16] = InputRecord::kSteer;
    CHECK(decodeInputs(b.data(), b.size(), d));
    CHECK(d.records[0].steer == 1);
}

TEST_CASE("net protocol: every decoder refuses truncations, a wrong type and trailing bytes") {
    auto decoders = allDecoders();
    auto valid = validMessages();
    REQUIRE(decoders.size() == valid.size());
    std::vector<u8> re;
    for (size_t k = 0; k < valid.size(); k++) {
        CAPTURE(decoders[k].first);
        const std::vector<u8>& m = valid[k];
        Decoder& dec = decoders[k].second;
        REQUIRE(dec(m.data(), m.size(), re));
        CHECK(re == m);
        for (size_t n = 0; n < m.size(); n++) CHECK(!dec(m.data(), n, re));
        std::vector<u8> longer = m;
        longer.push_back(0);
        CHECK(!dec(longer.data(), longer.size(), re));
        std::vector<u8> typed = m;
        typed[0] = 0x7F;
        CHECK(!dec(typed.data(), typed.size(), re));
        CHECK(!dec(nullptr, m.size(), re));
        // Only its own type: the other decoders refuse it.
        for (size_t j = 0; j < decoders.size(); j++)
            if (j != k) CHECK(!decoders[j].second(m.data(), m.size(), re));
    }
    CHECK(messageType(nullptr, 0) == 0);
}

// The decoders are strict and the encoding canonical, so whatever a decoder accepts encodes back
// to the very same bytes: nothing odd gets through by being read loosely.
TEST_CASE("net protocol: 10,000 random and mutated buffers per decoder: no crash, accepted ones are canonical") {
    auto decoders = allDecoders();
    auto valid = validMessages();
    Rng rng(180);
    std::vector<u8> re;
    for (size_t k = 0; k < decoders.size(); k++) {
        CAPTURE(decoders[k].first);
        int accepted = 0, refused = 0;
        for (int i = 0; i < 10000; i++) {
            std::vector<u8> buf;
            switch (i % 3) {
                case 0: { // random bytes after the right type byte
                    size_t n = rng.next() % 200;
                    buf.resize(n);
                    for (u8& x : buf) x = static_cast<u8>(rng.next());
                    if (n > 0) buf[0] = valid[k][0];
                    break;
                }
                case 1: { // a valid message truncated or extended
                    buf = valid[k];
                    size_t n = rng.next() % (buf.size() + 8);
                    buf.resize(n, static_cast<u8>(rng.next()));
                    break;
                }
                default: { // a valid message with a few bytes flipped (the type byte kept)
                    buf = valid[k];
                    int flips = 1 + static_cast<int>(rng.next() % 4);
                    for (int f = 0; f < flips && buf.size() > 1; f++)
                        buf[1 + rng.next() % (buf.size() - 1)] ^= static_cast<u8>(1 + rng.next() % 255);
                    break;
                }
            }
            // A heap block of exactly that size, so that a read past the end shows under ASan.
            std::unique_ptr<u8[]> exact(new u8[buf.size() ? buf.size() : 1]);
            if (!buf.empty()) std::memcpy(exact.get(), buf.data(), buf.size());
            if (decoders[k].second(buf.empty() ? nullptr : exact.get(), buf.size(), re)) {
                accepted++;
                CHECK(re == buf);
            } else {
                refused++;
            }
        }
        CHECK(accepted + refused == 10000);
        CHECK(refused > 3000); // at least the truncations
    }
}

TEST_CASE("net protocol: a decoded message re-encodes to the same bytes") {
    auto valid = validMessages();
    std::vector<u8> b;
    HelloMsg h;
    REQUIRE(decodeHello(valid[0].data(), valid[0].size(), h));
    encodeHello(h, b);
    CHECK(b == valid[0]);
    StartMissionMsg s;
    REQUIRE(decodeStartMission(valid[6].data(), valid[6].size(), s));
    encodeStartMission(s, b);
    CHECK(b == valid[6]);
    InputsPacket p;
    REQUIRE(decodeInputs(valid[11].data(), valid[11].size(), p));
    encodeInputs(p, b);
    CHECK(b == valid[11]);
}
