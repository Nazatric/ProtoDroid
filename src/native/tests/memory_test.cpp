// Guest allocator lifetime checks used by the static-texture scratch-release policy.
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <sys/mman.h>
#include <unistd.h>

#include "../guest.h"

namespace native {

std::string ArenaUsage();
// imports.cpp owns this in the real build; the allocator records it with every event.
uint32_t CurrentImportCaller() { return 0; }
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

constexpr uint32_t kPageBytes = 0x10000u;

bool PageReachable(uint32_t guest) {
    // Decommitted pages are mapped PROT_NONE, so they hold no resident page: mincore says so without touching them.
    const uint32_t page = uint32_t(::sysconf(_SC_PAGESIZE));
    unsigned char resident[64] = {};
    void* p = reinterpret_cast<void*>(static_cast<uintptr_t>(native::kGuestBase + guest));
    if (::mincore(p, kPageBytes, resident) != 0) return false;
    for (uint32_t i = 0; i < kPageBytes / page && i < sizeof(resident); ++i) {
        if (resident[i] & 1) return true;
    }
    return false;
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

    // VirtualFree(MEM_DECOMMIT) keeps the reservation but drops the backing: the next commit over it must read as
    // zero. A commit that handed back stale bytes (the old behaviour) gave the guest a garbage "fresh" structure.
    constexpr uint32_t kPoolBytes = 3u << 16;  // three pages
    const uint64_t arena_before_pool = UsedMiB();
    const uint32_t pool = PageAlloc(kPoolBytes);
    Check(pool != 0, "decommit pool allocation failed");
    memset(Ptr<void>(pool), 0xa5, kPoolBytes);
    Check(PageDecommit(pool, 0), "decommit of an allocation base was refused");
    Check(PageReachable(pool) == false, "decommitted page is still backed");

    {
        // Reading a decommitted page would fault on Windows; the test process must not fault, so the check is on the
        // mapping state and on the re-commit, which is what the engine relies on.
        char description[256];
        DescribeGuestAddress(pool, description, sizeof(description));
        Check(strstr(description, "decommitted") != nullptr, "decommitted page was not described as decommitted");
    }

    const uint32_t recommitted = PageAlloc(kPoolBytes, pool);
    Check(recommitted == pool, "commit of the decommitted base returned another address");
    for (uint32_t i = 0; i < kPoolBytes; ++i) {
        Check(R8(pool + i) == 0, "re-committed page was not zeroed");
    }
    Check(PageDecommit(pool, 0), "second decommit was refused");
    // Committing an interior range backs only that range.
    Check(PageAlloc(kPageBytes, pool + kPageBytes) == pool + kPageBytes, "interior commit returned another address");
    for (uint32_t i = 0; i < kPageBytes; ++i) Check(R8(pool + kPageBytes + i) == 0, "interior commit was not zeroed");
    PageFree(pool);
    Check(UsedMiB() == arena_before_pool, "decommit pool release did not free its pages");

    // A request that cannot fit the 2 GiB window fails cleanly instead of wrapping into a small allocation.
    Check(HeapAlloc(0xffffffffu) == 0, "an impossible allocation did not fail");
    Check(HeapAlloc(0xffffff00u) == 0, "an impossible allocation did not fail");
    Check(PageAlloc(0xfffffff0u) == 0, "an impossible page allocation did not fail");
    Check(UsedMiB() == arena_before_pool, "a failed allocation still reserved pages");

    // The crash report's address description separates a freed range from a live allocation.
    const uint32_t probe = HeapAlloc(64);
    char description[256];
    DescribeGuestAddress(probe, description, sizeof(description));
    Check(strstr(description, "committed allocation") != nullptr, "live allocation was not described as committed");
    HeapFree(probe);
    DescribeGuestAddress(probe, description, sizeof(description));
    Check(strstr(description, "freed by guest") != nullptr, "freed heap block was not described as freed");
    // A freed *page* is described as released-but-still-mapped: that is the behaviour Windows has, and the report
    // says so instead of claiming the mapping is at fault.
    const uint32_t page_probe = PageAlloc(kPageBytes);
    Check(page_probe != 0, "page probe allocation failed");
    PageFree(page_probe);
    DescribeGuestAddress(page_probe, description, sizeof(description));
    Check(strstr(description, "released block") != nullptr, "freed page range was not described as released");
    Check(GuestRangeReadable(page_probe, kPageBytes), "a quarantined page was reported unreadable");

    // The history ring records the operations most recent first, so the crash report can replay a block's lifetime.
    native::MemEvent event{};
    Check(MemoryHistoryAt(0, &event), "allocator history is empty");
    Check(MemoryHistoryAt(0, &event) && event.kind == 'p', "the newest event is not the page free just made");
    Check(MemoryHistoryAt(1, &event) && event.kind == 'P', "the page allocation before it is missing");
    Check(MemoryHistoryAt(2, &event) && event.kind == 'h', "the heap free before that is missing");
    Check(MemoryHistoryAt(3, &event) && event.kind == 'H', "the heap allocation before that is missing");
    Check(!MemoryHistoryAt(MemoryHistoryCount(), &event), "history returned an event past the end");

    // The crash report only dereferences guest pointers it has cleared through this check.
    Check(GuestRangeReadable(probe, 64), "a live allocation was not reported readable");
    Check(!GuestRangeReadable(0, 16), "the null address was reported readable");

    // Never-allocated arena space is genuinely unbacked (the window is reserved PROT_NONE there): a fault there is a
    // real mapping fault, and the report must say so.
    const uint32_t never = 0x20000000u;  // 512 MiB: inside the arena, no allocation made by this test
    Check(!GuestRangeReadable(never, kPageBytes), "never-allocated arena space was reported readable");

    // A released guest block stays mapped for diagnosis and for stale writes (Windows keeps freed heap memory
    // committed), so it reads as acceptable; a decommitted page does not, because Windows faults there too.
    const uint32_t released = PageAlloc(kPageBytes);
    Check(released != 0, "release probe allocation failed");
    PageFree(released);
    Check(GuestRangeReadable(released, kPageBytes), "a released block left mapped was reported unreadable");
    const uint32_t committed = PageAlloc(kPageBytes);
    Check(PageDecommit(committed, 0), "probe decommit was refused");
    Check(!GuestRangeReadable(committed, kPageBytes), "a decommitted page was reported readable");
    PageFree(committed);

    // VirtualAlloc(MEM_RESERVE) then a plain access: Windows makes reserved memory readable, so a reservation that
    // this port left unmapped was a fault with no Windows equivalent. The range must also stay out of the arena, so a
    // later allocation cannot be handed the same pages.
    constexpr uint32_t kReserveBytes = 4u << 16;
    const uint32_t reserved = PageReserve(kReserveBytes);
    Check(reserved != 0, "reservation failed");
    Check(GuestRangeReadable(reserved, kReserveBytes), "reserved memory was not readable");
    memset(Ptr<void>(reserved), 0x5a, kReserveBytes);  // a commit-less reservation is usable on Windows
    const uint32_t elsewhere = PageAlloc(kReserveBytes);
    Check(elsewhere != reserved && (elsewhere + kReserveBytes <= reserved || elsewhere >= reserved + kReserveBytes),
          "a new allocation overlapped a live reservation");
    // A commit over the reservation adopts it instead of failing (the guest still owns the address).
    Check(PageAlloc(kReserveBytes, reserved) == reserved, "commit of a reserved range failed");
    Check(PageFree(reserved), "release of the committed range failed");
    PageFree(elsewhere);

    puts("Guest large-page release, small-block reuse, decommit, reservation, readability and re-commit checks passed");
    return 0;
}
