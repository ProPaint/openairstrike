// as3d::ArchiveReader (as3d/archive.h): the detector, the libarchive-backed reader (zip, RAR,
// 7z, CAB, tar, ISO 9660, also when appended to a Windows executable), the shared extraction
// helpers and nested containers. The Inno Setup reader is inno_reader.cpp.
#include "as3d/archive.h"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>

#include "archive_internal.h"

namespace as3d {

using archive_internal::le16;
using archive_internal::le32;

namespace {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------------------
// Streams: a window of another stream (a container appended to an executable), bytes in
// memory that the caller keeps (the detector on a buffer).
// ---------------------------------------------------------------------------------------
class SubStream : public IStream {
public:
    SubStream(std::unique_ptr<IStream> base, std::uint64_t offset, std::uint64_t length)
        : base_(std::move(base)), offset_(static_cast<size_t>(offset)), length_(static_cast<size_t>(length)) {}
    size_t size() override { return length_; }
    bool readAt(size_t offset, void* out, size_t n) override {
        if (offset > length_ || n > length_ - offset) return false;
        return base_->readAt(offset_ + offset, out, n);
    }

private:
    std::unique_ptr<IStream> base_;
    size_t offset_, length_;
};

class BytesView : public IStream {
public:
    BytesView(const std::uint8_t* data, size_t n) : data_(data), n_(n) {}
    size_t size() override { return n_; }
    bool readAt(size_t offset, void* out, size_t n) override {
        if (offset > n_ || n > n_ - offset) return false;
        std::memcpy(out, data_ + offset, n);
        return true;
    }

private:
    const std::uint8_t* data_;
    size_t n_;
};

bool startsWith(const std::uint8_t* p, size_t n, const char* sig, size_t len) {
    return n >= len && std::memcmp(p, sig, len) == 0;
}

// The offset of a signature in [p, p + n), or -1.
long findBytes(const std::uint8_t* p, size_t n, const char* sig, size_t len, size_t from = 0) {
    if (n < len) return -1;
    for (size_t i = from; i + len <= n; ++i)
        if (p[i] == static_cast<std::uint8_t>(sig[0]) && std::memcmp(p + i, sig, len) == 0) return static_cast<long>(i);
    return -1;
}

// A container signature at `p` (the first bytes of a plain file, or somewhere in an overlay).
ArchiveKind plainKind(const std::uint8_t* p, size_t n) {
    if (startsWith(p, n, "PK\x03\x04", 4) || startsWith(p, n, "PK\x05\x06", 4) || startsWith(p, n, "PK\x07\x08", 4)) return ArchiveKind::Zip;
    if (startsWith(p, n, "Rar!\x1A\x07", 6)) return ArchiveKind::Rar;
    if (startsWith(p, n, "7z\xBC\xAF\x27\x1C", 6)) return ArchiveKind::SevenZip;
    if (startsWith(p, n, "MSCF", 4)) return ArchiveKind::Cab;
    if (startsWith(p, n, "\x1F\x8B", 2)) return ArchiveKind::Tar; // gzip: a .tar.gz, read through libarchive's gzip filter
    if (startsWith(p, n, "\xFD" "7zXZ\x00", 6)) return ArchiveKind::Tar;  // xz: a .tar.xz
    if (n > 257 + 5 && std::memcmp(p + 257, "ustar", 5) == 0) return ArchiveKind::Tar;
    return ArchiveKind::Unknown;
}

// A CAB header that fits the bytes that follow it: "MSCF", reserved1 0, cbCabinet, coffFiles.
bool plausibleCab(const std::uint8_t* p, size_t n, std::uint64_t remaining) {
    if (n < 36 || std::memcmp(p, "MSCF", 4) != 0) return false;
    const std::uint32_t cbCabinet = le32(p + 8), coffFiles = le32(p + 16);
    return le32(p + 4) == 0 && cbCabinet >= 36 && cbCabinet <= remaining && coffFiles >= 36 && coffFiles < cbCabinet && p[24] <= 1;
}

// The end of the last section of a PE file (where an appended payload starts), 0 if the
// headers do not parse.
std::uint64_t peOverlayStart(IStream& s) {
    std::uint8_t head[64];
    if (s.size() < 64 || !s.readAt(0, head, 64)) return 0;
    const std::uint32_t pe = le32(head + 0x3C);
    std::uint8_t ph[24];
    if (pe + 24 > s.size() || !s.readAt(pe, ph, 24) || std::memcmp(ph, "PE\0\0", 4) != 0) return 0;
    const unsigned sections = le16(ph + 6), optSize = le16(ph + 20);
    if (sections == 0 || sections > 96) return 0;
    const std::uint64_t table = static_cast<std::uint64_t>(pe) + 24 + optSize;
    std::uint64_t end = 0;
    for (unsigned i = 0; i < sections; ++i) {
        std::uint8_t sh[40];
        if (table + 40 * (i + 1) > s.size() || !s.readAt(static_cast<size_t>(table + 40 * i), sh, 40)) return 0;
        const std::uint64_t e = static_cast<std::uint64_t>(le32(sh + 20)) + le32(sh + 16); // PointerToRawData + SizeOfRawData
        end = std::max(end, e);
    }
    return end;
}

// Scans the first `limit` bytes of a stream for a signature, in windows; returns its offset or -1.
long long scanFor(IStream& s, std::uint64_t limit, const char* sig, size_t len, const std::uint8_t* mustPrecede, size_t precedeLen) {
    const size_t window = 1u << 20;
    std::vector<std::uint8_t> buf(window + len + precedeLen);
    const std::uint64_t total = std::min<std::uint64_t>(limit, s.size());
    for (std::uint64_t pos = 0; pos < total; pos += window) {
        const size_t n = static_cast<size_t>(std::min<std::uint64_t>(buf.size(), total - pos));
        if (!s.readAt(static_cast<size_t>(pos), buf.data(), n)) return -1;
        for (long at = findBytes(buf.data(), n, sig, len); at >= 0; at = findBytes(buf.data(), n, sig, len, static_cast<size_t>(at) + 1)) {
            if (static_cast<size_t>(at) >= window) break; // belongs to the next window
            if (precedeLen) {
                std::uint8_t before[8];
                const std::uint64_t abs = pos + static_cast<std::uint64_t>(at);
                if (abs < precedeLen || !s.readAt(static_cast<size_t>(abs - precedeLen), before, precedeLen) ||
                    std::memcmp(before, mustPrecede, precedeLen) != 0)
                    continue;
            }
            return static_cast<long long>(pos + static_cast<std::uint64_t>(at));
        }
    }
    return -1;
}

constexpr std::uint64_t kStubScanLimit = 32u << 20; // installers' stubs are well below this

ArchiveKind detect(IStream& s, bool complete, std::uint64_t* offset) {
    if (offset) *offset = 0;
    const size_t n = s.size();
    std::uint8_t head[0x8006 + 1];
    const size_t headLen = std::min(n, sizeof head);
    if (headLen == 0 || !s.readAt(0, head, headLen)) return ArchiveKind::Unknown;
    const ArchiveKind plain = plainKind(head, headLen);
    if (plain != ArchiveKind::Unknown) return plain;
    if (headLen > 0x8001 + 5 && std::memcmp(head + 0x8001, "CD001", 5) == 0) return ArchiveKind::Iso;
    if (!startsWith(head, headLen, "MZ", 2)) return ArchiveKind::Unknown;

    // A Windows executable: an installer's stub, or a self-extractor with a container appended.
    std::uint64_t o0, o1, tablePos;
    if (archive_internal::findInnoLoaderTable(s, kStubScanLimit, &o0, &o1, &tablePos)) return ArchiveKind::Inno;
    static const std::uint8_t kNsisMagic[4] = {0xEF, 0xBE, 0xAD, 0xDE};
    if (scanFor(s, kStubScanLimit, "NullsoftInst", 12, kNsisMagic, 4) >= 0 || scanFor(s, kStubScanLimit, "NullsoftInst", 12, nullptr, 0) >= 0)
        return ArchiveKind::Nsis;
    if (!complete) return ArchiveKind::Pe;
    const std::uint64_t overlay = peOverlayStart(s);
    if (overlay > 0 && overlay + 36 <= n) {
        std::vector<std::uint8_t> win(static_cast<size_t>(std::min<std::uint64_t>(64u << 10, n - overlay)));
        if (s.readAt(static_cast<size_t>(overlay), win.data(), win.size())) {
            for (size_t i = 0; i + 8 <= win.size(); ++i) {
                ArchiveKind k = ArchiveKind::Unknown;
                if (win[i] == 'M' && plausibleCab(win.data() + i, win.size() - i, n - overlay - i)) k = ArchiveKind::Cab;
                else if (win[i] == '7' && startsWith(win.data() + i, win.size() - i, "7z\xBC\xAF\x27\x1C", 6)) k = ArchiveKind::SevenZip;
                else if (win[i] == 'R' && startsWith(win.data() + i, win.size() - i, "Rar!\x1A\x07", 6)) k = ArchiveKind::Rar;
                if (k != ArchiveKind::Unknown) {
                    if (offset) *offset = overlay + i;
                    return k;
                }
            }
        }
    }
    // A zip appended to an executable: its end record is in the last 64 KB + 22 bytes.
    {
        const size_t tailLen = std::min<size_t>(n, (64u << 10) + 22);
        std::vector<std::uint8_t> tail(tailLen);
        if (tailLen >= 22 && s.readAt(n - tailLen, tail.data(), tailLen)) {
            for (size_t i = tailLen - 22 + 1; i-- > 0;) {
                if (std::memcmp(tail.data() + i, "PK\x05\x06", 4) == 0) return ArchiveKind::Zip;
            }
        }
    }
    return ArchiveKind::Pe;
}

// ---------------------------------------------------------------------------------------
// libarchive over an IStream.
// ---------------------------------------------------------------------------------------
struct Client {
    IStream* stream = nullptr;
    std::uint64_t pos = 0;
    std::vector<std::uint8_t> buf;
};

la_ssize_t clientRead(struct archive*, void* data, const void** buff) {
    Client* c = static_cast<Client*>(data);
    const std::uint64_t size = c->stream->size();
    const size_t n = static_cast<size_t>(std::min<std::uint64_t>(c->buf.size(), c->pos >= size ? 0 : size - c->pos));
    if (n == 0) {
        *buff = c->buf.data();
        return 0;
    }
    if (!c->stream->readAt(static_cast<size_t>(c->pos), c->buf.data(), n)) return -1;
    c->pos += n;
    *buff = c->buf.data();
    return static_cast<la_ssize_t>(n);
}

la_int64_t clientSeek(struct archive*, void* data, la_int64_t offset, int whence) {
    Client* c = static_cast<Client*>(data);
    const la_int64_t size = static_cast<la_int64_t>(c->stream->size());
    la_int64_t target;
    if (whence == SEEK_SET) target = offset;
    else if (whence == SEEK_CUR) target = static_cast<la_int64_t>(c->pos) + offset;
    else target = size + offset;
    if (target < 0 || target > size) return ARCHIVE_FATAL;
    c->pos = static_cast<std::uint64_t>(target);
    return target;
}

la_int64_t clientSkip(struct archive*, void* data, la_int64_t request) {
    Client* c = static_cast<Client*>(data);
    const std::uint64_t size = c->stream->size();
    const std::uint64_t n = std::min<std::uint64_t>(static_cast<std::uint64_t>(request), c->pos >= size ? 0 : size - c->pos);
    c->pos += n;
    return static_cast<la_int64_t>(n);
}

int clientClose(struct archive*, void*) { return ARCHIVE_OK; }

std::string normalisedName(const char* raw) {
    std::string n = raw ? raw : "";
    for (char& ch : n)
        if (ch == '\\') ch = '/';
    while (n.compare(0, 2, "./") == 0) n.erase(0, 2);
    while (!n.empty() && n[0] == '/') n.erase(0, 1);
    return n;
}

std::string laError(struct archive* a, const char* fallback) {
    const char* s = archive_error_string(a);
    return s && *s ? s : fallback;
}

class LibarchiveReader : public ArchiveReader {
public:
    LibarchiveReader(std::unique_ptr<IStream> stream, ArchiveKind kind) : stream_(std::move(stream)), kind_(kind) {}
    ArchiveKind kind() const override { return kind_; }

    bool list(std::string* error) {
        Client c;
        struct archive* a = openArchive(&c, error);
        if (!a) return false;
        struct archive_entry* e;
        int r;
        size_t index = 0;
        while ((r = archive_read_next_header(a, &e)) == ARCHIVE_OK || r == ARCHIVE_WARN) {
            ArchiveEntry out;
            const char* u8 = archive_entry_pathname_utf8(e);
            out.name = normalisedName(u8 ? u8 : archive_entry_pathname(e));
            out.size = archive_entry_size_is_set(e) ? static_cast<std::uint64_t>(archive_entry_size(e)) : 0;
            out.isDir = archive_entry_filetype(e) == AE_IFDIR || (!out.name.empty() && out.name.back() == '/');
            out.index = index++;
            entries_.push_back(out);
        }
        if (r != ARCHIVE_EOF) {
            if (error) *error = laError(a, "cannot read the archive's entries");
            archive_read_free(a);
            return false;
        }
        archive_read_free(a);
        return true;
    }

    bool extract(const ArchiveEntry& e, const ArchiveSink& sink, std::string* error) override {
        if (e.index >= entries_.size()) {
            if (error) *error = "no such entry";
            return false;
        }
        Client c;
        struct archive* a = openArchive(&c, error);
        if (!a) return false;
        struct archive_entry* ae = nullptr;
        bool found = false;
        for (size_t i = 0; i <= e.index; ++i) {
            const int r = archive_read_next_header(a, &ae);
            if (r != ARCHIVE_OK && r != ARCHIVE_WARN) {
                if (error) *error = laError(a, "cannot reach the entry");
                archive_read_free(a);
                return false;
            }
            found = i == e.index;
        }
        if (!found) {
            if (error) *error = "no such entry";
            archive_read_free(a);
            return false;
        }
        std::vector<std::uint8_t> buf(256u << 10);
        std::uint64_t total = 0;
        for (;;) {
            const la_ssize_t n = archive_read_data(a, buf.data(), buf.size());
            if (n == 0) break;
            if (n < 0) {
                if (error) *error = laError(a, "cannot read the entry's data");
                archive_read_free(a);
                return false;
            }
            total += static_cast<std::uint64_t>(n);
            if (!sink(buf.data(), static_cast<size_t>(n))) {
                if (error) *error = "stopped";
                archive_read_free(a);
                return false;
            }
        }
        archive_read_free(a);
        if (e.size != 0 && total != e.size) {
            if (error) *error = "truncated: " + std::to_string(e.size - total) + " bytes short";
            return false;
        }
        return true;
    }

private:
    struct archive* openArchive(Client* c, std::string* error) {
        struct archive* a = archive_read_new();
        if (!a) {
            if (error) *error = "out of memory";
            return nullptr;
        }
        archive_read_support_filter_none(a);
        archive_read_support_filter_gzip(a);
        archive_read_support_filter_xz(a);
        switch (kind_) {
        case ArchiveKind::Zip: archive_read_support_format_zip(a); break;
        case ArchiveKind::Rar:
            archive_read_support_format_rar(a);
            archive_read_support_format_rar5(a);
            break;
        case ArchiveKind::SevenZip: archive_read_support_format_7zip(a); break;
        case ArchiveKind::Cab: archive_read_support_format_cab(a); break;
        case ArchiveKind::Tar: archive_read_support_format_tar(a); break;
        case ArchiveKind::Iso: archive_read_support_format_iso9660(a); break;
        default: break;
        }
        c->stream = stream_.get();
        c->pos = 0;
        c->buf.resize(256u << 10);
        archive_read_set_read_callback(a, clientRead);
        archive_read_set_seek_callback(a, clientSeek);
        archive_read_set_skip_callback(a, clientSkip);
        archive_read_set_close_callback(a, clientClose);
        archive_read_set_callback_data(a, c);
        if (archive_read_open1(a) != ARCHIVE_OK) {
            if (error) *error = laError(a, "cannot open the archive");
            archive_read_free(a);
            return nullptr;
        }
        return a;
    }

    std::unique_ptr<IStream> stream_;
    ArchiveKind kind_;
};

int g_tempCounter = 0;

} // namespace

// ---------------------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------------------
const char* archiveKindName(ArchiveKind kind) {
    switch (kind) {
    case ArchiveKind::Zip: return "zip";
    case ArchiveKind::Rar: return "rar";
    case ArchiveKind::SevenZip: return "7z";
    case ArchiveKind::Cab: return "cab";
    case ArchiveKind::Tar: return "tar";
    case ArchiveKind::Iso: return "iso";
    case ArchiveKind::Inno: return "inno";
    case ArchiveKind::Nsis: return "nsis";
    case ArchiveKind::Pe: return "pe";
    default: return "unknown";
    }
}

ArchiveKind archiveKind(IStream& stream, std::uint64_t* offset) { return detect(stream, true, offset); }

ArchiveKind archiveKind(const std::string& path, std::uint64_t* offset) {
    std::unique_ptr<IStream> s = openFileStream(path);
    if (!s) {
        if (offset) *offset = 0;
        return ArchiveKind::Unknown;
    }
    return detect(*s, true, offset);
}

ArchiveKind archiveKind(const std::uint8_t* data, size_t n, bool complete, std::uint64_t* offset) {
    BytesView v(data, n);
    return detect(v, complete, offset);
}

ArchiveReader::~ArchiveReader() { removeTempFile(); }

void ArchiveReader::removeTempFile() {
    if (tempFile_.empty()) return;
    std::error_code ec;
    fs::remove(fs::path(tempFile_), ec);
    tempFile_.clear();
}

std::unique_ptr<ArchiveReader> ArchiveReader::open(const std::string& path, std::string* error) {
    std::unique_ptr<IStream> s = openFileStream(path);
    if (!s) {
        if (error) *error = "cannot open the file";
        return nullptr;
    }
    std::string name = path;
    const size_t slash = name.find_last_of('/');
    if (slash != std::string::npos) name = name.substr(slash + 1);
    return open(std::move(s), name, error);
}

std::unique_ptr<ArchiveReader> ArchiveReader::open(std::unique_ptr<IStream> stream, const std::string& name, std::string* error) {
    if (!stream) {
        if (error) *error = "no data";
        return nullptr;
    }
    std::uint64_t offset = 0;
    const ArchiveKind kind = detect(*stream, true, &offset);
    switch (kind) {
    case ArchiveKind::Inno: {
        std::unique_ptr<ArchiveReader> r = archive_internal::openInno(std::move(stream), name, error);
        return r;
    }
    case ArchiveKind::Nsis:
        if (error) *error = "an NSIS installer, which this app cannot open; install it on a PC and pick the game's data folder";
        return nullptr;
    case ArchiveKind::Pe:
        if (error) *error = "a Windows program, not an installer or archive this app can open";
        return nullptr;
    case ArchiveKind::Unknown:
        if (error) *error = "not an archive this app can open (zip, rar, 7z, cab, tar, iso, or an Inno Setup installer)";
        return nullptr;
    default: break;
    }
    if (offset > 0) {
        const std::uint64_t total = stream->size();
        stream.reset(new SubStream(std::move(stream), offset, total - offset));
    }
    std::unique_ptr<LibarchiveReader> r(new LibarchiveReader(std::move(stream), kind));
    r->name_ = name;
    if (!r->list(error)) return nullptr;
    return r;
}

bool ArchiveReader::extractToMemory(const ArchiveEntry& e, std::vector<std::uint8_t>* out, size_t maxSize, std::string* error) {
    out->clear();
    if (e.size > maxSize) {
        if (error) *error = "larger than " + std::to_string(maxSize) + " bytes";
        return false;
    }
    bool tooBig = false;
    const bool ok = extract(
        e,
        [&](const std::uint8_t* data, size_t n) {
            if (out->size() + n > maxSize) {
                tooBig = true;
                return false;
            }
            out->insert(out->end(), data, data + n);
            return true;
        },
        error);
    if (tooBig && error) *error = "larger than " + std::to_string(maxSize) + " bytes";
    if (!ok) out->clear();
    return ok;
}

bool ArchiveReader::extractToFile(const ArchiveEntry& e, const std::string& path, std::string* error) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        if (error) *error = "cannot write " + path;
        return false;
    }
    bool writeFailed = false;
    const bool ok = extract(
        e,
        [&](const std::uint8_t* data, size_t n) {
            if (std::fwrite(data, 1, n, f) != n) {
                writeFailed = true;
                return false;
            }
            return true;
        },
        error);
    const bool closed = std::fclose(f) == 0;
    if (!ok || writeFailed || !closed) {
        if (error && (writeFailed || !closed)) *error = "cannot write " + path;
        std::error_code ec;
        fs::remove(fs::path(path), ec);
        return false;
    }
    return true;
}

ArchiveKind ArchiveReader::entryKind(const ArchiveEntry& e) {
    if (e.isDir) return ArchiveKind::Unknown;
    std::vector<std::uint8_t> head;
    const size_t want = std::min<std::uint64_t>(e.size ? e.size : (64u << 10), 64u << 10);
    extract(
        e,
        [&](const std::uint8_t* data, size_t n) {
            head.insert(head.end(), data, data + n);
            return head.size() < want;
        },
        nullptr);
    if (head.empty()) return ArchiveKind::Unknown;
    return archiveKind(head.data(), head.size(), head.size() == e.size, nullptr);
}

std::unique_ptr<ArchiveReader> ArchiveReader::openEntry(const ArchiveEntry& e, std::string* error, const std::string& tempDir) {
    const std::string nested = name_ + ": " + e.name;
    if (e.size <= kNestedMemoryLimit) {
        std::vector<std::uint8_t> bytes;
        if (!extractToMemory(e, &bytes, static_cast<size_t>(kNestedMemoryLimit), error)) return nullptr;
        return open(makeMemoryStream(std::move(bytes)), nested, error);
    }
    std::error_code ec;
    fs::path dir = tempDir.empty() ? fs::temp_directory_path(ec) : fs::path(tempDir);
    if (ec) dir = fs::path(".");
    const fs::path tmp = dir / ("as3d-nested-" + std::to_string(++g_tempCounter) + "-" + std::to_string(e.index) + ".bin");
    if (!extractToFile(e, tmp.string(), error)) return nullptr;
    std::unique_ptr<IStream> s = openFileStream(tmp.string());
    if (!s) {
        if (error) *error = "cannot reopen " + tmp.string();
        fs::remove(tmp, ec);
        return nullptr;
    }
    std::unique_ptr<ArchiveReader> r = open(std::move(s), nested, error);
    if (!r) {
        fs::remove(tmp, ec);
        return nullptr;
    }
    r->tempFile_ = tmp.string();
    return r;
}

} // namespace as3d
