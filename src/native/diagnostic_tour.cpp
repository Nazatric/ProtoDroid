#include "diagnostic_tour.h"

#include <SDL2/SDL.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <time.h>

#include "diagnostic_tour_plan.h"
#include "input.h"
#include "present_stats.h"
#include "timing_window.h"

#ifndef PROTODROID_SOURCE_REVISION
#define PROTODROID_SOURCE_REVISION "unknown"
#endif

// Read by RecompDispatch. It is zero for ordinary Play, so gameplay-target classification is cold and no environment
// lookup or logging occurs on the computed-call hot path.
extern "C" {
volatile uint32_t RecompDiagnosticTour = 0;
}

namespace native {

void Log(const char* fmt, ...);

namespace {

std::atomic<bool> g_started{false};
std::atomic<bool> g_finished{false};
std::atomic<bool> g_gameplay_observed{false};
std::atomic<uint32_t> g_gameplay_target{0};
std::atomic<uint32_t> g_gameplay_dispatches{0};
std::atomic<uint64_t> g_actions_executed{0};
std::atomic<int> g_result{0};  // 0 running, 1 complete, 2 failed, 3 aborted
std::atomic<const char*> g_phase_name{"inactive"};

bool g_engine_ready = false;
bool g_save_ready = false;
bool g_running = false;
bool g_gameplay_logged = false;
bool g_phase_active = false;
int g_journal_fd = -1;
char g_journal_path[512] = {};
char g_phase_path[512] = {};
char g_result_path[512] = {};
char g_crash_path[512] = {};
TourPhaseCursor g_cursor;
size_t g_phase_index = 0;
uint64_t g_present_cursor = 0;
uint64_t g_phase_present_start = 0;
uint64_t g_phase_action_start = 0;
uint64_t g_action_frames = 0;
uint32_t g_phase_gameplay_start = 0;
int64_t g_phase_started_ns = 0;
int64_t g_phase_cpu_start_ns = -1;
int64_t g_tour_started_ns = 0;
int64_t g_tour_cpu_start_ns = -1;
uint64_t g_tour_present_start = 0;
int64_t g_engine_ready_ns = 0;
int64_t g_last_progress_ns = 0;
int64_t g_tour_deadline_ns = 0;

constexpr int64_t kGameplayTimeoutNs = 120ll * 1000 * 1000 * 1000;
constexpr int64_t kPresentStallTimeoutNs = 30ll * 1000 * 1000 * 1000;
constexpr int64_t kTourTimeoutNs = 10ll * 60 * 1000 * 1000 * 1000;

int64_t ProcessCpuNanos() {
    timespec time {};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time) != 0) return -1;
    return int64_t(time.tv_sec) * 1000000000ll + time.tv_nsec;
}

int64_t ElapsedNanos(int64_t start, int64_t now) {
    return start > 0 && now >= start ? now - start : -1;
}

double CpuPercent(int64_t cpu_ns, int64_t wall_ns) {
    return cpu_ns >= 0 && wall_ns > 0 ? double(cpu_ns) * 100.0 / double(wall_ns) : -1.0;
}

double Millis(int64_t nanos) { return nanos >= 0 ? double(nanos) / 1e6 : -1.0; }
uint64_t CounterDelta(uint64_t value, uint64_t start) { return value >= start ? value - start : 0; }

bool WriteAll(int fd, const char* data, size_t size) {
    while (size) {
        ssize_t written = write(fd, data, size);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        data += written;
        size -= size_t(written);
    }
    return true;
}

bool EnsureDirectories(const char* path) {
    std::string current(path ? path : "");
    if (current.empty()) return false;
    for (size_t i = 1; i < current.size(); ++i) {
        if (current[i] != '/') continue;
        current[i] = 0;
        if (mkdir(current.c_str(), 0755) != 0 && errno != EEXIST) return false;
        current[i] = '/';
    }
    if (mkdir(current.c_str(), 0755) != 0 && errno != EEXIST) return false;
    struct stat st {};
    return stat(current.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

void SyncLogDirectory() {
    int fd = open(kDiagnosticTourLogDir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return;
    fsync(fd);  // Some Android external-storage implementations reject directory fsync; rename remains atomic.
    close(fd);
}

bool WriteMarker(const char* path, const char* text, bool sync_directory = true) {
    if (!path || !*path) return false;
    std::string temporary = std::string(path) + ".tmp";
    int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return false;
    bool ok = WriteAll(fd, text, strlen(text));
    if (ok && fsync(fd) != 0) ok = false;
    if (close(fd) != 0) ok = false;
    if (ok && rename(temporary.c_str(), path) != 0) ok = false;
    if (!ok) unlink(temporary.c_str());
    if (sync_directory) SyncLogDirectory();
    return ok;
}

void Record(const char* fmt, bool durable, ...) {
    if (g_journal_fd < 0) return;
    char line[2048];
    const double seconds = double(MonotonicNanos()) / 1e9;
    int length = snprintf(line, sizeof(line), "[mono=%.3f present=%llu] ", seconds,
                          (unsigned long long)PresentedFrameCount());
    if (length < 0 || size_t(length) >= sizeof(line)) return;
    va_list ap;
    va_start(ap, durable);
    int body = vsnprintf(line + length, sizeof(line) - size_t(length), fmt, ap);
    va_end(ap);
    if (body < 0) return;
    size_t used = size_t(length) + std::min(size_t(body), sizeof(line) - size_t(length) - 1);
    if (used + 1 >= sizeof(line)) used = sizeof(line) - 2;
    line[used++] = '\n';
    if (WriteAll(g_journal_fd, line, used) && durable) fsync(g_journal_fd);
}

const char* ResultName(int result) {
    switch (result) {
        case 1: return "complete";
        case 2: return "failed";
        case 3: return "aborted";
        default: return "running";
    }
}

const char* InputName(TourInput input) {
    switch (input) {
        case TourInput::W: return "W";
        case TourInput::A: return "A";
        case TourInput::S: return "S";
        case TourInput::D: return "D";
        case TourInput::LeftShift: return "LeftShift";
        case TourInput::Space: return "Space";
        case TourInput::Tab: return "Tab";
        case TourInput::R: return "R";
        case TourInput::F1: return "F1";
        case TourInput::F2: return "F2";
        case TourInput::Num1: return "1";
        case TourInput::Num2: return "2";
        case TourInput::Num3: return "3";
        case TourInput::Num4: return "4";
        case TourInput::Q: return "Q";
        case TourInput::E: return "E";
        case TourInput::F: return "F";
        case TourInput::LeftMouse: return "LeftMouse";
        case TourInput::RightMouse: return "RightMouse";
        default: return "None";
    }
}

SDL_Scancode Scancode(TourInput input) {
    switch (input) {
        case TourInput::W: return SDL_SCANCODE_W;
        case TourInput::A: return SDL_SCANCODE_A;
        case TourInput::S: return SDL_SCANCODE_S;
        case TourInput::D: return SDL_SCANCODE_D;
        case TourInput::LeftShift: return SDL_SCANCODE_LSHIFT;
        case TourInput::Space: return SDL_SCANCODE_SPACE;
        case TourInput::Tab: return SDL_SCANCODE_TAB;
        case TourInput::R: return SDL_SCANCODE_R;
        case TourInput::F1: return SDL_SCANCODE_F1;
        case TourInput::F2: return SDL_SCANCODE_F2;
        case TourInput::Num1: return SDL_SCANCODE_1;
        case TourInput::Num2: return SDL_SCANCODE_2;
        case TourInput::Num3: return SDL_SCANCODE_3;
        case TourInput::Num4: return SDL_SCANCODE_4;
        case TourInput::Q: return SDL_SCANCODE_Q;
        case TourInput::E: return SDL_SCANCODE_E;
        case TourInput::F: return SDL_SCANCODE_F;
        default: return SDL_SCANCODE_UNKNOWN;
    }
}

void Execute(const TourAction& action) {
    const char* phase = g_phase_name.load(std::memory_order_relaxed);
    if (action.type == TourActionType::MouseMove) {
        InjectMouseMotion(action.x, action.y);
        Record("action phase=%s frame=%u type=mouse_move x=%d y=%d", false, phase, g_cursor.frame(), action.x,
               action.y);
        g_actions_executed.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const bool down = action.type == TourActionType::KeyDown;
    if (action.input == TourInput::LeftMouse || action.input == TourInput::RightMouse) {
        InjectMouseButton(action.input == TourInput::LeftMouse ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT, down);
    } else {
        InjectKey(Scancode(action.input), down);
    }
    Record("action phase=%s frame=%u input=%s down=%u", false, phase, g_cursor.frame(), InputName(action.input),
           down ? 1u : 0u);
    g_actions_executed.fetch_add(1, std::memory_order_relaxed);
}

void RunDueActions() {
    TourAction action{};
    while (g_cursor.Next(action)) Execute(action);
}

void Finish(int result, const char* reason) {
    if (g_finished.exchange(true, std::memory_order_acq_rel)) return;
    g_result.store(result, std::memory_order_release);
    RecompDiagnosticTour = 0;
    const char* name = ResultName(result);
    const uint32_t gameplay_dispatches = g_gameplay_dispatches.load(std::memory_order_acquire);
    const uint64_t actions = g_actions_executed.load(std::memory_order_acquire);
    const uint64_t presented = PresentedFrameCount();
    const uint64_t tour_presented = g_tour_started_ns > 0 ? CounterDelta(presented, g_tour_present_start) : 0;
    const int64_t now = MonotonicNanos();
    const int64_t wall_ns = ElapsedNanos(g_tour_started_ns, now);
    const int64_t cpu_end_ns = ProcessCpuNanos();
    const int64_t cpu_ns = g_tour_cpu_start_ns >= 0 && cpu_end_ns >= g_tour_cpu_start_ns
                               ? cpu_end_ns - g_tour_cpu_start_ns : -1;
    const double cpu_percent = CpuPercent(cpu_ns, wall_ns);
    Record("result=%s reason=%s gameplay_dispatches=%u wall_ms=%.3f process_cpu_ms=%.3f "
           "process_cpu_utilization_pct=%.2f actions_executed=%llu action_frames=%llu presented_frames=%llu "
           "visual_verification=not-performed physical_device_validation=not-performed",
           true, name, reason ? reason : "none", gameplay_dispatches, Millis(wall_ns),
           Millis(cpu_ns), cpu_percent, (unsigned long long)actions, (unsigned long long)g_action_frames,
           (unsigned long long)tour_presented);
    char marker[2048];
    snprintf(marker, sizeof(marker),
             "result=%s\nreason=%s\nphase=%s\npresent=%llu\ngameplay_dispatches=%u\n"
             "wall_ms=%.3f\nprocess_cpu_ms=%.3f\nprocess_cpu_utilization_pct=%.2f\n"
             "actions_executed=%llu\naction_frames=%llu\npresented_frames=%llu\n"
             "exit_behavior=intentional-isolated-process-close\n"
             "visual_verification=not-performed\nscript_completion_is_not_visual_correctness=true\n",
             name, reason ? reason : "none", g_phase_name.load(std::memory_order_relaxed),
             (unsigned long long)presented, gameplay_dispatches, Millis(wall_ns), Millis(cpu_ns),
             cpu_percent, (unsigned long long)actions, (unsigned long long)g_action_frames,
             (unsigned long long)tour_presented);
    if (!WriteMarker(g_result_path, marker, false))
        Log("diagnostic tour: could not write durable result marker %s: %s", g_result_path, strerror(errno));
    snprintf(marker, sizeof(marker), "phase=%s\nstatus=%s\nreason=%s\npresent=%llu\ngameplay_dispatches=%u\n"
             "actions_executed=%llu\nwall_ms=%.3f\n",
             g_phase_name.load(std::memory_order_relaxed), name, reason ? reason : "none",
             (unsigned long long)presented, gameplay_dispatches, (unsigned long long)actions, Millis(wall_ns));
    // One directory sync covers both same-directory renames; each file was already synced before its atomic rename.
    if (!WriteMarker(g_phase_path, marker))
        Log("diagnostic tour: could not write durable final phase marker %s: %s", g_phase_path, strerror(errno));
    Log("diagnostic tour %s: %s; scripted completion is not visual verification", name, reason ? reason : "none");
}

void BeginPhase(size_t index) {
    while (index < kTourPhaseCount) {
        g_phase_index = index;
        g_cursor.Begin(index);
        g_phase_active = false;
        const TourPhase& phase = kTourPhases[index];
        g_phase_name.store(phase.name, std::memory_order_release);
        char marker[1024];
        snprintf(marker, sizeof(marker), "phase=%s\nindex=%zu\nstatus=requested\npresent=%llu\ncoverage=%s\n",
                 phase.name, index, (unsigned long long)PresentedFrameCount(), phase.coverage);
        if (!WriteMarker(g_phase_path, marker)) {
            Log("diagnostic tour: could not write durable phase marker %s: %s", g_phase_path, strerror(errno));
            Record("phase=%s status=failed reason=durable phase marker write failed", true, phase.name);
            Finish(2, "durable phase marker write failed");
            return;
        }
        if (phase.skip_reason) {
            Log("tour phase skipped: %s: %s", phase.name, phase.skip_reason);
            Record("phase=%s index=%zu status=skipped reason=%s", true, phase.name, index, phase.skip_reason);
            ++index;
            continue;
        }
        g_phase_present_start = PresentedFrameCount();
        g_phase_action_start = g_actions_executed.load(std::memory_order_acquire);
        g_phase_started_ns = MonotonicNanos();
        g_phase_cpu_start_ns = ProcessCpuNanos();
        g_phase_gameplay_start = g_gameplay_dispatches.load(std::memory_order_acquire);
        g_phase_active = true;
        Log("tour phase begin: %s (%zu/%zu), at least %u presented frames; one action frame per engine update; coverage=%s",
            phase.name, index + 1, kTourPhaseCount, phase.frames, phase.coverage);
        Record("phase=%s index=%zu status=running minimum_present_frames=%u "
               "action_frame_policy=one-per-engine-update-with-new-present",
               false, phase.name, index, phase.frames);
        RunDueActions();
        return;
    }
    g_phase_name.store("complete", std::memory_order_release);
    Finish(1, "all requested phases reached a recorded tested or skipped outcome");
}

void CompletePhase() {
    const TourPhase& phase = kTourPhases[g_phase_index];
    ReleaseInjectedInput();
    const uint32_t dispatches =
        g_gameplay_dispatches.load(std::memory_order_acquire) - g_phase_gameplay_start;
    if (!dispatches) {
        // Presents alone can continue while the game logic is paused or stuck. Never label such a phase as tested.
        DiagnosticTourFail(phase.name, "no InGameState task dispatch during the presented phase");
        return;
    }
    const uint64_t presented_frames = PresentedFrameCount() - g_phase_present_start;
    const uint64_t actions = g_actions_executed.load(std::memory_order_acquire) - g_phase_action_start;
    if (actions != phase.action_count) {
        DiagnosticTourFail(phase.name, "executed-action count does not match the completed phase plan");
        return;
    }
    const int64_t now = MonotonicNanos();
    const int64_t wall_ns = ElapsedNanos(g_phase_started_ns, now);
    const int64_t cpu_end_ns = ProcessCpuNanos();
    const int64_t cpu_ns = g_phase_cpu_start_ns >= 0 && cpu_end_ns >= g_phase_cpu_start_ns
                               ? cpu_end_ns - g_phase_cpu_start_ns : -1;
    const PresentStatsSnapshot present_stats = GetPresentStats();
    Log("tour phase tested (inputs, presents, gameplay task activity): %s; dispatches=%u; presented=%llu; "
        "wall_ms=%.3f", phase.name, dispatches, (unsigned long long)presented_frames, Millis(wall_ns));
    Record("phase=%s index=%zu status=tested meaning=requested-inputs-rendered-dwell-and-gameplay-task-activity-completed "
           "gameplay_dispatches=%u presented_frames=%llu action_frames=%u actions_executed=%llu "
           "wall_ms=%.3f process_cpu_ms=%.3f process_cpu_utilization_pct=%.2f "
           "present_window=last-completed-rolling-window present_window_frames=%llu present_window_fps=%.3f "
           "present_window_average_ms=%.3f present_window_p99_ms=%.3f present_window_max_ms=%.3f "
           "visual_verification=not-performed",
           true, phase.name, g_phase_index, dispatches, (unsigned long long)presented_frames, g_cursor.frame(),
           (unsigned long long)actions, Millis(wall_ns), Millis(cpu_ns), CpuPercent(cpu_ns, wall_ns),
           (unsigned long long)present_stats.frames, present_stats.fps, present_stats.average_ms,
           present_stats.p99_ms, present_stats.max_ms);
    BeginPhase(g_phase_index + 1);
}

void FailFromPump(const char* reason) {
    // This runs at the main-loop boundary, not from inside a fatal guest callback.
    ReleaseInjectedInput();
    DiagnosticTourFail(g_phase_name.load(std::memory_order_relaxed), reason);
}

}  // namespace

bool DiagnosticTourEnabled() {
    const char* value = getenv(kDiagnosticTourEnv);
    return value && !strcmp(value, "1");
}

bool DiagnosticTourStart(const char* isolated_cache_dir) {
    if (!DiagnosticTourEnabled()) return true;
    if (g_started.load(std::memory_order_acquire)) {
        // Controller/journal state is deliberately single-run. Never reuse a completed result for a later SDL activity.
        Log("diagnostic tour cannot be armed twice in one native process");
        return false;
    }
    if (!EnsureDirectories(kDiagnosticTourLogDir) || !isolated_cache_dir || !*isolated_cache_dir ||
        !EnsureDirectories(isolated_cache_dir)) {
        Log("diagnostic tour cannot create isolated log/cache directories");
        return false;
    }
    snprintf(g_journal_path, sizeof(g_journal_path), "%s/journal.log", kDiagnosticTourLogDir);
    snprintf(g_phase_path, sizeof(g_phase_path), "%s/phase.txt", kDiagnosticTourLogDir);
    snprintf(g_result_path, sizeof(g_result_path), "%s/result.txt", kDiagnosticTourLogDir);
    snprintf(g_crash_path, sizeof(g_crash_path), "%s/crash.log", kDiagnosticTourLogDir);
    char previous[600];
    snprintf(previous, sizeof(previous), "%s/journal.prev.log", kDiagnosticTourLogDir);
    rename(g_journal_path, previous);
    snprintf(previous, sizeof(previous), "%s/crash.prev.log", kDiagnosticTourLogDir);
    rename(g_crash_path, previous);
    snprintf(previous, sizeof(previous), "%s/result.prev.txt", kDiagnosticTourLogDir);
    rename(g_result_path, previous);

    g_journal_fd = open(g_journal_path, O_WRONLY | O_CREAT | O_EXCL | O_APPEND | O_CLOEXEC, 0644);
    if (g_journal_fd < 0) {
        Log("diagnostic tour journal creation failed: %s", strerror(errno));
        return false;
    }
    int crash_fd = open(g_crash_path, O_WRONLY | O_CREAT | O_EXCL | O_APPEND | O_CLOEXEC, 0644);
    bool crash_ok = crash_fd >= 0;
    if (crash_fd >= 0) {
        const char text[] = "status=armed no crash has been recorded in this run\n";
        crash_ok = WriteAll(crash_fd, text, sizeof(text) - 1) && fsync(crash_fd) == 0;
        if (close(crash_fd) != 0) crash_ok = false;
    }
    if (!crash_ok) {
        Log("diagnostic tour crash-marker creation failed: %s", strerror(errno));
        close(g_journal_fd);
        g_journal_fd = -1;
        return false;
    }
    g_started.store(true, std::memory_order_release);
    RecompDiagnosticTour = 1;
    g_phase_name.store("startup", std::memory_order_release);
    Record("run status=requested source=%s command_line=%s logs=%s saves=%s cache=%s", true, PROTODROID_SOURCE_REVISION,
           kDiagnosticTourCommandLine, kDiagnosticTourLogDir, kDiagnosticTourSaveDir, isolated_cache_dir);
    Record("policy normal_play_unchanged=true normal_save_migration=false normal_cache_migration=false "
           "visual_verification=not-performed",
           true);
    for (size_t i = 0; i < kTourPhaseCount; ++i)
        Record("phase=%s index=%zu status=requested coverage=%s", false, kTourPhases[i].name, i,
               kTourPhases[i].coverage);
    if (!WriteMarker(g_phase_path, "phase=startup\nstatus=requested\nengine=not-initialized\n")) {
        Log("diagnostic tour initial phase-marker creation failed: %s", strerror(errno));
        close(g_journal_fd);
        g_journal_fd = -1;
        g_started.store(false, std::memory_order_release);
        RecompDiagnosticTour = 0;
        return false;
    }
    Log("diagnostic tour armed: %s; isolated saves=%s cache=%s", kDiagnosticTourCommandLine, kDiagnosticTourSaveDir,
        isolated_cache_dir);
    return true;
}

void DiagnosticTourSaveReady(const char* path) {
    if (!g_started.load(std::memory_order_acquire) || g_finished.load(std::memory_order_acquire)) return;
    if (!path || strcmp(path, kDiagnosticTourSaveDir)) {
        DiagnosticTourFail("startup", "save path is not the required isolated directory");
        return;
    }
    g_save_ready = true;
    Record("save_isolation status=tested path=%s normal_save_migration=false write_probe=passed "
           "game_save_write=not-requested",
           true, path);
    Log("diagnostic tour save isolation verified: %s; no user saves migrated or selected", path);
}

void DiagnosticTourEngineInitialized() {
    if (!g_started.load(std::memory_order_acquire) || g_finished.load(std::memory_order_acquire)) return;
    g_engine_ready = true;
    g_engine_ready_ns = MonotonicNanos();
    g_phase_name.store("gameplay_wait", std::memory_order_release);
    if (!WriteMarker(g_phase_path, "phase=gameplay_wait\nstatus=running\nwait=InGameState-dispatch\n")) {
        DiagnosticTourFail("startup", "could not persist the gameplay-wait marker");
        return;
    }
    Record("engine status=tested EngineInitialize=returned-success gameplay_wait=InGameState-dispatch", true);
}

void DiagnosticTourGameplayObserved(uint32_t target) {
    g_gameplay_target.store(target, std::memory_order_release);
    g_gameplay_dispatches.fetch_add(1, std::memory_order_relaxed);
    g_gameplay_observed.store(true, std::memory_order_release);
}

bool DiagnosticTourPump() {
    if (!g_started.load(std::memory_order_acquire)) return true;
    if (g_finished.load(std::memory_order_acquire)) return false;
    if (!g_engine_ready) return true;
    const int64_t now = MonotonicNanos();
    if (!g_save_ready) {
        DiagnosticTourFail("startup", "engine initialized without verified isolated saves");
        return false;
    }
    if (!g_gameplay_observed.load(std::memory_order_acquire)) {
        if (now - g_engine_ready_ns > kGameplayTimeoutNs)
            FailFromPump("InGameState dispatch was not observed within 120 seconds");
        return !g_finished.load(std::memory_order_acquire);
    }
    if (!g_gameplay_logged) {
        g_gameplay_logged = true;
        const uint32_t target = g_gameplay_target.load(std::memory_order_acquire);
        Log("diagnostic tour gameplay signal: InGameState dispatch target=%08x", target);
        Record("gameplay status=observed InGameState_target=%08x note=dispatch-is-not-visual-verification", true, target);
    }
    if (!g_running) {
        g_running = true;
        g_present_cursor = PresentedFrameCount();
        g_last_progress_ns = now;
        g_tour_deadline_ns = now + kTourTimeoutNs;
        g_tour_started_ns = now;
        g_tour_cpu_start_ns = ProcessCpuNanos();
        g_tour_present_start = g_present_cursor;
        Record("tour status=running start_present=%llu", true, (unsigned long long)g_present_cursor);
        BeginPhase(0);
        return !g_finished.load(std::memory_order_acquire);
    }
    if (now > g_tour_deadline_ns) {
        FailFromPump("ten-minute presented-frame tour deadline expired");
        return false;
    }
    const uint64_t presented = PresentedFrameCount();
    if (presented == g_present_cursor) {
        if (now - g_last_progress_ns > kPresentStallTimeoutNs)
            FailFromPump("no successful Present for 30 seconds");
        return !g_finished.load(std::memory_order_acquire);
    }
    if (presented < g_present_cursor) {
        FailFromPump("cumulative Present counter moved backwards");
        return false;
    }
    const uint64_t batch = presented - g_present_cursor;
    const uint64_t action_steps = TourActionStepsForPresentBatch(batch);
    if (batch > 1) {
        Record("present_batch count=%llu action_steps=%llu policy=action-timeline-not-compressed", false,
               (unsigned long long)batch, (unsigned long long)action_steps);
    }
    g_present_cursor = presented;
    g_last_progress_ns = now;
    // Never emit two logical frames after one EngineService: doing so can press and release a control before the game
    // runs again. The actual (possibly larger) presented-frame dwell is recorded when each phase completes.
    for (uint64_t i = 0; i < action_steps && !g_finished.load(std::memory_order_acquire); ++i) {
        g_cursor.AdvanceFrame();
        ++g_action_frames;
        RunDueActions();
        if (g_cursor.Complete()) CompletePhase();
    }
    return !g_finished.load(std::memory_order_acquire);
}

bool DiagnosticTourFailed() { return g_result.load(std::memory_order_acquire) == 2; }

void DiagnosticTourFail(const char* phase, const char* reason) {
    if (!g_started.load(std::memory_order_acquire) || g_finished.load(std::memory_order_acquire)) return;
    if (phase && *phase) g_phase_name.store(phase, std::memory_order_release);
    if (g_phase_active) {
        const int64_t now = MonotonicNanos();
        const int64_t wall_ns = ElapsedNanos(g_phase_started_ns, now);
        const int64_t cpu_end_ns = ProcessCpuNanos();
        const int64_t cpu_ns = g_phase_cpu_start_ns >= 0 && cpu_end_ns >= g_phase_cpu_start_ns
                                   ? cpu_end_ns - g_phase_cpu_start_ns : -1;
        Record("phase=%s status=failed reason=%s wall_ms=%.3f process_cpu_ms=%.3f "
               "process_cpu_utilization_pct=%.2f actions_executed=%llu action_frames=%u presented_frames=%llu "
               "gameplay_dispatches=%u",
               true, g_phase_name.load(std::memory_order_relaxed), reason ? reason : "unspecified",
               Millis(wall_ns), Millis(cpu_ns), CpuPercent(cpu_ns, wall_ns),
               (unsigned long long)(g_actions_executed.load(std::memory_order_acquire) - g_phase_action_start),
               g_cursor.frame(), (unsigned long long)CounterDelta(PresentedFrameCount(), g_phase_present_start),
               g_gameplay_dispatches.load(std::memory_order_acquire) - g_phase_gameplay_start);
    } else {
        Record("phase=%s status=failed reason=%s phase_metrics=not-started", true,
               g_phase_name.load(std::memory_order_relaxed), reason ? reason : "unspecified");
    }
    Finish(2, reason);
}

void DiagnosticTourAbort(const char* reason) {
    if (!g_started.load(std::memory_order_acquire) || g_finished.load(std::memory_order_acquire)) return;
    ReleaseInjectedInput();
    Record("phase=%s status=aborted reason=%s", true, g_phase_name.load(std::memory_order_relaxed),
           reason ? reason : "unspecified");
    Finish(3, reason);
}

void DiagnosticTourWatchdogStalled(uint64_t frame, int seconds) {
    if (!g_started.load(std::memory_order_acquire)) return;  // Ordinary Play keeps its existing watchdog behavior.
    if (!g_finished.load(std::memory_order_acquire)) {
        char reason[192];
        snprintf(reason, sizeof(reason), "main-loop frame %llu did not advance for %d seconds; watchdog backtraces captured",
                 (unsigned long long)frame, seconds);
        // Do not inject/release input or call guest code on the watchdog thread. Atomic markers are flushed before the
        // isolated process exits; process teardown releases any held OS input state without touching normal saves.
        DiagnosticTourFail(g_phase_name.load(std::memory_order_acquire), reason);
    }
    if (!DiagnosticTourFailed()) return;
    Log("diagnostic tour watchdog: ending the failed isolated process without EngineTerminate");
    _exit(71);
}

void DiagnosticTourCrashed(int signal_number) {
    if (!g_started.load(std::memory_order_acquire) || !g_crash_path[0]) return;
    char line[768];
    int length = snprintf(line, sizeof(line),
                          "crash signal=%d phase=%s result=%s present=%llu gameplay_observed=%u "
                          "gameplay_dispatches=%u actions_executed=%llu\n",
                          signal_number, g_phase_name.load(std::memory_order_relaxed),
                          ResultName(g_result.load(std::memory_order_relaxed)),
                          (unsigned long long)PresentedFrameCount(), g_gameplay_observed.load() ? 1u : 0u,
                          g_gameplay_dispatches.load(std::memory_order_relaxed),
                          (unsigned long long)g_actions_executed.load(std::memory_order_relaxed));
    if (length <= 0) return;
    int fd = open(g_crash_path, O_WRONLY | O_APPEND | O_CLOEXEC);
    if (fd < 0) return;
    WriteAll(fd, line, std::min(size_t(length), sizeof(line) - 1));
    fsync(fd);
    close(fd);
}

}  // namespace native
