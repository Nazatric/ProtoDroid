#!/usr/bin/env python3
"""Verify that DXVK BC queries honor the physical-device feature and select native DXT decoding."""
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("/tmp/protodroid-dxvk-test")
adapter = (root / "src/d3d9/d3d9_adapter.cpp").read_text(encoding="utf-8")
device = (root / "src/d3d9/d3d9_device.cpp").read_text(encoding="utf-8")
format_header = (root / "src/d3d9/d3d9_format.h").read_text(encoding="utf-8")
native = Path("src/native/d3d9.cpp").read_text(encoding="utf-8")

query = adapter.split("HRESULT D3D9Adapter::CheckDeviceFormat(", 1)[1]
query = query.split("HRESULT D3D9Adapter::CheckDeviceMultiSampleType(", 1)[0]
feature = "m_adapter->features().core.features.textureCompressionBC"
guard = f"if (isBc && !{feature})"
assert guard in query, "BC format queries do not check the physical-device feature"
assert query.index(guard) < query.index("return CheckDeviceVkFormat(format"), (
    "BC feature rejection must precede misleading Vulkan format-property acceptance"
)
assert guard + "\n      return D3DERR_NOTAVAILABLE;" in query, "unsupported BC must not report success"
assert "IsDXTFormat(CheckFormat)" in query, "the DXT1-5 family is not covered"
dxt_family = format_header.split("inline bool IsDXTFormat(", 1)[1].split("\n  }", 1)[0]
for name in ("DXT1", "DXT2", "DXT3", "DXT4", "DXT5"):
    assert f"D3D9Format::{name}" in dxt_family, f"{name} disappeared from DXVK's DXT family"
for name in ("ATI1", "ATI2"):
    assert f"CheckFormat == D3D9Format::{name}" in query, f"{name} bypasses the BC feature check"
assert "vendorId" not in query and "vendorID" not in query, "BC support must not be inferred from a vendor"

enabled = "enabled.core.features.textureCompressionBC = supported.core.features.textureCompressionBC;"
assert enabled in device, "logical-device creation must retain the real BC feature rather than forcing it on"

create = native.split("void x_Direct3DCreate9(Ctx* c)", 1)[1].split("Return(c, Wrap(d3d), 1);", 1)[0]
assert "D3DFMT_DXT5" in create and "bc ? 0 : 1" in create, (
    "ProtoDroid no longer selects automatic CPU decoding after a failed BC capability query"
)
check = native.split("M(IDirect3D9, CheckDeviceFormat)", 1)[1]
check = check.split("M(IDirect3DTexture9, GetLevelCount)", 1)[0]
assert "FAILED(hr) && g_decode_dxt && IsDxt(check)" in check, (
    "guest DXT formats are not re-enabled through the established decoder path"
)
assert "D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL" in check, (
    "the decoder fallback must not invent render-target or depth-stencil support"
)
print("DXVK BC feature queries and ProtoDroid automatic DXT fallback checks passed")
