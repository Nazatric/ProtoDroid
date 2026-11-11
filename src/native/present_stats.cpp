#include "present_stats.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <unistd.h>

namespace native {
namespace {

std::mutex g_stats_mutex;
PresentStatsTracker g_stats;
std::atomic<bool> g_detailed_enabled{false};
std::atomic<uint64_t> g_present_total{0};

}  // namespace

int64_t RecordPresent() {
    g_present_total.fetch_add(1, std::memory_order_relaxed);
    const int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
    thread_local const int tid = int(gettid());
    std::lock_guard<std::mutex> lock(g_stats_mutex);
    return g_stats.AddPresent(now, tid, PresentDetailedEnabled());
}

uint64_t PresentedFrameCount() { return g_present_total.load(std::memory_order_relaxed); }

PresentStatsSnapshot GetPresentStats() {
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

void SetPresentDetailedEnabled(bool enabled) { g_detailed_enabled.store(enabled, std::memory_order_release); }
bool PresentDetailedEnabled() { return g_detailed_enabled.load(std::memory_order_acquire); }

TimingWindowSnapshot TakePresentDetailedStats() {
    std::lock_guard<std::mutex> lock(g_stats_mutex);
    return g_stats.TakeDetailed();
}

}  // namespace native
