// Futex-based Windows synchronization primitives.
//
// Why this exists: the engine's job system performs millions of WaitForSingleObject/SetEvent/
// ReleaseSemaphore/EnterCriticalSection calls per run. A single global-mutex implementation
// serializes all of them across cores; these primitives replace that with one atomic load when
// the object is already signalled, a bounded adaptive spin, and a futex syscall only to sleep.
//
// Correctness contract (this header froze a real hang: every engine thread parked in
// FastSemaphore::Wait with no thread left to wake them):
//  * Tokens live in the atomic count, never "assigned" to a particular waiter. A release that
//    races a timeout simply leaves its token in the count for the next taker — no compensation
//    arithmetic that can drop one. Waiters consume only with a compare-exchange on a positive
//    count; sleepers park with the value they observed and the kernel revalidates the word at
//    sleep time, so a release landing between the load and the park either wakes the sleeper
//    (FUTEX_WAKE) or leaves the token observable to the recheck. A woken thread never assumes
//    the wake was meant for it: it re-runs the consume loop.
//  * Auto-reset events park on a sequence-tagged word (even = clear, odd = set). Any state
//    change makes every parked expected value stale, so a waiter that parked before a Reset
//    cannot sleep through the next Set; Set wakes exactly one waiter and only when it actually
//    flipped the state, which is what Windows auto-reset semantics require.
//  * Manual-reset events wake all waiters on the setting transition and stay signalled until
//    Reset. Semaphores reject releases beyond the maximum and report the previous count.
//  * Critical sections are recursive, owner-tracked, and use the 0/1/2 free/held/parked word
//    discipline: an arriving thread publishes "parked" by exchanging 2 before it sleeps, so the
//    leaver's FUTEX_WAKE can never fire into an empty futex and lose the handoff.
//
// The header is deliberately free of harness dependencies so it can be unit-tested and
// micro-benchmarked on any Linux host (scripts/test_sync_futex.cpp).
#pragma once

#include <atomic>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <sys/syscall.h>
#include <unistd.h>

#if defined(__linux__)
#include <linux/futex.h>
#endif

#ifndef FUTEX_WAIT_BITSET
#define FUTEX_WAIT_BITSET 9
#endif
#ifndef FUTEX_WAKE
#define FUTEX_WAKE 1
#endif
#ifndef FUTEX_PRIVATE_FLAG
#define FUTEX_PRIVATE_FLAG 128
#endif
#ifndef FUTEX_BITSET_MATCH_ANY
#define FUTEX_BITSET_MATCH_ANY 0xffffffffu
#endif

namespace protodroid {

// The futex syscall operates on 4-byte words; int32_t and uint32_t atomics share the
// representation, so both hand their address to these helpers.
inline long FutexWaitWord(std::atomic<uint32_t>* word, uint32_t expected, const timespec* deadline) {
    return syscall(SYS_futex, reinterpret_cast<uint32_t*>(word),
                   FUTEX_WAIT_BITSET | FUTEX_PRIVATE_FLAG, expected,
                   deadline, nullptr, FUTEX_BITSET_MATCH_ANY);
}

template <typename T>
inline long FutexWaitWord(std::atomic<T>* word, uint32_t expected, const timespec* deadline) {
    return FutexWaitWord(reinterpret_cast<std::atomic<uint32_t>*>(word), expected, deadline);
}

inline long FutexWakeWord(std::atomic<uint32_t>* word, int count) {
    return syscall(SYS_futex, reinterpret_cast<uint32_t*>(word),
                   FUTEX_WAKE | FUTEX_PRIVATE_FLAG, count, nullptr, nullptr, 0);
}

template <typename T>
inline long FutexWakeWord(std::atomic<T>* word, int count) {
    return FutexWakeWord(reinterpret_cast<std::atomic<uint32_t>*>(word), count);
}

// Bounded spin before the first sleep. Cross-core futex wakeups cost tens of microseconds on
// phone SoCs while the engine's job handoffs usually complete in a few hundred nanoseconds, so
// a short spin catches the common case. Overridable at runtime for device tuning.
inline int SyncSpinCount() {
    static const int spins = [] {
        const char* env = getenv("PROTODROID_SYNC_SPIN");
        return env ? atoi(env) : 300;
    }();
    return spins;
}

inline void SpinPause() {
#if defined(__aarch64__)
    asm volatile("yield");
#elif defined(__x86_64__) || defined(__i386__)
    asm volatile("pause");
#endif
}

// Computes the absolute CLOCK_MONOTONIC deadline for a Windows-style timeout.
// Returns false for the two special values that mean "never time out" / "poll only".
inline bool WaitDeadline(uint32_t ms, timespec* out) {
    if (ms == 0xffffffffu) return false;
    clock_gettime(CLOCK_MONOTONIC, out);
    out->tv_sec += ms / 1000;
    out->tv_nsec += long(ms % 1000) * 1000000L;
    if (out->tv_nsec >= 1000000000L) {
        out->tv_nsec -= 1000000000L;
        ++out->tv_sec;
    }
    return true;
}

// Counting semaphore. Wait returns true when a token was consumed. Release adds tokens and
// wakes at most one parked waiter per token added (waking a non-parked thread is harmless: it
// rechecks and either consumes or re-parks).
class FastSemaphore {
public:
    explicit FastSemaphore(int32_t initial = 0, int32_t maximum = INT32_MAX)
        : count_(initial > 0 ? initial : 0), maximum_(maximum > 0 ? maximum : INT32_MAX) {}

    bool Release(int32_t amount, int32_t* previous) {
        if (amount <= 0) return false;
        int32_t c = count_.load(std::memory_order_relaxed);
        for (;;) {
            if (c > maximum_ - amount) return false;  // beyond the maximum: reject, no change
            if (count_.compare_exchange_weak(c, c + amount, std::memory_order_release,
                                             std::memory_order_relaxed)) {
                break;
            }
        }
        if (previous) *previous = c;
        int wakes = amount > kMaxWakesPerRelease ? kMaxWakesPerRelease : static_cast<int>(amount);
        if (wakes > 0) FutexWakeWord(&count_, wakes);
        return true;
    }

    bool Wait(uint32_t ms) {
        const int spins = SyncSpinCount();
        timespec deadline;
        const bool timed = WaitDeadline(ms, &deadline);
        for (;;) {
            int32_t c = count_.load(std::memory_order_acquire);
            for (int i = 0; i < spins; ++i) {
                while (c > 0) {
                    if (count_.compare_exchange_weak(c, c - 1, std::memory_order_acquire,
                                                     std::memory_order_acquire)) {
                        return true;
                    }
                }
                SpinPause();
                c = count_.load(std::memory_order_acquire);
            }
            // Park ONLY on an empty semaphore. A token observed now must be retried instead:
            // parking on a positive count sleeps forever, because Release wakes only on new
            // transitions and nothing else will ever look at this sleeper again. (Both of
            // these park-on-available bugs are what the host stress tests caught; the second
            // one is the shape that froze every engine thread in the field.)
            if (c > 0) continue;
            if (timed) {
                timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                if (now.tv_sec > deadline.tv_sec ||
                    (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
                    return false;  // the token, if any, stays in count_ for the next taker
                }
                FutexWaitWord(&count_, uint32_t(c), &deadline);
            } else {
                FutexWaitWord(&count_, uint32_t(c), nullptr);
            }
        }
    }

    int32_t LoadForDiagnostics() const { return count_.load(std::memory_order_relaxed); }

    // Single non-blocking consume attempt for the generic wait path (WaitForMultipleObjects
    // under g_kernel_mutex, which has already checked Signaled()).
    bool TryConsume() {
        int32_t c = count_.load(std::memory_order_acquire);
        for (;;) {
            if (c <= 0) return false;
            if (count_.compare_exchange_weak(c, c - 1, std::memory_order_acquire,
                                             std::memory_order_acquire)) {
                return true;
            }
        }
    }

private:
    static constexpr int kMaxWakesPerRelease = 32;
    std::atomic<int32_t> count_;
    const int32_t maximum_;
};

// Event, Windows semantics. The state word packs a sequence number and the signalled bit
// (bit 0): every transition bumps the sequence, so a parked waiter's expected value goes stale
// on ANY change and the kernel revalidates before sleeping.
class FastEvent {
public:
    FastEvent(bool manual, bool initial) : manual_(manual) {
        state_.store(initial ? 1u : 0u, std::memory_order_release);
    }

    void Set() {
        uint32_t v = state_.load(std::memory_order_acquire);
        for (;;) {
            if (v & 1u) return;  // already set: Set is a no-op, and no valid sleeper waits on set
            if (state_.compare_exchange_weak(v, v + 1, std::memory_order_release,
                                             std::memory_order_acquire)) {
                FutexWakeWord(&state_, manual_ ? INT_MAX : 1);
                return;
            }
        }
    }

    void Reset() {
        uint32_t v = state_.load(std::memory_order_acquire);
        for (;;) {
            if (!(v & 1u)) return;
            if (state_.compare_exchange_weak(v, v + 1, std::memory_order_release,
                                             std::memory_order_acquire)) {
                return;
            }
        }
    }

    bool Wait(uint32_t ms) {
        const int spins = SyncSpinCount();
        timespec deadline;
        const bool timed = WaitDeadline(ms, &deadline);
        for (;;) {
            uint32_t v = state_.load(std::memory_order_acquire);
            for (int i = 0; i < spins; ++i) {
                if (v & 1u) {
                    if (manual_) return true;  // stays signalled; nothing to consume
                    if (state_.compare_exchange_weak(v, v + 1, std::memory_order_acquire,
                                                     std::memory_order_acquire)) {
                        return true;  // auto-reset: this waiter consumed the signal
                    }
                }
                SpinPause();
                v = state_.load(std::memory_order_acquire);
            }
            // Park ONLY on a clear state (auto-reset): parking with an odd expected value
            // sleeps through every future Set, because Set treats "already set" as a no-op
            // and wakes nobody. Retry the consume instead.
            if (v & 1u) continue;
            if (timed) {
                timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                if (now.tv_sec > deadline.tv_sec ||
                    (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
                    return false;
                }
                FutexWaitWord(&state_, v, &deadline);
            } else {
                FutexWaitWord(&state_, v, nullptr);
            }
        }
    }

    bool IsSetForDiagnostics() const { return (state_.load(std::memory_order_relaxed) & 1u) != 0; }

private:
    std::atomic<uint32_t> state_;
    const bool manual_;
};

// Recursive, owner-tracked critical section with no timeout. The lock word is 0 when free and
// nonzero when held; a thread about to sleep exchanges it to 2 first, so the leaver sees 2 and
// wakes exactly one parker — the handoff cannot be lost to a thread that has not slept yet,
// because such a thread re-runs the exchange loop after its futex returns.
class FastCriticalSection {
public:
    void Enter() {
        const uint32_t self = SelfTid();
        const int spins = SyncSpinCount();
        for (;;) {
            if (lock_.exchange(2, std::memory_order_acquire) == 0) {
                owner_.store(self, std::memory_order_relaxed);
                depth_.store(1, std::memory_order_relaxed);
                return;
            }
            for (int i = 0; i < spins; ++i) {
                if (owner_.load(std::memory_order_relaxed) == self) {
                    // We hold it (the exchange above re-marked 2; that is the same state).
                    depth_.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
                if (lock_.exchange(2, std::memory_order_acquire) == 0) {
                    owner_.store(self, std::memory_order_relaxed);
                    depth_.store(1, std::memory_order_relaxed);
                    return;
                }
                SpinPause();
            }
            FutexWaitWord(&lock_, 2, nullptr);
        }
    }

    bool TryEnter() {
        const uint32_t self = SelfTid();
        if (owner_.load(std::memory_order_relaxed) == self &&
            lock_.load(std::memory_order_relaxed) != 0) {
            depth_.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        if (lock_.exchange(2, std::memory_order_acquire) == 0) {
            owner_.store(self, std::memory_order_relaxed);
            depth_.store(1, std::memory_order_relaxed);
            return true;
        }
        return false;
    }

    void Leave() {
        const int depth = depth_.load(std::memory_order_relaxed);
        if (depth > 1) {
            depth_.store(depth - 1, std::memory_order_relaxed);
            return;
        }
        owner_.store(0, std::memory_order_relaxed);
        depth_.store(0, std::memory_order_relaxed);
        if (lock_.exchange(0, std::memory_order_release) == 2) FutexWakeWord(&lock_, 1);
    }

private:
    static uint32_t SelfTid() { return static_cast<uint32_t>(gettid()); }
    std::atomic<uint32_t> lock_{0};
    std::atomic<uint32_t> owner_{0};
    std::atomic<int> depth_{0};
};

}  // namespace protodroid
