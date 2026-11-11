#pragma once

#include <cstdint>

#include "bink_abi.h"

extern "C" {
struct AVFrame;
}

namespace native::bink {

// Re-reads the live guest structure and copies one decoded frame into every populated set (even if the active set is
// empty), because the guest can advance FrameNum without relocking the original textures. Completely empty sets are
// skipped. A null frame writes neutral Y=16, Cb=Cr=128, alpha=255. Plane order matches BINKFRAMEBUFFERS, not FFmpeg.
bool CopyRegisteredFrame(const AVFrame* frame, const Public& pub, FrameBuffers& buffers);

// CPU compatibility exports. Coordinates are unsigned in the Bink ABI; invalid/empty rectangles fail without writes.
// Both packed-pixel functions accept negative destination pitches and validate the entire touched guest span.
bool CopyRectToBuffer(const AVFrame* frame, const Public& pub, uint32_t dest, int32_t dest_pitch,
                      uint32_t dest_height, uint32_t dx, uint32_t dy, uint32_t sx, uint32_t sy,
                      uint32_t sw, uint32_t sh, uint32_t surface);
bool CopyToBuffer(const AVFrame* frame, const Public& pub, uint32_t dest, int32_t dest_pitch,
                  uint32_t dest_height, uint32_t dest_x, uint32_t dest_y, uint32_t flags);

// RAD's global 8-bit palette: 2 blue, 3 green and 3 red bits, returned as B,G,R,0 dwords.
uint32_t PaletteColor(unsigned index);
uint8_t PaletteIndex(uint8_t r, uint8_t g, uint8_t b);

}  // namespace native::bink
