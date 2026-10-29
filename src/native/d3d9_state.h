#ifndef PROTODROID_D3D9_STATE_H
#define PROTODROID_D3D9_STATE_H

#include <cstdint>

namespace native {

// Android's Vulkan WSI path uses FIFO. Keeping the D3D interval stable across CreateDevice and Reset avoids turning
// a cutscene transition into a presentation-mode/swapchain rebuild. Desktop requests remain unchanged.
constexpr uint32_t D3D9PresentationInterval(uint32_t requested, bool android) {
    return android ? 1u : requested;
}

// Numeric D3D9 values keep these helpers independent of the DXVK headers in host tests.
// D3DSAMP_MIN/MAG/MIPFILTER = 5/6/7, MAXANISOTROPY = 10; D3DTEXF_LINEAR/ANISOTROPIC = 2/3.
constexpr uint32_t D3D9EffectiveSamplerValue(uint32_t type, uint32_t requested, int maximum) {
    maximum = maximum < 1 ? 1 : maximum > 16 ? 16 : maximum;
    if (type == 10) return requested < uint32_t(maximum) ? requested : uint32_t(maximum);
    if (maximum == 1 && requested == 3 && (type == 5 || type == 6 || type == 7)) return 2;
    return requested;
}

struct D3D9LockPlan {
    bool copy_in;
    bool write_back;
};

// D3DLOCK_READONLY=0x10 and D3DLOCK_DISCARD=0x2000. NOOVERWRITE does not make old contents undefined, so it still
// copies in. A read-only lock always copies in and never copies back, including when combined with other flags.
constexpr D3D9LockPlan D3D9ShadowLockPlan(uint32_t flags) {
    const bool read_only = (flags & 0x10u) != 0;
    const bool discard = (flags & 0x2000u) != 0;
    return {read_only || !discard, !read_only};
}

// A nonzero ranged VB/IB lock uploads exactly SizeToLock. Only a whole-buffer lock (size zero) uses the remainder.
constexpr uint32_t D3D9BufferLockSize(uint32_t requested, uint32_t offset, uint32_t total) {
    return requested ? requested : total - offset;
}

}  // namespace native

#endif
