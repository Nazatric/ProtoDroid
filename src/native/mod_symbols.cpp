#include "mod_symbols.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "guest.h"

namespace native {
namespace {

struct ModSymbolRecord {
    uint32_t name_offset;
    uint32_t rva;
};

#include "mod_symbols.inc"

const char* RecordName(size_t index) { return kModSymbolNames + kModSymbolRecords[index].name_offset; }

int CompareRecord(size_t index, const std::string& name) { return strcmp(RecordName(index), name.c_str()); }

size_t LowerBound(const std::string& name) {
    size_t first = 0, count = kModSymbolCount;
    while (count) {
        size_t step = count / 2;
        size_t at = first + step;
        if (CompareRecord(at, name) < 0) {
            first = at + 1;
            count -= step + 1;
        } else {
            count = step;
        }
    }
    return first;
}

ModSymbolMatch MakeMatch(size_t index, size_t first, size_t last) {
    ModSymbolMatch result;
    result.name = RecordName(index);
    result.rva = kModSymbolRecords[index].rva;
    result.address = kEngineBase + result.rva;
    result.ordinal = static_cast<uint32_t>(index - first + 1);
    result.matches = static_cast<uint32_t>(last - first);
    return result;
}

char Lower(char value) { return static_cast<char>(std::tolower(static_cast<unsigned char>(value))); }

bool ContainsInsensitive(const char* haystack, const std::string& needle) {
    const size_t length = strlen(haystack);
    if (needle.size() > length) return false;
    for (size_t start = 0; start + needle.size() <= length; ++start) {
        size_t at = 0;
        while (at < needle.size() && Lower(haystack[start + at]) == Lower(needle[at])) ++at;
        if (at == needle.size()) return true;
    }
    return false;
}

}  // namespace

size_t ModSymbolCount() { return kModSymbolCount; }

bool ModSymbolResolve(const std::string& qualified_name, uint32_t ordinal, ModSymbolMatch* match) {
    if (!match || qualified_name.empty() || qualified_name.size() > 4095 || ordinal == 0) return false;
    const size_t first = LowerBound(qualified_name);
    if (first == kModSymbolCount || CompareRecord(first, qualified_name) != 0) return false;
    size_t last = first + 1;
    while (last < kModSymbolCount && CompareRecord(last, qualified_name) == 0) ++last;
    if (ordinal > last - first) return false;
    *match = MakeMatch(first + ordinal - 1, first, last);
    return true;
}

std::vector<ModSymbolMatch> ModSymbolSearch(const std::string& query, size_t limit) {
    std::vector<ModSymbolMatch> result;
    if (query.empty() || query.size() > 256 || limit == 0) return result;
    limit = std::min(limit, size_t{64});
    result.reserve(limit);
    for (size_t index = 0; index < kModSymbolCount && result.size() < limit; ++index) {
        if (!ContainsInsensitive(RecordName(index), query)) continue;
        const std::string name = RecordName(index);
        const size_t first = LowerBound(name);
        size_t last = index + 1;
        while (last < kModSymbolCount && CompareRecord(last, name) == 0) ++last;
        result.push_back(MakeMatch(index, first, last));
    }
    return result;
}

}  // namespace native
