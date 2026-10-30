// The 32-bit Windows affinity API can represent only 32 logical CPUs. Android can expose more, so preserve the
// low-CPU representation expected by the guest instead of truncating the mask to an arbitrary fixed core count.
#pragma once

#include <cstdint>

namespace native {

constexpr uint32_t FallbackCpuMask(uint32_t count) {
    if (count >= 32) return 0xffffffffu;
    return count ? (uint32_t(1) << count) - 1u : 1u;
}

template <typename Available>
uint32_t LowCpuMask(Available&& available) {
    uint32_t mask = 0;
    for (uint32_t cpu = 0; cpu < 32; ++cpu) {
        if (available(cpu)) mask |= uint32_t(1) << cpu;
    }
    return mask;
}

}  // namespace native
