#include "render_resolution.h"

#include <cstdio>
#include <cstdlib>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "render resolution test failed: %s\n", message);
        std::exit(1);
    }
}

void Expect(int height, int expected_width, int expected_height) {
    int width = -1, actual_height = -1;
    Check(native::RenderDimensionsForHeight(height, &width, &actual_height), "supported height was rejected");
    Check(width == expected_width && actual_height == expected_height, "16:9 output dimensions differ");
}

}  // namespace

int main() {
    Expect(540, 960, 540);
    Expect(720, 1280, 720);
    Expect(900, 1600, 900);
    Expect(1080, 1920, 1080);
    Expect(721, 1280, 721);  // Keep custom heights reasonable while bounding render width to a valid aligned value.

    int width = 17, height = 23;
    Check(!native::RenderDimensionsForHeight(0, &width, &height), "zero height must be rejected");
    Check(!native::RenderDimensionsForHeight(-720, &width, &height), "negative height must be rejected");
    Check(!native::RenderDimensionsForHeight(4321, &width, &height), "height beyond the 8K-class bound must be rejected");
    Check(!native::RenderDimensionsForHeight(720, nullptr, &height), "null width must be rejected");
    Check(width == 17 && height == 23, "invalid inputs must leave outputs unchanged");
    std::puts("render resolution tests passed");
    return 0;
}
