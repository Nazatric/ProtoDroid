#pragma once

#include <cstdint>
#include <limits>

namespace native {

// UNKNOWN is deliberately distinct from SPECIAL for diagnostics. Android maps both to the ordinary Special label;
// only a fresh, internally consistent CONSUME probe may change that presentation.
enum class SmartSpecialContext : uint32_t {
    Unknown = 0,
    Special = 1,
    Consume = 2,
};

constexpr int64_t kSmartContextFreshNs = 250000000;  // 250 ms; no probe cadence is assumed beyond this bound.

inline bool SmartPlayerHandleMatches(uint32_t handle, uint32_t player) {
    return player != 0 && (player & 0xffffu) != 0xffffu && handle == player;
}

inline SmartSpecialContext DecodeConsumeProbe(bool object_resolved, bool condition_result,
                                               uint8_t expected, uint8_t message) {
    // The original returns early without sending the message when handle resolution fails. The expected/message
    // values are bools in this condition; anything else means our stack/layout assumption is not trustworthy.
    if (!object_resolved || expected > 1 || message > 1) return SmartSpecialContext::Unknown;
    // Account for negated conditions and verify the original result against the exact comparison in the DLL.
    if (condition_result != (message == expected)) return SmartSpecialContext::Unknown;
    return message == 1 ? SmartSpecialContext::Consume : SmartSpecialContext::Special;
}

// One atomic holds both context and its monotonic-millisecond timestamp, preventing a UI reader from pairing a new
// context with an old timestamp (or vice versa). Two low bits store the public context values 0..2.
inline uint64_t EncodeSmartContext(SmartSpecialContext context, int64_t now_ns) {
    if (context == SmartSpecialContext::Unknown || now_ns < 0) return 0;
    const uint32_t value = static_cast<uint32_t>(context);
    if (value > 2) return 0;
    const uint64_t milliseconds = static_cast<uint64_t>(now_ns / 1000000);
    if (milliseconds > (std::numeric_limits<uint64_t>::max() >> 2)) return 0;
    return (milliseconds << 2) | value;
}

inline SmartSpecialContext DecodeSmartContext(uint64_t packed, int64_t now_ns) {
    const uint32_t value = static_cast<uint32_t>(packed & 3u);
    if (value == 0 || value > 2 || now_ns < 0) return SmartSpecialContext::Unknown;
    const uint64_t milliseconds = packed >> 2;
    if (milliseconds > static_cast<uint64_t>(std::numeric_limits<int64_t>::max() / 1000000)) {
        return SmartSpecialContext::Unknown;
    }
    const int64_t sampled_ns = static_cast<int64_t>(milliseconds) * 1000000;
    const int64_t age_ns = now_ns - sampled_ns;
    if (age_ns < 0 || age_ns > kSmartContextFreshNs) return SmartSpecialContext::Unknown;
    return static_cast<SmartSpecialContext>(value);
}

// Implemented by smart_controls.cpp for the Android/native host. Kept here so the reducer above remains host-testable.
void SmartControlsInitialize();
void SmartControlsSetEnabled(bool enabled);
int SmartSpecialContextSnapshot();

}  // namespace native
