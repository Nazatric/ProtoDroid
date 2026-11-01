#include "mod_platform.h"

#include <array>
#include <mutex>
#include <utility>
#include <vector>

#include "guest.h"

#ifdef PROTODROID_DOBBY
#include <dobby.h>
#endif

namespace native {
namespace {

// A Dobby replacement receives only Ctx*. Fixed dispatcher slots preserve the target identity without executable
// runtime code generation beyond Dobby's own trampoline. Registrations happen before engine startup.
constexpr size_t kMaxHookTargets = 64;

struct HookSlot {
    uint32_t guest_va = 0;
    ModHookFn head = nullptr;       // latest/outermost detour
    ModHookFn original = nullptr;   // Dobby trampoline to the recompiled function
    std::vector<ModHookFn> chain;   // registration order; each original points to the previous entry
};

std::array<HookSlot, kMaxHookTargets> g_slots;

template <size_t Index>
void Dispatch(Ctx* c) {
    ModHookFn head = g_slots[Index].head;
    if (head) head(c);
}

template <size_t... Index>
constexpr std::array<ModHookFn, sizeof...(Index)> Dispatchers(std::index_sequence<Index...>) {
    return {&Dispatch<Index>...};
}

constexpr auto g_dispatchers = Dispatchers(std::make_index_sequence<kMaxHookTargets>{});

}  // namespace

bool ModHookInstall(uint32_t guest_va, ModHookFn replacement, ModHookFn* original) {
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    if (!replacement || !original) return false;
    RecompFn target = RecompLookup(guest_va);
    if (!target) {
        Log("mods: hook %08x has no recompiled target", guest_va);
        return false;
    }

    size_t index = kMaxHookTargets;
    for (size_t i = 0; i < g_slots.size(); ++i) {
        if (g_slots[i].guest_va == guest_va) {
            index = i;
            break;
        }
        if (g_slots[i].guest_va == 0 && index == kMaxHookTargets) index = i;
    }
    if (index == kMaxHookTargets) {
        Log("mods: hook target table is full at %08x", guest_va);
        return false;
    }
    HookSlot& slot = g_slots[index];
    for (ModHookFn installed : slot.chain) {
        if (installed == replacement) {
            Log("mods: duplicate detour rejected at %08x", guest_va);
            return false;
        }
    }

    if (slot.chain.empty()) {
#ifdef PROTODROID_DOBBY
        void* trampoline = nullptr;
        int result = DobbyHook(reinterpret_cast<void*>(target), reinterpret_cast<void*>(g_dispatchers[index]),
                               &trampoline);
        if (result != 0 || !trampoline) {
            Log("mods: Dobby hook %08x failed (%d)", guest_va, result);
            slot = HookSlot{};
            return false;
        }
        slot.guest_va = guest_va;
        slot.original = reinterpret_cast<ModHookFn>(trampoline);
#else
        Log("mods: hook %08x unavailable: this host build has no Dobby backend", guest_va);
        return false;
#endif
    }

    // Later registrations are outermost. Their original pointer is the previous detour, while the first detour keeps
    // Dobby's original trampoline. Every registered path therefore remains reachable exactly once.
    *original = slot.chain.empty() ? slot.original : slot.head;
    slot.chain.push_back(replacement);
    slot.head = replacement;
    Log("mods: hooked guest function %08x (chain depth %zu)", guest_va, slot.chain.size());
    return true;
}

}  // namespace native
