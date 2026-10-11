// The frame pacer's drop rule (run.ini pace_rule=, ctr_loop.cpp updatePacer; docs/3ds-port.md
// "Third console soak", the offline study tools/3ds_pacer_replay.py): when a run of counted misses at
// 60 Hz sends pace=auto to 30 Hz. Platform neutral, header only (apps/tests/soak_report_test.cpp).
//
//   pace_rule=24 (default since R3.3): 24 counted misses in the last 120 frames at 60 Hz (20%);
//   pace_rule=N: N in 120 (12 = the rule of builds 5192662 and 664fee3);
//   pace_rule=run3: 3 misses in a row;
//   pace_rule=never: mono stays at 60 Hz whatever it misses.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>

namespace ctrapp {

struct PaceRule {
    enum Kind { Window, Run, Never } kind = Window;
    int need = 24;
    static constexpr int kWindow = 120;

    // "24", "12", "run3", "never": false (and the rule untouched) for anything else.
    bool parse(const std::string& v) {
        if (v == "never") {
            kind = Never;
            need = 0;
            return true;
        }
        if (v == "run3") {
            kind = Run;
            need = 3;
            return true;
        }
        char* end = nullptr;
        const long n = std::strtol(v.c_str(), &end, 10);
        if (v.empty() || *end != '\0' || n < 1 || n > kWindow) return false;
        kind = Window;
        need = static_cast<int>(n);
        return true;
    }
    std::string name() const {
        if (kind == Never) return "never";
        if (kind == Run) return "run" + std::to_string(need);
        return std::to_string(need) + "/" + std::to_string(kWindow);
    }
};

// The rule's state at 60 Hz: one call per frame with whether it was a counted miss; true when the
// pacer should go to 30 Hz (the caller resets the state when the rate changes).
class PaceDropState {
public:
    void reset() {
        for (uint8_t& m : ring_) m = 0;
        at_ = misses_ = run_ = 0;
    }
    bool frame(const PaceRule& rule, bool miss) {
        const uint8_t m = miss ? 1 : 0;
        misses_ += m - ring_[at_];
        ring_[at_] = m;
        at_ = (at_ + 1) % PaceRule::kWindow;
        run_ = miss ? run_ + 1 : 0;
        switch (rule.kind) {
        case PaceRule::Window: return misses_ >= rule.need;
        case PaceRule::Run: return run_ >= rule.need;
        case PaceRule::Never: return false;
        }
        return false;
    }
    int misses() const { return misses_; }

private:
    uint8_t ring_[PaceRule::kWindow] = {};
    int at_ = 0, misses_ = 0, run_ = 0;
};

} // namespace ctrapp
