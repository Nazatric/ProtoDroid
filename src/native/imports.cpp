// Import thunks: each imported dll!name gets a 16-byte slot at g_thunk_base + 16*index (an arena block: a fixed
// address may be taken on Android, where ART maps its heap below 4 GiB). Generated code calls through
// the IAT with RecompDispatch(target), and target falls in this range. Data imports (MSVCR80 _HUGE, _adjust_fdiv)
// keep their value in the slot itself.
#include <sys/mman.h>

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "guest.h"
#include "import_caller_profile.h"

namespace native {

// Defined by runtime.cpp. Import handlers can call back into guest code, so the active context must follow the
// import even when dispatch recognizes its thunk before attempting a recompiled-function lookup.
extern __thread Ctx* tl_ctx;

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

// Default dispatch needs only the function pointer. Keep this dense hot array in lockstep with Slots() so ordinary
// imports do not touch lazy-static guards or the two strings in each metadata record.
std::vector<ImportFn> g_slot_functions;

std::string Key(const char* dll, const char* name) { return std::string(dll) + "!" + name; }

// Setting import_profile: every thread logs, every 10 s, where its wall time went - the imports (WinAPI, D3D9
// methods) it spent the most time in, inclusive of nested calls, and the rest (guest code). Finds the waits that
// keep a frame long while no thread is busy.
bool ImportProfiling() {
    static const bool on = SettingInt("import_profile", 0) != 0;
    return on;
}

struct ThreadProfile {
    using Clock = std::chrono::steady_clock;
    Clock::time_point window = Clock::now();
    std::vector<uint64_t> ns, calls;
    ImportCallerProfile callers;
    int depth = 0;
};

void LogCallerStats(const ThreadProfile& tp, const std::vector<uint32_t>& order) {
    auto& slots = Slots();
    std::vector<ImportCallerMethod> methods;
    for (size_t k = 0; k < order.size() && k < 10; ++k) {
        const uint32_t index = order[k];
        if (slots[index].dll == "d3d9.dll")
            methods.push_back({index, slots[index].name, tp.ns[index], tp.calls[index]});
    }
    for (const auto& line : BuildImportCallerLines(tp.callers, int(gettid()), methods))
        Log("%s", line.c_str());
}

void ProfiledCall(const Slot& s, uint32_t index, Ctx* ctx) {
    thread_local ThreadProfile tp;
    if (tp.ns.size() <= index) {
        tp.ns.resize(index + 64);
        tp.calls.resize(index + 64);
    }
    const bool track_callers = s.dll == "d3d9.dll";
    uint32_t caller_rva = 0xffffffffu;
    if (track_callers) {
        const uint32_t return_address = SR32(ctx->esp);  // The import has not popped it yet.
        if (return_address >= g_engine_lo && return_address < g_engine_hi)
            caller_rva = return_address - g_engine_lo;
    }
    auto t0 = ThreadProfile::Clock::now();
    ++tp.depth;
    s.fn(ctx);
    --tp.depth;
    auto t1 = ThreadProfile::Clock::now();
    const uint64_t duration = uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    tp.ns[index] += duration;
    ++tp.calls[index];
    if (track_callers) tp.callers.Add(index, caller_rva, duration);
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
    auto& slots = Slots();
    for (size_t k = 0; k < order.size() && k < 10; ++k) {
        uint32_t i = order[k];
        char buf[160];
        snprintf(buf, sizeof(buf), " %s %.0f%% (%llu)", slots[i].name.c_str(), 100.0 * double(tp.ns[i]) / 1e6 / wall,
                 (unsigned long long)tp.calls[i]);
        line += buf;
    }
    Log("imports tid %d: guest %.0f%% |%s", int(gettid()), 100.0 * (wall - in_imports) / wall, line.c_str());
    LogCallerStats(tp, order);
    std::fill(tp.ns.begin(), tp.ns.end(), 0);
    std::fill(tp.calls.begin(), tp.calls.end(), 0);
    tp.callers.Reset();
    tp.window = t1;
}

}  // namespace

void RegisterImports(const ImportDef* defs, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Handlers()[Key(defs[i].dll, defs[i].name)] = defs[i].fn;
    }
}

uint32_t ImportThunk(const char* dll, const char* name) {
    if (!g_thunk_base) {
        g_thunk_base = PageAlloc(kThunkSize);
        if (!g_thunk_base) Fatal("cannot allocate the import thunk area");
    }
    std::string key = Key(dll, name);
    auto& slots = Slots();
    for (size_t i = 0; i < slots.size(); ++i) {
        if (slots[i].dll == dll && slots[i].name == name) return g_thunk_base + kSlot * uint32_t(i);
    }
    auto h = Handlers().find(key);
    slots.push_back({dll, name, h == Handlers().end() ? nullptr : h->second});
    try {
        g_slot_functions.push_back(slots.back().fn);
    } catch (...) {
        slots.pop_back();
        throw;
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
    uint32_t index = (target - g_thunk_base) / kSlot;
    if (index >= g_slot_functions.size() || (target & (kSlot - 1))) {
        Fatal("call into the import area at %08x", target);
    }
    const ImportFn fn = g_slot_functions[index];
    if (!fn) {
        const Slot& s = Slots()[index];
        Fatal("unimplemented import %s!%s (called from %08x, args %08x %08x %08x %08x)", s.dll.c_str(),
              s.name.c_str(), R32(ctx->esp), Arg(ctx, 0), Arg(ctx, 1), Arg(ctx, 2), Arg(ctx, 3));
    }
    if (ImportProfiling()) {
        const Slot& s = Slots()[index];
        if (s.name != "SwitchToFiber") {  // a fiber switch does not return until switched back
            ProfiledCall(s, index, ctx);
            tl_ctx = saved;
            return true;
        }
    }
    fn(ctx);
    tl_ctx = saved;
    return true;
}

}  // namespace native
