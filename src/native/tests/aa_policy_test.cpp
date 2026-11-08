#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <cstdlib>

#include "../aa_policy.h"

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "AA policy test: %s\n", message);
        std::exit(1);
    }
}

}  // namespace

int main() {
    using namespace native;
    Check(kAaGameDefault == -1 && kAaOff == 0 && kAaMsaa2x == 1 && kAaMsaa4x == 2,
          "original engine AA values changed");
    Check(kAaFxaa == 3, "FXAA reused an original engine value");
    Check(!AaOverrideValid(kAaGameDefault), "Game default became an explicit override");
    Check(AaOverrideValid(kAaOff) && AaOverrideValid(kAaMsaa2x) && AaOverrideValid(kAaMsaa4x) &&
              AaOverrideValid(kAaFxaa),
          "valid AA override rejected");
    Check(!AaOverrideValid(4) && !AaOverrideValid(-2), "out-of-range AA override accepted");

    Check(AaGameDbValue(kAaFxaa) == kAaOff, "FXAA did not disable built-in AA");
    Check(AaGameDbValue(kAaOff) == kAaOff && AaGameDbValue(kAaMsaa2x) == kAaMsaa2x &&
              AaGameDbValue(kAaMsaa4x) == kAaMsaa4x,
          "ordinary GameDB AA value changed");
    Check(AaUsesFxaa(kAaFxaa) && !AaUsesFxaa(kAaMsaa4x) && !AaUsesFxaa(kAaGameDefault),
          "FXAA mode detection is wrong");

    for (int mode : {kAaGameDefault, kAaOff, kAaMsaa2x, kAaMsaa4x}) {
        Check(AaPresentSampleType(mode, 4) == 4 && AaPresentSampleQuality(mode, 7) == 7,
              "non-FXAA present samples were modified");
    }
    Check(AaPresentSampleType(kAaFxaa, 4) == 0 && AaPresentSampleQuality(kAaFxaa, 7) == 0,
          "FXAA present samples were not normalized");
    Check(AaPresentSampleType(kAaFxaa, 0) == 0 && AaPresentSampleQuality(kAaFxaa, 0) == 0,
          "single-sampled FXAA present changed");

    const std::string custom = "d3d9.numBackBuffers = 3, d3d9.forceSwapchainMSAA = 4";
    Check(AaDxvkConfig(kAaMsaa4x, custom) == custom, "ordinary MSAA changed custom DXVK configuration");
    Check(AaDxvkConfig(kAaFxaa, "") == "d3d9.forceSwapchainMSAA = 0",
          "empty FXAA DXVK configuration is malformed");
    const std::string pinned = AaDxvkConfig(kAaFxaa, custom);
    Check(pinned == "d3d9.numBackBuffers = 3; d3d9.forceSwapchainMSAA = 4;d3d9.forceSwapchainMSAA = 0",
          "FXAA did not append the final forced-MSAA=0 override");
    Check(AaDxvkConfig(kAaFxaa, "d3d9.presentInterval = 1;") ==
              "d3d9.presentInterval = 1;d3d9.forceSwapchainMSAA = 0",
          "FXAA added a duplicate option separator");

    std::puts("AA GameDB, present-sample, and DXVK-override policy tests passed");
    return 0;
}
