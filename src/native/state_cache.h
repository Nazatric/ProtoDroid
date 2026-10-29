#ifndef PROTODROID_STATE_CACHE_H
#define PROTODROID_STATE_CACHE_H

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

}  // namespace native

#endif
