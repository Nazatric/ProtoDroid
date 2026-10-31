// Vulkan driver selection on Android. DXVK needs Vulkan features that Qualcomm's own Adreno drivers often lack
// (robustness2 null descriptors, extended dynamic state, ...), so on Adreno the port loads Mesa's Turnip driver
// shipped in the APK, through libadrenotools (which gets a driver past Android's linker namespaces), and hands its
// vkGetInstanceProcAddr to DXVK (DXVK_VK_GIPA, android/patches/dxvk-android.patch). Turnip is used only if it
// finds the GPU; otherwise, and on other GPUs (Mali, PowerVR, Xclipse), the system driver stays.
// Setting vulkan_driver: auto (default) | system | turnip | custom. Custom also allows a probed Mali/PanVK package.
#include "guest.h"

#if defined(__ANDROID__) && defined(PROTODROID_ADRENOTOOLS)
#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/system_properties.h>
#include <sys/wait.h>
#include <unistd.h>

#include <adrenotools/driver.h>
#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "turnip_load.h"

// libadrenotools is built for API 29, where libc exports android_get_device_api_level; libmain targets API 26,
// whose headers only have an inline version, so the Makefile points the symbol here (--defsym).
extern "C" int protodroid_device_api_level() {
    char value[PROP_VALUE_MAX] = {};
    __system_property_get("ro.build.version.sdk", value);
    return atoi(value);
}

namespace native {

namespace {

// Logs the system driver's GPU, for reports from phones where Turnip is not used. Vulkan 1.3 is preferred; 1.2
// requires DXVK's extension-equivalent path. Older drivers cannot run the game, so say that plainly instead of letting
// DXVK fail later with "no adapters found".
// The system driver's GPU and physical-device Vulkan version (0 when it has no device); logged.
uint32_t SystemDriverApi(std::string& device) {
    // Keep the system loader and its dependencies visible to subsequent
    // Vulkan users (including DXVK). The handle is intentionally retained.
    void* lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_GLOBAL);
    auto gipa = lib ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(lib, "vkGetInstanceProcAddr")) : nullptr;
    if (!gipa) {
        const char* error = dlerror();
        Log("vulkan: system loader unavailable: %s", error ? error : "vkGetInstanceProcAddr not exported");
        return 0;
    }
    Log("vulkan: system vkGetInstanceProcAddr resolved (RTLD_GLOBAL)");
    std::string report;
    ProbeInfo info;
    device = ProbeDevice(gipa, "system driver", report, &info);
    Log("%s", report.c_str());
    return device.empty() ? 0 : info.api_version;
}

// Logs the device extensions of the system driver: for drivers below Vulkan 1.3, which DXVK could only use through
// the extensions that became core in 1.2/1.3 (dynamic rendering, synchronization2, timeline semaphores, ...).
void LogSystemExtensions() {
    void* lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_GLOBAL);
    auto gipa = lib ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(lib, "vkGetInstanceProcAddr")) : nullptr;
    auto create = gipa ? reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr, "vkCreateInstance")) : nullptr;
    if (!create) {
        const char* error = dlerror();
        Log("vulkan: cannot query system extensions: %s", error ? error : "vkCreateInstance not exported");
        return;
    }
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    if (create(&ci, nullptr, &instance) != VK_SUCCESS) return;
    auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(gipa(instance, "vkEnumeratePhysicalDevices"));
    auto exts = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
        gipa(instance, "vkEnumerateDeviceExtensionProperties"));
    auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(gipa(instance, "vkDestroyInstance"));
    uint32_t count = 1;
    VkPhysicalDevice device = VK_NULL_HANDLE;
    if (enumerate && exts && enumerate(instance, &count, &device) >= 0 && device) {
        uint32_t n = 0;
        exts(device, nullptr, &n, nullptr);
        std::vector<VkExtensionProperties> list(n);
        exts(device, nullptr, &n, list.data());
        std::string line;
        for (uint32_t i = 0; i < n; ++i) {
            line += line.empty() ? "" : " ";
            line += list[i].extensionName;
            if (line.size() > 400 || i + 1 == n) {
                Log("vulkan: system driver extensions: %s", line.c_str());
                line.clear();
            }
        }
    }
    if (destroy) destroy(instance, nullptr);
}

void LogSystemDevice() {
    std::string device;
    uint32_t api = SystemDriverApi(device);
    if (!device.empty() && api < kVulkanApi13) LogSystemExtensions();
    if (!device.empty() && api < kVulkanApi12) {
        const bool adreno = access("/dev/kgsl-3d0", F_OK) == 0;
        char msg[512];
        snprintf(msg, sizeof(msg),
                 "The GPU driver of this phone (%s) supports Vulkan %u.%u; the game needs Vulkan 1.2 with its required extensions, or 1.3. %s",
                 device.c_str(), VK_API_VERSION_MAJOR(api), VK_API_VERSION_MINOR(api),
                 adreno ? "The bundled Turnip driver did not work on this GPU either."
                        : "A system update may bring a newer driver.");
        StartupFailure(msg);
    }
}

// Loads Turnip through adrenotools (in the libvulkan.so loader it returns, Turnip replaces the system driver) and
// checks that the device really is Turnip: if Turnip itself fails to load, the adrenotools hook quietly falls back
// to the system driver, and a second copy of that driver in the process must not be used.
// Why the test failed: an app may read its own logcat, so the test process's lines (bionic's abort message,
// Mesa's and adrenotools' logs) go to proto-native.log, where the reports come from.
void LogChildLogcat(pid_t pid) {
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "logcat -d -b main,crash -v tag --pid=%d 2>/dev/null", int(pid));
    FILE* p = popen(cmd, "r");
    if (!p) return;
    char line[512];
    for (int n = 0; n < 80 && fgets(line, sizeof(line), p);) {
        line[strcspn(line, "\n")] = 0;
        if (!*line || strstr(line, "beginning of")) continue;
        Log("vulkan test logcat: %s", line);
        ++n;
    }
    pclose(p);
}

// Tries the driver in a separate process first, so that a driver that crashes or hangs on this phone costs a second
// of startup instead of the game. The process is the probe program (turnip_probe.cpp, shipped as
// libturnipprobe.so) started with exec: a plain fork of the app is no good, because Android's Vulkan loader reports
// GPU stats over Binder when it loads a driver, and libbinder aborts in a forked child (seen on Adreno 710, not on
// every start). Returns true if the probe found a usable device; its report comes back through a pipe.
bool TurnipWorksInChild(const Driver& d) {
    const std::string probe = d.hook_dir + "libturnipprobe.so";
    if (access(probe.c_str(), X_OK) != 0) {
        Log("vulkan: no driver probe program, %s %s", d.custom ? "rejecting imported driver" : "trying",
            d.file.c_str());
        return !d.custom;  // an imported driver is never loaded into gameplay without a successful probe
    }
    int fds[2];
    if (pipe(fds) != 0) return !d.custom;
    const char* argv[] = {probe.c_str(), d.hook_dir.c_str(), d.dir.c_str(), d.file.c_str(), d.custom ? "1" : "0",
                          d.direct_icd ? "1" : "0", nullptr};
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return !d.custom;
    }
    if (pid == 0) {  // only async-signal-safe calls until exec
        dup2(fds[1], 1);
        close(fds[0]);
        close(fds[1]);
        alarm(5);
        execv(argv[0], const_cast<char* const*>(argv));
        _exit(127);
    }
    close(fds[1]);
    std::string report;
    char buf[256];
    for (int waited = 0; waited < 4000;) {  // a working driver answers in well under a second
        pollfd p = {fds[0], POLLIN, 0};
        int n = poll(&p, 1, 100);
        waited += 100;
        if (n > 0) {
            ssize_t got = read(fds[0], buf, sizeof(buf));
            if (got <= 0) break;  // child closed the pipe: done
            report.append(buf, size_t(got));
        }
    }
    close(fds[0]);
    int status = 0;
    pid_t done = 0;
    for (int i = 0; i < 20 && (done = waitpid(pid, &status, WNOHANG)) == 0; ++i) usleep(50000);
    if (done == 0) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        Log("vulkan: %s test hung", d.file.c_str());
        return false;
    }
    if (!report.empty()) Log("%s (test process)", report.c_str());
    if (WIFSIGNALED(status)) {
        Log("vulkan: %s test crashed (signal %d)", d.file.c_str(), WTERMSIG(status));
        LogChildLogcat(pid);
        return false;
    }
    bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (!ok) LogChildLogcat(pid);
    return ok;
}

// The GPU's model from KGSL ("Adreno840v2"), or "" where the file is missing or not readable.
std::string GpuModel() {
    char buf[64] = {};
    if (FILE* f = fopen("/sys/class/kgsl/kgsl-3d0/gpu_model", "r")) {
        if (!fgets(buf, sizeof(buf), f)) buf[0] = 0;
        fclose(f);
    }
    buf[strcspn(buf, "\r\n")] = 0;
    return buf;
}

// Crash guard (working directory = the app's files folder): while a Turnip run has not yet reached
// VulkanDriverConfirm, a crash (the crash handler calls VulkanDriverCrashed) writes turnip_failed, and auto mode then
// uses the system driver for this build, unless that driver cannot run the game anyway (Vulkan < 1.2: nothing to
// lose by trying Turnip again). Quitting, being closed from the launcher or killed by the system is no failure: it
// used to count as one and locked phones whose only usable driver is Turnip out of the game. vulkan_driver = turnip
// forces Turnip. A failed test in the child process is not recorded: it costs a second and is retried at every start.
constexpr const char* kFailed = "turnip_failed";
constexpr const char* kBuild = __DATE__ " " __TIME__;  // a failure recorded by another build is retried
bool g_turnip_pending = false;

// The recorded reason if Turnip failed with this build, "" otherwise.
std::string FailedBefore() {
    FILE* f = fopen(kFailed, "r");
    if (!f) return "";
    char build[64] = {}, reason[256] = {};
    fgets(build, sizeof(build), f);
    fgets(reason, sizeof(reason), f);
    fclose(f);
    build[strcspn(build, "\n")] = 0;
    reason[strcspn(reason, "\n")] = 0;
    if (strcmp(build, kBuild) != 0) return "";
    return reason[0] ? reason : "unknown";
}

}  // namespace

void VulkanDriverAbandon() { g_turnip_pending = false; }

void VulkanDriverConfirm() {
    if (!g_turnip_pending) return;
    g_turnip_pending = false;
    Log("vulkan: custom/bundled driver startup ok");
}

// From the crash handler: only open/write/close.
void VulkanDriverCrashed() {
    if (!g_turnip_pending) return;
    int fd = open(kFailed, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    static const char reason[] = "\nthe game crashed during startup on a custom/bundled Vulkan driver (see proto-native.prev.log)\n";
    (void)!write(fd, kBuild, strlen(kBuild));
    (void)!write(fd, reason, sizeof(reason) - 1);
    close(fd);
}

void SelectVulkanDriver() {
    std::string mode = Setting("vulkan_driver", "auto");
    if (mode != "auto" && mode != "system" && mode != "turnip" && mode != "custom") {
        Log("vulkan: unknown vulkan_driver '%s', using auto", mode.c_str());
        mode = "auto";
    }
    char soc[PROP_VALUE_MAX] = {};
    __system_property_get("ro.soc.model", soc);
    // SM8735 is an A825/KGSL platform even if Android restricts the model/device sysfs node.
    const bool adreno = access("/dev/kgsl-3d0", F_OK) == 0 || StartsWithText(soc, "SM8735");
    const char* custom = getenv("PROTODROID_CUSTOM_DRIVER");
    const char* custom_backend = getenv("PROTODROID_CUSTOM_DRIVER_BACKEND");
    const bool direct_custom = custom_backend &&
            (strcmp(custom_backend, "panvk-kbase") == 0 || strcmp(custom_backend, "panvk") == 0);
    const bool custom_available = custom && *custom && access(custom, R_OK) == 0;
    Log("vulkan: requested driver mode %s%s", mode.c_str(),
        mode == "custom" && direct_custom ? " (direct PanVK ICD)" : "");
    if (custom && *custom && !custom_available) Log("vulkan: imported driver is not readable: %s", custom);

    if (mode == "system") {
        LogSystemDevice();
        return;
    }
    if (mode == "custom" && !custom_available) {
        Log("vulkan: custom mode has no readable imported driver, using automatic driver selection");
        mode = "auto";
    }
    if (mode == "custom" && custom_available && direct_custom) {
        // PanVK-kbase otherwise defaults to lossy BC transcode/AFRC paths on some Mali GPUs. These are PanVK-only
        // settings: keep exact decoded textures and do not trade the game's accepted rendering quality for speed.
        setenv("PANVK_BC_MODE", "exact", 1);
        setenv("PANVK_BC_AFRC", "0", 1);
        setenv("PANVK_BC_BC6H", "exact", 1);
        // PanVK is an explicit experimental Custom choice, not an Auto substitution for a working Mali system driver.
        setenv("PAN_I_WANT_A_BROKEN_VULKAN_DRIVER", "1", 1);
    }
    if (mode == "turnip" && !adreno) {
        Log("vulkan: Turnip is Adreno-only; use custom mode for a compatible Mali/PanVK package");
        LogSystemDevice();
        return;
    }
    if (mode == "custom" && custom_available && !direct_custom && !adreno) {
        Log("vulkan: selected package is Turnip/AdrenoTools, which is Adreno-only on this Mali/non-Adreno device; using System");
        LogSystemDevice();
        return;
    }
    if (mode == "auto" && !adreno) {
        LogSystemDevice();
        return;
    }

    unlink("turnip_pending");  // left by older builds
    if (mode == "turnip" || mode == "custom") {
        unlink(kFailed);  // an explicit selection retries that driver
    } else if (std::string why = FailedBefore(); !why.empty()) {
        Log("vulkan: a custom/bundled driver failed here before: %s", why.c_str());
        std::string device;
        if (SystemDriverApi(device) >= kVulkanApi12) {
            Log("vulkan: using the system driver (explicit Turnip/custom selection retries)");
            return;
        }
        Log("vulkan: the system driver cannot run the game, trying the driver again");
    }

    const std::string model = adreno ? GpuModel() : "";
    if (!model.empty()) Log("vulkan: GPU %s", model.c_str());
    const bool adreno825 = PreferGen8Turnip(model.c_str(), soc);
    if (adreno825 && model.empty()) Log("vulkan: SoC %s: selecting the Adreno 825 driver policy", soc);
    const bool adreno8 = adreno && (model.compare(0, 7, "Adreno8") == 0 || adreno825);
    const bool gen8_auto = mode == "auto" && adreno825;
    const char* libdir = getenv("PROTODROID_NATIVE_LIB_DIR");  // set by ProtoActivity: extracted APK libraries
    if (!libdir) {
        LogSystemDevice();
        return;
    }
    const std::string hooks = std::string(libdir) + "/";
    std::vector<Driver> drivers;
    if (mode == "custom" && custom_available) {
        std::string path = custom;
        const size_t slash = path.rfind('/');
        if (slash != std::string::npos) {
            drivers.push_back({hooks, path.substr(0, slash + 1), path.substr(slash + 1), true, direct_custom});
            Log("vulkan: custom package %s, backend %s", path.substr(slash + 1).c_str(),
                direct_custom ? "panvk-kbase/direct-icd" : "turnip/adrenotools");
        }
    }
    auto add_bundled = [&](const char* file) {
        if (access((hooks + file).c_str(), F_OK) == 0) drivers.push_back({hooks, hooks, file, false});
    };

    if (adreno && mode != "custom") {
        if (adreno8) {
            if (mode == "turnip") {
                add_bundled("libvulkan_freedreno_gen8.so");
                add_bundled("libvulkan_freedreno.so");
                add_bundled("libvulkan_freedreno_next.so");
            } else if (gen8_auto) {
                Log("vulkan: Adreno 825: trying the A8xx Turnip build before Qualcomm's driver");
                add_bundled("libvulkan_freedreno_gen8.so");
            } else {
                Log("vulkan: Adreno 8xx: keeping the system driver in auto mode; Turnip/custom remain available");
            }
        } else {
            // Preserve the established A6xx/A7xx order: stable 26.2, then the 26.3 A710/A720 candidate.
            add_bundled("libvulkan_freedreno.so");
            add_bundled("libvulkan_freedreno_next.so");
        }
    }

    PFN_vkGetInstanceProcAddr gipa = nullptr;
    std::string device;
    for (const Driver& d : drivers) {
        if (!TurnipWorksInChild(d)) continue;
        std::string report;
        gipa = LoadTurnip(d, device, report);
        Log("%s (%s)", report.c_str(), d.custom ? "the imported driver" : d.file.c_str());
        break;  // a second driver in this process after a failed load is not worth the risk
    }
    if (!gipa) {
        Log("vulkan: no usable custom/bundled driver, using the system driver");
        LogSystemDevice();
        return;
    }
    char hex[32];
    snprintf(hex, sizeof(hex), "%llx", (unsigned long long)reinterpret_cast<uintptr_t>(gipa));
    setenv("DXVK_VK_GIPA", hex, 1);
    Log("vulkan: using %s", device.c_str());
    g_turnip_pending = true;
}

}  // namespace native

#else

namespace native {

void SelectVulkanDriver() {}
void VulkanDriverConfirm() {}
void VulkanDriverAbandon() {}
void VulkanDriverCrashed() {}

}  // namespace native

#endif
