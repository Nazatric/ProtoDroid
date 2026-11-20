// Host stress test for the futex primitives in sync_futex.h.
//
// Why this exists: the engine's job system parks and wakes worker threads millions of times per run, and a single lost
// wakeup freezes the game with every worker asleep and the main thread waiting for a job that will never run. The
// header has already frozen a real hang once (every engine thread parked in FastSemaphore::Wait with nobody left to
// wake them), and a device log cannot tell us which shape it hit. Every case here uses a bounded timeout and counts a
// timeout as a failure instead of hanging, so the test either proves the handoff or names the primitive.
//
// The handoff cases use a ring with exactly one token in flight: a thread waits on its own primitive and only then
// signals the next one. A thread that signals and then waits on the same auto-reset primitive can consume its own
// signal, which looks like a lost wakeup but is not - that harness mistake produced false reports once, so the shape
// here is deliberately one-directional.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

#include "sync_futex.h"

namespace {

using protodroid::FastCriticalSection;
using protodroid::FastEvent;
using protodroid::FastSemaphore;

int g_failures = 0;

void Fail(const char* what, int threads, long long value) {
    std::fprintf(stderr, "FAIL: %s (threads=%d value=%lld)\n", what, threads, value);
    ++g_failures;
}

// A ring of `threads` workers, one token, `rounds` laps: worker 0 starts with the token, every worker waits on its own
// primitive and then signals the next. Any lost wakeup leaves one worker asleep and the wait times out.
template <typename WaitFn, typename SignalFn>
bool RingHandoff(int threads, long long rounds, const char* what, WaitFn wait_i, SignalFn signal_i) {
    std::atomic<bool> stop{false};
    std::atomic<bool> failed{false};
    std::vector<std::thread> pool;
    for (int i = 1; i < threads; ++i) {
        pool.emplace_back([&, i] {
            for (;;) {
                if (!wait_i(i)) {
                    Fail(what, threads, 0);
                    failed.store(true, std::memory_order_release);
                    return;
                }
                if (stop.load(std::memory_order_acquire)) return;
                signal_i((i + 1) % threads);
            }
        });
    }
    // The main thread is slot 0: it takes the token, hands it on, and repeats, so exactly one token is ever in flight.
    for (long long round = 0; round < rounds; ++round) {
        if (!wait_i(0)) {
            Fail(what, threads, round);
            failed.store(true, std::memory_order_release);
            break;
        }
        signal_i(1 % threads);
    }
    // Stop every worker that is parked: one token per ring slot, then each worker exits after its wait returns.
    stop.store(true, std::memory_order_release);
    for (int i = 1; i < threads; ++i) signal_i(i);
    for (auto& t : pool) t.join();
    return !failed.load();
}

void SemaphoreRing(int threads, long long rounds) {
    // The primitives hold atomics, so they are not movable: build them in place instead of assigning into an array.
    FastSemaphore* tokens[8] = {};
    for (int i = 0; i < threads; ++i) tokens[i] = new FastSemaphore(i == 0 ? 1 : 0, INT32_MAX);
    struct Free {
        FastSemaphore** slots;
        int n;
        ~Free() {
            for (int i = 0; i < n; ++i) delete slots[i];
        }
    } free_slots{tokens, threads};
    RingHandoff(
        threads, rounds, "semaphore ring handoff",
        [&](int i) { return tokens[i]->Wait(2000); },
        [&](int i) { tokens[i]->Release(1, nullptr); });
}

void EventRing(int threads, long long rounds) {
    FastEvent* events[8] = {};
    for (int i = 0; i < threads; ++i) events[i] = new FastEvent(false, i == 0);
    struct Free {
        FastEvent** slots;
        int n;
        ~Free() {
            for (int i = 0; i < n; ++i) delete slots[i];
        }
    } free_slots{events, threads};
    RingHandoff(
        threads, rounds, "auto-reset event ring handoff",
        [&](int i) { return events[i]->Wait(2000); },
        [&](int i) { events[i]->Set(); });
}

// Many producers and consumers on one counting semaphore: no token may be lost, duplicated, or left unclaimed.
void ProducerConsumer(int threads) {
    constexpr int kPerProducer = 50000;
    const int producers = threads / 2;
    const int consumers = threads - producers;
    const long long items = static_cast<long long>(producers) * kPerProducer;
    FastSemaphore work(0, INT32_MAX);
    FastSemaphore done(0, INT32_MAX);
    std::atomic<long long> consumed{0};
    std::atomic<bool> stop{false};
    std::vector<std::thread> producer_pool;
    std::vector<std::thread> consumer_pool;
    for (int p = 0; p < producers; ++p) {
        producer_pool.emplace_back([&] {
            for (int i = 0; i < kPerProducer; ++i) {
                if (!work.Release(1, nullptr)) Fail("producer release rejected", threads, i);
            }
        });
    }
    for (int c = 0; c < consumers; ++c) {
        consumer_pool.emplace_back([&] {
            for (;;) {
                if (!work.Wait(2000)) {
                    Fail("consumer token wait timed out", threads, consumed.load());
                    return;
                }
                // The exit tokens below are not items: once every item has been taken and harvested, a wakeup only
                // means "go home", so it must not be counted or it would look like a duplicated token.
                if (stop.load(std::memory_order_acquire)) return;
                const long long taken = consumed.fetch_add(1, std::memory_order_acq_rel) + 1;
                if (!done.Release(1, nullptr)) Fail("consumer release rejected", threads, taken);
            }
        });
    }
    for (auto& t : producer_pool) t.join();
    for (long long taken = 0; taken < items;) {
        if (!done.Wait(2000)) {
            Fail("done token wait timed out", threads, taken);
            break;
        }
        taken = consumed.load(std::memory_order_acquire);
    }
    stop.store(true, std::memory_order_release);
    for (int c = 0; c < consumers; ++c) work.Release(1, nullptr);  // one exit token per parked consumer
    for (auto& t : consumer_pool) t.join();
    if (consumed.load() != items) Fail("producer/consumer lost or duplicated tokens", threads, consumed.load());
}

// A manual-reset event must wake every parked waiter and stay signalled until Reset.
void ManualResetBroadcast(int waiters) {
    for (int attempt = 0; attempt < 20; ++attempt) {
        FastEvent event(true, false);
        std::atomic<int> woke{0};
        std::vector<std::thread> pool;
        for (int i = 0; i < waiters; ++i) {
            pool.emplace_back([&] {
                if (!event.Wait(2000)) {
                    Fail("manual-reset waiter timed out", waiters, attempt);
                    return;
                }
                woke.fetch_add(1, std::memory_order_acq_rel);
            });
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        event.Set();
        for (auto& t : pool) t.join();
        if (woke.load() != waiters) Fail("manual-reset event did not wake every waiter", waiters, woke.load());
        if (!event.IsSetForDiagnostics()) Fail("manual-reset event cleared itself", waiters, 0);
        event.Reset();
        if (event.Wait(0)) Fail("manual-reset event stayed signalled after Reset", waiters, 0);
    }
}

// Auto-reset: one signal wakes exactly one waiter, the event clears, and the next signal wakes the next waiter.
void AutoResetOneWaiter(int waiters) {
    for (int attempt = 0; attempt < 50; ++attempt) {
        FastEvent event(false, false);
        std::atomic<int> woke{0};
        std::atomic<bool> go{false};
        std::vector<std::thread> pool;
        for (int i = 0; i < waiters; ++i) {
            pool.emplace_back([&] {
                // Each waiter keeps waiting until it has consumed exactly one signal.
                while (!go.load(std::memory_order_acquire)) {
                    if (!event.Wait(2000)) {
                        Fail("auto-reset waiter timed out", waiters, attempt);
                        return;
                    }
                    go.store(true, std::memory_order_release);
                    woke.fetch_add(1, std::memory_order_acq_rel);
                }
            });
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        event.Set();
        // Wait for the single wakeup to land, then signal as many times as there are waiters left.
        for (int spin = 0; spin < 2000 && woke.load() == 0; ++spin) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (woke.load() != 1) Fail("auto-reset event did not wake exactly one waiter", waiters, woke.load());
        for (int extra = 0; extra < waiters; ++extra) {
            event.Set();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        for (auto& t : pool) t.join();
        if (woke.load() != waiters) Fail("auto-reset event lost a waiter's signal", waiters, woke.load());
    }
}

// Recursive, owner-tracked critical section under contention: no update may be lost and re-entry must not deadlock.
void CriticalSectionContention(int threads) {
    constexpr int kPerThread = 20000;
    FastCriticalSection cs;
    long long counter = 0;
    std::vector<std::thread> pool;
    for (int i = 0; i < threads; ++i) {
        pool.emplace_back([&] {
            for (int k = 0; k < kPerThread; ++k) {
                cs.Enter();
                cs.Enter();  // recursive
                ++counter;
                cs.Leave();
                cs.Leave();
            }
        });
    }
    for (auto& t : pool) t.join();
    if (counter != static_cast<long long>(threads) * kPerThread) Fail("critical section lost updates", threads, counter);
}

// Windows rejects a release beyond the maximum count. Silently clamping would let the engine post work that it can
// never observe, which is one of the shapes a job-system stall takes.
void ReleaseBeyondMaximum() {
    FastSemaphore s(0, 2);
    int32_t previous = -1;
    if (!s.Release(2, &previous) || previous != 0) Fail("release to the maximum rejected", 1, previous);
    if (s.Release(1, &previous)) Fail("release beyond the maximum accepted", 1, previous);
    if (s.LoadForDiagnostics() != 2) Fail("rejected release changed the count", 1, s.LoadForDiagnostics());
    if (!s.Wait(0) || !s.Wait(0)) Fail("released token not consumable", 1, 0);
    if (s.Wait(0)) Fail("consumed more tokens than were released", 1, 0);
}

}  // namespace

int main() {
    SemaphoreRing(2, 100000);
    SemaphoreRing(8, 20000);
    EventRing(2, 100000);
    EventRing(8, 20000);
    ProducerConsumer(8);
    ManualResetBroadcast(8);
    AutoResetOneWaiter(4);
    CriticalSectionContention(8);
    ReleaseBeyondMaximum();
    if (g_failures) {
        std::fprintf(stderr, "%d futex synchronization failure(s)\n", g_failures);
        return 1;
    }
    std::printf("futex synchronization stress passed: ring handoff, producer/consumer, broadcast, auto-reset, critical "
                "section, and maximum-count checks\n");
    return 0;
}
