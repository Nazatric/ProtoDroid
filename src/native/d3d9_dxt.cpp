// CPU BC1-3 decoder for GPUs without DXT support. The scalar block decoder is shared with the host tests; ARM64
// additionally writes complete tiles four pixels at a time, including the 16-bit path used to save Mali bandwidth.
#include "d3d9_dxt.h"

#include <cstring>

#if defined(__aarch64__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#include <arm_neon.h>
#define PROTODROID_DXT_NEON 1
#endif

namespace native {

namespace {

constexpr uint32_t FourCC(char a, char b, char c, char d) {
    return uint32_t(uint8_t(a)) | uint32_t(uint8_t(b)) << 8 | uint32_t(uint8_t(c)) << 16 |
           uint32_t(uint8_t(d)) << 24;
}
constexpr uint32_t kDxt1 = FourCC('D', 'X', 'T', '1'), kDxt2 = FourCC('D', 'X', 'T', '2');
constexpr uint32_t kDxt3 = FourCC('D', 'X', 'T', '3');
constexpr uint32_t kA1R5G5B5 = 25;

uint32_t BlockBytes(uint32_t f) { return f == kDxt1 ? 8 : 16; }

// The complete 5/6-bit expansion tables take only 96 bytes and stay hot in L1. This removes six constant-division
// sequences per colour block without changing the established floor(v * 255 / max) rounding.
struct ExpandTables {
    uint8_t five[32];
    uint8_t six[64];
};
constexpr ExpandTables MakeExpandTables() {
    ExpandTables tables{};
    for (uint32_t i = 0; i < 32; ++i) tables.five[i] = uint8_t(i * 255 / 31);
    for (uint32_t i = 0; i < 64; ++i) tables.six[i] = uint8_t(i * 255 / 63);
    return tables;
}
constexpr ExpandTables kExpand = MakeExpandTables();

void Rgb565(uint16_t c, uint8_t out[4]) {
    out[2] = kExpand.five[(c >> 11) & 31];
    out[1] = kExpand.six[(c >> 5) & 63];
    out[0] = kExpand.five[c & 31];
    out[3] = 255;
}

uint16_t PackA1R5G5B5(const uint8_t p[4]) {
    return uint16_t((p[3] >= 128 ? 0x8000 : 0) | (p[2] >> 3) << 10 | (p[1] >> 3) << 5 | (p[0] >> 3));
}

void DecodeColorPalette(const uint8_t* b, bool dxt1, uint8_t pal[4][4]) {
    uint16_t c0 = uint16_t(b[0] | b[1] << 8), c1 = uint16_t(b[2] | b[3] << 8);
    Rgb565(c0, pal[0]);
    Rgb565(c1, pal[1]);
    if (c0 > c1 || !dxt1) {
        for (int k = 0; k < 4; ++k) {
            pal[2][k] = uint8_t((2 * pal[0][k] + pal[1][k] + 1) / 3);
            pal[3][k] = uint8_t((pal[0][k] + 2 * pal[1][k] + 1) / 3);
        }
    } else {
        for (int k = 0; k < 4; ++k) pal[2][k] = uint8_t((pal[0][k] + pal[1][k]) / 2);
        pal[3][0] = pal[3][1] = pal[3][2] = pal[3][3] = 0;  // transparent black
    }
}

// Colour block (DXT1 semantics when `dxt1`, else always four colours) -> 16 BGRA pixels.
void DecodeColorBlock(const uint8_t* b, bool dxt1, uint8_t px[16][4]) {
    uint8_t pal[4][4];
    DecodeColorPalette(b, dxt1, pal);
    uint32_t idx = uint32_t(b[4] | b[5] << 8 | b[6] << 16 | uint32_t(b[7]) << 24);
    for (int i = 0; i < 16; ++i) memcpy(px[i], pal[(idx >> (2 * i)) & 3], 4);
}

void DecodeBlock(uint32_t f, const uint8_t* b, uint8_t px[16][4]) {
    if (f == kDxt1) {
        DecodeColorBlock(b, true, px);
    } else if (f == kDxt2 || f == kDxt3) {
        DecodeColorBlock(b + 8, false, px);
        for (int i = 0; i < 16; ++i) px[i][3] = uint8_t(((b[i / 2] >> (4 * (i & 1))) & 15) * 17);
    } else {  // DXT4/5: interpolated alpha
        DecodeColorBlock(b + 8, false, px);
        uint8_t a[8] = {b[0], b[1]};
        if (a[0] > a[1]) {
            for (int i = 1; i < 7; ++i) a[i + 1] = uint8_t(((7 - i) * a[0] + i * a[1] + 3) / 7);
        } else {
            for (int i = 1; i < 5; ++i) a[i + 1] = uint8_t(((5 - i) * a[0] + i * a[1] + 2) / 5);
            a[6] = 0;
            a[7] = 255;
        }
        uint64_t bits = 0;
        for (int i = 0; i < 6; ++i) bits |= uint64_t(b[2 + i]) << (8 * i);
        for (int i = 0; i < 16; ++i) px[i][3] = a[(bits >> (3 * i)) & 7];
    }
}

#ifdef PROTODROID_DXT_NEON
inline void StoreFullTile(const uint8_t px[16][4], uint8_t* dst, int32_t dst_pitch, bool packed16) {
    if (!packed16) {
        for (uint32_t y = 0; y < 4; ++y)
            vst1q_u8(dst + int32_t(y) * dst_pitch, vld1q_u8(px[y * 4]));
        return;
    }
    const uint32x4_t mask = vdupq_n_u32(0xff);
    for (uint32_t y = 0; y < 4; ++y) {
        const uint32x4_t p = vreinterpretq_u32_u8(vld1q_u8(px[y * 4]));
        const uint32x4_t b = vshrq_n_u32(vandq_u32(p, mask), 3);
        const uint32x4_t g = vshlq_n_u32(vshrq_n_u32(vandq_u32(vshrq_n_u32(p, 8), mask), 3), 5);
        const uint32x4_t r = vshlq_n_u32(vshrq_n_u32(vandq_u32(vshrq_n_u32(p, 16), mask), 3), 10);
        const uint32x4_t a = vshlq_n_u32(vshrq_n_u32(p, 31), 15);
        const uint16x4_t v = vmovn_u32(vorrq_u32(vorrq_u32(b, g), vorrq_u32(r, a)));
        vst1_u8(dst + int32_t(y) * dst_pitch, vreinterpret_u8_u16(v));
    }
}
#endif

// Mali uses A1R5G5B5 for DXT1 to reduce upload bandwidth. Decode directly to that representation instead of
// materializing 16 BGRA pixels and narrowing every pixel afterwards. The palette still goes through the same
// 8-bit expansion and interpolation as the generic decoder, so rounding and transparent black remain identical.
void DecodeDxt1To16(const uint8_t* src, uint32_t pitch, uint32_t w, uint32_t h, uint8_t* dst, int32_t dst_pitch) {
    const uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
    uint16_t px[16];
    for (uint32_t by = 0; by < bh; ++by) {
        for (uint32_t bx = 0; bx < bw; ++bx) {
            const uint8_t* block = src + by * pitch + bx * 8;
            uint8_t palette[4][4];
            DecodeColorPalette(block, true, palette);
            uint16_t colors[4];
            for (int i = 0; i < 4; ++i) colors[i] = PackA1R5G5B5(palette[i]);
            const uint32_t idx = uint32_t(block[4] | block[5] << 8 | block[6] << 16 | uint32_t(block[7]) << 24);
            if (bx * 4 + 4 <= w && by * 4 + 4 <= h) {
                uint8_t* out = dst + int32_t(by * 4) * dst_pitch + bx * 8;
                for (uint32_t y = 0; y < 4; ++y) {
                    uint16_t row[4];
                    for (uint32_t x = 0; x < 4; ++x) row[x] = colors[(idx >> (2 * (y * 4 + x))) & 3];
#ifdef PROTODROID_DXT_NEON
                    vst1_u8(out + int32_t(y) * dst_pitch, vreinterpret_u8_u16(vld1_u16(row)));
#else
                    memcpy(out + int32_t(y) * dst_pitch, row, sizeof(row));
#endif
                }
                continue;
            }
            for (int i = 0; i < 16; ++i) px[i] = colors[(idx >> (2 * i)) & 3];
            for (uint32_t y = 0; y < 4 && by * 4 + y < h; ++y) {
                for (uint32_t x = 0; x < 4 && bx * 4 + x < w; ++x)
                    memcpy(dst + int32_t(by * 4 + y) * dst_pitch + (bx * 4 + x) * 2, &px[y * 4 + x], 2);
            }
        }
    }
}

}  // namespace

void DecodeDxt(uint32_t f, const uint8_t* src, uint32_t pitch, uint32_t w, uint32_t h, uint8_t* dst,
               int32_t dst_pitch, uint32_t dst_format) {
    if (f == kDxt1 && dst_format == kA1R5G5B5) {
        DecodeDxt1To16(src, pitch, w, h, dst, dst_pitch);
        return;
    }
    const bool packed16 = dst_format == kA1R5G5B5;
    uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4, bytes = BlockBytes(f);
    uint8_t px[16][4];
    for (uint32_t by = 0; by < bh; ++by) {
        for (uint32_t bx = 0; bx < bw; ++bx) {
            DecodeBlock(f, src + by * pitch + bx * bytes, px);
#ifdef PROTODROID_DXT_NEON
            if (bx * 4 + 4 <= w && by * 4 + 4 <= h) {
                uint8_t* out = dst + int32_t(by * 4) * dst_pitch + bx * (packed16 ? 8 : 16);
                StoreFullTile(px, out, dst_pitch, packed16);
                continue;
            }
#endif
            for (uint32_t y = 0; y < 4 && by * 4 + y < h; ++y) {
                for (uint32_t x = 0; x < 4 && bx * 4 + x < w; ++x) {
                    const uint8_t* p = px[y * 4 + x];
                    if (packed16) {  // B, G, R, A -> A1R5G5B5
                        uint16_t v = PackA1R5G5B5(p);
                        memcpy(dst + int32_t(by * 4 + y) * dst_pitch + (bx * 4 + x) * 2, &v, 2);
                    } else {
                        memcpy(dst + int32_t(by * 4 + y) * dst_pitch + (bx * 4 + x) * 4, p, 4);
                    }
                }
            }
        }
    }
}

}  // namespace native
