// Android Dynamic Performance Framework (APerformanceHint, Android 12+): the main and worker threads report how
// long each frame's work took against the frame budget, so the system raises CPU clocks only when a frame would
// miss it and lowers them when there is headroom - most of the battery a game burns goes to clocks above what it
// needs. Loaded at run time (libmain targets API 26); a no-op where unavailable. Setting perf_hint=0 disables it.
#include "guest.h"
#include "perf_hint_session.h"

#ifdef __ANDROID__
#include <dlfcn.h>
#include <unistd.h>

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
std::vector<int32_t> g_tids;
int64_t g_target_ns = 16666667;
uint64_t g_reports = 0, g_report_errors = 0, g_target_updates = 0, g_target_errors = 0;

bool ThreadAlive(int32_t tid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/self/task/%d", tid);
    return access(path, F_OK) == 0;
}

}  // namespace

void PerfHintStart(int fps_target) {
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
    if (g_session && g_api.setThreads && g_api.setThreads(g_session, g_tids.data(), g_tids.size()) != 0)
        g_tids.pop_back();
}

void PerfHintTarget(int fps_target) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_target_ns = 1000000000ll / (fps_target > 0 ? fps_target : 60);
    ++g_target_updates;
    if (g_session && g_api.updateTarget(g_session, g_target_ns) != 0) ++g_target_errors;
}

void PerfHintFrame(int64_t work_ns) {
    if (work_ns <= 0) return;
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
