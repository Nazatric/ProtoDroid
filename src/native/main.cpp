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

#include "aa_policy.h"
#include "diagnostic_tour.h"
#include "diagnostics.h"
#include "dxvk_config.h"
#include "guest.h"
#include "guest_time.h"
#include "input.h"
#include "mod_platform.h"
#include "present_stats.h"
#include "prime_affinity.h"
#include "timing_window.h"
#include "save_path.h"
#include "state_cache.h"
#include "win.h"

#ifndef PROTODROID_SOURCE_REVISION
#define PROTODROID_SOURCE_REVISION "unknown"
#endif

namespace native {

const char* g_game_dir = nullptr;
const char* g_data_dir = nullptr;  // Android: the app's external files folder (logs, ini, legacy saves)

namespace {
std::mutex g_log_mutex;
FILE* g_log = nullptr;
FILE* g_log_mirror = nullptr;  // Android 11+: /storage/emulated/0/Prototype/logs/
std::string g_mirror_dir;
std::string g_mirror_log_path;
std::string g_mirror_prev_path;
std::mutex g_mirror_copy_mutex;
std::thread g_mirror_thread;
std::atomic<bool> g_mirror_copy_active{false};
}  // namespace
void MirrorCopyDXVKLogs();
void MirrorCopyDXVKLogsAsync();
void WaitForMirrorCopy();

// Forward for mirroring helper

void Log(const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    const double monotonic_seconds = double(MonotonicNanos()) / 1e9;
    const int tid = int(gettid());
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
        fprintf(g_log, "[mono=%.3f tid=%d] ", monotonic_seconds, tid);
        va_start(ap, fmt);
        vfprintf(g_log, fmt, ap);
        va_end(ap);
        fputc('\n', g_log);
        fflush(g_log);
    }
    if (g_log_mirror) {
        fprintf(g_log_mirror, "[mono=%.3f tid=%d] ", monotonic_seconds, tid);
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
    DiagnosticTourCrashed(sig);
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
    // A tour never falls back to the ordinary shared log directory or the game root.
    const char* tour_candidates[] = {kDiagnosticTourLogDir, nullptr};
    const char* normal_candidates[] = {
        "/storage/emulated/0/Prototype/logs",
        "/storage/emulated/0/Prototype",
        nullptr
    };
    const char** candidates = DiagnosticTourEnabled() ? tour_candidates : normal_candidates;
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
    // A tour's DXVK logger writes directly into its isolated shared directory. Copying the ordinary app-private log
    // would overwrite that live tour log with stale normal-Play content; its isolated cache needs no ordinary backup.
    if (DiagnosticTourEnabled()) return;
    // Periodic work runs on a helper thread; shutdown and crash reports may still request a synchronous copy.
    std::lock_guard<std::mutex> copy_lock(g_mirror_copy_mutex);
    std::string base_log = std::string(g_data_dir) + "/app_process64_d3d9.log";
    std::string mirror_dxvk = g_mirror_dir + "/app_process64_d3d9.log";
    CopyFile(base_log, mirror_dxvk);
    // Do not replace a useful shared backup with an empty or invalid private file. Once DXVK has entries, retain the
    // existing periodic mirror so an external-files wipe does not discard all compiled pipeline state.
    std::string base_cache = std::string(g_data_dir) + "/prototypeenginef.dll.dxvk-cache";
    std::string mirror_cache = g_mirror_dir + "/prototypeenginef.dll.dxvk-cache";
    // Replace the ordinary backup atomically. A process kill or failed write must not truncate the last usable cache,
    // and the live DXVK writer remains in app-private storage instead of doing shader-cache I/O through shared FUSE.
    native::BackupStateCache(base_cache, mirror_cache);
}

void MirrorCopyDXVKLogsAsync() {
    if (g_mirror_dir.empty() || !g_data_dir || DiagnosticTourEnabled() ||
        g_mirror_copy_active.load(std::memory_order_acquire)) return;
    if (g_mirror_thread.joinable()) g_mirror_thread.join();
    bool expected = false;
    if (!g_mirror_copy_active.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return;
    try {
        g_mirror_thread = std::thread([] {
            MirrorCopyDXVKLogs();
            g_mirror_copy_active.store(false, std::memory_order_release);
        });
    } catch (...) {
        g_mirror_copy_active.store(false, std::memory_order_release);
        // Thread creation is only an optimization fallback; never skip the cache backup itself.
        MirrorCopyDXVKLogs();
    }
}

void WaitForMirrorCopy() {
    if (g_mirror_thread.joinable()) g_mirror_thread.join();
    g_mirror_copy_active.store(false, std::memory_order_release);
}

void Fatal(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Log("FATAL: %s", buf);
    DiagnosticTourFail("fatal", buf);
    VulkanDriverAbandon();  // not the driver's fault: the next start keeps trying Turnip
    _exit(70);
}

// A start that cannot go on: tell the user and end the process. Returning from main would let SDL run main again in
// the same process, on top of this run's guest memory.
[[noreturn]] void StartupFailure(const char* what) {
    Log("startup failed: %s", what);
    DiagnosticTourFail("startup", what);
    // End the attempt without blaming ordinary startup errors on the driver. A D3D9 initialization failure may
    // already have recorded its own recovery marker before reaching this common exit path.
    VulkanDriverAbandon();
    DiagnosticsStop();
    WaitForMirrorCopy();
    MirrorCopyDXVKLogs();
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
extern SDL_Window* g_sdl_window;

}  // namespace native


// ---- frame limiter and presented-frame counters (read by the on-screen overlay through JNI) ----
namespace native {
std::atomic<int> g_fps_limit{0};  // 0 = no limit (the swap interval still applies)
std::atomic<bool> g_unlock_menu_fps{true};

bool MenuFpsUnlocked() { return g_unlock_menu_fps.load(std::memory_order_relaxed); }

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
extern "C" JNIEXPORT void JNICALL Java_com_protodroid_TouchControls_nativeSetMenuFpsUnlocked(
    JNIEnv*, jclass, jboolean unlocked) {
    native::g_unlock_menu_fps.store(unlocked != JNI_FALSE, std::memory_order_relaxed);
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
    std::string native_log_path = "proto-native.log";
    std::string native_prev_log_path = "proto-native.prev.log";
#ifdef __ANDROID__
    const bool diagnostic_tour = native::DiagnosticTourEnabled();
    std::string diagnostic_cache_dir;
    if (!diagnostic_tour) {
        // These are native implementation details, not launcher settings. Clear any values left by a previous tour
        // activity in the same process; the user's INI choices are applied again below.
        unsetenv("PROTODROID_PIPELINE_TRACE");
        unsetenv("PROTODROID_PIPELINE_TRACE_DIR");
        unsetenv("PROTODROID_PIPELINE_TIMING");
        unsetenv("PROTODROID_IMPORT_PROFILE");
        unsetenv("DXVK_LOG_TIMESTAMPS");
    }
#else
    const bool diagnostic_tour = false;
#endif
    native::SetGuestClockDiagnosticTour(diagnostic_tour);
#ifdef __ANDROID__
    RedirectStderrToLogcat();
    setenv("DXVK_LOG_LEVEL", "info", 0);
#endif
    g_game_dir = argc > 1 ? argv[1] : getenv("PROTODROID_GAME");
#ifdef __ANDROID__
    // Logs, protodroid.ini and DXVK caches live in the app's external files folder. Saves use the shared Prototype
    // folder when it is writable, with the legacy app-specific native_c directory as a no-permission fallback.
    // The game is <that folder>/game, or wherever SetupActivity found it (PROTODROID_GAME, e.g. /sdcard/Prototype).
    static std::string android_game;
    const char* base = SDL_AndroidGetExternalStoragePath();
    if (!base) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Prototype", SDL_GetError(), nullptr);
        return 1;
    }
    if (chdir(base) != 0) return 1;
    g_data_dir = base;
    if (diagnostic_tour) {
        diagnostic_cache_dir = std::string(base) + "/diagnostic-tour-cache";
        // Set this before the journal records its profiling policy, and before any engine import can be dispatched.
        setenv("PROTODROID_IMPORT_PROFILE", "1", 1);
        if (!native::DiagnosticTourStart(diagnostic_cache_dir.c_str())) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Prototype",
                                     "The diagnostic tour could not create its isolated log/cache directories.",
                                     nullptr);
            return 1;
        }
        // Seed the tour's cache from the shared backup before DXVK opens it. The tour otherwise starts cold:
        // every pipeline compiles live during the tour (slow, and serialized on Mali), and the cache it
        // produces only lists what one scripted scenario reaches. Seeding makes the tour's cache a superset —
        // the backup's entries plus whatever new material the tour exercises — and normal play migrates that
        // superset, so the tour can only add coverage, never lose it.
        {
            std::vector<std::string> tour_cache_sources = {"/storage/emulated/0/Prototype/logs",
                                                            "/storage/emulated/0/Prototype"};
            native::StateCacheMigration tour_seed =
                native::PlanStateCacheMigration(diagnostic_cache_dir, tour_cache_sources);
            if (!tour_seed.source.empty()) {
                if (native::MigrateStateCache(tour_seed) == native::StateCacheMigrationResult::Migrated)
                    Log("tour cache seeded from %s", tour_seed.source.c_str());
                else
                    Log("tour cache seeding from %s failed: %s", tour_seed.source.c_str(), strerror(errno));
            }
        }
        const std::string private_logs = std::string(base) + "/diagnostic-tour-logs";
        if (!native::EnsureDir(private_logs)) {
            native::DiagnosticTourFail("startup", "app-private tour log directory is not writable");
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Prototype",
                                     "The diagnostic tour could not create its private log directory.", nullptr);
            return 1;
        }
        native_log_path = private_logs + "/proto-native.log";
        native_prev_log_path = private_logs + "/proto-native.prev.log";
    }
    // DXVK opens its log lazily, after this setup. Do not point it at a
    // path until the directory has actually been created and checked.
    if (!native::EnsureDir(base)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Prototype",
                                 "The app log directory is not writable.", nullptr);
        return 1;
    }
    // DXVK v2.5.3 reads DXVK_STATE_CACHE_PATH and the Android patch resolves prototypeenginef.dll.dxvk-cache within
    // it. Ordinary Play recovers a populated cache without inventing a seed. The tour starts with its own cache path
    // and performs no ordinary migration or backup.
    const char* cache_dir = diagnostic_tour ? diagnostic_cache_dir.c_str() : base;
    std::string state_cache = native::StateCacheFile(cache_dir, native::kStateCacheFilename);
    if (!diagnostic_tour) {
        // The diagnostic tour builds a populated state cache in its private directory (app files, same GPU and
        // driver as normal play). Seeding ordinary play from it removes the first-minutes pipeline-compile stalls;
        // sources earlier in the list keep priority, and the tour itself never migrates or is modified.
        std::vector<std::string> cache_sources = {"/storage/emulated/0/Prototype/logs",
                                                  "/storage/emulated/0/Prototype"};
        if (!diagnostic_cache_dir.empty() && diagnostic_cache_dir != base)
            cache_sources.push_back(diagnostic_cache_dir);
        native::StateCacheMigration cache_migration = native::PlanStateCacheMigration(base, cache_sources);
        if (!cache_migration.source.empty()) {
            native::StateCacheMigrationResult migrated = native::MigrateStateCache(cache_migration);
            if (migrated == native::StateCacheMigrationResult::Migrated) {
                Log("DXVK state cache restored from %s", cache_migration.source.c_str());
                // The precompile flow (SetupActivity dialog) runs the tour once and relaunches normal play;
                // seeding from the tour cache is that event. Mark it so the dialog is never offered again.
                if (!diagnostic_cache_dir.empty() &&
                    cache_migration.source.rfind(diagnostic_cache_dir, 0) == 0) {
                    FILE* marker = fopen((std::string(base) + "/shader-precompile.done").c_str(), "w");
                    if (marker) { fputs("seeded from the diagnostic tour cache\n", marker); fclose(marker); }
                    Log("shader precompile complete; marker written");
                }
            }
            else
                Log("DXVK state cache migration from %s failed: %s", cache_migration.source.c_str(), strerror(errno));
        }
    }
    setenv("DXVK_STATE_CACHE_PATH", cache_dir, 1);
    setenv("DXVK_LOG_PATH", diagnostic_tour ? kDiagnosticTourLogDir : base, 1);
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
        native::DiagnosticTourFail("startup", "prototypeenginef.dll is not readable");
        std::string msg = "Cannot read " + dll + " (" + strerror(errno) + ").\nCopy the game files to one of:\n"
            "/storage/emulated/0/Prototype\n/storage/emulated/0/Prototype/game\n" + std::string(base) + "/game";
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Prototype", msg.c_str(), nullptr);
        return 1;
    }
    // Log rotation + mirroring to shared storage (Issue 3). Tour mode rotates only its isolated private log.
    rename(native_log_path.c_str(), native_prev_log_path.c_str());
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
    rename(native_log_path.c_str(), native_prev_log_path.c_str());
#endif
    if (!g_log) g_log = fopen(native_log_path.c_str(), "w");
    InstallCrashHandler();
#ifdef __ANDROID__
    if (diagnostic_tour && !g_log) StartupFailure("The diagnostic tour could not open its isolated private log.");
    if (diagnostic_tour && g_mirror_dir.empty())
        StartupFailure("The diagnostic tour requires writable shared logs in /Prototype/diagnostic-tour/logs.");
#endif
    Log("protodroid build %s %s", __DATE__, __TIME__);
    Log("protodroid source %s", PROTODROID_SOURCE_REVISION);
#ifdef __ANDROID__
    if (!g_mirror_dir.empty()) {
        Log("logs mirrored to %s", g_mirror_dir.c_str());
        if (g_log_mirror) {
            // Ensure mirror has header too
            fprintf(g_log_mirror, "protodroid build %s %s\n", __DATE__, __TIME__);
            fprintf(g_log_mirror, "protodroid source %s\n", PROTODROID_SOURCE_REVISION);
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
    if (diagnostic_tour) {
        if (!native::ConfigureIsolatedSaveDirectory(native::kDiagnosticTourSaveDir))
            StartupFailure("The diagnostic tour requires its isolated shared save directory; normal saves were not selected.");
        native::DiagnosticTourSaveReady(native::SaveDirectory().c_str());
    } else {
        native::ConfigureSaveDirectory(base, native::kAndroidSharedSaveDir, native::kAndroidPreviousSharedSaveDir);
    }
    Log("save dir: %s", native::HostPath(native::kGuestSaveDir).c_str());
#endif
    // Detailed diagnostics and the durable pipeline trace remain opt-in. Arming the tour is an explicit opt-in;
    // ordinary Play retains the user's settings.
    const bool detailed_diagnostics = diagnostic_tour ||
                                      SettingInt("detailed_diagnostics", kDetailedDiagnosticsDefault) != 0;
    if (detailed_diagnostics) setenv("DXVK_LOG_TIMESTAMPS", "1", diagnostic_tour ? 1 : 0);
    if (diagnostic_tour) {
        // Import profiling remains opt-in for ordinary Play. The longer tour needs guest/import attribution to
        // correlate accelerated movement, streaming, combat, and any device-specific stall.
        Log("diagnostic tour: guest/import profiling enabled; 4x guest elapsed clocks, real-time host metrics");
    }
    setenv("PROTODROID_PIPELINE_TIMING",
           (diagnostic_tour || SettingInt("pipeline_timing", detailed_diagnostics ? 1 : 0)) ? "1" : "0",
           diagnostic_tour ? 1 : 0);
    if (diagnostic_tour) setenv("PROTODROID_PIPELINE_TRACE_DIR", kDiagnosticTourLogDir, 1);
    if (diagnostic_tour || SettingInt("pipeline_trace", 0) == 1) {
        setenv("PROTODROID_PIPELINE_TRACE", "1", diagnostic_tour ? 1 : 0);
        Log("DXVK pipeline trace enabled (%s)", diagnostic_tour ? "diagnostic tour" : "pipeline_trace = 1");
    }
    // DXVK reads this once when its swapchain blitter is created. Overwrite stale process state in both directions:
    // ordinary Off/MSAA must never inherit a post-process filter from an earlier launch or external environment.
    const bool fxaa = AaUsesFxaa(SettingInt("game_aa_level", kAaGameDefault));
    setenv("PROTODROID_FXAA", fxaa ? "1" : "0", 1);
    if (fxaa) Log("AA policy: FXAA present filter requested; Prototype built-in MSAA disabled");
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
    // Mali depth-path insurance: render D3D9 D24S8 depth with D32F_S8 (upstream useD32forD24).
    present_config += ";d3d9.useD32forD24 = True";
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

    int vram_report_mb = SettingInt("vram_report_mb", 0);
    if (vram_report_mb < 0) Log("mods: negative vram_report_mb ignored");
    if (vram_report_mb > 0) {
        vram_report_mb = vram_report_mb < 128 ? 128 : vram_report_mb > 16384 ? 16384 : vram_report_mb;
        std::string merged = getenv("DXVK_CONFIG") ? getenv("DXVK_CONFIG") : "";
        if (!merged.empty()) merged += ";";
        merged += "d3d9.maxAvailableMemory = " + std::to_string(vram_report_mb);
        setenv("DXVK_CONFIG", merged.c_str(), 1);
        Log("mods: DXVK memory report capped at %d MiB (not a physical VRAM measurement)", vram_report_mb);
    }

    // Apply last so stale user configuration cannot undo the official Prototype workaround. DXVK keys its own
    // profile to prototypef.exe, which never matches this native host process (app_process64 on Android).
    int fps_limit = SettingInt("fps_limit", 0);
    if (SettingInt("battery_saver", 0) && (fps_limit == 0 || fps_limit > 30)) fps_limit = 30;
    std::string dxvk_config = PrototypeDxvkConfig(getenv("DXVK_CONFIG") ? getenv("DXVK_CONFIG") : "");
    dxvk_config = AaDxvkConfig(SettingInt("game_aa_level", kAaGameDefault), dxvk_config);
    // Latency 1 serializes the CPU of frame N+1 behind the GPU of frame N, so a combat frame pays CPU+GPU and
    // misses the 16.6 ms vblank twice over on a 120 Hz panel (measured: combat sits at 30-40 FPS). Two frames in
    // flight overlap them: the frame rate becomes max(CPU, GPU) instead of CPU+GPU, at the cost of ~one frame of
    // extra latency. The 6-image FIFO swapchain already exists; this is the piece that kept it from helping.
    int dxvk_max_frame_latency = SettingInt("dxvk_max_frame_latency", 2);
    if (dxvk_max_frame_latency < 0) dxvk_max_frame_latency = 0;
    if (dxvk_max_frame_latency > 16) dxvk_max_frame_latency = 16;
    dxvk_config = FramePacingDxvkConfig(dxvk_config, dxvk_max_frame_latency, fps_limit);
    dxvk_config = DynamicBuffersDxvkConfig(dxvk_config, SettingInt("dxvk_cached_dynamic_buffers", 1));
    setenv("DXVK_CONFIG", dxvk_config.c_str(), 1);
    if (fxaa) Log("AA policy: DXVK forced swapchain MSAA override pinned to 0");
    Log("game profile: Prototype DF formats disabled for dynamic shadows and effects");
    if (dxvk_max_frame_latency > 0) Log("frame pacing: DXVK maxFrameLatency %d%s", dxvk_max_frame_latency,
                                        fps_limit > 0 ? "" : "; limiter follows the display refresh rate");

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
    DiagnosticsStart();

    std::string dll = std::string(g_game_dir) + "/prototypeenginef.dll";
    const int64_t load_start_ns = MonotonicNanos();
    const bool loaded = LoadImage(dll.c_str(), &g_engine, kEngineBase);
    Log("timing: LoadImage %.3f ms (success=%u)", double(MonotonicNanos() - load_start_ns) / 1e6, loaded ? 1u : 0u);
    if (!loaded) StartupFailure("Cannot load prototypeenginef.dll.");
    g_engine_lo = g_engine.base;
    g_engine_hi = g_engine.base + g_engine.size;
    g_unlock_menu_fps.store(SettingInt("unlock_menu_fps", 1) != 0, std::memory_order_relaxed);
    ModPlatformInitialize();  // optional packages, detours and plugins: before DllMain/EngineInitialize

    uint32_t init = FindExport(g_engine, "EngineInitialize");
    uint32_t service = FindExport(g_engine, "EngineService");
    uint32_t terminate = FindExport(g_engine, "EngineTerminate");
    uint32_t focus = FindExport(g_engine, "EngineWindowFocus");
    if (!init || !service || !terminate || !focus) Fatal("engine exports missing");

    Log("DllMain(PROCESS_ATTACH)");
    const int64_t dll_main_start_ns = MonotonicNanos();
    uint32_t ok = GuestCall(g_engine.entry, {g_engine.base, 1, 0});
    Log("DllMain returned %u after %.3f ms", ok, double(MonotonicNanos() - dll_main_start_ns) / 1e6);
    if (!ok) StartupFailure("The engine's DllMain failed.");

    uint32_t hwnd = HostWindow();
    uint32_t params = HeapAlloc(16);
    const char* engine_command_line = diagnostic_tour ? kDiagnosticTourCommandLine : "";
    Log("engine command line: %s", *engine_command_line ? engine_command_line : "(empty)");
    W32(params + 0, GuestStrdup(engine_command_line));
    W32(params + 4, hwnd);
    W32(params + 8, hwnd);
    Log("EngineInitialize");
    const int64_t initialize_start_ns = MonotonicNanos();
    uint32_t r = GuestCall(init, {params}) & 0xff;
    Log("EngineInitialize returned %u after %.3f ms", r, double(MonotonicNanos() - initialize_start_ns) / 1e6);
    if (!r) StartupFailure("EngineInitialize failed (game files incomplete or not the Steam build 19788432?).");
    DiagnosticTourEngineInitialized();

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
    // fps_limit (with battery_saver policy) was resolved before the DXVK_CONFIG environment was assembled, so
    // DXVK's maxFrameRate matches the host limiter exactly.
    g_fps_limit.store(fps_limit);
    ApplyGuestThreadAffinity(SettingInt("affinity_mode", 1));  // opt-in; see prime_affinity.h
    // ADPF must chase the panel the same way the limiter does: with fps_limit=0 the swap interval governs, so a
    // 120 Hz display needs an 8.33 ms hint instead of the 60 Hz fallback baked into PerfHintStart.
    int adpf_target = fps_limit;
    if (adpf_target <= 0 && g_sdl_window) {
        SDL_DisplayMode mode{};
        int display = SDL_GetWindowDisplayIndex(g_sdl_window);
        if (display >= 0 && SDL_GetCurrentDisplayMode(display, &mode) == 0 && mode.refresh_rate > 1)
            adpf_target = mode.refresh_rate;
    }
    PerfHintStart(adpf_target);
    // ---- adaptive display pacing ----
    // With FIFO vsync and no limit, a frame that misses a vblank waits for the next one: on a 120 Hz panel an
    // engine hovering between 8.3 and 16.7 ms lands on 2 or 3 vblanks and the visible rate wobbles between
    // 120/60/40 (the reported "60+ but drops to 45, inconsistent"). When the engine's median frame cannot hold
    // the full refresh rate but comfortably fits half of it, pacing frame starts to every other vblank makes
    // every frame hit the same cadence: a flat 60 instead of 60/45 oscillation. The mode re-evaluates with
    // hysteresis; the tour opts out (its 4x guest clock deliberately stresses the engine) and an explicit
    // fps_limit disables this entirely — the user's own limit always wins.
    class AdaptivePacer {
    public:
        void Configure(int refresh_hz, bool enabled) {
            if (refresh_hz > 1 && refresh_hz < 1000) {
                hz_ = refresh_hz;
                refresh_ns_ = 1000000000LL / refresh_hz;
            }
            enabled_ = enabled && refresh_ns_ > 0;
        }
        void ObserveService(int64_t service_ns) {
            if (!enabled_ || service_ns <= 0) return;
            window_[count_ % kWindow] = service_ns;
            if (count_ < kWindow) ++count_;
            else count_ = 1;  // wrap while keeping the window full
        }
        // Returns nanoseconds to sleep before this frame (0 = unpaced); *adpf_hz receives the
        // new performance-hint target on a mode change, or 0 when nothing changed.
        int64_t PacePeriod(std::chrono::steady_clock::time_point now,
                           std::chrono::steady_clock::time_point last_frame_start, int64_t* adpf_hz) {
            if (!enabled_ || refresh_ns_ <= 0) return 0;
            if (now < next_eval_) return period_ns_ > 0 ? SleepTarget(last_frame_start, now) : 0;
            next_eval_ = now + std::chrono::milliseconds(1500);
            if (count_ < kWindow) return 0;  // not enough samples yet
            int64_t sorted[kWindow];
            std::copy(window_, window_ + kWindow, sorted);
            std::sort(sorted, sorted + kWindow);
            const int64_t p50 = sorted[kWindow / 2];
            const int64_t full = refresh_ns_;
            const int64_t half = refresh_ns_ * 2;
            int want = 0;  // 0 = full/unpaced, 2 = half rate
            if (p50 > full * 58 / 100 && p50 <= half * 92 / 100) want = 2;
            if (want == pending_mode_) {
                if (want != mode_ && --pending_confirm_ <= 0) {
                    mode_ = want;
                    Log("frame pacing: engine p50 %.2f ms; pacing to %s", double(p50) / 1e6,
                        mode_ == 2 ? "every other vblank" : "the display rate");
                    if (adpf_hz) *adpf_hz = mode_ == 2 ? hz_ / 2 : hz_;
                }
            } else {
                pending_mode_ = want;
                pending_confirm_ = 2;  // two consecutive evaluations before switching
            }
            period_ns_ = mode_ == 2 ? half : 0;
            return period_ns_ > 0 ? SleepTarget(last_frame_start, now) : 0;
        }

    private:
        int64_t SleepTarget(std::chrono::steady_clock::time_point last_frame_start,
                            std::chrono::steady_clock::time_point now) const {
            constexpr int64_t kWakeMarginNs = 700000;  // scheduler wake-up slack before the vblank
            const auto due = last_frame_start + std::chrono::nanoseconds(period_ns_ - kWakeMarginNs);
            if (now >= due) return 0;
            return std::chrono::duration_cast<std::chrono::nanoseconds>(due - now).count();
        }
        enum : int { kWindow = 24 };
        int64_t window_[kWindow] = {};
        int count_ = 0;
        int hz_ = 0;
        int64_t refresh_ns_ = 0;
        int64_t period_ns_ = 0;
        std::chrono::steady_clock::time_point next_eval_{};
        int mode_ = 0;
        int pending_mode_ = -1;
        int pending_confirm_ = 2;
        bool enabled_ = false;
    };
    AdaptivePacer pacer;
    pacer.Configure(adpf_target, !diagnostic_tour && fps_limit <= 0);
    using Clock = std::chrono::steady_clock;
    Clock::time_point frame_start = Clock::now();
    for (uint64_t frame = 0;; ++frame) {
        frames.store(frame, std::memory_order_relaxed);
        if (frame == 900) VulkanDriverConfirm();  // well past the intro movies: the driver survived startup
        // The limiter remains anchored to the engine loop. FPS, frame statistics, and ADPF are anchored only to real
        // D3D9 presents, so thousands of empty message-pump iterations cannot inflate them or skew CPU clocking.
        Clock::time_point now = Clock::now();
        int64_t limiter_ns = -1;
        if (int limit = g_fps_limit.load(); limit > 0) {
            const Clock::time_point limiter_start = now;
            Clock::time_point due = frame_start + std::chrono::microseconds(1000000 / limit);
            if (now < due) {
                std::this_thread::sleep_until(due);
                now = Clock::now();
            }
            limiter_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now - limiter_start).count();
        } else {
            // Adaptive display pacing: sleep until this frame's slot when half-rate mode is engaged.
            const Clock::time_point limiter_start = now;
            int64_t adpf_hz = 0;
            const int64_t pace_ns = pacer.PacePeriod(now, frame_start, &adpf_hz);
            if (adpf_hz > 0) PerfHintTarget(int(adpf_hz));
            if (pace_ns > 0) {
                std::this_thread::sleep_for(std::chrono::nanoseconds(pace_ns));
                now = Clock::now();
            }
            limiter_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now - limiter_start).count();
        }
        frame_start = now;
        for (const Tap& t : taps) {
            if (t.key == SDL_SCANCODE_UNKNOWN) continue;
            if (frame == t.frame) InjectKey(t.key, true);
            if (frame == t.frame + 5) InjectKey(t.key, false);
        }
        if ((frame & (frame - 1)) == 0) Log("frame %llu", (unsigned long long)frame);
        bool quit = false;
        const bool timing = DetailedDiagnosticsEnabled();
        const int64_t pump_start_ns = timing ? MonotonicNanos() : 0;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT || e.type == SDL_APP_TERMINATING) {
                DiagnosticTourAbort("user or Android requested quit");
#ifdef __ANDROID__
                // Android closes the activity and waits for this thread on its UI thread: the engine's shutdown
                // (EngineTerminate waits for its threads) took long enough for "isn't responding". The game saves
                // at its checkpoints, so there is nothing to lose by ending the process here.
                Log("quit");
                VulkanDriverAbandon();
                native::DiagnosticsStop();
                native::WaitForMirrorCopy();
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
        const int64_t service_start_ns = MonotonicNanos();
        bool keep = (GuestCall(service, {quit ? 1u : 0u, focused ? 0u : 1u}) & 0xff) != 0;
        const int64_t service_end_ns = MonotonicNanos();
        pacer.ObserveService(service_end_ns - service_start_ns);
        GuestCall(focus, {focused ? 1u : 0u});
        if (timing) {
            const int64_t focus_end_ns = MonotonicNanos();
            RecordEngineLoop(service_end_ns, service_end_ns - service_start_ns, service_start_ns - pump_start_ns,
                             limiter_ns, focus_end_ns - service_end_ns);
        }
        ModPlatformFrame();
        if (!keep) {
            // This is a safe main-loop boundary, unlike a fatal callback from inside the guest. Do not leave tour keys
            // held while EngineTerminate runs or if Android later reuses this process.
            if (diagnostic_tour) ReleaseInjectedInput();
            DiagnosticTourFail("engine_loop", "engine stopped before the diagnostic tour completed");
        }
        if (!DiagnosticTourPump()) keep = false;
        if (!keep) break;
    }
    if (diagnostic_tour) {
        // The supplied G720 tour log showed EngineTerminate waiting indefinitely after an entry-gate failure while
        // Android input and the last rendered scene stayed visible. A tour already owns an isolated, process-ending
        // run: finish it at this safe engine-loop boundary instead of entering that shutdown path again.
        ReleaseInjectedInput();
        if (DiagnosticTourFailed())
            StartupFailure("Diagnostic tour failed. See /Prototype/diagnostic-tour/logs/journal.log and result.txt.");
        Log("diagnostic tour finished; ending the isolated process without EngineTerminate");
        VulkanDriverAbandon();  // An intentional tour exit is not evidence that the selected driver failed.
        native::DiagnosticsStop();
        native::WaitForMirrorCopy();
        native::MirrorCopyDXVKLogs();
        _exit(0);
    }

    // Ordinary Play retains the engine's normal termination path.
    ModPlatformShutdown();
    const int64_t terminate_start_ns = MonotonicNanos();
    GuestCall(terminate, {});
    Log("EngineTerminate done after %.3f ms", double(MonotonicNanos() - terminate_start_ns) / 1e6);
    native::DiagnosticsStop();
    native::WaitForMirrorCopy();
    native::MirrorCopyDXVKLogs();
    return 0;
}
