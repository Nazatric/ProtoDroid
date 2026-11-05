#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace native {

// Opt-in import_profile accounting for CRT memory-call sizes. Records live in ThreadProfile, so ordinary dispatch does
// not construct or touch them. The fixed bins cover tiny bridge crossings through large streaming copies without
// retaining individual call arguments or guest addresses.
inline constexpr size_t kMemorySizeBinCount = 14;
inline constexpr std::array<const char*, kMemorySizeBinCount> kMemorySizeBinLabels = {
    "0", "1-8", "9-16", "17-32", "33-64", "65-128", "129-256", "257-1024",
    "1025-4096", "4097-16384", "16385-65536", "65537-262144", "262145-1048576", "1048577+",
};

inline size_t MemorySizeBin(uint32_t size) {
    if (size == 0) return 0;
    if (size <= 8) return 1;
    if (size <= 16) return 2;
    if (size <= 32) return 3;
    if (size <= 64) return 4;
    if (size <= 128) return 5;
    if (size <= 256) return 6;
    if (size <= 1024) return 7;
    if (size <= 4096) return 8;
    if (size <= 16384) return 9;
    if (size <= 65536) return 10;
    if (size <= 262144) return 11;
    if (size <= 1048576) return 12;
    return 13;
}

struct MemorySizeRecord {
    uint64_t calls = 0;
    uint64_t total_bytes = 0;
    uint32_t min_size = 0;
    uint32_t max_size = 0;
    std::array<uint64_t, kMemorySizeBinCount> bins{};

    void Add(uint32_t size) {
        if (!calls) {
            min_size = max_size = size;
        } else {
            min_size = std::min(min_size, size);
            max_size = std::max(max_size, size);
        }
        ++calls;
        total_bytes += size;
        ++bins[MemorySizeBin(size)];
    }
};

class MemorySizeProfile {
public:
    void Add(uint32_t import_index, uint32_t size) {
        if (records_.size() <= import_index) records_.resize(size_t(import_index) + 1);
        records_[import_index].Add(size);
    }

    const MemorySizeRecord* Find(uint32_t import_index) const {
        return import_index < records_.size() && records_[import_index].calls ? &records_[import_index] : nullptr;
    }

    void Reset() {
        for (MemorySizeRecord& record : records_) record = {};
    }

private:
    std::vector<MemorySizeRecord> records_;
};

inline std::string BuildMemorySizeLine(int tid, const std::string& name, const MemorySizeRecord& record,
                                       uint64_t import_calls) {
    std::string line = "memory-sizes tid " + std::to_string(tid) + " name=" + name +
                       " calls=" + std::to_string(record.calls) +
                       " import_calls=" + std::to_string(import_calls) +
                       " total_bytes=" + std::to_string(record.total_bytes) +
                       " min=" + std::to_string(record.min_size) + " max=" + std::to_string(record.max_size) +
                       " bins=";
    for (size_t i = 0; i < record.bins.size(); ++i) {
        if (i) line += ',';
        line += kMemorySizeBinLabels[i];
        line += ':';
        line += std::to_string(record.bins[i]);
    }
    return line;
}

}  // namespace native
