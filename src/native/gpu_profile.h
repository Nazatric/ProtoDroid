// Pure GPU/vendor and Vulkan-version policy shared by native driver selection and host regression tests.
// Feature support is still queried from Vulkan; a vendor profile must never be used to invent a capability.
#pragma once

#include <algorithm>
#include <cstdint>

namespace native {

inline constexpr uint32_t kVendorArm = 0x13b5;
inline constexpr uint32_t kVendorQualcomm = 0x5143;
inline constexpr uint32_t kDriverMesaTurnip = 18;

constexpr uint32_t MakeVulkanApi(uint32_t major, uint32_t minor, uint32_t patch = 0) {
    return (major << 22) | (minor << 12) | patch;
}
inline constexpr uint32_t kVulkanApi12 = MakeVulkanApi(1, 2);
inline constexpr uint32_t kVulkanApi13 = MakeVulkanApi(1, 3);

enum class GpuFamily { Other, MaliImmortalis, QualcommAdreno };

struct GpuProfile {
    GpuFamily family;
    const char* name;
    bool prefer_turnip;
    bool mali_compatibility;
    uint32_t max_compiler_threads;  // zero means leave DXVK's normal automatic worker count unchanged
};

constexpr GpuProfile GetGpuProfile(uint32_t vendor_id) {
    if (vendor_id == kVendorArm) return {GpuFamily::MaliImmortalis, "mali-immortalis", false, true, 2};
    if (vendor_id == kVendorQualcomm) return {GpuFamily::QualcommAdreno, "qualcomm-adreno", true, false, 0};
    return {GpuFamily::Other, "other", false, false, 0};
}

// Negotiate no higher than the proven 1.3 path. A 1.2 result requires DXVK's extension-equivalent compatibility
// path; anything older is rejected before DXVK receives a null command or an incomplete feature chain.
constexpr uint32_t NegotiateVulkanApi(uint32_t instance_api, uint32_t device_api) {
    const uint32_t api = std::min(std::min(instance_api, device_api), kVulkanApi13);
    return api >= kVulkanApi12 ? api : 0;
}

constexpr bool ContainsText(const char* text, const char* needle) {
    if (!text || !needle || !*needle) return false;
    for (const char* start = text; *start; ++start) {
        const char* a = start;
        const char* b = needle;
        while (*a && *b && *a == *b) {
            ++a;
            ++b;
        }
        if (!*b) return true;
    }
    return false;
}

constexpr bool StartsWithText(const char* text, const char* prefix) {
    if (!text || !prefix) return false;
    while (*prefix) {
        if (*text++ != *prefix++) return false;
    }
    return true;
}

// KGSL uses names such as Adreno825 and Adreno840v2. Only the A825 gets the new Gen8 build automatically: other
// A8xx models keep their existing system-driver policy unless the player explicitly opts into Turnip/custom.
constexpr bool PreferGen8Turnip(const char* model) {
    return StartsWithText(model, "Adreno825") || ContainsText(model, "Adreno (TM) 825");
}

// Android 16 builds can expose ro.soc.model even when the KGSL sysfs model is unavailable. SM8735 is the A825
// platform; do not infer an A8xx generation from unrelated Snapdragon names.
constexpr bool PreferGen8Turnip(const char* model, const char* soc) {
    return PreferGen8Turnip(model) || StartsWithText(soc, "SM8735");
}

// Recent Turnip identifies itself through VkPhysicalDeviceDriverProperties. Keep the name fallback for older Mesa
// builds which predate a useful driverID, and never let this override the explicit custom-driver exception.
constexpr bool IsTurnipIdentity(uint32_t driver_id, const char* device_name) {
    return driver_id == kDriverMesaTurnip || ContainsText(device_name, "Turnip");
}

}  // namespace native
