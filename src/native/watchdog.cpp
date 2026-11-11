// Hang diagnostics: when the main loop stops presenting frames for a while, every guest thread is interrupted with a
// signal and logs its native backtrace. The recompiled functions keep their f_<guest address> names in the dynamic
// symbol table, so the log shows where in the engine each thread is (loop, wait, ...). Setting watchdog=0 disables
// it; watchdog_seconds sets the delay (default 10).
#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <unwind.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "diagnostic_tour.h"
#include "guest.h"

namespace native {

namespace {

struct Trace {
    void* pcs[32];
    int n;
};

// One slot per guest thread. The signal handler only writes raw program counters into its own slot: it must not take a
// lock, call malloc or log, because the interrupted thread may hold the log mutex when the signal arrives (the previous
// handler called Log(), which deadlocked exactly that way). The watchdog thread formats the captured trace afterwards.
constexpr int kMaxWatched = 128;
struct Slot {
    std::atomic<int> tid{-1};
    Trace trace{};
    std::atomic<bool> ready{false};
};
Slot g_slots[kMaxWatched];
std::atomic<int> g_slots_used{0};
std::atomic<uint64_t>* g_frames = nullptr;
std::atomic<bool> g_paused{false};

_Unwind_Reason_Code Collect(struct _Unwind_Context* ctx, void* arg) {
    auto* t = static_cast<Trace*>(arg);
    uintptr_t pc = _Unwind_GetIP(ctx);
    if (pc && t->n < 24) t->pcs[t->n++] = reinterpret_cast<void*>(pc);
    return t->n < 24 ? _URC_NO_REASON : _URC_END_OF_STACK;
}

Slot* SlotForTid(int tid) {
    const int used = g_slots_used.load(std::memory_order_acquire);
    for (int i = 0; i < used; ++i) {
        if (g_slots[i].tid.load(std::memory_order_acquire) == tid) return &g_slots[i];
    }
    return nullptr;
}

// Runs in signal context: no locks, no allocation, no formatting.
void OnSignal(int) {
    Slot* slot = SlotForTid(int(gettid()));
    if (!slot) return;
    Trace t{};
    _Unwind_Backtrace(Collect, &t);
    slot->trace = t;
    slot->ready.store(true, std::memory_order_release);
}

// Formats a captured trace and logs it; runs on the watchdog thread, never in a signal.
void ReportTrace(int tid, const Trace& t) {
    char line[2048];
    int len = snprintf(line, sizeof(line), "watchdog: thread %d:", tid);
    for (int i = 2; i < t.n && len < int(sizeof(line)) - 64; ++i) {  // skip the handler and the signal trampoline
        Dl_info info;
        const char* guest_name = RecompNameForAddress(reinterpret_cast<uintptr_t>(t.pcs[i]));
        if (guest_name) {
            len += snprintf(line + len, sizeof(line) - size_t(len), " %s", guest_name);
        } else if (dladdr(t.pcs[i], &info) && info.dli_sname) {
            len += snprintf(line + len, sizeof(line) - size_t(len), " %s+%#lx", info.dli_sname,
                            static_cast<unsigned long>(reinterpret_cast<uintptr_t>(t.pcs[i]) -
                                                       reinterpret_cast<uintptr_t>(info.dli_saddr)));
        } else {
            len += snprintf(line + len, sizeof(line) - size_t(len), " %p", t.pcs[i]);
        }
    }
    Log("%s", line);
}

void Run(int seconds) {
    uint64_t last = g_frames->load();
    int still = 0;
    bool reported = false;
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        uint64_t now = g_frames->load();
        if (now != last) {
            last = now;
            still = 0;
            reported = false;
            continue;
        }
        if (++still < seconds || reported) continue;
        if (g_paused.load(std::memory_order_relaxed)) continue;  // paused, backgrounded or playing a movie
        reported = true;
        Log("watchdog: no frame for %d s (frame %llu), guest thread backtraces:", seconds, (unsigned long long)now);
        const int used = g_slots_used.load(std::memory_order_acquire);
        for (int i = 0; i < used; ++i) {
            Slot& slot = g_slots[i];
            const int tid = slot.tid.load(std::memory_order_acquire);
            if (tid < 0) continue;  // thread has exited and its slot was released
            slot.ready.store(false, std::memory_order_release);
            tgkill(getpid(), tid, SIGUSR2);  // by tid: a thread that exited meanwhile is just skipped (ESRCH)
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            if (slot.ready.load(std::memory_order_acquire)) ReportTrace(tid, slot.trace);
        }
        // Capturing a long backtrace must not fail a tour that recovered meanwhile. Ordinary Play returns immediately.
        if (now == g_frames->load()) DiagnosticTourWatchdogStalled(now, seconds);
    }
}

}  // namespace

void RegisterGuestThread() {
    const int slot = g_slots_used.load(std::memory_order_acquire);
    if (slot >= kMaxWatched) return;
    g_slots[slot].tid.store(int(gettid()), std::memory_order_release);
    g_slots[slot].ready.store(false, std::memory_order_release);
    g_slots_used.store(std::max(slot + 1, g_slots_used.load(std::memory_order_relaxed)),
                       std::memory_order_release);
}

// Item 27: threads used to be registered forever, so a watchdog that fired late signalled thread ids the kernel had
// already recycled into unrelated threads (HWUI, Binder). Releasing the slot on exit removes both the stale signals and
// the unbounded growth of the list.
void UnregisterGuestThread() {
    Slot* slot = SlotForTid(int(gettid()));
    if (!slot) return;
    slot->ready.store(false, std::memory_order_release);
    slot->tid.store(-1, std::memory_order_release);
}

// Loading screens, Bink movies and a backgrounded app legitimately present no frames.
void SetWatchdogPaused(bool paused) { g_paused.store(paused, std::memory_order_relaxed); }

void StartWatchdog(std::atomic<uint64_t>* frames) {
    if (!SettingInt("watchdog", 1)) return;
    g_frames = frames;
    struct sigaction sa {};
    sa.sa_handler = OnSignal;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR2, &sa, nullptr);
    int seconds = SettingInt("watchdog_seconds", 10);
    std::thread([seconds] { Run(seconds); }).detach();
}

}  // namespace native
