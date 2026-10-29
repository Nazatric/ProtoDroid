// Native host entry point: replaces prototypef.exe. Loads prototypeenginef.dll into guest memory, runs its CRT
// startup (DllMain) and drives the six Engine* exports the way prototypef.exe does:
//   EngineInitialize(&{command line, hwnd, hwnd}) -> bool
//   loop: EngineKeyboardService(msg, wParam, lParam, caps, num) / EngineMouseService(msg, wParam, lParam) per
//         window message, then EngineService(quit_requested, inactive) -> bool (false: leave the loop),
//         EngineWindowFocus(focused)
//   EngineTerminate()
#include <SDL2/SDL.h>
#include <SDL2/SDL_main.h>  // Android: main() becomes SDL_main, called by SDLActivity
#include <dlfcn.h>
#include <sched.h>
#include <signal.h>
#include <ucontext.h>
#include <unwind.h>
#include <unistd.h>
#include <sys/stat.h>
#ifdef __ANDROID__
#include <android/log.h>
#include <jni.h>
#endif

#include <atomic>
#include <chrono>
#include <cerrno>
#include <cctype>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "guest.h"
#include "present_stats.h"
#include "win.h"

namespace native {

const char* g_game_dir = nullptr;
const char* g_data_dir = nullptr;  // Android: the app's external files folder (logs, ini, saves)

namespace {
std::mutex g_log_mutex;
FILE* g_log = nullptr;
FILE* g_log_mirror = nullptr;  // Android 11+: /storage/emulated/0/Prototype/logs/
std::string g_mirror_dir;
std::string g_mirror_log_path;
std::string g_mirror_prev_path;
}  // namespace
void MirrorCopyDXVKLogs();

// Forward for mirroring helper

void Log(const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    va_list ap;
    va_start(ap, fmt);
#ifdef __ANDROID__
    __android_log_vprint(ANDROID_LOG_INFO, "protodroid", fmt, ap);
#else
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
#endif
    va_end(ap);
    if (g_log) {
        va_start(ap, fmt);
        vfprintf(g_log, fmt, ap);
        va_end(ap);
        fputc('\n', g_log);
        fflush(g_log);
    }
    if (g_log_mirror) {
        va_start(ap, fmt);
        vfprintf(g_log_mirror, fmt, ap);
        va_end(ap);
        fputc('\n', g_log_mirror);
        fflush(g_log_mirror);
    }
}

void VulkanDriverAbandon();  // vulkan_driver.cpp
void VulkanDriverCrashed();

namespace {

// Crash report: the fault and the faulting code's library + offset go to the log (and logcat), then the previous
// handler (Android's debuggerd) takes over. Without it a crash on a user's phone just cuts the log off. Formatting
// is not strictly async-signal-safe, which is acceptable for a process that is dying anyway.
struct sigaction g_old_actions[32];

void CrashWrite(const char* text) {
#ifdef __ANDROID__
    __android_log_write(ANDROID_LOG_ERROR, "protodroid", text);
#else
    write(2, text, strlen(text));
    write(2, "\n", 1);
#endif
    if (g_log) {
        int fd = fileno(g_log);
        write(fd, text, strlen(text));
        write(fd, "\n", 1);
        fsync(fd);
    }
    if (g_log_mirror) {
        int fd = fileno(g_log_mirror);
        write(fd, text, strlen(text));
        write(fd, "\n", 1);
        fsync(fd);
    }
}

void DescribeAddress(char* out, size_t size, const char* what, uintptr_t address) {
    Dl_info info{};
    if (address && dladdr(reinterpret_cast<void*>(address), &info) && info.dli_fname) {
        const char* lib = strrchr(info.dli_fname, '/');
        const char* sym = info.dli_sname ? info.dli_sname : "?";
        // Use dladdr to get symbol + offset instead of (?) placeholder
        uintptr_t offset = address - reinterpret_cast<uintptr_t>(info.dli_fbase);
        uintptr_t sym_offset = info.dli_saddr ? address - reinterpret_cast<uintptr_t>(info.dli_saddr) : offset;
        if (info.dli_sname) {
            snprintf(out, size, "crash: %s %#lx = %s+%#lx (%s+%#lx)", what, (unsigned long)address, lib ? lib + 1 : info.dli_fname,
                     (unsigned long)offset, sym, (unsigned long)sym_offset);
        } else {
            snprintf(out, size, "crash: %s %#lx = %s+%#lx (%s)", what, (unsigned long)address, lib ? lib + 1 : info.dli_fname,
                     (unsigned long)offset, sym);
        }
    } else {
        snprintf(out, size, "crash: %s %#lx", what, (unsigned long)address);
    }
}

void OnCrash(int sig, siginfo_t* info, void* context) {
    VulkanDriverCrashed();  // a crash before the driver is confirmed: auto mode avoids Turnip next time
    char line[512];
    snprintf(line, sizeof(line), "crash: signal %d (%s) code %d addr %p on tid %d", sig, strsignal(sig), info->si_code,
             info->si_addr, int(gettid()));
    CrashWrite(line);
#if defined(__aarch64__)
    const auto& mc = static_cast<ucontext_t*>(context)->uc_mcontext;
    DescribeAddress(line, sizeof(line), "pc", mc.pc);
    CrashWrite(line);
    DescribeAddress(line, sizeof(line), "lr", mc.regs[30]);
    CrashWrite(line);
    // Keep the report factual: the signal, fault address and register-backed
    // PCs above are the only information available at this point.
    snprintf(line, sizeof(line), "crash: fault addr %p", info->si_addr);
    CrashWrite(line);
#elif defined(__x86_64__)
    DescribeAddress(line, sizeof(line), "pc", static_cast<ucontext_t*>(context)->uc_mcontext.gregs[REG_RIP]);
    CrashWrite(line);
#endif
    // The callers: for abort() the pc and lr above are both in libc, the caller that gave up is further up.
    struct Walk {
        char* line;
        size_t size;
        int depth;
    } walk = {line, sizeof(line), 0};
    _Unwind_Backtrace(
        [](_Unwind_Context* ctx, void* arg) -> _Unwind_Reason_Code {
            auto* w = static_cast<Walk*>(arg);
            uintptr_t pc = _Unwind_GetIP(ctx);
            if (!pc || w->depth >= 24) return _URC_END_OF_STACK;
            char what[16];
            snprintf(what, sizeof(what), "#%02d", w->depth++);
            DescribeAddress(w->line, w->size, what, pc);
            CrashWrite(w->line);
            return _URC_NO_REASON;
        },
        &walk);
    // Mirror DXVK logs on crash for post-mortem
    ::native::MirrorCopyDXVKLogs();
    if (g_log) fflush(g_log);
    if (g_log_mirror) fflush(g_log_mirror);
    sigaction(sig, &g_old_actions[sig], nullptr);
    if (info->si_code <= 0) raise(sig);  // sent by kill/tgkill/abort: deliver again; a real fault re-runs on return
}

void InstallCrashHandler() {
    struct sigaction sa {};
    sa.sa_sigaction = OnCrash;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) sigaction(sig, &sa, &g_old_actions[sig]);
}

// Mirror helpers for Android 11+ scoped storage: logs to shared storage

static bool EnsureDir(const std::string& path) {
    // mkdir -p
    std::string cur;
    for (size_t i = 0; i < path.size(); ++i) {
        cur += path[i];
        if (path[i] == '/' && cur.size() > 1) {
            mkdir(cur.c_str(), 0755);
        }
    }
    mkdir(path.c_str(), 0755);
    // Check if writable
    return access(path.c_str(), W_OK) == 0;
}

[[maybe_unused]] static void MirrorInit(const char* base) {
    // Try shared storage logs directory
    const char* candidates[] = {
        "/storage/emulated/0/Prototype/logs",
        "/storage/emulated/0/Prototype",
        nullptr
    };
    for (int i = 0; candidates[i]; ++i) {
        if (EnsureDir(candidates[i])) {
            // Test writability by opening a test file
            std::string test = std::string(candidates[i]) + "/.write_test";
            FILE* tf = fopen(test.c_str(), "w");
            if (tf) {
                fclose(tf);
                unlink(test.c_str());
                g_mirror_dir = candidates[i];
                break;
            }
        }
    }
    if (g_mirror_dir.empty()) return;

    // Rotate previous log in mirror as well
    std::string mirror_log = g_mirror_dir + "/proto-native.log";
    std::string mirror_prev = g_mirror_dir + "/proto-native.prev.log";
    // Move previous
    rename(mirror_log.c_str(), mirror_prev.c_str());
    g_mirror_log_path = mirror_log;
    g_mirror_prev_path = mirror_prev;
    g_log_mirror = fopen(mirror_log.c_str(), "w");
    if (g_log_mirror) {
        // Also log mirror path for diagnostics
        // Use direct write to avoid recursion before Log fully ready
        // We'll log after InstallCrashHandler
    }
    // Also handle DXVK logs: ensure dir exists, DXVK will write there via DXVK_LOG_PATH if we set it
    // We set DXVK_LOG_PATH to mirror dir as well via env after MirrorInit
}

static void CopyFile(const std::string& src, const std::string& dst) {
    FILE* in = fopen(src.c_str(), "rb");
    if (!in) return;
    FILE* out = fopen(dst.c_str(), "wb");
    if (!out) { fclose(in); return; }
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
}

}  // namespace

// Defined in native (not the anonymous namespace) so the forward declaration and all callers refer to the same
// symbol. CopyFile and the mirror state remain implementation details of the anonymous namespace above.
void MirrorCopyDXVKLogs() {
    if (g_mirror_dir.empty() || !g_data_dir) return;
    std::string base_log = std::string(g_data_dir) + "/app_process64_d3d9.log";
    std::string mirror_dxvk = g_mirror_dir + "/app_process64_d3d9.log";
    CopyFile(base_log, mirror_dxvk);
    // Also copy state cache if it exists.
    std::string base_cache = std::string(g_data_dir) + "/prototypeenginef.dll.dxvk-cache";
    std::string mirror_cache = g_mirror_dir + "/prototypeenginef.dll.dxvk-cache";
    CopyFile(base_cache, mirror_cache);
}

std::string TakeIoStats();   // kernel32.cpp
std::string ArenaUsage();    // memory.cpp

void Fatal(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Log("FATAL: %s", buf);
    VulkanDriverAbandon();  // not the driver's fault: the next start keeps trying Turnip
    _exit(70);
}

// A start that cannot go on: tell the user and end the process. Returning from main would let SDL run main again in
// the same process, on top of this run's guest memory.
[[noreturn]] void StartupFailure(const char* what) {
    Log("startup failed: %s", what);
    VulkanDriverAbandon();
    std::string msg = std::string(what) + "\n\nDetails are in proto-native.log.";
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Prototype", msg.c_str(), nullptr);
    _exit(1);
}

uint32_t HostWindow();  // user32.cpp
void InitClientSize(int window_w, int window_h);
void StartWatchdog(std::atomic<uint64_t>* frames);  // watchdog.cpp
void InitD3D9Bridge();  // d3d9.cpp
void SelectVulkanDriver();  // vulkan_driver.cpp
void VulkanDriverConfirm();
void InitInput(uint32_t keyboard_service, uint32_t mouse_service);  // input.cpp
void ForwardInput(const SDL_Event& e);
void InjectKey(SDL_Scancode sc, bool down);
extern SDL_Window* g_sdl_window;

}  // namespace native


// ---- frame limiter and presented-frame counters (read by the on-screen overlay through JNI) ----
namespace native {
std::atomic<int> g_fps_limit{0};  // 0 = no limit (the swap interval still applies)

static long ResidentMiB() {
    long pages = 0, resident = 0;
    if (FILE* f = fopen("/proc/self/statm", "r")) {
        if (fscanf(f, "%ld %ld", &pages, &resident) != 2) resident = 0;
        fclose(f);
    }
    return resident * (sysconf(_SC_PAGESIZE) / 1024) / 1024;
}
}  // namespace native

#ifdef __ANDROID__
extern "C" JNIEXPORT jfloat JNICALL Java_com_protodroid_TouchControls_nativeFps(JNIEnv*, jclass) {
    return jfloat(native::GetPresentStats().fps);
}
extern "C" JNIEXPORT jfloat JNICALL Java_com_protodroid_TouchControls_nativeFrameMs(JNIEnv*, jclass) {
    return jfloat(native::GetPresentStats().average_ms);
}
extern "C" JNIEXPORT jfloat JNICALL Java_com_protodroid_TouchControls_nativeFrameP99Ms(JNIEnv*, jclass) {
    return jfloat(native::GetPresentStats().p99_ms);
}
extern "C" JNIEXPORT jint JNICALL Java_com_protodroid_TouchControls_nativeMemoryMiB(JNIEnv*, jclass) {
    return jint(native::ResidentMiB());
}
extern "C" JNIEXPORT void JNICALL Java_com_protodroid_TouchControls_nativeSetFpsLimit(JNIEnv*, jclass, jint fps) {
    native::g_fps_limit.store(fps);
    native::PerfHintTarget(fps);
}
#endif

using namespace native;

#ifdef __ANDROID__
// DXVK and FAudio report errors on stderr, which goes nowhere in an app: forward it to logcat (tag protodroid-err).
static void RedirectStderrToLogcat() {
    static int fds[2];
    if (pipe(fds) != 0) return;
    dup2(fds[1], 2);
    setvbuf(stderr, nullptr, _IOLBF, 0);
    std::thread([] {
        char buf[1024];
        std::string line;
        for (ssize_t n; (n = read(fds[0], buf, sizeof(buf))) > 0;) {
            for (ssize_t i = 0; i < n; ++i) {
                if (buf[i] == '\n') {
                    __android_log_write(ANDROID_LOG_WARN, "protodroid-err", line.c_str());
                    line.clear();
                } else {
                    line += buf[i];
                }
            }
        }
    }).detach();
}
#endif

[[maybe_unused]] static std::string FindGameDir(const char* base, const char* env_game) {
    std::vector<std::string> candidates;
    if (env_game && env_game[0]) candidates.push_back(env_game);
    // Shared storage primary
    candidates.push_back("/storage/emulated/0/Prototype");
    candidates.push_back("/storage/emulated/0/Prototype/game");
    // App private
    if (base) {
        candidates.push_back(std::string(base) + "/game");
        candidates.push_back(std::string(base) + "/../Prototype");
        candidates.push_back(std::string(base) + "/../Prototype/game");
    }
    for (auto& cand : candidates) {
        std::string dll = cand + "/prototypeenginef.dll";
        if (access(dll.c_str(), R_OK) == 0) {
            return cand;
        }
        // Also log for diagnostics when checking via Log after it's available
    }
    return "";
}

int main(int argc, char** argv) {
#ifdef __ANDROID__
    RedirectStderrToLogcat();
    setenv("DXVK_LOG_LEVEL", "info", 0);
#endif
    g_game_dir = argc > 1 ? argv[1] : getenv("PROTODROID_GAME");
#ifdef __ANDROID__
    // Logs, protodroid.ini, saves (native_c) and the DXVK caches live in the app's external files folder
    // (/sdcard/Android/data/<package>/files). The game is <that folder>/game, or wherever SetupActivity found it
    // (PROTODROID_GAME, e.g. /sdcard/Prototype with all files access); SetupActivity checks it before starting us.
    static std::string android_game;
    const char* base = SDL_AndroidGetExternalStoragePath();
    if (!base) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Prototype", SDL_GetError(), nullptr);
        return 1;
    }
    if (chdir(base) != 0) return 1;
    g_data_dir = base;
    // DXVK opens its log lazily, after this setup. Do not point it at a
    // path until the directory has actually been created and checked.
    if (!native::EnsureDir(base)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Prototype",
                                 "The app log directory is not writable.", nullptr);
        return 1;
    }
    // DXVK previously derived app_process64.dxvk-cache from /proc/self/exe. The Android patch now uses a stable game
    // name; preserve pipelines compiled by older builds instead of starting with another empty cache.
    std::string state_cache = std::string(base) + "/prototypeenginef.dll.dxvk-cache";
    std::string legacy_cache = std::string(base) + "/app_process64.dxvk-cache";
    if (access(state_cache.c_str(), F_OK) != 0 && access(legacy_cache.c_str(), R_OK) == 0) {
        if (rename(legacy_cache.c_str(), state_cache.c_str()) == 0) Log("DXVK state cache migrated from app_process64");
        else CopyFile(legacy_cache, state_cache);
    }
    setenv("DXVK_STATE_CACHE_PATH", base, 1);
    setenv("DXVK_LOG_PATH", base, 1);
    Log("DXVK state cache: %s", state_cache.c_str());
    // Attempt to find game dir across multiple locations (Issue 4)
    if (!g_game_dir || access((std::string(g_game_dir) + "/prototypeenginef.dll").c_str(), R_OK) != 0) {
        std::string found = FindGameDir(base, g_game_dir);
        if (!found.empty()) {
            android_game = found;
            g_game_dir = android_game.c_str();
        } else if (!g_game_dir) {
            // Fallback to old behavior
            android_game = std::string(base) + "/game";
            g_game_dir = android_game.c_str();
        }
    }
    if (std::string dll = std::string(g_game_dir) + "/prototypeenginef.dll"; access(dll.c_str(), R_OK) != 0) {
        std::string msg = "Cannot read " + dll + " (" + strerror(errno) + ").\nCopy the game files to one of:\n"
            "/storage/emulated/0/Prototype\n/storage/emulated/0/Prototype/game\n" + std::string(base) + "/game";
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Prototype", msg.c_str(), nullptr);
        return 1;
    }
    // Log rotation + mirroring to shared storage (Issue 3)
    rename("proto-native.log", "proto-native.prev.log");
    // Also rotate mirror logs if exists
    // MirrorInit will handle mirror rotation
    native::MirrorInit(base);
    if (!g_mirror_dir.empty()) {
        // If mirror dir exists, rotate its previous log
        std::string shared_prev = g_mirror_dir + "/proto-native.prev.log";
        std::string shared_cur = g_mirror_dir + "/proto-native.log";
        // Already handled in MirrorInit; also ensure DXVK logs are mirrored
        // Set DXVK_LOG_PATH to mirror dir if we want DXVK logs in shared as well
        // Keep both: base for state cache, mirror for logs visibility
        // We duplicate DXVK logs via periodic copy
    }
#endif
    if (!g_game_dir) {
        fprintf(stderr, "usage: %s <game directory>\n", argv[0]);
        return 2;
    }
    // Rename handled above on Android, for desktop do it here
#ifndef __ANDROID__
    rename("proto-native.log", "proto-native.prev.log");
#endif
    if (!g_log) g_log = fopen("proto-native.log", "w");
    InstallCrashHandler();
    Log("protodroid build %s %s", __DATE__, __TIME__);
#ifdef __ANDROID__
    if (!g_mirror_dir.empty()) {
        Log("logs mirrored to %s", g_mirror_dir.c_str());
        if (g_log_mirror) {
            // Ensure mirror has header too
            fprintf(g_log_mirror, "protodroid build %s %s\n", __DATE__, __TIME__);
            fflush(g_log_mirror);
        }
        // Also set DXVK log path to mirror for immediate visibility, but keep state cache in base
        // DXVK will write app_process64_d3d9.log to DXVK_LOG_PATH; we want it in mirror dir
        // So we set it to mirror dir, but we also need state cache path separate
        // We'll set DXVK_LOG_PATH to mirror dir if writable
        setenv("DXVK_LOG_PATH", g_mirror_dir.c_str(), 1);
        Log("DXVK logs mirrored to %s", g_mirror_dir.c_str());
    }
    // Log game dir detection
    Log("game dir: %s", g_game_dir);
    Log("data dir: %s", g_data_dir ? g_data_dir : "(none)");
#endif
    // DXVK's durable pipeline diagnostics are strictly opt-in. Export this
    // before any DXVK device can be created; PROTODROID_PIPELINE_TRACE itself
    // remains an equivalent direct environment override.
    if (SettingInt("pipeline_trace", 0) == 1) {
        setenv("PROTODROID_PIPELINE_TRACE", "1", 0);
        Log("DXVK pipeline trace enabled (pipeline_trace = 1)");
    }
    InitGuestMemory();
    AttachThread(8u << 20);
    InitD3D9Bridge();
    setenv("DXVK_WSI_DRIVER", "SDL2", 1);  // DXVK native: the HWNDs we pass are SDL windows
    SelectVulkanDriver();  // Adreno: the bundled Turnip driver
    // audio_driver = aaudio | openslES: SDL's audio backend (FAudio opens SDL audio later); AAudio by default.
    if (std::string ad = Setting("audio_driver", ""); !ad.empty()) setenv("SDL_AUDIODRIVER", ad.c_str(), 1);
    if (SettingInt("present_stats", 0)) setenv("DXVK_PRESENT_STATS", "1", 0);
    // Test aid: DXVK's dummy resources instead of null descriptors, as on Mali drivers without VK_EXT_robustness2.
    if (SettingInt("dxvk_no_null_descriptor", 0)) setenv("DXVK_NO_NULL_DESCRIPTOR", "1", 0);  // Present timing in the DXVK log
#ifndef __ANDROID__
    // Desktop retains the optional present_mode/vsync configuration. Android is normalized below and in the D3D9
    // bridge so CreateDevice and every Reset see the same FIFO interval.
    {
        std::string pm = Setting("present_mode", "");
        int vsync = SettingInt("vsync", -1);
        bool wants_fifo = pm == "fifo" || pm == "vsync" || pm == "FIFO" || vsync == 1 ||
                          SettingInt("present_fifo", 0) == 1;
        if (wants_fifo) {
            std::string dc = Setting("dxvk_config", "");
            if (dc.empty()) dc = "d3d9.presentInterval = 1";
            else dc += ";d3d9.presentInterval = 1";
            for (char& ch : dc) if (ch == ',') ch = ';';
            setenv("DXVK_CONFIG", dc.c_str(), 1);
            setenv("PROTODROID_PRESENT_FIFO", "1", 1);
            Log("present mode: FIFO (vsync) forced via protodroid.ini present_mode=%s", pm.c_str());
        } else if (!pm.empty()) {
            Log("present mode: %s", pm.c_str());
        }
    }
#endif
    // dxvk_config = d3d9.presentInterval = 1, d3d9.numBackBuffers = 3  (DXVK options; ',' as ';' starts an ini
    // comment)
    if (std::string dc = Setting("dxvk_config", ""); !dc.empty()) {
        // If we already set DXVK_CONFIG above for present_mode, merge
        const char* existing = getenv("DXVK_CONFIG");
        std::string merged = existing ? existing : "";
        if (!merged.empty()) merged += ";";
        for (char& ch : dc) ch = ch == ',' ? ';' : ch;
        merged += dc;
        setenv("DXVK_CONFIG", merged.c_str(), 1);
    }
#ifdef __ANDROID__
    // Append last so a stale dxvk_config cannot reintroduce an interval change at the post-cutscene Reset.
    std::string present_config = getenv("DXVK_CONFIG") ? getenv("DXVK_CONFIG") : "";
    if (!present_config.empty()) present_config += ";";
    present_config += "d3d9.presentInterval = 1";
    setenv("DXVK_CONFIG", present_config.c_str(), 1);
    setenv("PROTODROID_PRESENT_FIFO", "1", 1);
    Log("present mode: FIFO (vsync), interval 1 for CreateDevice and Reset");
#endif
    // Android starts at 1x for Mali-class tile GPUs. Explicit DXVK configuration wins over this convenience default;
    // the D3D9 bridge still clamps requests to max_anisotropy and converts anisotropic filters to linear at 1x.
    {
#ifdef __ANDROID__
        const bool configure_dxvk = true;
        int max_anisotropy = SettingInt("max_anisotropy", 1);
#else
        const bool configure_dxvk = !Setting("max_anisotropy", "").empty();
        int max_anisotropy = SettingInt("max_anisotropy", 16);
#endif
        max_anisotropy = max_anisotropy < 1 ? 1 : max_anisotropy > 16 ? 16 : max_anisotropy;
        std::string merged = getenv("DXVK_CONFIG") ? getenv("DXVK_CONFIG") : "";
        auto has_aniso_option = [](std::string config) {
            for (char& ch : config) if (ch == ',') ch = ';';
            for (size_t begin = 0; begin <= config.size();) {
                const size_t end = config.find(';', begin);
                std::string key = config.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
                const size_t equals = key.find('=');
                if (equals != std::string::npos) key.resize(equals);
                const size_t first = key.find_first_not_of(" \t\r\n");
                const size_t last = key.find_last_not_of(" \t\r\n");
                key = first == std::string::npos ? "" : key.substr(first, last - first + 1);
                for (char& ch : key) ch = char(std::tolower(static_cast<unsigned char>(ch)));
                if (key == "d3d9.sampleranisotropy") return true;
                if (end == std::string::npos) break;
                begin = end + 1;
            }
            return false;
        };
        const char* config_file = getenv("DXVK_CONFIG_FILE");
        if (configure_dxvk && !has_aniso_option(merged) && (!config_file || !*config_file)) {
            if (!merged.empty()) merged += ";";
            merged += "d3d9.samplerAnisotropy = " + std::to_string(max_anisotropy);
            setenv("DXVK_CONFIG", merged.c_str(), 1);
            Log("anisotropy: bridge/DXVK maximum %dx", max_anisotropy);
        } else if (configure_dxvk) {
            Log("anisotropy: bridge maximum %dx; explicit DXVK choice preserved", max_anisotropy);
        } else {
            Log("anisotropy: bridge maximum %dx; DXVK option unchanged", max_anisotropy);
        }
    }

    // The game window: DXVK's native WSI takes the SDL window as HWND.
#ifdef __ANDROID__
    // SDL sets the activity's orientation from the window it creates, and for a resizable window it allows portrait
    // too, overriding the manifest: holding the phone upright then turned the game sideways. Landscape both ways only.
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) Fatal("SDL_Init: %s", SDL_GetError());
    g_sdl_window = SDL_CreateWindow("Prototype", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
                                    SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
#ifdef __ANDROID__
                                        | SDL_WINDOW_FULLSCREEN
#endif
    );
    if (!g_sdl_window) Fatal("SDL_CreateWindow: %s", SDL_GetError());
    int window_w, window_h;
    SDL_GetWindowSize(g_sdl_window, &window_w, &window_h);
#ifdef __ANDROID__
    SDL_DisplayMode mode;  // the window becomes the whole screen; its size is not known yet
    if (SDL_GetDesktopDisplayMode(0, &mode) == 0) {
        window_w = mode.w > mode.h ? mode.w : mode.h;  // landscape
        window_h = mode.w > mode.h ? mode.h : mode.w;
    }
#endif
    InitClientSize(window_w, window_h);

    std::string dll = std::string(g_game_dir) + "/prototypeenginef.dll";
    if (!LoadImage(dll.c_str(), &g_engine, kEngineBase)) StartupFailure("Cannot load prototypeenginef.dll.");
    g_engine_lo = g_engine.base;
    g_engine_hi = g_engine.base + g_engine.size;

    uint32_t init = FindExport(g_engine, "EngineInitialize");
    uint32_t service = FindExport(g_engine, "EngineService");
    uint32_t terminate = FindExport(g_engine, "EngineTerminate");
    uint32_t focus = FindExport(g_engine, "EngineWindowFocus");
    if (!init || !service || !terminate || !focus) Fatal("engine exports missing");

    Log("DllMain(PROCESS_ATTACH)");
    uint32_t ok = GuestCall(g_engine.entry, {g_engine.base, 1, 0});
    Log("DllMain returned %u", ok);
    if (!ok) StartupFailure("The engine's DllMain failed.");

    uint32_t hwnd = HostWindow();
    uint32_t params = HeapAlloc(16);
    W32(params + 0, GuestStrdup(""));  // command line
    W32(params + 4, hwnd);
    W32(params + 8, hwnd);
    Log("EngineInitialize");
    uint32_t r = GuestCall(init, {params}) & 0xff;
    Log("EngineInitialize returned %u", r);
    if (!r) StartupFailure("EngineInitialize failed (game files incomplete or not the Steam build 19788432?).");

    InitInput(FindExport(g_engine, "EngineKeyboardService"), FindExport(g_engine, "EngineMouseService"));
    bool focused = true;
    const bool ignore_focus = SettingInt("ignore_focus", 0) != 0;  // tests: an unfocused window keeps full speed
    // PROTODROID_INPUT="<frame>:<SDL key name>,...": taps a key at that frame (held for 5 frames), for scripted tests.
    struct Tap {
        uint64_t frame;
        SDL_Scancode key;
    };
    std::vector<Tap> taps;
    if (const char* spec = getenv("PROTODROID_INPUT")) {
        std::string all(spec);
        for (size_t pos = 0; pos < all.size();) {
            size_t end = all.find(',', pos);
            std::string item = all.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            size_t colon = item.find(':');
            if (colon != std::string::npos) {
                taps.push_back({strtoull(item.c_str(), nullptr, 10), SDL_GetScancodeFromName(item.c_str() + colon + 1)});
            }
            pos = end == std::string::npos ? all.size() : end + 1;
        }
    }
    static std::atomic<uint64_t> frames{0};
    StartWatchdog(&frames);
    int fps_limit = SettingInt("fps_limit", 0);
    if (SettingInt("battery_saver", 0) && (fps_limit == 0 || fps_limit > 30)) fps_limit = 30;
    g_fps_limit.store(fps_limit);
    PerfHintStart(fps_limit);
    using Clock = std::chrono::steady_clock;
    Clock::time_point frame_start = Clock::now(), next_log = frame_start;
    for (uint64_t frame = 0;; ++frame) {
        frames.store(frame, std::memory_order_relaxed);
        if (frame == 900) VulkanDriverConfirm();  // well past the intro movies: the driver survived startup
        // The limiter remains anchored to the engine loop. FPS, frame statistics, and ADPF are anchored only to real
        // D3D9 presents, so thousands of empty message-pump iterations cannot inflate them or skew CPU clocking.
        Clock::time_point now = Clock::now();
        if (int limit = g_fps_limit.load(); limit > 0) {
            Clock::time_point due = frame_start + std::chrono::microseconds(1000000 / limit);
            if (now < due) {
                std::this_thread::sleep_until(due);
                now = Clock::now();
            }
        }
        frame_start = now;
        if (now >= next_log) {
            if (frame) {
                const PresentStatsSnapshot present = GetPresentStats();
                Log("perf: %.1f present fps, avg %.2f ms, p99 %.2f ms, max %.2f ms, %ld MiB resident; arena %s; io: %s",
                    present.fps, present.average_ms, present.p99_ms, present.max_ms, ResidentMiB(),
                    ArenaUsage().c_str(), TakeIoStats().c_str());
                native::MirrorCopyDXVKLogs();
            }
            next_log = now + std::chrono::seconds(30);
        }
        for (const Tap& t : taps) {
            if (t.key == SDL_SCANCODE_UNKNOWN) continue;
            if (frame == t.frame) InjectKey(t.key, true);
            if (frame == t.frame + 5) InjectKey(t.key, false);
        }
        if ((frame & (frame - 1)) == 0) Log("frame %llu", (unsigned long long)frame);
        bool quit = false;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT || e.type == SDL_APP_TERMINATING) {
#ifdef __ANDROID__
                // Android closes the activity and waits for this thread on its UI thread: the engine's shutdown
                // (EngineTerminate waits for its threads) took long enough for "isn't responding". The game saves
                // at its checkpoints, so there is nothing to lose by ending the process here.
                Log("quit");
                VulkanDriverAbandon();
                native::MirrorCopyDXVKLogs();
                _exit(0);
#endif
                quit = true;
            } else if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) focused = true;
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST && !ignore_focus) focused = false;
            } else {
                ForwardInput(e);
            }
        }
        bool keep = (GuestCall(service, {quit ? 1u : 0u, focused ? 0u : 1u}) & 0xff) != 0;
        GuestCall(focus, {focused ? 1u : 0u});
        if (!keep) break;
    }
    GuestCall(terminate, {});
    Log("EngineTerminate done");
    native::MirrorCopyDXVKLogs();
    return 0;
}
