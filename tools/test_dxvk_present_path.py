#!/usr/bin/env python3
"""Structural present-path invariants; this is not a GPU rendering or FPS test."""
from pathlib import Path
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: test_dxvk_present_path.py <patched DXVK source root>")
root = Path(sys.argv[1])
dxvk = root / "src/dxvk"
blitter = (dxvk / "dxvk_swapchain_blitter.cpp").read_text(encoding="utf-8")
presenter = (dxvk / "dxvk_presenter.cpp").read_text(encoding="utf-8")
adapter = (dxvk / "dxvk_adapter.cpp").read_text(encoding="utf-8")
options = (root / "src/d3d9/d3d9_options.cpp").read_text(encoding="utf-8")
device = (root / "src/d3d9/d3d9_device.cpp").read_text(encoding="utf-8")
checks = 0


def check(condition, message):
    global checks
    checks += 1
    if not condition:
        raise AssertionError(message)


def function(source, signature, next_signature=None):
    start = source.index(signature)
    if next_signature:
        return source[start:source.index(next_signature, start + len(signature))]
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f"unterminated function: {signature}")


begin = function(blitter, "void DxvkSwapchainBlitter::beginPresent")
end = function(blitter, "void DxvkSwapchainBlitter::endPresent")
draw = function(blitter, "void DxvkSwapchainBlitter::performDraw")
create_pipeline = function(blitter, "VkPipeline DxvkSwapchainBlitter::createPipeline")
create_shader = function(blitter, "void DxvkSwapchainBlitter::createShaderModule")
create_shaders = function(blitter, "void DxvkSwapchainBlitter::createShaders")

# Full-frame presentation may discard old contents, but every uncovered letterbox/pillarbox path must clear black.
check("attachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE" in begin, "default load op")
check("attachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE" in begin, "presented contents must be stored")
clear = begin.split("if (srcRect.extent != dstRect.extent", 1)[1].split("attachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR", 1)[0]
for condition in ("dstRect.offset.x != 0", "dstRect.offset.y != 0",
                  "dstRect.extent.width != dstExtent.width", "dstRect.extent.height != dstExtent.height"):
    check(condition in clear, f"uncovered-area clear condition: {condition}")
check("barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED" in begin, "initial discard layout")
check("barrier.newLayout = dstView->image()->pickLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)" in begin,
      "color attachment layout")
check("attachmentInfo.clearValue.color.float32[3] = 1.0f" in begin, "opaque black clear")

# The color-to-present dependency and semaphore chain are required even when Present-wall time is small.
for text in ("barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT",
             "barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT",
             "barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT",
             "barrier.newLayout = dstView->image()->info().layout"):
    check(text in end, f"end-present dependency: {text}")
check(end.index("ctx.cmd->cmdEndRendering()") < end.index("ctx.cmd->cmdPipelineBarrier"), "render/barrier order")
check("ctx.cmd->cmdDraw(3, 1, 0, 0)" in draw, "single full-screen triangle")

# Binding 1 remains declared even without gamma. A dummy source view is required without nullDescriptor;
# an unwritten descriptor would not be a valid substitute on either capability path.
check("std::array<VkWriteDescriptorSet, 2> descriptorWrites" in draw, "two descriptor writes")
check("ctx.cmd->updateDescriptorSets(\n      descriptorWrites.size(), descriptorWrites.data())" in draw,
      "both descriptor writes are submitted")
check("if (m_gammaView)" in draw and "else if (!m_device->features().extRobustness2.nullDescriptor)" in draw,
      "gamma/null-descriptor branches")
fallback = draw.split("else if (!m_device->features().extRobustness2.nullDescriptor)", 1)[1].split("std::array<VkWriteDescriptorSet", 1)[0]
check("gammaDescriptor.imageView = srcView->handle()" in fallback, "valid gamma fallback view")
check("gammaDescriptor.imageLayout = srcView->image()->info().layout" in fallback, "valid gamma fallback layout")

# Preserve quality/capability-driven pipeline selection and pass-through restrictions.
check("key.srcSamples = srcView->image()->info().sampleCount" in draw, "real source sample count")
check("key.needsBlit = dstRect.extent != srcRect.extent" in draw, "real blit requirement")
check("key.needsBlending = enableBlending" in draw, "cursor blending state")
check("key.srcSamples == VK_SAMPLE_COUNT_1_BIT" in draw and "key.srcSpace == VK_COLOR_SPACE_PASS_THROUGH_EXT" not in draw,
      "no unconditional color-space pass-through in key setup")
pass_through = create_pipeline.split("if (specConstants.srcSpace == specConstants.dstSpace && key.srcSamples == VK_SAMPLE_COUNT_1_BIT)", 1)[1]
check("specConstants.srcSpace = VK_COLOR_SPACE_PASS_THROUGH_EXT" in pass_through and
      "specConstants.dstSpace = VK_COLOR_SPACE_PASS_THROUGH_EXT" in pass_through,
      "matching single-sample color-space pass-through")
check("key.needsBlit ? m_shaderFsBlit.stageInfo : m_shaderFsCopy.stageInfo" in create_pipeline,
      "single-sample copy/blit selection")
check("key.needsBlit ? m_shaderFsMsBlit.stageInfo : m_shaderFsMsResolve.stageInfo" in create_pipeline,
      "MSAA copy/resolve selection")
check("if (m_fxaa)" in create_shaders and "createShaderModule(m_shaderFsFxaa" in create_shaders,
      "FXAA remains opt-in")
check("m_device->features().khrMaintenance5.maintenance5" in create_shader and
      "m_device->features().extGraphicsPipelineLibrary.graphicsPipelineLibrary" in create_shader,
      "capability-driven module creation")
check("vk->vkCreateShaderModule" in create_shader and "if (vr != VK_SUCCESS)" in create_shader,
      "shader-module fallback and failure handling")

# Present IDs, modes, fences, and FIFO policy retain their feature/result gates.
present = function(presenter, "VkResult Presenter::presentImage")
check("m_device->features().khrPresentId.presentId && frameId" in present, "present-ID capability gate")
check("m_device->features().extSwapchainMaintenance1.swapchainMaintenance1" in present, "maintenance capability gate")
check("currSync.fenceSignaled = status >= 0" in present, "fence state follows present result")
check("m_vkd->vkQueuePresentKHR" in present, "real queue present retained")
check("#ifdef __ANDROID__" in presenter and "return VK_PRESENT_MODE_FIFO_KHR" in presenter, "Android FIFO safety")

# Heavy tracing is Mali+tour only; lightweight timing is opt-in and does not serialize other vendors.
check("static const bool enabled = env::getEnvVar(\"PROTODROID_PIPELINE_TIMING\") == \"1\"" in adapter,
      "pipeline timing option")
check("const int64_t callStart = timing ? pipelineNowNs() : 0" in adapter, "disabled timing clocks")
check("if (timing)\n      recordPipelineTiming" in adapter, "disabled timing counters")
check("if (serialize && maliPipelineTraceEnabled()" in adapter, "heavy trace gate")
check("const bool serializePipelines = m_deviceInfo.core.properties.vendorID == 0x13B5" in adapter,
      "Mali-only serialization")
check("if (serializePipelines || timePipelines)" in adapter, "wrapper installation gate")

# Existing Android memory behavior is not traded for a speculative GPU-path change.
check('#ifdef __ANDROID__\n    this->freeManagedCopies             = config.getOption<bool>        ("d3d9.freeManagedCopies",             true);' in options,
      "Android managed-copy default")
upload = function(device, "void D3D9DeviceEx::UploadManagedTexture")
for condition in ("m_d3d9Options.freeManagedCopies", "!pResource->IsDynamic()",
                  "!pResource->IsAnySubresourceLocked()", "D3D9ConversionFormat_None"):
    check(condition in upload, f"managed-copy release condition: {condition}")

print(f"DXVK present-path structural checks passed ({checks})")
