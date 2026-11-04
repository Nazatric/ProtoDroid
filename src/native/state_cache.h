#ifndef PROTODROID_STATE_CACHE_H
#define PROTODROID_STATE_CACHE_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace native {

inline constexpr const char* kStateCacheFilename = "prototypeenginef.dll.dxvk-cache";
inline constexpr const char* kLegacyStateCacheFilename = "app_process64.dxvk-cache";

enum class StateCacheStatus {
    Missing,
    Invalid,
    Empty,
    Populated,
};

struct StateCacheMigration {
    std::string target;
    std::string source;
    bool copy = false;
};

enum class StateCacheMigrationResult {
    NoCandidate,
    Migrated,
    Failed,
};

std::string StateCacheFile(const std::string& directory, const std::string& filename);
StateCacheStatus InspectStateCache(const std::string& path);

// The app-private cache is authoritative once it contains entries. An empty valid cache can be replaced by a
// populated legacy/shared cache. Invalid files are left for DXVK to diagnose rather than silently overwritten.
StateCacheMigration PlanStateCacheMigration(const std::string& directory,
                                            const std::vector<std::string>& shared_directories);
StateCacheMigrationResult MigrateStateCache(const StateCacheMigration& migration);

// Opaque file-version identities recorded after a stable successful backup. Callers must serialize access to a
// tracker. A tracker is valid only for its recorded source/target paths; untracked calls retain the original copy.
using StateCacheFileIdentity = std::array<uint64_t, 7>;
struct StateCacheBackupState {
    std::string source_path;
    std::string target_path;
    StateCacheFileIdentity source_identity{};
    StateCacheFileIdentity target_identity{};
    bool valid = false;
};

// Atomically replace a shared backup from a populated live cache. Missing, empty, or invalid sources return false
// without touching the target. The live writer remains in app-private storage. A supplied tracker skips only when
// both identities and all bytes match the last stable successful copy; external wipes and all changes still copy.
bool BackupStateCache(const std::string& source, const std::string& target,
                      StateCacheBackupState* state = nullptr);

}  // namespace native

#endif
