#include "mod_feature_policy.h"

#include <cstdio>
#include <cstdlib>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "mod feature policy test failed: %s\n", message);
        std::exit(1);
    }
}

void Dimensions(int selected, uint32_t width, uint32_t height, uint32_t expected_width, uint32_t expected_height,
                const char* message) {
    uint32_t actual_width = 0, actual_height = 0;
    Check(native::ModScaleShadowResolution(selected, width, height, &actual_width, &actual_height), message);
    Check(actual_width == expected_width && actual_height == expected_height, message);
}

}  // namespace

int main() {
    using native::ModScaleShadowResolution;
    using native::ModShadowResolutionSupported;
    Check(!ModShadowResolutionSupported(0), "zero must preserve the original instead of installing an override");
    Check(!ModShadowResolutionSupported(256) && !ModShadowResolutionSupported(6000)
              && !ModShadowResolutionSupported(16384) && !ModShadowResolutionSupported(-512),
          "unsupported shadow values");
    for (int value : {512, 1024, 2048, 4096, 8192}) Check(ModShadowResolutionSupported(value), "documented range");

    Dimensions(512, 2048, 1024, 512, 256, "512 downscale retains ratio");
    Dimensions(8192, 2048, 1024, 8192, 4096, "8192 upscale retains ratio");
    Dimensions(4096, 1024, 2048, 2048, 4096, "portrait map uses largest dimension");
    Dimensions(512, 16384, 256, 512, 8, "small secondary dimension is not forced to 256");
    Dimensions(2048, 2048, 1024, 2048, 1024, "same-size override is stable");

    uint32_t width = 7, height = 9;
    Check(!ModScaleShadowResolution(0, 1024, 1024, &width, &height), "zero is not an override");
    Check(!ModScaleShadowResolution(6000, 1024, 1024, &width, &height), "invalid override rejected");
    Check(!ModScaleShadowResolution(8192, 0, 1024, &width, &height), "zero dimension rejected");
    Check(!ModScaleShadowResolution(8192, 16385, 1024, &width, &height), "corrupt dimension rejected");
    Check(!ModScaleShadowResolution(8192, 1024, 1024, nullptr, &height), "null output rejected");
    Check(width == 7 && height == 9, "failed calls leave outputs untouched");
    std::puts("mod feature policy tests passed");
    return 0;
}
