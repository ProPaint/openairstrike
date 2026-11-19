// The ctl messages and the Inputs packet (as3d/net_protocol.h, docs/spec/issues/180-online-coop.md
// 3). T0 stub: the encoders write nothing, the decoders refuse everything. T1 makes them real.
#include "as3d/net_protocol.h"

namespace as3d::net {

u8 messageType(const u8* data, size_t n) { return data && n > 0 ? data[0] : 0; }

void encodeHello(const HelloMsg&, std::vector<u8>& out) { out.clear(); }
bool decodeHello(const u8*, size_t, HelloMsg&) { return false; }
void encodeBye(const ByeMsg&, std::vector<u8>& out) { out.clear(); }
bool decodeBye(const u8*, size_t, ByeMsg&) { return false; }
void encodePing(const PingMsg&, std::vector<u8>& out) { out.clear(); }
bool decodePing(const u8*, size_t, PingMsg&) { return false; }
void encodePong(const PingMsg&, std::vector<u8>& out) { out.clear(); }
bool decodePong(const u8*, size_t, PingMsg&) { return false; }
void encodeLobby(const LobbyMsg&, std::vector<u8>& out) { out.clear(); }
bool decodeLobby(const u8*, size_t, LobbyMsg&) { return false; }
void encodeLobbyGuest(const LobbyGuestMsg&, std::vector<u8>& out) { out.clear(); }
bool decodeLobbyGuest(const u8*, size_t, LobbyGuestMsg&) { return false; }
void encodeStartMission(const StartMissionMsg&, std::vector<u8>& out) { out.clear(); }
bool decodeStartMission(const u8*, size_t, StartMissionMsg&) { return false; }
void encodeReady(const ReadyMsg&, std::vector<u8>& out) { out.clear(); }
bool decodeReady(const u8*, size_t, ReadyMsg&) { return false; }
void encodeFlowChoice(const FlowChoiceMsg&, std::vector<u8>& out) { out.clear(); }
bool decodeFlowChoice(const u8*, size_t, FlowChoiceMsg&) { return false; }
void encodeHash(const HashMsg&, std::vector<u8>& out) { out.clear(); }
bool decodeHash(const u8*, size_t, HashMsg&) { return false; }
void encodeDesync(const DesyncMsg&, std::vector<u8>& out) { out.clear(); }
bool decodeDesync(const u8*, size_t, DesyncMsg&) { return false; }
void encodeInputs(const InputsPacket&, std::vector<u8>& out) { out.clear(); }
bool decodeInputs(const u8*, size_t, InputsPacket&) { return false; }

} // namespace as3d::net
