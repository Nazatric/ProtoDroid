#include "frame_limiter.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace native {
namespace {

int64_t NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::atomic<int> g_target_fps{0};
// A dirty schedule is consumed by the next limited (or one unlimited) call. Target changes never wait for the sleep
// mutex on the Android UI thread; a live change takes effect at the next Present, after at most one in-progress wait.
std::atomic<bool> g_schedule_dirty{true};
std::mutex g_limiter_mutex;
FrameLimitSchedule g_schedule;

}  // namespace

void FrameLimitSchedule::Reset() {
    target_fps_ = 0;
    period_ns_ = 0;
    period_remainder_ = 0;
    fractional_ns_ = 0;
    next_deadline_ns_ = 0;
    have_schedule_ = false;
}

void FrameLimitSchedule::AdvanceDeadline() {
    next_deadline_ns_ += period_ns_;
    fractional_ns_ += period_remainder_;
    if (fractional_ns_ >= target_fps_) {
        fractional_ns_ -= target_fps_;
        ++next_deadline_ns_;
    }
}

FrameLimitPlan FrameLimitSchedule::Plan(int target_fps, int64_t now_ns) {
    FrameLimitPlan plan;
    plan.now_ns = now_ns;
    if (target_fps <= 0) {
        Reset();
        return plan;
    }

    if (!have_schedule_ || target_fps_ != target_fps) {
        target_fps_ = target_fps;
        period_ns_ = 1000000000ll / target_fps_;
        period_remainder_ = 1000000000ll % target_fps_;
        fractional_ns_ = 0;
        next_deadline_ns_ = now_ns;
        have_schedule_ = true;
        plan.schedule_reset = true;
    }

    plan.target_fps = target_fps_;
    plan.deadline_ns = next_deadline_ns_;
    plan.requested_wait_ns = std::max<int64_t>(0, next_deadline_ns_ - now_ns);

    // Compare by subtraction rather than next+period, which cannot overflow a monotonic int64 timeline. The normal
    // path advances the old absolute grid even when the preceding sleep overshoots; only a full period of lateness
    // abandons that grid, preventing a sequence of zero-wait catch-up Presents after loading or surface interruption.
    if (!plan.schedule_reset && period_ns_ > 0 && now_ns >= next_deadline_ns_ &&
        now_ns - next_deadline_ns_ >= period_ns_) {
        next_deadline_ns_ = now_ns;
        fractional_ns_ = 0;
        plan.late_reset = true;
    }
    AdvanceDeadline();
    return plan;
}

void SetFrameLimitTarget(int target_fps) {
    target_fps = std::max(target_fps, 0);
    if (g_target_fps.load(std::memory_order_acquire) == target_fps) return;
    // Publish invalidation before the target. A Present that acquires the new target therefore cannot clear a stale
    // dirty flag and then have this setter mark the just-established new schedule dirty a second time.
    g_schedule_dirty.store(true, std::memory_order_release);
    g_target_fps.store(target_fps, std::memory_order_release);
}

int FrameLimitTarget() { return g_target_fps.load(std::memory_order_acquire); }

FrameLimitResult LimitPresentedFrame() {
    int target_fps = FrameLimitTarget();
    if (target_fps <= 0) {
        if (g_schedule_dirty.exchange(false, std::memory_order_acq_rel)) {
            std::lock_guard<std::mutex> lock(g_limiter_mutex);
            g_schedule.Reset();
        }
        return {};
    }

    // Present calls are normally confined to the game's render thread. Keeping this private mutex through the sleep
    // serializes deadline updates and waits if an additional swapchain thread is used. It is never the surface mutex
    // and is not acquired by SetFrameLimitTarget, ResetPresentFrameLimiter, or the Android surface JNI callbacks.
    std::lock_guard<std::mutex> lock(g_limiter_mutex);
    target_fps = FrameLimitTarget();
    if (target_fps <= 0) {
        g_schedule.Reset();
        g_schedule_dirty.store(false, std::memory_order_release);
        return {};
    }
    if (g_schedule_dirty.exchange(false, std::memory_order_acq_rel)) g_schedule.Reset();

    FrameLimitResult result;
    result.present_ns = NowNs();
    const FrameLimitPlan plan = g_schedule.Plan(target_fps, result.present_ns);
    result.target_fps = plan.target_fps;
    result.deadline_ns = plan.deadline_ns;
    result.requested_wait_ns = plan.requested_wait_ns;
    result.schedule_reset = plan.schedule_reset;
    result.late_reset = plan.late_reset;
    if (plan.requested_wait_ns > 0) {
        const auto deadline = std::chrono::steady_clock::time_point(std::chrono::nanoseconds(plan.deadline_ns));
        std::this_thread::sleep_until(deadline);
        const int64_t wake_ns = NowNs();
        result.wait_ns = std::max<int64_t>(0, wake_ns - result.present_ns);
        result.oversleep_ns = std::max<int64_t>(0, wake_ns - plan.deadline_ns);
    }
    return result;
}

void ResetPresentFrameLimiter() { g_schedule_dirty.store(true, std::memory_order_release); }

}  // namespace native
