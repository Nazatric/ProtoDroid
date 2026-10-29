// Presented-frame timing, independent of the engine's message-pump loop. Times are monotonic nanoseconds supplied by
// the caller so the production clock and host regression tests use exactly the same accounting.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace native {

struct PresentStatsSnapshot {
    double fps = 0;
    double average_ms = 0;
    double p99_ms = 0;
    double max_ms = 0;
    uint64_t frames = 0;
};

class PresentStatsTracker {
public:
    PresentStatsTracker() { samples_.reserve(4096); }  // avoid repeated first-window allocations in fast menus

    // The first Present establishes the baseline. Every later call returns one complete Present-to-Present interval;
    // this is also the single value that may be reported to ADPF for that Present.
    int64_t AddPresent(int64_t now_ns) {
        if (!have_last_) {
            have_last_ = true;
            last_present_ns_ = now_ns;
            window_start_ns_ = now_ns;
            return 0;
        }

        const int64_t duration_ns = now_ns - last_present_ns_;
        last_present_ns_ = now_ns;
        if (duration_ns <= 0) return 0;

        ++window_frames_;
        window_total_ns_ += duration_ns;
        window_max_ns_ = std::max(window_max_ns_, duration_ns);
        samples_.push_back(duration_ns);

        const int64_t elapsed_ns = now_ns - window_start_ns_;
        if (elapsed_ns >= 1000000000ll) FinishWindow(elapsed_ns);
        return duration_ns;
    }

    PresentStatsSnapshot Snapshot() const { return current_; }

private:
    void FinishWindow(int64_t elapsed_ns) {
        current_ = {};
        current_.frames = window_frames_;
        current_.fps = double(window_frames_) * 1e9 / double(elapsed_ns);
        current_.average_ms = double(window_total_ns_) / double(window_frames_) / 1e6;
        current_.max_ms = double(window_max_ns_) / 1e6;

        // Nearest-rank p99: the 99th item for 100 samples, not the maximum unless the distribution requires it.
        const size_t rank = (99 * samples_.size() + 99) / 100;
        auto percentile = samples_.begin() + (rank - 1);
        std::nth_element(samples_.begin(), percentile, samples_.end());
        current_.p99_ms = double(*percentile) / 1e6;

        window_frames_ = 0;
        window_total_ns_ = 0;
        window_max_ns_ = 0;
        window_start_ns_ = last_present_ns_;
        samples_.clear();
    }

    bool have_last_ = false;
    int64_t last_present_ns_ = 0;
    int64_t window_start_ns_ = 0;
    uint64_t window_frames_ = 0;
    int64_t window_total_ns_ = 0;
    int64_t window_max_ns_ = 0;
    std::vector<int64_t> samples_;
    PresentStatsSnapshot current_;
};

// Process-wide thread-safe adapter used by D3D9 Present, the main-loop logger, and the Android overlay.
int64_t RecordPresent();  // monotonic clock read and tracker update; returns the ADPF interval, or zero
PresentStatsSnapshot GetPresentStats();

}  // namespace native
