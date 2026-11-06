#!/usr/bin/env python3
"""Structural DXVK frame-signal/limiter invariants; not a latency or FPS benchmark."""
from pathlib import Path
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: test_dxvk_frame_pacing.py <patched DXVK source root>")
root = Path(sys.argv[1])
presenter = (root / "src/dxvk/dxvk_presenter.cpp").read_text(encoding="utf-8")
limiter = (root / "src/util/util_fps_limiter.cpp").read_text(encoding="utf-8")
swapchain = (root / "src/d3d9/d3d9_swapchain.cpp").read_text(encoding="utf-8")
swapchain_h = (root / "src/d3d9/d3d9_swapchain.h").read_text(encoding="utf-8")
adapter = (root / "src/dxvk/dxvk_adapter.cpp").read_text(encoding="utf-8")
checks = 0


def check(condition, message):
    global checks
    checks += 1
    if not condition:
        raise AssertionError(message)


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f"unterminated {signature}")


signal = function(presenter, "void Presenter::signalFrame")
destroy = function(presenter, "void Presenter::destroySwapchain")
frame_thread = function(presenter, "void Presenter::runFrameThread")
set_rate = function(limiter, "void FpsLimiter::setTargetFrameRate")
delay = function(limiter, "void FpsLimiter::delay")
heuristic = function(limiter, "bool FpsLimiter::testRefreshHeuristic")
sync_latency = function(swapchain, "void D3D9SwapChainEx::SyncFrameLatency")

check("if (m_signal == nullptr || !frameId)" in signal, "null/repeat signal guard")
check("m_device->features().khrPresentWait.presentWait" in signal, "presentWait feature branch")
direct = signal.split("} else {", 1)[1].split("m_lastFrameId.store", 1)[0]
check("m_fpsLimiter.delay()" in direct, "direct limiter path")
check("m_signal->signal(frameId)" in direct, "direct frame signal")
check("m_lastFrameId.store(frameId, std::memory_order_release)" in signal, "release publication")
check("m_signal->wait(m_lastFrameId.load(std::memory_order_acquire))" in destroy, "acquire destruction wait")
check(destroy.index("m_signal->wait") < destroy.index("waitForSwapchainFence") < destroy.index("vkDestroySwapchainKHR"),
      "destruction wait order")
check("vkWaitForPresentKHR" in frame_thread, "actual present wait")
check("frame.mode == VK_PRESENT_MODE_FIFO_KHR || frame.mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR" in frame_thread,
      "present-wait FIFO modes")
check(frame_thread.index("m_fpsLimiter.delay()") < frame_thread.index("m_signal->signal(frame.frameId)"),
      "frame-thread limiter/signal order")
check("Always signal even on error" in frame_thread, "error progress guarantee")

check("if (!m_envOverride)" in set_rate, "environment override precedence")
check("frameRate != 0.0" in set_rate and "TimerDuration::zero()" in set_rate, "zero interval configuration")
check("if (interval == TimerDuration::zero())" in delay and "m_nextFrame = TimePoint()" in delay,
      "disabled schedule reset")
check("if (interval < TimerDuration::zero())" in delay and "testRefreshHeuristic(interval, t1, latency)" in delay,
      "negative-interval refresh heuristic")
check(delay.index("lock.unlock()") < delay.index("Sleep::sleepUntil"), "sleep outside limiter mutex")
check("m_nextFrame + interval" in delay and "t1 + interval" in delay, "deadline catch-up policy")
check("maxFrameCount = m_heuristicFrameCount + maxLatency" in heuristic, "latency-aware heuristic")
check("m_heuristicFrameCount >= MaxWindowSize" in heuristic, "bounded heuristic window")
check("Engaging frame rate limiter" in heuristic, "explicit heuristic activation diagnostic")

check("uint64_t                       frameId = D3D9DeviceEx::MaxFrameLatency" in swapchain_h,
      "initial frame ID prevents latency underflow")
check("wctx.frameLatencySignal = new sync::Fence(wctx.frameId)" in swapchain, "initial fence value")
check("m_wctx->frameLatencySignal->wait(m_wctx->frameId - GetActualFrameLatency())" in sync_latency,
      "frame-latency wait target")
check("uint64_t frameId = cRepeat ? 0 : cFrameId" in swapchain, "repeated present suppresses frame signal")
check("enabledFeatures.khrPresentWait.presentWait =" in adapter and
      "m_deviceFeatures.khrPresentId.presentId &&" in adapter and
      "m_deviceFeatures.khrPresentWait.presentWait" in adapter,
      "presentWait requires presentId")

print(f"DXVK frame-pacing structural checks passed ({checks})")
