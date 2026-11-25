// The web page's 3DS music (docs/web.md "Files for the 3DS"): a small WebAssembly module of its
// own (as3d_music.js/.wasm), loaded only when the player asks for the 3DS files, in a Web Worker
// (site/music_worker.js), never by the game. It renders the music of a game's paks as the 3DS
// plays it, with the code of audio_tool render-music (as3d/music_prerender.h) and the same
// encoder (as3d/ogg_vorbis_encoder.h, libvorbis), so the files are those of the desktop tool.
//
// Calls from the worker (ccall):
//   as3d_music_jobs(paks)          `paks`: the pak files' paths, one per line, in mount order (a
//                                  later pak wins). Mounts them and returns the files to make
//                                  as JSON: [{"module": "music\\track01.mo3", "file":
//                                  "track01.ogg", "order": 0}, ...] (as3d::audio::prerenderJobs).
//   as3d_music_render(i, outPath)  makes file i of that list into `outPath` (MEMFS) and returns
//                                  {"ok": true, "frames": N, "rate": R, "seconds": S, "peak": P,
//                                  "clipped": C, "bytes": B} or {"ok": false, "error": "..."}.
//                                  Progress goes to Module.onMusicProgress(fraction) about once
//                                  per second of audio.
#include <emscripten.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/music_prerender.h"
#include "as3d/ogg_vorbis_encoder.h"
#include "as3d/vfs.h"

namespace {

std::unique_ptr<as3d::Vfs> g_vfs;
std::vector<as3d::audio::PrerenderJob> g_jobs;
std::string g_result;  // the last JSON returned (valid until the next call)

std::string jsonString(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", c);
            out += buf;
        } else {
            out += c;
        }
    }
    return out + "\"";
}

const char* fail(const std::string& error) {
    g_result = "{\"ok\":false,\"error\":" + jsonString(error) + "}";
    return g_result.c_str();
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE const char* as3d_music_jobs(const char* paks) {
    g_vfs = std::make_unique<as3d::Vfs>();
    g_jobs.clear();
    std::string list = paks ? paks : "";
    size_t pos = 0;
    int mounted = 0;
    while (pos <= list.size()) {
        const size_t nl = list.find('\n', pos);
        const std::string path = list.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? list.size() + 1 : nl + 1;
        if (path.empty()) continue;
        auto stream = as3d::openFileStream(path);
        auto source = stream ? as3d::makePakSource(std::move(stream)) : nullptr;
        if (!source) {
            AS3D_WARN("music: cannot open the pak %s", path.c_str());
            continue;
        }
        g_vfs->mount(std::move(source));
        ++mounted;
    }
    if (mounted) g_jobs = as3d::audio::prerenderJobs(*g_vfs);
    std::string out = "[";
    for (size_t i = 0; i < g_jobs.size(); ++i) {
        if (i) out += ",";
        out += "{\"module\":" + jsonString(g_jobs[i].module) + ",\"file\":" + jsonString(g_jobs[i].file) +
               ",\"order\":" + std::to_string(g_jobs[i].order) + "}";
    }
    g_result = out + "]";
    return g_result.c_str();
}

EMSCRIPTEN_KEEPALIVE const char* as3d_music_render(int index, const char* outPath) {
    if (!g_vfs || index < 0 || static_cast<size_t>(index) >= g_jobs.size()) return fail("no such file");
    const as3d::audio::PrerenderJob& job = g_jobs[static_cast<size_t>(index)];
    as3d::Blob data;
    if (!g_vfs->read(job.module, data)) return fail("cannot read " + job.module);
    const int rate = as3d::audio::kPrerenderedMusicRate;
    as3d::audio::OggVorbisEncoder enc;
    if (!enc.begin(2, rate, as3d::audio::kPrerenderedMusicQuality)) return fail(enc.error());
    as3d::audio::RenderStats stats;
    std::string error;
    const bool ok = as3d::audio::renderModule(
        data, rate, job.order, [&](const std::int16_t* frames, size_t n) { return enc.write(frames, n); }, stats,
        error, [](double f) {
            EM_ASM({ if (Module.onMusicProgress) Module.onMusicProgress($0); }, f);
            return true;
        });
    if (!ok) return fail(job.module + ": " + error);
    if (!enc.finish()) return fail(enc.error());
    FILE* f = std::fopen(outPath, "wb");
    if (!f) return fail(std::string("cannot write ") + outPath);
    const bool written = std::fwrite(enc.bytes().data(), 1, enc.bytes().size(), f) == enc.bytes().size();
    std::fclose(f);
    if (!written) return fail(std::string("cannot write ") + outPath);
    char buf[256];
    std::snprintf(buf, sizeof buf,
                  "{\"ok\":true,\"frames\":%zu,\"rate\":%d,\"seconds\":%.3f,\"peak\":%.4f,\"clipped\":%zu,\"bytes\":%zu}",
                  stats.frames, rate, static_cast<double>(stats.frames) / rate, static_cast<double>(stats.peak),
                  stats.clipped, enc.bytes().size());
    g_result = buf;
    return g_result.c_str();
}

}  // extern "C"
