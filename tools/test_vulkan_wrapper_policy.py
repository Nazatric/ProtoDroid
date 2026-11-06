#!/usr/bin/env python3
"""Regression policy for the real Vulkan 1.1+ Mesa compatibility route."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
driver = (ROOT / "src/native/vulkan_driver.cpp").read_text(encoding="utf-8")
loader = (ROOT / "src/native/turnip_load.h").read_text(encoding="utf-8")

configure = driver.split("void ConfigureWrapperLogging(bool probe)", 1)[1].split("bool TryVulkanWrapper", 1)[0]
assert 'setenv("WRAPPER_VK_VERSION", "1.3", 1);' in configure, (
    "Mesa lowers DXVK's API but still advertises the base version without its effective-version override"
)
assert "ProbeRequiredDxvkFeatures" in configure, "the override must document the independent feature gate"

try_wrapper = driver.split("bool TryVulkanWrapper", 1)[1].split("void AutomaticSystemDevice", 1)[0]
assert "ReadDriverFailureMarker(kWrapperFailed, kBuild)" in try_wrapper, "ordinary Auto launches lost the wrapper crash-loop guard"
assert "unlink(kWrapperFailed)" not in driver, "native launch must leave marker cleanup to an explicit launcher driver action"
assert "ConfigureWrapperLogging(true);" in try_wrapper, "isolated wrapper probe lacks the effective-version override"
assert "TurnipWorksInChild(driver)" in try_wrapper, "wrapper probe must remain isolated"
assert "ConfigureWrapperLogging(false);" in try_wrapper, "in-process wrapper lacks the effective-version override"
assert "compatibility_wrapper = false" in loader and "bool compatibility_wrapper = false" in loader
assert 'app.pEngineName = "DXVK";' in loader, "DXVK request no longer selects Mesa's real lowering path"
assert "ProbeRequiredDxvkFeatures(gipa, instance, device, p.apiVersion, report)" in loader, (
    "effective wrapper features are no longer queried"
)
assert "if (effective_api < kVulkanApi12)" in loader, "base-version-only devices must still fail closed"
assert "wrapper missing required features" in loader and "wrapper missing required device extensions" in loader

system = driver.split("uint32_t SystemDriverApi", 1)[1].split("void LogSystemExtensions", 1)[0]
assert "ConfigureWrapperLogging" not in system and "WRAPPER_VK_VERSION" not in system, (
    "a direct system driver must never receive the wrapper override"
)
failed = driver.split("else if (std::string why = FailedBefore()", 1)[1].split("const std::string model", 1)[0]
assert "AutomaticSystemDevice();" in failed, "a failed Auto Turnip attempt must not bypass the wrapper fallback"
assert "trying the driver again" not in failed, "Auto would retry the same failed Turnip build on a <1.2 system"
print("Vulkan wrapper effective-version, required-feature, and failed-driver recovery checks passed")
