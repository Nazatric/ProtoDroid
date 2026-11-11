// Allocation-minimized hot-path timing windows. The producer only records integers and a four-entry worst-event list;
// the diagnostics thread periodically takes the samples and computes percentiles outside the rendering code.
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace native {

inline int64_t MonotonicNanos() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

struct TimingEvent {
    int64_t at_ns = 0;
    int64_t duration_ns = -1;
    int tid = 0;
};

struct TimingWindowSnapshot {
    uint64_t count = 0;
    uint64_t total_ns = 0;
    int64_t begin_ns = 0;
    int64_t end_ns = 0;
    // Fixed thresholds, not claims about physical display drops: >8.5, >17, >34, >50, >100, >250 and >1000 ms.
    std::array<uint64_t, 7> over{};
    std::array<TimingEvent, 4> worst{};
    std::vector<int64_t> samples;

    double ElapsedSeconds() const { return end_ns > begin_ns ? double(end_ns - begin_ns) / 1e9 : 0.0; }
    double Frequency() const {
        const double seconds = ElapsedSeconds();
        return seconds > 0 ? double(count) / seconds : 0.0;
    }
    double AverageMs() const { return count ? double(total_ns) / double(count) / 1e6 : 0.0; }
    double MaxMs() const { return worst[0].duration_ns > 0 ? double(worst[0].duration_ns) / 1e6 : 0.0; }

    // Nearest-rank percentile. This runs on the diagnostics thread after Take(), never in Present or EngineService.
    double PercentileMs(unsigned percent) {
        if (samples.empty()) return 0.0;
        percent = std::clamp(percent, 1u, 100u);
        const size_t rank = std::clamp((size_t(percent) * samples.size() + 99) / 100, size_t(1), samples.size());
        auto at = samples.begin() + (rank - 1);
        std::nth_element(samples.begin(), at, samples.end());
        return double(*at) / 1e6;
    }
};

class TimingWindow {
public:
    explicit TimingWindow(size_t reserve = 4096) : reserve_(reserve) { samples_.reserve(reserve_); }

    void Add(int64_t at_ns, int64_t duration_ns, int tid = 0) {
        if (duration_ns < 0) return;
        if (!count_) begin_ns_ = have_last_ ? last_at_ns_ : at_ns - duration_ns;
        ++count_;
        total_ns_ += uint64_t(duration_ns);
        end_ns_ = at_ns;
        last_at_ns_ = at_ns;
        have_last_ = true;
        samples_.push_back(duration_ns);

        static constexpr std::array<int64_t, 7> kThresholds = {
            8500000ll, 17000000ll, 34000000ll, 50000000ll, 100000000ll, 250000000ll, 1000000000ll,
        };
        for (size_t i = 0; i < kThresholds.size(); ++i)
            if (duration_ns > kThresholds[i]) ++over_[i];

        // Keep the four longest events in descending order. Four comparisons are cheaper and bounded compared with
        // logging or allocating an event object for every long frame.
        for (size_t i = 0; i < worst_.size(); ++i) {
            if (duration_ns <= worst_[i].duration_ns) continue;
            for (size_t j = worst_.size() - 1; j > i; --j) worst_[j] = worst_[j - 1];
            worst_[i] = {at_ns, duration_ns, tid};
            break;
        }
    }

    TimingWindowSnapshot Take() {
        TimingWindowSnapshot out;
        out.count = count_;
        out.total_ns = total_ns_;
        out.begin_ns = begin_ns_;
        out.end_ns = end_ns_;
        out.over = over_;
        out.worst = worst_;
        out.samples.swap(samples_);
        const size_t next_reserve = std::max(reserve_, std::min<size_t>(size_t(count_), 262144));

        count_ = 0;
        total_ns_ = 0;
        begin_ns_ = end_ns_ = 0;
        over_.fill(0);
        for (TimingEvent& event : worst_) event = {};
        // Reserve now, on the reader thread, so the next producer does not immediately allocate after a swap. If an
        // unusual window exceeded its reserve, size the next buffer for that observed rate (bounded to 262144 samples).
        samples_.reserve(next_reserve);
        return out;
    }

private:
    size_t reserve_ = 4096;
    uint64_t count_ = 0;
    uint64_t total_ns_ = 0;
    int64_t begin_ns_ = 0;
    int64_t end_ns_ = 0;
    int64_t last_at_ns_ = 0;
    bool have_last_ = false;
    std::array<uint64_t, 7> over_{};
    std::array<TimingEvent, 4> worst_{};
    std::vector<int64_t> samples_;
};

}  // namespace native
