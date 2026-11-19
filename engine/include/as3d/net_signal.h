// The connection codes of online play (docs/spec/issues/180-online-coop.md 2.3): what one peer's
// WebRTC session description holds that the other needs (ICE credentials, the DTLS fingerprint,
// the candidates), its compact binary form, and that form in Crockford base32 for pasting
// through any chat. The SDP on either side is rebuilt from a SignalInfo (sdpFromSignal) and
// parsed into one (signalFromSdp): the code never carries raw SDP.
//
// Binary layout (little-endian), kSignalVersion = 1:
//   u8 version, u8 kind (0 invite, 1 reply), u8 n + ufrag (ASCII 4..32), u8 n + pwd (22..32),
//   u8 algo (0 = sha-256) + 32 bytes of fingerprint, u8 setup (0 actpass, 1 active, 2 passive),
//   u16 sctpPort, u8 count (<= kMaxSignalCandidates), per candidate: u8 type (bits 0..1: 0 host,
//   1 srflx, 2 prflx) | bit 4 (IPv6), u32 priority, 4 or 16 bytes of address, u16 port; then
//   u32 CRC-32 of everything before it.
// Bumping the version: a decoder sees the first byte, refuses an unknown version with false
// (the screen says "not valid"), and kNetProtocolVersion (as3d/net_protocol.h) moves with it
// when the meaning of a field changes. T1 documents the bump path here.
//
// No dependency beyond as3d_core: the rtc backend (engine/src/net/rtc) converts libdatachannel's
// descriptions through these functions and never parses SDP itself.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "as3d/core.h"

namespace as3d::net {

constexpr u8 kSignalVersion = 1;
constexpr int kMaxSignalCandidates = 6;
constexpr u16 kDefaultSctpPort = 5000;

enum class SignalKind : u8 { Invite = 0, Reply = 1 };
enum class DtlsSetup : u8 { ActPass = 0, Active = 1, Passive = 2 };
enum class CandidateType : u8 { Host = 0, Srflx = 1, Prflx = 2 };

// One ICE candidate as the code keeps it: UDP only; the foundation is regenerated as the index,
// raddr / rport are written as 0.0.0.0 0 (2.3).
struct SignalCandidate {
    CandidateType type = CandidateType::Host;
    bool ipv6 = false;
    u32 priority = 0;
    u8 address[16] = {};       // IPv4 in the first 4 bytes, network order
    u16 port = 0;
    bool operator==(const SignalCandidate& o) const;
};

struct SignalInfo {
    SignalKind kind = SignalKind::Invite;
    std::string ufrag, pwd;    // a=ice-ufrag, a=ice-pwd
    u8 fingerprint[32] = {};   // a=fingerprint:sha-256
    DtlsSetup setup = DtlsSetup::ActPass;
    u16 sctpPort = kDefaultSctpPort;
    std::vector<SignalCandidate> candidates; // at most kMaxSignalCandidates after selectCandidates
    bool operator==(const SignalInfo& o) const;
};

// The binary form above. encode: false when a field is out of range (ufrag / pwd length, more
// than kMaxSignalCandidates candidates). decode: bounds-checked, false on anything odd (version,
// lengths, CRC).
bool encodeSignalBinary(const SignalInfo& s, std::vector<u8>& out);
bool decodeSignalBinary(const u8* data, size_t n, SignalInfo& out);

// Crockford base32 (0-9, A-Z without I L O U), a dash every 8 characters. The decoder ignores
// dashes, spaces and line breaks, accepts lower case and maps I / L to 1 and O to 0.
std::string base32Encode(const u8* data, size_t n);
bool base32Decode(std::string_view text, std::vector<u8>& out);

// The code the players paste: base32 of the binary form. decodeSignalCode is false for anything
// that is not a whole, valid code; the caller compares `out.kind` with what it expects
// (online.err.wrong_kind).
std::string encodeSignalCode(const SignalInfo& s);
bool decodeSignalCode(std::string_view text, SignalInfo& out);

// SDP <-> SignalInfo. signalFromSdp reads a=ice-ufrag, a=ice-pwd, a=fingerprint, a=setup,
// a=sctp-port and a=candidate lines at session or media level and ignores the rest; `kind` is
// not in the SDP (the caller sets it). False when a required field is missing. Candidates go
// through selectCandidates. sdpFromSignal writes the minimal data-channel description of 2.3
// (sessionId = the first 8 bytes of SHA-256(ufrag) as decimal).
bool signalFromSdp(std::string_view sdp, SignalInfo& out);
std::string sdpFromSignal(const SignalInfo& s);
// One "candidate:..." or "a=candidate:..." line (the onLocalCandidate form). False for what
// the code does not keep (TCP, malformed).
bool candidateFromSdpLine(std::string_view line, SignalCandidate& out);
// The address as text ("192.168.1.2", "2001:db8::1").
std::string candidateAddress(const SignalCandidate& c);
// The selection of 2.3 in place: UDP only (already), no loopback, no IPv6 link-local (fe80::/10),
// server-reflexive first, then host IPv4, then host IPv6, duplicates dropped, cut at
// kMaxSignalCandidates.
void selectCandidates(std::vector<SignalCandidate>& candidates);

} // namespace as3d::net
