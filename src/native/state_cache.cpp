#include "state_cache.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <sys/stat.h>
#include <unistd.h>

namespace native {
namespace {

uint32_t ReadLe32(const unsigned char* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

StateCacheFileIdentity FileIdentity(const struct stat& st) {
    // Device/inode detect replacement; size and nanosecond mtime/ctime detect appends, rewrites, and in-place edits.
    return {uint64_t(st.st_dev), uint64_t(st.st_ino), uint64_t(st.st_size),
            uint64_t(st.st_mtim.tv_sec), uint64_t(st.st_mtim.tv_nsec),
            uint64_t(st.st_ctim.tv_sec), uint64_t(st.st_ctim.tv_nsec)};
}

bool ReadFileIdentity(const std::string& path, StateCacheFileIdentity& identity) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    identity = FileIdentity(st);
    return true;
}

bool FilesEqual(const std::string& source, const std::string& target) {
    // The mirror can also be requested by the crash reporter on an arbitrary thread. Keep the comparison buffers
    // off its stack; an allocation failure simply falls back to the legacy complete copy.
    constexpr size_t kBufferSize = 16384;
    std::unique_ptr<unsigned char[]> storage(new (std::nothrow) unsigned char[2 * kBufferSize]);
    if (!storage) return false;
    unsigned char* source_buffer = storage.get();
    unsigned char* target_buffer = source_buffer + kBufferSize;

    FILE* in = fopen(source.c_str(), "rb");
    FILE* backup = fopen(target.c_str(), "rb");
    if (!in || !backup) {
        if (in) fclose(in);
        if (backup) fclose(backup);
        return false;
    }
    bool equal = true;
    while (equal) {
        size_t source_count = fread(source_buffer, 1, kBufferSize, in);
        size_t target_count = fread(target_buffer, 1, kBufferSize, backup);
        if (source_count != target_count || ferror(in) || ferror(backup) ||
            (source_count && memcmp(source_buffer, target_buffer, source_count) != 0)) {
            equal = false;
            break;
        }
        if (source_count < kBufferSize) break;
    }
    if (fclose(in) != 0) equal = false;
    if (fclose(backup) != 0) equal = false;
    return equal;
}

bool CopyAtomically(const std::string& source, const std::string& target, bool durable) {
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
    if (fflush(out) != 0 || (durable && fsync(fileno(out)) != 0)) ok = false;
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

const char* StateCacheStatusName(StateCacheStatus status) {
    switch (status) {
        case StateCacheStatus::Missing: return "missing";
        case StateCacheStatus::Invalid: return "invalid";
        case StateCacheStatus::Empty: return "empty";
        case StateCacheStatus::Populated: return "populated";
    }
    return "unknown";
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
    return CopyAtomically(migration.source, migration.target, true) ? StateCacheMigrationResult::Migrated
                                                                     : StateCacheMigrationResult::Failed;
}

bool BackupStateCache(const std::string& source, const std::string& target, StateCacheBackupState* state) {
    StateCacheFileIdentity source_now{}, target_now{};
    bool have_source_now = false;
    if (state) {
        if (state->valid && state->source_path == source && state->target_path == target) {
            have_source_now = ReadFileIdentity(source, source_now);
            if (have_source_now && source_now == state->source_identity &&
                ReadFileIdentity(target, target_now) && target_now == state->target_identity &&
                FilesEqual(source, target)) {
                // Metadata avoids copying after ordinary changes, but byte comparison is still required: a coarse
                // timestamp filesystem can preserve every identity field across a rapid same-size in-place edit.
                return true;
            }
        }
        // Any non-match invalidates the fast path until another complete, stable copy succeeds.
        state->valid = false;
    }

    StateCacheFileIdentity before{};
    bool have_before = false;
    if (state) {
        // Reuse a successful fast-path source probe. Changed sources avoid both a redundant target probe and a
        // second source stat before the full copy; before/after stability validation remains unchanged.
        before = source_now;
        have_before = have_source_now || ReadFileIdentity(source, before);
    }
    if (InspectStateCache(source) != StateCacheStatus::Populated) return false;
    // Atomic replacement protects the last good backup from process interruption without adding an fsync stall to
    // gameplay; startup migration above remains durable. The ordinary mirror serializes its tracker and copies.
    if (!CopyAtomically(source, target, false)) return false;
    if (!state) return true;

    StateCacheFileIdentity after{}, written{};
    const bool have_after = ReadFileIdentity(source, after);
    const bool have_written = ReadFileIdentity(target, written);
    if (have_before && have_after && have_written && before == after) {
        state->source_path = source;
        state->target_path = target;
        state->source_identity = after;
        state->target_identity = written;
        state->valid = true;
    }
    // If the source changed before/during the copy, or either identity cannot be read, the next call must copy again.
    return true;
}

}  // namespace native
