// Shared performance-core selection. ARM capacity is preferred; identical capacities on all-big-core parts (for
// example Dimensity 8400) fall back to each CPU's advertised maximum frequency instead of treating every core alike.
#include "cpu_topology.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <sys/stat.h>

namespace native {

int SettingInt(const char* key, int def);
void Log(const char* fmt, ...);

namespace {

uint32_t ReadNumber(const std::string& path) {
    std::ifstream f(path);
    uint64_t value = 0;
    f >> value;
    return f ? uint32_t(std::min<uint64_t>(value, 0xffffffffu)) : 0;
}

std::string CpuPath(int cpu, const char* leaf) {
    return "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/" + leaf;
}

std::string Join(const std::vector<uint32_t>& values) {
    std::string out;
    for (uint32_t value : values) {
        if (!out.empty()) out += ',';
        out += std::to_string(value);
    }
    return out;
}

std::string Join(const std::vector<int>& values) {
    std::string out;
    for (int value : values) {
        if (!out.empty()) out += ',';
        out += std::to_string(value);
    }
    return out;
}

}  // namespace

std::vector<int> SelectFastCores(const std::vector<uint32_t>& caps, const std::vector<uint32_t>& freqs,
                                 bool main_thread) {
    const size_t n = caps.size();
    if (!n || freqs.size() != n) return {};
    const auto mm = [](const std::vector<uint32_t>& v) {
        return std::minmax_element(v.begin(), v.end());
    };
    const auto cap = mm(caps);
    const bool capacities_valid = *cap.first > 0;
    if (capacities_valid && *cap.first != *cap.second) {
        std::vector<int> selected;
        for (size_t i = 0; i < n; ++i) {
            // Preserve the established capacity policy on heterogeneous big.LITTLE CPUs.
            const bool fast = main_thread ? uint64_t(caps[i]) * 2 >= *cap.second : caps[i] > *cap.first;
            if (fast) selected.push_back(int(i));
        }
        return selected.size() == n ? std::vector<int>{} : selected;
    }

    const auto frequency = mm(freqs);
    if (*frequency.first == 0 || *frequency.first == *frequency.second) return {};
    std::vector<int> selected;
    for (size_t i = 0; i < n; ++i) {
        // On equal-capacity designs use max-kHz evidence. Main/render threads use the fast cluster (within 25% of
        // the fastest core); other high-priority workers merely avoid the slowest cluster.
        const bool fast = main_thread ? uint64_t(freqs[i]) * 4 >= uint64_t(*frequency.second) * 3
                                      : freqs[i] > *frequency.first;
        if (fast) selected.push_back(int(i));
    }
    return selected.size() == n ? std::vector<int>{} : selected;
}

const cpu_set_t* FastCoreAffinity(bool main_thread) {
    static cpu_set_t main_set, worker_set;
    static bool main_valid = false, worker_valid = false;
    static std::once_flag once;
    std::call_once(once, [] {
        if (SettingInt("big_cores", 1) == 0 || SettingInt("battery_saver", 0) != 0) return;
        std::vector<uint32_t> caps, freqs;
        for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
            struct stat st {};
            const std::string root = "/sys/devices/system/cpu/cpu" + std::to_string(cpu);
            if (stat(root.c_str(), &st) != 0) break;
            caps.push_back(ReadNumber(CpuPath(cpu, "cpu_capacity")));
            uint32_t khz = ReadNumber(CpuPath(cpu, "cpufreq/cpuinfo_max_freq"));
            if (!khz) khz = ReadNumber(CpuPath(cpu, "cpufreq/scaling_max_freq"));
            freqs.push_back(khz);
        }
        const std::vector<int> main = SelectFastCores(caps, freqs, true);
        const std::vector<int> workers = SelectFastCores(caps, freqs, false);
        auto fill = [](cpu_set_t& set, const std::vector<int>& cpus) {
            CPU_ZERO(&set);
            for (int cpu : cpus) CPU_SET(cpu, &set);
            return !cpus.empty();
        };
        main_valid = fill(main_set, main);
        worker_valid = fill(worker_set, workers);
        Log("cpu: %zu cores, capacity [%s], max-kHz [%s], main [%s], workers [%s]", caps.size(),
            Join(caps).c_str(), Join(freqs).c_str(), Join(main).c_str(), Join(workers).c_str());
    });
    const cpu_set_t* set = main_thread ? &main_set : &worker_set;
    const bool valid = main_thread ? main_valid : worker_valid;
    return valid ? set : nullptr;
}

}  // namespace native
