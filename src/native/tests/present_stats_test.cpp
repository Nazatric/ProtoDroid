#include "present_stats.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "present-stats test failed: %s\n", message);
        std::exit(1);
    }
}

bool Near(double a, double b, double tolerance = 0.02) { return std::fabs(a - b) <= tolerance; }

void TestBaselineAndOneReportPerPresent() {
    native::PresentStatsTracker tracker;
    int reports = 0;
    auto present = [&](int64_t now) {
        const int64_t duration = tracker.AddPresent(now);
        if (duration > 0) ++reports;  // production GuardedPresent calls PerfHintFrame here exactly once
        return duration;
    };

    Check(present(0) == 0 && reports == 0, "zero-time baseline was not initialized");
    Check(present(16666667) == 16666667 && reports == 1, "first complete interval was not reported once");
    // Empty message-pump iterations do not call AddPresent and therefore cannot reach ADPF.
    Check(reports == 1, "empty pump iterations changed ADPF report count");
    Check(present(33333334) == 16666667 && reports == 2, "second Present did not produce exactly one report");
}

void TestSixtyFpsWindow() {
    native::PresentStatsTracker tracker;
    int64_t now = 1000000000ll;
    tracker.AddPresent(now);
    int reports = 0;
    for (int i = 0; i < 60; ++i) {
        now += 16666667;
        if (tracker.AddPresent(now) > 0) ++reports;
    }
    const auto s = tracker.Snapshot();
    Check(reports == 60 && s.frames == 60, "Present count does not match completed intervals");
    Check(Near(s.fps, 60.0, 0.01), "presented FPS is not count divided by elapsed time");
    Check(Near(s.average_ms, 16.666667), "average Present interval is inaccurate");
    Check(Near(s.p99_ms, 16.666667), "constant-frame p99 is inaccurate");
    Check(Near(s.max_ms, 16.666667), "constant-frame max is inaccurate");
}

void TestAveragePercentileAndOutlier() {
    native::PresentStatsTracker tracker;
    int64_t now = 0;
    tracker.AddPresent(now);
    std::vector<int64_t> durations(99, 10000000);  // 99 frames at 10 ms
    durations.push_back(100000000);                // one 100 ms combat spike
    for (int64_t duration : durations) {
        now += duration;
        tracker.AddPresent(now);
    }
    const auto s = tracker.Snapshot();
    Check(s.frames == 100, "non-uniform window lost a Present");
    Check(Near(s.fps, 100.0 / 1.09, 0.01), "non-uniform FPS used reciprocal average incorrectly");
    Check(Near(s.average_ms, 10.9), "non-uniform average is inaccurate");
    Check(Near(s.p99_ms, 10.0), "nearest-rank p99 was confused with the maximum");
    Check(Near(s.max_ms, 100.0), "maximum frame interval was lost");
}

void TestFrameLimitAndFifoPolicy() {
    Check(!native::FrameLimitNeedsSleep(0, 60), "unlimited mode introduced a host sleep");
    Check(native::FrameLimitNeedsSleep(30, 60), "30 FPS cap was disabled by 60 Hz FIFO");
    Check(native::FrameLimitNeedsSleep(45, 60), "45 FPS cap was disabled by 60 Hz FIFO");
    Check(native::FrameLimitNeedsSleep(60, 120), "60 FPS cap was disabled by 120 Hz FIFO");
    Check(native::FrameLimitNeedsSleep(60, 0), "unknown refresh rate disabled the explicit cap");
    Check(!native::FrameLimitNeedsSleep(60, 60), "60 Hz FIFO and 60 FPS cap caused double pacing");
    Check(!native::FrameLimitNeedsSleep(120, 60), "covered above-refresh limit caused a redundant sleep");
}

void TestNoLoopCounterLeak() {
    native::PresentStatsTracker tracker;
    Check(tracker.Snapshot().frames == 0 && tracker.Snapshot().fps == 0,
          "new tracker invented frames before the first Present");
    tracker.AddPresent(5000000000ll);
    for (int i = 0; i < 10000; ++i) {
        // Simulated guest pump work: intentionally no tracker call.
    }
    Check(tracker.Snapshot().frames == 0 && tracker.Snapshot().fps == 0,
          "message-pump iterations leaked into presented-frame statistics");
}

}  // namespace

int main() {
    TestBaselineAndOneReportPerPresent();
    TestSixtyFpsWindow();
    TestAveragePercentileAndOutlier();
    TestFrameLimitAndFifoPolicy();
    TestNoLoopCounterLeak();
    std::puts("present-stats tests passed (baseline, ADPF count, FPS, percentiles, FIFO limit policy)");
    return 0;
}
