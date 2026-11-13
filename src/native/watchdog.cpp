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

struct Registered {
    pthread_t handle;
    int tid;
};
std::mutex g_mutex;
std::vector<Registered> g_threads;
std::atomic<uint64_t>* g_frames = nullptr;

struct Trace {
    void* pcs[24];
    int n;
};

_Unwind_Reason_Code Collect(struct _Unwind_Context* ctx, void* arg) {
    auto* t = static_cast<Trace*>(arg);
    uintptr_t pc = _Unwind_GetIP(ctx);
    if (pc && t->n < 24) t->pcs[t->n++] = reinterpret_cast<void*>(pc);
    return t->n < 24 ? _URC_NO_REASON : _URC_END_OF_STACK;
}

void OnSignal(int) {
    Trace t{};
    _Unwind_Backtrace(Collect, &t);
    char line[2048];
    int len = snprintf(line, sizeof(line), "watchdog: thread %d:", int(gettid()));
    for (int i = 2; i < t.n && len < int(sizeof(line)) - 64; ++i) {  // skip the handler and the signal trampoline
        Dl_info info;
        if (dladdr(t.pcs[i], &info) && info.dli_sname) {
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
    int sample = 0;
    int next_report = seconds;
    uint64_t calls = GuestCallTotal();
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        uint64_t now = g_frames->load();
        const uint64_t calls_now = GuestCallTotal();
        const uint64_t calls_per_second = calls_now - calls;
        calls = calls_now;
        if (now != last) {
            last = now;
            still = 0;
            sample = 0;
            next_report = seconds;
            continue;
        }
        if (++still < next_report) continue;
        // Repeat while the stall lasts, slower each time. One snapshot cannot distinguish "stuck in one place" from
        // "executing far too slowly" (a loading script that never yields); consecutive snapshots with the same
        // symbols and the same frame count prove the first, different positions prove the second. A single snapshot
        // is all a frozen app used to log, because the app was closed before the next report was due.
        next_report = still + seconds * (sample == 0 ? 3 : 6);
        ++sample;
        Log("watchdog: no frame for %d s (frame %llu), sample %d, %llu guest calls/s, guest thread backtraces:",
            still, (unsigned long long)now, sample, (unsigned long long)calls_per_second);
        std::vector<Registered> threads;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            threads = g_threads;
        }
        for (const Registered& r : threads) {
            tgkill(getpid(), r.tid, SIGUSR2);  // by tid: a thread that has exited is just skipped (ESRCH)
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        // Capturing a long backtrace must not fail a tour that recovered meanwhile. Ordinary Play returns immediately.
        if (now == g_frames->load()) DiagnosticTourWatchdogStalled(now, seconds);
    }
}

}  // namespace

void RegisterGuestThread() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_threads.push_back({pthread_self(), int(gettid())});
}

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
