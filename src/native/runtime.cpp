// Host services for the recompiled code (runtime.h) in the native host: dispatch of computed calls, imports,
// setjmp/longjmp, cpuid, traps.
#include <algorithm>
#include <atomic>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <utility>
#include <vector>

#include "diagnostic_tour.h"
#include "diagnostic_tour_probe.h"
#include "guest.h"

extern "C" {
extern const struct RecompTableEntry {
    uint32_t va;
    RecompFn fn;
} g_recomp_table[];
extern const uint32_t g_recomp_count;

volatile uint32_t RecompRestrictLo = 0;
volatile uint32_t RecompRestrictHi = 0xffffffffu;
volatile uint32_t RecompTestDetours = 0;
extern volatile uint32_t RecompDiagnosticTour;
uint8_t* RecompMemoryBase = nullptr;
}

namespace native {

namespace {

constexpr int kHashBits = 18;
struct HashSlot {
    uint32_t va;
    RecompFn fn;
};
// Published, not assigned in place: the table used to be stored into the global before it was filled, so a second
// thread that reached a lookup while the first was still building saw a valid pointer to a half-built table and got a
// null (or a miss) for a function that exists. That is a Fatal "neither recompiled nor an import" on whichever thread
// loses the race. The build is serialised, the result is published with release, and readers take it with acquire.
std::atomic<HashSlot*> g_hash{nullptr};
std::mutex g_hash_build;

uint32_t HashOf(uint32_t va) { return (va * 2654435761u) >> (32 - kHashBits); }

void BuildHash() {
    std::lock_guard<std::mutex> lock(g_hash_build);
    if (g_hash.load(std::memory_order_acquire)) return;  // built while this thread waited
    auto* table = static_cast<HashSlot*>(calloc(size_t(1) << kHashBits, sizeof(HashSlot)));
    if (!table) Fatal("cannot allocate the recompiled-function dispatch hash");
    for (uint32_t i = 0; i < g_recomp_count; ++i) {
        uint32_t h = HashOf(g_recomp_table[i].va);
        while (table[h].va) h = (h + 1) & ((1u << kHashBits) - 1);
        table[h] = {g_recomp_table[i].va, g_recomp_table[i].fn};
    }
    g_hash.store(table, std::memory_order_release);
}

// Item 14: a monomorphic inline cache in front of the hash. C++ virtual calls and function-pointer tables in the engine
// are overwhelmingly monomorphic per call site, so the second and later dispatches to the same target cost one
// predictable load instead of a 4 MiB hash probe (kHashBits 18, ~48% load) that usually misses L2 on a phone.
// The cache entry has to be read atomically with its tag. The first version was a plain shared array of {va, fn}: every
// thread wrote it, so a reader could observe the tag of one target next to the function pointer of another and dispatch
// guest code to the wrong address - a crash far away from the cause, and one that only appears once the engine runs
// several threads. Entries are therefore immutable once published and reached through one atomic pointer per slot:
// the tag and the function pointer always belong to each other, with no lock, no 16-byte atomic (not lock-free on
// pre-LSE2 ARM64) and no per-thread copy (dynamic TLS costs a __tls_get_addr call per dispatch in a dlopen'd library).
constexpr uint32_t kInlineCacheSize = 512;
constexpr uint32_t kInlineRecords = 4096;  // published records; caching stops (correctly, just slower) when used up
struct InlineCacheEntry {
    uint32_t va;
    RecompFn fn;
};
InlineCacheEntry g_inline_records[kInlineRecords];
std::atomic<uint32_t> g_inline_next{0};
std::atomic<const InlineCacheEntry*> g_inline_cache[kInlineCacheSize]{};

inline void PublishInlineCache(uint32_t target, RecompFn fn) {
    const uint32_t index = g_inline_next.fetch_add(1, std::memory_order_relaxed);
    if (index >= kInlineRecords) return;
    g_inline_records[index] = {target, fn};
    g_inline_cache[(target >> 2) & (kInlineCacheSize - 1)].store(&g_inline_records[index], std::memory_order_release);
}

inline const InlineCacheEntry* InlineCacheSlot(uint32_t target) {
    return g_inline_cache[(target >> 2) & (kInlineCacheSize - 1)].load(std::memory_order_acquire);
}

}  // namespace

// Ctx of the guest code that most recently called out to the host on this thread/fiber; host callbacks into guest
// code continue below its stack pointer.
thread_local Ctx* tl_ctx = nullptr;
uint32_t g_engine_lo = 0, g_engine_hi = 0;

}  // namespace native

using namespace native;

// Called once from startup (main.cpp) after the table is linked in. RecompLookup still builds the hash on first use if
// something dispatches earlier, but the hot path no longer runs a std::call_once (an atomic acquire plus a branch) on
// every indirect call.
extern "C" void RecompInit() {
    if (!g_hash.load(std::memory_order_acquire)) BuildHash();
}

extern "C" RecompFn RecompLookup(uint32_t target) {
    if (const InlineCacheEntry* cached = InlineCacheSlot(target)) {
        if (cached->va == target) return cached->fn;
    }
    const HashSlot* hash = g_hash.load(std::memory_order_acquire);
    if (!hash) {
        BuildHash();
        hash = g_hash.load(std::memory_order_acquire);
    }
    for (uint32_t h = HashOf(target);; h = (h + 1) & ((1u << kHashBits) - 1)) {
        if (hash[h].va == target) {
            PublishInlineCache(target, hash[h].fn);
            return hash[h].fn;
        }
        if (!hash[h].va) {
            PublishInlineCache(target, nullptr);
            return nullptr;
        }
    }
}

// Item 4: name a program counter from the recompilation table instead of the dynamic symbol table. The old approach
// needed every recompiled function exported with dladdr() metadata; this needs nothing exported and no allocation.
// Returns "f_<guest va>+0x<offset>" in a thread-local buffer, or null when the address is not inside the engine.
extern "C" const char* RecompNameForAddress(uintptr_t pc) {
    static thread_local char name[64];
    if (!g_recomp_count) return nullptr;
    // Sorted once by host address; only reached while formatting a crash or watchdog report.
    static std::once_flag once;
    static std::vector<std::pair<uintptr_t, uint32_t>> sorted;
    std::call_once(once, [] {
        sorted.reserve(g_recomp_count);
        for (uint32_t i = 0; i < g_recomp_count; ++i)
            sorted.emplace_back(reinterpret_cast<uintptr_t>(g_recomp_table[i].fn), g_recomp_table[i].va);
        std::sort(sorted.begin(), sorted.end());
    });
    auto it = std::upper_bound(sorted.begin(), sorted.end(), std::make_pair(pc, UINT32_MAX),
                               [](const std::pair<uintptr_t, uint32_t>& a,
                                  const std::pair<uintptr_t, uint32_t>& b) { return a.first < b.first; });
    if (it == sorted.begin()) return nullptr;
    --it;
    const uintptr_t offset = pc - it->first;
    if (offset > (1u << 20)) return nullptr;  // not plausibly inside that function
    snprintf(name, sizeof(name), "f_%08x+0x%llx", it->second, (unsigned long long)offset);
    return name;
}

// Thunks are not recompiled (Ghidra marks them as such): `jmp dword ptr [iat]` and `jmp rel32` are followed here.
static uint32_t FollowThunks(uint32_t target) {
    for (int i = 0; i < 8; ++i) {
        if (target < g_engine_lo || target >= g_engine_hi) break;
        if (R8(target) == 0xff && R8(target + 1) == 0x25) {
            target = R32(R32(target + 2));
        } else if (R8(target) == 0xe9) {
            target = target + 5 + R32(target + 1);
        } else {
            break;
        }
    }
    return target;
}

extern "C" void RecompDispatch(Ctx* ctx, uint32_t target) {
    if (RecompDiagnosticTour) {
        // Vtable thunks are recompiled and use RecompDirect on Android, so checking only their final task addresses
        // here misses the normal gameplay route. The cold tour-only classifier recognizes both forms.
        if (const uint32_t task = DiagnosticTourGameplayTaskTarget(target, kEngineBase))
            DiagnosticTourGameplayObserved(task);
    }
    // Import thunks occupy their own address range and cannot have a recompiled entry. Recognize them before the
    // guest-function hash: D3D9/Win32 calls otherwise paid for a known-miss lookup on every crossing.
    if (DispatchImport(ctx, target)) return;
    if (RecompFn fn = RecompLookup(target)) {
        fn(ctx);
        return;
    }
    uint32_t real = FollowThunks(target);
    if (real != target) {
        if (RecompDiagnosticTour) {
            if (const uint32_t task = DiagnosticTourGameplayTaskTarget(real, kEngineBase))
                DiagnosticTourGameplayObserved(task);
        }
        if (RecompFn fn = RecompLookup(real)) {
            fn(ctx);
            return;
        }
        target = real;
        // An engine thunk can resolve to an import even though its original target was outside the import range.
        if (DispatchImport(ctx, target)) return;
    }
    // Preserve the active guest context for crash diagnostics; Fatal does not return.
    tl_ctx = ctx;
    Fatal("call to %08x: neither recompiled nor an import (return address %08x)", target, R32(ctx->esp));
}

extern "C" void RecompCpuid(Ctx* ctx) {
    // A plain SSE2 Pentium 4-class CPU: the engine only probes for SSE/SSE2 and the core count.
    switch (ctx->eax) {
        case 0:
            ctx->eax = 1;
            ctx->ebx = 0x756e6547;  // "GenuineIntel"
            ctx->edx = 0x49656e69;
            ctx->ecx = 0x6c65746e;
            break;
        case 1:
            ctx->eax = 0x00000f29;
            ctx->ebx = 0x00010800;
            ctx->ecx = 0x00000001;   // SSE3
            ctx->edx = 0x078bfbffu;  // FPU..SSE2, HTT clear
            break;
        default:
            ctx->eax = ctx->ebx = ctx->ecx = ctx->edx = 0;
    }
}

extern "C" void RecompUnimplemented(Ctx* ctx, uint32_t va, const char* what) {
    Fatal("trap at %08x (%s), esp=%08x", va, what, ctx->esp);
}

extern "C" int RecompRemoveTickRateLimit() {
    static const int enabled = SettingInt("remove_tick_rate_limit", 0);
    return enabled != 0;
}

extern "C" void RecompBridgeSave(uint8_t state[32]) { memset(state, 0, 32); }
extern "C" void RecompBridgeRestore(const uint8_t[32]) {}

// ---- setjmp/longjmp (same scheme as the in-process bridge: slots keyed by the guest jmp_buf) ----
namespace {

constexpr int kJmpSlots = 64;
struct JmpSlots {
    RecompJmpSlot slots[kJmpSlots];
    int next;
};
thread_local JmpSlots* tl_jmp = nullptr;

RecompJmpSlot* FindSlot(uint32_t guest_buf, bool create) {
    if (!tl_jmp) {
        tl_jmp = static_cast<JmpSlots*>(calloc(1, sizeof(JmpSlots)));
        if (!tl_jmp) return nullptr;
    }
    for (auto& s : tl_jmp->slots) {
        if (s.guest_buf == guest_buf) return &s;
    }
    if (!create) return nullptr;
    RecompJmpSlot* s = &tl_jmp->slots[tl_jmp->next];
    tl_jmp->next = (tl_jmp->next + 1) % kJmpSlots;
    s->guest_buf = guest_buf;
    return s;
}

}  // namespace

extern "C" jmp_buf* RecompSetjmpPrepare(Ctx* ctx, uint32_t guest_buf) {
    RecompJmpSlot* s = FindSlot(guest_buf, true);
    if (!s) Fatal("cannot allocate setjmp state for %08x", guest_buf);
    s->esp = ctx->esp;
    s->ebp = ctx->ebp;
    s->ebx = ctx->ebx;
    s->esi = ctx->esi;
    s->edi = ctx->edi;
    s->st_top = ctx->st_top;
    s->fpu_cw = ctx->fpu_cw;
    s->value = 0;
    return &s->host;
}

extern "C" void RecompSetjmpResume(Ctx* ctx, uint32_t guest_buf) {
    RecompJmpSlot* s = FindSlot(guest_buf, false);
    if (!s) RecompUnimplemented(ctx, guest_buf, "setjmp resume from an unknown jmp_buf");
    ctx->esp = s->esp;
    ctx->ebp = s->ebp;
    ctx->ebx = s->ebx;
    ctx->esi = s->esi;
    ctx->edi = s->edi;
    ctx->st_top = s->st_top;
    ctx->fpu_cw = s->fpu_cw;
    ctx->eax = s->value;
}

extern "C" void RecompLongjmp(Ctx* ctx, uint32_t guest_buf, uint32_t value) {
    RecompJmpSlot* s = FindSlot(guest_buf, false);
    if (!s) RecompUnimplemented(ctx, guest_buf, "longjmp to an unknown jmp_buf");
    s->value = value ? value : 1;
    longjmp(s->host, 1);
}

namespace native {

uint32_t GuestCall(uint32_t va, std::initializer_list<uint32_t> args, uint32_t ecx, uint32_t* edx_out) {
    RecompFn fn = RecompLookup(va);
    if (!fn) Fatal("GuestCall: %08x is not a recompiled function", va);
    GuestThread* t = CurrentThread();
    Ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.esp = (tl_ctx ? tl_ctx->esp - 256 : t->stack_hi - 64) & ~15u;
    ctx.fs_base = t->teb;
    ctx.fpu_cw = 0x027f;
    ctx.ecx = ecx;
    const uint32_t* a = args.end();
    while (a != args.begin()) Push32(&ctx, *--a);
    Push32(&ctx, 0xfffffff0u);  // return address; never jumped to
    Ctx* saved = tl_ctx;
    tl_ctx = nullptr;
    fn(&ctx);
    tl_ctx = saved;
    if (edx_out) *edx_out = ctx.edx;
    return ctx.eax;
}

uint32_t GuestCallNested(Ctx* outer, uint32_t va, std::initializer_list<uint32_t> args, uint32_t ecx) {
    Ctx* saved = tl_ctx;
    tl_ctx = outer;
    const uint32_t result = GuestCall(va, args, ecx);
    tl_ctx = saved;
    return result;
}

uint32_t GuestCallArray(Ctx* outer, uint32_t va, const uint32_t* args, size_t count, uint32_t ecx) {
    RecompFn fn = RecompLookup(va);
    if (!fn || (count && !args)) return 0;
    GuestThread* t = CurrentThread();
    Ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    if (outer) ctx.esp = (outer->esp - 256) & ~15u;
    else ctx.esp = (tl_ctx ? tl_ctx->esp - 256 : t->stack_hi - 64) & ~15u;
    for (size_t i = count; i > 0; --i) Push32(&ctx, args[i - 1]);
    Push32(&ctx, 0xfffffff0u);  // return address; never jumped to
    ctx.fs_base = t->teb;
    ctx.fpu_cw = 0x027f;
    ctx.ecx = ecx;
    Ctx* saved = tl_ctx;
    tl_ctx = nullptr;
    fn(&ctx);
    tl_ctx = saved;
    return ctx.eax;
}

}  // namespace native
