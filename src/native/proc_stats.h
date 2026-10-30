// Parser for Linux /proc/PID/stat and /proc/PID/task/TID/stat. comm may contain spaces and ')' characters, so the
// final ") " delimiter is significant; fixed whitespace splitting misreads otherwise valid thread names.
#pragma once

#include <charconv>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace native {

struct ProcStatInfo {
    int pid = 0;
    std::string comm;
    char state = '?';
    uint64_t user_jiffies = 0;
    uint64_t system_jiffies = 0;
    uint64_t start_time_jiffies = 0;
    int processor = -1;
};

namespace detail {

template <typename T>
bool ParseProcNumber(const std::string& text, T& value) {
    if (text.empty()) return false;
    const char* begin = text.data();
    const char* end = begin + text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(*begin))) ++begin;
    const auto result = std::from_chars(begin, end, value);
    const char* trailing = result.ptr;
    while (trailing < end && std::isspace(static_cast<unsigned char>(*trailing))) ++trailing;
    return result.ec == std::errc() && trailing == end;
}

}  // namespace detail

inline bool ParseProcStat(const std::string& text, ProcStatInfo& out) {
    const size_t open = text.find('(');
    const size_t close = text.rfind(") ");
    if (open == std::string::npos || close == std::string::npos || close <= open + 1) return false;

    ProcStatInfo parsed;
    if (!detail::ParseProcNumber(text.substr(0, open), parsed.pid)) return false;
    parsed.comm = text.substr(open + 1, close - open - 1);

    std::istringstream input(text.substr(close + 2));
    std::vector<std::string> fields;
    for (std::string field; input >> field;) fields.push_back(std::move(field));
    // fields[0] is field 3 (state). We need through field 39 (processor).
    if (fields.size() <= 36 || fields[0].size() != 1) return false;
    parsed.state = fields[0][0];
    if (!detail::ParseProcNumber(fields[11], parsed.user_jiffies) ||
        !detail::ParseProcNumber(fields[12], parsed.system_jiffies) ||
        !detail::ParseProcNumber(fields[19], parsed.start_time_jiffies) ||
        !detail::ParseProcNumber(fields[36], parsed.processor)) {
        return false;
    }

    out = std::move(parsed);
    return true;
}

}  // namespace native
