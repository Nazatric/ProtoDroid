// Host-testable gameplay-probe address classification for the supported Steam build 19788432.
#pragma once

#include <cstdint>

namespace native {

// The InGameState vtables contain the two small thunks below. Recompiled Android thunks use RecompDirect(), so the
// tour must recognize both those vtable targets and their final simulation/real-time task targets. All values are
// offsets from the loaded engine base; the preferred DLL base is 0x10000000 and ProtoDroid uses 0x7c000000.
constexpr uint32_t DiagnosticTourGameplayTaskTarget(uint32_t target, uint32_t engine_base) {
    struct ProbeTarget {
        uint32_t observed_offset;
        uint32_t task_offset;
    };
    constexpr ProbeTarget targets[] = {
        {0x000246b3u, 0x004e45c0u},  // Method1<InGameState_void_bool> -> ISimulationTask.
        {0x0002bc01u, 0x004e4ad0u},  // Closure0<InGameState_void> -> IRealTimeTask.
        {0x004e45c0u, 0x004e45c0u},  // Direct simulation-task target, including FollowThunks().
        {0x004e4ad0u, 0x004e4ad0u},  // Direct real-time-task target, including FollowThunks().
    };
    for (const ProbeTarget& entry : targets) {
        if (target == engine_base + entry.observed_offset) return engine_base + entry.task_offset;
    }
    return 0;
}

}  // namespace native
