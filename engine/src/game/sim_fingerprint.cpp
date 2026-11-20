// The simulation data fingerprint (as3d/sim_fingerprint.h, docs/spec/issues/180-online-coop.md
// 5.3): SHA-256 of the mounted non-media files below a directory (a file at the root is left
// out, see isMedia) in sorted path order, each as the normalised path,
// its NUL, its size (u32, little-endian) and its bytes. Vfs::list gives the paths sorted and
// without duplicates, and a read takes the file of the source that wins, so the pak layout does
// not matter, only what the game reads.
#include <cstring>

#include "as3d/sha256.h"
#include "as3d/sim_fingerprint.h"
#include "as3d/vfs.h"

namespace as3d {

namespace {

// The media types that cannot reach the simulation: textures, sounds, music, fonts and videos.
// Checked against tools/pak_entries/ (the three games' paks hold mdl, tga, scr, wav, obj, hsc,
// ps, mo3, wpn, txt, sc and one max file): anything not listed here stays in the fingerprint.
constexpr const char* kMediaExtensions[] = {
    "tga", "bmp", "jpg", "jpeg", "png", "dds", "wav", "ogg", "mp3", "xm", "it",
    "s3m", "mod", "mo3", "mid", "ttf", "fnt", "avi", "bik",
};

bool isMedia(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    const size_t sep = path.find_last_of("\\/");
    // A file at the root is not the paks' either: the three games' paks hold none (tools/
    // pak_entries/), and the root of an extracted data directory holds the texts file read out
    // of the executable (texts_v170.txt...: menu texts, never read by the simulation). Counting
    // it made a desktop playing from assets_extracted/ refuse a peer playing from the paks (the
    // web, Android, any player's install) with "Your game files differ".
    if (sep == std::string::npos) return true;
    if (dot == std::string::npos || (sep != std::string::npos && dot < sep)) return false;
    std::string ext = path.substr(dot + 1);
    for (char& c : ext)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    for (const char* m : kMediaExtensions)
        if (ext == m) return true;
    return false;
}

int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

} // namespace

std::string simDataFingerprint(Vfs& vfs) {
    Sha256 sha;
    Blob data;
    for (const std::string& path : vfs.list("")) {
        if (isMedia(path)) continue;
        const bool ok = vfs.read(path, data);
        if (!ok) data.clear();
        // An unreadable file counts with the size 0xffffffff (not as an empty one).
        const u32 size = ok ? static_cast<u32>(data.size()) : 0xffffffffu;
        const u8 head[4] = {static_cast<u8>(size), static_cast<u8>(size >> 8), static_cast<u8>(size >> 16),
                            static_cast<u8>(size >> 24)};
        sha.update(path.c_str(), path.size() + 1);
        sha.update(head, 4);
        if (!data.empty()) sha.update(data.data(), data.size());
    }
    return sha.hexDigest();
}

void simDataFingerprintBytes(Vfs& vfs, u8 out[32]) {
    const std::string hex = simDataFingerprint(vfs);
    std::memset(out, 0, 32);
    for (size_t i = 0; i < 32 && 2 * i + 1 < hex.size(); ++i)
        out[i] = static_cast<u8>(hexNibble(hex[2 * i]) << 4 | hexNibble(hex[2 * i + 1]));
}

} // namespace as3d
