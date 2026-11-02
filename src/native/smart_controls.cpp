#include "smart_controls.h"

#include <atomic>

#include "guest.h"
#include "mod_platform.h"
#include "timing_window.h"

namespace native {
namespace {

#ifdef PROTODROID_DOBBY
// Steam build 19788432, preferred VA 0x10122160:
// proto::fightitems::motion::TransformationConsumeDescriptionValidCondition::vfunction10.
constexpr uint32_t kConsumeCondition = kEngineBase + 0x00122160u;
// Preferred VA 0x1128348c. The original condition resolves index/generation through this object table.
constexpr uint32_t kObjectTablePointer = kEngineBase + 0x0128348cu;
// Preferred VA 0x1126e40c. IsPlayerCharacterCondition::vfunction10 compares its resolved handle with this player
// character handle at 0x101d90a8. It uses the same 32-bit handle format and object table as this Consume condition.
constexpr uint32_t kPlayerCharacterHandle = kEngineBase + 0x0126e40cu;
ModHookFn g_consume_original = nullptr;
#endif

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_probe_ready{false};
std::atomic<uint64_t> g_sample{0};

#ifdef PROTODROID_DOBBY
// Mirror the handle-normalization and early-exit checks at 0x107fc7a0/0x10122160, then require the engine's own
// player-character handle. This is deliberately evaluated before the original: unresolved/non-player objects must
// not construct a sample or clear a valid player sample, and the unresolved original path sends no message.
bool ConsumeProbePlayerResolved(Ctx* context) {
    const uint32_t reference = R32(context->esp + 0x0c);
    if (!reference) return false;
    const uint32_t marker = R32(reference + 0x08);
    if (marker < 0x10000u || marker > 0x2ffffu) return false;
    const uint32_t handle = R32(reference + 0x0c);
    const uint32_t index = handle & 0xffffu;
    if (index == 0xffffu) return false;
    const uint32_t player = R32(kPlayerCharacterHandle);
    if (!SmartPlayerHandleMatches(handle, player)) return false;
    const uint32_t table = R32(kObjectTablePointer);
    if (!table) return false;
    const uint32_t entry = table + index * 8u;
    if (R16(entry + 4) != ((handle >> 16) & 0xffffu)) return false;
    return R32(entry) != 0;
}

void ConsumeConditionDetour(Ctx* context) {
    if (!g_enabled.load(std::memory_order_relaxed)) {
        g_consume_original(context);
        return;
    }

    const uint32_t entry_esp = context->esp;
    const bool resolved = ConsumeProbePlayerResolved(context);
    const uint8_t expected = resolved && context->ecx ? R8(context->ecx + 0x0c) : 0xffu;
    g_consume_original(context);
    if (!g_enabled.load(std::memory_order_relaxed) || !resolved) return;

    // At 0x101221d3 the original compares [entry_esp-4] with this+0x0c. The original has returned to this detour,
    // but its local message bytes remain intact below the caller's stack pointer. Read the actual message result,
    // then verify the condition result so a bad layout assumption fails closed instead of changing the label.
    const uint8_t message = R8(entry_esp - 4);
    const bool condition_result = (context->eax & 0xffu) != 0;
    const SmartSpecialContext decoded = DecodeConsumeProbe(true, condition_result, expected, message);
    if (decoded == SmartSpecialContext::Unknown) return;
    g_sample.store(EncodeSmartContext(decoded, MonotonicNanos()), std::memory_order_release);
}
#endif

}  // namespace

void SmartControlsInitialize() {
    g_enabled.store(SettingInt("touch_controls", 0) != 0, std::memory_order_relaxed);
    g_sample.store(0, std::memory_order_relaxed);
    // Install before startup even when Classic is selected so the in-game setting can switch modes safely. The
    // disabled detour is only an atomic check plus the original call, and no gameplay state is read in that path.
#ifdef PROTODROID_DOBBY
    g_probe_ready.store(ModHookInstall(kConsumeCondition, ConsumeConditionDetour, &g_consume_original),
                        std::memory_order_release);
#else
    g_probe_ready.store(false, std::memory_order_release);
#endif
    if (g_enabled.load(std::memory_order_relaxed) && !g_probe_ready.load(std::memory_order_acquire)) {
        Log("smart controls: consume-validity probe unavailable; right mouse remains Special");
    }
}

void SmartControlsSetEnabled(bool enabled) {
    const bool previous = g_enabled.exchange(enabled, std::memory_order_acq_rel);
    if (previous != enabled) g_sample.store(0, std::memory_order_release);
    if (enabled && !previous && !g_probe_ready.load(std::memory_order_acquire)) {
        Log("smart controls: consume-validity probe unavailable; right mouse remains Special");
    }
}

int SmartSpecialContextSnapshot() {
    if (!g_enabled.load(std::memory_order_acquire) || !g_probe_ready.load(std::memory_order_acquire)) return 0;
    return static_cast<int>(DecodeSmartContext(g_sample.load(std::memory_order_acquire), MonotonicNanos()));
}

}  // namespace native
