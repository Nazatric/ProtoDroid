#include "dxvk_pipeline_drain.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
struct FakeClock {
    using duration = std::chrono::nanoseconds;
    using rep = duration::rep;
    using period = duration::period;
    using time_point = std::chrono::time_point<FakeClock, duration>;
    static constexpr bool is_steady = true;
    static inline int64_t now_ns = 0;
    static time_point now() { return time_point(duration(now_ns)); }
};

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

using Stats = dxvk::DxvkPipelineDrainStats;
using Drain = dxvk::DxvkPipelineDrain;
}  // namespace

int main() {
    FakeClock::now_ns = 0;
    Stats stats;
    unsigned sleeps = 0;
    auto sleep = [&](std::chrono::nanoseconds delay) {
        ++sleeps;
        FakeClock::now_ns += delay.count();
    };
    auto idle = Drain::Wait<FakeClock>(1000000, sleep, [&] { return stats; });
    Check(idle.drained && idle.iterations == 1 && idle.waitNs == 0 && sleeps == 0,
          "initial idle did not return immediately");

    FakeClock::now_ns = 0;
    sleeps = 0;
    stats = {1, 5, 2};
    auto backlog = Drain::Wait<FakeClock>(10000000, [&](std::chrono::nanoseconds delay) {
        ++sleeps;
        FakeClock::now_ns += delay.count();
        if (sleeps == 3) stats = {};
    }, [&] { return stats; });
    Check(backlog.drained && backlog.waitNs == 3000000 && backlog.iterations == 4 && sleeps == 3,
          "feeder/pipeline backlog did not drain at the advertised transition");
    Check(backlog.stats.idle(), "final drained snapshot is not idle");

    FakeClock::now_ns = 0;
    sleeps = 0;
    stats = {1, 5, 1};
    auto timeout = Drain::Wait<FakeClock>(5000000, sleep, [&] { return stats; });
    Check(!timeout.drained && timeout.waitNs == 5000000 && timeout.iterations == 6 && sleeps == 5,
          "five-millisecond timeout did not bound polling");
    Check(timeout.stats.stateCachePending == 1 && timeout.stats.pipelinePending() == 4,
          "timeout lost pending-stage counts");

    FakeClock::now_ns = 0;
    sleeps = 0;
    auto zero = Drain::Wait<FakeClock>(0, sleep, [&] { return stats; });
    Check(!zero.drained && zero.iterations == 1 && zero.waitNs == 0 && sleeps == 0,
          "zero timeout slept or invented a drain");

    FakeClock::now_ns = 0;
    std::vector<int64_t> delays;
    auto partial = Drain::Wait<FakeClock>(1500000, [&](std::chrono::nanoseconds delay) {
        delays.push_back(delay.count());
        FakeClock::now_ns += delay.count();
    }, [&] { return stats; });
    Check(!partial.drained && partial.waitNs == 1500000 && delays.size() == 2 &&
              delays[0] == 1000000 && delays[1] == 500000,
          "final sleep did not use the remaining deadline");

    FakeClock::now_ns = 0;
    sleeps = 0;
    auto capped = Drain::Wait<FakeClock>(31000000000ull, sleep, [&] { return stats; });
    Check(!capped.drained && capped.waitNs == 30000000000ull && capped.iterations == 30001 && sleeps == 30000,
          "thirty-second maximum wait was not enforced");

    Check(Stats{0, 2, 5}.pipelinePending() == 0 && Stats{0, 2, 5}.idle(),
          "counter snapshot did not saturate a completed/total inversion");
    Check(!Stats{1, 2, 2}.idle(), "empty pipeline counters hid a state-cache feeder item");
    std::puts("DXVK pipeline-drain fake-clock tests passed (idle, two-stage backlog, timeout, partial poll, cap)");
    return 0;
}
