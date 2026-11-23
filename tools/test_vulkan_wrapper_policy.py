#!/usr/bin/env python3
"""Regression policy for the real Vulkan 1.1+ Mesa compatibility route."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
driver = (ROOT / "src/native/vulkan_driver.cpp").read_text(encoding="utf-8")
loader = (ROOT / "src/native/turnip_load.h").read_text(encoding="utf-8")
build_script = (ROOT / "android/build-vulkan-wrapper.sh").read_text(encoding="utf-8")
mesa_patch = (ROOT / "android/patches/mesa-protodroid-android-wrapper.patch").read_text(encoding="utf-8")

# Android 11's app classloader cannot load Mesa's private libcutils/libhardware/libnativewindow/libsync sonames.
# Statically link the no-op helpers and use the NDK's public AHardwareBuffer implementation.
stub_patch = mesa_patch.split("diff --git a/src/android_stub/meson.build", 1)[1]
assert "+  lib_names = ['cutils', 'hardware', 'sync']" in stub_patch
assert "+    stub_libs += static_library(" in stub_patch
assert "+    stub_libs += shared_library(" not in stub_patch
assert "+      gnu_symbol_visibility : 'hidden'," in stub_patch and "+      pic : true," in stub_patch
assert "cc.find_library('android', required : true)" in stub_patch
assert "cc.find_library('log', required : true)" in stub_patch
assert all(
    f"lib{name}.so" in build_script
    for name in ("cutils", "hardware", "nativewindow", "sync", "backtrace")
)
assert "Shared library: [libandroid.so]" in build_script, "wrapper must use real public AHardwareBuffer APIs"

configure = driver.split("void ConfigureWrapperLogging(bool probe)", 1)[1].split("bool TryVulkanWrapper", 1)[0]
assert 'setenv("WRAPPER_VK_VERSION", "1.3", 1);' in configure, (
    "Mesa lowers DXVK's API but still advertises the base version without its effective-version override"
)
assert "ProbeRequiredDxvkFeatures" in configure, "the override must document the independent feature gate"

try_wrapper = driver.split("bool TryVulkanWrapper", 1)[1].split("void AutomaticSystemDevice", 1)[0]
assert "ConfigureWrapperLogging(true);" in try_wrapper, "isolated wrapper probe lacks the effective-version override"
assert "TurnipWorksInChild(driver)" in try_wrapper, "wrapper probe must remain isolated"
assert "ConfigureWrapperLogging(false);" in try_wrapper, "in-process wrapper lacks the effective-version override"
assert "compatibility_wrapper = false" in loader and "bool compatibility_wrapper = false" in loader
assert 'app.pEngineName = "DXVK";' in loader, "DXVK request no longer selects Mesa's real lowering path"
assert "ProbeRequiredDxvkFeatures(gipa, instance, device, p.apiVersion," in loader and "p.vendorID, info.driver_id, report)" in loader, (
    "effective wrapper feature requirements must include the physical GPU and driver profiles"
)
assert "if (effective_api < kVulkanApi12)" in loader, "base-version-only devices must still fail closed"
assert "wrapper missing required features" in loader and "wrapper missing required device extensions" in loader

# Vulkan API 1.2 does not imply that optional VkPhysicalDeviceFeatures / Vulkan12 feature bits are true. Match the
# exact D3D9 request, including Mali's documented shader fallbacks, rather than trusting DXVK's enabled-feature dump.
assert "const DxvkFeatureRequirements shader_requirements = GetDxvkFeatureRequirements(vendor_id);" in loader
assert 'DXVK\'s "Device features" dump is its enable request, not physical-device support' in loader
assert "get_features(device, &core);" in loader
for feature, policy in (
    ("shaderClipDistance", "shader_clip_distance"),
    ("shaderCullDistance", "shader_cull_distance"),
):
    assert f'{{"{feature}", shader_requirements.{policy}, core.features.{feature}}}' in loader, (
        f"Mali fallback still requires {feature}"
    )
assert '{"shaderDemoteToHelperInvocation", shader_requirements.shader_demote_to_helper_invocation, demote}' in loader
assert '{"fillModeNonSolid", true, core.features.fillModeNonSolid}' in loader
assert '{"multiViewport", true, core.features.multiViewport}' in loader
assert '{"maintenance4", !compatibility12 && !amd_driver, maintenance4}' in loader, (
    "maintenance4 is optional on Vulkan 1.2 and AMD's Vulkan 1.3 path"
)
assert '{"extendedDynamicState", compatibility12, extended12.extendedDynamicState}' in loader
assert "if (feature.required && !feature.supported)" in loader
assert "shader_requirements.shader_demote_to_helper_invocation &&" in loader
assert "driver_id == VK_DRIVER_ID_AMD_OPEN_SOURCE" in loader
assert "driver_id == VK_DRIVER_ID_AMD_PROPRIETARY" in loader
assert "VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME" in loader
assert "mali_shader_fallback=%u" in loader

system = driver.split("uint32_t SystemDriverApi", 1)[1].split("void LogSystemExtensions", 1)[0]
assert "ConfigureWrapperLogging" not in system and "WRAPPER_VK_VERSION" not in system, (
    "a direct system driver must never receive the wrapper override"
)
failed = driver.split("else if (std::string why = FailedBefore()", 1)[1].split("const std::string model", 1)[0]
assert "AutomaticSystemDevice();" in failed, "a failed Auto Turnip attempt must not bypass the wrapper fallback"
assert "trying the driver again" not in failed, "Auto would retry the same failed Turnip build on a <1.2 system"
print("Vulkan wrapper effective-version, required-feature, and failed-driver recovery checks passed")
