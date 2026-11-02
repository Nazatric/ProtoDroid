#!/usr/bin/env python3
"""Structural regressions for ProtoDroid's opt-in DXVK present-pass FXAA."""

from pathlib import Path
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: test_dxvk_fxaa.py <patched DXVK source root>")
root = Path(sys.argv[1])
dxvk = root / "src/dxvk"
cpp = (dxvk / "dxvk_swapchain_blitter.cpp").read_text(encoding="utf-8")
header = (dxvk / "dxvk_swapchain_blitter.h").read_text(encoding="utf-8")
meson = (dxvk / "meson.build").read_text(encoding="utf-8")
shader = (dxvk / "shaders/dxvk_present_frag_fxaa.frag").read_text(encoding="utf-8")
copy_shader = (dxvk / "shaders/dxvk_present_frag.frag").read_text(encoding="utf-8")
blit_shader = (dxvk / "shaders/dxvk_present_frag_blit.frag").read_text(encoding="utf-8")

assert 'env::getEnvVar("PROTODROID_FXAA") == "1"' in cpp
assert "DXVK present: FXAA requested for single-sample SDR game frames" in cpp
assert "DXVK present: FXAA pipeline active" in cpp
assert "DXVK present: FXAA bypassed by a multisampled game frame" in cpp
assert "m_fxaaActiveLogged" in cpp and "m_fxaaMsaaWarned" in cpp
assert "#include <dxvk_present_frag_fxaa.h>" in cpp
assert "shaders/dxvk_present_frag_fxaa.frag" in meson
assert "sizeof(dxvk_present_frag_fxaa), dxvk_present_frag_fxaa" in cpp
assert "if (m_fxaa)" in cpp, "FXAA module must not be created in ordinary mode"

# The new path is narrow: original MSAA resolves, gamma, cursor blending, and non-SDR presentation stay unchanged.
gate = cpp.split("key.needsFxaa =", 1)[1].split(";", 1)[0]
for condition in (
    "m_fxaa", "!key.needsBlending", "!key.needsGamma",
    "key.srcSamples == VK_SAMPLE_COUNT_1_BIT",
    "key.srcSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR",
    "key.dstSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR",
):
    assert condition in gate, f"missing FXAA gate: {condition}"
select = cpp.split("if (key.needsFxaa)", 1)[1].split("blitStages[1].pSpecializationInfo", 1)[0]
assert "m_shaderFsFxaa.stageInfo" in select
assert "else if (key.srcSamples == VK_SAMPLE_COUNT_1_BIT)" in select
assert "m_shaderFsMsResolve.stageInfo" in select

# Pipeline identity must distinguish FXAA from the original copy/blit path.
assert header.count("needsFxaa") >= 4
assert "hash.add(uint32_t(needsFxaa))" in header
assert "&& needsFxaa == other.needsFxaa" in header
assert "m_shaderFsFxaa" in header

# Display-referred sampling, bounded rectangles, source alpha, and the existing output conversion are required.
assert '#include "dxvk_present_common.glsl"' in shader
assert "clamp(pos, lo, hi)" in shader and "src_offset + src_extent - 1" in shader
assert "linear_to_srgb(color.rgb)" in shader and "srgb_to_linear(color.rgb)" in shader
assert "sc_rgb_to_output(input_to_sc_rgb(color))" in shader
assert "textureLod(s_image, center, 0.0f)" in shader
assert "lumaMax - lumaMin < max(FXAA_EDGE_MIN, lumaMax * FXAA_EDGE_REL)" in shader
assert "vec4(color, colorM.a)" in shader
assert "s_image_ms" not in shader, "FXAA shader must only see the single-sample source"

# Do not accidentally fold experimental code into the established ordinary shaders.
assert "fxaa" not in copy_shader.lower() and "fxaa" not in blit_shader.lower()
print("DXVK FXAA environment, pipeline gating, bounded shader, and fallback checks passed")
