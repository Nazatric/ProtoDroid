#include "bink_blit.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

extern "C" {
#include <libavutil/frame.h>
}

#include "guest.h"

namespace native::bink {
namespace {

constexpr uint64_t kGuestLimit = 0x100000000ull;

uint8_t Clamp8(int v) { return static_cast<uint8_t>(std::clamp(v, 0, 255)); }
uint64_t AbsPitch(int32_t pitch) {
    return pitch == std::numeric_limits<int32_t>::min() ? uint64_t(std::numeric_limits<int32_t>::max()) + 1
                                                        : uint64_t(pitch < 0 ? -int64_t(pitch) : pitch);
}

struct Rgb { uint8_t r, g, b, a; };

Rgb ConvertPixel(int y, int cb, int cr, int a) {
    // BT.601 studio-range YUV, 16.16 fixed point. Bink stores U=Cb in plane 1 and V=Cr in plane 2.
    constexpr int ky = 76309, krv = 104597, kgu = 25675, kgv = 53279, kbu = 132201;
    int c = (y - 16) * ky + 32768;
    int u = cb - 128, v = cr - 128;
    return {Clamp8((c + krv * v) >> 16), Clamp8((c - kgu * u - kgv * v) >> 16),
            Clamp8((c + kbu * u) >> 16), static_cast<uint8_t>(a)};
}

bool GuestSpan(uint32_t start, int32_t pitch, uint32_t width, uint32_t height, uint32_t bytes_per_pixel,
               uint64_t row_bytes, uint64_t& first, uint64_t& last) {
    if (!start || !height || !row_bytes) return false;
    uint64_t row_advance = row_bytes;
    if (bytes_per_pixel) {
        if (width > std::numeric_limits<uint32_t>::max() / bytes_per_pixel) return false;
        row_advance = uint64_t(width) * bytes_per_pixel;
    }
    uint64_t abs_pitch = AbsPitch(pitch);
    if (abs_pitch < row_advance) return false;
    int64_t final_offset = int64_t(height - 1) * int64_t(pitch);
    int64_t a = int64_t(start), b = a + final_offset;
    int64_t minimum = std::min(a, b), maximum = std::max(a, b);
    if (minimum < 0 || uint64_t(maximum) > kGuestLimit || row_advance > kGuestLimit - uint64_t(maximum)) return false;
    first = uint64_t(minimum);
    last = uint64_t(maximum) + row_advance;
    return last <= kGuestLimit;
}

bool PlaneSpan(const Plane& p, uint32_t width, uint32_t height, bool required) {
    if (!p.buffer) return !required || width == 0 || height == 0;
    uint64_t first, last;
    return GuestSpan(p.buffer, static_cast<int32_t>(p.pitch), width, height, 1, width, first, last);
}

bool CopyPlane(const uint8_t* src, int src_pitch, const Plane& dst, uint32_t width, uint32_t height,
               uint8_t fill, bool required) {
    if (!dst.buffer) return !required;
    if (!width || !height) return true;
    if (!src && fill == 0) return false;
    if (src && !src_pitch) return false;
    int32_t pitch = static_cast<int32_t>(dst.pitch);
    for (uint32_t row = 0; row < height; ++row) {
        int64_t address = int64_t(dst.buffer) + int64_t(row) * pitch;
        uint8_t* out = Ptr<uint8_t>(static_cast<uint32_t>(address));
        if (src) memcpy(out, src + ptrdiff_t(row) * src_pitch, width);
        else memset(out, fill, width);
    }
    return true;
}

int BytesPerPixel(uint32_t surface) {
    switch (surface & kSurfaceMask) {
        case kSurfaceP8: return 1;
        case kSurface24: case kSurface24R: return 3;
        case kSurface32: case kSurface32R: case kSurface32A: case kSurface32RA: return 4;
        case kSurface4444: case kSurface5551: case kSurface555: case kSurface565:
        case kSurface655: case kSurface664: case kSurfaceYUY2: case kSurfaceUYVY: return 2;
        default: return 0;
    }
}

void Store16(uint8_t* p, uint16_t value) {
    p[0] = uint8_t(value);
    p[1] = uint8_t(value >> 8);
}

void StoreRgb(uint8_t* p, uint32_t surface, const Rgb& px) {
    switch (surface & kSurfaceMask) {
        case kSurfaceP8: p[0] = PaletteIndex(px.r, px.g, px.b); break;
        case kSurface24: p[0] = px.b; p[1] = px.g; p[2] = px.r; break;
        case kSurface24R: p[0] = px.r; p[1] = px.g; p[2] = px.b; break;
        case kSurface32: p[0] = px.b; p[1] = px.g; p[2] = px.r; p[3] = 255; break;
        case kSurface32R: p[0] = px.r; p[1] = px.g; p[2] = px.b; p[3] = 255; break;
        case kSurface32A: p[0] = px.b; p[1] = px.g; p[2] = px.r; p[3] = px.a; break;
        case kSurface32RA: p[0] = px.r; p[1] = px.g; p[2] = px.b; p[3] = px.a; break;
        case kSurface4444: Store16(p, uint16_t(((px.a >> 4) << 12) | ((px.r >> 4) << 8) | ((px.g >> 4) << 4) | (px.b >> 4))); break;
        case kSurface5551: Store16(p, uint16_t(((px.a >> 7) << 15) | ((px.r >> 3) << 10) | ((px.g >> 3) << 5) | (px.b >> 3))); break;
        case kSurface555: Store16(p, uint16_t(((px.r >> 3) << 10) | ((px.g >> 3) << 5) | (px.b >> 3))); break;
        case kSurface565: Store16(p, uint16_t(((px.r >> 3) << 11) | ((px.g >> 2) << 5) | (px.b >> 3))); break;
        case kSurface655: Store16(p, uint16_t(((px.r >> 2) << 10) | ((px.g >> 3) << 5) | (px.b >> 3))); break;
        case kSurface664: Store16(p, uint16_t(((px.r >> 2) << 10) | ((px.g >> 2) << 4) | (px.b >> 4))); break;
        default: break;
    }
}

bool ClipRect(const Public& pub, int32_t pitch, uint32_t dest_height, int bpp, uint32_t& dx, uint32_t& dy,
              uint32_t& sx, uint32_t& sy, uint32_t& sw, uint32_t& sh) {
    if (sx >= pub.width || sy >= pub.height || dy >= dest_height || !sw || !sh) return false;
    sw = std::min(sw, pub.width - sx);
    sh = std::min(sh, pub.height - sy);
    sh = std::min(sh, dest_height - dy);
    uint64_t abs_pitch = AbsPitch(pitch);
    uint32_t capacity = static_cast<uint32_t>(std::min<uint64_t>(abs_pitch / bpp, 0xffffffffu));
    if (dx >= capacity) return false;
    sw = std::min(sw, capacity - dx);
    return sw && sh;
}

bool ValidatePackedDestination(uint32_t dest, int32_t pitch, uint32_t height, uint32_t dx, uint32_t sw, int bpp) {
    uint64_t first, last, ignored_first, ignored_last;
    // Validate the complete allocated rows, not only the rectangle, so an odd negative pitch cannot wrap at the end.
    if (!GuestSpan(dest, pitch, 0, height, 0, AbsPitch(pitch), first, last)) return false;
    uint64_t row_bytes = uint64_t(dx + sw) * bpp;
    return GuestSpan(dest, pitch, 0, height, 0, row_bytes, ignored_first, ignored_last);
}

}  // namespace

uint32_t PaletteColor(unsigned index) {
    index &= 255;
    uint32_t b = 255 * (index >> 6) / 3;
    uint32_t g = 255 * ((index >> 3) & 7) / 7;
    uint32_t r = 255 * (index & 7) / 7;
    return b | (g << 8) | (r << 16);
}
uint8_t PaletteIndex(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint8_t>((r >> 5) | ((g >> 5) << 3) | ((b >> 6) << 6));
}

bool CopyRegisteredFrame(const AVFrame* frame, const Public& pub, FrameBuffers& buffers) {
    if (buffers.total_frames < 1 || buffers.total_frames > 2 || buffers.frame_num >= 2 ||
        buffers.frame_num >= static_cast<uint32_t>(buffers.total_frames) || !pub.width || !pub.height) return false;
    uint32_t cw = (pub.width + 1) / 2, ch = (pub.height + 1) / 2;
    if (buffers.crcb_width < cw || buffers.crcb_height < ch || buffers.ya_width < pub.width ||
        buffers.ya_height < pub.height) return false;
    if (frame && (!frame->data[0] || !frame->data[1] || !frame->data[2])) return false;

    // The guest can call NextFrame and change FrameNum while the originally selected textures are still locked. Copy
    // every populated set, not just the newest index, so UnlockRect flushes this same retained/decoded presentation.
    // A completely unused set is legal; a partially populated or out-of-range set is not. Preflight before any write.
    unsigned populated = 0;
    for (unsigned index = 0; index < static_cast<unsigned>(buffers.total_frames); ++index) {
        const FramePlaneSet& p = buffers.frames[index];
        if (!p.y.buffer && !p.cr.buffer && !p.cb.buffer && !p.alpha.buffer) continue;
        if (!PlaneSpan(p.y, pub.width, pub.height, true) || !PlaneSpan(p.cr, cw, ch, true) ||
            !PlaneSpan(p.cb, cw, ch, true) || !PlaneSpan(p.alpha, pub.width, pub.height, false)) return false;
        ++populated;
    }
    if (!populated) return false;
    for (unsigned index = 0; index < static_cast<unsigned>(buffers.total_frames); ++index) {
        const FramePlaneSet& p = buffers.frames[index];
        if (!p.y.buffer && !p.cr.buffer && !p.cb.buffer && !p.alpha.buffer) continue;
        const uint8_t* y = frame ? frame->data[0] : nullptr;
        const uint8_t* cb = frame ? frame->data[1] : nullptr;
        const uint8_t* cr = frame ? frame->data[2] : nullptr;
        const uint8_t* alpha = frame ? frame->data[3] : nullptr;
        // A missing retained frame is a neutral black presentation, never zero chroma (which renders bright green).
        if (!CopyPlane(y, frame ? frame->linesize[0] : 0, p.y, pub.width, pub.height, 16, true) ||
            !CopyPlane(cr, frame ? frame->linesize[2] : 0, p.cr, cw, ch, 128, true) ||
            !CopyPlane(cb, frame ? frame->linesize[1] : 0, p.cb, cw, ch, 128, true)) return false;
        if (p.alpha.buffer && !CopyPlane(alpha, frame && alpha ? frame->linesize[3] : 0, p.alpha,
                                         pub.width, pub.height, 255, false)) return false;
    }
    return true;
}

bool CopyRectToBuffer(const AVFrame* frame, const Public& pub, uint32_t dest, int32_t dest_pitch,
                      uint32_t dest_height, uint32_t dx, uint32_t dy, uint32_t sx, uint32_t sy,
                      uint32_t sw, uint32_t sh, uint32_t surface) {
    if (!frame || !dest || !dest_pitch || !frame->data[0] || !frame->data[1] || !frame->data[2]) return false;
    surface &= kSurfaceMask;
    if (surface == kSurfaceYV12) {
        if (dest_pitch <= 0 || (dest_pitch & 1)) return false;
        if (!ClipRect(pub, dest_pitch, dest_height, 1, dx, dy, sx, sy, sw, sh)) return false;
        uint64_t y_end = uint64_t(dest) + uint64_t(dest_pitch) * dest_height;
        uint32_t cp = dest_pitch / 2, crows = (dest_height + 1) / 2;
        uint64_t total = uint64_t(dest_pitch) * dest_height + 2ull * cp * crows;
        if (y_end > kGuestLimit || total > kGuestLimit - uint64_t(dest)) return false;
        for (uint32_t row = 0; row < sh; ++row) {
            uint64_t address = uint64_t(dest) + uint64_t(dy + row) * uint32_t(dest_pitch) + dx;
            memcpy(Ptr<uint8_t>(static_cast<uint32_t>(address)),
                   frame->data[0] + ptrdiff_t(sy + row) * frame->linesize[0] + sx, sw);
        }
        uint8_t* vb = Ptr<uint8_t>(static_cast<uint32_t>(y_end));
        uint8_t* ub = vb + ptrdiff_t(cp) * crows;
        uint32_t c0 = dy / 2, c1 = (dy + sh - 1) / 2, k0 = dx / 2, k1 = (dx + sw - 1) / 2;
        for (uint32_t c = c0; c <= c1 && c < crows; ++c) {
            uint32_t drow = std::max(2 * c, dy);
            uint32_t srow = std::min((sy + drow - dy) / 2, (pub.height + 1) / 2 - 1);
            for (uint32_t k = k0; k <= k1 && k < uint32_t(cp); ++k) {
                uint32_t dcol = std::max(2 * k, dx);
                uint32_t scol = std::min((sx + dcol - dx) / 2, (pub.width + 1) / 2 - 1);
                vb[ptrdiff_t(c) * cp + k] = frame->data[2][ptrdiff_t(srow) * frame->linesize[2] + scol];
                ub[ptrdiff_t(c) * cp + k] = frame->data[1][ptrdiff_t(srow) * frame->linesize[1] + scol];
            }
        }
        return true;
    }

    int bpp = BytesPerPixel(surface);
    if (!bpp || !ClipRect(pub, dest_pitch, dest_height, bpp, dx, dy, sx, sy, sw, sh)) return false;
    if (!ValidatePackedDestination(dest, dest_pitch, dest_height, dx, sw, bpp)) return false;
    for (uint32_t row = 0; row < sh; ++row) {
        uint32_t y = sy + row;
        const uint8_t* yr = frame->data[0] + ptrdiff_t(y) * frame->linesize[0];
        const uint8_t* ur = frame->data[1] + ptrdiff_t(y / 2) * frame->linesize[1];
        const uint8_t* vr = frame->data[2] + ptrdiff_t(y / 2) * frame->linesize[2];
        const uint8_t* ar = frame->data[3] ? frame->data[3] + ptrdiff_t(y) * frame->linesize[3] : nullptr;
        int64_t out_address = int64_t(dest) + int64_t(dy + row) * dest_pitch + int64_t(dx) * bpp;
        uint8_t* out = Ptr<uint8_t>(static_cast<uint32_t>(out_address));
        if (surface == kSurfaceYUY2 || surface == kSurfaceUYVY) {
            bool uyvy = surface == kSurfaceUYVY;
            for (uint32_t col = 0; col < sw; ++col) {
                uint32_t x = sx + col;
                uint8_t chroma = ((dx + col) & 1) ? vr[x / 2] : ur[x / 2];
                out[2 * col + (uyvy ? 1 : 0)] = yr[x];
                out[2 * col + (uyvy ? 0 : 1)] = chroma;
            }
        } else {
            for (uint32_t col = 0; col < sw; ++col) {
                uint32_t x = sx + col;
                Rgb px = ConvertPixel(yr[x], ur[x / 2], vr[x / 2], ar ? ar[x] : 255);
                StoreRgb(out + ptrdiff_t(col) * bpp, surface, px);
            }
        }
    }
    return true;
}

bool CopyToBuffer(const AVFrame* frame, const Public& pub, uint32_t dest, int32_t dest_pitch,
                  uint32_t dest_height, uint32_t dest_x, uint32_t dest_y, uint32_t flags) {
    uint32_t scale_x = 1, scale_y = 1;
    switch (flags & kCopyScalingMask) {
        case kCopy2XH: case kCopy2XHI: scale_y = 2; break;
        case kCopy2XW: scale_x = 2; break;
        case kCopy2XWH: case kCopy2XWHI: scale_x = 2; scale_y = 2; break;
        default: break;
    }
    uint32_t surface = flags & kSurfaceMask;
    if (!frame || !pub.width || !pub.height || pub.width > 0xffffffffu / scale_x ||
        pub.height > 0xffffffffu / scale_y) return false;
    uint32_t out_w = pub.width * scale_x, out_h = pub.height * scale_y;
    if (scale_x == 1 && scale_y == 1)
        return CopyRectToBuffer(frame, pub, dest, dest_pitch, dest_height, dest_x, dest_y, 0, 0,
                                out_w, out_h, surface);
    int bpp = BytesPerPixel(surface);
    if (!bpp || surface == kSurfaceYV12 || !dest_pitch || dest_y >= dest_height) return false;
    uint64_t capacity = AbsPitch(dest_pitch) / bpp;
    if (dest_x >= capacity) return false;
    uint32_t rows = std::min(out_h, dest_height - dest_y);
    uint32_t cols = static_cast<uint32_t>(std::min<uint64_t>(out_w, capacity - dest_x));
    uint64_t first, last;
    if (!GuestSpan(dest, dest_pitch, 0, dest_height, 0, AbsPitch(dest_pitch), first, last)) return false;
    // Scaled output uses nearest-neighbour sampling. It preserves exact dimensions and avoids reading beyond the
    // decoded rectangle; the renderer used by the game takes the registered-plane path instead.
    for (uint32_t y = 0; y < rows; ++y) {
        uint32_t sy = std::min(y / scale_y, pub.height - 1);
        const uint8_t* yr = frame->data[0] + ptrdiff_t(sy) * frame->linesize[0];
        const uint8_t* ur = frame->data[1] + ptrdiff_t(sy / 2) * frame->linesize[1];
        const uint8_t* vr = frame->data[2] + ptrdiff_t(sy / 2) * frame->linesize[2];
        const uint8_t* ar = frame->data[3] ? frame->data[3] + ptrdiff_t(sy) * frame->linesize[3] : nullptr;
        int64_t address = int64_t(dest) + int64_t(dest_y + y) * dest_pitch + int64_t(dest_x) * bpp;
        uint8_t* out = Ptr<uint8_t>(static_cast<uint32_t>(address));
        for (uint32_t x = 0; x < cols; ++x) {
            uint32_t sx = std::min(x / scale_x, pub.width - 1);
            if (surface == kSurfaceYUY2 || surface == kSurfaceUYVY) {
                bool uyvy = surface == kSurfaceUYVY;
                uint8_t chroma = ((dest_x + x) & 1) ? vr[sx / 2] : ur[sx / 2];
                out[2 * x + (uyvy ? 1 : 0)] = yr[sx];
                out[2 * x + (uyvy ? 0 : 1)] = chroma;
            } else {
                StoreRgb(out + ptrdiff_t(x) * bpp, surface,
                         ConvertPixel(yr[sx], ur[sx / 2], vr[sx / 2], ar ? ar[sx] : 255));
            }
        }
    }
    return true;
}

}  // namespace native::bink
