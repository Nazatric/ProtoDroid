#pragma once

#include <cstdint>

#include "frame_limiter.h"

namespace native {

// Detailed, read-only diagnostics are opt-in. A worker performs proc/file reads and log I/O; when enabled, hot
// paths record bounded timing samples. Ordinary Present/FPS tracking and shared log mirroring remain independent.
constexpr int kDetailedDiagnosticsDefault = 0;
bool DetailedDiagnosticsEnabled();
void DiagnosticsStart();
void DiagnosticsStop();

// One main-loop iteration. Engine loops are deliberately not frame-limiter samples.
void RecordEngineLoop(int64_t at_ns, int64_t service_ns, int64_t event_pump_ns, int64_t focus_ns);

// One successful-Present limiter call. Unlimited calls are ignored; zero-wait capped calls remain visible so a device
// capture can distinguish an accurate cap from a workload that cannot reach its target.
void RecordPresentLimiter(const FrameLimitResult& result);

}  // namespace native
