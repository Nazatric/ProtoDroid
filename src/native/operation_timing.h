#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <utility>

namespace native {

// Counter block for opt-in native operation diagnostics. Add/Take are not internally synchronized: the owner
// serializes them with the periodic reader. Keeping that policy outside the measured operation avoids holding the
// bookkeeping mutex around a driver call, allocation, copy, decode, or guest-visible lock.
struct OperationTiming {
    uint64_t calls = 0;
    uint64_t total_ns = 0;
    uint64_t max_ns = 0;
    std::array<uint64_t, 3> over{};  // >10, >50 and >100 ms

    void Add(uint64_t ns) {
        ++calls;
        total_ns += ns;
        max_ns = std::max(max_ns, ns);
        static constexpr uint64_t kThresholds[] = {10000000ull, 50000000ull, 100000000ull};
        for (size_t i = 0; i < over.size(); ++i)
            if (ns > kThresholds[i]) ++over[i];
    }

    OperationTiming Take() {
        OperationTiming out;
        out.calls = std::exchange(calls, 0);
        out.total_ns = std::exchange(total_ns, 0);
        out.max_ns = std::exchange(max_ns, 0);
        out.over = over;
        over.fill(0);
        return out;
    }
};

inline uint64_t DurationNanos(std::chrono::steady_clock::duration elapsed) {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
}

}  // namespace native
