// GFx's type-4 value in this Prototype build is a 32-bit float in the low payload word at +8. The following word is
// reserved/uninitialised, not the high half of a double. Keep the ABI operation host-testable and reject bogus data.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

namespace native {

constexpr uint32_t kMinimapGFxFloatType = 4;

inline bool MinimapDecodeFloat(uint32_t type, uint32_t payload, float& value) {
    if (type != kMinimapGFxFloatType) return false;
    static_assert(sizeof(value) == sizeof(payload), "GFx float payload must be 32-bit");
    std::memcpy(&value, &payload, sizeof(value));
    return std::isfinite(value);
}

inline uint32_t MinimapEncodeFloat(float value) {
    uint32_t payload = 0;
    static_assert(sizeof(value) == sizeof(payload), "GFx float payload must be 32-bit");
    std::memcpy(&payload, &value, sizeof(payload));
    return payload;
}

inline bool MinimapCoordinateValid(float value) {
    // Logical 720p coordinates plus the allowed user offset remain far inside this bound. It rejects stale pointers,
    // uninitialised payloads, NaN/Inf, and other values that could send the entire HUD clip off screen.
    return std::isfinite(value) && std::fabs(value) <= 8192.0f;
}

inline bool MinimapCoordinateNear(float actual, float expected) {
    return MinimapCoordinateValid(actual) && std::fabs(actual - expected) <= 0.01f;
}

}  // namespace native
