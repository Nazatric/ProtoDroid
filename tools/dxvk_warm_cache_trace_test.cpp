#include "d3d9_warm_cache_trace.h"
#include "dxvk_pipeline_drain.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

namespace {

using Trace = dxvk::D3D9WarmCacheTrace;
using Snapshot = dxvk::DxvkWarmCacheSnapshot;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
  }
}

}  // namespace

int main() {
  Trace::State disabled;
  unsigned clocks = 0;
  unsigned reads = 0;
  unsigned emits = 0;
  Trace::MaybeLog(disabled, false, [&] { ++clocks; return int64_t(0); },
                  [&] { ++reads; return 1; }, [&](int, int64_t) { ++emits; });
  Check(clocks == 0 && reads == 0 && emits == 0 &&
            disabled.nextLogNs.load(std::memory_order_relaxed) == 0,
        "disabled poll read the clock, queue, emitter, or scheduler state");

  Trace::State state;
  int64_t now = 100;
  Snapshot snapshot;
  snapshot.stateCacheKnown = false;
  snapshot.stats = {9, 5, 7};
  int64_t emittedAt = -1;
  Snapshot emitted;
  Trace::MaybeLog(state, true, [&] { ++clocks; return now; }, [&] { ++reads; return snapshot; },
                  [&](const Snapshot& value, int64_t atNs) {
                    ++emits;
                    emittedAt = atNs;
                    emitted = value;
                  });
  Check(clocks == 1 && reads == 1 && emits == 1 && emittedAt == 100,
        "initial enabled poll did not emit exactly one timestamped snapshot");
  Check(!emitted.stateCacheKnown && emitted.stats.stateCachePending == 9 &&
            emitted.stats.pipelineTasksTotal == 5 && emitted.stats.pipelineTasksCompleted == 7 &&
            emitted.stats.pipelinePending() == 0,
        "snapshot changed an explicit unknown or raw/inverted pipeline counters");
  Check(state.nextLogNs.load(std::memory_order_relaxed) == 100 + Trace::kIntervalNs,
        "initial poll did not reserve the next 250 ms window");

  now = 100 + Trace::kIntervalNs - 1;
  Trace::MaybeLog(state, true, [&] { ++clocks; return now; }, [&] { ++reads; return snapshot; },
                  [&](const Snapshot&, int64_t) { ++emits; });
  Check(clocks == 2 && reads == 1 && emits == 1, "pre-deadline poll read or emitted queue state");

  now = 100 + Trace::kIntervalNs;
  Trace::MaybeLog(state, true, [&] { ++clocks; return now; }, [&] { ++reads; return snapshot; },
                  [&](const Snapshot& value, int64_t atNs) {
                    ++emits;
                    emittedAt = atNs;
                    emitted = value;
                  });
  Check(clocks == 3 && reads == 2 && emits == 2 && emittedAt == now,
        "exact deadline did not produce the second snapshot");

  now -= 1000;
  Trace::MaybeLog(state, true, [&] { ++clocks; return now; }, [&] { ++reads; return snapshot; },
                  [&](const Snapshot&, int64_t) { ++emits; });
  Check(clocks == 4 && reads == 2 && emits == 2, "clock regression fabricated a due snapshot");

  now = 100 + 20 * Trace::kIntervalNs;
  Trace::MaybeLog(state, true, [&] { ++clocks; return now; }, [&] { ++reads; return snapshot; },
                  [&](const Snapshot&, int64_t atNs) {
                    ++emits;
                    emittedAt = atNs;
                  });
  Check(clocks == 5 && reads == 3 && emits == 3 && emittedAt == now &&
            state.nextLogNs.load(std::memory_order_relaxed) == now + Trace::kIntervalNs,
        "long pause caused catch-up bursts or a stale next deadline");

  Trace::State concurrent;
  std::atomic<unsigned> concurrentReads{0};
  std::atomic<unsigned> concurrentEmits{0};
  std::atomic<unsigned> concurrentClocks{0};
  std::vector<std::thread> threads;
  for (unsigned i = 0; i < 12; ++i) {
    threads.emplace_back([&] {
      Trace::MaybeLog(concurrent, true, [&] { ++concurrentClocks; return int64_t(1000000000); },
                      [&] { ++concurrentReads; return 7; }, [&](int value, int64_t atNs) {
                        Check(value == 7 && atNs == 1000000000, "concurrent snapshot metadata changed");
                        ++concurrentEmits;
                      });
    });
  }
  for (auto& thread : threads)
    thread.join();
  Check(concurrentClocks == 12 && concurrentReads == 1 && concurrentEmits == 1,
        "concurrent Present polls did not elect exactly one queue reader");

  Check(Trace::NextDue(0) == Trace::kIntervalNs, "ordinary due interval changed");
  Check(Trace::NextDue(std::numeric_limits<int64_t>::max() - 1) ==
            std::numeric_limits<int64_t>::max(),
        "due-time addition did not saturate at int64 maximum");

  std::puts("DXVK warm-cache trace fake-clock tests passed "
            "(disabled, cadence, regression, long pause, raw unknowns, contention, saturation)");
  return 0;
}
