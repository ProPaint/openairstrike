// The 3DS's pre-rendered music (docs/3ds-port.md "M2.4 Music"): each tracker module of a game's
// music\ played once (no loop) at the 3DS DSP's rate, plus its part from the game-over order,
// as the Ogg Vorbis files sdmc:/3ds/openairstrike/<game>/music/<name>.ogg and
// <name>_gameover.ogg. Shared by audio_tool render-music (desktop) and the web page's "Prepare
// files for 3DS" (apps/web/music_main.cpp), so both make the same files. The output is derived
// from the game's data: never commit it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "as3d/core.h"

namespace as3d {
class Vfs;
}

namespace as3d::audio {

constexpr int kPrerenderedMusicRate = 32728;          // the 3DS DSP's rate: no resampling there
constexpr double kPrerenderedMusicQuality = 4.0;      // oggenc's -q scale (-1..10), about 100 kbps
constexpr double kPrerenderedMusicMaxSeconds = 20.0 * 60.0;

// One file to make: `module` (a VFS path, music\trackNN.mo3) from pattern order `order` to the
// module's end, as `file` (trackNN.ogg or trackNN_gameover.ogg).
struct PrerenderJob {
    std::string module;
    std::string file;
    int order = 0;
};

// The files of every module of music\ (.mo3, .it, .xm, .s3m, .mod), in the VFS's listing order:
// <name>.ogg from order 0 and, when the module has the game-over order (kGameOverMusicOrder),
// <name>_gameover.ogg from it. Names are lower case. A module that does not load still gets its
// <name>.ogg job (rendering it reports the error).
std::vector<PrerenderJob> prerenderJobs(Vfs& vfs);

// Whether a path names a tracker module the music code plays (by extension).
bool isTrackerModulePath(const std::string& path);

struct RenderStats {
    std::size_t frames = 0;   // stereo frames written (trailing digital silence left out)
    float peak = 0.0f;        // the largest absolute sample before clamping
    std::size_t clipped = 0;  // samples above full scale (clamped)
};

// Renders `module` from `order` to its end (no loop), at `rate`, as 16-bit interleaved stereo:
// each float sample clamped to [-1, 1] and rounded from s * 32767 (as a 16-bit WAV of it).
// The frames are handed to `sink` as they are made (any count per call), except the digital
// silence at the very end. `progress` (optional) gets the fraction done (by the module's
// duration from order 0, so a rough figure) about every second of audio; returning false from
// either stops the render (an error "stopped"). False with `error` set on any failure.
bool renderModule(const Blob& module, int rate, int order,
                  const std::function<bool(const std::int16_t* interleaved, std::size_t frames)>& sink,
                  RenderStats& stats, std::string& error, const std::function<bool(double)>& progress = {});

}  // namespace as3d::audio
