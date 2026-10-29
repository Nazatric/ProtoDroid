// Testable thread-list and session-creation policy for Android performance hints. The Android entry points are
// injected by perf_hint.cpp, so this logic also runs in host tests without a device or NDK.
#pragma once

#include <cstdint>
#include <vector>

namespace native::detail {

inline void AddHintThread(std::vector<int32_t>& tids, int32_t tid) {
    if (tid <= 0) return;
    for (int32_t present : tids)
        if (present == tid) return;
    tids.push_back(tid);
}

template <typename IsAlive>
std::vector<int32_t> LiveHintThreads(const std::vector<int32_t>& registered, int32_t current, IsAlive alive) {
    std::vector<int32_t> result;
    for (int32_t tid : registered)
        if (alive(tid)) AddHintThread(result, tid);
    AddHintThread(result, current);  // the calling thread is alive by definition
    return result;
}

// Creates the session with all live workers. If Android rejects that list, retry with the calling engine thread;
// dynamic thread-list support can then restore the workers without making session creation all-or-nothing.
template <typename CreateSession, typename SetThreads>
auto CreateHintSession(std::vector<int32_t>& tids, int32_t current, CreateSession create,
                       SetThreads set_threads, bool& retried) {
    retried = false;
    auto session = create(tids);
    if (session || tids.size() <= 1) return session;

    retried = true;
    const std::vector<int32_t> requested = tids;
    tids = {current};
    session = create(tids);
    if (session && set_threads(session, requested) == 0) {
        tids = requested;
    } else if (session) {
        // Best-effort rollback after a rejected worker list; the session already has `current`.
        set_threads(session, tids);
    }
    return session;
}

}  // namespace native::detail
