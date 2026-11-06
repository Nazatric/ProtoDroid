#!/usr/bin/env python3
"""Validate DXVK device-creation compatibility and post-failure feature diagnostics on the patched source."""
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("/tmp/protodroid-dxvk-test")
source = (root / "src/dxvk/dxvk_adapter.cpp").read_text(encoding="utf-8")
body = source.split("Rc<DxvkDevice> DxvkAdapter::createDevice(", 1)[1].split(
    "DxvkAdapter::getQueueFamilies", 1
)[0]
check = "if (!checkFeatureSupport(enabledFeatures))"
audit = "checkFeatureSupport(enabledFeatures);"
create = body.index("m_vki->vkCreateDevice")
v12_start = body.index(
    "    if (useVulkan12Compatibility()) {\n"
    "      enabledFeatures.extExtendedDynamicState.extendedDynamicState"
)
v12_end = body.index("#endif", v12_start)
v12 = body[v12_start:v12_end]

# Preserve the established Vulkan 1.2 preflight exactly, but never extend it to the Vulkan 1.3 path.
assert check in v12 and body[:create].count(check) == 1, (
    "the original Vulkan 1.2 preflight changed or a new pre-create rejection was introduced"
)
assert check not in body[v12_end:create], (
    "regression: Vulkan 1.3 devices such as Mali-G720 must reach vkCreateDevice before any feature verdict"
)
assert "required logical-device features are not supported" not in body

# On every Android path, an actual driver FEATURE_NOT_PRESENT result gets the exact requested-feature names.
post = body[create:]
guard = "if (vr == VK_ERROR_FEATURE_NOT_PRESENT)"
assert guard in post and audit in post, "actual device-creation failures lost their feature audit"
assert post.index("if (vr != VK_SUCCESS && enableCudaInterop)") < post.index(guard) < post.index(audit), (
    "feature diagnostics must audit the final result after the existing CUDA retry policy"
)
assert post.index(audit) < post.index('if (vr != VK_SUCCESS)\n      throw DxvkError'), (
    "feature diagnostics must precede the existing final failure throw"
)
assert "vkCreateDevice reported VK_ERROR_FEATURE_NOT_PRESENT" in post
assert 'Logger::err(str::format("DxvkAdapter: missing required feature ", name))' in source, (
    "the audit must retain exact missing-feature names"
)

# maintenance4 is genuinely optional on Android: the allocator has a temporary-resource
# fallback, so an unsupported bit must not be forced into vkCreateDevice on the 1.3 path.
android_maintenance = """#ifdef __ANDROID__
      enabledFeatures.vk13.maintenance4 = m_deviceFeatures.vk13.maintenance4;
      if (!enabledFeatures.vk13.maintenance4)
        Logger::warn(\"DxvkAdapter: maintenance4 unavailable; using temporary-resource memory queries\");
#else
      enabledFeatures.vk13.maintenance4 = VK_TRUE;
#endif"""
assert android_maintenance in body, (
    "Android maintenance4 must follow physical support while retaining the non-Android policy"
)
assert "enabledFeatures.vk13.maintenance4 = useVulkan12Compatibility()" not in body, (
    "the old Vulkan 1.3 unconditional maintenance4 request remains"
)
assert "if (maintenance4) {\n      DXVK_ALIAS_COMMAND(vkGetDeviceBufferMemoryRequirements" in source, (
    "Vulkan 1.2 command aliases must remain conditional on maintenance4"
)

memory = (root / "src/dxvk/dxvk_memory.cpp").read_text(encoding="utf-8")
for signature, create, query, destroy in (
    ("bool DxvkMemoryAllocator::getBufferMemoryRequirements(", "vkCreateBuffer", "vkGetBufferMemoryRequirements2", "vkDestroyBuffer"),
    ("bool DxvkMemoryAllocator::getImageMemoryRequirements(", "vkCreateImage", "vkGetImageMemoryRequirements2", "vkDestroyImage"),
):
    block = memory.split(signature, 1)[1].split("\n  }\n", 1)[0]
    assert "if (m_device->features().vk13.maintenance4)" in block
    assert "else" in block and create in block and query in block and destroy in block, (
        f"the {signature} fallback must retain temporary-resource querying"
    )

wrapper = (Path(__file__).resolve().parents[1] / "src/native/turnip_load.h").read_text(encoding="utf-8")
required = wrapper.split("const Required required[] = {", 1)[1].split("};", 1)[0]
assert '{"maintenance4"' not in required, (
    "the isolated wrapper probe must not reject an optional maintenance4 capability"
)
assert "temporary buffer/image" in wrapper and "maintenance4=%u" in wrapper, (
    "the wrapper must retain maintenance4 diagnostics while removing only its required-gate status"
)
print("DXVK creation preservation, post-failure diagnostics, and maintenance4 fallback checks passed")
