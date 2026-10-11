// The platform-neutral parts of the 3DS soak and log push (apps/3ds/source/soak_report.h,
// docs/3ds-port.md "Soak mode and the log push (round 2, P7)"): the stage list of run.ini soak=,
// report_to=, the file names the push may carry, the exact bytes of the framing that
// tools/3ds_receive.py reads (its own self-test, tools/ref/test_3ds_receive.py, parses the same
// stream), and the soak.txt verdict.
#include "doctest.h"

#include <string>
#include <utility>
#include <vector>

#include "../3ds/source/soak_report.h"

using namespace ctrapp;

TEST_CASE("soak: the stage list of run.ini soak=") {
    std::vector<SoakStage> s;
    REQUIRE(parseSoakList("as3d:1:mono, as3d:1:stereo,as3d:2:mono,gulf:1:stereo", &s));
    REQUIRE(s.size() == 4);
    CHECK(s[0].game == "as3d");
    CHECK(s[0].level == 1);
    CHECK_FALSE(s[0].stereo);
    CHECK(s[1].stereo);
    CHECK(s[3].game == "gulf");
    CHECK(soakStageName(s[3]) == "gulf:1:stereo");
    std::vector<SoakStage> keep = s;
    CHECK_FALSE(parseSoakList("as3d:1", &s));
    CHECK_FALSE(parseSoakList("as3d:0:mono", &s));
    CHECK_FALSE(parseSoakList("as3d:1:left", &s));
    CHECK_FALSE(parseSoakList("", &s));
    CHECK(s.size() == keep.size()); // untouched on failure

    const std::vector<SoakStage> d = defaultSoakStages({"as3d", "gulf"});
    REQUIRE(d.size() == 8);
    CHECK(soakStageName(d[0]) == "as3d:1:mono");
    CHECK(soakStageName(d[1]) == "as3d:1:stereo");
    CHECK(soakStageName(d[2]) == "as3d:2:mono");
    CHECK(soakStageName(d[7]) == "gulf:2:stereo");
}

TEST_CASE("soak: report_to and the file names of the push") {
    std::string host;
    int port = 0;
    REQUIRE(parseHostPort("192.168.1.20:7777", &host, &port));
    CHECK(host == "192.168.1.20");
    CHECK(port == 7777);
    CHECK_FALSE(parseHostPort("192.168.1.20", &host, &port));
    CHECK_FALSE(parseHostPort("192.168.1.20:0", &host, &port));
    CHECK_FALSE(parseHostPort("192.168.1.20:70000", &host, &port));
    CHECK_FALSE(parseHostPort("pc.local:7777", &host, &port));
    CHECK_FALSE(parseHostPort("1.2.3.256:7777", &host, &port));

    CHECK(validReportName("log.txt"));
    CHECK(validReportName("mark01_top.ppm"));
    CHECK_FALSE(validReportName(""));
    CHECK_FALSE(validReportName(".."));
    CHECK_FALSE(validReportName("../run.ini"));
    CHECK_FALSE(validReportName("a/b.txt"));
    CHECK_FALSE(validReportName("a\\b.txt"));
    CHECK_FALSE(validReportName(std::string("a\nb")));
    CHECK_FALSE(validReportName(std::string(65, 'x')));

    CHECK(isReportFile("log.txt"));
    CHECK(isReportFile("perf.csv"));
    CHECK(isReportFile("soak.txt"));
    CHECK(isReportFile("mark03_depth.ppm"));
    CHECK(isReportFile("fatal_top.ppm"));
    CHECK_FALSE(isReportFile("run.ini"));
    CHECK_FALSE(isReportFile("profile.bin"));
    CHECK_FALSE(isReportFile("top_left.ppm"));
    CHECK_FALSE(isReportFile("mark01_top.txt"));
}

TEST_CASE("soak: the framing of the push, byte for byte") {
    const std::string stream = frameReport({{"soak.txt", "VERDICT PASS\n"}, {"bad/name", "x"}, {"empty.ppm", ""},
                                            {"log.txt", std::string("a\0b", 3)}});
    const std::string expected = std::string("AS3D1\n") + "soak.txt\n13\nVERDICT PASS\n" + "empty.ppm\n0\n" + "log.txt\n3\n" +
                                 std::string("a\0b", 3) + "END\n";
    CHECK(stream == expected);
    CHECK(reportFileHeader("perf.csv", 123456789012ull) == "perf.csv\n123456789012\n");
    CHECK(frameReport({}) == "AS3D1\nEND\n");
}

namespace {
SoakStageResult goodStage(const char* game, int level, bool stereo) {
    SoakStageResult r;
    r.stage = SoakStage{game, level, stereo};
    r.ran = true;
    r.finished = true;
    r.frames = stereo ? 900 : 1800;
    r.steps = r.targetSteps = 1800;
    r.hzEnd = stereo ? 30 : 60;
    r.graceFrames = stereo ? 840 : 1500;
    r.graceAt60 = stereo ? 0 : 1500;
    r.graceAt30 = stereo ? 840 : 0;
    r.gpuAvg = 11.5;
    r.gpuP95 = 13.0;
    r.probes = 60;
    return r;
}
bool hasLine(const std::string& text, const std::string& start) {
    return text.find("\n" + start) != std::string::npos || text.compare(0, start.size(), start) == 0;
}
} // namespace

TEST_CASE("soak: the verdict of soak.txt") {
    std::vector<SoakStageResult> r = {goodStage("as3d", 1, false), goodStage("as3d", 1, true)};
    SoakStageResult skipped;
    skipped.stage = SoakStage{"gulf", 1, false};
    skipped.skipped = "(not installed)";
    r.push_back(skipped);
    std::string v = soakVerdict(r, {});
    CHECK(hasLine(v, "PASS wash terrain_gpu_mismatches=0"));
    CHECK(hasLine(v, "PASS mono60 as3d:1:mono at60=100.0%"));
    CHECK(hasLine(v, "PASS stereo as3d:1:stereo settled=30Hz"));
    CHECK(hasLine(v, "PASS lazy_loads total=0 (TerraMorph stamps, not precachable: 0)"));
    CHECK(hasLine(v, "PASS fence AS3D_FENCE=0"));
    CHECK(hasLine(v, "PASS gpu_busy AS3D_GPU_BUSY=0"));
    CHECK(hasLine(v, "PASS tex_in_frame AS3D_TEX_IN_FRAME=0"));
    CHECK(hasLine(v, "PASS stages_finished 2 of 2"));
    CHECK(hasLine(v, "INFO skipped gulf:1:mono (not installed)"));
    CHECK(hasLine(v, "VERDICT PASS"));

    // The wash, mono at 30 Hz for a while, a lazy load, an unfinished stage, a crashed launch.
    r[0].terrainGpuMismatches = 2;
    r[0].gpuMismatches = 3;
    r[0].graceAt60 = 1400;
    r[0].graceAt30 = 100;
    r[1].lazyLoads = 1;
    r[1].finished = false;
    v = soakVerdict(r, {"the previous launch stopped inside a stage (stage 3 of 8)"});
    CHECK(hasLine(v, "FAIL note the previous launch stopped inside a stage"));
    CHECK(hasLine(v, "FAIL wash terrain_gpu_mismatches=2 gpu_mismatches=3"));
    CHECK(hasLine(v, "FAIL mono60 as3d:1:mono at60=93.3%"));
    CHECK(hasLine(v, "FAIL lazy_loads total=1"));
    CHECK(hasLine(v, "FAIL stages_finished 1 of 2 unfinished: as3d:1:stereo"));
    CHECK(hasLine(v, "VERDICT FAIL"));

    // Stereo fails only when it could not hold 30 Hz; no probe at all means the wash check did not run.
    std::vector<SoakStageResult> s = {goodStage("as2", 2, true)};
    s[0].misses30 = 100;
    s[0].probes = 0;
    v = soakVerdict(s, {});
    CHECK(hasLine(v, "FAIL stereo as2:2:stereo"));
    CHECK(hasLine(v, "FAIL wash terrain_gpu_mismatches=0 gpu_mismatches=0 probes=0"));
    // The detector's judged frames count as a run check; a washed episode fails the stage.
    s[0].washEvals = 1000;
    v = soakVerdict(s, {});
    CHECK(hasLine(v, "PASS wash terrain_gpu_mismatches=0 gpu_mismatches=0 probes=0 marks=0 wash_frames=0"));
    s[0].washEpisodes = 2;
    s[0].washFrames = 7;
    s[0].washLongest = 5;
    s[0].washFirstFrame = 4321;
    s[0].washMaxSamples = 40;
    v = soakVerdict(s, {});
    CHECK(hasLine(v, "FAIL wash terrain_gpu_mismatches=0 gpu_mismatches=0 probes=0 marks=0 wash_frames=7 wash_episodes=2"));
    CHECK(hasLine(v, "FAIL wash as2:2:stereo first_frame=4321 episodes=2 frames=7 longest=5 max_samples=40"));

    // A stage shorter than the pacer's grace: no rate to judge.
    s = {goodStage("as3d", 1, false)};
    s[0].graceFrames = s[0].graceAt60 = 0;
    v = soakVerdict(s, {});
    CHECK(hasLine(v, "INFO mono60 as3d:1:mono no frames after the pacer's grace"));
    CHECK(hasLine(v, "VERDICT PASS"));
}
