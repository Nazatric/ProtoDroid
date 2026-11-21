// Guest address space: a private 4 GiB window of the host address space at kGuestBase (guest.h), reserved
// inaccessible at startup.
//
//   import thunks: an arena block (imports.cpp)
//   kEngineBase (guest.h) engine image, rebased; kept out of the arena
//   0x01000000 .. 0x80000000  arena: VirtualAlloc pages, guest heap, stacks, TEBs (below 2 GiB: the game is not
//                             large-address aware)
//
// Guest allocations make their pages read/write (MAP_NORESERVE: the kernel commits pages on first touch, close
// enough to VirtualAlloc's reserve/commit) and frees make them inaccessible again. The window keeps the guest out of
// the low 4 GiB, where Android's Java heap (up to 3+ GiB on some phones) and Qualcomm's GPU driver live.
#include <sys/mman.h>
#include <unistd.h>

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <map>
#include <mutex>

#include "guest.h"

namespace native {

namespace {

constexpr uint32_t kArenaLo = 0x01000000u;
constexpr uint32_t kArenaHi = 0x80000000u;
constexpr uint64_t kWindow = 1ull << 32;
constexpr uint32_t kPage = 0x10000u;  // VirtualAlloc allocation granularity

std::mutex g_mutex;
std::map<uint32_t, uint32_t> g_free;  // start -> size, page ranges available
std::map<uint32_t, uint32_t> g_used;  // start -> size, allocated page ranges
std::map<uint32_t, uint32_t> g_decommitted;  // start -> size, committed-but-unbacked sub-ranges inside g_used

// A page-rounded size, or 0 when rounding would wrap. Callers treat 0 as "cannot allocate": a request that overflows
// 32 bits must fail like it does on Windows, not become a small allocation that the caller then overruns.
uint32_t RoundUp(uint32_t v, uint32_t a) {
    uint64_t r = (uint64_t(v) + a - 1) & ~uint64_t(a - 1);
    return r > 0xffffffffull ? 0u : uint32_t(r);
}

// Window-sized request ceiling: the arena is below 2 GiB because the game is not large-address aware, so a single
// allocation larger than the window cannot succeed. Rejecting it here keeps HeapAlloc's size+header arithmetic and the
// page rounding inside 32 bits (a wrapped size used to hand back a tiny block for an enormous request, which then
// overflowed the guest heap and corrupted whatever the guest stored after it).
constexpr uint32_t kMaxRequest = kArenaHi - kArenaLo - kPage;

// ---- allocator history (fault diagnosis) ----
// Every page/heap operation is recorded in a fixed ring with its guest caller, so a crash handler can say what the
// faulting address used to be: a block that was freed three seconds ago, memory that was never committed, or a
// committed block (which makes the fault the *writer's* problem, not the mapping's).
enum : char { kPageAllocEvent = 'P', kPageFreeEvent = 'p', kPageDecommitEvent = 'D', kHeapAllocEvent = 'H',
              kHeapFreeEvent = 'h', kHeapReallocEvent = 'R' };
constexpr int kHistory = 512;
MemEvent g_history[kHistory];
std::atomic<uint32_t> g_history_next{0};

uint64_t NowNs() {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count());
}

void RecordEvent(char kind, uint32_t addr, uint32_t size) {
    const uint32_t slot = g_history_next.fetch_add(1, std::memory_order_relaxed) % kHistory;
    MemEvent& e = g_history[slot];
    e.ns = NowNs();
    e.addr = addr;
    e.size = size;
    e.caller = CurrentImportCaller();
    e.tid = uint32_t(gettid());
    e.kind = kind;
}

// Makes [a, a + size) of the guest window usable, or (unmap) inaccessible and empty again.
bool MapGuest(uint32_t a, uint32_t size) {
    return mmap(Ptr<void>(a), size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED,
                -1, 0) != MAP_FAILED;
}
void UnmapGuest(uint32_t a, uint32_t size) {
    mmap(Ptr<void>(a), size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0);
}

void CommitDecommittedLocked(uint32_t lo, uint64_t hi);
int ClassFor(uint32_t total);
uint32_t HeapAllocLocked(uint32_t size);

// The arena (g_free) is mapped page range by page range as the guest allocates it.
uint32_t PageAllocLocked(uint32_t size, uint32_t at) {
    if (size > kMaxRequest) return 0;
    size = RoundUp(size ? size : 1, kPage);
    if (!size) return 0;
    if (at) {
        at &= ~(kPage - 1);
        // Committing inside an existing reservation returns it unchanged.
        auto u = g_used.upper_bound(at);
        if (u != g_used.begin()) {
            --u;
            if (at >= u->first && at < u->first + u->second) {
                // Committing inside a reservation (or re-committing after a decommit): every decommitted byte the
                // commit covers is backed by fresh zero pages again, which is what VirtualAlloc promises.
                CommitDecommittedLocked(at, uint64_t(at) + size);
                return at;
            }
        }
        auto f = g_free.upper_bound(at);
        if (f == g_free.begin()) {
            return 0;
        }
        --f;
        uint32_t fs = f->first, fsz = f->second;
        if (at < fs || uint64_t(at) + size > uint64_t(fs) + fsz) {
            return 0;
        }
        if (!MapGuest(at, size)) return 0;
        g_free.erase(f);
        if (at > fs) {
            g_free[fs] = at - fs;
        }
        if (at + size < fs + fsz) {
            g_free[at + size] = fs + fsz - (at + size);
        }
        g_used[at] = size;
        return at;
    }
    for (auto f = g_free.begin(); f != g_free.end(); ++f) {
        if (f->second < size) continue;
        uint32_t a = f->first, rest = f->second - size;
        if (!MapGuest(a, size)) return 0;
        g_free.erase(f);
        if (rest) {
            g_free[a + size] = rest;
        }
        g_used[a] = size;
        return a;
    }
    return 0;
}

// ---- heap: size classes carved from page chunks, large blocks straight from pages ----
// Block header (16 bytes, keeps 16-byte alignment): size class index or ~0 for large, user size.
struct Header {
    uint32_t cls;
    uint32_t size;
    uint32_t magic;
    uint32_t pad;
};
constexpr uint32_t kMagic = 0x48454150u;  // "HEAP"
constexpr uint32_t kLarge = 0xffffffffu;
constexpr int kClasses = 40;
uint32_t g_class_size[kClasses];
uint32_t g_free_list[kClasses];
uint32_t g_chunk_ptr = 0, g_chunk_end = 0;

void InitClasses() {
    int n = 0;
    for (uint32_t s = 32; s <= 512; s += 32) g_class_size[n++] = s;       // 16 classes
    for (uint32_t s = 768; s <= 65536 && n < kClasses; s += s / 2) g_class_size[n++] = s;
    while (n < kClasses) g_class_size[n++] = 0xffffffffu;
}

int ClassFor(uint32_t total) {
    for (int i = 0; i < kClasses; ++i) {
        if (g_class_size[i] != 0xffffffffu && total <= g_class_size[i]) return i;
    }
    return -1;
}

}  // namespace

void InitGuestMemory() {
    void* want = reinterpret_cast<void*>(static_cast<uintptr_t>(kGuestBase));
    void* p = mmap(want, kWindow, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (p != want) {
        Fatal("cannot reserve the guest address space at %llx (%s)", (unsigned long long)kGuestBase,
              p == MAP_FAILED ? strerror(errno) : "taken");
    }
    g_free[kArenaLo] = kEngineBase - kArenaLo;
    g_free[kEngineBase + kEngineReserve] = kArenaHi - (kEngineBase + kEngineReserve);
    Log("memory: guest window at %llx, arena %u MiB", (unsigned long long)kGuestBase,
        (kArenaHi - kArenaLo - kEngineReserve) >> 20);
    InitClasses();
}

uint32_t PageAlloc(uint32_t size, uint32_t at) {
    uint32_t r;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        r = PageAllocLocked(size, at);
    }
    if (r) RecordEvent(kPageAllocEvent, r, size);
    return r;
}

// Address space the guest holds (reserved or committed) and the highest address in use, for the perf line.
std::string ArenaUsage() {
    std::lock_guard<std::mutex> lock(g_mutex);
    uint64_t used = 0;
    uint32_t top = 0;
    for (const auto& u : g_used) {
        used += u.second;
        top = std::max(top, u.first + u.second);
    }
    static uint64_t peak = 0;
    peak = std::max(peak, used);
    char line[96];
    snprintf(line, sizeof(line), "%llu MiB (peak %llu), up to %08x", (unsigned long long)(used >> 20),
             (unsigned long long)(peak >> 20), top);
    return line;
}

namespace {

// Within an existing reservation, re-backs every decommitted byte of [lo, hi) and forgets those sub-ranges.
void CommitDecommittedLocked(uint32_t lo, uint64_t hi) {
    auto d = g_decommitted.upper_bound(lo);
    if (d != g_decommitted.begin()) --d;  // the range that starts at or before `lo` may still cover it
    for (; d != g_decommitted.end();) {
        if (d->first >= hi) break;
        const uint32_t ds = d->first, de = d->first + d->second;
        const uint32_t from = std::max(lo, ds), to = uint32_t(std::min<uint64_t>(hi, de));
        if (to > from) {
            // A fresh mapping drops whatever the pages held; only the committed part is touched.
            (void)MapGuest(from, to - from);
            const uint32_t keep_left = ds, keep_right = de;
            d = g_decommitted.erase(d);
            if (from > keep_left) g_decommitted[keep_left] = from - keep_left;
            if (keep_right > to) g_decommitted[to] = keep_right - to;
        } else {
            ++d;
        }
    }
}

// Drops [a, a + size) back to the reservation: inaccessible now, zero on the next commit (VirtualAlloc's guarantee).
void ReleaseLocked(uint32_t a, uint32_t size) {
    UnmapGuest(a, size);
    auto next = g_free.find(a + size);
    if (next != g_free.end()) {
        size += next->second;
        g_free.erase(next);
    }
    auto prev = g_free.lower_bound(a);
    if (prev != g_free.begin()) {
        --prev;
        if (prev->first + prev->second == a) {
            prev->second += size;
            return;
        }
    }
    g_free[a] = size;
}

}  // namespace

void PageFree(uint32_t addr) {
    uint32_t size = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto u = g_used.find(addr);
        if (u == g_used.end()) {
            return;
        }
        size = u->second;
        g_used.erase(u);
        for (auto d = g_decommitted.lower_bound(addr); d != g_decommitted.end();) {
            if (d->first >= addr + size) break;
            d = g_decommitted.erase(d);
        }
        ReleaseLocked(addr, size);
    }
    RecordEvent(kPageFreeEvent, addr, size);
}

// VirtualFree(MEM_DECOMMIT): the reservation stays, the pages stop being backed, and the next commit over them reads
// as zero - exactly as on Windows. Leaving the pages mapped (the old behaviour) meant a commit-after-decommit handed
// the guest stale bytes where Windows guarantees zeros, and a stale pointer inside a "freshly committed" structure is
// a crash waiting for the moment the engine uses it. Windows requires the allocation base for a decommit.
bool PageDecommit(uint32_t addr, uint32_t size) {
    bool found = false;
    uint32_t released = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto u = g_used.find(addr);
        if (u != g_used.end()) {
            found = true;
            released = (!size || size >= u->second) ? u->second : (size & ~(kPage - 1));
            if (released) {
                UnmapGuest(addr, released);
                g_decommitted[addr] = released;
            }
        }
    }
    if (released) RecordEvent(kPageDecommitEvent, addr, released);
    return found;
}


namespace {

// Takes g_mutex.
uint32_t HeapAllocLocked(uint32_t size) {
    // A request that cannot fit the arena fails, as it does on Windows; it must not wrap the size arithmetic below
    // into a small class and return a block the caller then overruns.
    if (size > kMaxRequest) return 0;
    uint32_t total = size + sizeof(Header);
    int cls = ClassFor(total);
    uint32_t block;
    if (cls < 0) {
        block = PageAllocLocked(total, 0);
        if (!block) return 0;
        *Ptr<Header>(block) = {kLarge, size, kMagic, 0};
        return block + sizeof(Header);
    }
    if (g_free_list[cls]) {
        block = g_free_list[cls];
        g_free_list[cls] = R32(block + sizeof(Header));
    } else {
        uint32_t csize = g_class_size[cls];
        if (g_chunk_ptr + csize > g_chunk_end) {
            g_chunk_ptr = PageAllocLocked(1 << 20, 0);
            if (!g_chunk_ptr) return 0;
            g_chunk_end = g_chunk_ptr + (1 << 20);
        }
        block = g_chunk_ptr;
        g_chunk_ptr += csize;
    }
    *Ptr<Header>(block) = {uint32_t(cls), size, kMagic, 0};
    return block + sizeof(Header);
}

}  // namespace

uint32_t HeapAlloc(uint32_t size) {
    uint32_t user = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        user = HeapAllocLocked(size);
    }
    if (user) RecordEvent(kHeapAllocEvent, user, size);
    return user;
}

void HeapFree(uint32_t addr) {
    if (!addr) return;
    uint32_t block = addr - sizeof(Header);
    Header* h = Ptr<Header>(block);
    if (h->magic != kMagic) {
        Log("heap: free of a foreign block %08x ignored", addr);
        return;
    }
    const uint32_t size = h->size;
    if (h->cls == kLarge) {
        h->magic = 0;
        PageFree(block);
        RecordEvent(kHeapFreeEvent, addr, size);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        h->magic = 0;
        W32(addr, g_free_list[h->cls]);
        g_free_list[h->cls] = block;
    }
    RecordEvent(kHeapFreeEvent, addr, size);
}

uint32_t HeapSize(uint32_t addr) {
    Header* h = Ptr<Header>(addr - sizeof(Header));
    return h->magic == kMagic ? h->size : 0xffffffffu;
}

uint32_t HeapRealloc(uint32_t addr, uint32_t size) {
    if (!addr) return HeapAlloc(size);
    if (!size) {
        HeapFree(addr);
        return 0;
    }
    if (size > kMaxRequest) return 0;
    Header* h = Ptr<Header>(addr - sizeof(Header));
    if (h->magic != kMagic) {
        // Not one of our blocks: realloc's contract is "no change and no free", never a silent corruption.
        Log("heap: realloc of a foreign block %08x ignored", addr);
        return 0;
    }
    if (h->cls != kLarge && size + sizeof(Header) <= g_class_size[h->cls]) {
        h->size = size;
        RecordEvent(kHeapReallocEvent, addr, size);
        return addr;
    }
    uint32_t n = HeapAlloc(size);
    if (n) {
        memcpy(Ptr<void>(n), Ptr<void>(addr), h->size < size ? h->size : size);
        HeapFree(addr);
    }
    return n;
}

uint32_t GuestStrdup(const char* s) {
    uint32_t n = static_cast<uint32_t>(strlen(s)) + 1;
    uint32_t a = HeapAlloc(n);
    memcpy(Ptr<void>(a), s, n);
    return a;
}


// Whether [a, a + size) is committed guest memory that can be read without faulting. Used by the crash report before
// it touches a guest pointer it does not trust.
bool GuestRangeReadable(uint32_t a, uint32_t size) {
    if (!a || !size) return false;
    const uint64_t end = uint64_t(a) + size;
    std::lock_guard<std::mutex> lock(g_mutex);
    auto u = g_used.upper_bound(a);
    if (u == g_used.begin()) return false;
    --u;
    if (end > uint64_t(u->first) + u->second) return false;
    auto d = g_decommitted.upper_bound(a);
    if (d != g_decommitted.begin()) {
        --d;
        if (end > d->first && a < uint64_t(d->first) + d->second) return false;
    }
    return true;
}

// ---- fault and allocator history (crash handler) ----
int MemoryHistoryCount() { return kHistory; }

bool MemoryHistoryAt(int index, MemEvent* out) {
    const uint32_t next = g_history_next.load(std::memory_order_relaxed);
    const uint32_t available = next < uint32_t(kHistory) ? next : uint32_t(kHistory);
    if (index < 0 || uint32_t(index) >= available) return false;
    const uint32_t slot = (next - 1u - uint32_t(index)) % kHistory;
    *out = g_history[slot];
    return true;
}

// Names what a guest address is, for a crash report. Two facts matter: what the *page* is (a committed allocation, a
// decommitted hole, an unbacked free range, or nothing in the arena) and, inside a committed page, whether the *block*
// at that address was freed without ever being handed out again - a use-after-free in the guest, which is the usual
// reason a store lands on memory the mapping itself would have accepted. The allocator lock is taken with try_lock:
// the crash being described may have happened while some thread held it, and a report is worth more than precision.
void DescribeGuestAddress(uint32_t a, char* out, size_t size) {
    if (!out || !size) return;
    if (!a) {
        snprintf(out, size, "null address");
        return;
    }
    const MemEvent* newest = nullptr;
    {
        const uint32_t next = g_history_next.load(std::memory_order_relaxed);
        const uint32_t available = next < uint32_t(kHistory) ? next : uint32_t(kHistory);
        for (uint32_t i = 0; i < available; ++i) {
            const MemEvent& e = g_history[(next - 1u - i) % kHistory];
            if (e.kind == kPageFreeEvent) continue;  // the page state below covers this
            if (a >= e.addr && uint64_t(a) < uint64_t(e.addr) + e.size) {
                newest = &e;
                break;
            }
        }
    }
    char block[128] = "";
    if (newest) {
        switch (newest->kind) {
            case kHeapFreeEvent:
                snprintf(block, sizeof(block), "; that heap block was freed by guest %08x on tid %u and not allocated "
                                                "again (use after free)", newest->caller, newest->tid);
                break;
            case kPageDecommitEvent:
                snprintf(block, sizeof(block), "; the page was decommitted by guest %08x on tid %u",
                         newest->caller, newest->tid);
                break;
            case kHeapAllocEvent:
                snprintf(block, sizeof(block), "; inside a block allocated by guest %08x on tid %u (%u byte(s))",
                         newest->caller, newest->tid, newest->size);
                break;
            default: break;
        }
    }
    const bool locked = g_mutex.try_lock();
    if (locked) {
        auto u = g_used.upper_bound(a);
        if (u != g_used.begin()) {
            --u;
            if (a < u->first + u->second) {
                const uint32_t start = u->first, len = u->second;
                bool decommitted = false;
                auto d = g_decommitted.upper_bound(a);
                if (d != g_decommitted.begin()) {
                    --d;
                    decommitted = a < d->first + d->second;
                }
                if (decommitted) {
                    snprintf(out, size, "decommitted page inside the allocation [%08x,%08x): access faults until the "
                                        "next commit, as on Windows%s", start, start + len, block);
                } else {
                    snprintf(out, size, "committed allocation [%08x,%08x), %u byte(s) from its base%s", start,
                             start + len, a - start, block);
                }
                g_mutex.unlock();
                return;
            }
        }
        auto f = g_free.upper_bound(a);
        if (f != g_free.begin()) {
            --f;
            if (a < f->first + f->second) {
                snprintf(out, size, "unbacked page of the free range [%08x,%08x): released, decommitted or never "
                                    "allocated%s", f->first, f->first + f->second, block);
                g_mutex.unlock();
                return;
            }
        }
        g_mutex.unlock();
    }
    if (a >= kArenaLo && a < kArenaHi) {
        snprintf(out, size, "inside the arena but in no allocation (engine image reservation or allocator busy)%s",
                 block);
        return;
    }
    snprintf(out, size, "outside the arena (engine image, guest stack or host pointer)%s", block);
}

}  // namespace native
