#pragma once

#include <cstdint>

namespace native {

// Detailed, read-only diagnostics are opt-in. A worker performs proc/file reads and log I/O; when enabled, hot
// paths record bounded timing samples. Ordinary Present/FPS tracking and shared log mirroring remain independent.
constexpr int kDetailedDiagnosticsDefault = 0;
bool DetailedDiagnosticsEnabled();
void DiagnosticsStart();
void DiagnosticsStop();

// One main-loop iteration. A negative limiter duration means unlimited mode and is not counted as a limiter sample.
void RecordEngineLoop(int64_t at_ns, int64_t service_ns, int64_t event_pump_ns, int64_t limiter_ns, int64_t focus_ns);

}  // namespace native
