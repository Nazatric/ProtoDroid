#!/usr/bin/env python3
"""Extract and test the actual patched DXVK Present diagnostics; no GPU/FPS inference."""
import argparse
import hashlib
import json
import os
import statistics
import subprocess
from pathlib import Path

COMMENT = '  // Android port diagnostics (DXVK_PRESENT_STATS): where an application-side acquire waits.'


def extract_function(source, signature):
    start = source.find(signature)
    if start < 0:
        raise RuntimeError(f'missing {signature}')
    opening = source.find('{', start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == '{':
            depth += 1
        elif source[index] == '}':
            depth -= 1
            if depth == 0:
                return source[start:index + 1].replace('Presenter::', 'TestPresenter::', 1)
    raise RuntimeError(f'unterminated {signature}')


def parts(source):
    begin = source.index(COMMENT)
    end = source.index('  VkResult Presenter::acquireNextImage', begin)
    return {
        'globals': source[begin:end],
        'acquire': extract_function(source, '  VkResult Presenter::acquireNextImage'),
        'present': extract_function(source, '  VkResult Presenter::presentImage'),
        'wait': extract_function(source, '  void Presenter::waitForSwapchainFence'),
    }


def d3d9_block(source):
    start = source.index('  static D3D9PresentStats g_presentStats;')
    end = source.index('    const double tStart', start)
    return source[start:end]


def transform_d3d9(before):
    after = before.replace(
        '  extern std::atomic<uint64_t> g_acqFenceUs, g_acqCallUs, g_acqSlow, g_preAcqUs, g_preFenceUs;  // dxvk_presenter.cpp',
        '  extern std::atomic<uint64_t> g_acqFenceUs, g_acqCallUs, g_acqSlow, g_preAcqUs, g_preFenceUs;  // dxvk_presenter.cpp\n'
        '  extern std::atomic<bool> g_presentStatsEnabled;')
    after = after.replace(
        '    static const bool statsOn = env::getEnvVar("DXVK_PRESENT_STATS") == "1";  // read after the host set it',
        '    static const bool statsOn = [] {\n'
        '      const bool enabled = env::getEnvVar("DXVK_PRESENT_STATS") == "1";  // read after the host set it\n'
        '      g_presentStatsEnabled.store(enabled, std::memory_order_relaxed);\n'
        '      return enabled;\n'
        '    }();')
    return after


def transform(before):
    after = dict(before)
    after['globals'] = before['globals'].replace(
        '  static uint64_t nowUs() {',
        '  std::atomic<bool> g_presentStatsEnabled{false};\n  static uint64_t nowUs() {')
    old_app = '''      uint64_t t0 = nowUs();
      waitForSwapchainFence(semaphores);
      uint64_t t1 = nowUs();'''
    new_app = '''      const bool stats = g_presentStatsEnabled.load(std::memory_order_relaxed);
      uint64_t t0 = stats ? nowUs() : 0;
      waitForSwapchainFence(semaphores);
      uint64_t t1 = stats ? nowUs() : 0;'''
    old_app_counters = '''      g_acqFenceUs += t1 - t0;
      g_acqCallUs += nowUs() - t1;
      g_acqSlow += 1;'''
    new_app_counters = '''      if (stats) {
        g_acqFenceUs += t1 - t0;
        g_acqCallUs += nowUs() - t1;
        g_acqSlow += 1;
      }'''
    after['acquire'] = before['acquire'].replace(old_app, new_app).replace(old_app_counters, new_app_counters)
    old_pre = '''    uint64_t t0 = nowUs();
    waitForSwapchainFence(nextSync);
    uint64_t t1 = nowUs();'''
    new_pre = '''    const bool stats = g_presentStatsEnabled.load(std::memory_order_relaxed);
    uint64_t t0 = stats ? nowUs() : 0;
    waitForSwapchainFence(nextSync);
    uint64_t t1 = stats ? nowUs() : 0;'''
    old_pre_counters = '''    g_preFenceUs += t1 - t0;
    g_preAcqUs += nowUs() - t1;'''
    new_pre_counters = '''    if (stats) {
      g_preFenceUs += t1 - t0;
      g_preAcqUs += nowUs() - t1;
    }'''
    after['present'] = before['present'].replace(old_pre, new_pre).replace(old_pre_counters, new_pre_counters)
    return after


STUBS = r'''
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using VkResult = int;
using VkPresentModeKHR = int;
constexpr VkResult VK_SUCCESS = 0, VK_NOT_READY = 1, VK_SUBOPTIMAL_KHR = 1000001003, VK_ERROR = -1;
constexpr int VK_STRUCTURE_TYPE_PRESENT_ID_KHR = 1;
constexpr int VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT = 2;
constexpr int VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_EXT = 3;
constexpr int VK_STRUCTURE_TYPE_PRESENT_INFO_KHR = 4;
constexpr uint64_t VK_NULL_HANDLE = 0;
constexpr int VK_TRUE = 1, VK_FALSE = 0;
struct VkBase { int sType; void* pNext; };
struct VkPresentIdKHR { int sType; void* pNext = nullptr; uint32_t swapchainCount = 0; const uint64_t* pPresentIds = nullptr; };
struct VkSwapchainPresentFenceInfoEXT { int sType; void* pNext = nullptr; uint32_t swapchainCount = 0; const uint64_t* pFences = nullptr; };
struct VkSwapchainPresentModeInfoEXT { int sType; void* pNext = nullptr; uint32_t swapchainCount = 0; const VkPresentModeKHR* pPresentModes = nullptr; };
struct VkPresentInfoKHR {
  int sType; void* pNext = nullptr; uint32_t waitSemaphoreCount = 0; const uint64_t* pWaitSemaphores = nullptr;
  uint32_t swapchainCount = 0; const uint64_t* pSwapchains = nullptr; const uint32_t* pImageIndices = nullptr;
};
struct PresenterSync { uint64_t acquire = 0, present = 0, fence = 0; int fenceSignaled = VK_FALSE; };
namespace str { template<typename... T> std::string format(T&&...) { return {}; } }
struct Logger { static void err(const std::string&) {} };
struct FakeFeatures {
  struct { bool presentId = false; } khrPresentId;
  struct { bool swapchainMaintenance1 = false; } extSwapchainMaintenance1;
};
struct FakeDevice {
  FakeFeatures value;
  struct { struct { uint64_t queueHandle = 77; } graphics; } queueValues;
  FakeFeatures& features() { return value; }
  auto& queues() { return queueValues; }
};
struct FakeVkd {
  bool record = false;
  std::vector<std::string> events, chain;
  VkResult acquireResult = VK_SUCCESS, presentResult = VK_SUCCESS, waitResult = VK_SUCCESS, resetResult = VK_SUCCESS;
  uint32_t nextImage = 0;
  uint64_t lastWaitSemaphore = 0, lastSwapchain = 0, lastPresentMode = 999;
  void log(const char* event) { if (record) events.emplace_back(event); }
  uint64_t device() const { return 99; }
  VkResult vkAcquireNextImageKHR(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint32_t* index) {
    log("acquire"); *index = nextImage++ % 6; return acquireResult;
  }
  VkResult vkWaitForFences(uint64_t, uint32_t, const uint64_t*, int, uint64_t) { log("wait-fences"); return waitResult; }
  VkResult vkResetFences(uint64_t, uint32_t, const uint64_t*) { log("reset-fences"); return resetResult; }
  VkResult vkQueuePresentKHR(uint64_t, VkPresentInfoKHR* info) {
    log("queue-present"); chain.clear(); lastWaitSemaphore = info->pWaitSemaphores[0]; lastSwapchain = info->pSwapchains[0];
    for (auto* base = static_cast<VkBase*>(info->pNext); base; base = static_cast<VkBase*>(base->pNext)) {
      if (record) chain.push_back(std::to_string(base->sType));
      if (base->sType == VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_EXT)
        lastPresentMode = *reinterpret_cast<VkSwapchainPresentModeInfoEXT*>(base)->pPresentModes;
    }
    return presentResult;
  }
};

#define PRESENTER_CLASS \
class TestPresenter { \
 public: \
  std::vector<PresenterSync> m_semaphores = std::vector<PresenterSync>(6); \
  uint32_t m_frameIndex = 0, m_imageIndex = 0; \
  VkResult m_acquireStatus = VK_NOT_READY; \
  uint64_t m_swapchain = 55; \
  std::shared_ptr<FakeVkd> m_vkd = std::make_shared<FakeVkd>(); \
  std::shared_ptr<FakeDevice> m_device = std::make_shared<FakeDevice>(); \
  VkResult acquireNextImage(PresenterSync&, uint32_t&); \
  VkResult presentImage(VkPresentModeKHR, uint64_t); \
  void waitForSwapchainFence(PresenterSync&); \
};
'''


HARNESS = r'''
static void Require(bool condition, const char* message) {
  if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
struct BaselineTraits {
  using P = baseline::TestPresenter;
  static constexpr bool always = true;
  static void setEnabled(bool) {}
  static void reset() {
    baseline::g_acqFenceUs = 0; baseline::g_acqCallUs = 0; baseline::g_acqSlow = 0;
    baseline::g_preAcqUs = 0; baseline::g_preFenceUs = 0;
  }
  static uint64_t appCount() { return baseline::g_acqSlow; }
  static uint64_t preFence() { return baseline::g_preFenceUs; }
  static uint64_t preAcq() { return baseline::g_preAcqUs; }
};
struct CandidateTraits {
  using P = candidate::TestPresenter;
  static constexpr bool always = false;
  static void setEnabled(bool value) { candidate::g_presentStatsEnabled.store(value, std::memory_order_relaxed); }
  static void reset() {
    candidate::g_acqFenceUs = 0; candidate::g_acqCallUs = 0; candidate::g_acqSlow = 0;
    candidate::g_preAcqUs = 0; candidate::g_preFenceUs = 0;
  }
  static uint64_t appCount() { return candidate::g_acqSlow; }
  static uint64_t preFence() { return candidate::g_preFenceUs; }
  static uint64_t preAcq() { return candidate::g_preAcqUs; }
};

template<typename T>
void Semantics(bool enabled) {
  T::reset(); T::setEnabled(enabled);
  typename T::P p;
  for (uint32_t i = 0; i < 6; ++i) {
    p.m_semaphores[i].acquire = 100 + i; p.m_semaphores[i].present = 200 + i; p.m_semaphores[i].fence = 300 + i;
  }
  p.m_vkd->record = true;
  p.m_device->features().khrPresentId.presentId = true;
  p.m_device->features().extSwapchainMaintenance1.swapchainMaintenance1 = true;
  p.m_semaphores[0].fenceSignaled = VK_TRUE;
  p.m_acquireStatus = VK_NOT_READY;
  PresenterSync sync{}; uint32_t index = 99;
  Require(p.acquireNextImage(sync, index) == VK_SUCCESS, "application acquire result");
  Require((p.m_vkd->events == std::vector<std::string>{"wait-fences", "reset-fences", "acquire"}),
          "application acquire call order");
  Require(index == 0 && sync.acquire == 100 && !p.m_semaphores[0].fenceSignaled, "application acquire state");
  const bool counted = T::always || enabled;
  Require(T::appCount() == (counted ? 1u : 0u), "application counter gate");
  p.m_vkd->events.clear();
  Require(p.acquireNextImage(sync, index) == VK_SUCCESS && p.m_vkd->events.empty(), "duplicate application acquire");

  p.m_semaphores[1].fenceSignaled = VK_TRUE;
  p.m_vkd->events.clear();
  Require(p.presentImage(1, 42) == VK_SUCCESS, "queue present result");
  Require((p.m_vkd->events == std::vector<std::string>{"queue-present", "wait-fences", "reset-fences", "acquire"}),
          "queue present/pre-acquire call order");
  Require((p.m_vkd->chain == std::vector<std::string>{"2", "3", "1"}), "present pNext chain");
  Require(p.m_vkd->lastPresentMode == 1 && p.m_vkd->lastWaitSemaphore == 200 && p.m_vkd->lastSwapchain == 55,
          "present arguments");
  Require(p.m_frameIndex == 1 && p.m_acquireStatus == VK_SUCCESS, "pre-acquire state");
  Require(T::appCount() == (counted ? 1u : 0u), "application counter changed by queue present");
  Require(counted || (T::preAcq() == 0 && T::preFence() == 0), "disabled pre-acquire counters changed");

  p.m_vkd->events.clear(); p.m_vkd->presentResult = VK_SUBOPTIMAL_KHR;
  Require(p.presentImage(1, 0) == VK_SUBOPTIMAL_KHR, "suboptimal present result");
  Require((p.m_vkd->events == std::vector<std::string>{"queue-present", "acquire"}), "suboptimal call order");
  Require((p.m_vkd->chain == std::vector<std::string>{"2", "3"}), "zero frame ID present chain");

  p.m_vkd->events.clear(); p.m_vkd->presentResult = VK_ERROR;
  const uint32_t beforeFrame = p.m_frameIndex;
  const uint64_t beforeApp = T::appCount();
  Require(p.presentImage(1, 43) == VK_ERROR, "present error result");
  Require((p.m_vkd->events == std::vector<std::string>{"queue-present"}), "error path performed pre-acquire");
  Require(p.m_frameIndex == beforeFrame && T::appCount() == beforeApp, "error path state/counter");

  p.m_vkd->events.clear(); p.m_vkd->acquireResult = VK_ERROR; p.m_acquireStatus = VK_NOT_READY;
  p.m_frameIndex = 0; p.m_semaphores[0].fenceSignaled = VK_FALSE;
  Require(p.acquireNextImage(sync, index) == VK_ERROR, "acquire error result");
  Require((p.m_vkd->events == std::vector<std::string>{"acquire"}), "acquire error call order");
  Require(T::appCount() == beforeApp + (counted ? 1u : 0u), "acquire error counter gate");
  T::reset(); T::setEnabled(false);
}

template<typename T>
void Publication() {
  T::reset(); T::setEnabled(false);
  typename T::P p;
  std::atomic<bool> start{false};
  std::thread worker([&] {
    while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
    for (uint32_t i = 0; i < 1000; ++i) p.presentImage(1, i + 1);
  });
  T::setEnabled(true);
  start.store(true, std::memory_order_release);
  worker.join();
  Require(T::preAcq() > 0, "cross-thread publication did not retain enabled timing");
  T::reset(); T::setEnabled(false);
}

template<typename P>
double TimePresent(P& p, uint32_t calls) {
  const auto start = std::chrono::steady_clock::now();
  for (uint32_t i = 0; i < calls; ++i) {
    VkResult result = p.presentImage(1, i + 1);
    asm volatile("" : : "r"(result) : "memory");
  }
  return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / calls;
}
template<typename P>
double TimeApplication(P& p, uint32_t calls) {
  PresenterSync sync{}; uint32_t index = 0;
  const auto start = std::chrono::steady_clock::now();
  for (uint32_t i = 0; i < calls; ++i) {
    p.m_acquireStatus = VK_NOT_READY;
    VkResult result = p.acquireNextImage(sync, index);
    asm volatile("" : : "r"(result) : "memory");
  }
  return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / calls;
}

int main(int argc, char** argv) {
  const uint32_t calls = argc > 1 ? uint32_t(std::stoul(argv[1])) : 200000;
  const uint32_t rounds = argc > 2 ? uint32_t(std::stoul(argv[2])) : 16;
  const bool benchmark = calls > 0 && rounds > 0;
  Semantics<BaselineTraits>(false); Semantics<CandidateTraits>(false); Semantics<CandidateTraits>(true);
  Publication<BaselineTraits>(); Publication<CandidateTraits>();
  std::puts("DXVK Present diagnostics semantics passed (call order, counters, errors, feature chains, publication)");
  if (!benchmark) return 0;
  std::printf("{\"kind\":\"native-host-present-diagnostic-microfixture-not-gpu-or-fps\",\"calls\":%u,\"rounds\":%u,\n", calls, rounds);
  std::printf("\"measurements\":[");
  for (uint32_t round = 0; round < rounds; ++round) {
    BaselineTraits::reset(); CandidateTraits::reset(); CandidateTraits::setEnabled(false);
    BaselineTraits::P a, control; CandidateTraits::P b;
    double presentA, presentB, presentControl, appA, appB;
    if (round & 1) {
      presentB = TimePresent(b, calls); presentA = TimePresent(a, calls); presentControl = TimePresent(control, calls);
      appB = TimeApplication(b, calls); appA = TimeApplication(a, calls);
    } else {
      presentA = TimePresent(a, calls); presentB = TimePresent(b, calls); presentControl = TimePresent(control, calls);
      appA = TimeApplication(a, calls); appB = TimeApplication(b, calls);
    }
    Require(a.m_acquireStatus == VK_SUCCESS && b.m_acquireStatus == VK_SUCCESS, "benchmark call failure");
    Require(BaselineTraits::appCount() == calls && CandidateTraits::appCount() == 0, "benchmark counter gate");
    std::printf("%s\n{\"round\":%u,\"order\":\"%s\",\"present_baseline_ns\":%.9f,\"present_candidate_ns\":%.9f,\"present_control_ns\":%.9f,\"application_baseline_ns\":%.9f,\"application_candidate_ns\":%.9f}",
                round ? "," : "", round, (round & 1) ? "candidate-first" : "baseline-first",
                presentA, presentB, presentControl, appA, appB);
  }
  // Enabled mode must keep collecting; compare its cost separately without using it as an FPS result.
  std::printf("],\n\"enabled_measurements\":[");
  for (uint32_t round = 0; round < rounds; ++round) {
    BaselineTraits::reset(); CandidateTraits::reset(); CandidateTraits::setEnabled(true);
    BaselineTraits::P a; CandidateTraits::P b;
    const double before = TimePresent(a, calls), after = TimePresent(b, calls);
    Require(BaselineTraits::appCount() == 0 && CandidateTraits::appCount() == 0, "present-only application count");
    Require(BaselineTraits::preAcq() > 0 && CandidateTraits::preAcq() > 0, "enabled accumulated pre-acquire timing");
    std::printf("%s\n{\"round\":%u,\"present_baseline_ns\":%.9f,\"present_enabled_candidate_ns\":%.9f}",
                round ? "," : "", round, before, after);
  }
  std::printf("]}\n");
  return 0;
}
'''


def namespace(name, values):
    return f'namespace {name} {{\n' + values['globals'] + '\n' + 'PRESENTER_CLASS\n' + values['acquire'] + '\n\n' + values['present'] + '\n\n' + values['wait'] + '\n}\n'


def median(values):
    return statistics.median(values)


def summarize(rows, before, after):
    ratios = [row[after] / row[before] for row in rows]
    deltas = [row[before] - row[after] for row in rows]
    return {
        'rounds': len(rows), 'baseline_median_ns': median([row[before] for row in rows]),
        'candidate_median_ns': median([row[after] for row in rows]),
        'paired_ratio_median': median(ratios), 'paired_saving_ns_median': median(deltas),
        'paired_candidate_faster_rounds': sum(delta > 0 for delta in deltas),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('before', type=Path)
    parser.add_argument('after', type=Path)
    parser.add_argument('--before-d3d9', type=Path)
    parser.add_argument('--after-d3d9', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--calls', type=int, default=200000)
    parser.add_argument('--rounds', type=int, default=16)
    parser.add_argument('--sanitizer', choices=['none', 'ubsan', 'asan-ubsan', 'tsan'], default='none')
    args = parser.parse_args()
    before_text, after_text = args.before.read_text(), args.after.read_text()
    before, actual_after = parts(before_text), parts(after_text)
    expected_after = transform(before)
    for key in before:
        if actual_after[key] != expected_after[key]:
            raise SystemExit(f'candidate {key} differs from the exact intended gate transformation')
    if bool(args.before_d3d9) != bool(args.after_d3d9):
        raise SystemExit('both D3D9 source paths are required together')
    d3d9_hashes = None
    if args.before_d3d9:
        before_d3d9 = d3d9_block(args.before_d3d9.read_text())
        after_d3d9 = d3d9_block(args.after_d3d9.read_text())
        if after_d3d9 != transform_d3d9(before_d3d9):
            raise SystemExit('candidate D3D9 block differs from the exact lazy-publication transformation')
        if 'g_presentStatsEnabled.store' not in after_d3d9:
            raise SystemExit('D3D9 publication is missing')
        d3d9_hashes = {
            'before_sha256': hashlib.sha256(args.before_d3d9.read_bytes()).hexdigest(),
            'after_sha256': hashlib.sha256(args.after_d3d9.read_bytes()).hexdigest(),
        }
    args.output.mkdir(parents=True, exist_ok=True)
    source = STUBS + '\n' + namespace('baseline', before) + '\n' + namespace('candidate', actual_after) + '\n' + HARNESS
    cpp = args.output / 'present_stats_fixture.cpp'
    binary = args.output / 'present_stats_fixture'
    cpp.write_text(source)
    flags = ['g++', '-std=c++17', '-O2' if args.sanitizer == 'none' else '-O1', '-g', '-Wall', '-Wextra', '-Werror']
    if args.sanitizer != 'none':
        flags += ['-fsanitize=' + {'ubsan': 'undefined', 'asan-ubsan': 'address,undefined', 'tsan': 'thread'}[args.sanitizer],
                  '-fno-sanitize-recover=all']
    flags += [str(cpp), '-pthread', '-o', str(binary)]
    build = subprocess.run(flags, text=True, capture_output=True)
    (args.output / 'build-command.json').write_text(json.dumps(flags, indent=2) + '\n')
    (args.output / 'build.stdout').write_text(build.stdout)
    (args.output / 'build.stderr').write_text(build.stderr)
    if build.returncode:
        raise SystemExit(f'fixture build failed; see {args.output}/build.stderr')
    run_args = [str(binary), str(args.calls if args.sanitizer == 'none' else 0),
                str(args.rounds if args.sanitizer == 'none' else 0)]
    environment = dict(os.environ, UBSAN_OPTIONS='print_stacktrace=1:halt_on_error=1', TSAN_OPTIONS='halt_on_error=1')
    run = subprocess.run(run_args, text=True, capture_output=True, env=environment)
    (args.output / 'run.stdout').write_text(run.stdout)
    (args.output / 'run.stderr').write_text(run.stderr)
    if run.returncode:
        raise SystemExit(f'fixture failed; see {args.output}/run.stderr')
    report = {
        'schema': 1, 'kind': 'native-present-diagnostic-microfixture-not-gpu-or-android-fps',
        'before': str(args.before), 'after': str(args.after),
        'before_sha256': hashlib.sha256(args.before.read_bytes()).hexdigest(),
        'after_sha256': hashlib.sha256(args.after.read_bytes()).hexdigest(),
        'sanitizer': args.sanitizer, 'correctness': 'passed', 'd3d9': d3d9_hashes,
        'semantics_stdout': run.stdout.split('{', 1)[0], 'build_stderr': build.stderr, 'run_stderr': run.stderr,
    }
    if args.sanitizer == 'none':
        raw = json.loads(run.stdout[run.stdout.index('{'):])
        (args.output / 'timing.json').write_text(json.dumps(raw, indent=2) + '\n')
        rows = raw['measurements']
        report['present'] = summarize(rows, 'present_baseline_ns', 'present_candidate_ns')
        report['application_acquire_fallback'] = summarize(rows, 'application_baseline_ns', 'application_candidate_ns')
        report['aa_control'] = summarize(rows, 'present_baseline_ns', 'present_control_ns')
        report['enabled_candidate'] = summarize(raw['enabled_measurements'], 'present_baseline_ns', 'present_enabled_candidate_ns')
        report['raw_timing_rows'] = len(rows) + len(raw['enabled_measurements'])
    (args.output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
