#!/usr/bin/env python3
"""Verify the Prototype-safe MSAA depth-resolve semantics in patched DXVK."""

import sys
from pathlib import Path

path = Path(sys.argv[1] if len(sys.argv) > 1 else "/tmp/protodroid-dxvk-test/src/d3d9/d3d9_device.cpp")
source = path.read_text(encoding="utf-8")
start = source.index("auto EmitResolveCS = [&](const Rc<DxvkImage>& resolveDst, bool intermediate)")
end = source.index("if (fastPath) {", start)
block = source[start:end]
assert "ctx->resolveImage(" in block, "color resolve path was removed"
assert "ctx->resolveDepthStencilImage(" in block, "depth resolve path is missing"
assert "VK_RESOLVE_MODE_AVERAGE_BIT" not in block, "depth samples must never be averaged into occlusion data"
assert block.count("VK_RESOLVE_MODE_SAMPLE_ZERO_BIT") >= 2, "depth and stencil must both copy sample zero"
assert "Prototype's MSAA effects path" in block, "the Android compatibility intent must remain documented"

resolve_z = source[source.index("void D3D9DeviceEx::ResolveZ()"):]
resolve_z = resolve_z[:resolve_z.index("void D3D9DeviceEx::TransitionImage")]
assert "constexpr auto resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;" in resolve_z
assert "first sample only" in resolve_z
print("DXVK color resolve preserved; MSAA depth/stencil resolves use sample zero")
