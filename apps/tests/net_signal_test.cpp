// The connection codes (as3d/net_signal.h, docs/spec/issues/180-online-coop.md 2.3 and 13 T1):
// the binary form and its CRC, Crockford base32 and its forgiving decoder, the candidate lines
// and their selection, and the SDP round trip on a libdatachannel-style and a Chromium-style
// description.
#include <cstring>
#include <memory>

#include "doctest.h"

#include "as3d/core.h"
#include "as3d/net_signal.h"

using namespace as3d;
using namespace as3d::net;

namespace {

// libdatachannel (v0.24-style offer: session-level fingerprint, media-level ICE, candidates of a
// host with Docker and IPv6, one srflx, end-of-candidates).
const char kLibdatachannelOffer[] =
    "v=0\r\n"
    "o=rtc 3817219813 0 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=group:BUNDLE 0\r\n"
    "a=group:LS 0\r\n"
    "a=msid-semantic:WMS *\r\n"
    "a=ice-options:ice2,trickle\r\n"
    "a=fingerprint:sha-256 6B:8B:5D:EA:59:04:20:23:29:C8:87:1C:CC:87:32:BE:DD:8C:66:A5:8E:50:55:EA:8C:D3:B6:5C:09:5E:A6:BC\r\n"
    "m=application 50373 UDP/DTLS/SCTP webrtc-datachannel\r\n"
    "c=IN IP4 192.168.1.20\r\n"
    "a=mid:0\r\n"
    "a=sendrecv\r\n"
    "a=sctp-port:5000\r\n"
    "a=max-message-size:262144\r\n"
    "a=setup:actpass\r\n"
    "a=ice-ufrag:Vx9q\r\n"
    "a=ice-pwd:Ff0nOh4vR2HaV2vQxY7k3s/+\r\n"
    "a=candidate:1 1 UDP 2122317823 192.168.1.20 50373 typ host\r\n"
    "a=candidate:2 1 UDP 2122317567 172.17.0.1 50373 typ host\r\n"
    "a=candidate:3 1 UDP 2122317311 2a01:e0a:1f2:3b40:4c1e:22ff:fe5d:9a10 50373 typ host\r\n"
    "a=candidate:4 1 UDP 2122317055 fe80::4c1e:22ff:fe5d:9a10 50373 typ host\r\n"
    "a=candidate:5 1 UDP 2122316799 127.0.0.1 50373 typ host\r\n"
    "a=candidate:6 1 UDP 1686109951 82.64.101.7 50373 typ srflx raddr 192.168.1.20 rport 50373\r\n"
    "a=candidate:7 1 TCP 1015022079 192.168.1.20 9 typ host tcptype active\r\n"
    "a=end-of-candidates\r\n";

// Chromium (an answer to a data-channel offer: mDNS host candidates, which a code cannot carry,
// a srflx and an IPv6 host with network-id extensions; fingerprint at media level).
const char kChromiumAnswer[] =
    "v=0\r\n"
    "o=- 4611731400430051336 2 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=group:BUNDLE 0\r\n"
    "a=extmap-allow-mixed\r\n"
    "a=msid-semantic: WMS\r\n"
    "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=candidate:2999745851 1 udp 2122260223 0b5f1a8e-1d3c-4c2a-9f7e-2a1b3c4d5e6f.local 54400 typ host generation 0 network-id 1\r\n"
    "a=candidate:1425432140 1 udp 2122197247 2001:db8:85a3::8a2e:370:7334 54401 typ host generation 0 network-id 2\r\n"
    "a=candidate:842163049 1 udp 1677729535 203.0.113.9 61234 typ srflx raddr 0.0.0.0 rport 0 generation 0 network-id 1 network-cost 10\r\n"
    "a=candidate:4233069003 1 tcp 1518280447 0b5f1a8e-1d3c-4c2a-9f7e-2a1b3c4d5e6f.local 9 typ host tcptype active generation 0 network-id 1\r\n"
    "a=ice-ufrag:E2rd\r\n"
    "a=ice-pwd:KHx1tdsbWQGQ1Nqd4p9IXyLq\r\n"
    "a=ice-options:trickle\r\n"
    "a=fingerprint:sha-256 0A:1B:2C:3D:4E:5F:60:71:82:93:A4:B5:C6:D7:E8:F9:0a:1b:2c:3d:4e:5f:60:71:82:93:a4:b5:c6:d7:e8:f9\r\n"
    "a=setup:active\r\n"
    "a=mid:0\r\n"
    "a=sctp-port:5000\r\n"
    "a=max-message-size:262144\r\n";

SignalCandidate v4(CandidateType t, u8 a, u8 b, u8 c, u8 d, u16 port, u32 prio = 100) {
    SignalCandidate x;
    x.type = t;
    x.address[0] = a;
    x.address[1] = b;
    x.address[2] = c;
    x.address[3] = d;
    x.port = port;
    x.priority = prio;
    return x;
}

SignalCandidate fromLine(const char* line) {
    SignalCandidate c;
    REQUIRE(candidateFromSdpLine(line, c));
    return c;
}

// A worst-case-ish invite: six IPv6 candidates, typical credential lengths (4 and 24, what
// libjuice and Chromium write).
SignalInfo sixIpv6() {
    SignalInfo s;
    s.kind = SignalKind::Invite;
    s.ufrag = "a1B/";
    s.pwd = "abcdefghijklmnopqrstuv+/";
    for (int i = 0; i < 32; i++) s.fingerprint[i] = static_cast<u8>(0xA0 + i);
    s.setup = DtlsSetup::ActPass;
    for (int i = 0; i < 6; i++) {
        SignalCandidate c;
        c.type = i == 0 ? CandidateType::Srflx : CandidateType::Host;
        c.ipv6 = true;
        c.priority = 0xFFFFFFFFu - static_cast<u32>(i);
        for (int k = 0; k < 16; k++) c.address[k] = static_cast<u8>(0x20 + 16 * i + k);
        c.port = static_cast<u16>(65535 - i);
        s.candidates.push_back(c);
    }
    return s;
}

} // namespace

TEST_CASE("net signal: base32 is Crockford's alphabet with a dash every 8 characters") {
    const u8 zero[5] = {0, 0, 0, 0, 0};
    CHECK(base32Encode(zero, 5) == "00000000");
    const u8 ones[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    CHECK(base32Encode(ones, 5) == "ZZZZZZZZ");
    const u8 hello[] = {'H', 'e', 'l', 'l', 'o', '!'};
    std::string code = base32Encode(hello, sizeof hello);
    CHECK(code == "91JPRV3F-44");
    for (char c : std::string("ILOU")) CHECK(code.find(c) == std::string::npos);
    std::vector<u8> out;
    REQUIRE(base32Decode(code, out));
    CHECK(out == std::vector<u8>(hello, hello + sizeof hello));
    CHECK(base32Encode(nullptr, 0).empty());
    REQUIRE(base32Decode("", out));
    CHECK(out.empty());

    // Every length round-trips.
    Rng rng(7);
    for (size_t n = 0; n < 64; n++) {
        std::vector<u8> bytes(n);
        for (u8& b : bytes) b = static_cast<u8>(rng.next());
        std::string t = base32Encode(bytes.data(), bytes.size());
        REQUIRE(base32Decode(t, out));
        CHECK(out == bytes);
    }
}

TEST_CASE("net signal: the base32 decoder forgives case, separators and I/L/O, refuses U and bad padding") {
    const u8 bytes[] = {0x08, 0x42, 0x10, 0x84, 0x21, 0x00, 0x01};
    std::string code = base32Encode(bytes, sizeof bytes);
    std::vector<u8> want(bytes, bytes + sizeof bytes), out;
    // "0" and "1" digits in the code read the same from O / I / L in either case.
    std::string sloppy;
    for (char c : code) {
        if (c == '0') sloppy += 'o';
        else if (c == '1') sloppy += (sloppy.size() % 2) ? 'I' : 'l';
        else if (c == '-') sloppy += " -\r\n";
        else sloppy += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    REQUIRE(base32Decode(sloppy, out));
    CHECK(out == want);
    CHECK(!base32Decode("U0000000", out));
    CHECK(!base32Decode("00*00000", out));
    // Leftover bits: "01" is 10 bits = one byte + 2 nonzero padding bits; "0" alone is 5 bits.
    CHECK(!base32Decode("01", out));
    CHECK(base32Decode("00", out));
    CHECK(out == std::vector<u8>{0});
    CHECK(!base32Decode("0", out));
    CHECK(!base32Decode("000", out)); // 15 bits: a whole spare character
    CHECK(!base32Decode(std::string(5000, '0'), out));
}

TEST_CASE("net signal: the binary form round-trips; version, CRC, lengths and ranges are checked") {
    SignalInfo s = sixIpv6();
    std::vector<u8> bin;
    REQUIRE(encodeSignalBinary(s, bin));
    CHECK(bin.size() == 1 + 1 + 5 + 25 + 33 + 1 + 2 + 1 + 6 * 23 + 4);
    SignalInfo d;
    REQUIRE(decodeSignalBinary(bin.data(), bin.size(), d));
    CHECK(d == s);

    // Each byte flipped: the CRC (or the version) refuses it.
    for (size_t i = 0; i < bin.size(); i++) {
        std::vector<u8> b = bin;
        b[i] ^= 0x40;
        CHECK(!decodeSignalBinary(b.data(), b.size(), d));
    }
    for (size_t n = 0; n < bin.size(); n++) CHECK(!decodeSignalBinary(bin.data(), n, d));

    // Out of range for the encoder.
    SignalInfo bad = s;
    bad.ufrag = "abc";
    CHECK(!encodeSignalBinary(bad, bin));
    CHECK(bin.empty());
    bad = s;
    bad.pwd = std::string(21, 'a');
    CHECK(!encodeSignalBinary(bad, bin));
    bad.pwd = std::string(33, 'a');
    CHECK(!encodeSignalBinary(bad, bin));
    bad = s;
    bad.ufrag = "ab\r\na=x"; // would inject SDP
    CHECK(!encodeSignalBinary(bad, bin));
    bad = s;
    bad.candidates.push_back(bad.candidates[0]);
    CHECK(!encodeSignalBinary(bad, bin));
    CHECK(encodeSignalCode(bad).empty());

    // No candidates is a valid (if useless) code; the kind and setup survive.
    SignalInfo empty = s;
    empty.candidates.clear();
    empty.kind = SignalKind::Reply;
    empty.setup = DtlsSetup::Passive;
    empty.sctpPort = 65535;
    REQUIRE(encodeSignalBinary(empty, bin));
    REQUIRE(decodeSignalBinary(bin.data(), bin.size(), d));
    CHECK(d == empty);
}

TEST_CASE("net signal: a six-candidate IPv6 code round-trips in under 400 characters") {
    SignalInfo s = sixIpv6();
    std::string code = encodeSignalCode(s);
    MESSAGE("six IPv6 candidates: " << code.size() << " characters");
    CHECK(code.size() < 400);
    SignalInfo d;
    REQUIRE(decodeSignalCode(code, d));
    CHECK(d == s);
    // Lower case, line breaks every 40, I/L/O for 1/1/0, and no dashes at all.
    std::string sloppy;
    int n = 0;
    for (char c : code) {
        if (c == '-') continue;
        if (++n % 40 == 0) sloppy += "\n";
        if (c == '1') sloppy += (n % 2) ? 'i' : 'L';
        else if (c == '0') sloppy += 'O';
        else sloppy += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    SignalInfo d2;
    REQUIRE(decodeSignalCode("  " + sloppy + " \r\n", d2));
    CHECK(d2 == s);

    // A typical invite (one srflx, one host IPv4): well under 200 characters.
    SignalInfo t = s;
    t.candidates = {v4(CandidateType::Srflx, 82, 64, 101, 7, 50373), v4(CandidateType::Host, 192, 168, 1, 20, 50373)};
    std::string typical = encodeSignalCode(t);
    MESSAGE("typical invite: " << typical.size() << " characters");
    CHECK(typical.size() < 200);

    // A code with one character changed is refused (CRC), as is a truncated one.
    std::string broken = code;
    broken[10] = broken[10] == 'A' ? 'B' : 'A';
    CHECK(!decodeSignalCode(broken, d));
    CHECK(!decodeSignalCode(code.substr(0, code.size() - 3), d));
    CHECK(!decodeSignalCode("", d));
    CHECK(!decodeSignalCode("hello there", d));
}

TEST_CASE("net signal: random and mutated codes never crash the decoders") {
    SignalInfo s = sixIpv6();
    std::vector<u8> bin;
    REQUIRE(encodeSignalBinary(s, bin));
    Rng rng(2026);
    SignalInfo d;
    int accepted = 0;
    for (int i = 0; i < 10000; i++) {
        std::vector<u8> b;
        if (i % 2 == 0) {
            b.resize(rng.next() % 300);
            for (u8& x : b) x = static_cast<u8>(rng.next());
            if (!b.empty()) b[0] = kSignalVersion;
        } else {
            b = bin;
            b.resize(rng.next() % (bin.size() + 10), 0);
            if (!b.empty()) b[rng.next() % b.size()] ^= static_cast<u8>(rng.next());
        }
        std::unique_ptr<u8[]> exact(new u8[b.size() ? b.size() : 1]);
        if (!b.empty()) std::memcpy(exact.get(), b.data(), b.size());
        if (decodeSignalBinary(b.empty() ? nullptr : exact.get(), b.size(), d)) accepted++;
        std::string text;
        size_t len = rng.next() % 500;
        for (size_t k = 0; k < len; k++) text += static_cast<char>(rng.next() % 128);
        decodeSignalCode(text, d);
    }
    // A random buffer passes the CRC about once in 4 billion; a flipped-and-resized one only when
    // the flip was a no-op on the whole message (xor 0 at full length).
    CHECK(accepted < 50);
}

TEST_CASE("net signal: candidate lines") {
    SignalCandidate c = fromLine("a=candidate:6 1 UDP 1686109951 82.64.101.7 50373 typ srflx raddr 192.168.1.20 rport 50373");
    CHECK(c.type == CandidateType::Srflx);
    CHECK(!c.ipv6);
    CHECK(c.priority == 1686109951u);
    CHECK(c.port == 50373);
    CHECK(candidateAddress(c) == "82.64.101.7");
    c = fromLine("candidate:1 1 udp 4294967295 2001:DB8::1 1 typ prflx");
    CHECK(c.type == CandidateType::Prflx);
    CHECK(c.ipv6);
    CHECK(c.priority == 4294967295u);
    CHECK(candidateAddress(c) == "2001:db8::1");
    c = fromLine("candidate:1 1 udp 1 ::ffff:10.0.0.1 9 typ host\r\n");
    CHECK(candidateAddress(c) == "::ffff:a00:1");
    c = fromLine("candidate:1 1 udp 1 2001:0:0:1:0:0:0:1 9 typ host");
    CHECK(candidateAddress(c) == "2001:0:0:1::1"); // the longer zero run is compressed
    c = fromLine("candidate:1 1 udp 1 1:0:0:2:0:0:0:0 9 typ host");
    CHECK(candidateAddress(c) == "1:0:0:2::");
    c = fromLine("candidate:1 1 udp 1 :: 9 typ host");
    CHECK(candidateAddress(c) == "::");

    SignalCandidate x;
    CHECK(!candidateFromSdpLine("a=candidate:7 1 TCP 1015022079 192.168.1.20 9 typ host tcptype active", x));
    CHECK(!candidateFromSdpLine("a=candidate:1 1 udp 1 abc.local 9 typ host", x));
    CHECK(!candidateFromSdpLine("a=candidate:1 1 udp 1 10.0.0.1 9 typ relay raddr 1.2.3.4 rport 5", x));
    CHECK(!candidateFromSdpLine("a=candidate:1 2 udp 1 10.0.0.1 9 typ host", x)); // component 2
    CHECK(!candidateFromSdpLine("a=candidate:1 1 udp 1 10.0.0.1 0 typ host", x));
    CHECK(!candidateFromSdpLine("a=candidate:1 1 udp 1 10.0.0.1 65536 typ host", x));
    CHECK(!candidateFromSdpLine("a=candidate:1 1 udp 4294967296 10.0.0.1 9 typ host", x));
    CHECK(!candidateFromSdpLine("a=candidate:1 1 udp 1 10.0.0.256 9 typ host", x));
    CHECK(!candidateFromSdpLine("a=candidate:1 1 udp 1 10.0.0 9 typ host", x));
    CHECK(!candidateFromSdpLine("a=candidate:1 1 udp 1 1::2::3 9 typ host", x));
    CHECK(!candidateFromSdpLine("a=candidate:1 1 udp 1 1:2:3:4:5:6:7:8:9 9 typ host", x));
    CHECK(!candidateFromSdpLine("a=candidate:1 1 udp 1 fe80::1%eth0 9 typ host", x));
    CHECK(!candidateFromSdpLine("a=candidate:1 1 udp 1 10.0.0.1 9", x));
    CHECK(!candidateFromSdpLine("a=ice-ufrag:abcd", x));
    CHECK(!candidateFromSdpLine("", x));
}

TEST_CASE("net signal: selectCandidates keeps reachable UDP addresses, srflx first, at most six") {
    std::vector<SignalCandidate> c;
    c.push_back(fromLine("candidate:1 1 udp 2000 fe80::1 5 typ host"));           // link-local: out
    c.push_back(fromLine("candidate:1 1 udp 2000 2001:db8::5 5 typ host"));       // host v6
    c.push_back(v4(CandidateType::Host, 127, 0, 0, 1, 5));                          // loopback: out
    c.push_back(fromLine("candidate:1 1 udp 2000 ::1 5 typ host"));               // loopback: out
    c.push_back(v4(CandidateType::Host, 169, 254, 3, 4, 5));                        // link-local: out
    c.push_back(v4(CandidateType::Host, 192, 168, 1, 2, 5, 300));                   // host v4
    c.push_back(v4(CandidateType::Host, 10, 0, 0, 2, 5, 400));                      // host v4, higher
    c.push_back(v4(CandidateType::Srflx, 203, 0, 113, 9, 7));                       // srflx
    c.push_back(v4(CandidateType::Srflx, 203, 0, 113, 9, 7, 50));                   // duplicate address
    selectCandidates(c);
    REQUIRE(c.size() == 4);
    CHECK(c[0].type == CandidateType::Srflx);
    CHECK(candidateAddress(c[1]) == "10.0.0.2");
    CHECK(candidateAddress(c[2]) == "192.168.1.2");
    CHECK(candidateAddress(c[3]) == "2001:db8::5");

    std::vector<SignalCandidate> many;
    for (int i = 0; i < 10; i++) many.push_back(v4(CandidateType::Host, 10, 0, 0, static_cast<u8>(i + 1), 9));
    selectCandidates(many);
    CHECK(many.size() == static_cast<size_t>(kMaxSignalCandidates));
}

TEST_CASE("net signal: SDP to SignalInfo and back, idempotent on libdatachannel and Chromium samples") {
    SignalInfo s;
    REQUIRE(signalFromSdp(kLibdatachannelOffer, s));
    CHECK(s.kind == SignalKind::Invite); // not in the SDP: untouched
    CHECK(s.ufrag == "Vx9q");
    CHECK(s.pwd == "Ff0nOh4vR2HaV2vQxY7k3s/+");
    CHECK(s.fingerprint[0] == 0x6B);
    CHECK(s.fingerprint[31] == 0xBC);
    CHECK(s.setup == DtlsSetup::ActPass);
    CHECK(s.sctpPort == 5000);
    // srflx, then the two host IPv4 by priority, then the global IPv6; no link-local, loopback, TCP.
    REQUIRE(s.candidates.size() == 4);
    CHECK(s.candidates[0].type == CandidateType::Srflx);
    CHECK(candidateAddress(s.candidates[0]) == "82.64.101.7");
    CHECK(candidateAddress(s.candidates[1]) == "192.168.1.20");
    CHECK(candidateAddress(s.candidates[2]) == "172.17.0.1");
    CHECK(candidateAddress(s.candidates[3]) == "2a01:e0a:1f2:3b40:4c1e:22ff:fe5d:9a10");

    std::string sdp1 = sdpFromSignal(s);
    SignalInfo s2;
    REQUIRE(signalFromSdp(sdp1, s2));
    CHECK(s2 == s);
    CHECK(sdpFromSignal(s2) == sdp1);
    CHECK(sdp1.find("a=candidate:0 1 udp 1686109951 82.64.101.7 50373 typ srflx raddr 0.0.0.0 rport 0 generation 0\r\n") !=
          std::string::npos);
    CHECK(sdp1.find("a=candidate:1 1 udp 2122317823 192.168.1.20 50373 typ host generation 0\r\n") != std::string::npos);
    CHECK(sdp1.find("a=setup:actpass\r\n") != std::string::npos);
    CHECK(sdp1.find("a=fingerprint:sha-256 6B:8B:5D:EA:") != std::string::npos);
    CHECK(sdp1.rfind("v=0\r\no=- ", 0) == 0);
    CHECK(sdp1.size() > 21);
    CHECK(sdp1.substr(sdp1.size() - 21) == "a=end-of-candidates\r\n");

    // Through the code too.
    s.kind = SignalKind::Invite;
    SignalInfo viaCode;
    REQUIRE(decodeSignalCode(encodeSignalCode(s), viaCode));
    CHECK(sdpFromSignal(viaCode) == sdp1);

    SignalInfo c;
    c.kind = SignalKind::Reply;
    REQUIRE(signalFromSdp(kChromiumAnswer, c));
    CHECK(c.kind == SignalKind::Reply);
    CHECK(c.ufrag == "E2rd");
    CHECK(c.setup == DtlsSetup::Active);
    CHECK(c.fingerprint[15] == 0xF9);
    CHECK(c.fingerprint[16] == 0x0A); // lower-case hex read as well
    // The mDNS host candidates are dropped (a code carries addresses only).
    REQUIRE(c.candidates.size() == 2);
    CHECK(c.candidates[0].type == CandidateType::Srflx);
    CHECK(candidateAddress(c.candidates[0]) == "203.0.113.9");
    CHECK(c.candidates[0].port == 61234);
    CHECK(candidateAddress(c.candidates[1]) == "2001:db8:85a3::8a2e:370:7334");
    std::string csdp = sdpFromSignal(c);
    SignalInfo c2;
    c2.kind = SignalKind::Reply;
    REQUIRE(signalFromSdp(csdp, c2));
    CHECK(c2 == c);
    CHECK(sdpFromSignal(c2) == csdp);
    CHECK(csdp.find("a=setup:active\r\n") != std::string::npos);

    // The session id is a function of the ufrag alone.
    SignalInfo other = c;
    other.candidates.clear();
    std::string o1 = sdpFromSignal(c), o2 = sdpFromSignal(other);
    CHECK(o1.substr(0, o1.find("\r\ns=")) == o2.substr(0, o2.find("\r\ns=")));
}

TEST_CASE("net signal: signalFromSdp refuses descriptions missing what a code needs") {
    std::string base = kLibdatachannelOffer;
    auto without = [&](const char* prefix) {
        std::string out;
        size_t pos = 0;
        while (pos < base.size()) {
            size_t nl = base.find('\n', pos);
            std::string line = base.substr(pos, nl - pos + 1);
            if (line.rfind(prefix, 0) != 0) out += line;
            pos = nl + 1;
        }
        return out;
    };
    SignalInfo s;
    CHECK(!signalFromSdp(without("a=ice-ufrag:"), s));
    CHECK(!signalFromSdp(without("a=ice-pwd:"), s));
    CHECK(!signalFromSdp(without("a=fingerprint:"), s));
    CHECK(!signalFromSdp(without("a=setup:"), s));
    REQUIRE(signalFromSdp(without("a=sctp-port:"), s));
    CHECK(s.sctpPort == kDefaultSctpPort);
    CHECK(!signalFromSdp("", s));
    // A sha-1 fingerprint only is not enough; a broken sha-256 one is refused.
    std::string sha1 = without("a=fingerprint:") + "a=fingerprint:sha-1 00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33\r\n";
    CHECK(!signalFromSdp(sha1, s));
    std::string shortFp = without("a=fingerprint:") + "a=fingerprint:sha-256 00:11\r\n";
    CHECK(!signalFromSdp(shortFp, s));
    // \n line ends work as well as \r\n.
    std::string lf;
    for (char ch : base)
        if (ch != '\r') lf += ch;
    REQUIRE(signalFromSdp(lf, s));
    CHECK(s.candidates.size() == 4);
}
