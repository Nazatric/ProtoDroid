// Vulkan driver selection on Android. DXVK needs Vulkan features that Qualcomm's own Adreno drivers often lack
// (robustness2 null descriptors, extended dynamic state, ...), so on Adreno the port loads Mesa's Turnip driver
// shipped in the APK, through libadrenotools (which gets a driver past Android's linker namespaces), and hands its
// vkGetInstanceProcAddr to DXVK (DXVK_VK_GIPA, android/patches/dxvk-android.patch). Turnip is used only if it
// finds the GPU; otherwise, and on other GPUs (Mali, PowerVR, Xclipse), the system driver stays.
// Setting vulkan_driver: auto (default) | system | turnip | custom. Custom also allows a probed Mali/PanVK package.
#include "guest.h"

#include <cstdlib>
#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

#include "exports.h"

// libadrenotools is built for API 29, where libc exports android_get_device_api_level; libmain targets API 26, whose
// headers only have an inline version, so the Makefile points that name here (--defsym). The definition lives outside
// the Adreno-only block because src/native/libmain.map keeps the symbol in .dynsym in every Android build, including
// an x86_64 one built without the Turnip/AdrenoTools libraries. It needs PROTODROID_EXPORT: the Android build compiles
// with -fvisibility=hidden, and a version script cannot export a symbol the compiler hid.
extern "C" PROTODROID_EXPORT int protodroid_device_api_level() {
#ifdef __ANDROID__
    char value[PROP_VALUE_MAX] = {};
    __system_property_get("ro.build.version.sdk", value);
    return atoi(value);
#else
    // No Android API level outside Android; the symbol exists so the export map stays linkable everywhere.
    return 0;
#endif
}

#if defined(__ANDROID__) && defined(PROTODROID_ADRENOTOOLS)
#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <adrenotools/driver.h>
#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "diagnostic_tour.h"
#include "turnip_load.h"
#include "driver_recovery.h"

namespace native {

namespace {

// Logs the system driver's GPU, for reports from phones where Turnip is not used. Vulkan 1.3 is preferred; 1.2
// requires DXVK's extension-equivalent path. Older drivers cannot run the game, so say that plainly instead of letting
// DXVK fail later with "no adapters found".
// The system driver's GPU and physical-device Vulkan version (0 when it has no device); logged.
uint32_t SystemDriverApi(std::string& device, ProbeInfo* out = nullptr) {
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
    if (out) *out = info;
    return info.api_version;
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
    ProbeInfo info;
    uint32_t api = SystemDriverApi(device, &info);
    if (api < kVulkanApi13) LogSystemExtensions();
    if (device.empty() || info.usable_api < kVulkanApi12) {
        const bool adreno = access("/dev/kgsl-3d0", F_OK) == 0;
        char msg[768];
        snprintf(msg, sizeof(msg),
                 "The direct Vulkan path is unusable (%s, device Vulkan %u.%u, loader %u.%u); the game needs Vulkan 1.2 with its required extensions, or 1.3. %s",
                 device.empty() ? "no directly negotiated device" : device.c_str(),
                 VK_API_VERSION_MAJOR(api), VK_API_VERSION_MINOR(api),
                 VK_API_VERSION_MAJOR(info.loader_api), VK_API_VERSION_MINOR(info.loader_api),
                 adreno ? "The explicitly selected/bundled driver did not work on this GPU either."
                        : "A system update may bring a newer driver. Auto mode can also try the compatibility wrapper.");
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
        return !(d.custom || d.compatibility_wrapper);  // imported/wrapper drivers require a successful isolated probe
    }
    int fds[2];
    if (pipe(fds) != 0) return !(d.custom || d.compatibility_wrapper);
    const char* argv[] = {probe.c_str(), d.hook_dir.c_str(), d.dir.c_str(), d.file.c_str(), d.custom ? "1" : "0",
                          d.direct_icd ? "1" : "0", d.compatibility_wrapper ? "1" : "0", nullptr};
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return !(d.custom || d.compatibility_wrapper);
    }
    if (pid == 0) {  // only async-signal-safe calls until exec
        dup2(fds[1], 1);
        close(fds[0]);
        close(fds[1]);
        alarm(d.compatibility_wrapper ? 15 : 5);
        execv(argv[0], const_cast<char* const*>(argv));
        _exit(127);
    }
    close(fds[1]);
    std::string report;
    char buf[256];
    const int pipe_wait_ms = d.compatibility_wrapper ? 14000 : 4000;
    for (int waited = 0; waited < pipe_wait_ms;) {  // ordinary drivers answer quickly; wrapper initialization gets a bounded longer window
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
// VulkanDriverConfirm, a crash (the crash handler calls VulkanDriverCrashed) writes turnip_failed. Auto then keeps a
// genuinely usable system driver, or runs the normal direct/wrapper policy when Vulkan < 1.2 instead of crash-looping
// the same failed Turnip build. Quitting, being closed from the launcher or killed by the system is no failure: it
// used to count as one and locked phones whose only usable driver is Turnip out of the game. vulkan_driver = turnip
// forces Turnip. A failed test in the child process is not recorded: it costs a second and is retried at every start.
constexpr const char* kFailed = "turnip_failed";
constexpr const char* kWrapperFailed = "vulkan_wrapper_failed";
constexpr const char* kBuild = __DATE__ " " __TIME__;  // a failure recorded by another build is retried
bool g_turnip_pending = false;
bool g_wrapper_pending = false;
bool g_auto_recovery_pending = false;

// The recorded reason if Turnip failed with this build, "" otherwise.
std::string FailedBefore() {
    return ReadDriverFailureMarker(kFailed, kBuild);
}

// Auto-only last resort for a direct system path below 1.2. This wrapper always sits around Android's system Vulkan
// loader; it is not a custom Turnip/PanVK route. A child must prove the effective DXVK feature set before the real
// process repeats the load. The pending marker covers that second, unavoidable in-process portion.
void ConfigureWrapperLogging(bool probe) {
    const char* directory = getenv("DXVK_LOG_PATH");
    const std::string root = directory && *directory ? directory : ".";
    const std::string path = root + (probe ? "/vulkan_wrapper.probe.log" : "/vulkan_wrapper.log");
    const std::string diag_path = root + (probe ? "/vulkan_wrapper.probe-diag.txt" : "/vulkan_wrapper.diag.txt");
    const std::string work_dir = root + (probe ? "/vulkan-wrapper-probe" : "/vulkan-wrapper");
    const char* cache_directory = getenv("DXVK_STATE_CACHE_PATH");
    const std::string cache_root = cache_directory && *cache_directory ? cache_directory : ".";
    const std::string cache_dir = cache_root + (probe ? "/vulkan-wrapper-probe-cache" : "/vulkan-wrapper-cache");
    const bool tour = DiagnosticTourEnabled();
    setenv("WRAPPER_LOG_LEVEL", tour ? "info,error,shader,bcn" : "info,error", 1);
    setenv("WRAPPER_LOG_FILE", path.c_str(), 1);
    setenv("WRAPPER_WORK_DIR", work_dir.c_str(), 1);
    setenv("WRAPPER_CACHE_PATH", cache_dir.c_str(), 1);
    setenv("WRAPPER_DIAG_FILE", diag_path.c_str(), 1);
    setenv("WRAPPER_DIAG_APPID", "com.protodroid", 1);
    // Mesa implements the core-1.3 lowering for a DXVK/1.3 application request, but its
    // wrapper_effective_api_version() still reports the base version unless this override is present. Advertising
    // the effective interface is only half of negotiation: ProbeRequiredDxvkFeatures below must still validate the
    // actual lowered features/extensions before this route can reach DXVK. Never set this for a direct driver.
    setenv("WRAPPER_VK_VERSION", "1.3", 1);
    // GameNative's diagnostic mode includes per-texture output; shader/BC logging adds further large artifacts. Keep
    // that cost out of ordinary Play; the deliberate tour opts into it and uses its isolated log directory.
    setenv("WRAPPER_DIAG", tour ? "1" : "0", 1);
}

bool TryVulkanWrapper(std::string& device, std::string& failure) {
    if (std::string why = ReadDriverFailureMarker(kWrapperFailed, kBuild); !why.empty()) {
        failure = "the wrapper previously failed during in-process startup: " + why;
        Log("vulkan: not automatically retrying the failed compatibility wrapper: %s", why.c_str());
        return false;
    }
    const char* libdir = getenv("PROTODROID_NATIVE_LIB_DIR");
    if (!libdir || !*libdir) {
        failure = "the APK native-library directory is unavailable";
        return false;
    }
    const std::string directory = std::string(libdir) + "/";
    if (access((directory + "libvulkan_wrapper.so").c_str(), R_OK) != 0) {
        failure = "libvulkan_wrapper.so is missing or unreadable in the APK";
        Log("vulkan: %s", failure.c_str());
        return false;
    }
    ConfigureWrapperLogging(true);
    Driver driver{directory, directory, "libvulkan_wrapper.so", false, true, true};
    if (!TurnipWorksInChild(driver)) {
        failure = "the isolated wrapper probe did not expose a usable DXVK device (see the probe report and vulkan_wrapper.probe.log)";
        return false;
    }

    ConfigureWrapperLogging(false);
    g_wrapper_pending = true;
    std::string report;
    PFN_vkGetInstanceProcAddr gipa = LoadTurnip(driver, device, report);
    Log("%s (compatibility wrapper)", report.c_str());
    if (!gipa) {
        g_wrapper_pending = false;
        failure = report.empty() ? "the in-process wrapper load failed" : report;
        return false;
    }
    char hex[32];
    snprintf(hex, sizeof(hex), "%llx", (unsigned long long)reinterpret_cast<uintptr_t>(gipa));
    setenv("DXVK_VK_GIPA", hex, 1);
    Log("vulkan: wrapper vk_icdGetInstanceProcAddr handed to DXVK through DXVK_VK_GIPA");
    return true;
}

// Probe the direct route first and keep it whenever it is genuinely negotiable. Only the otherwise-unusable Auto path
// reaches the wrapper. This function is also the final Auto fallback after established Adreno/Turnip attempts.
void AutomaticSystemDevice() {
    std::string device;
    ProbeInfo info;
    const uint32_t api = SystemDriverApi(device, &info);
    if (!device.empty() && info.usable_api >= kVulkanApi12) {
        Log("vulkan: Auto keeps the direct system Vulkan %u.%u path",
            VK_API_VERSION_MAJOR(info.usable_api), VK_API_VERSION_MINOR(info.usable_api));
        return;
    }
    if (api < kVulkanApi13) LogSystemExtensions();

    std::string wrapper_device, failure;
    if (ShouldProbeVulkanWrapper(true, device.empty() ? 0 : info.usable_api) &&
        TryVulkanWrapper(wrapper_device, failure)) {
        Log("vulkan: Auto uses the compatibility wrapper for %s (base device Vulkan %u.%u; effective device %s)",
            device.empty() ? "the system GPU" : device.c_str(), VK_API_VERSION_MAJOR(api), VK_API_VERSION_MINOR(api),
            wrapper_device.c_str());
        return;
    }

    char msg[1536];
    snprintf(msg, sizeof(msg),
             "No usable Vulkan path was found. Direct system device: %s (device Vulkan %u.%u, loader %u.%u). "
             "The Auto compatibility wrapper was not used: %s. Check proto-native.log and vulkan_wrapper.log for the missing capability or probe failure.",
             device.empty() ? "not negotiated" : device.c_str(), VK_API_VERSION_MAJOR(api), VK_API_VERSION_MINOR(api),
             VK_API_VERSION_MAJOR(info.loader_api), VK_API_VERSION_MINOR(info.loader_api), failure.c_str());
    StartupFailure(msg);
}

}  // namespace

void VulkanDriverAbandon() {
    g_turnip_pending = false;
    g_wrapper_pending = false;
    g_auto_recovery_pending = false;
}

void VulkanDriverConfirm() {
    if (g_wrapper_pending) {
        g_wrapper_pending = false;
        g_auto_recovery_pending = false;
        Log("vulkan: compatibility wrapper startup ok");
    }
    if (!g_turnip_pending) return;
    g_turnip_pending = false;
    g_auto_recovery_pending = false;
    Log("vulkan: custom/bundled driver startup ok");
}

// A graceful DXVK initialization failure is just as much a failed driver attempt as a native crash. Record it before
// StartupFailure abandons the active attempt; only Auto promises a fresh direct/wrapper policy on the next launch.
bool VulkanDriverStartupFailed(const char* reason) {
    if (g_wrapper_pending) {
        if (WriteDriverFailureMarker(kWrapperFailed, kBuild, reason))
            Log("vulkan: recorded compatibility-wrapper initialization failure; Auto will not crash-loop this build");
        else
            Log("vulkan: could not record the compatibility wrapper's initialization failure");
        return false;  // there is no usable direct <1.2 fallback to promise for the next launch
    }
    if (!g_turnip_pending) return false;
    const bool automatic = g_auto_recovery_pending;
    if (!WriteDriverFailureMarker(kFailed, kBuild, reason)) {
        Log("vulkan: could not record the selected driver's initialization failure");
        return false;
    }
    Log("vulkan: recorded selected-driver initialization failure: %s", reason);
    if (automatic) Log("vulkan: Auto will re-check the direct system/wrapper policy on the next launch");
    return automatic;
}

// From the crash handler: only open/write/close.
void VulkanDriverCrashed() {
    if (g_wrapper_pending) {
        WriteDriverFailureMarker(kWrapperFailed, kBuild,
                                 "the game crashed during compatibility-wrapper startup (see proto-native.prev.log)");
        return;
    }
    if (!g_turnip_pending) return;
    WriteDriverFailureMarker(kFailed, kBuild,
                             "the game crashed during startup on a custom/bundled Vulkan driver (see proto-native.prev.log)");
}

void SelectVulkanDriver() {
    // A previous SDL activity in this process may have used Turnip, PanVK, or the wrapper. Never let its function
    // pointer bypass the direct path selected for this launch.
    unsetenv("DXVK_VK_GIPA");
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
        AutomaticSystemDevice();
        return;
    }

    unlink("turnip_pending");  // left by older builds
    if (mode == "turnip" || mode == "custom") {
        unlink(kFailed);  // an explicit selection retries that driver
    } else if (std::string why = FailedBefore(); !why.empty()) {
        Log("vulkan: a custom/bundled driver failed here before: %s", why.c_str());
        std::string device;
        ProbeInfo info;
        SystemDriverApi(device, &info);
        if (!device.empty() && info.usable_api >= kVulkanApi12) {
            Log("vulkan: using the system driver (explicit Turnip/custom selection retries)");
            return;
        }
        // A graceful DXVK failure can happen after the isolated Turnip probe succeeded. On a direct <1.2 path,
        // retrying that same Turnip build would repeat the failure on every Auto launch. The established wrapper
        // policy performs a fresh isolated capability probe; explicit Turnip/custom selections are unaffected.
        Log("vulkan: the direct system driver is unusable; trying Auto's compatibility-wrapper fallback");
        AutomaticSystemDevice();
        return;
    }

    const std::string model = adreno ? GpuModel() : "";
    if (!model.empty()) Log("vulkan: GPU %s", model.c_str());
    const bool adreno825 = PreferGen8Turnip(model.c_str(), soc);
    if (adreno825 && model.empty()) Log("vulkan: SoC %s: selecting the Adreno 825 driver policy", soc);
    const bool adreno8 = adreno && (model.compare(0, 7, "Adreno8") == 0 || adreno825);
    const bool gen8_auto = mode == "auto" && adreno825;
    const char* libdir = getenv("PROTODROID_NATIVE_LIB_DIR");  // set by ProtoActivity: extracted APK libraries
    if (!libdir) {
        if (mode == "auto") AutomaticSystemDevice();
        else LogSystemDevice();
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
        Log("vulkan: no usable custom/bundled driver, using the system driver policy");
        if (mode == "auto") AutomaticSystemDevice();
        else LogSystemDevice();
        return;
    }
    char hex[32];
    snprintf(hex, sizeof(hex), "%llx", (unsigned long long)reinterpret_cast<uintptr_t>(gipa));
    setenv("DXVK_VK_GIPA", hex, 1);
    Log("vulkan: using %s", device.c_str());
    g_auto_recovery_pending = mode == "auto";
    g_turnip_pending = true;
}

}  // namespace native

#else

namespace native {

void SelectVulkanDriver() {}
void VulkanDriverConfirm() {}
void VulkanDriverAbandon() {}
bool VulkanDriverStartupFailed(const char*) { return false; }
void VulkanDriverCrashed() {}

}  // namespace native

#endif
