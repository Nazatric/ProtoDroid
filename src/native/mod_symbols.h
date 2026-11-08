#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace native {

struct ModSymbolMatch {
    std::string name;
    uint32_t rva = 0;
    uint32_t address = 0;
    uint32_t ordinal = 0;
    uint32_t matches = 0;
};

size_t ModSymbolCount();
bool ModSymbolResolve(const std::string& qualified_name, uint32_t ordinal, ModSymbolMatch* match);
std::vector<ModSymbolMatch> ModSymbolSearch(const std::string& query, size_t limit = 32);

}  // namespace native
