#!/usr/bin/env python3
"""Validate the fully patched DXVK logical-device feature preflight, not just patch text."""
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("/tmp/protodroid-dxvk-test")
source = (root / "src/dxvk/dxvk_adapter.cpp").read_text(encoding="utf-8")
body = source.split("Rc<DxvkDevice> DxvkAdapter::createDevice(", 1)[1].split(
    "DxvkAdapter::getQueueFamilies", 1
)[0]
check = "if (!checkFeatureSupport(enabledFeatures))"
assert body.count(check) == 1, "feature preflight must run once on the final Android feature set"
position = body.index(check)
clamp = body.index("clampFeature(enabledFeatures.extRobustness2.nullDescriptor")
null_override = body.index("DXVK_NO_NULL_DESCRIPTOR")
create = body.index("m_vki->vkCreateDevice")
assert clamp < null_override < position < create, (
    "final feature validation must follow optional-feature clamping and precede vkCreateDevice"
)
assert "required logical-device features are not supported" in body
assert 'Logger::err(str::format("DxvkAdapter: missing required feature ", name))' in source, (
    "the preflight must retain exact missing-feature names"
)
# The Vulkan 1.2 extended-dynamic-state check remains specialized; the general check is no longer hidden in that path.
v12 = body.split("if (useVulkan12Compatibility()) {", 1)[1].split("#endif", 1)[0]
assert check not in v12, "general preflight is still restricted to the Vulkan 1.2 compatibility path"
print("DXVK final logical-device feature-preflight checks passed")
