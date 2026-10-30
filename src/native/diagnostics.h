#pragma once

#include <cstdint>

namespace native {

// Detailed, read-only diagnostics. A worker performs proc/file reads and log I/O; hot paths only record timings.
void DiagnosticsStart();
void DiagnosticsStop();

// One main-loop iteration. A negative limiter duration means unlimited mode and is not counted as a limiter sample.
void RecordEngineLoop(int64_t at_ns, int64_t service_ns, int64_t event_pump_ns, int64_t limiter_ns, int64_t focus_ns);

}  // namespace native
