#include "gpu_profile.h"

#include <cstdio>
#include <cstdlib>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "gpu-profile test failed: %s\n", message);
        std::exit(1);
    }
}

static_assert(native::GetGpuProfile(native::kVendorArm).mali_compatibility, "ARM must retain Mali quirks");
static_assert(native::GetGpuProfile(native::kVendorArm).max_compiler_threads == 2, "Mali compiler cap changed");
static_assert(native::GetGpuProfile(native::kVendorQualcomm).prefer_turnip, "Adreno must prefer Turnip in auto mode");
static_assert(!native::GetGpuProfile(native::kVendorQualcomm).mali_compatibility, "Adreno received Mali quirks");
static_assert(native::NegotiateVulkanApi(native::kVulkanApi13, native::kVulkanApi13) == native::kVulkanApi13,
              "Vulkan 1.3 must remain preferred");
static_assert(native::NegotiateVulkanApi(native::kVulkanApi13, native::kVulkanApi12) == native::kVulkanApi12,
              "Vulkan 1.2 device was rejected");
static_assert(native::NegotiateVulkanApi(native::MakeVulkanApi(1, 4), native::MakeVulkanApi(1, 4)) ==
                  native::kVulkanApi13,
              "future APIs must stay on the tested 1.3 path");
static_assert(native::NegotiateVulkanApi(native::kVulkanApi12, native::MakeVulkanApi(1, 1)) == 0,
              "Vulkan 1.1 device was accepted");

static_assert(native::PreferGen8Turnip("Adreno825"), "A825 did not select its Gen8 candidate");
static_assert(native::PreferGen8Turnip("Adreno825v1"), "A825 revision did not select its Gen8 candidate");
static_assert(native::PreferGen8Turnip("Adreno (TM) 825"), "Vulkan-style A825 name was not recognized");
static_assert(!native::PreferGen8Turnip("Adreno840v2"), "A840 automatic policy was broadened");
static_assert(!native::PreferGen8Turnip("Adreno830"), "A830 automatic policy was broadened");
static_assert(!native::PreferGen8Turnip("Adreno (TM) 750"), "A7xx was sent to the Gen8-only driver");
static_assert(!native::PreferGen8Turnip(""), "missing GPU model selected Gen8");
static_assert(native::PreferGen8Turnip("", "SM8735"), "SM8735 fallback did not select A825 Gen8");
static_assert(native::PreferGen8Turnip("", "SM8735-AB"), "SM8735 variant did not select A825 Gen8");
static_assert(!native::PreferGen8Turnip("", "SM8750"), "another Snapdragon SoC selected A825 Gen8");
static_assert(!native::PreferGen8Turnip("", ""), "empty SoC identity selected Gen8");

void TestProfiles() {
    const auto mali = native::GetGpuProfile(native::kVendorArm);
    Check(mali.family == native::GpuFamily::MaliImmortalis && !mali.prefer_turnip,
          "Mali/Immortalis automatic profile is wrong");
    const auto adreno = native::GetGpuProfile(native::kVendorQualcomm);
    Check(adreno.family == native::GpuFamily::QualcommAdreno && adreno.max_compiler_threads == 0,
          "Qualcomm automatic profile is wrong");
    const auto other = native::GetGpuProfile(0x10de);
    Check(other.family == native::GpuFamily::Other && !other.mali_compatibility && !other.prefer_turnip,
          "unknown vendor did not retain the generic system-driver path");
}

void TestNegotiationAndIdentity() {
    Check(native::NegotiateVulkanApi(native::kVulkanApi12, native::kVulkanApi13) == native::kVulkanApi12,
          "1.2 loader did not constrain a 1.3 device");
    Check(native::NegotiateVulkanApi(native::MakeVulkanApi(1, 1), native::kVulkanApi13) == 0,
          "1.1 loader was accepted");
    Check(native::IsTurnipIdentity(native::kDriverMesaTurnip, "Adreno (TM) 750"), "Turnip driverID was ignored");
    Check(native::IsTurnipIdentity(0, "Turnip (Mesa)"), "legacy Turnip name fallback was removed");
    Check(!native::IsTurnipIdentity(0, "Adreno (TM) 750"), "Qualcomm system driver was mistaken for Turnip");
}

}  // namespace

int main() {
    TestProfiles();
    TestNegotiationAndIdentity();
    std::puts("gpu-profile tests passed (Mali, Adreno, other, Vulkan 1.2/1.3, Turnip identity)");
    return 0;
}
