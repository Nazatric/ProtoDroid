#include "cpu_affinity.h"

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

    std::puts("CPU affinity tests passed (eight-core, sparse, fallback, and 32-bit boundary masks)");
    return 0;
}
