#include "present_stats.h"

#include <chrono>
#include <mutex>

namespace native {
namespace {

std::mutex g_stats_mutex;
PresentStatsTracker g_stats;

}  // namespace

int64_t RecordPresent() {
    const int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
    std::lock_guard<std::mutex> lock(g_stats_mutex);
    return g_stats.AddPresent(now);
}

PresentStatsSnapshot GetPresentStats() {
    std::lock_guard<std::mutex> lock(g_stats_mutex);
    return g_stats.Snapshot();
}

}  // namespace native
