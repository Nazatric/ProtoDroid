// Host-side validation of src/native/sync_futex.h: Windows semantics under contention plus a
// ping-pong micro-benchmark against the old design (one global mutex + condition variables,
// which is what kernel32.cpp used for every event/semaphore/wait).
// Build & run: g++ -O2 -std=c++17 -pthread scripts/test_sync_futex.cpp -o /tmp/test_sync && /tmp/test_sync
#include "../native/sync_futex.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

using protodroid::FastCriticalSection;
using protodroid::FastEvent;
using protodroid::FastSemaphore;
using Clock = std::chrono::steady_clock;

static int g_failures = 0;
#define CHECK(cond, ...)                                        \
    do {                                                        \
        if (!(cond)) {                                          \
            ++g_failures;                                       \
            std::printf("FAIL %s:%d ", __FILE__, __LINE__);     \
            std::printf(__VA_ARGS__);                           \
            std::printf("\n");                                  \
        }                                                       \
    } while (0)

static void TestAutoResetExactlyOne() {
    FastEvent e(false, false);
    std::atomic<int> woken{0};
    std::atomic<int> started{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i)
        threads.emplace_back([&] {
            started.fetch_add(1);
            if (e.Wait(0xffffffffu) == protodroid::kWaitObject0) woken.fetch_add(1);
        });
    while (started.load() < 8) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    e.Set();
    e.Set();  // two signals while signalled state is 1: collapses to one (Windows auto-reset)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // Collapse is expected when both Sets land before the first waiter consumes, but the
    // waiter may consume in between on a loaded machine — accept either, then settle to 2.
    CHECK(woken.load() >= 1 && woken.load() <= 2, "auto-reset woke %d after two sets", woken.load());
    for (int guard = 0; woken.load() < 2 && guard < 1000; ++guard) {
        e.Set();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(woken.load() == 2, "auto-reset woke %d after settling, expected 2", woken.load());
    for (int guard = 0; woken.load() < 8 && guard < 10000; ++guard) {
        e.Set();  // rapid Sets can collapse on an already-signalled event; keep setting
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(woken.load() == 8, "drain after checks woke %d/8", woken.load());
    for (auto& t : threads) t.join();
}

static void TestManualResetWakesAll() {
    FastEvent e(true, false);
    std::atomic<int> woken{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 6; ++i)
        threads.emplace_back([&] {
            if (e.Wait(0xffffffffu) == protodroid::kWaitObject0) woken.fetch_add(1);
        });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    e.Set();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(woken.load() == 6, "manual-reset woke %d, expected 6", woken.load());
    e.Reset();
    CHECK(e.Wait(0) == protodroid::kWaitTimeout, "reset event must not be signalled");
    for (auto& t : threads) t.join();  // all returned already; join for cleanliness
}

static void TestAutoResetDrain() {
    // Windows collapses rapid Sets on an already-signalled auto-reset event, so the drain loop
    // must keep setting until every waiter has returned rather than assume one wake per Set.
    FastEvent e(false, false);
    std::atomic<int> woken{0};
    std::atomic<int> started{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i)
        threads.emplace_back([&] {
            started.fetch_add(1);
            if (e.Wait(0xffffffffu) == protodroid::kWaitObject0) woken.fetch_add(1);
        });
    while (started.load() < 8) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    for (int guard = 0; woken.load() < 8 && guard < 10000; ++guard) {
        e.Set();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(woken.load() == 8, "drain woke %d/8", woken.load());
    for (auto& t : threads) t.join();
}

static void TestSemaphoreCounting() {
    FastSemaphore s(0, 4);
    CHECK(!s.Release(5), "release beyond max must fail");
    CHECK(s.Release(2), "release within max must succeed");
    int32_t prev = -1;
    CHECK(s.Release(2, &prev) && prev == 2, "previous count must be 2, got %d", prev);
    CHECK(s.Wait(0) == protodroid::kWaitObject0, "wait must succeed with count 4");
    CHECK(s.TryAcquire(), "TryAcquire must succeed with units left");  // consumes one; 2 remain
    for (int i = 0; i < 2; ++i) CHECK(s.Wait(0) == protodroid::kWaitObject0, "drain %d", i);
    CHECK(s.Wait(0) == protodroid::kWaitTimeout, "empty semaphore must time out on poll");

    // Stress: 4 producers x 25000 single releases vs 4 consumers, exact conservation.
    FastSemaphore q(0, INT32_MAX);
    std::atomic<int64_t> produced{0}, consumed{0};
    std::vector<std::thread> ps, cs;
    for (int i = 0; i < 4; ++i)
        ps.emplace_back([&] {
            for (int k = 0; k < 25000; ++k)
                if (q.Release(1)) produced += 1;
        });
    for (int i = 0; i < 4; ++i)
        cs.emplace_back([&] {
            while (consumed.load() < 100000) {
                if (q.Wait(200) == protodroid::kWaitObject0) consumed.fetch_add(1);
            }
        });
    for (auto& t : ps) t.join();
    for (auto& t : cs) t.join();
    CHECK(produced.load() == consumed.load(), "semaphore lost units: produced %lld consumed %lld",
          (long long)produced.load(), (long long)consumed.load());
}

static void TestTimeoutAccuracy() {
    FastEvent e(false, false);
    auto t0 = Clock::now();
    uint32_t r = e.Wait(60);
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();
    CHECK(r == protodroid::kWaitTimeout, "expected timeout, got 0x%x", r);
    CHECK(us >= 55000 && us <= 200000, "60ms wait took %.1fms", us / 1000.0);
}

static void TestCriticalSection() {
    // Recursion and ownership.
    FastCriticalSection cs;
    cs.Initialize();
    cs.Enter(0x11);
    cs.Enter(0x11);
    cs.Enter(0x11);
    CHECK(cs.TryEnter(0x11), "owner TryEnter must succeed");
    CHECK(!cs.TryEnter(0x22), "other thread TryEnter must fail while held");
    cs.Leave();
    cs.Leave();
    cs.Leave();
    cs.Leave();
    CHECK(cs.TryEnter(0x22), "must be free after full unwind");
    cs.Leave();
    CHECK(cs.lock.load() == 0 && cs.owner.load() == 0, "lock word and owner must be zero after unwind");

    // Contended stress: 8 threads x 200k increments, exact final count.
    std::atomic<uint64_t> counter{0};
    std::atomic<bool> stop{false};
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t)
        threads.emplace_back([&, t] {
            const uint32_t tid = 0x100 + t;
            for (int i = 0; i < 200000; ++i) {
                cs.Enter(tid);
                counter.fetch_add(1, std::memory_order_relaxed);
                if (i % 7 == 0) cs.Enter(tid);  // occasional recursion
                if (i % 7 == 0) cs.Leave();
                cs.Leave();
            }
        });
    for (auto& th : threads) th.join();
    CHECK(counter.load() == 8ull * 200000, "CS lost updates: %llu", (unsigned long long)counter.load());
}

// ---- benchmark: the old design (global mutex + per-thread cv) vs the futex primitives ----
struct OldEvent {
    std::mutex mtx;
    std::condition_variable cv;
    bool state = false;
    void Set() {
        {
            std::lock_guard<std::mutex> lock(mtx);
            state = true;
        }
        cv.notify_one();
    }
    void Wait() {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&] { return state; });
        state = false;
    }
};

static double PingPongOld() {
    OldEvent a, b;
    const int kTrips = 20000;
    std::thread responder([&] {
        for (int i = 0; i < kTrips; ++i) {
            a.Wait();
            b.Set();
        }
    });
    auto t0 = Clock::now();
    for (int i = 0; i < kTrips; ++i) {
        a.Set();
        b.Wait();
    }
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();
    responder.join();
    return double(us) / kTrips;
}

static double PingPongNew() {
    FastEvent a(false), b(false);
    const int kTrips = 20000;
    std::thread responder([&] {
        for (int i = 0; i < kTrips; ++i) {
            a.Wait(0xffffffffu);
            b.Set();
        }
    });
    auto t0 = Clock::now();
    for (int i = 0; i < kTrips; ++i) {
        a.Set();
        b.Wait(0xffffffffu);
    }
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();
    responder.join();
    return double(us) / kTrips;
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    struct Named { const char* name; void (*fn)(); } tests[] = {
        {"auto", TestAutoResetExactlyOne},
        {"drain", TestAutoResetDrain},
        {"manual", TestManualResetWakesAll},
        {"semaphore", TestSemaphoreCounting},
        {"timeout", TestTimeoutAccuracy},
        {"cs", TestCriticalSection},
    };
    for (const Named& t : tests) {
        if (argc > 1 && std::string(argv[1]) != t.name) continue;
        std::printf("running %s...\n", t.name);
        t.fn();
        std::printf("%s done\n", t.name);
    }
    if (argc > 1) return g_failures == 0 ? 0 : 1;
    const double old_us = PingPongOld();
    std::printf("ping-pong old done: %.2f us\n", old_us);
    const double new_us = PingPongNew();
    std::printf("ping-pong handoff: old (mutex+cv) %.2f us  |  futex %.2f us  (%.1fx)\n",
                old_us, new_us, old_us / new_us);
    if (g_failures == 0) std::printf("ALL SEMANTIC TESTS PASSED\n");
    else std::printf("%d FAILURES\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
