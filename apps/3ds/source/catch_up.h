// The frame clock's catch-up rule (run.ini catchup=, catchup_lag=, catchup_margin_ms=;
// ctr_loop.cpp FrameClock::due; docs/3ds-port.md "Mono at 60 and the level start (round 4,
// R4.2)"). Platform neutral, header only (apps/tests/soak_report_test.cpp); the offline replay
// tools/3ds_pacer_replay.py runs the same rules on a console perf.csv.
//
// The simulation steps once per vblank. A frame that missed its vblank leaves two vblanks to the
// next one: two steps are due. On the console (fourth soak, mission 1 mono) a frame that runs two
// steps misses its own vblank 62 to 82% of the time (the heavy stretches of a mission), so one
// late frame starts a cascade: 80% of the misses were the second and later frames of such runs.
//
//   catchup=immediate (the rule before R4.2): every due step runs in the next frame, at most
//     maxCatchUp() of them (2 at 60 Hz, 4 at 30); the rest are dropped.
//   catchup=deferred (the default since R4.2): the frame runs its own steps (1 at 60 Hz, 2 at 30)
//     and an owed step only when the frame has room for it: the previous frame's CPU path outside
//     the steps (wait + submit + end + after), its prepare and one more step's cost (the last
//     frame's cost per step) must fit in the frame's period (16.7 ms per vblank of the rate) minus
//     catchup_margin_ms (2 by default). Otherwise the simulation stays behind the clock, at most
//     catchup_lag steps (3 by default, 50 ms at 60 Hz): beyond that the steps run anyway (forced),
//     up to maxCatchUp(); what is still beyond the bound after that is dropped (as the immediate
//     rule drops what is over its cap).
//
// The simulation runs exactly the same steps in both rules (the bot's input and the weapon tour
// are functions of the step number, the pad is read per step), only their frame changes; the
// world's clock keeps 60 steps per second on average and is at most catchup_lag steps late. A
// frame never runs 0 steps (the base steps always run), and the pacer still decides misses by the
// vblanks between two frames: a frame on time with steps owed is not a miss.
#pragma once

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace ctrapp {

struct CatchUpRule {
    enum Mode { Immediate, Deferred } mode = Deferred;
    int maxLag = 3;         // steps the simulation may stay behind the clock (deferred)
    float marginMs = 2.0f;  // the budget is the frame's period minus this (deferred)

    // catchup=: "deferred", "immediate"; false (the rule untouched) for anything else.
    bool parseMode(const std::string& v) {
        if (v == "deferred") mode = Deferred;
        else if (v == "immediate") mode = Immediate;
        else return false;
        return true;
    }
    // catchup_lag=1..8
    bool parseLag(const std::string& v) {
        char* end = nullptr;
        const long n = std::strtol(v.c_str(), &end, 10);
        if (v.empty() || *end != '\0' || n < 1 || n > 8) return false;
        maxLag = static_cast<int>(n);
        return true;
    }
    // catchup_margin_ms=0..10
    bool parseMargin(const std::string& v) {
        char* end = nullptr;
        const double x = std::strtod(v.c_str(), &end);
        if (v.empty() || *end != '\0' || !(x >= 0.0 && x <= 10.0)) return false;
        marginMs = static_cast<float>(x);
        return true;
    }
    std::string name() const {
        if (mode == Immediate) return "immediate";
        char buf[64];
        std::snprintf(buf, sizeof buf, "deferred(lag=%d,margin=%.1f)", maxLag, static_cast<double>(marginMs));
        return buf;
    }
};

// What the clock knows when it decides a frame's steps (all of the frames before it).
struct CatchUpFrame {
    int due = 1;          // vblanks since the last frame (>= 1), uncapped
    int base = 1;         // the frame's own steps at the current rate: 1 at 60 Hz, 2 at 30
    int cap = 2;          // the most steps one frame runs (maxCatchUp: 2 at 60 Hz, 4 at 30)
    float fixedMs = 0.0f; // the last frame's CPU path outside its steps: wait + submit + end + after
    float prepareMs = 0.0f; // the last frame's prepare
    float stepMs = 0.0f;    // the last frame's cost per step (0: unknown, an owed step only when forced)
};

struct CatchUpPlan {
    int steps = 1;    // to run in this frame
    int dropped = 0;  // never run (over the cap, or over the lag bound after the cap)
    int repaid = 0;   // owed steps run because the frame had room
    int forced = 0;   // owed steps run because the lag bound was reached
    int lag = 0;      // steps still owed after this frame
};

class CatchUpState {
public:
    void reset() { owed_ = 0; }
    int owed() const { return owed_; }

    CatchUpPlan plan(const CatchUpRule& rule, const CatchUpFrame& f) {
        CatchUpPlan p;
        owed_ += std::max(1, f.due);
        const int cap = std::max(1, f.cap);
        int run;
        int keep = 0; // steps that may stay owed after the frame
        if (rule.mode == CatchUpRule::Immediate) {
            run = std::min(owed_, cap);
        } else {
            keep = rule.maxLag;
            run = std::min(owed_, std::min(std::max(1, f.base), cap));
            const float budget = (1000.0f / 60.0f) * static_cast<float>(std::max(1, f.base)) - rule.marginMs;
            while (run < cap && run < owed_) {
                const bool force = owed_ - run > keep;
                const bool room = f.stepMs > 0.0f && f.fixedMs + f.prepareMs + static_cast<float>(run + 1) * f.stepMs <= budget;
                if (!force && !room) break;
                if (force) ++p.forced;
                else ++p.repaid;
                ++run;
            }
        }
        p.steps = run;
        p.dropped = std::max(0, owed_ - run - keep);
        owed_ -= run + p.dropped;
        p.lag = owed_;
        return p;
    }

private:
    int owed_ = 0;
};

} // namespace ctrapp
