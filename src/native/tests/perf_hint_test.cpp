#include "perf_hint_session.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

using Threads = std::vector<int32_t>;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "performance-hint test failed: %s\n", message);
        std::exit(1);
    }
}

struct FakeSession {};

void TestLiveThreadPruning() {
    Threads threads = native::detail::LiveHintThreads({10, 11, 12, 11, -1}, 20, [](int32_t tid) {
        return tid == 10 || tid == 11;  // 12 exited; invalid and duplicate entries are discarded too
    });
    Check(threads == Threads({10, 11, 20}), "stale, duplicate, and current thread handling");
}

void TestInitialSession() {
    FakeSession session;
    Threads threads{10, 11};
    int creates = 0, updates = 0;
    bool retried = true;
    auto create = [&](const Threads&) { ++creates; return &session; };
    auto update = [&](FakeSession*, const Threads&) { ++updates; return 0; };
    Check(native::detail::CreateHintSession(threads, 10, create, update, retried) == &session,
          "initial session pointer");
    Check(!retried && creates == 1 && updates == 0 && threads == Threads({10, 11}),
          "successful initial session must not retry or rewrite threads");
}

void TestFallbackAndRestore() {
    FakeSession session;
    Threads threads{10, 11, 20};
    int creates = 0, updates = 0;
    bool retried = false;
    auto create = [&](const Threads& list) {
        ++creates;
        return list == Threads({20}) ? &session : nullptr;
    };
    auto update = [&](FakeSession*, const Threads& list) {
        ++updates;
        Check(list == Threads({10, 11, 20}), "worker list restored after minimal session");
        return 0;
    };
    Check(native::detail::CreateHintSession(threads, 20, create, update, retried) == &session,
          "fallback session pointer");
    Check(retried && creates == 2 && updates == 1 && threads == Threads({10, 11, 20}),
          "fallback must restore workers when dynamic updates succeed");
}

void TestFallbackWithoutWorkerUpdate() {
    FakeSession session;
    Threads threads{10, 11, 20};
    int creates = 0, updates = 0;
    bool retried = false;
    auto create = [&](const Threads& list) {
        ++creates;
        return list.size() == 1 ? &session : nullptr;
    };
    auto update = [&](FakeSession*, const Threads&) { ++updates; return -1; };
    Check(native::detail::CreateHintSession(threads, 20, create, update, retried) == &session,
          "main-only fallback pointer");
    Check(retried && creates == 2 && updates == 2 && threads == Threads({20}),
          "failed worker update must retain the valid main-only session");
}

void TestRejectedSessions() {
    Threads multiple{10, 11};
    int creates = 0, updates = 0;
    bool retried = false;
    auto reject = [&](const Threads&) { ++creates; return static_cast<FakeSession*>(nullptr); };
    auto update = [&](FakeSession*, const Threads&) { ++updates; return 0; };
    Check(native::detail::CreateHintSession(multiple, 10, reject, update, retried) == nullptr,
          "rejected fallback pointer");
    Check(retried && creates == 2 && updates == 0 && multiple == Threads({10}),
          "rejected fallback must not attempt worker updates");

    Threads single{10};
    creates = 0;
    retried = false;
    Check(native::detail::CreateHintSession(single, 10, reject, update, retried) == nullptr,
          "rejected single-thread session pointer");
    Check(!retried && creates == 1 && single == Threads({10}),
          "single-thread rejection must not repeat the identical request");
}

}  // namespace

int main() {
    TestLiveThreadPruning();
    TestInitialSession();
    TestFallbackAndRestore();
    TestFallbackWithoutWorkerUpdate();
    TestRejectedSessions();
    std::puts("performance-hint session tests passed");
    return 0;
}
