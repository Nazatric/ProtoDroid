#include "render_resolution.h"

#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "render resolution test failed: %s\n", message);
        std::exit(1);
    }
}

void Expect(int height, int window_width, int window_height, int expected_width, int expected_height) {
    int width = -1, actual_height = -1;
    Check(native::RenderDimensionsForHeight(height, window_width, window_height, &width, &actual_height),
          "valid render/window dimensions were rejected");
    Check(width == expected_width && actual_height == expected_height, "aspect-preserving output dimensions differ");
}

}  // namespace

int main() {
    // Current user's 2712x1220 fullscreen surface: preserve its ~20:9 aspect rather than stretching 16:9.
    Expect(540, 2712, 1220, 1200, 540);
    Expect(720, 2712, 1220, 1600, 720);
    Expect(900, 2712, 1220, 2000, 900);
    Expect(1080, 2712, 1220, 2400, 1080);

    // Keep the existing 16:9 behavior, including 8-pixel alignment for non-standard heights.
    Expect(720, 1920, 1080, 1280, 720);
    Expect(721, 1920, 1080, 1280, 721);
    // Match the earlier 2460x1080 device case, where 540p aligned to 1232x540.
    Expect(540, 2460, 1080, 1232, 540);
    Expect(4320, 1, 1, 4320, 4320);  // The safety ceiling itself remains a valid square target.

    int width = 17, height = 23;
    Check(!native::RenderDimensionsForHeight(0, 2712, 1220, &width, &height), "zero height must be rejected");
    Check(!native::RenderDimensionsForHeight(-720, 2712, 1220, &width, &height), "negative height must be rejected");
    Check(!native::RenderDimensionsForHeight(4321, 2712, 1220, &width, &height), "height beyond 8K bound rejected");
    Check(!native::RenderDimensionsForHeight(4320, std::numeric_limits<int>::max(), 1, &width, &height),
          "width beyond integer range rejected");
    Check(!native::RenderDimensionsForHeight(720, 0, 1220, &width, &height), "zero window width rejected");
    Check(!native::RenderDimensionsForHeight(720, 2712, 0, &width, &height), "zero window height rejected");
    Check(!native::RenderDimensionsForHeight(720, 2712, 1220, nullptr, &height), "null width rejected");
    Check(width == 17 && height == 23, "invalid inputs must leave outputs unchanged");
    std::puts("aspect-preserving render resolution tests passed");
    return 0;
}
