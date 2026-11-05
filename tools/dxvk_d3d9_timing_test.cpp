#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "d3d9_timing.h"

namespace {
  std::vector<std::string> messages;

  void Expect(bool condition, const std::string& message) {
    if (!condition) {
      std::cerr << "FAIL: " << message << "\n";
      std::exit(1);
    }
  }

  [[maybe_unused]] bool LastContains(const std::string& text) {
    return !messages.empty() && messages.back().find(text) != std::string::npos;
  }
}

namespace dxvk {
  void Logger::info(const std::string& message) {
    messages.push_back(message);
  }
}

namespace dxvk::env {
  std::string getEnvVar(const char* name) {
    Expect(std::string(name) == "PROTODROID_D3D9_TIMING", "unexpected environment lookup");
#ifdef TEST_DISABLED
    return "";
#else
    return "1";
#endif
  }
}

int main() {
#ifdef __ANDROID__
#ifdef TEST_DISABLED
  dxvk::D3D9Timing::Record(dxvk::D3D9Timing::Op::DrawTotal, 1000000);
  dxvk::D3D9Timing::MaybeLog();
  dxvk::D3D9Timing::Flush("disabled");
  Expect(messages.empty(), "disabled timing emitted output");
  std::cout << "disabled D3D9 timing runtime checks passed\n";
#else
  using Clock = dxvk::D3D9Timing;
  Clock::Record(Clock::Op::DrawTotal, 1000000, 0, true, false, 3);
  Clock::Record(Clock::Op::DrawTotal, 2000000, 1, false, true);
  Clock::Record(Clock::Op::DrawTotal, -100, -1);
  Expect(messages.empty(), "Record logged inside the hot path");
  Clock::MaybeLog();
  Expect(messages.size() == 2, "initial MaybeLog did not emit announcement and one snapshot");
  Expect(messages[0].find("not GPU time") != std::string::npos, "missing timing limitation");
  Expect(LastContains("draw_total n=3 ok=1 false=1 errors=1 changed=1 flagged=1 units=3 avg=1.000 max=2.000ms"),
         "incorrect aggregate/result/unit output: " + messages.back());
  Clock::MaybeLog();
  Expect(messages.size() == 2, "immediate MaybeLog emitted a duplicate interval");

  constexpr int kThreads = 4;
  constexpr int kIterations = 1000;
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; t++) {
    threads.emplace_back([]() {
      for (int i = 0; i < kIterations; i++)
        Clock::Record(Clock::Op::StreamTotal, 125000, 0, true, false, 2);
    });
  }
  for (auto& thread : threads)
    thread.join();
  Clock::Flush("concurrent");
  Expect(LastContains("stream_total n=4000 ok=4000 false=0 errors=0 changed=4000 flagged=0 units=8000 avg=0.125 max=0.125ms"),
         "concurrent/residual accounting mismatch: " + messages.back());
  Expect(!LastContains("draw_total"), "interval counter was not reset");

  Clock::Record(Clock::Op::QueryEvent, 500000, 1);
  Clock::Flush("residual");
  Expect(LastContains("query_event n=1 ok=0 false=1 errors=0"), "post-interval residual was lost");
  Expect(!LastContains("stream_total"), "flushed counter reappeared in residual");
  Clock::Flush("empty");
  Expect(!LastContains(" n="), "empty flush invented an operation");
  std::cout << "enabled D3D9 timing runtime checks passed\n";
#endif
#else
#error This harness must compile the Android timing branch
#endif
  return 0;
}
