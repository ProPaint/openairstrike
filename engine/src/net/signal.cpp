// The connection codes (as3d/net_signal.h, docs/spec/issues/180-online-coop.md 2.3): SignalInfo
// in its binary form (CRC-32 checked), that form in Crockford base32, and the SDP on either side
// rebuilt from it / parsed into it. No dependency beyond as3d_core: the SHA-256 of the session
// id and the CRC-32 are small local copies (as3d/sha256.h lives in the game module), and the
// addresses are parsed and printed here (no socket headers).
#include <algorithm>
#include <cstdio>
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

namespace {

// ICE credential lengths of 2.3 (RFC 8839 allows 4..256 and 22..256; every stack we meet writes
// 4..8 and 22..32).
constexpr size_t kUfragMin = 4, kUfragMax = 32, kPwdMin = 22, kPwdMax = 32;
// The decoder refuses longer text before decoding: the worst code is ~450 characters.
constexpr size_t kMaxCodeText = 4096;

// ice-char (RFC 8839): ALPHA / DIGIT / "+" / "/". Anything else would let a code inject SDP.
bool iceChars(const std::string& s, size_t minLen, size_t maxLen) {
    if (s.size() < minLen || s.size() > maxLen) return false;
    for (char c : s) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '+' || c == '/';
        if (!ok) return false;
    }
    return true;
}

u32 crc32(const u8* p, size_t n) {
    u32 c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return c ^ 0xFFFFFFFFu;
}

// SHA-256 (FIPS 180-4) of a short string, for the SDP session id only.
void sha256(const u8* data, size_t n, u8 out[32]) {
    static const u32 k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    u32 h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::vector<u8> m(data, data + n);
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    u64 bits = static_cast<u64>(n) * 8;
    for (int i = 7; i >= 0; i--) m.push_back(static_cast<u8>(bits >> (8 * i)));
    auto rotr = [](u32 x, int r) { return (x >> r) | (x << (32 - r)); };
    for (size_t b = 0; b < m.size(); b += 64) {
        u32 w[64];
        for (int i = 0; i < 16; i++)
            w[i] = (u32(m[b + 4 * i]) << 24) | (u32(m[b + 4 * i + 1]) << 16) | (u32(m[b + 4 * i + 2]) << 8) | m[b + 4 * i + 3];
        for (int i = 16; i < 64; i++) {
            u32 s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            u32 s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        u32 a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            u32 t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            u32 t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & bb) ^ (a & c) ^ (bb & c));
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = bb;
            bb = a;
            a = t1 + t2;
        }
        h[0] += a;
        h[1] += bb;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 4; j++) out[4 * i + j] = static_cast<u8>(h[i] >> (24 - 8 * j));
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    return s;
}

bool startsWith(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.substr(0, p.size()) == p; }

bool equalsNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

// Splits on spaces and tabs (runs count as one).
std::vector<std::string_view> words(std::string_view s) {
    std::vector<std::string_view> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
        size_t j = i;
        while (j < s.size() && s[j] != ' ' && s[j] != '\t') j++;
        if (j > i) out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

// A decimal number no larger than `max`.
bool parseUnsigned(std::string_view s, u64 max, u64& out) {
    if (s.empty() || s.size() > 20) return false;
    u64 v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        u64 d = static_cast<u64>(c - '0');
        if (v > (max - d) / 10) return false;
        v = v * 10 + d;
    }
    out = v;
    return true;
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool parseIpv4(std::string_view s, u8 out[4]) {
    int part = 0;
    size_t i = 0;
    while (part < 4) {
        size_t j = i;
        while (j < s.size() && s[j] >= '0' && s[j] <= '9') j++;
        u64 v;
        if (j == i || j - i > 3 || !parseUnsigned(s.substr(i, j - i), 255, v)) return false;
        out[part++] = static_cast<u8>(v);
        if (part < 4) {
            if (j >= s.size() || s[j] != '.') return false;
            j++;
        }
        i = j;
    }
    return i == s.size();
}

// RFC 4291 text: hex groups with at most one "::", an embedded IPv4 tail; no zone ("%eth0").
bool parseIpv6(std::string_view s, u8 out[16]) {
    u16 groups[8] = {};
    int count = 0, gap = -1;
    size_t i = 0;
    if (startsWith(s, "::")) {
        gap = 0;
        i = 2;
        if (i == s.size()) {
            std::memset(out, 0, 16);
            return true;
        }
    }
    while (i < s.size()) {
        if (count >= 8) return false;
        size_t j = i;
        while (j < s.size() && hexDigit(s[j]) >= 0) j++;
        if (j < s.size() && s[j] == '.') { // the IPv4 tail takes two groups
            u8 v4[4];
            if (count > 6 || !parseIpv4(s.substr(i), v4)) return false;
            groups[count++] = static_cast<u16>((v4[0] << 8) | v4[1]);
            groups[count++] = static_cast<u16>((v4[2] << 8) | v4[3]);
            i = s.size();
            break;
        }
        if (j == i || j - i > 4) return false;
        u16 g = 0;
        for (size_t k = i; k < j; k++) g = static_cast<u16>((g << 4) | hexDigit(s[k]));
        groups[count++] = g;
        i = j;
        if (i == s.size()) break;
        if (s[i] != ':') return false;
        i++;
        if (i < s.size() && s[i] == ':') {
            if (gap >= 0) return false;
            gap = count;
            i++;
            if (i == s.size()) break;
        } else if (i == s.size()) {
            return false; // a trailing single ':'
        }
    }
    u16 full[8] = {};
    if (gap < 0) {
        if (count != 8) return false;
        std::memcpy(full, groups, sizeof full);
    } else {
        if (count > 7) return false;
        int tail = count - gap;
        for (int k = 0; k < gap; k++) full[k] = groups[k];
        for (int k = 0; k < tail; k++) full[8 - tail + k] = groups[gap + k];
    }
    for (int k = 0; k < 8; k++) {
        out[2 * k] = static_cast<u8>(full[k] >> 8);
        out[2 * k + 1] = static_cast<u8>(full[k]);
    }
    return true;
}

// RFC 5952: lower case, leading zeros dropped, the longest run (two groups or more, the first
// on a tie) of zero groups as "::".
std::string formatIpv6(const u8 a[16]) {
    u16 g[8];
    for (int k = 0; k < 8; k++) g[k] = static_cast<u16>((a[2 * k] << 8) | a[2 * k + 1]);
    int bestStart = -1, bestLen = 0;
    for (int k = 0; k < 8;) {
        if (g[k] != 0) {
            k++;
            continue;
        }
        int e = k;
        while (e < 8 && g[e] == 0) e++;
        if (e - k > bestLen) {
            bestLen = e - k;
            bestStart = k;
        }
        k = e;
    }
    if (bestLen < 2) bestStart = -1;
    std::string out;
    char buf[8];
    for (int k = 0; k < 8; k++) {
        if (k == bestStart) {
            out += "::";
            k += bestLen - 1;
            continue;
        }
        if (!out.empty() && out.back() != ':') out += ':';
        std::snprintf(buf, sizeof buf, "%x", g[k]);
        out += buf;
    }
    return out;
}

bool isLoopback(const SignalCandidate& c) {
    if (!c.ipv6) return c.address[0] == 127;
    for (int i = 0; i < 15; i++)
        if (c.address[i] != 0) return false;
    return c.address[15] == 1;
}

// Addresses no peer can reach: 0.0.0.0 / ::, IPv4 link-local 169.254/16 (an unconfigured
// interface; not in 2.3's list but as useless as fe80::/10), IPv6 link-local fe80::/10.
bool isUnusable(const SignalCandidate& c) {
    if (!c.ipv6) {
        bool zero = c.address[0] == 0 && c.address[1] == 0 && c.address[2] == 0 && c.address[3] == 0;
        return zero || (c.address[0] == 169 && c.address[1] == 254);
    }
    bool zero = true;
    for (int i = 0; i < 16; i++)
        if (c.address[i] != 0) zero = false;
    return zero || (c.address[0] == 0xfe && (c.address[1] & 0xc0) == 0x80);
}

// 2.3's order: server-reflexive (and peer-reflexive) first, then host IPv4, then host IPv6.
int candidateRank(const SignalCandidate& c) {
    if (c.type != CandidateType::Host) return 0;
    return c.ipv6 ? 2 : 1;
}

const char* candidateTypeName(CandidateType t) {
    switch (t) {
        case CandidateType::Host: return "host";
        case CandidateType::Srflx: return "srflx";
        case CandidateType::Prflx: return "prflx";
    }
    return "host";
}

const char kBase32[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

int base32Value(char c) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (c == 'I' || c == 'L') return 1;
    if (c == 'O') return 0;
    for (int i = 0; i < 32; i++)
        if (kBase32[i] == c) return i;
    return -1;
}

} // namespace

// Bumping kSignalVersion (2.3): the version byte comes first and is the only field read before
// the CRC; a decoder refuses any version but its own (the screen says the code is not valid), so
// a new layout needs no compatibility code. Bump it whenever the layout or the meaning of a field
// changes, and bump kNetProtocolVersion (as3d/net_protocol.h) with it when the peers' behaviour
// changes too; old and new builds then refuse each other's codes before any connection is tried.
bool encodeSignalBinary(const SignalInfo& s, std::vector<u8>& out) {
    out.clear();
    if (!iceChars(s.ufrag, kUfragMin, kUfragMax) || !iceChars(s.pwd, kPwdMin, kPwdMax)) return false;
    if (s.candidates.size() > static_cast<size_t>(kMaxSignalCandidates)) return false;
    if (static_cast<u8>(s.kind) > 1 || static_cast<u8>(s.setup) > 2) return false;
    out.push_back(kSignalVersion);
    out.push_back(static_cast<u8>(s.kind));
    out.push_back(static_cast<u8>(s.ufrag.size()));
    out.insert(out.end(), s.ufrag.begin(), s.ufrag.end());
    out.push_back(static_cast<u8>(s.pwd.size()));
    out.insert(out.end(), s.pwd.begin(), s.pwd.end());
    out.push_back(0); // sha-256
    out.insert(out.end(), s.fingerprint, s.fingerprint + 32);
    out.push_back(static_cast<u8>(s.setup));
    out.push_back(static_cast<u8>(s.sctpPort));
    out.push_back(static_cast<u8>(s.sctpPort >> 8));
    out.push_back(static_cast<u8>(s.candidates.size()));
    for (const SignalCandidate& c : s.candidates) {
        if (static_cast<u8>(c.type) > 2) {
            out.clear();
            return false;
        }
        out.push_back(static_cast<u8>(static_cast<u8>(c.type) | (c.ipv6 ? 0x10 : 0)));
        for (int i = 0; i < 4; i++) out.push_back(static_cast<u8>(c.priority >> (8 * i)));
        out.insert(out.end(), c.address, c.address + (c.ipv6 ? 16 : 4));
        out.push_back(static_cast<u8>(c.port));
        out.push_back(static_cast<u8>(c.port >> 8));
    }
    u32 crc = crc32(out.data(), out.size());
    for (int i = 0; i < 4; i++) out.push_back(static_cast<u8>(crc >> (8 * i)));
    return true;
}

bool decodeSignalBinary(const u8* data, size_t n, SignalInfo& out) {
    if (!data || n < 5 || data[0] != kSignalVersion) return false;
    u32 crc = 0;
    for (int i = 0; i < 4; i++) crc |= static_cast<u32>(data[n - 4 + i]) << (8 * i);
    if (crc32(data, n - 4) != crc) return false;
    size_t end = n - 4, pos = 1;
    auto need = [&](size_t k) { return end - pos >= k; };
    SignalInfo s;
    if (!need(1) || data[pos] > 1) return false;
    s.kind = static_cast<SignalKind>(data[pos++]);
    auto readStr = [&](std::string& v) {
        if (!need(1)) return false;
        size_t k = data[pos++];
        if (!need(k)) return false;
        v.assign(reinterpret_cast<const char*>(data + pos), k);
        pos += k;
        return true;
    };
    if (!readStr(s.ufrag) || !iceChars(s.ufrag, kUfragMin, kUfragMax)) return false;
    if (!readStr(s.pwd) || !iceChars(s.pwd, kPwdMin, kPwdMax)) return false;
    if (!need(1 + 32 + 1 + 2 + 1) || data[pos] != 0) return false;
    std::memcpy(s.fingerprint, data + pos + 1, 32);
    pos += 33;
    if (data[pos] > 2) return false;
    s.setup = static_cast<DtlsSetup>(data[pos++]);
    s.sctpPort = static_cast<u16>(data[pos] | (data[pos + 1] << 8));
    pos += 2;
    size_t count = data[pos++];
    if (count > static_cast<size_t>(kMaxSignalCandidates)) return false;
    for (size_t i = 0; i < count; i++) {
        if (!need(1)) return false;
        u8 t = data[pos++];
        if ((t & ~0x13) != 0 || (t & 3) > 2) return false;
        SignalCandidate c;
        c.type = static_cast<CandidateType>(t & 3);
        c.ipv6 = (t & 0x10) != 0;
        size_t alen = c.ipv6 ? 16 : 4;
        if (!need(4 + alen + 2)) return false;
        for (int k = 0; k < 4; k++) c.priority |= static_cast<u32>(data[pos + k]) << (8 * k);
        pos += 4;
        std::memcpy(c.address, data + pos, alen);
        pos += alen;
        c.port = static_cast<u16>(data[pos] | (data[pos + 1] << 8));
        pos += 2;
        if (c.port == 0) return false;
        s.candidates.push_back(c);
    }
    if (pos != end) return false;
    out = std::move(s);
    return true;
}

// 5 bits per character, most significant first; the last character holds the leftover bits
// padded with zeros. A dash after every 8 characters (not at the end).
std::string base32Encode(const u8* data, size_t n) {
    std::string out;
    u32 acc = 0;
    int bits = 0, chars = 0;
    auto put = [&](int v) {
        if (chars > 0 && chars % 8 == 0) out += '-';
        out += kBase32[v & 31];
        chars++;
    };
    for (size_t i = 0; i < n; i++) {
        acc = (acc << 8) | data[i];
        bits += 8;
        while (bits >= 5) {
            put(static_cast<int>(acc >> (bits - 5)));
            bits -= 5;
        }
        acc &= (1u << bits) - 1;
    }
    if (bits > 0) put(static_cast<int>(acc << (5 - bits)));
    return out;
}

// Dashes, spaces, tabs and line breaks are skipped; any other character outside the alphabet
// (U included) fails. The leftover bits of the last character must be zero padding (fewer than
// 8 of them), so that each byte string has exactly one code.
bool base32Decode(std::string_view text, std::vector<u8>& out) {
    out.clear();
    if (text.size() > kMaxCodeText) return false;
    u32 acc = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '-' || c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        int v = base32Value(c);
        if (v < 0) {
            out.clear();
            return false;
        }
        acc = (acc << 5) | static_cast<u32>(v);
        bits += 5;
        if (bits >= 8) {
            out.push_back(static_cast<u8>(acc >> (bits - 8)));
            bits -= 8;
            acc &= (1u << bits) - 1;
        }
    }
    if (bits >= 5 || acc != 0) {
        out.clear();
        return false;
    }
    return true;
}

std::string encodeSignalCode(const SignalInfo& s) {
    std::vector<u8> bin;
    if (!encodeSignalBinary(s, bin)) return {};
    return base32Encode(bin.data(), bin.size());
}

bool decodeSignalCode(std::string_view text, SignalInfo& out) {
    std::vector<u8> bin;
    if (!base32Decode(text, bin)) return false;
    return decodeSignalBinary(bin.data(), bin.size(), out);
}

// candidate:<foundation> <component> <transport> <priority> <address> <port> typ <type> [...]
// Component 1 only (one bundled data-channel transport), UDP only, host / srflx / prflx (relay
// is never used: no TURN), a literal IPv4 or IPv6 address (an mDNS name such as Chromium's
// "<uuid>.local" host candidates cannot go into a code and is dropped).
bool candidateFromSdpLine(std::string_view line, SignalCandidate& out) {
    line = trim(line);
    if (startsWith(line, "a=")) line.remove_prefix(2);
    if (!startsWith(line, "candidate:")) return false;
    line.remove_prefix(10);
    std::vector<std::string_view> w = words(line);
    if (w.size() < 8 || w[1] != "1" || !equalsNoCase(w[2], "udp") || w[6] != "typ") return false;
    SignalCandidate c;
    u64 v;
    if (!parseUnsigned(w[3], 0xFFFFFFFFu, v)) return false;
    c.priority = static_cast<u32>(v);
    if (!parseUnsigned(w[5], 65535, v) || v == 0) return false;
    c.port = static_cast<u16>(v);
    if (w[7] == "host") c.type = CandidateType::Host;
    else if (w[7] == "srflx") c.type = CandidateType::Srflx;
    else if (w[7] == "prflx") c.type = CandidateType::Prflx;
    else return false;
    std::string_view addr = w[4];
    if (addr.find(':') != std::string_view::npos) {
        if (!parseIpv6(addr, c.address)) return false;
        c.ipv6 = true;
    } else if (!parseIpv4(addr, c.address)) {
        return false;
    }
    out = c;
    return true;
}

std::string candidateAddress(const SignalCandidate& c) {
    if (c.ipv6) return formatIpv6(c.address);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%u.%u.%u.%u", c.address[0], c.address[1], c.address[2], c.address[3]);
    return buf;
}

// Within a rank the higher ICE priority first (stable for equal priorities); a duplicate is the
// same address and port, whatever the type (the first after sorting stays).
void selectCandidates(std::vector<SignalCandidate>& candidates) {
    std::vector<SignalCandidate> kept;
    for (const SignalCandidate& c : candidates)
        if (!isLoopback(c) && !isUnusable(c)) kept.push_back(c);
    std::stable_sort(kept.begin(), kept.end(), [](const SignalCandidate& a, const SignalCandidate& b) {
        int ra = candidateRank(a), rb = candidateRank(b);
        if (ra != rb) return ra < rb;
        return a.priority > b.priority;
    });
    std::vector<SignalCandidate> out;
    for (const SignalCandidate& c : kept) {
        bool dup = false;
        for (const SignalCandidate& o : out)
            if (o.ipv6 == c.ipv6 && o.port == c.port && std::memcmp(o.address, c.address, sizeof c.address) == 0) dup = true;
        if (dup) continue;
        out.push_back(c);
        if (out.size() == static_cast<size_t>(kMaxSignalCandidates)) break;
    }
    candidates = std::move(out);
}

// Lines at session or media level, in any order, "\r\n" or "\n" line ends. Required: ice-ufrag,
// ice-pwd (ice-chars, the lengths of the binary form), a sha-256 fingerprint (other algorithms
// are skipped) and a=setup; a=sctp-port defaults to 5000. When a field appears twice (session and
// media level) the last one wins.
bool signalFromSdp(std::string_view sdp, SignalInfo& out) {
    SignalInfo s;
    s.kind = out.kind;
    bool haveFp = false, haveSetup = false;
    std::vector<SignalCandidate> cands;
    size_t pos = 0;
    while (pos <= sdp.size()) {
        size_t nl = sdp.find('\n', pos);
        if (nl == std::string_view::npos) nl = sdp.size();
        std::string_view line = trim(sdp.substr(pos, nl - pos));
        pos = nl + 1;
        if (startsWith(line, "a=ice-ufrag:")) {
            s.ufrag = std::string(trim(line.substr(12)));
        } else if (startsWith(line, "a=ice-pwd:")) {
            s.pwd = std::string(trim(line.substr(10)));
        } else if (startsWith(line, "a=fingerprint:")) {
            std::vector<std::string_view> w = words(line.substr(14));
            if (w.size() != 2 || !equalsNoCase(w[0], "sha-256")) continue;
            std::string_view hex = w[1];
            if (hex.size() != 32 * 3 - 1) return false;
            for (int i = 0; i < 32; i++) {
                int hi = hexDigit(hex[3 * i]), lo = hexDigit(hex[3 * i + 1]);
                if (hi < 0 || lo < 0 || (i < 31 && hex[3 * i + 2] != ':')) return false;
                s.fingerprint[i] = static_cast<u8>((hi << 4) | lo);
            }
            haveFp = true;
        } else if (startsWith(line, "a=setup:")) {
            std::string_view v = trim(line.substr(8));
            if (v == "actpass") s.setup = DtlsSetup::ActPass;
            else if (v == "active") s.setup = DtlsSetup::Active;
            else if (v == "passive") s.setup = DtlsSetup::Passive;
            else return false;
            haveSetup = true;
        } else if (startsWith(line, "a=sctp-port:")) {
            u64 v;
            if (!parseUnsigned(trim(line.substr(12)), 65535, v) || v == 0) return false;
            s.sctpPort = static_cast<u16>(v);
        } else if (startsWith(line, "a=candidate:")) {
            SignalCandidate c;
            if (candidateFromSdpLine(line, c)) cands.push_back(c);
        }
    }
    if (!haveFp || !haveSetup || !iceChars(s.ufrag, kUfragMin, kUfragMax) || !iceChars(s.pwd, kPwdMin, kPwdMax))
        return false;
    selectCandidates(cands);
    s.candidates = std::move(cands);
    out = std::move(s);
    return true;
}

// The minimal data-channel description of 2.3. The session id is the first 8 bytes of
// SHA-256(ufrag), big-endian, with the top bit cleared (some parsers read it as a signed 64-bit
// number); both sides derive it from the code alone.
std::string sdpFromSignal(const SignalInfo& s) {
    u8 digest[32];
    sha256(reinterpret_cast<const u8*>(s.ufrag.data()), s.ufrag.size(), digest);
    u64 sessionId = 0;
    for (int i = 0; i < 8; i++) sessionId = (sessionId << 8) | digest[i];
    sessionId &= 0x7FFFFFFFFFFFFFFFull;
    std::string fp;
    char buf[64];
    for (int i = 0; i < 32; i++) {
        std::snprintf(buf, sizeof buf, i ? ":%02X" : "%02X", s.fingerprint[i]);
        fp += buf;
    }
    const char* setup = s.setup == DtlsSetup::Active ? "active" : s.setup == DtlsSetup::Passive ? "passive" : "actpass";
    std::string out;
    std::snprintf(buf, sizeof buf, "%llu", static_cast<unsigned long long>(sessionId));
    out += "v=0\r\no=- ";
    out += buf;
    out += " 0 IN IP4 127.0.0.1\r\n"
           "s=-\r\n"
           "t=0 0\r\n"
           "a=group:BUNDLE 0\r\n"
           "a=msid-semantic: WMS\r\n"
           "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
           "c=IN IP4 0.0.0.0\r\n"
           "a=mid:0\r\n";
    out += "a=ice-ufrag:" + s.ufrag + "\r\n";
    out += "a=ice-pwd:" + s.pwd + "\r\n";
    out += "a=fingerprint:sha-256 " + fp + "\r\n";
    out += std::string("a=setup:") + setup + "\r\n";
    out += "a=sctp-port:" + std::to_string(s.sctpPort) + "\r\n";
    out += "a=max-message-size:262144\r\n";
    for (size_t i = 0; i < s.candidates.size(); i++) {
        const SignalCandidate& c = s.candidates[i];
        out += "a=candidate:" + std::to_string(i) + " 1 udp " + std::to_string(c.priority) + " " + candidateAddress(c) +
               " " + std::to_string(c.port) + " typ " + candidateTypeName(c.type);
        if (c.type != CandidateType::Host) out += " raddr 0.0.0.0 rport 0";
        out += " generation 0\r\n";
    }
    out += "a=end-of-candidates\r\n";
    return out;
}

} // namespace as3d::net
