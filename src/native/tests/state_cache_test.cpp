#include "dxvk_state_cache_io.h"
#include "state_cache.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

namespace fs = std::filesystem;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "state-cache test failed: %s\n", message);
        std::exit(1);
    }
}

struct Header {
    uint32_t type;
    uint32_t size;
};
struct Hash {
    uint8_t bytes[4];
};

struct FakeStream {
    std::vector<std::string> writes;
    int flushes = 0;

    void write(const char* data, size_t size) { writes.emplace_back(data, size); }
    void flush() { ++flushes; }
};

struct TempDirectory {
    fs::path path;

    TempDirectory() {
        char pattern[] = "/tmp/protodroid-state-cache-XXXXXX";
        char* created = mkdtemp(pattern);
        Check(created != nullptr, "could not create temporary test directory");
        path = created;
    }
    ~TempDirectory() {
        std::error_code error;
        fs::remove_all(path, error);
    }
};

template<typename T>
std::string Bytes(const T& value) {
    return std::string(reinterpret_cast<const char*>(&value), sizeof(value));
}

void WriteCache(const fs::path& path, uint32_t version, const std::string& payload, const char* magic = "DXVK") {
    fs::create_directories(path.parent_path());
    unsigned char header[12] = {0};
    memcpy(header, magic, 4);
    for (int i = 0; i < 4; ++i) header[4 + i] = static_cast<unsigned char>(version >> (8 * i));
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(header), sizeof(header));
    file.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    file.close();
    Check(file.good(), "could not write cache fixture");
}

std::string ReadFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void TestIncrementalEntryFlushAndLayout() {
    const Header header = {7, 5};
    const Hash hash = {{0x91, 0x82, 0x73, 0x64}};
    const char payload[] = {'c', 'a', 'c', 'h', 'e'};
    FakeStream stream;

    dxvk::writeStateCacheEntryPayload(stream, header, hash, payload, sizeof(payload));
    Check(stream.flushes == 1, "one entry did not produce exactly one incremental flush");
    Check(stream.writes.size() == 3, "entry layout does not contain header, hash, and payload");
    Check(stream.writes[0] == Bytes(header), "entry header bytes changed");
    Check(stream.writes[1] == Bytes(hash), "entry hash bytes changed");
    Check(stream.writes[2] == std::string(payload, sizeof(payload)), "entry payload bytes changed");

    dxvk::writeStateCacheEntryPayload(stream, header, hash, payload, sizeof(payload));
    Check(stream.flushes == 2 && stream.writes.size() == 6,
          "second entry was buffered instead of flushed incrementally");
}

void TestWriterDrainPolicy() {
    Check(dxvk::stateCacheWriterShouldStop(true, true), "empty stopped writer did not exit");
    Check(!dxvk::stateCacheWriterShouldStop(false, true), "stop request discarded queued cache entries");
    Check(!dxvk::stateCacheWriterShouldStop(true, false), "idle writer exited without a stop request");
    Check(!dxvk::stateCacheWriterShouldStop(false, false), "active writer exited unexpectedly");
}

void TestExactDxvkResolution() {
    Check(std::string(dxvk::stateCachePathEnvironmentVariable()) == "DXVK_STATE_CACHE_PATH",
          "DXVK cache directory environment variable changed");
    Check(std::string(dxvk::androidStateCacheExecutableName()) == "prototypeenginef.dll",
          "Android executable name no longer matches the game DLL");
    Check(dxvk::stateCacheFileName("/data/cache", "prototypeenginef.dll") ==
              "/data/cache/prototypeenginef.dll.dxvk-cache",
          "DXVK resolved an unexpected Android cache filename");
    Check(dxvk::stateCacheFileName("/data/cache/", "prototypeenginef.dll") ==
              "/data/cache/prototypeenginef.dll.dxvk-cache",
          "DXVK added a duplicate cache-path separator");
    Check(dxvk::stateCacheFileName("", "prototypeenginef.dll") == "prototypeenginef.dll.dxvk-cache",
          "DXVK empty-directory resolution changed");
    Check(native::StateCacheFile("/data/cache", native::kStateCacheFilename) ==
              "/data/cache/prototypeenginef.dll.dxvk-cache",
          "native and DXVK cache paths disagree");
}

void TestCacheHeaderInspection() {
    Check(std::string(native::StateCacheStatusName(native::StateCacheStatus::Missing)) == "missing" &&
              std::string(native::StateCacheStatusName(native::StateCacheStatus::Invalid)) == "invalid" &&
              std::string(native::StateCacheStatusName(native::StateCacheStatus::Empty)) == "empty" &&
              std::string(native::StateCacheStatusName(native::StateCacheStatus::Populated)) == "populated",
          "persistent cache-status labels changed or are incomplete");
    TempDirectory temp;
    fs::path cache = temp.path / "cache.dxvk-cache";
    Check(native::InspectStateCache(cache.string()) == native::StateCacheStatus::Missing,
          "missing cache was not reported as missing");
    WriteCache(cache, 18, "");
    Check(native::InspectStateCache(cache.string()) == native::StateCacheStatus::Empty,
          "valid header-only cache was not recognized");
    WriteCache(cache, 18, "payload");
    Check(native::InspectStateCache(cache.string()) == native::StateCacheStatus::Populated,
          "valid populated cache candidate was rejected");
    WriteCache(cache, 17, "payload");
    Check(native::InspectStateCache(cache.string()) == native::StateCacheStatus::Populated,
          "supported older DXVK cache was rejected");
    for (uint32_t version : {7u, 16u, 19u}) {
        WriteCache(cache, version, "payload");
        Check(native::InspectStateCache(cache.string()) == native::StateCacheStatus::Invalid,
              "unsupported DXVK cache version was accepted");
    }
    WriteCache(cache, 18, "payload", "NOPE");
    Check(native::InspectStateCache(cache.string()) == native::StateCacheStatus::Invalid,
          "arbitrary file with a non-DXVK header was accepted");
    std::ofstream(cache, std::ios::binary | std::ios::trunc).write("DXVK", 4);
    Check(native::InspectStateCache(cache.string()) == native::StateCacheStatus::Invalid,
          "truncated DXVK header was accepted");
}

void TestLegacyMigration() {
    TempDirectory temp;
    fs::path base = temp.path / "base";
    fs::create_directories(base);
    fs::path legacy = base / native::kLegacyStateCacheFilename;
    WriteCache(legacy, 18, "legacy-cache-payload");

    native::StateCacheMigration plan = native::PlanStateCacheMigration(base.string(), {});
    Check(plan.source == legacy.string() && !plan.copy, "readable legacy cache was not selected for migration");
    Check(native::MigrateStateCache(plan) == native::StateCacheMigrationResult::Migrated,
          "legacy cache migration failed");
    Check(!fs::exists(legacy), "same-directory legacy migration did not rename the source");
    Check(native::InspectStateCache(plan.target) == native::StateCacheStatus::Populated,
          "migrated legacy cache is not readable at DXVK's resolved path");
    Check(ReadFile(plan.target).find("legacy-cache-payload") != std::string::npos,
          "legacy cache payload changed during migration");
}

void TestSharedRestoreAndTargetPreservation() {
    TempDirectory temp;
    fs::path base = temp.path / "base", shared = temp.path / "shared";
    fs::path target = base / native::kStateCacheFilename;
    fs::path shared_cache = shared / native::kStateCacheFilename;
    std::vector<std::string> shared_dirs = {shared.string()};

    WriteCache(target, 18, "");
    WriteCache(shared_cache, 18, "shared-cache-payload");
    native::StateCacheMigration plan = native::PlanStateCacheMigration(base.string(), shared_dirs);
    Check(plan.source == shared_cache.string() && plan.copy, "populated shared cache did not replace an empty target");
    Check(native::MigrateStateCache(plan) == native::StateCacheMigrationResult::Migrated,
          "shared cache restore failed");
    Check(ReadFile(target) == ReadFile(shared_cache), "atomic shared cache copy changed its bytes");
    Check(fs::exists(shared_cache), "shared cache backup was consumed by a copy migration");

    WriteCache(target, 18, "private-cache-payload");
    WriteCache(shared_cache, 18, "different-shared-payload");
    plan = native::PlanStateCacheMigration(base.string(), shared_dirs);
    Check(plan.source.empty(), "shared cache displaced an already populated private cache");
    Check(ReadFile(target).find("private-cache-payload") != std::string::npos,
          "populated private cache was not preserved");

    WriteCache(target, 18, "payload", "BAD!");
    plan = native::PlanStateCacheMigration(base.string(), shared_dirs);
    Check(plan.source.empty(), "invalid private cache was silently overwritten instead of left for DXVK diagnosis");
    Check(ReadFile(target).find("BAD!") == 0, "invalid target changed despite the no-migration plan");
}

void TestAtomicBackupPreservesLastGoodCopy() {
    TempDirectory temp;
    fs::path source = temp.path / "private" / native::kStateCacheFilename;
    fs::path target = temp.path / "shared" / native::kStateCacheFilename;
    fs::create_directories(target.parent_path());
    WriteCache(target, 18, "last-known-good");
    const std::string previous = ReadFile(target);

    Check(!native::BackupStateCache(source.string(), target.string()), "missing source unexpectedly replaced a backup");
    Check(ReadFile(target) == previous, "missing source changed the last good backup");

    WriteCache(source, 18, "");
    Check(!native::BackupStateCache(source.string(), target.string()), "header-only source replaced a populated backup");
    Check(ReadFile(target) == previous, "empty source changed the last good backup");

    WriteCache(source, 18, "invalid", "NOPE");
    Check(!native::BackupStateCache(source.string(), target.string()), "invalid source replaced a valid backup");
    Check(ReadFile(target) == previous, "invalid source changed the last good backup");

    WriteCache(source, 18, "new-cache-payload");
    Check(native::BackupStateCache(source.string(), target.string()), "populated cache backup failed");
    Check(ReadFile(target) == ReadFile(source), "atomic backup did not copy the populated source exactly");
    Check(fs::exists(source), "backup consumed the live private cache");
    for (const fs::directory_entry& entry : fs::directory_iterator(target.parent_path()))
        Check(entry.path().filename().string().find(".migrate.") == std::string::npos,
              "successful backup left a temporary file behind");
}

void TestReadOnlyFailurePreservesCache() {
    if (geteuid() == 0) {
        std::puts("state-cache read-only test skipped for root");
        return;
    }

    TempDirectory temp;
    fs::path source = temp.path / "private" / native::kStateCacheFilename;
    fs::path target = temp.path / "shared" / native::kStateCacheFilename;
    fs::create_directories(target.parent_path());
    WriteCache(source, 18, "initial");
    native::StateCacheBackupState state;
    Check(native::BackupStateCache(source.string(), target.string(), &state) && state.valid,
          "initial read-only-fixture backup failed");
    WriteCache(source, 18, "updated");
    const std::string previous = ReadFile(target);
    Check(chmod(target.parent_path().c_str(), 0500) == 0, "could not make backup directory read-only");
    const bool backup_ok = native::BackupStateCache(source.string(), target.string(), &state);
    Check(chmod(target.parent_path().c_str(), 0700) == 0, "could not restore backup directory permissions");
    Check(!backup_ok && !state.valid, "read-only backup unexpectedly succeeded or retained its fast path");
    Check(ReadFile(target) == previous, "read-only backup damaged the last good target");
    Check(native::BackupStateCache(source.string(), target.string(), &state) && state.valid,
          "backup did not recover after permissions were restored");
    Check(ReadFile(target) == ReadFile(source), "read-only backup retry did not copy exact source bytes");

    fs::path base = temp.path / "migration-base";
    fs::path shared = temp.path / "migration-source";
    fs::path migration_target = base / native::kStateCacheFilename;
    fs::path migration_source = shared / native::kStateCacheFilename;
    WriteCache(migration_target, 18, "");
    WriteCache(migration_source, 18, "shared-restore");
    const std::string shared_bytes = ReadFile(migration_source);
    native::StateCacheMigration plan = native::PlanStateCacheMigration(base.string(), {shared.string()});
    Check(plan.source == migration_source.string() && plan.copy, "read-only migration fixture was not selected");
    Check(chmod(base.c_str(), 0500) == 0, "could not make migration directory read-only");
    const native::StateCacheMigrationResult migration_result = native::MigrateStateCache(plan);
    Check(chmod(base.c_str(), 0700) == 0, "could not restore migration directory permissions");
    Check(migration_result == native::StateCacheMigrationResult::Failed,
          "read-only migration did not report failure");
    Check(native::InspectStateCache(migration_target.string()) == native::StateCacheStatus::Empty,
          "read-only migration replaced the empty target");
    Check(ReadFile(migration_source) == shared_bytes, "read-only migration consumed or changed its shared source");
    Check(native::MigrateStateCache(plan) == native::StateCacheMigrationResult::Migrated,
          "migration did not recover after permissions were restored");
    Check(ReadFile(migration_target) == shared_bytes, "migration retry did not copy exact shared bytes");
    for (const fs::directory_entry& entry : fs::directory_iterator(base))
        Check(entry.path().filename().string().find(".migrate.") == std::string::npos,
              "read-only migration left a temporary file behind");
}

ino_t Inode(const fs::path& path) {
    struct stat st;
    Check(stat(path.c_str(), &st) == 0, "could not stat backup inode");
    return st.st_ino;
}

native::StateCacheFileIdentity Identity(const fs::path& path) {
    struct stat st;
    Check(stat(path.c_str(), &st) == 0, "could not stat backup identity");
    return {uint64_t(st.st_dev), uint64_t(st.st_ino), uint64_t(st.st_size),
            uint64_t(st.st_mtim.tv_sec), uint64_t(st.st_mtim.tv_nsec),
            uint64_t(st.st_ctim.tv_sec), uint64_t(st.st_ctim.tv_nsec)};
}

void TestTrackedBackupSkipsOnlyUnchangedFiles() {
    TempDirectory temp;
    fs::path source = temp.path / "private" / native::kStateCacheFilename;
    fs::path target = temp.path / "shared" / native::kStateCacheFilename;
    native::StateCacheBackupState state;

    fs::create_directories(target.parent_path());
    WriteCache(source, 18, "aaaa");
    Check(native::BackupStateCache(source.string(), target.string(), &state), "initial tracked backup failed");
    Check(state.valid && ReadFile(target) == ReadFile(source), "initial tracked backup was not valid and exact");
    const ino_t initial_target = Inode(target);

    Check(native::BackupStateCache(source.string(), target.string(), &state), "unchanged tracked backup failed");
    Check(Inode(target) == initial_target, "unchanged backup was unnecessarily replaced");
    Check(state.valid, "unchanged backup did not retain its validated state");

    // Simulate a coarse-timestamp/external-editor collision: every tracked identity matches, but the bytes do not.
    // This must remain a full-copy case even when metadata alone would incorrectly authorize a skip.
    WriteCache(target, 18, "cccc");
    state.source_identity = Identity(source);
    state.target_identity = Identity(target);
    state.valid = true;
    Check(native::BackupStateCache(source.string(), target.string(), &state),
          "identity-colliding target rewrite was not backed up");
    Check(Inode(target) != initial_target && ReadFile(target) == ReadFile(source),
          "byte comparison failed to repair an identity-colliding target rewrite");
    const ino_t collision_target = Inode(target);

    struct stat source_before;
    Check(stat(source.c_str(), &source_before) == 0, "could not snapshot source timestamps");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    WriteCache(source, 18, "bbbb");  // Same-size in-place rewrite must not hit the fast path.
    const struct timespec restored_times[2] = {source_before.st_atim, source_before.st_mtim};
    Check(utimensat(AT_FDCWD, source.c_str(), restored_times, 0) == 0, "could not restore source mtime");
    struct stat source_after;
    Check(stat(source.c_str(), &source_after) == 0 && source_after.st_mtim.tv_sec == source_before.st_mtim.tv_sec &&
              source_after.st_mtim.tv_nsec == source_before.st_mtim.tv_nsec &&
              (source_after.st_ctim.tv_sec != source_before.st_ctim.tv_sec ||
               source_after.st_ctim.tv_nsec != source_before.st_ctim.tv_nsec),
          "fixture did not isolate ctime-only source detection");
    Check(native::BackupStateCache(source.string(), target.string(), &state), "same-size source rewrite was not backed up");
    Check(Inode(target) != collision_target && ReadFile(target) == ReadFile(source),
          "same-size source rewrite was skipped or copied incorrectly");
    const ino_t rewritten_target = Inode(target);

    {
        std::ofstream append(source, std::ios::binary | std::ios::app);
        append << "- appended";
        append.close();
        Check(append.good(), "could not append source cache fixture");
    }
    Check(native::BackupStateCache(source.string(), target.string(), &state), "appended source was not backed up");
    Check(Inode(target) != rewritten_target && ReadFile(target) == ReadFile(source),
          "source append was skipped or copied incorrectly");

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    WriteCache(target, 18, "external-target-edit");
    Check(native::BackupStateCache(source.string(), target.string(), &state), "external target edit was not repaired");
    Check(ReadFile(target) == ReadFile(source), "external target edit did not force a complete source copy");

    Check(fs::remove(target), "could not remove target for external-wipe test");
    Check(native::BackupStateCache(source.string(), target.string(), &state), "external target wipe was not repaired");
    Check(ReadFile(target) == ReadFile(source), "external target wipe did not restore exact source bytes");

    fs::remove_all(target.parent_path());
    Check(!native::BackupStateCache(source.string(), target.string(), &state) && !state.valid,
          "failed copy unexpectedly retained a usable fast path");
    fs::create_directories(target.parent_path());
    Check(native::BackupStateCache(source.string(), target.string(), &state) && state.valid,
          "copy failure did not force a complete retry");
    Check(ReadFile(target) == ReadFile(source), "retry after copy failure did not restore exact bytes");

    const std::string last_good = ReadFile(target);
    const ino_t good_target = Inode(target);
    WriteCache(source, 18, "invalid", "NOPE");
    Check(!native::BackupStateCache(source.string(), target.string(), &state),
          "invalid source unexpectedly succeeded with a tracker");
    Check(!state.valid && Inode(target) == good_target && ReadFile(target) == last_good,
          "invalid source damaged the last good tracked backup or left a false fast path");

    WriteCache(source, 18, "restored");
    Check(native::BackupStateCache(source.string(), target.string(), &state), "valid source did not recover after invalid input");
    Check(state.valid && ReadFile(target) == ReadFile(source), "recovered tracked backup is not valid and exact");

    fs::path second_target = temp.path / "second-shared" / native::kStateCacheFilename;
    fs::create_directories(second_target.parent_path());
    Check(native::BackupStateCache(source.string(), second_target.string(), &state), "target-path change was not copied");
    Check(state.valid && state.target_path == second_target.string() && ReadFile(second_target) == ReadFile(source),
          "target-path change reused incorrect tracker state");

    fs::path second_source = temp.path / "second-private" / native::kStateCacheFilename;
    WriteCache(second_source, 18, "second-source-payload");
    const ino_t before_source_change = Inode(second_target);
    Check(native::BackupStateCache(second_source.string(), second_target.string(), &state),
          "source-path change was not copied");
    Check(state.valid && state.source_path == second_source.string() && Inode(second_target) != before_source_change &&
              ReadFile(second_target) == ReadFile(second_source),
          "source-path change reused incorrect tracker state");

    const ino_t before_untracked = Inode(second_target);
    Check(native::BackupStateCache(second_source.string(), second_target.string()), "untracked backup failed");
    Check(Inode(second_target) != before_untracked, "untracked compatibility call unexpectedly skipped a full copy");
    for (const fs::directory_entry& entry : fs::directory_iterator(second_target.parent_path()))
        Check(entry.path().filename().string().find(".migrate.") == std::string::npos,
              "tracked backup left a temporary file behind");
}

void TestEmptyAndInvalidCandidatesIgnored() {
    TempDirectory temp;
    fs::path base = temp.path / "base", shared = temp.path / "shared";
    WriteCache(shared / native::kLegacyStateCacheFilename, 18, "");
    native::StateCacheMigration plan = native::PlanStateCacheMigration(base.string(), {shared.string()});
    Check(plan.source.empty(), "empty cache was treated as a useful migration seed");
    Check(native::MigrateStateCache(plan) == native::StateCacheMigrationResult::NoCandidate,
          "empty migration plan attempted an operation");

    WriteCache(shared / native::kStateCacheFilename, 16, "unsupported");
    plan = native::PlanStateCacheMigration(base.string(), {shared.string()});
    Check(plan.source.empty(), "unsupported cache was selected as a migration seed");
    Check(!fs::exists(base / native::kStateCacheFilename), "test fabricated a cache without a valid populated source");
}

}  // namespace

int main() {
    TestIncrementalEntryFlushAndLayout();
    TestWriterDrainPolicy();
    TestExactDxvkResolution();
    TestCacheHeaderInspection();
    TestLegacyMigration();
    TestSharedRestoreAndTargetPreservation();
    TestAtomicBackupPreservesLastGoodCopy();
    TestReadOnlyFailurePreservesCache();
    TestTrackedBackupSkipsOnlyUnchangedFiles();
    TestEmptyAndInvalidCandidatesIgnored();
    std::puts("state-cache tests passed (resolution, migration, read-only/atomic/tracked backup, entry layout, flush, drain)");
    return 0;
}
