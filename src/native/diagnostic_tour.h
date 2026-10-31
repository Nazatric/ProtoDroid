// Deliberately activated, isolated gameplay diagnostic tour.
#pragma once

#include <cstdint>

namespace native {

inline constexpr char kDiagnosticTourEnv[] = "PROTODROID_DIAGNOSTIC_TOUR";
inline constexpr char kDiagnosticTourLogDir[] = "/storage/emulated/0/Prototype/diagnostic-tour/logs";
inline constexpr char kDiagnosticTourSaveDir[] = "/storage/emulated/0/Prototype/diagnostic-tour/saves";
inline constexpr char kDiagnosticTourCommandLine[] = "Level=manhattanTest NoAutoSave";

bool DiagnosticTourEnabled();
// Creates/rotates the durable journal before the engine or DXVK starts. The cache directory is app-specific and
// isolated; logs and saves above are shared. Returns false instead of falling back to normal paths.
bool DiagnosticTourStart(const char* isolated_cache_dir);
void DiagnosticTourSaveReady(const char* path);
void DiagnosticTourEngineInitialized();

// Called only for the two preferred-base InGameState task targets when the tour probe is armed.
void DiagnosticTourGameplayObserved(uint32_t target);

// Call after EngineService. Returns false after completion/failure so the main loop can terminate cleanly.
// Ordinary Play always returns true and has no controller state.
bool DiagnosticTourPump();
bool DiagnosticTourFailed();
void DiagnosticTourFail(const char* phase, const char* reason);
void DiagnosticTourAbort(const char* reason);

// Async-signal-context best effort, like the existing native crash report. Uses only precomputed paths and atomics.
void DiagnosticTourCrashed(int signal_number);

}  // namespace native
