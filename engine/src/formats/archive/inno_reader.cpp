// The Inno Setup installer reader of the archive module (as3d/archive.h, docs/spec/archive.md
// section 3): the loader's offset table at the end of the stub, the setup header data (two
// CRC-checked blocks of LZMA1 chunks: the header with its entry tables, then the file location
// table) of setup data versions 5.1.0 to 6.4.x, unicode and ANSI, and the file data in chunks
// compressed with zlib, LZMA1 or LZMA2 (through liblzma's raw decoders), stored, or bzip2
// (refused). Files of x86 code had their CALL and JMP targets rewritten before compression;
// the three variants of that transform are undone. Every file's MD5, SHA-1 or SHA-256
// recorded by the compiler is checked after extraction.
//
// Written from the format's published definitions (Inno Setup's Struct.pas of each version
// and its loader, block and extractor units) and from the survey of docs/spec/issues/170; no
// code of other extractors was used.
#include <lzma.h>
#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "archive_internal.h"

namespace as3d {
namespace archive_internal {

namespace {

std::uint32_t crc32Of(const std::uint8_t* p, size_t n) {
    return static_cast<std::uint32_t>(::crc32(::crc32(0L, Z_NULL, 0), p, static_cast<uInt>(n)));
}

// ---------------------------------------------------------------------------------------
// MD5 (RFC 1321), SHA-1 and SHA-256 (FIPS 180-4), small, for the checksums the compiler
// recorded (one per version range; the engine's own SHA-256 lives in the game module, which
// depends on this one).
// ---------------------------------------------------------------------------------------
class Md5 {
public:
    Md5() { reset(); }
    void reset() {
        h_[0] = 0x67452301u; h_[1] = 0xefcdab89u; h_[2] = 0x98badcfeu; h_[3] = 0x10325476u;
        len_ = 0;
        bufLen_ = 0;
    }
    void update(const std::uint8_t* p, size_t n) {
        len_ += n;
        while (n) {
            const size_t take = std::min(n, sizeof buf_ - bufLen_);
            std::memcpy(buf_ + bufLen_, p, take);
            bufLen_ += take;
            p += take;
            n -= take;
            if (bufLen_ == 64) {
                block(buf_);
                bufLen_ = 0;
            }
        }
    }
    void digest(std::uint8_t out[16]) {
        const std::uint64_t bits = len_ * 8;
        std::uint8_t pad = 0x80;
        update(&pad, 1);
        pad = 0;
        while (bufLen_ != 56) update(&pad, 1);
        std::uint8_t len[8];
        for (int i = 0; i < 8; ++i) len[i] = static_cast<std::uint8_t>(bits >> (8 * i));
        update(len, 8);
        for (int i = 0; i < 4; ++i)
            for (int k = 0; k < 4; ++k) out[4 * i + k] = static_cast<std::uint8_t>(h_[i] >> (8 * k));
    }

private:
    static std::uint32_t rotl(std::uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }
    void block(const std::uint8_t* p) {
        static const std::uint32_t K[64] = {
            0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1,
            0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453,
            0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942,
            0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
            0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d,
            0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
        static const int R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                  4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
        std::uint32_t w[16];
        for (int i = 0; i < 16; ++i) w[i] = le32(p + 4 * i);
        std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
        for (int i = 0; i < 64; ++i) {
            std::uint32_t f;
            int g;
            if (i < 16) { f = (b & c) | (~b & d); g = i; }
            else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) & 15; }
            else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) & 15; }
            else { f = c ^ (b | ~d); g = (7 * i) & 15; }
            const std::uint32_t t = d;
            d = c;
            c = b;
            b = b + rotl(a + f + K[i] + w[g], R[i]);
            a = t;
        }
        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
    }
    std::uint32_t h_[4];
    std::uint8_t buf_[64];
    size_t bufLen_;
    std::uint64_t len_;
};

class Sha1 {
public:
    Sha1() { reset(); }
    void reset() {
        h_[0] = 0x67452301u; h_[1] = 0xEFCDAB89u; h_[2] = 0x98BADCFEu; h_[3] = 0x10325476u; h_[4] = 0xC3D2E1F0u;
        len_ = 0;
        bufLen_ = 0;
    }
    void update(const std::uint8_t* p, size_t n) {
        len_ += n;
        while (n) {
            const size_t take = std::min(n, sizeof buf_ - bufLen_);
            std::memcpy(buf_ + bufLen_, p, take);
            bufLen_ += take;
            p += take;
            n -= take;
            if (bufLen_ == 64) {
                block(buf_);
                bufLen_ = 0;
            }
        }
    }
    void digest(std::uint8_t out[20]) {
        const std::uint64_t bits = len_ * 8;
        std::uint8_t pad = 0x80;
        update(&pad, 1);
        pad = 0;
        while (bufLen_ != 56) update(&pad, 1);
        std::uint8_t len[8];
        for (int i = 0; i < 8; ++i) len[i] = static_cast<std::uint8_t>(bits >> (8 * (7 - i)));
        update(len, 8);
        for (int i = 0; i < 5; ++i)
            for (int k = 0; k < 4; ++k) out[4 * i + k] = static_cast<std::uint8_t>(h_[i] >> (8 * (3 - k)));
    }

private:
    static std::uint32_t rotl(std::uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }
    void block(const std::uint8_t* p) {
        std::uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = (static_cast<std::uint32_t>(p[4 * i]) << 24) | (static_cast<std::uint32_t>(p[4 * i + 1]) << 16) |
                   (static_cast<std::uint32_t>(p[4 * i + 2]) << 8) | p[4 * i + 3];
        for (int i = 16; i < 80; ++i) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4];
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
            const std::uint32_t t = rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl(b, 30);
            b = a;
            a = t;
        }
        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d; h_[4] += e;
    }
    std::uint32_t h_[5];
    std::uint8_t buf_[64];
    size_t bufLen_;
    std::uint64_t len_;
};

class Sha256Sum {
public:
    Sha256Sum() { reset(); }
    void reset() {
        static const std::uint32_t init[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
        std::memcpy(h_, init, sizeof h_);
        len_ = 0;
        bufLen_ = 0;
    }
    void update(const std::uint8_t* p, size_t n) {
        len_ += n;
        while (n) {
            const size_t take = std::min(n, sizeof buf_ - bufLen_);
            std::memcpy(buf_ + bufLen_, p, take);
            bufLen_ += take;
            p += take;
            n -= take;
            if (bufLen_ == 64) {
                block(buf_);
                bufLen_ = 0;
            }
        }
    }
    void digest(std::uint8_t out[32]) {
        const std::uint64_t bits = len_ * 8;
        std::uint8_t pad = 0x80;
        update(&pad, 1);
        pad = 0;
        while (bufLen_ != 56) update(&pad, 1);
        std::uint8_t len[8];
        for (int i = 0; i < 8; ++i) len[i] = static_cast<std::uint8_t>(bits >> (8 * (7 - i)));
        update(len, 8);
        for (int i = 0; i < 8; ++i)
            for (int k = 0; k < 4; ++k) out[4 * i + k] = static_cast<std::uint8_t>(h_[i] >> (8 * (3 - k)));
    }

private:
    static std::uint32_t rotr(std::uint32_t x, int c) { return (x >> c) | (x << (32 - c)); }
    void block(const std::uint8_t* p) {
        static const std::uint32_t K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
            0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
            0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
            0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
            0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
            0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (static_cast<std::uint32_t>(p[4 * i]) << 24) | (static_cast<std::uint32_t>(p[4 * i + 1]) << 16) |
                   (static_cast<std::uint32_t>(p[4 * i + 2]) << 8) | p[4 * i + 3];
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = h + S1 + ch + K[i] + w[i];
            const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = S0 + maj;
            h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d; h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
    }
    std::uint32_t h_[8];
    std::uint8_t buf_[64];
    size_t bufLen_;
    std::uint64_t len_;
};

// ---------------------------------------------------------------------------------------
// The setup data versions this reader knows: the serialised layout of the records it
// needs, per "Inno Setup Setup Data (a.b.c)" version (docs/spec/archive.md section 3.4).
// Offsets are into the fixed part of a record, which follows its length-prefixed strings.
// ---------------------------------------------------------------------------------------
enum Digest { kMd5, kSha1, kSha256 };

struct Row {
    unsigned version;   // major * 1000000 + minor * 1000 + build
    int hdrStrings;     // length-prefixed strings before the header's fixed part
    int hdrFixedA, hdrFixedU; // size of the header's fixed part, ANSI and unicode builds (-1: no such build)
    int cmA, cmU;       // offset of CompressMethod in it
    int langFixedA, langFixedU; // size of a language entry's fixed part (10 strings before it)
    int fileStrings;    // strings of a file entry (its fixed part is 43 bytes, LocationEntry at +20)
    int locFixed;       // size of a file location entry
    int flagsOff;       // its Flags byte
    Digest digest;      // its checksum at +36
};
const Row kRows[] = {
    {5001000, 27, 178, -1, 169, -1, 24, -1, 9, 69, 68, kMd5},
    {5001013, 28, 178, -1, 169, -1, 24, -1, 9, 70, 68, kMd5},
    {5002000, 28, 178, -1, 169, -1, 24, -1, 9, 70, 68, kMd5},
    {5002001, 29, 186, -1, 169, -1, 24, -1, 9, 70, 68, kMd5},
    {5002003, 29, 186, -1, 169, -1, 25, -1, 9, 70, 68, kMd5},
    {5003000, 29, 186, 154, 169, 137, 25, 21, 10, 70, 68, kMd5},
    {5003003, 29, 188, 156, 169, 137, 25, 21, 10, 70, 68, kMd5},
    {5003006, 29, 192, 160, 169, 137, 25, 21, 10, 70, 68, kMd5},
    {5003008, 30, 192, 160, 169, 137, 25, 21, 10, 70, 68, kMd5},
    {5003009, 30, 196, 164, 173, 141, 25, 21, 10, 74, 72, kSha1},
    {5003010, 30, 188, 156, 173, 141, 25, 21, 10, 74, 72, kSha1},
    {5005000, 31, 192, 160, 173, 141, 25, 21, 10, 74, 72, kSha1},
    {5005006, 32, 192, 160, 173, 141, 25, 21, 10, 74, 72, kSha1},
    {5005007, 32, 189, 157, 170, 138, 25, 21, 10, 74, 72, kSha1},
    {6000000, 34, 200, 167, 180, 148, 25, 21, 10, 74, 72, kSha1},
    {6003000, 36, -1, 166, -1, 148, -1, 21, 10, 75, 72, kSha1},
    {6004000, 36, -1, 177, -1, 160, -1, 21, 10, 87, 84, kSha256},
    {6004002, 37, -1, 177, -1, 160, -1, 21, 10, 87, 84, kSha256},
    {6004003, 37, -1, 177, -1, 160, -1, 21, 10, 85, 84, kSha256},
};
constexpr unsigned kFirstVersion = 5001000, kEndVersion = 6005000;
// The fixed parts of the entries between the header and the file entries (all versions here).
constexpr int kMsgStrings = 2, kMsgFixed = 4, kPermStrings = 1, kPermFixed = 0, kTypeStrings = 4, kTypeFixed = 30,
              kCompStrings = 5, kCompFixed = 42, kTaskStrings = 6, kTaskFixed = 26, kDirStrings = 7, kDirFixed = 27,
              kLangStrings = 10, kFileFixed = 43, kFileLocationAt = 20;

struct Layout {
    unsigned version = 0;
    bool unicode = false;
    int hdrStrings = 0, hdrFixed = 0, cmOff = 0, langFixed = 0, fileStrings = 0, locFixed = 0, flagsOff = 0;
    Digest digest = kMd5;
    int countsOff = 0; // the 16 entry counts (ANSI builds: after the 32-byte lead-byte set)
    int callBit = 4, encryptedBit = 6, compressedBit = 7;
    int callVersion = 1; // the call instruction transform: 1 (< 5.2.0), 2 (< 5.3.9), 3
};

bool parseSetupId(const char* id, size_t n, unsigned* version, bool* unicode, std::string* shown) {
    const char* prefix = "Inno Setup Setup Data (";
    const size_t pl = std::strlen(prefix);
    if (n < pl || std::memcmp(id, prefix, pl) != 0) return false;
    unsigned parts[4] = {0, 0, 0, 0};
    int count = 0;
    size_t i = pl;
    while (i < n && count < 4) {
        if (id[i] < '0' || id[i] > '9') return false;
        unsigned v = 0;
        while (i < n && id[i] >= '0' && id[i] <= '9') v = v * 10 + static_cast<unsigned>(id[i++] - '0');
        parts[count++] = v;
        if (i < n && id[i] == '.') ++i;
        else break;
    }
    if (count < 3 || i >= n || id[i] != ')') return false;
    *shown = std::to_string(parts[0]) + "." + std::to_string(parts[1]) + "." + std::to_string(parts[2]);
    *version = parts[0] * 1000000 + parts[1] * 1000 + parts[2];
    *unicode = std::string(id, n).find("(u)") != std::string::npos || parts[0] >= 6;
    return true;
}

bool layoutFor(unsigned version, bool unicode, Layout* L) {
    if (version < kFirstVersion || version >= kEndVersion) return false;
    const Row* row = nullptr;
    for (const Row& r : kRows)
        if (r.version <= version) row = &r;
    if (!row) return false;
    L->version = version;
    L->unicode = unicode;
    L->hdrStrings = row->hdrStrings;
    L->hdrFixed = unicode ? row->hdrFixedU : row->hdrFixedA;
    L->cmOff = unicode ? row->cmU : row->cmA;
    L->langFixed = unicode ? row->langFixedU : row->langFixedA;
    if (L->hdrFixed < 0 || L->cmOff < 0 || L->langFixed < 0) return false;
    L->fileStrings = row->fileStrings;
    L->locFixed = row->locFixed;
    L->flagsOff = row->flagsOff;
    L->digest = row->digest;
    L->countsOff = unicode ? 0 : 32;
    if (version >= 6004003) {
        L->callBit = 2;
        L->encryptedBit = 3;
        L->compressedBit = 4;
    }
    L->callVersion = version < 5002000 ? 1 : version < 5003009 ? 2 : 3;
    return true;
}

// ---------------------------------------------------------------------------------------
// Decoders: a sequential source over a region of the stream, and the chunk decoders.
// ---------------------------------------------------------------------------------------
class Source {
public:
    Source(IStream& s, std::uint64_t pos, std::uint64_t len) : s_(s), pos_(pos), end_(pos + len) {}
    size_t read(std::uint8_t* out, size_t n) {
        const std::uint64_t left = end_ > pos_ ? end_ - pos_ : 0;
        n = static_cast<size_t>(std::min<std::uint64_t>(n, left));
        if (n == 0) return 0;
        if (!s_.readAt(static_cast<size_t>(pos_), out, n)) {
            failed_ = true;
            return 0;
        }
        pos_ += n;
        return n;
    }
    bool failed() const { return failed_; }

private:
    IStream& s_;
    std::uint64_t pos_, end_;
    bool failed_ = false;
};

enum Method { kStored = 0, kZlib = 1, kBzip2 = 2, kLzma1 = 3, kLzma2 = 4 };

class Decoder {
public:
    explicit Decoder(Source& src) : src_(src), in_(64u << 10) {}
    ~Decoder() {
        if (method_ == kLzma1 || method_ == kLzma2) lzma_end(&lz_);
        if (method_ == kZlib && zInit_) inflateEnd(&z_);
    }
    bool init(Method m, std::string* error) {
        method_ = m;
        if (m == kStored) return true;
        if (m == kZlib) {
            std::memset(&z_, 0, sizeof z_);
            if (inflateInit(&z_) != Z_OK) return fail(error, "zlib init failed");
            zInit_ = true;
            return true;
        }
        std::uint8_t props[5];
        const size_t propsLen = m == kLzma1 ? 5 : 1;
        if (src_.read(props, propsLen) != propsLen) return fail(error, "truncated LZMA header");
        lzma_filter filters[2];
        filters[0].id = m == kLzma1 ? LZMA_FILTER_LZMA1 : LZMA_FILTER_LZMA2;
        filters[0].options = nullptr;
        filters[1].id = LZMA_VLI_UNKNOWN;
        filters[1].options = nullptr;
        if (lzma_properties_decode(&filters[0], nullptr, props, static_cast<std::uint32_t>(propsLen)) != LZMA_OK)
            return fail(error, "bad LZMA properties");
        lzma_stream init = LZMA_STREAM_INIT;
        lz_ = init;
        const lzma_ret r = lzma_raw_decoder(&lz_, filters);
        std::free(filters[0].options);
        if (r != LZMA_OK) return fail(error, r == LZMA_MEM_ERROR ? "not enough memory for the LZMA dictionary" : "LZMA init failed");
        return true;
    }
    // Up to n bytes; 0 at the end of the stream, or on failure (then *error is set).
    size_t read(std::uint8_t* out, size_t n, std::string* error) {
        if (ended_ || n == 0) return 0;
        if (method_ == kStored) {
            const size_t got = src_.read(out, n);
            if (got == 0) {
                if (src_.failed()) fail(error, "cannot read the installer");
                ended_ = true;
            }
            return got;
        }
        if (method_ == kZlib) {
            z_.next_out = out;
            z_.avail_out = static_cast<uInt>(n);
            while (z_.avail_out > 0) {
                if (z_.avail_in == 0) {
                    const size_t got = src_.read(in_.data(), in_.size());
                    if (src_.failed()) return fail(error, "cannot read the installer"), 0;
                    z_.next_in = in_.data();
                    z_.avail_in = static_cast<uInt>(got);
                    if (got == 0) inEof_ = true;
                }
                const int r = inflate(&z_, inEof_ ? Z_FINISH : Z_NO_FLUSH);
                if (r == Z_STREAM_END) {
                    ended_ = true;
                    break;
                }
                if (r == Z_BUF_ERROR && inEof_) return fail(error, "truncated zlib data"), 0;
                if (r != Z_OK && r != Z_BUF_ERROR) return fail(error, "corrupt zlib data"), 0;
            }
            return n - z_.avail_out;
        }
        lz_.next_out = out;
        lz_.avail_out = n;
        while (lz_.avail_out > 0) {
            if (lz_.avail_in == 0) {
                const size_t got = src_.read(in_.data(), in_.size());
                if (src_.failed()) return fail(error, "cannot read the installer"), 0;
                lz_.next_in = in_.data();
                lz_.avail_in = got;
                if (got == 0) inEof_ = true;
            }
            const lzma_ret r = lzma_code(&lz_, inEof_ ? LZMA_FINISH : LZMA_RUN);
            if (r == LZMA_STREAM_END) {
                ended_ = true;
                break;
            }
            if (r == LZMA_BUF_ERROR && inEof_) return fail(error, "truncated LZMA data"), 0;
            if (r != LZMA_OK && r != LZMA_BUF_ERROR) return fail(error, r == LZMA_MEM_ERROR ? "not enough memory for the LZMA dictionary" : "corrupt LZMA data"), 0;
        }
        return n - lz_.avail_out;
    }
    // Exactly n bytes, or an error ("truncated" when the stream ends first).
    bool readFully(std::uint8_t* out, size_t n, std::string* error) {
        size_t got = 0;
        while (got < n) {
            std::string err;
            const size_t k = read(out + got, n - got, &err);
            if (k == 0) {
                if (error) *error = err.empty() ? "truncated: the installer's data ends early" : err;
                return false;
            }
            got += k;
        }
        return true;
    }

private:
    bool fail(std::string* error, const char* what) {
        if (error) *error = what;
        ended_ = true;
        return false;
    }
    Source& src_;
    Method method_ = kStored;
    std::vector<std::uint8_t> in_;
    lzma_stream lz_;
    z_stream z_;
    bool zInit_ = false, inEof_ = false, ended_ = false;
};

// Undoes the compiler's CALL/JMP target rewriting (section 3.6 of the spec).
struct CallTransform {
    int version = 1;
    // version 1: a running state over the whole file
    std::uint32_t offset = 5, addr = 0;
    int addrBytesLeft = 0;

    void apply(std::uint8_t* p, size_t n, std::uint32_t blockStart) {
        if (version == 1) {
            for (size_t i = 0; i < n; ++i) {
                if (addrBytesLeft == 0) {
                    if (p[i] == 0xE8 || p[i] == 0xE9) {
                        addr = 0u - offset;
                        addrBytesLeft = 4;
                    }
                } else {
                    addr += p[i];
                    p[i] = static_cast<std::uint8_t>(addr);
                    addr >>= 8;
                    --addrBytesLeft;
                }
                ++offset;
            }
            return;
        }
        if (n < 5) return;
        const size_t limit = n - 4;
        size_t i = 0;
        while (i < limit) {
            if (p[i] != 0xE8 && p[i] != 0xE9) {
                ++i;
                continue;
            }
            ++i;
            if (p[i + 3] == 0x00 || p[i + 3] == 0xFF) {
                if (version == 2) {
                    std::uint32_t a = 0u - (blockStart + static_cast<std::uint32_t>(i) + 4);
                    for (int x = 0; x < 3; ++x) {
                        a += p[i + x];
                        p[i + x] = static_cast<std::uint8_t>(a);
                        a >>= 8;
                    }
                } else {
                    const std::uint32_t a = (blockStart + static_cast<std::uint32_t>(i) + 4) & 0xFFFFFFu;
                    std::uint32_t rel = p[i] | (static_cast<std::uint32_t>(p[i + 1]) << 8) | (static_cast<std::uint32_t>(p[i + 2]) << 16);
                    rel -= a;
                    if (rel & 0x800000u) p[i + 3] = static_cast<std::uint8_t>(~p[i + 3]);
                    p[i] = static_cast<std::uint8_t>(rel);
                    p[i + 1] = static_cast<std::uint8_t>(rel >> 8);
                    p[i + 2] = static_cast<std::uint8_t>(rel >> 16);
                }
            }
            i += 4;
        }
    }
};

std::string utf16ToUtf8(const std::uint8_t* p, size_t n) {
    std::string out;
    for (size_t i = 0; i + 1 < n; i += 2) {
        std::uint32_t c = le16(p + i);
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < n) {
            const std::uint32_t lo = le16(p + i + 2);
            if (lo >= 0xDC00 && lo < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
            }
        }
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

std::string latin1ToUtf8(const std::uint8_t* p, size_t n) {
    std::string out;
    for (size_t i = 0; i < n; ++i) {
        if (p[i] < 0x80) out += static_cast<char>(p[i]);
        else {
            out += static_cast<char>(0xC0 | (p[i] >> 6));
            out += static_cast<char>(0x80 | (p[i] & 0x3F));
        }
    }
    return out;
}

// Walks the records of a decompressed block.
struct Cursor {
    const std::uint8_t* p;
    size_t n, pos = 0;
    bool skipString() {
        if (pos + 4 > n) return false;
        const std::uint32_t len = le32(p + pos);
        pos += 4;
        if (len > n - pos) return false;
        pos += len;
        return true;
    }
    bool string(const std::uint8_t** data, size_t* len) {
        if (pos + 4 > n) return false;
        const std::uint32_t l = le32(p + pos);
        pos += 4;
        if (l > n - pos) return false;
        *data = p + pos;
        *len = l;
        pos += l;
        return true;
    }
    bool skip(size_t k) {
        if (k > n - pos) return false;
        pos += k;
        return true;
    }
};

struct Location {
    std::uint32_t startOffset = 0;
    std::uint64_t suboffset = 0, size = 0, compressedSize = 0;
    std::uint8_t digest[32] = {0};
    std::uint8_t flags = 0;
    int firstSlice = 0, lastSlice = 0;
};

class InnoReader : public ArchiveReader {
public:
    explicit InnoReader(std::unique_ptr<IStream> stream) : stream_(std::move(stream)) {}
    ArchiveKind kind() const override { return ArchiveKind::Inno; }
    void setName(const std::string& n) { name_ = n; }

    bool open(std::string* error) {
        IStream& s = *stream_;
        std::uint64_t tablePos;
        if (!findInnoLoaderTable(s, s.size(), &offset0_, &offset1_, &tablePos)) return fail(error, "no Inno Setup loader table");
        if (offset1_ == 0) return fail(error, "an Inno Setup installer whose files are on separate disks (setup-1.bin ...), which this app cannot open");
        std::uint8_t id[64];
        if (offset0_ + 64 > s.size() || !s.readAt(static_cast<size_t>(offset0_), id, 64)) return fail(error, "truncated Inno Setup installer");
        unsigned version;
        bool unicode;
        if (!parseSetupId(reinterpret_cast<const char*>(id), 64, &version, &unicode, &versionShown_))
            return fail(error, "no Inno Setup version marker where the loader table points");
        if (!layoutFor(version, unicode, &L_))
            return fail(error, "an Inno Setup " + versionShown_ + " installer; this app reads versions 5.1 to 6.4");
        std::vector<std::uint8_t> block;
        std::uint64_t next;
        if (!readBlock(offset0_ + 64, &block, &next, error)) return false;
        if (!parseHeaderBlock(block, error)) return false;
        block.clear();
        if (!readBlock(next, &block, &next, error)) return false;
        if (!parseLocations(block, error)) return false;
        for (size_t i = 0; i < files_.size(); ++i) {
            const File& f = files_[i];
            if (f.location < 0 || static_cast<size_t>(f.location) >= locations_.size()) continue;
            ArchiveEntry e;
            e.name = f.name;
            e.size = locations_[static_cast<size_t>(f.location)].size;
            e.index = entries_.size();
            entryLocation_.push_back(static_cast<size_t>(f.location));
            entries_.push_back(e);
        }
        return true;
    }

    bool extract(const ArchiveEntry& e, const ArchiveSink& sink, std::string* error) override {
        if (e.index >= entries_.size()) return fail(error, "no such entry");
        const Location& loc = locations_[entryLocation_[e.index]];
        if (loc.flags & (1u << L_.encryptedBit)) return fail(error, "a password-protected Inno Setup installer, which this app cannot open");
        if (loc.firstSlice != loc.lastSlice || loc.firstSlice != 0) return fail(error, "the file spans several disks of the installer");
        IStream& s = *stream_;
        const std::uint64_t chunk = offset1_ + loc.startOffset;
        std::uint8_t idb[4];
        if (chunk + 4 + loc.compressedSize > s.size()) return fail(error, "truncated: the installer's data ends early");
        if (!s.readAt(static_cast<size_t>(chunk), idb, 4) || std::memcmp(idb, "zlb\x1A", 4) != 0) return fail(error, "corrupt: no chunk marker where the file should start");
        const bool compressed = (loc.flags & (1u << L_.compressedBit)) != 0;
        const Method m = compressed ? static_cast<Method>(compressMethod_) : kStored;
        if (m == kBzip2) return fail(error, "compressed with bzip2, which this app cannot read");
        if (compressed && compressMethod_ > kLzma2) return fail(error, "unknown compression method " + std::to_string(compressMethod_));
        Source src(s, chunk + 4, loc.compressedSize);
        Decoder d(src);
        if (!d.init(m, error)) return false;
        // The files of a chunk follow each other in its decompressed stream.
        std::vector<std::uint8_t> buf(64u << 10);
        std::uint64_t skip = loc.suboffset;
        while (skip > 0) {
            const size_t k = static_cast<size_t>(std::min<std::uint64_t>(skip, buf.size()));
            if (!d.readFully(buf.data(), k, error)) return false;
            skip -= k;
        }
        CallTransform ct;
        ct.version = L_.callVersion;
        const bool transform = (loc.flags & (1u << L_.callBit)) != 0;
        Md5 md5;
        Sha1 sha1;
        Sha256Sum sha256;
        std::uint64_t left = loc.size, done = 0;
        while (left > 0) {
            const size_t k = static_cast<size_t>(std::min<std::uint64_t>(left, buf.size()));
            if (!d.readFully(buf.data(), k, error)) return false;
            if (transform) ct.apply(buf.data(), k, static_cast<std::uint32_t>(done));
            if (L_.digest == kMd5) md5.update(buf.data(), k);
            else if (L_.digest == kSha1) sha1.update(buf.data(), k);
            else sha256.update(buf.data(), k);
            if (!sink(buf.data(), k)) return fail(error, "stopped");
            left -= k;
            done += k;
        }
        bool match;
        if (L_.digest == kMd5) {
            std::uint8_t dg[16];
            md5.digest(dg);
            match = std::memcmp(dg, loc.digest, 16) == 0;
        } else if (L_.digest == kSha1) {
            std::uint8_t dg[20];
            sha1.digest(dg);
            match = std::memcmp(dg, loc.digest, 20) == 0;
        } else {
            std::uint8_t dg[32];
            sha256.digest(dg);
            match = std::memcmp(dg, loc.digest, 32) == 0;
        }
        if (!match) return fail(error, "checksum mismatch: the installer is corrupt or was cut short");
        return true;
    }

private:
    struct File {
        std::string name;
        int location = -1;
    };

    bool fail(std::string* error, const std::string& what) {
        if (error) *error = what;
        return false;
    }

    // One setup data block: a CRC-checked 9-byte header, then chunks of up to 4 KB each
    // preceded by their CRC-32, holding an LZMA1 stream (or the bytes themselves).
    bool readBlock(std::uint64_t pos, std::vector<std::uint8_t>* out, std::uint64_t* next, std::string* error) {
        IStream& s = *stream_;
        std::uint8_t hdr[9];
        if (pos + 9 > s.size() || !s.readAt(static_cast<size_t>(pos), hdr, 9)) return fail(error, "truncated Inno Setup installer (setup header)");
        if (crc32Of(hdr + 4, 5) != le32(hdr)) return fail(error, "corrupt Inno Setup installer (setup header CRC)");
        const std::uint32_t stored = le32(hdr + 4);
        const bool compressed = hdr[8] != 0;
        if (pos + 9 + stored > s.size()) return fail(error, "truncated Inno Setup installer (setup header block)");
        std::vector<std::uint8_t> raw;
        raw.reserve(stored);
        std::uint64_t p = pos + 9, left = stored;
        std::uint8_t chunk[4096];
        while (left > 0) {
            if (left < 5) return fail(error, "corrupt Inno Setup installer (setup header chunk)");
            std::uint8_t crc[4];
            const size_t len = static_cast<size_t>(std::min<std::uint64_t>(4096, left - 4));
            if (!s.readAt(static_cast<size_t>(p), crc, 4) || !s.readAt(static_cast<size_t>(p + 4), chunk, len))
                return fail(error, "cannot read the installer");
            if (crc32Of(chunk, len) != le32(crc)) return fail(error, "corrupt Inno Setup installer (setup header chunk CRC)");
            raw.insert(raw.end(), chunk, chunk + len);
            p += 4 + len;
            left -= 4 + len;
        }
        *next = pos + 9 + stored;
        if (!compressed) {
            *out = std::move(raw);
            return true;
        }
        std::unique_ptr<IStream> mem = makeMemoryStream(std::move(raw));
        Source src(*mem, 0, mem->size());
        Decoder d(src);
        if (!d.init(kLzma1, error)) return false;
        out->clear();
        std::uint8_t buf[64 << 10];
        for (;;) {
            std::string err;
            const size_t k = d.read(buf, sizeof buf, &err);
            if (k == 0) {
                if (!err.empty()) return fail(error, "corrupt Inno Setup installer (" + err + ")");
                break;
            }
            if (out->size() + k > (64u << 20)) return fail(error, "the installer's setup header is larger than 64 MB");
            out->insert(out->end(), buf, buf + k);
        }
        return true;
    }

    bool parseHeaderBlock(const std::vector<std::uint8_t>& block, std::string* error) {
        Cursor c{block.data(), block.size()};
        const char* corrupt = "corrupt Inno Setup installer (setup header records)";
        for (int i = 0; i < L_.hdrStrings; ++i)
            if (!c.skipString()) return fail(error, corrupt);
        const size_t fixed = c.pos;
        if (fixed + static_cast<size_t>(L_.hdrFixed) > c.n) return fail(error, corrupt);
        std::uint32_t counts[9];
        for (int k = 0; k < 9; ++k) counts[k] = le32(c.p + fixed + static_cast<size_t>(L_.countsOff) + 4 * static_cast<size_t>(k));
        compressMethod_ = c.p[fixed + static_cast<size_t>(L_.cmOff)];
        c.pos = fixed + static_cast<size_t>(L_.hdrFixed);
        for (std::uint32_t k = 0; k < 9; ++k)
            if (counts[k] > 1000000) return fail(error, corrupt);
        auto skipEntries = [&](std::uint32_t count, int strings, int fixedSize) {
            for (std::uint32_t k = 0; k < count; ++k) {
                for (int i = 0; i < strings; ++i)
                    if (!c.skipString()) return false;
                if (!c.skip(static_cast<size_t>(fixedSize))) return false;
            }
            return true;
        };
        if (!skipEntries(counts[0], kLangStrings, L_.langFixed) || !skipEntries(counts[1], kMsgStrings, kMsgFixed) ||
            !skipEntries(counts[2], kPermStrings, kPermFixed) || !skipEntries(counts[3], kTypeStrings, kTypeFixed) ||
            !skipEntries(counts[4], kCompStrings, kCompFixed) || !skipEntries(counts[5], kTaskStrings, kTaskFixed) ||
            !skipEntries(counts[6], kDirStrings, kDirFixed))
            return fail(error, corrupt);
        for (std::uint32_t k = 0; k < counts[7]; ++k) {
            File f;
            for (int i = 0; i < L_.fileStrings; ++i) {
                const std::uint8_t* d;
                size_t len;
                if (!c.string(&d, &len)) return fail(error, corrupt);
                if (i == 1) f.name = L_.unicode ? utf16ToUtf8(d, len) : latin1ToUtf8(d, len);
            }
            if (c.pos + kFileFixed > c.n) return fail(error, corrupt);
            f.location = static_cast<int>(le32(c.p + c.pos + kFileLocationAt));
            c.pos += kFileFixed;
            for (char& ch : f.name)
                if (ch == '\\') ch = '/';
            files_.push_back(std::move(f));
        }
        locationCount_ = counts[8];
        return true;
    }

    bool parseLocations(const std::vector<std::uint8_t>& block, std::string* error) {
        const size_t need = static_cast<size_t>(locationCount_) * static_cast<size_t>(L_.locFixed);
        if (block.size() < need) return fail(error, "corrupt Inno Setup installer (file location table)");
        for (std::uint32_t k = 0; k < locationCount_; ++k) {
            const std::uint8_t* p = block.data() + static_cast<size_t>(k) * static_cast<size_t>(L_.locFixed);
            Location l;
            l.firstSlice = static_cast<int>(le32(p));
            l.lastSlice = static_cast<int>(le32(p + 4));
            l.startOffset = le32(p + 8);
            l.suboffset = le64(p + 12);
            l.size = le64(p + 20);
            l.compressedSize = le64(p + 28);
            std::memcpy(l.digest, p + 36, L_.digest == kMd5 ? 16 : L_.digest == kSha1 ? 20 : 32);
            l.flags = p[L_.flagsOff];
            locations_.push_back(l);
        }
        return true;
    }

    std::unique_ptr<IStream> stream_;
    std::uint64_t offset0_ = 0, offset1_ = 0;
    std::string versionShown_;
    Layout L_;
    unsigned compressMethod_ = 0;
    std::uint32_t locationCount_ = 0;
    std::vector<File> files_;
    std::vector<Location> locations_;
    std::vector<size_t> entryLocation_;
};

} // namespace

bool findInnoLoaderTable(IStream& stream, std::uint64_t limit, std::uint64_t* offset0, std::uint64_t* offset1, std::uint64_t* tablePos) {
    static const std::uint8_t kId[12] = {'r', 'D', 'l', 'P', 't', 'S', 0xCD, 0xE6, 0xD7, 0x7B, 0x0B, 0x2A};
    const size_t window = 1u << 20;
    const std::uint64_t total = std::min<std::uint64_t>(limit, stream.size());
    std::vector<std::uint8_t> buf(window + 44);
    for (std::uint64_t pos = 0; pos < total; pos += window) {
        const size_t n = static_cast<size_t>(std::min<std::uint64_t>(buf.size(), total - pos));
        if (n < 12 || !stream.readAt(static_cast<size_t>(pos), buf.data(), n)) return false;
        for (size_t i = 0; i + 12 <= n && i < window; ++i) {
            if (buf[i] != 'r' || std::memcmp(buf.data() + i, kId, 12) != 0) continue;
            std::uint8_t t[44];
            const std::uint64_t at = pos + i;
            if (at + 44 > stream.size() || !stream.readAt(static_cast<size_t>(at), t, 44)) continue;
            // With the Version field (5.1.5 and later) or without it (older loaders).
            if (crc32Of(t, 40) == le32(t + 40)) {
                *offset0 = le32(t + 32);
                *offset1 = le32(t + 36);
            } else if (crc32Of(t, 36) == le32(t + 36)) {
                *offset0 = le32(t + 28);
                *offset1 = le32(t + 32);
            } else {
                continue;
            }
            *tablePos = at;
            return true;
        }
    }
    return false;
}

std::unique_ptr<ArchiveReader> openInno(std::unique_ptr<IStream> stream, const std::string& name, std::string* error) {
    std::unique_ptr<InnoReader> r(new InnoReader(std::move(stream)));
    r->setName(name);
    if (!r->open(error)) return nullptr;
    return r;
}

} // namespace archive_internal
} // namespace as3d
