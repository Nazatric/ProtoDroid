// Android Dynamic Performance Framework (APerformanceHint, Android 12+): the main and worker threads report how
// long each frame's work took against the frame budget, so the system raises CPU clocks only when a frame would
// miss it and lowers them when there is headroom - most of the battery a game burns goes to clocks above what it
// needs. Loaded at run time (libmain targets API 26); a no-op where unavailable. Setting perf_hint=0 disables it.
#include "guest.h"
#include "perf_hint_session.h"

#ifdef __ANDROID__
#include <dirent.h>
#include <dlfcn.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace native {

namespace {

struct APerformanceHintManager;
struct APerformanceHintSession;

struct Api {
    APerformanceHintManager* (*getManager)();
    APerformanceHintSession* (*createSession)(APerformanceHintManager*, const int32_t*, size_t, int64_t);
    int (*updateTarget)(APerformanceHintSession*, int64_t);
    int (*reportActual)(APerformanceHintSession*, int64_t);
    int (*setThreads)(APerformanceHintSession*, const int32_t*, size_t);  // API 34
} g_api;

std::mutex g_mutex;
APerformanceHintSession* g_session = nullptr;
// Most unsupported/older devices never create a session. Avoid taking the session mutex on every Present there.
std::atomic<bool> g_session_active{false};
std::atomic<bool> g_start_attempted{false};
std::vector<int32_t> g_tids;
int64_t g_target_ns = 16666667;
uint64_t g_reports = 0, g_report_errors = 0, g_target_updates = 0, g_target_errors = 0;
uint64_t g_thread_updates = 0, g_thread_update_errors = 0;

bool ThreadAlive(int32_t tid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/self/task/%d", tid);
    return access(path, F_OK) == 0;
}

// CreateDevice starts DXVK's native CS and submission threads before the engine starts its frame session. Discover
// those exact threads once here; Present still performs its separate first-success registration for API-34 updates.
size_t AddDxvkFrameThreads(std::vector<int32_t>& tids) {
    size_t added = 0;
    DIR* dir = opendir("/proc/self/task");
    if (!dir) {
        Log("perf hint: native DXVK frame-thread scan unavailable");
        return 0;
    }
    while (dirent* entry = readdir(dir)) {
        char* end = nullptr;
        const long tid = strtol(entry->d_name, &end, 10);
        if (!end || *end || tid <= 0 || tid > INT32_MAX) continue;
        char path[128], comm[64];
        snprintf(path, sizeof(path), "/proc/self/task/%ld/comm", tid);
        FILE* file = fopen(path, "r");
        if (!file) continue;
        char* line = fgets(comm, sizeof(comm), file);
        fclose(file);
        if (!line || !detail::IsDxvkFrameThreadComm(comm)) continue;
        comm[strcspn(comm, "\r\n")] = 0;
        const size_t before = tids.size();
        detail::AddHintThread(tids, int32_t(tid));
        if (tids.size() != before) {
            ++added;
            Log("perf hint: native frame thread %ld (%s)", tid, comm);
        }
    }
    closedir(dir);
    return added;
}

}  // namespace

void PerfHintStart(int fps_target) {
    g_start_attempted.store(true, std::memory_order_release);
    if (!SettingInt("perf_hint", 1)) return;
    void* lib = dlopen("libandroid.so", RTLD_NOW);
    if (!lib) return;
    g_api.getManager = reinterpret_cast<decltype(g_api.getManager)>(dlsym(lib, "APerformanceHint_getManager"));
    g_api.createSession = reinterpret_cast<decltype(g_api.createSession)>(dlsym(lib, "APerformanceHint_createSession"));
    g_api.updateTarget =
        reinterpret_cast<decltype(g_api.updateTarget)>(dlsym(lib, "APerformanceHint_updateTargetWorkDuration"));
    g_api.reportActual =
        reinterpret_cast<decltype(g_api.reportActual)>(dlsym(lib, "APerformanceHint_reportActualWorkDuration"));
    g_api.setThreads = reinterpret_cast<decltype(g_api.setThreads)>(dlsym(lib, "APerformanceHint_setThreads"));
    if (!g_api.getManager || !g_api.createSession || !g_api.updateTarget || !g_api.reportActual) return;
    APerformanceHintManager* manager = g_api.getManager();
    if (!manager) return;

    std::lock_guard<std::mutex> lock(g_mutex);
    const int32_t current = int32_t(gettid());
    detail::AddHintThread(g_tids, current);
    AddDxvkFrameThreads(g_tids);
    g_target_ns = 1000000000ll / (fps_target > 0 ? fps_target : 60);

    // A worker can exit between SetThreadPriority and session creation. Android rejects the complete session when an
    // initial tid is stale, so prune first and retain a useful main-thread fallback instead of disabling all hints.
    g_tids = detail::LiveHintThreads(g_tids, current, ThreadAlive);
    auto create = [&](const std::vector<int32_t>& tids) {
        return g_api.createSession(manager, tids.data(), tids.size(), g_target_ns);
    };
    auto set_threads = [&](APerformanceHintSession* session, const std::vector<int32_t>& tids) {
        return g_api.setThreads ? g_api.setThreads(session, tids.data(), tids.size()) : -1;
    };
    bool retried = false;
    g_session = detail::CreateHintSession(g_tids, current, create, set_threads, retried);
    g_session_active.store(g_session != nullptr, std::memory_order_release);
    Log("perf hint: %s%s (target %.1f ms, %zu threads)", g_session ? "session" : "unavailable",
        retried ? " after retry" : "", double(g_target_ns) / 1e6, g_tids.size());
}

// A thread that works on every frame (the engine's render thread, the audio threads): part of the session
// (Android 14+).
// Threads may register before the session exists (the engine raises its render thread during EngineInitialize,
// before the main loop starts the session): they are kept and join it then.
void PerfHintAddThread(int tid) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (int32_t t : g_tids)
        if (t == tid) return;
    g_tids.push_back(int32_t(tid));
    const char* outcome = g_start_attempted.load(std::memory_order_acquire)
                              ? "recorded; no active session"
                              : "queued for session creation";
    bool dynamic_update = false;
    int update_status = 0;
    size_t stale_pruned = 0;
    if (g_session && g_api.setThreads) {
        dynamic_update = true;
        // Android rejects the complete list when any earlier worker has exited. Startup pruning alone is insufficient:
        // audio and other frame workers are created later, after loading threads may already have terminated. Refresh
        // only on this rare registration path, never once per frame, and send the same live list that we retain.
        ++g_thread_updates;
        update_status = detail::RefreshHintSessionThreads(
            g_tids, tid, ThreadAlive, [&](const std::vector<int32_t>& live) {
                return g_api.setThreads(g_session, live.data(), live.size());
            },
            stale_pruned);
        if (update_status != 0) {
            ++g_thread_update_errors;
            outcome = "rejected by dynamic thread update";
        } else {
            bool retained = false;
            for (int32_t t : g_tids) retained = retained || t == tid;
            outcome = retained ? "added to active session" : "live roster refreshed; new thread exited";
        }
    } else if (g_session) {
        outcome = "recorded; active session predates dynamic thread updates";
    }
    // Registration happens only for a newly observed engine/audio/Present thread, never once per frame. In particular,
    // this makes a rejected Android update visible instead of silently losing the real render thread from the session.
    if (dynamic_update)
        Log("perf hint: thread %d %s (registered=%zu, update_status=%d, stale_pruned=%zu)", tid, outcome,
            g_tids.size(), update_status, stale_pruned);
    else
        Log("perf hint: thread %d %s (registered=%zu)", tid, outcome, g_tids.size());
}

void PerfHintTarget(int fps_target) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_target_ns = 1000000000ll / (fps_target > 0 ? fps_target : 60);
    ++g_target_updates;
    if (g_session && g_api.updateTarget(g_session, g_target_ns) != 0) ++g_target_errors;
}

void PerfHintFrame(int64_t work_ns) {
    if (work_ns <= 0 || !g_session_active.load(std::memory_order_acquire)) return;
    // NDK session methods are not thread-safe: worker registration can arrive from the audio thread while the
    // engine reports a frame, so reportActual must use the same lock as setThreads/updateTarget.
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_session) {
        ++g_reports;
        if (g_api.reportActual(g_session, work_ns) != 0) ++g_report_errors;
    }
}

PerfHintSnapshot GetPerfHintStats() {
    std::lock_guard<std::mutex> lock(g_mutex);
    PerfHintSnapshot out;
    out.session = g_session != nullptr;
    out.target_ns = g_target_ns;
    out.threads = g_tids.size();
    out.reports = g_reports;
    out.report_errors = g_report_errors;
    out.target_updates = g_target_updates;
    out.target_errors = g_target_errors;
    out.thread_updates = g_thread_updates;
    out.thread_update_errors = g_thread_update_errors;
    return out;
}

}  // namespace native

#else

namespace native {

void PerfHintStart(int) {}
void PerfHintAddThread(int) {}
void PerfHintTarget(int) {}
void PerfHintFrame(int64_t) {}
PerfHintSnapshot GetPerfHintStats() { return {}; }

}  // namespace native

#endif
