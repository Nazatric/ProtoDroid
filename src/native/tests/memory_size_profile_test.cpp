#include "memory_size_profile.h"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "memory-size-profile test failed: %s\n", message);
        std::exit(1);
    }
}

void TestBinBoundaries() {
    const std::vector<std::pair<uint32_t, size_t>> cases = {
        {0, 0}, {1, 1}, {8, 1}, {9, 2}, {16, 2}, {17, 3}, {32, 3}, {33, 4}, {64, 4},
        {65, 5}, {128, 5}, {129, 6}, {256, 6}, {257, 7}, {1024, 7}, {1025, 8}, {4096, 8},
        {4097, 9}, {16384, 9}, {16385, 10}, {65536, 10}, {65537, 11}, {262144, 11},
        {262145, 12}, {1048576, 12}, {1048577, 13}, {std::numeric_limits<uint32_t>::max(), 13},
    };
    for (const auto& item : cases) Check(native::MemorySizeBin(item.first) == item.second, "size-bin boundary");
}

void TestAccountingAndReset() {
    native::MemorySizeProfile profile;
    const std::vector<uint32_t> sizes = {
        0, 1, 8, 9, 16, 17, 32, 33, 64, 65, 128, 129, 256, 257, 1024, 1025, 4096, 4097,
        16384, 16385, 65536, 65537, 262144, 262145, 1048576, 1048577, std::numeric_limits<uint32_t>::max(),
    };
    uint64_t bytes = 0;
    for (const uint32_t size : sizes) {
        profile.Add(7, size);
        bytes += size;
    }
    profile.Add(2, 16);
    const native::MemorySizeRecord* record = profile.Find(7);
    Check(record && record->calls == sizes.size(), "import call accounting");
    Check(record->total_bytes == bytes && record->min_size == 0 &&
              record->max_size == std::numeric_limits<uint32_t>::max(), "byte/min/max accounting");
    uint64_t binned = 0;
    for (size_t i = 0; i < record->bins.size(); ++i) {
        binned += record->bins[i];
        Check(record->bins[i] != 0, "a covered size bin was not populated");
    }
    Check(binned == record->calls, "bins dropped or duplicated calls");
    Check(profile.Find(2) && profile.Find(2)->calls == 1 && profile.Find(2)->min_size == 16,
          "independent import record");
    Check(profile.Find(1) == nullptr && profile.Find(99) == nullptr, "unknown import invented a record");

    const std::string line = native::BuildMemorySizeLine(42, "memcpy", *record, record->calls);
    Check(line.find("memory-sizes tid 42 name=memcpy") == 0, "line identity");
    Check(line.find("total_bytes=" + std::to_string(bytes)) != std::string::npos, "line byte total");
    Check(line.find("1048577+:2") != std::string::npos, "line upper-bin count");

    profile.Reset();
    Check(profile.Find(2) == nullptr && profile.Find(7) == nullptr, "window reset retained calls");
    profile.Add(7, 100);
    record = profile.Find(7);
    Check(record && record->calls == 1 && record->total_bytes == 100 && record->min_size == 100 &&
              record->max_size == 100 && record->bins[native::MemorySizeBin(100)] == 1,
          "reset did not clear previous min/max/bytes/bins");
}

}  // namespace

int main() {
    TestBinBoundaries();
    TestAccountingAndReset();
    std::puts("memory-size-profile tests passed (all boundaries, exact bins/bytes, independent imports, line, reset)");
    return 0;
}
