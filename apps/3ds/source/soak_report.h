// The platform-neutral parts of run.ini mode=soak and report_to= (docs/3ds-port.md "Soak mode and
// the log push (round 2, P7)"): the stage list, the soak.txt verdict and the framing of the log
// push. No 3DS API here: the desktop tests (apps/tests/soak_report_test.cpp) compile this file.
//
// The push's framing, what tools/3ds_receive.py reads:
//   "AS3D1\n"                                   the magic
//   per file: "<name>\n<size in decimal>\n" then exactly <size> bytes
//   "END\n"
// A name is a plain file name of the data directory: no slash, no backslash, not "." or "..",
// at most 64 characters, no control characters (validReportName).
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ctrapp {

// One stage of the soak: a bot run of `level` of `game` in mono or stereo (slider forced to 0.5),
// with run.ini key=value overrides for this stage only (R3.3: the A/B stages of one launch).
struct SoakStage {
    std::string game; // a profile key: as3d, as2, gulf
    int level = 1;
    bool stereo = false;
    std::vector<std::pair<std::string, std::string>> options; // in the list's order
};

// "as3d:1:mono,as3d:1:stereo:eye_replay=0:soak_frames=3600,gulf:2:stereo": false (and *out
// untouched) on a malformed entry. An option is key=value, the key [a-z0-9_]+ and not one the
// stage itself sets (mode, game, level, slider, soak, report_to), the value without spaces,
// commas, colons or '='.
bool parseSoakList(const std::string& text, std::vector<SoakStage>* out);
// The overrides as run.ini lines ("key=value\n" each).
std::string soakStageOptionsText(const SoakStage& s);
// "eye_replay=0:soak_frames=3600", or "none".
std::string soakStageOptions(const SoakStage& s);
// The default stages: for each game key, mission 1 and 2, each in mono then stereo.
std::vector<SoakStage> defaultSoakStages(const std::vector<std::string>& gameKeys);
std::string soakStageName(const SoakStage& s); // "as3d:1:mono", "as3d:1:stereo:eye_replay=0"

// What a stage measured (ctr_soak.cpp fills it from the loop's report).
struct SoakStageResult {
    SoakStage stage;
    bool ran = false;          // the stage was started (false: skipped or aborted before it)
    bool finished = false;     // it reached its last step (a whole-mission stage: the level's end)
    bool levelEnded = false;   // a whole-mission stage reached Mission Complete / Game Over
    bool gameOver = false;     // ... Game Over (the bot died: less of the mission was covered)
    unsigned levelEndStep = 0; // the step it happened at
    std::string skipped;       // why it did not run ("not installed", "aborted")
    unsigned frames = 0, steps = 0, targetSteps = 0;
    int hzEnd = 0;             // the pacer's rate at the end
    unsigned graceFrames = 0;  // frames after the pacer grace (the start's 300)
    unsigned graceAt60 = 0, graceAt30 = 0; // of those, drawn at 60 / 30 Hz
    unsigned misses60 = 0, misses30 = 0, excused60 = 0;
    unsigned paceChanges = 0;
    // R4.2 (ctr_loop.h PaceStats): frames at 60 Hz shown late by their GPU, stutters at 60 Hz,
    // frames that ended with simulation steps owed (the deferred catch-up).
    unsigned gpuLate60 = 0, hitches60 = 0, behindFrames = 0;
    double gpuAvg = 0.0, gpuP95 = 0.0;
    long long lazyLoads = 0;   // first-use loads after the precache, stamps excepted
    long long lazyStamps = 0;  // the sequels' TerraMorph stamps: not precachable (as3d/load_stats.h), reported only
    unsigned gpuMismatches = 0;        // AS3D_GPU_MISMATCH, every pass
    unsigned terrainGpuMismatches = 0; // of those, pass=terrain (the water wash signature)
    unsigned stateMismatches = 0;      // AS3D_STATE_MISMATCH (the CPU layers)
    unsigned probes = 0, autoMarks = 0;
    unsigned primeGpuMismatches = 0;   // pass=terrain_prime: the terrain's first state block lost (the priming draw takes it)
    // The wash detector (docs/3ds-port.md "Wash detector"): eye-frames judged, washed frames,
    // episodes (runs of washed frames), the longest, the first one's frame, the most washed samples.
    unsigned washEvals = 0, washFrames = 0, washEpisodes = 0, washLongest = 0, washFirstFrame = 0, washMaxSamples = 0;
    // R3.3 (docs/3ds-port.md "Third console soak"): probes with a mismatch, of those whose second
    // read differed from the first, the canary read back (all probes) and missing on a mismatch.
    unsigned mismatchProbes = 0, rereadChanged = 0, canaryOk = 0, canaryStale = 0;
    unsigned fence = 0;        // AS3D_FENCE (the GX queue running at frameBegin)
    unsigned gpuBusy = 0;      // AS3D_GPU_BUSY frames
    unsigned texInFrame = 0;   // AS3D_TEX_IN_FRAME (textures created inside a frame)
};

// Thresholds of the verdict.
constexpr double kSoakMono60Pct = 97.0;     // mono: frames at 60 Hz after the grace
constexpr double kSoakStereo30MissPct = 3.0; // stereo fails only when it cannot hold 30 Hz

// The soak.txt text: one line per check, "PASS name value" or "FAIL name value", then
// "VERDICT PASS|FAIL". `notes` are extra FAIL lines (a crashed previous launch, an abort).
std::string soakVerdict(const std::vector<SoakStageResult>& results, const std::vector<std::string>& failNotes);

// report_to=IP:PORT: false when malformed (the port must be 1..65535, the host a dotted IPv4).
bool parseHostPort(const std::string& text, std::string* host, int* port);
// A file name the push may carry (see the header).
bool validReportName(const std::string& name);
// The framing pieces.
constexpr const char* kReportMagic = "AS3D1\n";
constexpr const char* kReportEnd = "END\n";
std::string reportFileHeader(const std::string& name, uint64_t size);
// The whole stream for in-memory files (the desktop test, and small files); invalid names are skipped.
std::string frameReport(const std::vector<std::pair<std::string, std::string>>& files);
// Whether a data-directory file is one the push sends: log.txt, perf.csv, soak.txt, mark*.ppm, fatal*.ppm.
bool isReportFile(const std::string& name);

} // namespace ctrapp
