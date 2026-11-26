#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace native {

// Prototype Fix 1.8b documents power-of-two overrides from 512 through 8192. Zero means "leave the game unchanged"
// and is handled by the caller rather than accepted as an override here.
constexpr bool ModShadowResolutionSupported(int value) {
    return value == 512 || value == 1024 || value == 2048 || value == 4096 || value == 8192;
}

// Scale the largest original dimension to the requested override. Keep the original aspect ratio and do not invent
// a square map or a larger driver capability. The original 16K sanity bound rejects corrupted output pointers/data.
inline bool ModScaleShadowResolution(int selected, uint32_t width, uint32_t height,
                                     uint32_t* output_width, uint32_t* output_height) {
    if (!ModShadowResolutionSupported(selected) || !output_width || !output_height || !width || !height
        || width > 16384 || height > 16384) {
        return false;
    }
    const uint32_t largest = std::max(width, height);
    const double scale = static_cast<double>(selected) / largest;
    const auto scaled = [scale](uint32_t value) {
        const double result = std::nearbyint(value * scale);
        return static_cast<uint32_t>(std::max(1.0, std::min(8192.0, result)));
    };
    *output_width = scaled(width);
    *output_height = scaled(height);
    return true;
}

// The supported Steam engine is a PE32 image with SizeOfImage 0x01424000. At this RVA it stores RAD_FINAL,
// followed by three NUL bytes. Check both the image size and exact string/padding before writing RAD_RELEASE; unknown
// engine builds remain unchanged even if the image happens to contain a similar string elsewhere.
constexpr size_t kDeveloperMenuSupportedImageSize = 0x01424000;
constexpr size_t kDeveloperMenuBuildTypeRva = 0x00e0f3ac;
inline bool ModPatchDeveloperMenuBuildType(uint8_t* image, size_t image_size) {
    static constexpr char original[] = "RAD_FINAL";
    static constexpr char replacement[] = "RAD_RELEASE";
    if (!image || image_size != kDeveloperMenuSupportedImageSize || kDeveloperMenuBuildTypeRva > image_size
        || sizeof(replacement) > image_size - kDeveloperMenuBuildTypeRva) {
        return false;
    }
    uint8_t* field = image + kDeveloperMenuBuildTypeRva;
    if (std::memcmp(field, original, sizeof(original)) != 0
        || field[sizeof(original)] != 0 || field[sizeof(original) + 1] != 0) {
        return false;
    }
    std::memcpy(field, replacement, sizeof(replacement));
    return true;
}

}  // namespace native
