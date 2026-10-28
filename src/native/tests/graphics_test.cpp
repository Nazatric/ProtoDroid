// Host UBSAN tests for the production DXT decoder and the state cache used by the D3D9 bridge.
#include "d3d9_dxt.h"
#include "d3d9_state.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

using namespace native;

namespace {

constexpr uint32_t FourCC(char a, char b, char c, char d) {
    return uint32_t(uint8_t(a)) | uint32_t(uint8_t(b)) << 8 | uint32_t(uint8_t(c)) << 16 |
           uint32_t(uint8_t(d)) << 24;
}
constexpr uint32_t kDxt1 = FourCC('D', 'X', 'T', '1'), kDxt2 = FourCC('D', 'X', 'T', '2');
constexpr uint32_t kDxt3 = FourCC('D', 'X', 'T', '3'), kDxt4 = FourCC('D', 'X', 'T', '4');
constexpr uint32_t kDxt5 = FourCC('D', 'X', 'T', '5'), kBgra = FourCC('A', 'R', 'G', 'B'), kA1R5G5B5 = 25;

void Check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

void Rgb565(uint16_t c, uint8_t out[4]) {
    out[2] = uint8_t(((c >> 11) & 31) * 255 / 31);
    out[1] = uint8_t(((c >> 5) & 63) * 255 / 63);
    out[0] = uint8_t((c & 31) * 255 / 31);
    out[3] = 255;
}

void DecodeColorBlock(const uint8_t* b, bool dxt1, uint8_t px[16][4]) {
    uint16_t c0 = uint16_t(b[0] | b[1] << 8), c1 = uint16_t(b[2] | b[3] << 8);
    uint8_t pal[4][4];
    Rgb565(c0, pal[0]);
    Rgb565(c1, pal[1]);
    if (c0 > c1 || !dxt1) {
        for (int k = 0; k < 4; ++k) {
            pal[2][k] = uint8_t((2 * pal[0][k] + pal[1][k] + 1) / 3);
            pal[3][k] = uint8_t((pal[0][k] + 2 * pal[1][k] + 1) / 3);
        }
    } else {
        for (int k = 0; k < 4; ++k) pal[2][k] = uint8_t((pal[0][k] + pal[1][k]) / 2);
        pal[3][0] = pal[3][1] = pal[3][2] = pal[3][3] = 0;
    }
    uint32_t idx = uint32_t(b[4] | b[5] << 8 | b[6] << 16 | uint32_t(b[7]) << 24);
    for (int i = 0; i < 16; ++i) memcpy(px[i], pal[(idx >> (2 * i)) & 3], 4);
}

void DecodeBlock(uint32_t f, const uint8_t* b, uint8_t px[16][4]) {
    if (f == kDxt1) {
        DecodeColorBlock(b, true, px);
    } else if (f == kDxt2 || f == kDxt3) {
        DecodeColorBlock(b + 8, false, px);
        for (int i = 0; i < 16; ++i) px[i][3] = uint8_t(((b[i / 2] >> (4 * (i & 1))) & 15) * 17);
    } else {
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

void Reference(uint32_t f, const uint8_t* src, uint32_t pitch, uint32_t w, uint32_t h, uint8_t* dst,
               int32_t dst_pitch, uint32_t dst_format) {
    const bool packed16 = dst_format == kA1R5G5B5;
    uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4, bytes = f == kDxt1 ? 8 : 16;
    uint8_t px[16][4];
    for (uint32_t by = 0; by < bh; ++by) {
        for (uint32_t bx = 0; bx < bw; ++bx) {
            DecodeBlock(f, src + by * pitch + bx * bytes, px);
            for (uint32_t y = 0; y < 4 && by * 4 + y < h; ++y) {
                for (uint32_t x = 0; x < 4 && bx * 4 + x < w; ++x) {
                    const uint8_t* p = px[y * 4 + x];
                    if (packed16) {
                        uint16_t v = uint16_t((p[3] >= 128 ? 0x8000 : 0) | (p[2] >> 3) << 10 |
                                              (p[1] >> 3) << 5 | (p[0] >> 3));
                        memcpy(dst + int32_t(by * 4 + y) * dst_pitch + (bx * 4 + x) * 2, &v, 2);
                    } else {
                        memcpy(dst + int32_t(by * 4 + y) * dst_pitch + (bx * 4 + x) * 4, p, 4);
                    }
                }
            }
        }
    }
}

uint32_t Random(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

void TestDxt() {
    uint32_t random = 0x7a31c09du;
    const uint32_t formats[] = {kDxt1, kDxt2, kDxt3, kDxt4, kDxt5};
    for (uint32_t f : formats) {
        for (int iteration = 0; iteration < 300; ++iteration) {
            const uint32_t w = 1 + Random(random) % 19, h = 1 + Random(random) % 19;
            const uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4, bytes = f == kDxt1 ? 8 : 16;
            const uint32_t pitch = bw * bytes + Random(random) % 7;
            std::vector<uint8_t> src(pitch * bh);
            for (uint8_t& v : src) v = uint8_t(Random(random));
            if (iteration == 0) {
                src[1] = src[3] = 0;
                src[0] = src[2] = 0;  // exercise the transparent DXT1 and short DXT5 interpolation branches
            }
            for (uint32_t output : {kBgra, kA1R5G5B5}) {
                const uint32_t pixel = output == kA1R5G5B5 ? 2 : 4;
                const int32_t stride = int32_t(w * pixel + Random(random) % 13);
                std::vector<uint8_t> actual(stride * h + 16, 0xa5), expected(stride * h + 16, 0xa5);
                DecodeDxt(f, src.data(), pitch, w, h, actual.data(), stride, output);
                Reference(f, src.data(), pitch, w, h, expected.data(), stride, output);
                Check(actual == expected, "production DXT output differs from scalar reference");
            }
        }
    }
}

void TestSamplerTransform() {
    Check(D3D9EffectiveSamplerValue(10, 16, 1) == 1, "1x did not clamp MAXANISOTROPY");
    Check(D3D9EffectiveSamplerValue(10, 0, 1) == 0, "zero anisotropy was changed");
    for (uint32_t type : {5u, 6u, 7u}) {
        Check(D3D9EffectiveSamplerValue(type, 3, 1) == 2, "1x anisotropic filter did not become linear");
        Check(D3D9EffectiveSamplerValue(type, 3, 4) == 3, "higher limit removed anisotropic filtering");
        Check(D3D9EffectiveSamplerValue(type, 1, 1) == 1, "point filtering was changed");
    }
    Check(D3D9EffectiveSamplerValue(10, 8, 4) == 4, "4x did not clamp MAXANISOTROPY");
    Check(D3D9EffectiveSamplerValue(10, 2, 8) == 2, "lower anisotropy request was raised");
}

void TestStateCache() {
    D3D9StateCache cache;
    using Kind = D3D9StateCache::Kind;
    Check(!cache.Equal(Kind::Render, 7, 1), "empty render cache hit");
    cache.Store(Kind::Render, 7, 1);
    Check(cache.Equal(Kind::Render, 7, 1), "stored render state missed");
    Check(!cache.Equal(Kind::Render, 7, 2), "changed render state was suppressed");
    Check(!cache.Equal(Kind::Render, 8, 1), "render states alias");

    const uint32_t sampler = D3D9SamplerCacheIndex(2, 10);
    cache.Store(Kind::Sampler, sampler, 4);
    Check(cache.Equal(Kind::Sampler, sampler, 4), "stored sampler state missed");
    const uint32_t stage = D3D9TextureStageCacheIndex(3, 26);
    cache.Store(Kind::TextureStage, stage, 2);
    Check(cache.Equal(Kind::TextureStage, stage, 2), "stored texture-stage state missed");
    Check(!cache.Equal(Kind::TextureStage, stage + 1, 2), "texture-stage states alias");

    cache.Store(Kind::Sampler, D3D9SamplerCacheIndex(16, 0), 1);
    Check(!cache.Equal(Kind::Sampler, D3D9SamplerCacheIndex(16, 0), 1), "invalid sampler index was cached");
    cache.Invalidate();
    Check(!cache.Equal(Kind::Render, 7, 1) && !cache.Equal(Kind::Sampler, sampler, 4) &&
              !cache.Equal(Kind::TextureStage, stage, 2),
          "Reset/StateBlock invalidation left a cached state valid");
}

}  // namespace

int main() {
    try {
        TestDxt();
        TestSamplerTransform();
        TestStateCache();
        std::puts("graphics tests passed (DXT1/2/3/4/5, BGRA/A1R5G5B5, anisotropy, state invalidation)");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "graphics test failed: %s\n", e.what());
        return 1;
    }
}
