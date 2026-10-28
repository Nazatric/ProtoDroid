#include "cpu_topology.h"

#include <cstdarg>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace native {
int SettingInt(const char*, int def) { return def; }
void Log(const char*, ...) {}
}  // namespace native

namespace {

void Check(const std::vector<int>& actual, const std::vector<int>& expected, const char* what) {
    if (actual != expected) throw std::runtime_error(what);
}

}  // namespace

int main() {
    using native::SelectFastCores;
    try {
        Check(SelectFastCores({}, {}, true), {}, "empty topology should be unrestricted");
        Check(SelectFastCores({100, 100, 500, 1000}, {1, 1, 1, 1}, true), {2, 3},
              "capacity main-thread policy changed");
        Check(SelectFastCores({100, 200, 500, 1000}, {1, 1, 1, 1}, false), {1, 2, 3},
              "capacity worker policy changed");
        Check(SelectFastCores({1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024},
                              {2100000, 2100000, 2100000, 2100000, 3000000, 3000000, 3000000, 3250000}, true),
              {4, 5, 6, 7}, "all-big-core max-frequency fallback not applied");
        Check(SelectFastCores({1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024},
                              {2100000, 2100000, 2100000, 2100000, 3000000, 3000000, 3000000, 3250000}, false),
              {4, 5, 6, 7}, "workers did not avoid the slowest frequency cluster");
        Check(SelectFastCores({0, 0, 0}, {1000, 2000, 3000}, true), {2},
              "unreadable capacities did not fall back to frequency");
        Check(SelectFastCores({1024, 1024}, {2000000, 2000000}, true), {},
              "homogeneous CPU should not be pinned");
        Check(SelectFastCores({1024, 1024}, {2000000, 2200000}, true), {},
              "near-homogeneous selection should not pin every CPU");
        std::puts("performance topology tests passed");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "performance test failed: %s\n", e.what());
        return 1;
    }
}
