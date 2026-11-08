#include "prime_affinity.h"

#include <dirent.h>
#include <sched.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "guest.h"

namespace native {

namespace {

std::atomic<uint32_t> g_cached_mask{0};

uint32_t SysfsMaxFreq(uint32_t cpu) {
    char path[96];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%u/cpufreq/cpuinfo_max_freq", cpu);
    FILE* f = fopen(path, "r");
    if (!f) return 0;
    uint32_t khz = 0;
    if (fscanf(f, "%u", &khz) != 1) khz = 0;
    fclose(f);
    return khz;
}

}  // namespace

uint32_t PrimeCpuMask(FreqReader read_freq) {
    uint32_t best = 0, lowest = 0xffffffffu;
    for (uint32_t cpu = 0; cpu < 32; ++cpu) {
        uint32_t f = read_freq(cpu);
        if (f > best) best = f;
        if (f && f < lowest) lowest = f;
    }
    if (!best) return 0;
    // Only pin when a real cluster hierarchy exists (fastest at least 25% faster than the slowest core): on
    // all-big or homogeneous low-end SoCs the scheduler has no bad cluster to avoid, and pinning would only
    // concentrate thermal load.
    if (best < lowest + lowest / 4) return 0;
    // 90% of the top frequency: the three A725 3.0 GHz cores sit beside the 3.25 GHz prime, the A720 2.3 GHz
    // cluster does not. Allowing the whole big cluster keeps four useful cores instead of one.
    uint32_t mask = 0;
    for (uint32_t cpu = 0; cpu < 32; ++cpu) {
        uint32_t f = read_freq(cpu);
        if (f >= best - best / 10 && f > 0) mask |= uint32_t(1) << cpu;
    }
    return mask;
}

uint32_t PrimeCpuMaskFromSysfs() {
    uint32_t cached = g_cached_mask.load(std::memory_order_relaxed);
    if (cached) return cached;
    // One pass over the online CPUs; cpufreq does not change per boot session.
    uint32_t mask = PrimeCpuMask(SysfsMaxFreq);
    g_cached_mask.store(mask, std::memory_order_relaxed);
    return mask;
}

bool PinCurrentThreadToPrimeCores(uint32_t mask) {
    if (!mask) return false;
    cpu_set_t set;
    CPU_ZERO(&set);
    int pinned = 0;
    for (uint32_t cpu = 0; cpu < 32; ++cpu) {
        if (mask & (uint32_t(1) << cpu)) {
            CPU_SET(int(cpu), &set);
            ++pinned;
        }
    }
    if (!pinned) return false;
    return sched_setaffinity(0, sizeof(set), &set) == 0;
}

bool ApplyGuestThreadAffinity(int mode) {
    if (mode != 1) return false;
    uint32_t mask = PrimeCpuMaskFromSysfs();
    static std::atomic<bool> logged{false};
    bool ok = PinCurrentThreadToPrimeCores(mask);
    if (!logged.exchange(true)) {
        if (!mask) {
            Log("affinity: no cpufreq data, leaving threads to the scheduler");
        } else if (ok) {
            Log("affinity: pinned tid %d to prime cluster mask %08x (affinity_mode=1)", gettid(), mask);
        } else {
            Log("affinity: pinning to mask %08x was not permitted (affinity_mode=1)", mask);
        }
    }
    return ok;
}

}  // namespace native
