// Host-independent D3D9 value/geometry helpers. Runtime state is forwarded directly to DXVK; there is no shadow
// state cache to lock, deduplicate, or invalidate across Reset and StateBlock Apply.
#pragma once

#include <cstdint>

namespace native {

// Numeric D3D9 values keep this helper independent of the DXVK headers in its host test.
// D3DSAMP_MIN/MAG/MIPFILTER = 5/6/7, MAXANISOTROPY = 10; D3DTEXF_LINEAR/ANISOTROPIC = 2/3.
constexpr uint32_t D3D9EffectiveSamplerValue(uint32_t type, uint32_t requested, int maximum) {
    maximum = maximum < 1 ? 1 : maximum > 16 ? 16 : maximum;
    if (type == 10) return requested < uint32_t(maximum) ? requested : uint32_t(maximum);
    if (maximum == 1 && requested == 3 && (type == 5 || type == 6 || type == 7)) return 2;
    return requested;
}

constexpr bool D3D9DepthDrawMayBeWorld(int depth_test, int depth_write) {
    // Unknown (-1) is deliberately world-like; only a known disabled test or write makes this a 2D draw.
    return depth_test != 0 && depth_write != 0;
}

constexpr bool D3D9FrontendMaskTiming(bool movie_open, uint32_t frames_since_world) {
    return movie_open || frames_since_world > 2;
}

struct D3D9Pillarboxes {
    uint32_t left = 0;        // [0, left)
    uint32_t right = 0;       // [right, width)
    uint32_t content = 0;     // centered 16:9 width
};

// Returns true only for an aspect wider than 16:9. Integer rounding assigns any odd spare pixel to the right bar.
constexpr bool D3D9WidePillarboxes(uint32_t width, uint32_t height, D3D9Pillarboxes& out) {
    out = {};
    if (!width || !height || uint64_t(width) * 9 <= uint64_t(height) * 16) return false;
    const uint32_t content = uint32_t(uint64_t(height) * 16 / 9);
    if (content >= width) return false;
    out.left = (width - content) / 2;
    out.right = out.left + content;
    out.content = content;
    return out.left != 0 || out.right != width;
}

}  // namespace native
