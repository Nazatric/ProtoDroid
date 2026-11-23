#pragma once

#include <cstdint>
#include <limits>

namespace native {

// Interpret render_height as the vertical render resolution and preserve the fullscreen window's aspect ratio.
// Align width to 8 pixels for D3D9 render targets. On a 2712x1220 (about 20:9) display, 720p is 1600x720 rather
// than a stretched 1280x720. The 4320-pixel height bound rejects corrupt values before surface allocation.
inline bool RenderDimensionsForHeight(int render_height, int window_width, int window_height,
                                      int* render_width, int* output_height) {
    if (render_height <= 0 || render_height > 4320 || window_width <= 0 || window_height <= 0
        || !render_width || !output_height) {
        return false;
    }
    const uint64_t scaled_width = uint64_t(render_height) * uint64_t(window_width) / uint64_t(window_height);
    const uint64_t aligned_width = (scaled_width + 4u) & ~uint64_t(7u);
    if (aligned_width < 8u || aligned_width > uint64_t(std::numeric_limits<int>::max())) return false;
    *render_width = int(aligned_width);
    *output_height = render_height;
    return true;
}

}  // namespace native
