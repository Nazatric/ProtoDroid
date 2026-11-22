#include "present_stats.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <unistd.h>

namespace native {
namespace {

std::mutex g_stats_mutex;
PresentStatsTracker g_stats;
// Bit 0 selects detailed collection; upper bits are a generation used to rebase each thread's ordinary-play window
// when diagnostics start or stop. A single atomic keeps the mode and generation consistent for RecordPresent().
std::atomic<uint64_t> g_present_mode{0};
std::atomic<uint64_t> g_present_total{0};

// Ordinary play only needs a recent one-second summary. Keep the per-Present samples thread-local and bounded so the
// render path avoids both the diagnostics mutex and the detailed tracker's growing vector.
constexpr uint32_t kSampleRing = 1024;
struct LightWindow {
    bool initialized = false;
    uint64_t generation = 0;
    int64_t last_ns = 0;
    int64_t start_ns = 0;
    uint64_t frames = 0;
    int64_t total_ns = 0;
    int64_t max_ns = 0;
    uint32_t us[kSampleRing]{};
    uint32_t count = 0;
    uint32_t next = 0;
};
thread_local LightWindow tl_light;

std::mutex g_light_mutex;
PresentStatsSnapshot g_light;

void ResetLightWindow(LightWindow& window, uint64_t generation, int64_t now_ns) {
    window = LightWindow{};
    window.initialized = true;
    window.generation = generation;
    window.last_ns = now_ns;
    window.start_ns = now_ns;
}

uint32_t WindowP99Us(const LightWindow& window) {
    const uint32_t n = window.count;
    if (!n) return 0;
    uint32_t copy[kSampleRing];
    std::memcpy(copy, window.us, sizeof(uint32_t) * n);
    const uint32_t rank = (99 * n + 99) / 100;
    std::nth_element(copy, copy + (rank - 1), copy + n);
    return copy[rank - 1];
}

void FinishLightWindow(LightWindow& window, int64_t now_ns) {
    const int64_t elapsed_ns = now_ns - window.start_ns;
    if (!window.frames || elapsed_ns <= 0) return;

    PresentStatsSnapshot out;
    out.frames = window.frames;
    out.fps = double(window.frames) * 1e9 / double(elapsed_ns);
    out.average_ms = double(window.total_ns) / double(window.frames) / 1e6;
    out.max_ms = double(window.max_ns) / 1e6;
    out.p99_ms = double(WindowP99Us(window)) / 1e3;
    {
        std::lock_guard<std::mutex> lock(g_light_mutex);
        g_light = out;
    }

    window.frames = 0;
    window.total_ns = 0;
    window.max_ns = 0;
    window.start_ns = now_ns;
    window.count = 0;
    window.next = 0;
}

}  // namespace

int64_t RecordPresent() {
    g_present_total.fetch_add(1, std::memory_order_relaxed);
    const int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
    const uint64_t mode = g_present_mode.load(std::memory_order_acquire);
    if (mode & 1u) {
        thread_local const int tid = int(gettid());
        std::lock_guard<std::mutex> lock(g_stats_mutex);
        return g_stats.AddPresent(now, tid, true);
    }

    LightWindow& window = tl_light;
    const uint64_t generation = mode >> 1;
    if (!window.initialized || window.generation != generation) {
        ResetLightWindow(window, generation, now);
        return 0;
    }

    const int64_t duration_ns = now - window.last_ns;
    window.last_ns = now;
    if (duration_ns <= 0) return 0;

    ++window.frames;
    window.total_ns += duration_ns;
    if (duration_ns > window.max_ns) window.max_ns = duration_ns;
    const uint64_t duration_us = uint64_t(duration_ns) / 1000;
    window.us[window.next] = duration_us > 0xffffffffull ? 0xffffffffu : uint32_t(duration_us);
    window.next = (window.next + 1) % kSampleRing;
    if (window.count < kSampleRing) ++window.count;
    if (now - window.start_ns >= 1000000000ll) FinishLightWindow(window, now);
    return duration_ns;
}

uint64_t PresentedFrameCount() { return g_present_total.load(std::memory_order_relaxed); }

PresentStatsSnapshot GetPresentStats() {
    if (!(g_present_mode.load(std::memory_order_acquire) & 1u)) {
        std::lock_guard<std::mutex> lock(g_light_mutex);
        return g_light;
    }
    std::lock_guard<std::mutex> lock(g_stats_mutex);
    return g_stats.Snapshot();
}

PresentStatsTotals GetDetailedPresentTotals() {
    std::lock_guard<std::mutex> lock(g_stats_mutex);
    return g_stats.DetailedTotals();
}

void ResetDetailedPresentTotals() {
    std::lock_guard<std::mutex> lock(g_stats_mutex);
    g_stats.ResetDetailedTotals();
}

void SetPresentDetailedEnabled(bool enabled) {
    uint64_t state = g_present_mode.load(std::memory_order_relaxed);
    for (;;) {
        if (bool(state & 1u) == enabled) return;
        const uint64_t next = ((state & ~uint64_t(1)) + 2) | uint64_t(enabled);
        if (g_present_mode.compare_exchange_weak(state, next, std::memory_order_acq_rel,
                                                 std::memory_order_relaxed)) {
            if (!enabled) {
                std::lock_guard<std::mutex> lock(g_light_mutex);
                g_light = {};
            }
            return;
        }
    }
}

bool PresentDetailedEnabled() { return (g_present_mode.load(std::memory_order_acquire) & 1u) != 0; }

TimingWindowSnapshot TakePresentDetailedStats() {
    std::lock_guard<std::mutex> lock(g_stats_mutex);
    return g_stats.TakeDetailed();
}

}  // namespace native
