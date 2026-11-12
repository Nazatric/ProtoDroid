// Import thunks: each imported dll!name gets a 16-byte slot at g_thunk_base + 16*index (an arena block: a fixed
// address may be taken on Android, where ART maps its heap below 4 GiB). Generated code calls through
// the IAT with RecompDispatch(target), and target falls in this range. Data imports (MSVCR80 _HUGE, _adjust_fdiv)
// keep their value in the slot itself.
#include <sys/mman.h>

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "guest.h"

namespace native {

// Defined by runtime.cpp. Import handlers can call back into guest code, so the active context must follow the
// import even when dispatch recognizes its thunk before attempting a recompiled-function lookup.
extern thread_local Ctx* tl_ctx;

namespace {

uint32_t g_thunk_base = 0;
constexpr uint32_t kThunkSize = 0x00100000u;
constexpr uint32_t kSlot = 16;

struct Slot {
    std::string dll, name;
    ImportFn fn;
};

std::map<std::string, ImportFn>& Handlers() {
    static std::map<std::string, ImportFn> handlers;
    return handlers;
}

std::vector<Slot>& Slots() {
    static std::vector<Slot> slots;
    return slots;
}

// Item 15: a dense dispatch table parallel to Slots(). A D3D9 call or Win32 import used to walk: range check ->
// function-local-static guard for Slots() -> index into a vector<Slot> whose Slot holds two std::string objects (so the
// lookup touched slot metadata, then a separate cache line for the handler) -> another static guard for the profiler ->
// thread-local save/restore. The entry below is the handler and both names in one 24-byte record, so the hot lookup is
// one indexed load at a fixed stride and touches a single cache line.
struct Entry {
    ImportFn fn;
    const char* dll;
    const char* name;
};
// Registration can happen after the engine has started its threads: GetProcAddress reaches ImportThunk, and COM
// vtables are built lazily. So the table is published, not mutated in place: a new table is filled, the pointer is
// stored, then the count (the reader loads them in that order, see DispatchImport), and the previous table is kept
// rather than freed so a thread that already read the old pointer never touches released memory. The tables are a few
// hundred kilobytes in total and are only grown when the guest resolves a name it has not resolved before.
std::atomic<Entry*> g_entries{nullptr};
std::atomic<uint32_t> g_entry_count{0};
std::vector<Entry*> g_retired;  // previous tables, under g_register_mutex, intentionally never freed
std::mutex g_register_mutex;
std::atomic<bool> g_profiling{false};  // set once at start-up, read on every import call

std::string Key(const char* dll, const char* name) { return std::string(dll) + "!" + name; }

// Setting import_profile: every thread logs, every 10 s, where its wall time went - the imports (WinAPI, D3D9
// methods) it spent the most time in, inclusive of nested calls, and the rest (guest code). Finds the waits that
// keep a frame long while no thread is busy. The flag itself is read directly from g_profiling on the hot path.

struct ThreadProfile {
    using Clock = std::chrono::steady_clock;
    Clock::time_point window = Clock::now();
    std::vector<uint64_t> ns, calls;
    int depth = 0;
};

void ProfiledCall(ImportFn fn, uint32_t index, Ctx* ctx) {
    thread_local ThreadProfile tp;
    if (tp.ns.size() <= index) {
        tp.ns.resize(index + 64);
        tp.calls.resize(index + 64);
    }
    auto t0 = ThreadProfile::Clock::now();
    ++tp.depth;
    fn(ctx);
    --tp.depth;
    auto t1 = ThreadProfile::Clock::now();
    tp.ns[index] += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    ++tp.calls[index];
    if (tp.depth || t1 - tp.window < std::chrono::seconds(10)) return;
    const double wall = std::chrono::duration<double, std::milli>(t1 - tp.window).count();
    std::vector<uint32_t> order;
    for (uint32_t i = 0; i < tp.ns.size(); ++i) {
        if (tp.calls[i]) order.push_back(i);
    }
    std::sort(order.begin(), order.end(), [](uint32_t a, uint32_t b) { return tp.ns[a] > tp.ns[b]; });
    std::string line;
    double in_imports = 0;
    for (uint32_t i : order) in_imports += double(tp.ns[i]) / 1e6;
    for (size_t k = 0; k < order.size() && k < 10; ++k) {
        uint32_t i = order[k];
        char buf[160];
        Entry* entries = g_entries.load(std::memory_order_acquire);
        const char* name = entries && i < g_entry_count.load(std::memory_order_acquire) && entries[i].name
                               ? entries[i].name
                               : "?";
        snprintf(buf, sizeof(buf), " %s %.0f%% (%llu)", name, 100.0 * double(tp.ns[i]) / 1e6 / wall,
                 (unsigned long long)tp.calls[i]);
        line += buf;
    }
    Log("imports tid %d: guest %.0f%% |%s", int(gettid()), 100.0 * (wall - in_imports) / wall, line.c_str());
    std::fill(tp.ns.begin(), tp.ns.end(), 0);
    std::fill(tp.calls.begin(), tp.calls.end(), 0);
    tp.window = t1;
}

}  // namespace

void RegisterImports(const ImportDef* defs, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Handlers()[Key(defs[i].dll, defs[i].name)] = defs[i].fn;
    }
}

void InitImportDispatch() {
    g_profiling.store(SettingInt("import_profile", 0) != 0, std::memory_order_relaxed);
}

uint32_t ImportThunk(const char* dll, const char* name) {
    if (!g_thunk_base) {
        g_thunk_base = PageAlloc(kThunkSize);
        if (!g_thunk_base) Fatal("cannot allocate the import thunk area");
    }
    std::string key = Key(dll, name);
    std::lock_guard<std::mutex> registration(g_register_mutex);
    auto& slots = Slots();
    for (size_t i = 0; i < slots.size(); ++i) {
        if (slots[i].dll == dll && slots[i].name == name) return g_thunk_base + kSlot * uint32_t(i);
    }
    auto h = Handlers().find(key);
    slots.push_back({dll, name, h == Handlers().end() ? nullptr : h->second});
    // Mirror into the dense table (cold path: only when the guest resolves a name it has not asked for before).
    const uint32_t count = uint32_t(slots.size());
    if (count > g_entry_count.load(std::memory_order_relaxed)) {
        auto* grown = static_cast<Entry*>(malloc(size_t(count) * sizeof(Entry)));
        if (!grown) Fatal("cannot grow the import dispatch table");
        Entry* previous = g_entries.load(std::memory_order_relaxed);
        const uint32_t previous_count = g_entry_count.load(std::memory_order_relaxed);
        if (previous) memcpy(grown, previous, size_t(previous_count) * sizeof(Entry));
        // The names are duplicated because the Slot strings live in a container that reallocates.
        grown[count - 1] = {h == Handlers().end() ? nullptr : h->second, strdup(dll), strdup(name)};
        g_entries.store(grown, std::memory_order_release);   // pointer before count, in that order
        g_entry_count.store(count, std::memory_order_release);
        if (previous) g_retired.push_back(previous);
    }
    uint32_t addr = g_thunk_base + kSlot * uint32_t(slots.size() - 1);
    if (strcmp(name, "_HUGE") == 0) {
        WF64(addr, HUGE_VAL);
    }
    return addr;
}

bool DispatchImport(Ctx* ctx, uint32_t target) {
    if (!g_thunk_base || target < g_thunk_base || target >= g_thunk_base + kThunkSize) return false;
    Ctx* saved = tl_ctx;
    tl_ctx = ctx;
    const uint32_t index = (target - g_thunk_base) / kSlot;
    // Count first, then the table: a table published after that count is at least as long, and an older table is only
    // ever retired, never freed, so either pointer is safe for every index below the count that was read.
    const uint32_t count = g_entry_count.load(std::memory_order_acquire);
    if (index >= count || (target & (kSlot - 1))) {
        Fatal("call into the import area at %08x", target);
    }
    Entry* entries = g_entries.load(std::memory_order_acquire);
    const Entry& e = entries[index];
    if (!e.fn) {
        Fatal("unimplemented import %s!%s (called from %08x, args %08x %08x %08x %08x)", e.dll ? e.dll : "?",
              e.name ? e.name : "?", R32(ctx->esp), Arg(ctx, 0), Arg(ctx, 1), Arg(ctx, 2), Arg(ctx, 3));
    }
    if (g_profiling.load(std::memory_order_relaxed) && !(e.name && strcmp(e.name, "SwitchToFiber") == 0)) {
        ProfiledCall(e.fn, index, ctx);
    } else {
        e.fn(ctx);
    }
    tl_ctx = saved;
    return true;
}

}  // namespace native
