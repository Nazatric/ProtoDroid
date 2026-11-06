#include "frame_limiter.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "frame-limiter test failed: %s\n", message);
        std::exit(1);
    }
}

bool Near(double value, double expected, double tolerance) { return std::fabs(value - expected) <= tolerance; }

double SimulateAbsolute(int fps, int frames, int64_t work_ns, int64_t oversleep_ns) {
    native::FrameLimitSchedule schedule;
    int64_t now = 1000000000ll;
    int64_t first = -1, last_present = now;
    constexpr int kWarmupFrames = 10;
    for (int frame = 0; frame < frames; ++frame) {
        last_present = now;  // RecordPresent() happens before the post-Present limiter wait.
        if (frame == kWarmupFrames) first = now;
        const auto plan = schedule.Plan(fps, now);
        Check(plan.target_fps == fps, "absolute simulation lost its target");
        if (plan.requested_wait_ns > 0) now = plan.deadline_ns + oversleep_ns;
        now += work_ns;  // real work before the next successful Present
    }
    return double(frames - kWarmupFrames - 1) * 1e9 / double(last_present - first);
}

double SimulatePostSleepReanchor(int fps, int frames, int64_t work_ns, int64_t oversleep_ns) {
    // Faithful post-Present form of the old policy: each new deadline starts at the actual overslept time, so the
    // overshoot is added to every later frame instead of being absorbed by the existing absolute schedule.
    int64_t now = 1000000000ll;
    int64_t anchor = now;
    int64_t first = -1, last_present = now;
    constexpr int kWarmupFrames = 10;
    const int64_t period = 1000000 / fps * 1000;  // old whole-microsecond truncation
    for (int frame = 0; frame < frames; ++frame) {
        last_present = now;
        if (frame == kWarmupFrames) first = now;
        const int64_t due = anchor + period;
        if (now < due) now = due + oversleep_ns;
        anchor = now;
        now += work_ns;
    }
    return double(frames - kWarmupFrames - 1) * 1e9 / double(last_present - first);
}

void TestOversleepDoesNotLowerAverageRate() {
    const double candidate60 = SimulateAbsolute(60, 1200, 4000000, 1500000);
    const double old60 = SimulatePostSleepReanchor(60, 1200, 4000000, 1500000);
    Check(Near(candidate60, 60.0, 0.000001), "absolute 60 FPS schedule accumulated oversleep");
    Check(Near(old60, 55.0478917, 0.000001), "old-policy control did not expose reanchoring loss");

    const double candidate90 = SimulateAbsolute(90, 1800, 1000000, 250000);
    const double old90 = SimulatePostSleepReanchor(90, 1800, 1000000, 250000);
    Check(Near(candidate90, 90.0, 0.000001), "absolute 90 FPS schedule accumulated oversleep");
    Check(Near(old90, 88.0204207, 0.000001), "old-policy 90 FPS control did not expose reanchoring loss");
}

void TestRationalDeadlineGrid() {
    for (int fps : {30, 45, 60, 90, 120, 144, 240, 500, 1000}) {
        native::FrameLimitSchedule schedule;
        const int64_t base = 5000000000ll;
        Check(schedule.Plan(fps, base).schedule_reset, "new target did not establish a baseline");
        // At each exact deadline no wait is needed. Advancing the target's own number of frames must produce exactly
        // one second, including 90/144 rates whose periods are not integral microseconds or nanoseconds.
        for (int frame = 1; frame <= fps; ++frame) {
            const int64_t expected = base + (int64_t(frame) * 1000000000ll) / fps;
            const auto plan = schedule.Plan(fps, expected);
            Check(plan.deadline_ns == expected, "rational deadline grid drifted");
            Check(plan.requested_wait_ns == 0, "on-time Present was delayed");
            Check(!plan.late_reset, "on-time Present incorrectly reset the schedule");
        }
    }
}

void TestTargetChangesAndDisableReset() {
    native::FrameLimitSchedule schedule;
    const int64_t base = 100000000ll;
    auto first = schedule.Plan(60, base);
    Check(first.schedule_reset && first.requested_wait_ns == 0, "initial cap was not a no-wait baseline");
    auto limited = schedule.Plan(60, base + 1000000);
    Check(limited.requested_wait_ns == 15666666, "60 FPS remaining deadline is wrong");

    auto changed = schedule.Plan(90, base + 2000000);
    Check(changed.schedule_reset && changed.requested_wait_ns == 0, "live target change retained the old deadline");
    auto ninety = schedule.Plan(90, base + 3000000);
    Check(ninety.deadline_ns == base + 2000000 + 11111111, "90 FPS baseline after target change is wrong");

    auto off = schedule.Plan(0, base + 4000000);
    Check(off.target_fps == 0 && off.requested_wait_ns == 0, "unlimited mode retained a wait");
    auto reenabled = schedule.Plan(90, base + 5000000);
    Check(reenabled.schedule_reset && reenabled.requested_wait_ns == 0, "off/on did not reset the old schedule");
    Check(schedule.Plan(-1, base + 6000000).target_fps == 0, "negative custom target was not unlimited");
}

void TestLongPauseDoesNotBurst() {
    native::FrameLimitSchedule schedule;
    const int64_t base = 9000000000ll;
    schedule.Plan(60, base);
    schedule.Plan(60, base + 1000000);
    const int64_t resumed = base + 200000000ll;
    auto late = schedule.Plan(60, resumed);
    Check(late.late_reset && late.requested_wait_ns == 0, "long pause did not reanchor without a wait");
    auto next = schedule.Plan(60, resumed + 1000000);
    Check(!next.late_reset && !next.schedule_reset, "post-pause frame repeatedly reset the schedule");
    Check(next.deadline_ns == resumed + 16666666, "post-pause deadline did not start from the resumed Present");
    Check(next.requested_wait_ns == 15666666, "post-pause remaining wait is wrong");
}

void TestRealAdapterTargetsAndReset() {
    native::SetFrameLimitTarget(0);
    native::ResetPresentFrameLimiter();
    Check(native::LimitPresentedFrame().target_fps == 0, "unlimited adapter returned a target");
    native::SetFrameLimitTarget(-20);
    Check(native::FrameLimitTarget() == 0, "negative JNI/config target was not clamped to unlimited");
    native::SetFrameLimitTarget(60);
    Check(native::FrameLimitTarget() == 60, "live target was not published");
    const auto first = native::LimitPresentedFrame();
    Check(first.target_fps == 60 && first.schedule_reset && first.requested_wait_ns == 0,
          "real adapter did not establish a no-wait baseline");
    native::SetFrameLimitTarget(60);  // reapplying an unchanged launcher value must not reset the grid
    Check(!native::LimitPresentedFrame().schedule_reset, "unchanged live target unnecessarily reset the schedule");
    native::ResetPresentFrameLimiter();
    const auto reset = native::LimitPresentedFrame();
    Check(reset.schedule_reset && reset.requested_wait_ns == 0, "surface/device reset did not create a new baseline");
    native::SetFrameLimitTarget(0);
    Check(native::LimitPresentedFrame().target_fps == 0, "live disable did not return to unlimited");
}

}  // namespace

int main() {
    TestOversleepDoesNotLowerAverageRate();
    TestRationalDeadlineGrid();
    TestTargetChangesAndDisableReset();
    TestLongPauseDoesNotBurst();
    TestRealAdapterTargetsAndReset();
    std::puts("frame-limiter tests passed (absolute/rational deadlines, oversleep control, targets, pause/reset, adapter)");
    return 0;
}
