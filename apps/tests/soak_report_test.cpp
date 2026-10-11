// The platform-neutral parts of the 3DS soak and log push (apps/3ds/source/soak_report.h,
// docs/3ds-port.md "Soak mode and the log push (round 2, P7)"): the stage list of run.ini soak=,
// report_to=, the file names the push may carry, the exact bytes of the framing that
// tools/3ds_receive.py reads (its own self-test, tools/ref/test_3ds_receive.py, parses the same
// stream), and the soak.txt verdict.
#include "doctest.h"

#include <string>
#include <utility>
#include <vector>

#include "../3ds/source/catch_up.h"
#include "../3ds/source/pace_rule.h"
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
    CHECK(soakStageOptions(d[0]) == "none");
}

TEST_CASE("soak: per-stage run.ini overrides (R3.3)") {
    std::vector<SoakStage> s;
    REQUIRE(parseSoakList("as3d:1:mono,as3d:1:stereo:eye_replay=0:soak_frames=3600, as3d:1:mono:wash_check=0", &s));
    REQUIRE(s.size() == 3);
    CHECK(s[0].options.empty());
    REQUIRE(s[1].options.size() == 2);
    CHECK(s[1].options[0].first == "eye_replay");
    CHECK(s[1].options[0].second == "0");
    CHECK(s[1].options[1].first == "soak_frames");
    CHECK(s[1].options[1].second == "3600");
    CHECK(soakStageName(s[1]) == "as3d:1:stereo:eye_replay=0:soak_frames=3600");
    CHECK(soakStageOptions(s[1]) == "eye_replay=0:soak_frames=3600");
    CHECK(soakStageOptionsText(s[1]) == "eye_replay=0\nsoak_frames=3600\n");
    CHECK(soakStageName(s[2]) == "as3d:1:mono:wash_check=0");
    const std::vector<SoakStage> keep = s;
    CHECK_FALSE(parseSoakList("as3d:1:stereo:eye_replay", &s));      // no value
    CHECK_FALSE(parseSoakList("as3d:1:stereo:=0", &s));              // no key
    CHECK_FALSE(parseSoakList("as3d:1:stereo:Eye=0", &s));           // not a run.ini key's spelling
    CHECK_FALSE(parseSoakList("as3d:1:stereo:level=2", &s));         // the stage's own keys
    CHECK_FALSE(parseSoakList("as3d:1:stereo:report_to=1.2.3.4", &s));
    CHECK(s.size() == keep.size());
    CHECK(soakStageName(s[1]) == soakStageName(keep[1]));
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

TEST_CASE("pace: the drop rules of run.ini pace_rule= (R3.3)") {
    PaceRule r;
    CHECK(r.name() == "24/120"); // the default
    REQUIRE(r.parse("12"));
    CHECK(r.name() == "12/120");
    CHECK_FALSE(r.parse("0"));
    CHECK_FALSE(r.parse("121"));
    CHECK_FALSE(r.parse("abc"));
    CHECK(r.name() == "12/120"); // untouched
    PaceDropState s;
    int dropAt = -1;
    for (int i = 0; i < 240 && dropAt < 0; ++i)
        if (s.frame(r, i % 10 == 0)) dropAt = i; // one miss in ten: the 12th at frame 110
    CHECK(dropAt == 110);
    s.reset();
    REQUIRE(r.parse("24"));
    bool dropped = false;
    for (int i = 0; i < 600; ++i) dropped = dropped || s.frame(r, i % 10 == 0); // 10%: never 24 in 120
    CHECK_FALSE(dropped);
    REQUIRE(r.parse("run3"));
    s.reset();
    CHECK_FALSE(s.frame(r, true));
    CHECK_FALSE(s.frame(r, true));
    CHECK_FALSE(s.frame(r, false));
    CHECK_FALSE(s.frame(r, true));
    CHECK_FALSE(s.frame(r, true));
    CHECK(s.frame(r, true));
    REQUIRE(r.parse("never"));
    s.reset();
    for (int i = 0; i < 200; ++i) CHECK_FALSE(s.frame(r, true));
}

TEST_CASE("catch-up: run.ini catchup= and its rules (R4.2)") {
    CatchUpRule r;
    CHECK(r.mode == CatchUpRule::Deferred); // the default since R4.2
    CHECK(r.maxLag == 3);
    CHECK(r.name() == "deferred(lag=3,margin=2.0)");
    CHECK(r.parseMode("immediate"));
    CHECK(r.name() == "immediate");
    CHECK_FALSE(r.parseMode("later"));
    CHECK(r.mode == CatchUpRule::Immediate); // untouched
    CHECK_FALSE(r.parseLag("0"));
    CHECK_FALSE(r.parseLag("9"));
    CHECK(r.parseLag("2"));
    CHECK_FALSE(r.parseMargin("-1"));
    CHECK_FALSE(r.parseMargin("x"));
    CHECK(r.parseMargin("1.5"));
    CHECK(r.maxLag == 2);
    CHECK(r.marginMs == doctest::Approx(1.5));

    // Immediate: what the clock did before R4.2: every due step at once, the rest over the cap dropped.
    CatchUpRule imm;
    imm.parseMode("immediate");
    CatchUpState s;
    CatchUpFrame f; // 60 Hz: base 1, cap 2
    f.fixedMs = 12.0f;
    f.prepareMs = 3.0f;
    f.stepMs = 4.0f; // no room for a second step (12 + 3 + 8 > 14.7)
    f.due = 1;
    CHECK(s.plan(imm, f).steps == 1);
    f.due = 2;
    CHECK(s.plan(imm, f).steps == 2);
    f.due = 5;
    CatchUpPlan p = s.plan(imm, f);
    CHECK(p.steps == 2);
    CHECK(p.dropped == 3);
    CHECK(s.owed() == 0);

    // Deferred: a late frame (two due) with no room runs one step and owes one.
    CatchUpRule def;
    s.reset();
    f.due = 2;
    p = s.plan(def, f);
    CHECK(p.steps == 1);
    CHECK(p.lag == 1);
    CHECK(p.repaid == 0);
    // On time again but still no room: one step, the lag stays.
    f.due = 1;
    p = s.plan(def, f);
    CHECK(p.steps == 1);
    CHECK(p.lag == 1);
    // A frame with room (5 + 1.5 + 2 x 2 <= 14.7) repays it.
    f.fixedMs = 5.0f;
    f.prepareMs = 1.5f;
    f.stepMs = 2.0f;
    p = s.plan(def, f);
    CHECK(p.steps == 2);
    CHECK(p.repaid == 1);
    CHECK(p.lag == 0);
    // An unknown step cost (0) never repays: only the bound forces.
    f.stepMs = 0.0f;
    f.due = 2;
    p = s.plan(def, f);
    CHECK(p.steps == 1);
    CHECK(p.lag == 1);

    // The bound: late frames in a row without room owe 1, 2, 3; the fourth forces a step.
    f.fixedMs = 12.0f;
    f.prepareMs = 3.0f;
    f.stepMs = 4.0f;
    s.reset();
    int lagAfter[4];
    int forced = 0;
    for (int i = 0; i < 4; ++i) {
        f.due = 2;
        p = s.plan(def, f);
        lagAfter[i] = p.lag;
        forced += p.forced;
        CHECK(p.dropped == 0);
    }
    CHECK(lagAfter[0] == 1);
    CHECK(lagAfter[1] == 2);
    CHECK(lagAfter[2] == 3);
    CHECK(lagAfter[3] == 3); // 3 + 2 due: the base step and one forced (the cap of 2), 3 still owed
    CHECK(forced == 1);
    // A long hitch (20 vblanks): the cap runs, what is over the bound is dropped.
    f.due = 20;
    p = s.plan(def, f);
    CHECK(p.steps == 2);
    CHECK(p.lag == 3);
    CHECK(p.dropped == 3 + 20 - 2 - 3);

    // Steps are kept: over any sequence of frames, run + dropped + still owed = due in total; at
    // most one late frame in a row is never more than the cap can absorb, so nothing is dropped.
    s.reset();
    long due = 0, run = 0, dropped = 0;
    unsigned x = 12345;
    for (int i = 0; i < 5000; ++i) {
        x = x * 1103515245u + 12345u;
        f.due = 1 + static_cast<int>((x >> 16) % 3 == 0); // a third of the frames late
        f.fixedMs = 4.0f + static_cast<float>((x >> 8) % 100) / 10.0f;
        f.stepMs = 2.0f;
        f.prepareMs = 1.5f;
        p = s.plan(def, f);
        due += f.due;
        run += p.steps;
        dropped += p.dropped;
        CHECK(p.steps >= 1);
        CHECK(p.steps <= 2);
        CHECK(p.lag <= 3);
    }
    CHECK(run + dropped + s.owed() == due);

    // 30 Hz: base 2, cap 4, the budget two vblanks (31.3 ms with the margin).
    s.reset();
    f.due = 2;
    f.base = 2;
    f.cap = 4;
    f.fixedMs = 12.0f;
    f.prepareMs = 3.0f;
    f.stepMs = 4.0f;
    CHECK(s.plan(def, f).steps == 2);
    f.due = 4; // late: 12 + 3 + 3 x 4 = 27 fits, 12 + 3 + 4 x 4 = 31 fits too
    p = s.plan(def, f);
    CHECK(p.steps == 4);
    CHECK(p.lag == 0);
    f.fixedMs = 16.0f; // 16 + 3 + 3 x 4 = 31 fits, 4 x 4 = 35 does not
    f.due = 4;
    p = s.plan(def, f);
    CHECK(p.steps == 3);
    CHECK(p.lag == 1);
}
