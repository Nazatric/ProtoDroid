#pragma once

#include <sched.h>

#include <cstdint>
#include <vector>

namespace native {

// Empty means "leave the scheduler unrestricted". Otherwise these are the CPU indices selected for a main/render
// thread (main_thread=true) or another high-priority worker. Exposed separately for host topology tests.
std::vector<int> SelectFastCores(const std::vector<uint32_t>& capacities, const std::vector<uint32_t>& max_khz,
                                 bool main_thread);

// Cached process topology. Returns nullptr when affinity is disabled, unavailable, or would include every core.
const cpu_set_t* FastCoreAffinity(bool main_thread);

}  // namespace native
