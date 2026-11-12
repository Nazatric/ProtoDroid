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

#include <algorithm>
#include <cerrno>
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
constexpr uint32_t kPage = 0x10000u;     // VirtualAlloc allocation granularity (guest reserve requests)
constexpr uint32_t kHostPage = 0x1000u;  // internal heap blocks: commit/return at the host page, wasting <= 4 KiB

std::mutex g_mutex;
std::map<uint32_t, uint32_t> g_free;  // start -> size, page ranges available
std::map<uint32_t, uint32_t> g_used;  // start -> size, allocated page ranges

uint32_t RoundUp(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

// Makes [a, a + size) of the guest window usable, or (unmap) inaccessible and empty again.
bool MapGuest(uint32_t a, uint32_t size) {
    return mmap(Ptr<void>(a), size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED,
                -1, 0) != MAP_FAILED;
}
void UnmapGuest(uint32_t a, uint32_t size) {
    mmap(Ptr<void>(a), size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0);
}

// The arena (g_free) is mapped page range by page range as the guest allocates it. `gran` is the allocation unit: the
// guest's VirtualAlloc reserve uses its 64 KiB granularity, the internal heap uses the 4 KiB host page (a 70 KB block
// then costs 72 KB rather than 128 KB).
uint32_t PageAllocLocked(uint32_t size, uint32_t at, uint32_t gran = kPage) {
    size = RoundUp(size ? size : 1, gran);
    if (at) {
        at &= ~(kPage - 1);
        // Committing inside an existing reservation returns it unchanged.
        auto u = g_used.upper_bound(at);
        if (u != g_used.begin()) {
            --u;
            if (at >= u->first && at < u->first + u->second) {
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
constexpr uint32_t kMaxClassSize = 0x10000u;  // largest size class; anything above is page-backed
constexpr uint32_t kChunk = 1u << 20;
uint32_t g_class_size[kClasses];
uint32_t g_free_list[kClasses];
uint32_t g_chunk_ptr = 0, g_chunk_end = 0;

void BuildClassLookup();

void InitClasses() {
    int n = 0;
    for (uint32_t s = 32; s <= 512; s += 32) g_class_size[n++] = s;  // 16 classes
    // Growth is 1.5x, and every class size is rounded up to 16 bytes: a block's user pointer is block + sizeof(Header),
    // so an odd class size (the sequence reaches 13122, 19683, 29524, 44286) handed the engine 2-byte-aligned storage
    // for the 16-byte structures it allocates with malloc/HeapAlloc. The header itself then sat at a misaligned address,
    // which is undefined behaviour in C++ and an unaligned 16-byte store on ARM64.
    for (uint32_t s = 768; s <= 65536 && n < kClasses; s += s / 2) g_class_size[n++] = RoundUp(s, 16);
    while (n < kClasses) g_class_size[n++] = 0xffffffffu;
    BuildClassLookup();
}

// Size -> class in one array read: every size up to the largest class maps to its class index, and anything above it -
// including a request that falls in a gap above the last class - maps to kNoClass and is served from pages. The old
// linear scan over 40 entries ran on every malloc/new in the process.
//
// The sentinel matters: the lookup table is zero-initialised, so with a plain "0 means class 0" a request between the
// largest class and kMaxClassSize was served from the 32-byte class. Every guest malloc, HeapAlloc and D3D9 surface
// in that range (44,289..65,536 bytes, which includes the engine's DXT decode scratch and many asset buffers) then got a
// 32-byte block and overwrote the heap around it. tools/test_perf_program.py's guest allocator test sweeps the whole
// range now.
constexpr uint8_t kNoClass = 0xff;
uint8_t g_class_lookup[kMaxClassSize + 1];

void BuildClassLookup() {
    memset(g_class_lookup, kNoClass, sizeof(g_class_lookup));
    uint32_t size = 0;
    for (int i = 0; i < kClasses && g_class_size[i] != 0xffffffffu; ++i) {
        for (; size <= g_class_size[i] && size <= kMaxClassSize; ++size) g_class_lookup[size] = static_cast<uint8_t>(i);
    }
}

inline int ClassFor(uint32_t total) {
    if (total > kMaxClassSize) return -1;
    const uint8_t cls = g_class_lookup[total];
    return cls == kNoClass ? -1 : static_cast<int>(cls);
}

// The free-list link of a block lives in the first bytes of its user area, so it costs no header space.
inline uint32_t ReadLink(uint32_t block) {
    uint32_t next;
    memcpy(&next, Ptr<uint8_t>(block + sizeof(Header)), sizeof(next));
    return next;
}
inline void WriteLink(uint32_t block, uint32_t next) {
    memcpy(Ptr<uint8_t>(block + sizeof(Header)), &next, sizeof(next));
}

// ---- per-thread magazines ----
// The guest heap used to take one process-wide mutex for every malloc/new/HeapAlloc, so the engine's workers, loader,
// audio and render threads serialised on every small object and bounced that lock's cache line between clusters of a
// big.LITTLE phone. Each thread now keeps a private stack of free blocks per size class: the common path is a load and
// a store with no lock, and the shared lock is only taken to refill or return a batch. Blocks stay interchangeable
// between threads, so a block allocated on one thread and freed on another needs no ownership bookkeeping.
constexpr int kMagazineBlocks = 16;  // per class, per thread (x40 classes x4 B = 2.5 KiB of TLS)
constexpr int kMagazineBatch = 8;    // blocks moved per lock acquisition

struct Magazines {
    uint32_t block[kClasses][kMagazineBlocks];
    uint8_t count[kClasses];
    ~Magazines();  // returns whatever the exiting thread still holds, so thread churn cannot strand blocks
};
thread_local Magazines tl_magazines;
bool g_heap_ready = false;

// Fills `m` for class `cls` with up to kMagazineBatch blocks. The caller holds g_mutex.
void RefillLocked(Magazines& m, int cls) {
    int n = 0;
    while (n < kMagazineBatch && g_free_list[cls]) {
        const uint32_t block = g_free_list[cls];
        g_free_list[cls] = ReadLink(block);
        m.block[cls][n++] = block;
    }
    const uint32_t csize = g_class_size[cls];
    while (n < kMagazineBatch) {
        if (g_chunk_ptr + csize > g_chunk_end) {
            // Nothing left in the current chunk: hand its tail to the largest class that still fits rather than
            // abandoning it (the old code dropped up to a megabyte of a chunk on every switch).
            while (g_chunk_end > g_chunk_ptr && g_chunk_end - g_chunk_ptr >= g_class_size[0]) {
                const uint32_t remaining = g_chunk_end - g_chunk_ptr;
                int fit = -1;
                for (int i = 0; i < kClasses && g_class_size[i] != 0xffffffffu; ++i) {
                    if (g_class_size[i] <= remaining) fit = i;
                }
                if (fit < 0) break;
                const uint32_t tail = g_chunk_ptr;
                g_chunk_ptr += g_class_size[fit];
                WriteLink(tail, g_free_list[fit]);
                g_free_list[fit] = tail;
            }
            const uint32_t bytes = csize > kChunk ? RoundUp(csize, kHostPage) : kChunk;
            const uint32_t fresh = PageAllocLocked(bytes, 0, kHostPage);
            if (!fresh) break;
            g_chunk_ptr = fresh;
            g_chunk_end = fresh + bytes;
        }
        m.block[cls][n++] = g_chunk_ptr;
        g_chunk_ptr += csize;
    }
    m.count[cls] = static_cast<uint8_t>(n);
}

Magazines::~Magazines() {
    if (!g_heap_ready) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    for (int cls = 0; cls < kClasses; ++cls) {
        for (int i = 0; i < count[cls]; ++i) {
            const uint32_t b = block[cls][i];
            WriteLink(b, g_free_list[cls]);
            g_free_list[cls] = b;
        }
        count[cls] = 0;
    }
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
    g_heap_ready = true;
}

uint32_t PageAlloc(uint32_t size, uint32_t at) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return PageAllocLocked(size, at);
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

void PageFree(uint32_t addr) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto u = g_used.find(addr);
    if (u == g_used.end()) {
        return;
    }
    uint32_t a = u->first, size = u->second;
    g_used.erase(u);
    UnmapGuest(a, size);  // drops the pages: zero on reuse, as VirtualAlloc guarantees
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

uint32_t HeapAlloc(uint32_t size) {
    const uint32_t total = size + sizeof(Header);
    const int cls = ClassFor(total);
    if (cls < 0) {
        // Large block: a page range of its own. Frees still unmap it (VirtualAlloc guarantees zeroed memory on reuse),
        // but the range is committed at 4 KiB granularity instead of 64 KiB.
        std::lock_guard<std::mutex> lock(g_mutex);
        const uint32_t block = PageAllocLocked(total, 0, kHostPage);
        if (!block) return 0;
        *Ptr<Header>(block) = {kLarge, size, kMagic, 0};
        return block + sizeof(Header);
    }
    Magazines& m = tl_magazines;
    if (m.count[cls] == 0) {
        std::lock_guard<std::mutex> lock(g_mutex);
        RefillLocked(m, cls);
        if (m.count[cls] == 0) return 0;
    }
    const uint32_t block = m.block[cls][--m.count[cls]];
    *Ptr<Header>(block) = {uint32_t(cls), size, kMagic, 0};
    return block + sizeof(Header);
}

void HeapFree(uint32_t addr) {
    if (!addr) return;
    const uint32_t block = addr - sizeof(Header);
    Header* h = Ptr<Header>(block);
    if (h->magic != kMagic) {
        Log("heap: free of a foreign block %08x ignored", addr);
        return;
    }
    const uint32_t cls = h->cls;
    h->magic = 0;
    if (cls == kLarge) {
        PageFree(block);
        return;
    }
    if (cls >= uint32_t(kClasses)) {
        std::lock_guard<std::mutex> lock(g_mutex);
        WriteLink(block, g_free_list[cls]);
        g_free_list[cls] = block;
        return;
    }
    Magazines& m = tl_magazines;
    if (m.count[cls] >= kMagazineBlocks) {
        // Magazine full: return half of it so the number of cached blocks per class stays bounded.
        std::lock_guard<std::mutex> lock(g_mutex);
        const int keep = kMagazineBlocks / 2;
        for (int i = keep; i < m.count[cls]; ++i) {
            const uint32_t b = m.block[cls][i];
            WriteLink(b, g_free_list[cls]);
            g_free_list[cls] = b;
        }
        m.count[cls] = static_cast<uint8_t>(keep);
    }
    m.block[cls][m.count[cls]++] = block;
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
    Header* h = Ptr<Header>(addr - sizeof(Header));
    if (h->cls != kLarge && size + sizeof(Header) <= g_class_size[h->cls]) {
        h->size = size;
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

}  // namespace native
