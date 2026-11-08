#include "dxvk_state_cache_io.h"
#include "state_cache.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
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
    TestEmptyAndInvalidCandidatesIgnored();
    std::puts("state-cache tests passed (resolution, migration, atomic backup, entry layout, flush, drain)");
    return 0;
}
