// Save/profile storage location and one-time migration from the legacy app-specific directory.
#pragma once

#include <string>

namespace native {

// The engine appends profile.bin and slot-*.bin below this guest directory.
extern const char* const kGuestSaveDir;
extern const char* const kAndroidSharedSaveDir;

// Select the shared save directory only when it is genuinely writable. The first successful selection migrates the
// legacy files without overwriting shared files and records completion so later external deletions are not undone.
// Returns true when shared storage is active; otherwise the legacy app-specific directory remains active.
bool ConfigureSaveDirectory(const char* data_dir, const char* shared_dir);
const std::string& SaveDirectory();

// Matches only the save directory itself or its descendants, case-insensitively like Win32.
bool IsGuestSavePath(const std::string& full_path);

}  // namespace native
