#include "diagnostics.h"

#include <SDL2/SDL.h>
#include <dirent.h>
#include <dlfcn.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "guest.h"
#include "present_stats.h"
#include "proc_stats.h"
#include "timing_window.h"

namespace native {

void MirrorCopyDXVKLogsAsync();
std::string TakeIoStats();
std::string TakeIoEvents();
std::string ArenaUsage();
std::string TakeTextureTimingStats();
TimingWindowSnapshot TakePresentTimingWindow();

extern std::atomic<int> g_fps_limit;

namespace {

std::atomic<bool> g_active{false};
std::atomic<bool> g_detailed{false};
std::mutex g_lifecycle_mutex;
std::condition_variable g_wake;
std::thread g_thread;
int g_interval_seconds = 5;

std::mutex g_engine_mutex;
TimingWindow g_service_timing{65536}, g_pump_timing{65536}, g_focus_timing{65536};
TimingWindow g_limiter_timing;

std::string ReadText(const std::string& path, size_t maximum = 262144) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::string text(maximum, '\0');
    input.read(text.data(), std::streamsize(maximum));
    text.resize(size_t(input.gcount()));
    return text;
}

uint64_t NamedKb(const std::string& text, const std::string& key) {
    for (size_t begin = 0; begin < text.size();) {
        const size_t end = text.find('\n', begin);
        const size_t size = (end == std::string::npos ? text.size() : end) - begin;
        if (size > key.size() + 1 && text.compare(begin, key.size(), key) == 0 && text[begin + key.size()] == ':') {
            const std::string value = text.substr(begin + key.size() + 1, size - key.size() - 1);
            return strtoull(value.c_str(), nullptr, 10);
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return 0;
}

[[maybe_unused]] std::string ReadProperty(const char* key) {
#ifdef __ANDROID__
    char value[PROP_VALUE_MAX] = {};
    const int size = __system_property_get(key, value);
    return size > 0 ? std::string(value, size_t(size)) : std::string();
#else
    (void)key;
    return {};
#endif
}

[[maybe_unused]] std::string Known(std::string value) { return value.empty() ? "unknown" : value; }

void LogDeviceInfo() {
#ifdef __ANDROID__
    Log("device: model=%s device=%s soc=%s platform=%s hardware=%s android=%s sdk=%s abi=%s",
        Known(ReadProperty("ro.product.model")).c_str(), Known(ReadProperty("ro.product.device")).c_str(),
        Known(ReadProperty("ro.soc.model")).c_str(), Known(ReadProperty("ro.board.platform")).c_str(),
        Known(ReadProperty("ro.hardware")).c_str(), Known(ReadProperty("ro.build.version.release")).c_str(),
        Known(ReadProperty("ro.build.version.sdk")).c_str(), Known(ReadProperty("ro.product.cpu.abi")).c_str());
#endif
    const long online = sysconf(_SC_NPROCESSORS_ONLN);
    const long configured = sysconf(_SC_NPROCESSORS_CONF);
    const long page_size = sysconf(_SC_PAGESIZE);
    const long clock_ticks = sysconf(_SC_CLK_TCK);
    Log("host: online_cpus=%ld configured_cpus=%ld page_size=%ld clk_tck=%ld native_abi=%s", online, configured,
        page_size, clock_ticks,
#if defined(__aarch64__)
        "arm64"
#elif defined(__x86_64__)
        "x86_64"
#else
        "unknown"
#endif
    );

    SDL_DisplayMode mode{};
    if (SDL_GetCurrentDisplayMode(0, &mode) == 0) {
        Log("display: current=%dx%d refresh=%dHz format=%08x", mode.w, mode.h, mode.refresh_rate, mode.format);
    } else {
        Log("display: current mode unavailable: %s", SDL_GetError());
    }

    const std::string meminfo = ReadText("/proc/meminfo");
    Log("memory capacity: total=%llu MiB available=%llu MiB",
        (unsigned long long)(NamedKb(meminfo, "MemTotal") / 1024),
        (unsigned long long)(NamedKb(meminfo, "MemAvailable") / 1024));

    std::string frequencies;
    const int cores = int(std::max(online, 0l));
    for (int cpu = 0; cpu < cores && cpu < 64; ++cpu) {
        const std::string path = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/cpufreq/scaling_max_freq";
        const std::string value = ReadText(path, 64);
        if (value.empty()) continue;
        if (!frequencies.empty()) frequencies += ' ';
        frequencies += std::to_string(cpu) + "=" + std::to_string(strtoull(value.c_str(), nullptr, 10) / 1000) + "MHz";
    }
    Log("cpu maximum frequencies: %s", frequencies.empty() ? "unavailable" : frequencies.c_str());
}

struct HealthSample {
    int thermal_status = -2;  // -2: API unsupported; another negative value: temporarily unavailable
    float headroom = NAN;
    float forecast = NAN;
    float cpu_headroom = NAN;
    float gpu_headroom = NAN;
    int headroom_error = ENOTSUP;
    int cpu_error = ENOTSUP;
    int gpu_error = ENOTSUP;
};

class HealthProbe {
public:
    HealthProbe() {
#ifdef __ANDROID__
        lib_ = dlopen("libandroid.so", RTLD_NOW | RTLD_NOLOAD);
        if (!lib_) lib_ = dlopen("libandroid.so", RTLD_NOW);
        if (!lib_) return;
        acquire = reinterpret_cast<decltype(acquire)>(dlsym(lib_, "AThermal_acquireManager"));
        release = reinterpret_cast<decltype(release)>(dlsym(lib_, "AThermal_releaseManager"));
        status = reinterpret_cast<decltype(status)>(dlsym(lib_, "AThermal_getCurrentThermalStatus"));
        headroom = reinterpret_cast<decltype(headroom)>(dlsym(lib_, "AThermal_getThermalHeadroom"));
        cpu = reinterpret_cast<decltype(cpu)>(dlsym(lib_, "ASystemHealth_getCpuHeadroom"));
        gpu = reinterpret_cast<decltype(gpu)>(dlsym(lib_, "ASystemHealth_getGpuHeadroom"));
        if (acquire) manager = acquire();
#endif
    }

    ~HealthProbe() {
#ifdef __ANDROID__
        if (manager && release) release(manager);
#endif
    }

    std::string Capabilities() const {
        char out[256];
        snprintf(out, sizeof(out), "thermal_manager=%s status_api=%s headroom_api=%s cpu_headroom_api=%s gpu_headroom_api=%s",
                 manager ? "yes" : "no", status ? "yes" : "no", headroom ? "yes" : "no", cpu ? "yes" : "no",
                 gpu ? "yes" : "no");
        return out;
    }

    HealthSample Sample() const {
        HealthSample out;
#ifdef __ANDROID__
        if (manager && status) out.thermal_status = status(manager);
        else if (status) out.thermal_status = -1;
        if (manager && headroom) {
            out.headroom_error = 0;
            out.headroom = headroom(manager, 0);
            out.forecast = headroom(manager, 10);
        } else if (headroom) {
            out.headroom_error = ENODATA;
        }
        if (cpu) out.cpu_error = cpu(nullptr, &out.cpu_headroom);
        if (gpu) out.gpu_error = gpu(nullptr, &out.gpu_headroom);
#endif
        return out;
    }

private:
    struct AThermalManager;
    struct ACpuHeadroomParams;
    struct AGpuHeadroomParams;
    void* lib_ = nullptr;
    AThermalManager* manager = nullptr;
    AThermalManager* (*acquire)() = nullptr;
    void (*release)(AThermalManager*) = nullptr;
    int (*status)(AThermalManager*) = nullptr;
    float (*headroom)(AThermalManager*, int) = nullptr;
    int (*cpu)(const ACpuHeadroomParams*, float*) = nullptr;
    int (*gpu)(const AGpuHeadroomParams*, float*) = nullptr;
};

const char* ThermalName(int status) {
    static constexpr const char* names[] = {"none", "light", "moderate", "severe", "critical", "emergency", "shutdown"};
    if (status >= 0 && status < 7) return names[status];
    return status == -2 ? "unsupported" : "unavailable";
}

std::string FloatValue(float value, int error = 0) {
    if (error == ENOTSUP) return "unsupported";
    if (error == ENODATA) return "unavailable";
    if (error) return "unavailable(error=" + std::to_string(error) + ")";
    if (std::isnan(value)) return "unavailable(NaN)";
    if (!std::isfinite(value)) return "unavailable(non-finite)";
    char out[48];
    snprintf(out, sizeof(out), "%.3f", double(value));
    return out;
}

struct CpuTimes {
    uint64_t total = 0;
    uint64_t idle = 0;
};

std::map<std::string, CpuTimes> ReadCpuTimes() {
    std::map<std::string, CpuTimes> out;
    std::istringstream input(ReadText("/proc/stat"));
    for (std::string line; std::getline(input, line);) {
        if (line.compare(0, 3, "cpu") != 0) continue;
        std::istringstream fields(line);
        std::string name;
        fields >> name;
        if (name.size() > 3 && !std::isdigit(static_cast<unsigned char>(name[3]))) continue;
        std::vector<uint64_t> values;
        for (uint64_t value; fields >> value;) values.push_back(value);
        if (values.size() < 4) continue;
        CpuTimes times;
        for (uint64_t value : values) times.total += value;
        times.idle = values[3] + (values.size() > 4 ? values[4] : 0);
        out.emplace(std::move(name), times);
    }
    return out;
}

bool ReadProcStatFile(const std::string& path, ProcStatInfo& info) {
    const std::string text = ReadText(path, 4096);
    return !text.empty() && ParseProcStat(text, info);
}

std::string ReadCurrentFrequencies() {
    std::string out;
    const long count = std::max(sysconf(_SC_NPROCESSORS_CONF), 1l);
    for (long cpu = 0; cpu < count; ++cpu) {
        const std::string value = ReadText(
            "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/cpufreq/scaling_cur_freq", 128);
        if (value.empty()) continue;
        if (!out.empty()) out += ' ';
        char field[64];
        snprintf(field, sizeof(field), "cpu%ld=%.0f", cpu, strtoull(value.c_str(), nullptr, 10) / 1000.0);
        out += field;
    }
    return out.empty() ? "unavailable" : out;
}

class CpuMonitor {
public:
    std::string TakeProcessAndCores() {
        const int64_t now = MonotonicNanos();
        const double elapsed = last_sample_ns_ ? double(now - last_sample_ns_) / 1e9 : 0.0;
        last_sample_ns_ = now;

        ProcStatInfo process;
        const bool have_process = ReadProcStatFile("/proc/self/stat", process);
        const uint64_t user = have_process ? process.user_jiffies : 0;
        const uint64_t system = have_process ? process.system_jiffies : 0;
        const uint64_t total = user + system;
        uint64_t user_delta = 0, system_delta = 0, total_delta = 0;
        if (have_process_baseline_ && have_process && process.start_time_jiffies == process_start_ && total >= process_total_) {
            user_delta = user - process_user_;
            system_delta = system - process_system_;
            total_delta = total - process_total_;
        }
        if (have_process) {
            process_user_ = user;
            process_system_ = system;
            process_total_ = total;
            process_start_ = process.start_time_jiffies;
            have_process_baseline_ = true;
        }

        const double jiffy_ms = 1000.0 / double(std::max(sysconf(_SC_CLK_TCK), 1l));
        const double process_ms = double(total_delta) * jiffy_ms;
        const double process_percent = elapsed > 0 ? process_ms / (elapsed * 10.0) : 0.0;
        auto current = ReadCpuTimes();
        double system_busy = 0;
        const auto aggregate = current.find("cpu");
        const auto previous = previous_cpus_.find("cpu");
        if (aggregate != current.end() && previous != previous_cpus_.end() && aggregate->second.total >= previous->second.total &&
            aggregate->second.idle >= previous->second.idle) {
            const uint64_t total_diff = aggregate->second.total - previous->second.total;
            const uint64_t idle_diff = aggregate->second.idle - previous->second.idle;
            if (total_diff) system_busy = 100.0 * double(total_diff - idle_diff) / double(total_diff);
        }

        std::string cores;
        for (const auto& entry : current) {
            if (entry.first == "cpu") continue;
            const auto old = previous_cpus_.find(entry.first);
            if (old == previous_cpus_.end() || entry.second.total < old->second.total || entry.second.idle < old->second.idle)
                continue;
            const uint64_t total_diff = entry.second.total - old->second.total;
            const uint64_t idle_diff = entry.second.idle - old->second.idle;
            if (!total_diff) continue;
            if (!cores.empty()) cores += ' ';
            char value[64];
            snprintf(value, sizeof(value), "%s=%.1f", entry.first.c_str(),
                     100.0 * double(total_diff - idle_diff) / double(total_diff));
            cores += value;
        }
        previous_cpus_ = std::move(current);
        const std::string frequencies = ReadCurrentFrequencies();

        char out[768];
        snprintf(out, sizeof(out),
                 "process_cpu=%.2f%% of_one_core delta=%.1fms user=%.1fms system=%.1fms last_core=%d; "
                 "device_cpu_busy=%.2f%%; cores_busy%%=%s; current_MHz=%s",
                 process_percent, process_ms, double(user_delta) * jiffy_ms, double(system_delta) * jiffy_ms,
                 have_process ? process.processor : -1, system_busy, cores.empty() ? "baseline/unavailable" : cores.c_str(),
                 frequencies.c_str());
        return out;
    }

    std::string TakeThreads() {
        struct Sample {
            int tid = 0;
            ProcStatInfo info;
            uint64_t total = 0;
            uint64_t delta = 0;
            bool is_new = false;
        };
        std::vector<Sample> samples;
        std::unordered_map<int, ProcStatInfo> current;
        int new_threads = 0;
        if (DIR* dir = opendir("/proc/self/task")) {
            while (dirent* entry = readdir(dir)) {
                char* end = nullptr;
                const long tid = strtol(entry->d_name, &end, 10);
                if (!end || *end || tid <= 0) continue;
                ProcStatInfo info;
                if (!ReadProcStatFile("/proc/self/task/" + std::to_string(tid) + "/stat", info)) continue;
                const uint64_t total = info.user_jiffies + info.system_jiffies;
                uint64_t delta = 0;
                bool is_new = false;
                const auto old = previous_threads_.find(info.pid);
                if (old != previous_threads_.end() && old->second.start_time_jiffies == info.start_time_jiffies &&
                    total >= old->second.user_jiffies + old->second.system_jiffies) {
                    delta = total - old->second.user_jiffies - old->second.system_jiffies;
                } else {
                    is_new = true;
                    ++new_threads;
                }
                samples.push_back({info.pid, info, total, delta, is_new});
                current.emplace(info.pid, info);
            }
            closedir(dir);
        }
        previous_threads_ = std::move(current);
        std::sort(samples.begin(), samples.end(), [](const Sample& a, const Sample& b) {
            if (a.delta != b.delta) return a.delta > b.delta;
            return a.total > b.total;
        });
        const double jiffy_ms = 1000.0 / double(std::max(sysconf(_SC_CLK_TCK), 1l));
        char header[160];
        snprintf(header, sizeof(header), "threads=%zu new=%d top=", samples.size(), new_threads);
        std::string out = header;
        const size_t count = std::min(samples.size(), size_t(6));
        for (size_t i = 0; i < count; ++i) {
            const Sample& sample = samples[i];
            char item[320];
            snprintf(item, sizeof(item), "%s%d:%s delta=%.1fms total=%.1fms state=%c core=%d%s",
                     i ? " | " : "", sample.tid, sample.info.comm.c_str(), double(sample.delta) * jiffy_ms,
                     double(sample.total) * jiffy_ms, sample.info.state, sample.info.processor,
                     sample.is_new ? " new" : "");
            out += item;
        }
        if (!count) out += "unavailable";
        return out;
    }

private:
    uint64_t process_user_ = 0, process_system_ = 0, process_total_ = 0, process_start_ = 0;
    bool have_process_baseline_ = false;
    int64_t last_sample_ns_ = 0;
    std::map<std::string, CpuTimes> previous_cpus_;
    std::unordered_map<int, ProcStatInfo> previous_threads_;
};

std::string TimingSummary(TimingWindowSnapshot& snapshot) {
    const double p50 = snapshot.PercentileMs(50);
    const double p95 = snapshot.PercentileMs(95);
    const double p99 = snapshot.PercentileMs(99);
    char out[512];
    snprintf(out, sizeof(out),
             "n=%llu avg=%.3f p50=%.3f p95=%.3f p99=%.3f max=%.3f ms; "
             "gt8.5/17/34/50/100/250/1000ms=%llu/%llu/%llu/%llu/%llu/%llu/%llu",
             (unsigned long long)snapshot.count, snapshot.AverageMs(), p50, p95, p99, snapshot.MaxMs(),
             (unsigned long long)snapshot.over[0], (unsigned long long)snapshot.over[1],
             (unsigned long long)snapshot.over[2], (unsigned long long)snapshot.over[3],
             (unsigned long long)snapshot.over[4], (unsigned long long)snapshot.over[5],
             (unsigned long long)snapshot.over[6]);
    return out;
}

std::string TimingBasic(TimingWindowSnapshot& snapshot) {
    const double p95 = snapshot.PercentileMs(95);
    char out[192];
    snprintf(out, sizeof(out), "n=%llu avg=%.3f p95=%.3f max=%.3f ms", (unsigned long long)snapshot.count,
             snapshot.AverageMs(), p95, snapshot.MaxMs());
    return out;
}

std::string WorstEvents(const TimingWindowSnapshot& snapshot, size_t maximum = 4) {
    std::string out;
    size_t count = 0;
    for (const TimingEvent& event : snapshot.worst) {
        if (event.duration_ns < 0) break;
        if (count++ >= maximum) break;
        char item[160];
        snprintf(item, sizeof(item), "%s%.3fms@mono=%.3f tid=%d", out.empty() ? "" : " | ",
                 double(event.duration_ns) / 1e6, double(event.at_ns) / 1e9, event.tid);
        out += item;
    }
    return out.empty() ? "none" : out;
}

struct EngineSnapshot {
    TimingWindowSnapshot service, pump, limiter, focus;
};

EngineSnapshot TakeEngineSnapshot() {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    return {g_service_timing.Take(), g_pump_timing.Take(), g_limiter_timing.Take(), g_focus_timing.Take()};
}

struct RunSummary {
    uint64_t frames = 0;
    uint64_t total_ns = 0;
    std::array<uint64_t, 7> over{};
    TimingEvent worst;

    void Add(const TimingWindowSnapshot& snapshot) {
        frames += snapshot.count;
        total_ns += snapshot.total_ns;
        for (size_t i = 0; i < over.size(); ++i) over[i] += snapshot.over[i];
        if (snapshot.worst[0].duration_ns > worst.duration_ns) worst = snapshot.worst[0];
    }
};

void LogRunSummary(const RunSummary& run) {
    if (!run.frames) {
        Log("diagnostics run summary: no completed Present intervals");
        return;
    }
    const double average_ms = run.frames ? double(run.total_ns) / double(run.frames) / 1e6 : 0.0;
    const double fps = run.total_ns ? double(run.frames) * 1e9 / double(run.total_ns) : 0.0;
    Log("diagnostics run summary: present=%llu average_fps=%.3f average=%.3fms worst=%.3fms@mono=%.3f tid=%d; "
        "gt8.5/17/34/50/100/250/1000ms=%llu/%llu/%llu/%llu/%llu/%llu/%llu",
        (unsigned long long)run.frames, fps, average_ms, double(run.worst.duration_ns) / 1e6,
        double(run.worst.at_ns) / 1e9, run.worst.tid, (unsigned long long)run.over[0],
        (unsigned long long)run.over[1], (unsigned long long)run.over[2], (unsigned long long)run.over[3],
        (unsigned long long)run.over[4], (unsigned long long)run.over[5], (unsigned long long)run.over[6]);
}

void EmitWindow(HealthProbe& health, CpuMonitor& cpu, RunSummary& run, int& last_thermal, int64_t& last_emit_ns,
                bool final) {
    const int64_t now = MonotonicNanos();
    const double window_seconds = last_emit_ns ? double(now - last_emit_ns) / 1e9 : 0.0;
    last_emit_ns = now;

    auto present = TakePresentDetailedStats();
    auto present_wall = TakePresentTimingWindow();
    EngineSnapshot engine = TakeEngineSnapshot();
    run.Add(present);

    Log("diag%s: window=%.3fs loops=%llu rate=%.2f/s fps_limit=%d; present_fps=%.3f intervals %s",
        final ? " final" : "", window_seconds, (unsigned long long)engine.service.count,
        window_seconds > 0 ? double(engine.service.count) / window_seconds : 0.0, g_fps_limit.load(),
        present.Frequency(), TimingSummary(present).c_str());
    if (present.count) Log("diag: present worst: %s", WorstEvents(present).c_str());
    Log("diag: EngineService %s; worst: %s", TimingSummary(engine.service).c_str(),
        WorstEvents(engine.service, 2).c_str());
    Log("diag: event pump %s; limiter wait %s; focus %s", TimingBasic(engine.pump).c_str(),
        TimingBasic(engine.limiter).c_str(), TimingBasic(engine.focus).c_str());
    Log("diag: Present wall (includes FIFO/vsync, not GPU time) %s; worst: %s", TimingSummary(present_wall).c_str(),
        WorstEvents(present_wall, 2).c_str());
    Log("diag: texture %s", TakeTextureTimingStats().c_str());
    Log("diag: io %s", TakeIoStats().c_str());
    const std::string io_events = TakeIoEvents();
    if (!io_events.empty()) Log("diag: slow io: %s", io_events.c_str());

    const std::string status = ReadText("/proc/self/status", 65536);
    const std::string meminfo = ReadText("/proc/meminfo");
    Log("diag: memory rss=%lluMiB peak=%lluMiB virtual=%lluMiB anon=%lluMiB file=%lluMiB shmem=%lluMiB swap=%lluMiB; "
        "system_available=%lluMiB; arena %s",
        (unsigned long long)(NamedKb(status, "VmRSS") / 1024), (unsigned long long)(NamedKb(status, "VmHWM") / 1024),
        (unsigned long long)(NamedKb(status, "VmSize") / 1024), (unsigned long long)(NamedKb(status, "RssAnon") / 1024),
        (unsigned long long)(NamedKb(status, "RssFile") / 1024), (unsigned long long)(NamedKb(status, "RssShmem") / 1024),
        (unsigned long long)(NamedKb(status, "VmSwap") / 1024),
        (unsigned long long)(NamedKb(meminfo, "MemAvailable") / 1024), ArenaUsage().c_str());
    Log("diag: cpu %s", cpu.TakeProcessAndCores().c_str());
    Log("diag: %s", cpu.TakeThreads().c_str());

    const HealthSample sample = health.Sample();
    Log("diag: health thermal=%s(%d) headroom=%s forecast10s=%s cpu_headroom=%s gpu_headroom=%s",
        ThermalName(sample.thermal_status), sample.thermal_status,
        FloatValue(sample.headroom, sample.headroom_error).c_str(),
        FloatValue(sample.forecast, sample.headroom_error).c_str(), FloatValue(sample.cpu_headroom, sample.cpu_error).c_str(),
        FloatValue(sample.gpu_headroom, sample.gpu_error).c_str());
    if (sample.thermal_status >= 0 && sample.thermal_status != last_thermal) {
        Log("diag: thermal transition %s(%d) -> %s(%d)", ThermalName(last_thermal), last_thermal,
            ThermalName(sample.thermal_status), sample.thermal_status);
        last_thermal = sample.thermal_status;
    }

    const PerfHintSnapshot hint = GetPerfHintStats();
    Log("diag: adpf session=%s target=%.3fms threads=%zu reports_total=%llu report_errors_total=%llu "
        "target_updates_total=%llu target_errors_total=%llu",
        hint.session ? "yes" : "no", double(hint.target_ns) / 1e6, hint.threads, (unsigned long long)hint.reports,
        (unsigned long long)hint.report_errors, (unsigned long long)hint.target_updates,
        (unsigned long long)hint.target_errors);
}

void DiagnosticsWorker() {
    HealthProbe health;
    CpuMonitor cpu;
    RunSummary run;
    int last_thermal = -2;
    int64_t last_emit_ns = MonotonicNanos();
    const bool detailed = g_detailed.load(std::memory_order_acquire);
    if (detailed) Log("diagnostics health APIs: %s", health.Capabilities().c_str());

    std::unique_lock<std::mutex> lock(g_lifecycle_mutex);
    auto next_emit = detailed ? std::chrono::steady_clock::now() + std::chrono::seconds(1)
                              : std::chrono::steady_clock::time_point::max();
    auto next_mirror = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (g_active.load(std::memory_order_acquire)) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_emit) {
            lock.unlock();
            EmitWindow(health, cpu, run, last_thermal, last_emit_ns, false);
            lock.lock();
            next_emit = std::chrono::steady_clock::now() + std::chrono::seconds(g_interval_seconds);
        }
        if (now >= next_mirror) {
            lock.unlock();
            MirrorCopyDXVKLogsAsync();
            lock.lock();
            next_mirror = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        }
        const auto wake = std::min(next_emit, next_mirror);
        g_wake.wait_until(lock, wake, [] { return !g_active.load(std::memory_order_acquire); });
    }
    lock.unlock();
    if (detailed) {
        EmitWindow(health, cpu, run, last_thermal, last_emit_ns, true);
        LogRunSummary(run);
    }
}

void RunDiagnosticsWorker() noexcept {
    try {
        DiagnosticsWorker();
    } catch (const std::exception& error) {
        Log("diagnostics: worker stopped after an error: %s", error.what());
    } catch (...) {
        Log("diagnostics: worker stopped after an unknown error");
    }
    // Never leave hot-path sample collection running without a consumer, even if the worker failed.
    g_active.store(false, std::memory_order_release);
    g_detailed.store(false, std::memory_order_release);
    SetPresentDetailedEnabled(false);
}

}  // namespace

void RecordEngineLoop(int64_t at_ns, int64_t service_ns, int64_t event_pump_ns, int64_t limiter_ns, int64_t focus_ns) {
    if (!g_active.load(std::memory_order_relaxed) || !g_detailed.load(std::memory_order_relaxed)) return;
    thread_local const int tid = int(gettid());
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    g_service_timing.Add(at_ns, service_ns, tid);
    g_pump_timing.Add(at_ns, event_pump_ns, tid);
    if (limiter_ns >= 0) g_limiter_timing.Add(at_ns, limiter_ns, tid);
    g_focus_timing.Add(at_ns, focus_ns, tid);
}

void DiagnosticsStart() {
    std::lock_guard<std::mutex> lock(g_lifecycle_mutex);
    if (g_thread.joinable() || g_active.load()) return;
    const bool detailed = SettingInt("detailed_diagnostics", 1) != 0;
    g_detailed.store(detailed, std::memory_order_release);
    SetPresentDetailedEnabled(detailed);
    g_interval_seconds = detailed ? std::clamp(SettingInt("detailed_diagnostics_interval", 5), 1, 60) : 0;
    if (detailed) {
        LogDeviceInfo();
        Log("diagnostics: enabled; window=%ds; native/DXVK timestamps are monotonic seconds with thread IDs; "
            "Present wall is not GPU time",
            g_interval_seconds);
    } else {
        Log("diagnostics: detailed metrics disabled; shared log mirroring remains active");
    }
    g_active.store(true, std::memory_order_release);
    try {
        g_thread = std::thread(RunDiagnosticsWorker);
    } catch (const std::exception& error) {
        g_active.store(false, std::memory_order_release);
        g_detailed.store(false, std::memory_order_release);
        SetPresentDetailedEnabled(false);
        Log("diagnostics: worker creation failed: %s", error.what());
    }
}

void DiagnosticsStop() {
    {
        std::lock_guard<std::mutex> lock(g_lifecycle_mutex);
        if (!g_thread.joinable()) {
            g_active.store(false, std::memory_order_release);
            g_detailed.store(false, std::memory_order_release);
            SetPresentDetailedEnabled(false);
            return;
        }
        g_active.store(false, std::memory_order_release);
    }
    g_wake.notify_all();
    g_thread.join();
    g_detailed.store(false, std::memory_order_release);
    SetPresentDetailedEnabled(false);
}

}  // namespace native
