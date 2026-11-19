// The connection codes (as3d/net_signal.h, docs/spec/issues/180-online-coop.md 2.3). T0 stub:
// nothing encodes or decodes; the comparisons are real. T1 makes the rest real.
#include <cstring>

#include "as3d/net_signal.h"

namespace as3d::net {

bool SignalCandidate::operator==(const SignalCandidate& o) const {
    return type == o.type && ipv6 == o.ipv6 && priority == o.priority && port == o.port &&
           std::memcmp(address, o.address, sizeof(address)) == 0;
}

bool SignalInfo::operator==(const SignalInfo& o) const {
    return kind == o.kind && ufrag == o.ufrag && pwd == o.pwd &&
           std::memcmp(fingerprint, o.fingerprint, sizeof(fingerprint)) == 0 && setup == o.setup &&
           sctpPort == o.sctpPort && candidates == o.candidates;
}

bool encodeSignalBinary(const SignalInfo&, std::vector<u8>& out) {
    out.clear();
    return false;
}
bool decodeSignalBinary(const u8*, size_t, SignalInfo&) { return false; }
std::string base32Encode(const u8*, size_t) { return {}; }
bool base32Decode(std::string_view, std::vector<u8>& out) {
    out.clear();
    return false;
}
std::string encodeSignalCode(const SignalInfo&) { return {}; }
bool decodeSignalCode(std::string_view, SignalInfo&) { return false; }
bool signalFromSdp(std::string_view, SignalInfo&) { return false; }
std::string sdpFromSignal(const SignalInfo&) { return {}; }
bool candidateFromSdpLine(std::string_view, SignalCandidate&) { return false; }
std::string candidateAddress(const SignalCandidate&) { return {}; }
void selectCandidates(std::vector<SignalCandidate>&) {}

} // namespace as3d::net
