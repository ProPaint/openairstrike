// The on-screen frame counter (Options "Show FPS", desktop --fps; docs/spec/issues/140): frames
// per second averaged over the last whole second, the worst frame time of that second and the
// simulation steps dropped in it, from the numbers the loop already collects for AS3D_PERF.
// Pure arithmetic on the intervals it is given, so it is tested with synthetic frame times.
#pragma once

#include <algorithm>

namespace as3d_game {

class FpsCounter {
public:
    // One presented frame, `interval` seconds after the previous one, with the simulation
    // steps dropped since. Every full second of intervals publishes new values.
    void frame(double interval, int dropped = 0) {
        if (!(interval >= 0) || interval > 5.0) { // a load or a stay in the background
            reset();
            return;
        }
        ++frames_;
        sum_ += interval;
        worst_ = std::max(worst_, interval);
        dropped_ += std::max(dropped, 0);
        if (sum_ >= 1.0) {
            fps_ = static_cast<double>(frames_) / sum_;
            worstMs_ = worst_ * 1000.0;
            droppedShown_ = dropped_;
            valid_ = true;
            frames_ = 0;
            sum_ = worst_ = 0;
            dropped_ = 0;
        }
    }
    // Drops the second in progress (the values shown stay).
    void reset() {
        frames_ = 0;
        sum_ = worst_ = 0;
        dropped_ = 0;
    }
    bool valid() const { return valid_; }
    double fps() const { return fps_; }
    double worstMs() const { return worstMs_; }
    int dropped() const { return droppedShown_; }

private:
    int frames_ = 0;
    double sum_ = 0, worst_ = 0;
    int dropped_ = 0;
    bool valid_ = false;
    double fps_ = 0, worstMs_ = 0;
    int droppedShown_ = 0;
};

} // namespace as3d_game
