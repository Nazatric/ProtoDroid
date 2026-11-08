// Guest-visible elapsed clocks. Ordinary Play is always real time; the explicitly isolated diagnostic tour uses a
// fixed clock multiplier to exercise timer-driven movement and systems faster without changing host watchdog timing.
#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

namespace native {

inline constexpr uint32_t kDiagnosticTourGuestClockScale = 4;

constexpr uint64_t ScaleGuestElapsedNanos(uint64_t elapsed_ns, bool diagnostic_tour) {
    if (!diagnostic_tour) return elapsed_ns;
    constexpr uint64_t limit = std::numeric_limits<uint64_t>::max() / kDiagnosticTourGuestClockScale;
    return elapsed_ns > limit ? std::numeric_limits<uint64_t>::max()
                              : elapsed_ns * kDiagnosticTourGuestClockScale;
}

namespace detail {
inline std::atomic<bool> g_diagnostic_tour_clock{false};
}  // namespace detail

inline void SetGuestClockDiagnosticTour(bool diagnostic_tour) {
    // Reset at every native entry: Android can reuse a process for a later ordinary SDL activity. Relaxed ordering is
    // sufficient because this atomic carries only the mode; engine startup provides the thread synchronization.
    detail::g_diagnostic_tour_clock.store(diagnostic_tour, std::memory_order_relaxed);
}

inline uint64_t GuestElapsedNanos(uint64_t elapsed_ns) {
    // No getenv or settings lookup on QPC/timeGetTime hot paths, and no one-time mode that can leak into normal Play.
    return ScaleGuestElapsedNanos(elapsed_ns, detail::g_diagnostic_tour_clock.load(std::memory_order_relaxed));
}

}  // namespace native
