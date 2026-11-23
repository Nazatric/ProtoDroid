#pragma once

#include <cstdint>
#include <limits>

namespace native {

// Android's selectable render modes are fixed 16:9 targets. Keep their width independent of the device/display
// aspect ratio: 540p/720p/900p/1080p are 960x540/1280x720/1600x900/1920x1080. Round non-standard INI heights to
// the nearest 8-pixel width, as D3D9 render targets benefit from aligned dimensions. 4320 is an 8K-class ceiling that
// prevents corrupt configuration values from requesting unbounded render surfaces.
inline bool RenderDimensionsForHeight(int render_height, int* render_width, int* output_height) {
    if (render_height <= 0 || render_height > 4320 || !render_width || !output_height) return false;
    const uint64_t pixels = uint64_t(render_height) * 16u;
    const uint64_t unaligned_width = (pixels + 4u) / 9u;
    const uint64_t aligned_width = (unaligned_width + 4u) & ~uint64_t(7u);
    if (aligned_width < 8u || aligned_width > uint64_t(std::numeric_limits<int>::max())) return false;
    *render_width = int(aligned_width);
    *output_height = render_height;
    return true;
}

}  // namespace native
