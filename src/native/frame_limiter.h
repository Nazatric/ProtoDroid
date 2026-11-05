// Successful-Present frame pacing with absolute, rational nanosecond deadlines. The scheduler is independent from the
// engine message pump and from the physical display's FIFO/vsync cadence. It never skips, repeats, or fabricates work.
#pragma once

#include <cstdint>

namespace native {

struct FrameLimitPlan {
    int target_fps = 0;
    int64_t now_ns = 0;
    int64_t deadline_ns = 0;
    int64_t requested_wait_ns = 0;
    bool schedule_reset = false;
    bool late_reset = false;
};

class FrameLimitSchedule {
public:
    // Plan a wait immediately after one successful Present. The first Present at a new target establishes a baseline;
    // later calls retain the original deadline grid. A call at least one complete period behind reanchors instead of
    // emitting a burst of catch-up frames. `now_ns` is a monotonic timestamp supplied by the caller for determinism.
    FrameLimitPlan Plan(int target_fps, int64_t now_ns);
    void Reset();

private:
    void AdvanceDeadline();

    int target_fps_ = 0;
    int64_t period_ns_ = 0;
    int64_t period_remainder_ = 0;
    int64_t fractional_ns_ = 0;
    int64_t next_deadline_ns_ = 0;
    bool have_schedule_ = false;
};

struct FrameLimitResult {
    int target_fps = 0;
    int64_t present_ns = 0;
    int64_t deadline_ns = 0;
    int64_t requested_wait_ns = 0;
    int64_t wait_ns = 0;
    int64_t oversleep_ns = 0;
    bool schedule_reset = false;
    bool late_reset = false;
};

// Live launcher/JNI target. Non-positive values mean unlimited. The atomic fast path keeps steady unlimited Present
// calls free of limiter clock reads and mutex acquisitions; a one-time transition may clear the old schedule.
void SetFrameLimitTarget(int target_fps);
int FrameLimitTarget();

// Called only after a real successful Present has been recorded. The caller must not hold the surface/Present mutex:
// this function can sleep while Android independently destroys or recreates the surface.
FrameLimitResult LimitPresentedFrame();

// Defer a clean baseline to the next limiter call after a skipped/failed Present or a device/surface interruption.
void ResetPresentFrameLimiter();

}  // namespace native
