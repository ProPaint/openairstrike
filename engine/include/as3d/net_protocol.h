// The online protocol (docs/spec/issues/180-online-coop.md 3): the messages of the reliable
// "ctl" channel and the Inputs packet of the unreliable "in" channel, with one encode / decode
// pair per message.
//
// Wire rules: integers little-endian, strings `u8 length + bytes` (at most kMaxWireString bytes;
// longer ones are cut by the encoder, refused by the decoder), the first byte is the message type
// (MsgType). Encoders replace the contents of `out`. Decoders are bounds-checked, check the type
// byte and every range, and return false on anything odd (a bad message ends the session with
// "protocol error"); trailing bytes after a complete message are an error too. A receiver ignores
// types it does not know (forward compatibility within one protocol version).
//
// Versioning: kNetProtocolVersion is checked first (Hello), then the build id (netBuildId(),
// AS3D_BUILD_ID from `git describe --tags --always --dirty` at configure time), then the game key,
// then the data fingerprint (as3d/sim_fingerprint.h).
#pragma once

#include <string>
#include <vector>

#include "as3d/core.h"

namespace as3d::net {

constexpr u16 kNetProtocolVersion = 1;
constexpr size_t kMaxWireString = 64;
// Every Inputs packet repeats the sender's last kRedundancy records (200 ms of cover, 2.2).
constexpr int kRedundancy = 12;
// A state hash goes out after every simulated frame F with F % kHashEvery == 0 (4.4).
constexpr int kHashEvery = 20;
// The weapon slots a StartMission carries per player (as3d/game_profile.h kMaxWeaponSlots; the
// game side static_asserts that they agree).
constexpr int kWireWeaponSlots = 20;
// Session timing (4.1, 6.4, 8.5): a Ping every second; no Inputs and no Pong for
// kSilenceTimeoutSeconds ends the session ("lost"); the HUD says "Waiting for the other
// player..." after kStallNoteSeconds of stall.
constexpr double kPingIntervalSeconds = 1.0;
constexpr double kSilenceTimeoutSeconds = 10.0;
constexpr double kStallNoteSeconds = 0.5;

enum class MsgType : u8 {
    Hello = 0x01,
    Bye = 0x02,
    Ping = 0x03,
    Pong = 0x04,
    Lobby = 0x10,
    LobbyGuest = 0x11,
    StartMission = 0x12,
    Ready = 0x13,
    FlowChoice = 0x14,
    Hash = 0x20,
    Desync = 0x21,
    Inputs = 0x30,
};
// The type byte of a message, 0 for an empty one.
u8 messageType(const u8* data, size_t n);

enum class PeerPlatform : u8 { Desktop = 0, Web = 1, Android = 2 };
const char* peerPlatformName(u8 platform); // "desktop", "web", "android", "unknown"
// This build's platform (__EMSCRIPTEN__: web, __ANDROID__: android, else desktop).
PeerPlatform localPlatform();
// AS3D_BUILD_ID of this build ("unknown" without git at configure time).
const char* netBuildId();

enum class PeerRole : u8 { Host = 0, Guest = 1 };
enum class ByeReason : u8 { Quit = 0, LobbyLeft = 1, ProtocolError = 2, Desync = 3, Timeout = 4 };
enum class FlowCode : u8 { Next = 0, Restart = 1, Continue = 2, Quit = 3, StartCampaign = 4 };

// 0x01 Hello: both sides, the first message on ctl.
struct HelloMsg {
    u16 protocolVersion = kNetProtocolVersion;
    std::string buildId;
    u8 platform = 0;               // PeerPlatform
    std::string gameKey;
    u8 simFingerprint[32] = {};    // simDataFingerprintBytes
    u8 role = 0;                   // PeerRole
};
// 0x02 Bye.
struct ByeMsg {
    u8 reason = 0;                 // ByeReason
    std::string text;
};
// 0x03 Ping every kPingIntervalSeconds; 0x04 Pong echoes both fields.
struct PingMsg {
    u32 seq = 0;
    u32 sentMs = 0;                // the sender's clock
};
// 0x10 Lobby, host -> guest on every change.
struct LobbyMsg {
    u8 mission = 0;                // 0-based
    u8 difficulty = 0;
    u8 hostHeli = 0, guestHeli = 0; // the guest's as the host sees it
    u8 godMode = 0;
};
// 0x11 LobbyGuest, guest -> host on every change.
struct LobbyGuestMsg {
    u8 heli = 0;
    u8 ready = 0;
};
// The as3d::ui::MissionStart fields of a StartMission.
struct WireMissionStart {
    u8 mission = 0, difficulty = 0, players = 2;
    u8 heli[2] = {0, 0};
    u8 lives[2] = {0, 0};
    i64 banked[2] = {0, 0};
    u8 restart = 0, carryUpgrades = 0;
    u8 upgrades[2][kWireWeaponSlots] = {};
    u8 weapon[2] = {0, 0};
    double rankAccumulator[2] = {0.0, 0.0}; // as f64 bits
};
// 0x12 StartMission, host -> guest: a level start with a new token (the barrier, 4.3).
struct StartMissionMsg {
    u32 token = 0;
    u32 seed = 0;
    u8 inputDelay = 2;
    u8 camera = 1;
    u8 godMode = 0;
    WireMissionStart start;
};
// 0x13 Ready: my level `token` is loaded, I am at its frame 0.
struct ReadyMsg {
    u32 token = 0;
};
// 0x14 FlowChoice, host -> guest.
struct FlowChoiceMsg {
    u8 choice = 0;                 // FlowCode
};
// 0x20 Hash: the sender's World::stateHash after simulating `frame`.
struct HashMsg {
    u32 token = 0, frame = 0;
    u64 hash = 0;
};
// 0x21 Desync: the sender saw a mismatch.
struct DesyncMsg {
    u32 token = 0, frame = 0;
    u64 mine = 0, yours = 0;
};

// One peer's input for one simulation frame (4 bytes on the wire).
struct InputRecord {
    static constexpr u8 kConfirm = 1; // the hint box's OK (held for one frame)
    static constexpr u8 kPause = 2;   // held while this peer holds the pause
    static constexpr u8 kSteer = 4;   // `steer` is valid (the sequels' relative mouse steering)
    u16 held = 0;                     // p_action bits of this peer's player (10 bits used)
    u8 flags = 0;
    u8 steer = 0;                     // with kSteer: angle * 256 / 2 pi (as3d/net_lockstep.h kSteerTable)
    bool operator==(const InputRecord& o) const { return held == o.held && flags == o.flags && steer == o.steer; }
    bool operator!=(const InputRecord& o) const { return !(*this == o); }
};
// 0x30 Inputs on the "in" channel: records for frames firstFrame .. firstFrame + count - 1;
// `ack` = the highest frame for which the sender holds the other side's record with no gap below
// it (0xFFFFFFFF: none yet). Packets of another token are dropped.
constexpr u32 kNoFrame = 0xFFFFFFFFu;
struct InputsPacket {
    u32 token = 0;
    u32 ack = kNoFrame;
    u32 firstFrame = 0;
    u8 count = 0;                      // 1..kRedundancy
    InputRecord records[kRedundancy];
};

void encodeHello(const HelloMsg& m, std::vector<u8>& out);
bool decodeHello(const u8* data, size_t n, HelloMsg& out);
void encodeBye(const ByeMsg& m, std::vector<u8>& out);
bool decodeBye(const u8* data, size_t n, ByeMsg& out);
void encodePing(const PingMsg& m, std::vector<u8>& out);
bool decodePing(const u8* data, size_t n, PingMsg& out);
void encodePong(const PingMsg& m, std::vector<u8>& out);
bool decodePong(const u8* data, size_t n, PingMsg& out);
void encodeLobby(const LobbyMsg& m, std::vector<u8>& out);
bool decodeLobby(const u8* data, size_t n, LobbyMsg& out);
void encodeLobbyGuest(const LobbyGuestMsg& m, std::vector<u8>& out);
bool decodeLobbyGuest(const u8* data, size_t n, LobbyGuestMsg& out);
void encodeStartMission(const StartMissionMsg& m, std::vector<u8>& out);
bool decodeStartMission(const u8* data, size_t n, StartMissionMsg& out);
void encodeReady(const ReadyMsg& m, std::vector<u8>& out);
bool decodeReady(const u8* data, size_t n, ReadyMsg& out);
void encodeFlowChoice(const FlowChoiceMsg& m, std::vector<u8>& out);
bool decodeFlowChoice(const u8* data, size_t n, FlowChoiceMsg& out);
void encodeHash(const HashMsg& m, std::vector<u8>& out);
bool decodeHash(const u8* data, size_t n, HashMsg& out);
void encodeDesync(const DesyncMsg& m, std::vector<u8>& out);
bool decodeDesync(const u8* data, size_t n, DesyncMsg& out);
void encodeInputs(const InputsPacket& m, std::vector<u8>& out);
bool decodeInputs(const u8* data, size_t n, InputsPacket& out);

} // namespace as3d::net
