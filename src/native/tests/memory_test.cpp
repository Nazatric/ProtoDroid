// Guest allocator lifetime checks used by the static-texture scratch-release policy.
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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

    // Every size the guest can ask for, not just the two above. The size-class table used to leave a gap between its
    // largest class and its page-backed limit, and a zero-initialised lookup answered every size in that gap with the
    // 32-byte class - a 60,000-byte request got 32 bytes and the caller's first store corrupted the heap next to it.
    // Requests in the gap are also checked to come from pages, and every block must be 16-byte aligned because the
    // engine stores 16-byte structures and COM objects in them (the 1.5x growth sequence reaches odd sizes such as
    // 13122 and 44286, and a block's user pointer is its header address plus 16).
    auto distance = [](uint32_t a, uint32_t b) { return a > b ? a - b : b - a; };
    const uint64_t sweep_start = UsedMiB();
    for (uint32_t size = 1; size <= 70000; ++size) {
        const uint32_t a = HeapAlloc(size);
        const uint32_t b = HeapAlloc(size);
        if (!a || !b) {
            Check(false, "guest allocation failed across the size sweep");
        }
        Check((a & 15u) == 0 && (b & 15u) == 0, "a guest block is not 16-byte aligned");
        Check(HeapSize(a) == size && HeapSize(b) == size, "HeapSize does not return the requested size");
        Check(distance(a, b) >= size + 16, "two guest blocks of one size overlap (size class answered with a smaller block)");
        // Touch both ends of each block: with the gap bug the first store already ran past the block.
        // A one-byte block has one byte, so the pattern has to be written once for it.
        Ptr<uint8_t>(a)[0] = 0xa5;
        Ptr<uint8_t>(b)[0] = 0xa5;
        if (size > 1) {
            Ptr<uint8_t>(a)[size - 1] = 0x5a;
            Ptr<uint8_t>(b)[size - 1] = 0x5a;
        }
        Check(Ptr<uint8_t>(a)[0] == 0xa5 && Ptr<uint8_t>(b)[0] == 0xa5 &&
                  (size == 1 || (Ptr<uint8_t>(a)[size - 1] == 0x5a && Ptr<uint8_t>(b)[size - 1] == 0x5a)),
              "a guest block was overwritten by its neighbour");
        HeapFree(a);
        HeapFree(b);
    }
    // Small blocks keep their page chunks (that is the point of the size classes), so the sweep may retain a few
    // megabytes of chunk space; what it must not do is retain a page range per request.
    Check(UsedMiB() - sweep_start <= 8, "the size sweep retained far more guest space than its chunks");

    // The range above the largest size class is page-backed, so its memory is returned on free rather than cached.
    // Enough blocks are taken that the arena's usage moves by whole megabytes, which is what distinguishes real pages
    // from the 32-byte class the gap used to fall into.
    constexpr uint32_t kGapBytes = 52000;
    constexpr int kGapBlocks = 200;
    const uint64_t gap_start = UsedMiB();
    std::vector<uint32_t> gap;
    for (int i = 0; i < kGapBlocks; ++i) {
        const uint32_t block = HeapAlloc(kGapBytes);
        Check(block != 0, "allocation above the largest size class failed");
        memset(Ptr<void>(block), 0x5a, kGapBytes);
        gap.push_back(block);
    }
    Check(UsedMiB() >= gap_start + 8, "allocations above the largest size class are not page-backed");
    for (uint32_t block : gap) HeapFree(block);
    Check(UsedMiB() == gap_start, "a page-backed block did not return its pages");

    puts("Guest size-class sweep, alignment, large-page release, and small-block reuse checks passed");
    return 0;
}
