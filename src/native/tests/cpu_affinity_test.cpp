namespace native {
void Log(const char*, ...) {}
}  // namespace native

#include "cpu_affinity.h"

#include "prime_affinity.h"

#include <cstdio>
#include <cstdlib>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "CPU affinity test failed: %s\n", message);
        std::exit(1);
    }
}

}  // namespace

int main() {
    using native::FallbackCpuMask;
    using native::LowCpuMask;

    Check(FallbackCpuMask(0) == 1u, "an unknown CPU count must retain one usable CPU");
    Check(FallbackCpuMask(1) == 0x1u && FallbackCpuMask(4) == 0xfu && FallbackCpuMask(8) == 0xffu,
          "fallback masks do not match their CPU counts");
    Check(FallbackCpuMask(31) == 0x7fffffffu && FallbackCpuMask(32) == 0xffffffffu &&
              FallbackCpuMask(64) == 0xffffffffu,
          "32-bit mask boundaries are wrong");

    Check(LowCpuMask([](uint32_t cpu) { return cpu < 8; }) == 0xffu,
          "eight visible CPUs were not reported to the engine");
    Check(LowCpuMask([](uint32_t cpu) { return cpu == 1 || cpu == 4 || cpu == 7; }) == 0x92u,
          "sparse Android CPU sets were not preserved");
    Check(LowCpuMask([](uint32_t cpu) { return cpu == 31 || cpu == 32 || cpu == 63; }) == 0x80000000u,
          "the low 32-bit mask included unsupported high CPUs");
    Check(LowCpuMask([](uint32_t) { return false; }) == 0, "an empty CPU set produced a false mask");

    using native::PrimeCpuMask;

    // Dimensity 8400 layout: CPU0 3.25 GHz, CPU1-3 3.0 GHz, CPU4-7 2.3 GHz -> prime cluster 0x0f.
    {
        static const uint32_t d8400[8] = {3250000, 3000000, 3000000, 3000000, 2300000, 2300000, 2300000, 2300000};
        Check(PrimeCpuMask([](uint32_t c) { return c < 8 ? d8400[c] : 0u; }) == 0x0fu,
              "Dimensity 8400 prime cluster (1x A725 3.25 + 3x A725 3.0) was not 0x0f");
    }
    // Snapdragon 8 Gen 3: X4 3.3, 3x A720 3.15, 2x A720 2.96, 2x A520 2.27 -> 0x0f (2.96 GHz is below 90%).
    {
        static const uint32_t s8g3[8] = {3300000, 3150000, 3150000, 3150000, 2960000, 2960000, 2270000, 2270000};
        Check(PrimeCpuMask([](uint32_t c) { return c < 8 ? s8g3[c] : 0u; }) == 0x0fu,
              "Snapdragon 8 Gen 3 prime cluster (X4 + 3x A720 3.15) was not 0x0f");
    }
    // Homogeneous all-big SoC (e.g. an 8x 2.4 GHz device): no hierarchy, no pinning.
    Check(PrimeCpuMask([](uint32_t) { return 2400000u; }) == 0,
          "homogeneous SoC must not be pinned");
    // No cpufreq data at all: no pinning.
    Check(PrimeCpuMask([](uint32_t) { return 0u; }) == 0, "missing cpufreq data must disable pinning");
    // A 7% in-cluster bin spread (3.0/2.8 GHz, no little cluster) is one homogeneous pool: no hierarchy, no
    // pinning, and no concentration of thermal load on one bin.
    {
        static const uint32_t binned[4] = {3000000, 3000000, 2800000, 2800000};
        Check(PrimeCpuMask([](uint32_t c) { return c < 4 ? binned[c] : 0u; }) == 0,
              "a 7% bin spread without a little cluster must not pin");
    }

    std::puts("CPU affinity tests passed (eight-core, sparse, fallback, 32-bit boundary, and prime-cluster masks)");
    return 0;
}
