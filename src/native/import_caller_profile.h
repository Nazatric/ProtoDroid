#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace native {

// Diagnostic-only bounded (import, guest return RVA) accounting. This lives inside import_profile's existing
// opt-in path; ordinary dispatch does not construct or touch it. Entries beyond the cap are not dropped: their
// calls/time are folded into per-import overflow totals so the emitted method totals remain auditable.
class ImportCallerProfile {
public:
    struct Entry {
        uint32_t import_index = 0;
        uint32_t caller_rva = 0;
        uint64_t total_ns = 0;
        uint64_t calls = 0;
    };
    struct Overflow {
        uint64_t total_ns = 0;
        uint64_t calls = 0;
    };

    static constexpr size_t kMaxEntries = 2048;

    void Add(uint32_t import_index, uint32_t caller_rva, uint64_t duration_ns) {
        const uint64_t key = (uint64_t(import_index) << 32) | caller_rva;
        auto found = lookup_.find(key);
        if (found == lookup_.end()) {
            if (entries_.size() >= kMaxEntries) {
                if (overflow_.size() <= import_index)
                    overflow_.resize(size_t(import_index) + 1);
                overflow_[import_index].total_ns += duration_ns;
                ++overflow_[import_index].calls;
                return;
            }
            const uint32_t position = static_cast<uint32_t>(entries_.size());
            entries_.push_back({import_index, caller_rva, duration_ns, 1});
            lookup_.emplace(key, position);
            return;
        }
        Entry& entry = entries_[found->second];
        entry.total_ns += duration_ns;
        ++entry.calls;
    }

    const std::vector<Entry>& entries() const { return entries_; }

    Overflow overflow(uint32_t import_index) const {
        return import_index < overflow_.size() ? overflow_[import_index] : Overflow{};
    }

    void Reset() {
        entries_.clear();
        lookup_.clear();
        overflow_.clear();
    }

private:
    std::vector<Entry> entries_;
    std::unordered_map<uint64_t, uint32_t> lookup_;
    std::vector<Overflow> overflow_;
};

struct ImportCallerMethod {
    uint32_t import_index = 0;
    std::string name;
    uint64_t total_ns = 0;
    uint64_t total_calls = 0;
};

inline std::vector<std::string> BuildImportCallerLines(
        const ImportCallerProfile& profile, int tid, const std::vector<ImportCallerMethod>& methods) {
    std::vector<std::string> lines;
    for (const auto& method : methods) {
        std::vector<const ImportCallerProfile::Entry*> callers;
        for (const auto& entry : profile.entries()) {
            if (entry.import_index == method.import_index) callers.push_back(&entry);
        }
        std::sort(callers.begin(), callers.end(), [](const auto* a, const auto* b) {
            return a->total_ns != b->total_ns ? a->total_ns > b->total_ns : a->caller_rva < b->caller_rva;
        });
        std::string line = "import-callers tid " + std::to_string(tid) + " method " + method.name +
                           " total_ns=" + std::to_string(method.total_ns) +
                           " total_calls=" + std::to_string(method.total_calls);
        uint64_t listed_ns = 0, listed_calls = 0;
        for (size_t i = 0; i < callers.size() && i < 3; ++i) {
            const auto& entry = *callers[i];
            char rva[16];
            std::snprintf(rva, sizeof(rva), "%08x", entry.caller_rva);
            line += " caller=" + std::string(rva) + ":" + std::to_string(entry.total_ns) +
                    ":" + std::to_string(entry.calls);
            listed_ns += entry.total_ns;
            listed_calls += entry.calls;
        }
        line += " unlisted_ns=" + std::to_string(method.total_ns - listed_ns) +
                " unlisted_calls=" + std::to_string(method.total_calls - listed_calls);
        lines.push_back(std::move(line));
    }
    return lines;
}

}  // namespace native
