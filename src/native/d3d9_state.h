#ifndef PROTORE_D3D9_STATE_H
#define PROTORE_D3D9_STATE_H

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace native {

// Numeric D3D9 values keep this helper independent of the DXVK headers in its host test.
// D3DSAMP_MIN/MAG/MIPFILTER = 5/6/7, MAXANISOTROPY = 10; D3DTEXF_LINEAR/ANISOTROPIC = 2/3.
constexpr uint32_t D3D9EffectiveSamplerValue(uint32_t type, uint32_t requested, int maximum) {
    maximum = maximum < 1 ? 1 : maximum > 16 ? 16 : maximum;
    if (type == 10) return requested < uint32_t(maximum) ? requested : uint32_t(maximum);
    if (maximum == 1 && requested == 3 && (type == 5 || type == 6 || type == 7)) return 2;
    return requested;
}

struct D3D9NdcBounds {
    float min_x = 0, min_y = 0, max_x = 0, max_y = 0;
};

inline bool D3D9BoundsFinite(const D3D9NdcBounds& b) {
    return std::isfinite(b.min_x) && std::isfinite(b.min_y) && std::isfinite(b.max_x) &&
           std::isfinite(b.max_y) && b.max_x >= b.min_x && b.max_y >= b.min_y;
}

inline bool D3D9BoundsValid(const D3D9NdcBounds& b) {
    return D3D9BoundsFinite(b) && b.max_x > b.min_x && b.max_y > b.min_y;
}

constexpr bool D3D9DepthDrawMayBeWorld(int depth_test, int depth_write) {
    // Unknown (-1) is deliberately world-like; only a known disabled test or write makes this a 2D draw.
    return depth_test != 0 && depth_write != 0;
}

inline bool D3D9MinimapDepthEligible(int depth_test, int depth_write) {
    // Stencil state is intentionally irrelevant: the circular stencil mask and stencil-tested map both qualify.
    return depth_test == 0 || depth_write == 0;
}

inline bool D3D9MinimapBoundsMatch(const D3D9NdcBounds& b, int depth_test, int depth_write) {
    // Point sprites and horizontal/vertical ring segments have one or two zero extents, but still have a valid
    // transformed position. Loading quads separately require strictly positive two-dimensional bounds.
    if (!D3D9BoundsFinite(b) || !D3D9MinimapDepthEligible(depth_test, depth_write)) return false;
    const float width = b.max_x - b.min_x, height = b.max_y - b.min_y;
    return b.min_x >= -1.05f && b.max_x <= -0.40f && b.min_y >= -1.05f && b.max_y <= -0.15f &&
           width < 0.55f && height < 0.75f;
}

inline bool D3D9MinimapDrawMatch(const D3D9NdcBounds& b, int depth_test, int depth_write, bool stencil_enabled) {
    (void)stencil_enabled;  // Both the stencil-write mask and stencil-tested components must move.
    return D3D9MinimapBoundsMatch(b, depth_test, depth_write);
}

inline void D3D9MinimapOffsetNdc(int x, int y, float& dx, float& dy) {
    // protore.ini/Java coordinates are logical 1600x720 HUD pixels. D3D9 NDC y points up, Java/HUD y down.
    dx = float(x) * (2.0f / 1600.0f);
    dy = float(-y) * (2.0f / 720.0f);
}

// Pure3D uploads four float4 constants before its buffer-backed draws. DXSO m4x4 uses row-output constants,
// while other shaders may store columns; the runtime matcher checks both and prefers verified Pure3D rows on ties.
enum class D3D9TransformLayout { Rows, Columns };
struct D3D9ClipPosition {
    float x = 0, y = 0, w = 0;
};

inline D3D9ClipPosition D3D9TransformPosition(const float c[16], D3D9TransformLayout layout,
                                               float x, float y, float z, float w = 1.0f) {
    D3D9ClipPosition out;
    if (layout == D3D9TransformLayout::Rows) {
        out.x = c[0] * x + c[1] * y + c[2] * z + c[3] * w;
        out.y = c[4] * x + c[5] * y + c[6] * z + c[7] * w;
        out.w = c[12] * x + c[13] * y + c[14] * z + c[15] * w;
    } else {
        out.x = c[0] * x + c[4] * y + c[8] * z + c[12] * w;
        out.y = c[1] * x + c[5] * y + c[9] * z + c[13] * w;
        out.w = c[3] * x + c[7] * y + c[11] * z + c[15] * w;
    }
    return out;
}

// Add delta*clip.w to clip.x/clip.y. This is a true post-transform NDC translation, including perspective w.
inline void D3D9TranslateConstants(const float original[16], float translated[16], D3D9TransformLayout layout,
                                   float dx, float dy) {
    float in[16];
    std::memcpy(in, original, sizeof(in));
    std::memcpy(translated, original, sizeof(in));
    if (layout == D3D9TransformLayout::Rows) {
        for (size_t i = 0; i < 4; ++i) {
            translated[i] = in[i] + dx * in[12 + i];
            translated[4 + i] = in[4 + i] + dy * in[12 + i];
        }
    } else {
        for (size_t i = 0; i < 4; ++i) {
            translated[4 * i] = in[4 * i] + dx * in[4 * i + 3];
            translated[4 * i + 1] = in[4 * i + 1] + dy * in[4 * i + 3];
        }
    }
}

// Used only on a temporary host copy of UP vertex data. memcpy avoids alignment UB on packed guest vertex layouts.
inline void D3D9TranslatePretransformedVertices(void* data, size_t count, size_t stride, size_t position_offset,
                                                float pixel_dx, float pixel_dy) {
    auto* bytes = static_cast<uint8_t*>(data);
    for (size_t i = 0; i < count; ++i) {
        float x = 0, y = 0;
        uint8_t* position = bytes + i * stride + position_offset;
        std::memcpy(&x, position, sizeof(x));
        std::memcpy(&y, position + sizeof(x), sizeof(y));
        x += pixel_dx;
        y += pixel_dy;
        std::memcpy(position, &x, sizeof(x));
        std::memcpy(position + sizeof(x), &y, sizeof(y));
    }
}

inline bool D3D9LateLoadingQuad(const D3D9NdcBounds& b, uint32_t backbuffer_width,
                                uint32_t backbuffer_height, uint32_t vertex_count, bool late) {
    if (!late || (vertex_count != 4 && vertex_count != 6) || !backbuffer_width || !backbuffer_height ||
        !D3D9BoundsValid(b)) return false;
    const float width = b.max_x - b.min_x, height = b.max_y - b.min_y;
    if (height < 1.96f || height > 2.04f || std::fabs((b.min_y + b.max_y) * 0.5f) > 0.02f ||
        std::fabs((b.min_x + b.max_x) * 0.5f) > 0.02f) return false;
    const float pixel_width = width * float(backbuffer_width) * 0.5f;
    const float pixel_height = height * float(backbuffer_height) * 0.5f;
    // Compare in pixel space so the same centered 16:9 image is recognized at every wide-screen resolution.
    return std::fabs(pixel_width * 9.0f - pixel_height * 16.0f) <= pixel_height * 0.48f;  // 3% aspect tolerance
}

inline bool D3D9LoadingMaskRequired(bool bink_active, uint32_t depth_write_draws, bool late_loading_quad) {
    return bink_active || depth_write_draws < 20 || late_loading_quad;
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

#endif
