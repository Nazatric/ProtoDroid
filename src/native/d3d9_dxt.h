#pragma once

#include <cstdint>

namespace native {

// D3DFORMAT values are passed as their underlying 32-bit values so this decoder also has a small host test harness.
// Guest DXT data (rows of 4x4 blocks) becomes host BGRA, or A1R5G5B5 when dst_format is 25.
void DecodeDxt(uint32_t format, const uint8_t* src, uint32_t pitch, uint32_t width, uint32_t height,
               uint8_t* dst, int32_t dst_pitch, uint32_t dst_format);

}  // namespace native
