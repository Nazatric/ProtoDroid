#include "state_cache.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

namespace native {
namespace {

uint32_t ReadLe32(const unsigned char* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

bool CopyAtomically(const std::string& source, const std::string& target) {
    FILE* in = fopen(source.c_str(), "rb");
    if (!in) return false;

    std::string temporary = target + ".migrate.XXXXXX";
    std::vector<char> name(temporary.begin(), temporary.end());
    name.push_back('\0');
    int fd = mkstemp(name.data());
    if (fd < 0) {
        fclose(in);
        return false;
    }
    FILE* out = fdopen(fd, "wb");
    if (!out) {
        close(fd);
        unlink(name.data());
        fclose(in);
        return false;
    }

    bool ok = true;
    unsigned char buffer[16384];
    while (ok) {
        size_t count = fread(buffer, 1, sizeof(buffer), in);
        if (count && fwrite(buffer, 1, count, out) != count) ok = false;
        if (count < sizeof(buffer)) {
            if (ferror(in)) ok = false;
            break;
        }
    }
    if (fflush(out) != 0 || fsync(fileno(out)) != 0) ok = false;
    if (fclose(out) != 0) ok = false;
    fclose(in);
    if (ok && rename(name.data(), target.c_str()) != 0) ok = false;
    if (!ok) unlink(name.data());
    return ok;
}

}  // namespace

std::string StateCacheFile(const std::string& directory, const std::string& filename) {
    if (directory.empty() || directory.back() == '/') return directory + filename;
    return directory + '/' + filename;
}

StateCacheStatus InspectStateCache(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return StateCacheStatus::Missing;
    if (!S_ISREG(st.st_mode) || st.st_size < 12) return StateCacheStatus::Invalid;

    FILE* file = fopen(path.c_str(), "rb");
    if (!file) return StateCacheStatus::Invalid;
    unsigned char header[12];
    size_t count = fread(header, 1, sizeof(header), file);
    bool read_error = ferror(file) != 0;
    fclose(file);
    if (count != sizeof(header) || read_error || memcmp(header, "DXVK", 4) != 0) return StateCacheStatus::Invalid;

    // DXVK v2.5.3 writes version 18 and accepts v8 through v18 except v16. Entry payloads and hashes remain DXVK's
    // responsibility; this check only prevents arbitrary files from being selected as migration candidates.
    uint32_t version = ReadLe32(header + 4);
    if (version < 8 || version > 18 || version == 16) return StateCacheStatus::Invalid;
    return st.st_size == 12 ? StateCacheStatus::Empty : StateCacheStatus::Populated;
}

StateCacheMigration PlanStateCacheMigration(const std::string& directory,
                                            const std::vector<std::string>& shared_directories) {
    StateCacheMigration plan;
    plan.target = StateCacheFile(directory, kStateCacheFilename);
    StateCacheStatus target = InspectStateCache(plan.target);
    if (target == StateCacheStatus::Populated || target == StateCacheStatus::Invalid) return plan;

    auto usable = [](StateCacheStatus status) { return status == StateCacheStatus::Populated; };
    std::string legacy = StateCacheFile(directory, kLegacyStateCacheFilename);
    if (usable(InspectStateCache(legacy))) {
        plan.source = legacy;
        plan.copy = false;
        return plan;
    }
    for (const std::string& shared : shared_directories) {
        for (const char* filename : {kStateCacheFilename, kLegacyStateCacheFilename}) {
            std::string candidate = StateCacheFile(shared, filename);
            if (candidate != plan.target && usable(InspectStateCache(candidate))) {
                plan.source = candidate;
                plan.copy = true;
                return plan;
            }
        }
    }
    return plan;
}

StateCacheMigrationResult MigrateStateCache(const StateCacheMigration& migration) {
    if (migration.source.empty()) return StateCacheMigrationResult::NoCandidate;
    if (!migration.copy) {
        if (rename(migration.source.c_str(), migration.target.c_str()) == 0) return StateCacheMigrationResult::Migrated;
        // Cross-device or restricted renames fall back to an atomic copy. Keep the legacy source if this succeeds.
    }
    return CopyAtomically(migration.source, migration.target) ? StateCacheMigrationResult::Migrated
                                                              : StateCacheMigrationResult::Failed;
}

}  // namespace native
