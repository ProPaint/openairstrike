// as3d::ZipReader and the inflater (as3d/zip_reader.h).
#include "as3d/zip_reader.h"

#include <cstring>
#include <memory>

namespace as3d {

namespace {

// ---------------------------------------------------------------------------------------
// CRC-32 (IEEE 802.3, reflected, as zip uses it)
// ---------------------------------------------------------------------------------------
struct CrcTable {
    std::uint32_t t[256];
    CrcTable() {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
    }
};

// ---------------------------------------------------------------------------------------
// Inflate (RFC 1951). Input from a pull source, output through a 64 KB ring that keeps the
// 32 KB window and is handed to the sink in 32 KB pieces, so a 40 MB pak costs no more memory
// than a small one.
// ---------------------------------------------------------------------------------------
using Source = std::function<size_t(std::uint8_t* buf, size_t n)>; // 0 = end of input

constexpr int kMaxBits = 15;

// A canonical Huffman code as a direct lookup table on kMaxBits reversed bits:
// entry = symbol | (length << 9); length 0 marks an unused pattern.
struct Huffman {
    std::uint16_t table[1 << kMaxBits];
    // false if over-subscribed. Incomplete codes are allowed (unused entries stay invalid).
    bool build(const std::uint8_t* lengths, int count) {
        int blCount[kMaxBits + 1] = {0};
        for (int i = 0; i < count; ++i) blCount[lengths[i]]++;
        blCount[0] = 0;
        int left = 1;
        for (int len = 1; len <= kMaxBits; ++len) {
            left = (left << 1) - blCount[len];
            if (left < 0) return false;
        }
        int next[kMaxBits + 2] = {0};
        int code = 0;
        for (int len = 1; len <= kMaxBits; ++len) {
            code = (code + blCount[len - 1]) << 1;
            next[len] = code;
        }
        std::memset(table, 0, sizeof table);
        for (int sym = 0; sym < count; ++sym) {
            int len = lengths[sym];
            if (!len) continue;
            int c = next[len]++;
            int rev = 0;
            for (int i = 0; i < len; ++i) rev |= ((c >> i) & 1) << (len - 1 - i);
            const std::uint16_t entry = static_cast<std::uint16_t>(sym | (len << 9));
            for (int fill = rev; fill < (1 << kMaxBits); fill += 1 << len) table[fill] = entry;
        }
        return true;
    }
};

const std::uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const std::uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const std::uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                     193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const std::uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                     6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

class Inflater {
public:
    Inflater(const Source& src, const ZipSink& sink, std::uint64_t maxOut)
        : src_(src), sink_(sink), maxOut_(maxOut), ring_(kRing), in_(1 << 16) {}

    // True when the final block was decoded and flushed.
    bool run() {
        for (;;) {
            const unsigned final = bits(1);
            const unsigned type = bits(2);
            bool ok;
            if (type == 0) ok = stored();
            else if (type == 1) ok = fixed();
            else if (type == 2) ok = dynamic();
            else ok = false;
            if (!ok || bad_) return false;
            if (final) break;
        }
        return flush();
    }
    std::uint64_t produced() const { return outPos_; }

private:
    static constexpr size_t kRing = 1 << 16;
    static constexpr size_t kFlushAt = 1 << 15;

    // ---- input
    void refill() {
        while (bitCount_ <= 56) {
            if (inPos_ == inLen_) {
                inLen_ = eof_ ? 0 : src_(in_.data(), in_.size());
                inPos_ = 0;
                if (inLen_ == 0) {
                    eof_ = true;
                    // Pad with zeros; consuming them is caught in bits().
                    padBits_ += 8;
                    bitCount_ += 8;
                    continue;
                }
            }
            bitBuf_ |= std::uint64_t(in_[inPos_++]) << bitCount_;
            bitCount_ += 8;
        }
    }
    unsigned peek(int n) {
        if (bitCount_ < n) refill();
        return static_cast<unsigned>(bitBuf_ & ((1ull << n) - 1));
    }
    void drop(int n) {
        bitBuf_ >>= n;
        bitCount_ -= n;
        if (bitCount_ < padBits_) bad_ = true; // read past the end of the data
    }
    unsigned bits(int n) {
        if (n == 0) return 0;
        unsigned v = peek(n);
        drop(n);
        return v;
    }
    int decode(const Huffman& h) {
        const std::uint16_t e = h.table[peek(kMaxBits)];
        const int len = e >> 9;
        if (!len) {
            bad_ = true;
            return -1;
        }
        drop(len);
        return e & 0x1FF;
    }

    // ---- output
    bool put(std::uint8_t b) {
        if (outPos_ >= maxOut_) return false;
        ring_[outPos_ & (kRing - 1)] = b;
        ++outPos_;
        if (outPos_ - flushed_ == kFlushAt) return flush();
        return true;
    }
    bool flush() {
        while (flushed_ < outPos_) {
            size_t start = flushed_ & (kRing - 1);
            size_t n = static_cast<size_t>(outPos_ - flushed_);
            if (start + n > kRing) n = kRing - start;
            if (!sink_(ring_.data() + start, n)) return false;
            flushed_ += n;
        }
        return true;
    }

    // ---- blocks
    bool stored() {
        // To the next byte boundary: whole bytes are loaded, so the bits left over are
        // bitCount_ % 8.
        drop(bitCount_ % 8);
        const unsigned len = bits(16), nlen = bits(16);
        if (bad_ || (len ^ 0xFFFFu) != nlen) return false;
        for (unsigned i = 0; i < len; ++i) {
            if (!put(static_cast<std::uint8_t>(bits(8))) || bad_) return false;
        }
        return true;
    }
    bool codes(const Huffman& lit, const Huffman& dist) {
        for (;;) {
            const int sym = decode(lit);
            if (sym < 0) return false;
            if (sym < 256) {
                if (!put(static_cast<std::uint8_t>(sym))) return false;
                continue;
            }
            if (sym == 256) return !bad_;
            const int li = sym - 257;
            if (li >= 29) return false;
            const unsigned len = kLenBase[li] + bits(kLenExtra[li]);
            const int di = decode(dist);
            if (di < 0 || di >= 30) return false;
            const unsigned d = kDistBase[di] + bits(kDistExtra[di]);
            if (bad_ || d > outPos_) return false;
            for (unsigned i = 0; i < len; ++i)
                if (!put(ring_[(outPos_ - d) & (kRing - 1)])) return false;
        }
    }
    bool fixed() {
        if (!fixedReady_) {
            std::uint8_t l[288];
            for (int i = 0; i < 144; ++i) l[i] = 8;
            for (int i = 144; i < 256; ++i) l[i] = 9;
            for (int i = 256; i < 280; ++i) l[i] = 7;
            for (int i = 280; i < 288; ++i) l[i] = 8;
            fixedLit_.build(l, 288);
            std::uint8_t d[30];
            for (int i = 0; i < 30; ++i) d[i] = 5;
            fixedDist_.build(d, 30);
            fixedReady_ = true;
        }
        return codes(fixedLit_, fixedDist_);
    }
    bool dynamic() {
        static const std::uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        const int nlen = static_cast<int>(bits(5)) + 257, ndist = static_cast<int>(bits(5)) + 1;
        const int ncode = static_cast<int>(bits(4)) + 4;
        if (nlen > 286 || ndist > 30) return false;
        std::uint8_t lengths[320] = {0};
        for (int i = 0; i < ncode; ++i) lengths[order[i]] = static_cast<std::uint8_t>(bits(3));
        if (!lenCode_.build(lengths, 19)) return false;
        int index = 0;
        std::memset(lengths, 0, sizeof lengths);
        while (index < nlen + ndist) {
            int sym = decode(lenCode_);
            if (sym < 0) return false;
            if (sym < 16) {
                lengths[index++] = static_cast<std::uint8_t>(sym);
                continue;
            }
            std::uint8_t value = 0;
            int repeat;
            if (sym == 16) {
                if (index == 0) return false;
                value = lengths[index - 1];
                repeat = 3 + static_cast<int>(bits(2));
            } else if (sym == 17) repeat = 3 + static_cast<int>(bits(3));
            else repeat = 11 + static_cast<int>(bits(7));
            if (index + repeat > nlen + ndist) return false;
            while (repeat--) lengths[index++] = value;
        }
        if (bad_ || lengths[256] == 0) return false;
        if (!dynLit_.build(lengths, nlen) || !dynDist_.build(lengths + nlen, ndist)) return false;
        return codes(dynLit_, dynDist_);
    }

    const Source& src_;
    const ZipSink& sink_;
    std::uint64_t maxOut_;
    std::vector<std::uint8_t> ring_;
    std::vector<std::uint8_t> in_;
    size_t inPos_ = 0, inLen_ = 0;
    bool eof_ = false, bad_ = false;
    std::uint64_t bitBuf_ = 0;
    int bitCount_ = 0, padBits_ = 0;
    std::uint64_t outPos_ = 0, flushed_ = 0;
    bool fixedReady_ = false;
    Huffman fixedLit_, fixedDist_, lenCode_, dynLit_, dynDist_;
};

bool inflateStream(const Source& src, const ZipSink& sink, std::uint64_t maxOut, std::uint64_t* produced) {
    // The tables are large (5 x 64 KB): on the heap.
    std::unique_ptr<Inflater> inf(new Inflater(src, sink, maxOut));
    bool ok = inf->run();
    if (produced) *produced = inf->produced();
    return ok;
}

std::uint16_t le16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t le32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
std::uint64_t le64(const std::uint8_t* p) { return std::uint64_t(le32(p)) | (std::uint64_t(le32(p + 4)) << 32); }

bool seekTo(std::FILE* f, std::uint64_t off) {
#if defined(_WIN32)
    return _fseeki64(f, static_cast<long long>(off), SEEK_SET) == 0;
#else
    return fseeko(f, static_cast<off_t>(off), SEEK_SET) == 0;
#endif
}

bool readAt(std::FILE* f, std::uint64_t off, void* buf, size_t n) {
    return seekTo(f, off) && std::fread(buf, 1, n, f) == n;
}

bool fail(std::string* error, const std::string& msg) {
    if (error) *error = msg;
    return false;
}

} // namespace

std::uint32_t crc32Update(std::uint32_t crc, const std::uint8_t* data, size_t n) {
    static const CrcTable table;
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table.t[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

bool inflateRaw(const std::uint8_t* data, size_t n, std::vector<std::uint8_t>* out) {
    size_t pos = 0;
    Source src = [&](std::uint8_t* buf, size_t want) {
        size_t k = n - pos < want ? n - pos : want;
        std::memcpy(buf, data + pos, k);
        pos += k;
        return k;
    };
    out->clear();
    ZipSink sink = [&](const std::uint8_t* p, size_t k) {
        out->insert(out->end(), p, p + k);
        return true;
    };
    return inflateStream(src, sink, ~0ull, nullptr);
}

ZipReader::~ZipReader() {
    if (f_) std::fclose(f_);
}

bool looksLikeZip(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::uint8_t sig[4] = {0};
    bool ok = std::fread(sig, 1, 4, f) == 4;
    std::fclose(f);
    if (!ok) return false;
    const std::uint32_t s = le32(sig);
    return s == 0x04034b50u || s == 0x06054b50u;
}

bool ZipReader::open(const std::string& path, std::string* error) {
    if (f_) std::fclose(f_);
    entries_.clear();
    f_ = std::fopen(path.c_str(), "rb");
    if (!f_) return fail(error, "cannot open");
    if (!seekTo(f_, 0) || std::fseek(f_, 0, SEEK_END) != 0) return fail(error, "cannot seek");
#if defined(_WIN32)
    fileSize_ = static_cast<std::uint64_t>(_ftelli64(f_));
#else
    fileSize_ = static_cast<std::uint64_t>(ftello(f_));
#endif
    // The end of central directory record: within the last 64 KB + 22 bytes (comment).
    const std::uint64_t tail = fileSize_ < 65557 ? fileSize_ : 65557;
    if (tail < 22) return fail(error, "not a zip file");
    std::vector<std::uint8_t> buf(static_cast<size_t>(tail));
    if (!readAt(f_, fileSize_ - tail, buf.data(), buf.size())) return fail(error, "read error");
    long eocd = -1;
    for (long i = static_cast<long>(buf.size()) - 22; i >= 0; --i)
        if (le32(&buf[static_cast<size_t>(i)]) == 0x06054b50u) {
            eocd = i;
            break;
        }
    if (eocd < 0) return fail(error, "not a zip file (no central directory)");
    const std::uint8_t* e = &buf[static_cast<size_t>(eocd)];
    std::uint64_t count = le16(e + 10), cdSize = le32(e + 12), cdOffset = le32(e + 16);
    if (count == 0xFFFF || cdSize == 0xFFFFFFFFu || cdOffset == 0xFFFFFFFFu) {
        // Zip64: the locator sits just before the end record.
        const std::uint64_t locAt = fileSize_ - tail + static_cast<std::uint64_t>(eocd);
        std::uint8_t loc[20], rec[56];
        if (locAt < 20 || !readAt(f_, locAt - 20, loc, 20) || le32(loc) != 0x07064b50u)
            return fail(error, "zip64 locator missing");
        if (!readAt(f_, le64(loc + 8), rec, 56) || le32(rec) != 0x06064b50u) return fail(error, "zip64 record missing");
        count = le64(rec + 32);
        cdSize = le64(rec + 40);
        cdOffset = le64(rec + 48);
    }
    if (cdOffset > fileSize_ || cdSize > fileSize_ - cdOffset || cdSize > (256u << 20))
        return fail(error, "bad central directory");
    std::vector<std::uint8_t> cd(static_cast<size_t>(cdSize));
    if (!cd.empty() && !readAt(f_, cdOffset, cd.data(), cd.size())) return fail(error, "read error");
    size_t p = 0;
    for (std::uint64_t i = 0; i < count; ++i) {
        if (p + 46 > cd.size() || le32(&cd[p]) != 0x02014b50u) return fail(error, "bad central directory entry");
        const std::uint8_t* h = &cd[p];
        ZipEntry z;
        z.flags = le16(h + 8);
        z.method = le16(h + 10);
        z.crc = le32(h + 16);
        z.compSize = le32(h + 20);
        z.size = le32(h + 24);
        const size_t nameLen = le16(h + 28), extraLen = le16(h + 30), commentLen = le16(h + 32);
        z.localOffset = le32(h + 42);
        if (p + 46 + nameLen + extraLen + commentLen > cd.size()) return fail(error, "bad central directory entry");
        z.name.assign(reinterpret_cast<const char*>(h + 46), nameLen);
        for (char& c : z.name)
            if (c == '\\') c = '/';
        // Zip64 extended information: the 0xFFFFFFFF fields, in this order.
        const std::uint8_t* x = h + 46 + nameLen;
        for (size_t k = 0; k + 4 <= extraLen;) {
            const std::uint16_t id = le16(x + k), len = le16(x + k + 2);
            if (k + 4 + len > extraLen) break;
            if (id == 0x0001) {
                const std::uint8_t* q = x + k + 4;
                size_t left = len;
                auto take = [&](std::uint64_t& v) {
                    if (v == 0xFFFFFFFFu && left >= 8) {
                        v = le64(q);
                        q += 8;
                        left -= 8;
                    }
                };
                take(z.size);
                take(z.compSize);
                take(z.localOffset);
            }
            k += 4 + len;
        }
        entries_.push_back(z);
        p += 46 + nameLen + extraLen + commentLen;
    }
    return true;
}

bool ZipReader::extract(const ZipEntry& z, const ZipSink& sink, std::string* error) {
    if (!f_) return fail(error, "zip not open");
    if (z.flags & 1) return fail(error, z.name + ": encrypted");
    if (z.method != 0 && z.method != 8) return fail(error, z.name + ": unsupported compression method " + std::to_string(z.method));
    std::uint8_t lh[30];
    if (!readAt(f_, z.localOffset, lh, 30) || le32(lh) != 0x04034b50u) return fail(error, z.name + ": bad local header");
    const std::uint64_t dataAt = z.localOffset + 30 + le16(lh + 26) + le16(lh + 28);
    if (dataAt > fileSize_ || z.compSize > fileSize_ - dataAt) return fail(error, z.name + ": truncated");
    if (!seekTo(f_, dataAt)) return fail(error, z.name + ": cannot seek");
    std::uint64_t left = z.compSize;
    std::uint32_t crc = 0;
    std::uint64_t produced = 0;
    bool sinkStopped = false;
    ZipSink checked = [&](const std::uint8_t* p, size_t n) {
        crc = crc32Update(crc, p, n);
        produced += n;
        if (!sink(p, n)) {
            sinkStopped = true;
            return false;
        }
        return true;
    };
    std::FILE* f = f_;
    Source src = [&](std::uint8_t* buf, size_t want) -> size_t {
        size_t k = left < want ? static_cast<size_t>(left) : want;
        if (k == 0) return 0;
        size_t got = std::fread(buf, 1, k, f);
        left -= got;
        return got;
    };
    bool ok;
    if (z.method == 0) {
        if (z.compSize != z.size) return fail(error, z.name + ": bad stored size");
        std::vector<std::uint8_t> buf(1 << 16);
        ok = true;
        while (ok && left) {
            size_t n = src(buf.data(), buf.size());
            if (n == 0) break;
            ok = checked(buf.data(), n);
        }
    } else {
        ok = inflateStream(src, checked, z.size, nullptr);
    }
    if (sinkStopped) return fail(error, z.name + ": cannot write");
    if (!ok) return fail(error, z.name + ": corrupt data");
    if (produced != z.size) return fail(error, z.name + ": wrong size");
    if (crc != z.crc) return fail(error, z.name + ": CRC mismatch");
    return true;
}

bool ZipReader::extractToFile(const ZipEntry& z, const std::string& path, std::string* error) {
    std::FILE* out = std::fopen(path.c_str(), "wb");
    if (!out) return fail(error, "cannot create " + path);
    bool ok = extract(z, [&](const std::uint8_t* p, size_t n) { return std::fwrite(p, 1, n, out) == n; }, error);
    if (std::fclose(out) != 0 && ok) ok = fail(error, "cannot write " + path);
    if (!ok) std::remove(path.c_str());
    return ok;
}

bool ZipReader::extractToMemory(const ZipEntry& z, std::vector<std::uint8_t>* out, size_t maxSize, std::string* error) {
    if (z.size > maxSize) return fail(error, z.name + ": too large");
    out->clear();
    out->reserve(static_cast<size_t>(z.size));
    return extract(z, [&](const std::uint8_t* p, size_t n) {
        out->insert(out->end(), p, p + n);
        return true;
    }, error);
}

} // namespace as3d
