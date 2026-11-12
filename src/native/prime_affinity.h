// Opt-in CPU-cluster pinning for big.LITTLE SoCs (Dimensity, Snapdragon): finds the fastest CPU cluster from
// cpufreq, not from hardcoded core numbers, and pins guest threads to it. Off by default - the v0.15 policy
// leaves scheduling to Android, and the sanctioned boost path is the ADPF hint session (perf_hint.cpp). This
// module exists for devices where the scheduler leaves frame work on slow cores (affinity_mode=1).
#pragma once

#include <cstdint>

namespace native {

// Bitmask (over Linux CPU numbers, capped at 31) of CPUs in the fastest cluster, or 0 when cpufreq data is
// unavailable (then no pinning happens). A CPU belongs to the prime cluster when its maximum frequency is at
// least 90% of the highest maximum seen - this survives the CPU-number layouts of every shipping SoC
// (Dimensity 8400: 1x A725 3.25 GHz + 3x A725 3.0 GHz + 4x A720 2.3 GHz; Snapdragon 8 Gen 3: 1x X4 + 5x A720
// + 2x A520) and is robust to clusters that differ only by a rounding step.
using FreqReader = uint32_t (*)(uint32_t cpu);

uint32_t PrimeCpuMask(FreqReader read_freq);
uint32_t PrimeCpuMaskFromSysfs();

// Pins the CALLING thread (when mask != 0 and not already pinned inside it). Returns true when the affinity
// was applied. Never fails hard: an unpinned thread plays the same as v0.15.
bool PinCurrentThreadToPrimeCores(uint32_t mask);

// Combined helper: resolves the mask once per process (cached) and pins the calling thread. mode 0 = off,
// 1 = on. Logs the decision the first time it is called.
bool ApplyGuestThreadAffinity(int mode);

}  // namespace native
