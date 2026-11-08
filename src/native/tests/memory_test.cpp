// Guest allocator lifetime checks used by the static-texture scratch-release policy.
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../guest.h"

namespace native {

std::string ArenaUsage();
void Log(const char*, ...) {}
[[noreturn]] void Fatal(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    abort();
}

}  // namespace native

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "memory test: %s\n", message);
        abort();
    }
}

uint64_t UsedMiB() {
    const std::string usage = native::ArenaUsage();
    const size_t end = usage.find(" MiB");
    Check(end != std::string::npos, "arena usage format changed");
    return std::stoull(usage.substr(0, end));
}

}  // namespace

int main() {
    using namespace native;
    InitGuestMemory();
    Check(UsedMiB() == 0, "fresh guest arena is not empty");

    // Blocks above the size-class limit come straight from pages. This is the allocation class used by substantial
    // DXT upload buffers, so completing an UnlockRect can return their pages instead of retaining them forever.
    constexpr uint32_t kLargeBytes = 4u << 20;
    const uint32_t large = HeapAlloc(kLargeBytes);
    Check(large != 0, "large guest allocation failed");
    memset(Ptr<void>(large), 0x5a, kLargeBytes);
    Check(UsedMiB() >= 4, "large allocation was not page-backed as expected");
    HeapFree(large);
    Check(UsedMiB() == 0, "large free did not release its page range");

    // The policy deliberately leaves ordinary small-block behavior alone: free-list reuse avoids turning frequent
    // dynamic locks into page-map operations.
    const uint32_t small = HeapAlloc(128);
    Check(small != 0, "small guest allocation failed");
    HeapFree(small);
    const uint32_t reused = HeapAlloc(128);
    Check(reused == small, "small size-class block was not reused");
    HeapFree(reused);

    puts("Guest large-page release and small-block reuse checks passed");
    return 0;
}
