// Host UBSAN tests for production DXT decoding, effective sampler values, D3D9 HUD submission transforms, and loading masks.
#include "d3d9_dxt.h"
#include "d3d9_state.h"

#include <algorithm>
#include <cmath>
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

bool Near(float a, float b, float epsilon = 0.0001f) {
    return std::fabs(a - b) <= epsilon;
}

D3D9NdcBounds QuadBounds(const float constants[16], D3D9TransformLayout layout) {
    const float positions[4][2] = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}};
    D3D9NdcBounds b {1.0e30f, 1.0e30f, -1.0e30f, -1.0e30f};
    for (const auto& p : positions) {
        const D3D9ClipPosition clip = D3D9TransformPosition(constants, layout, p[0], p[1], 0.0f);
        Check(clip.w != 0.0f, "test quad has no clip w");
        const float x = clip.x / clip.w, y = clip.y / clip.w;
        b.min_x = std::min(b.min_x, x);
        b.max_x = std::max(b.max_x, x);
        b.min_y = std::min(b.min_y, y);
        b.max_y = std::max(b.max_y, y);
    }
    return b;
}

void TestMinimapMatching() {
    const D3D9NdcBounds map {-1.0f, -1.0f, -0.5f, -0.3f};
    Check(D3D9MinimapDrawMatch(map, 0, 1, false), "depth-disabled map did not match");
    Check(D3D9MinimapDrawMatch(map, 1, 0, true), "stencil-write circular mask was rejected");
    Check(D3D9MinimapDrawMatch(map, 0, 0, true), "stencil-tested map component was rejected");
    Check(!D3D9MinimapDrawMatch(map, 1, 1, false), "depth-writing world draw matched the minimap");
    Check(!D3D9MinimapDrawMatch(map, -1, -1, false), "unknown depth state matched the minimap");

    const D3D9NdcBounds north_badge {-0.62f, -0.22f, -0.56f, -0.16f};
    const D3D9NdcBounds marker {-0.72f, -0.61f, -0.70f, -0.57f};
    const D3D9NdcBounds thin_ring {-1.02f, -1.02f, -0.48f, -0.28f};
    Check(D3D9MinimapDrawMatch(north_badge, 0, 0, false), "north badge bounds were rejected");
    Check(D3D9MinimapDrawMatch(marker, 0, 0, false), "small marker bounds were rejected");
    Check(D3D9MinimapDrawMatch(thin_ring, 1, 0, true), "ring/stencil bounds were rejected");
    Check(!D3D9MinimapDrawMatch({-0.39f, -1.0f, -0.1f, -0.3f}, 0, 0, false),
          "adjacent health/HUD geometry matched");
    Check(!D3D9MinimapDrawMatch({-1.04f, -1.0f, -0.41f, -0.3f}, 0, 0, false),
          "over-wide bottom-left geometry matched");
    Check(!D3D9MinimapDrawMatch({-1.0f, -0.14f, -0.5f, 0.4f}, 0, 0, false),
          "geometry above the minimap zone matched");
    Check(D3D9MinimapDrawMatch({-0.8f, -1.0f, -0.8f, -0.3f}, 0, 0, false),
          "vertical ring segment was rejected");
    Check(D3D9MinimapDrawMatch({-0.8f, -0.6f, -0.8f, -0.6f}, 0, 0, false),
          "point-sprite marker was rejected");
    Check(!D3D9MinimapDrawMatch({-0.2f, -0.6f, -0.2f, -0.6f}, 0, 0, false),
          "out-of-zone point sprite matched");
    Check(!D3D9MinimapDrawMatch({-1.0f, -1.0f, NAN, -0.3f}, 0, 0, false),
          "non-finite geometry matched");

    float dx = 0, dy = 0;
    D3D9MinimapOffsetNdc(0, 0, dx, dy);
    Check(dx == 0 && dy == 0, "Bottom-Left is not a no-op");
    D3D9MinimapOffsetNdc(0, -450, dx, dy);
    Check(dx == 0 && Near(dy, 1.25f), "Top-Left NDC translation changed");
    D3D9MinimapOffsetNdc(50, 100, dx, dy);
    Check(Near(dx, 0.0625f) && Near(dy, -2.0f * 100.0f / 720.0f), "live offset scaling changed");
}

void TestShaderTranslationAndRestoration() {
    const float rows[16] = {
        0.5f, 0.0f, 0.0f, -0.75f,
        0.0f, 0.5f, 0.0f, -0.60f,
        0.0f, 0.0f, 1.0f,  0.0f,
        0.0f, 0.0f, 0.0f,  1.0f,
    };
    const D3D9NdcBounds original_bounds = QuadBounds(rows, D3D9TransformLayout::Rows);
    Check(D3D9MinimapDrawMatch(original_bounds, 0, 0, false), "shader quad fixture is outside the minimap");

    float columns[16];
    for (size_t r = 0; r < 4; ++r)
        for (size_t c = 0; c < 4; ++c) columns[4 * r + c] = rows[4 * c + r];
    const D3D9NdcBounds column_bounds = QuadBounds(columns, D3D9TransformLayout::Columns);
    Check(Near(column_bounds.min_x, original_bounds.min_x) && Near(column_bounds.max_y, original_bounds.max_y),
          "column-layout fixture does not represent the same transform");

    for (D3D9TransformLayout layout : {D3D9TransformLayout::Rows, D3D9TransformLayout::Columns}) {
        const float* source = layout == D3D9TransformLayout::Rows ? rows : columns;
        float device[16], translated[16];
        std::memcpy(device, source, sizeof(device));
        float dx = 0, dy = 0;
        D3D9MinimapOffsetNdc(50, -100, dx, dy);
        D3D9TranslateConstants(device, translated, layout, dx, dy);
        std::memcpy(device, translated, sizeof(device));  // temporary SetVertexShaderConstantF(0, ..., 4)
        const D3D9NdcBounds moved = QuadBounds(device, layout);
        Check(Near(moved.min_x, original_bounds.min_x + dx) && Near(moved.max_x, original_bounds.max_x + dx) &&
                  Near(moved.min_y, original_bounds.min_y + dy) && Near(moved.max_y, original_bounds.max_y + dy),
              "shader translation is not a uniform NDC offset");
        std::memcpy(device, source, sizeof(device));  // immediate post-draw restoration
        Check(std::memcmp(device, source, sizeof(device)) == 0, "original shader constants were not restored exactly");
    }

    float perspective[16];
    std::memcpy(perspective, rows, sizeof(rows));
    perspective[12] = 0.1f;
    perspective[13] = -0.2f;
    perspective[15] = 1.1f;
    float shifted[16];
    D3D9TranslateConstants(perspective, shifted, D3D9TransformLayout::Rows, 0.2f, -0.3f);
    const D3D9ClipPosition before = D3D9TransformPosition(perspective, D3D9TransformLayout::Rows, 0.3f, 0.4f, 0.2f);
    const D3D9ClipPosition after = D3D9TransformPosition(shifted, D3D9TransformLayout::Rows, 0.3f, 0.4f, 0.2f);
    Check(Near(after.x, before.x + 0.2f * before.w) && Near(after.y, before.y - 0.3f * before.w) &&
              Near(after.w, before.w), "perspective-w translation or w preservation failed");
}

void TestPretransformedVertexCopy() {
    struct Vertex {
        float x, y, z, rhw;
        uint32_t color;
        float u, v;
    };
    const Vertex source[4] = {
        {20.0f, 600.0f, 0.0f, 1.0f, 0x11223344u, 0.0f, 0.0f},
        {180.0f, 600.0f, 0.2f, 1.0f, 0x22334455u, 1.0f, 0.0f},
        {180.0f, 710.0f, 0.4f, 1.0f, 0x33445566u, 1.0f, 1.0f},
        {20.0f, 710.0f, 0.6f, 1.0f, 0x44556677u, 0.0f, 1.0f},
    };
    Vertex temporary[4];
    std::memcpy(temporary, source, sizeof(source));
    D3D9TranslatePretransformedVertices(temporary, 4, sizeof(Vertex), 0, 100.0f, -450.0f);
    for (size_t i = 0; i < 4; ++i) {
        Check(Near(temporary[i].x, source[i].x + 100.0f) && Near(temporary[i].y, source[i].y - 450.0f),
              "UP position copy was not translated");
        Check(temporary[i].z == source[i].z && temporary[i].rhw == source[i].rhw &&
                  temporary[i].color == source[i].color && temporary[i].u == source[i].u &&
                  temporary[i].v == source[i].v,
              "UP translation modified z/rhw/color/UV data");
    }
    Check(source[0].x == 20.0f && source[0].y == 600.0f, "source UP vertices were modified");

    // Packed, deliberately unaligned host storage exercises the memcpy-based accessor under UBSAN.
    std::vector<uint8_t> packed(3 + 4 * sizeof(Vertex) + 5, 0xa5);
    std::memcpy(packed.data() + 3, source, sizeof(source));
    D3D9TranslatePretransformedVertices(packed.data() + 3, 4, sizeof(Vertex), 0, 100.0f, -450.0f);
    Check(packed[0] == 0xa5 && packed[1] == 0xa5 && packed[2] == 0xa5 && packed.back() == 0xa5,
          "UP translation crossed the temporary allocation");
    float x = 0, y = 0;
    std::memcpy(&x, packed.data() + 3, sizeof(x));
    std::memcpy(&y, packed.data() + 3 + sizeof(x), sizeof(y));
    Check(Near(x, 120.0f) && Near(y, 150.0f), "unaligned UP copy was translated incorrectly");
}

void TestLoadingAndPillarboxes() {
    Check(D3D9DepthDrawMayBeWorld(1, 1), "depth-tested world draw was not detected");
    Check(D3D9DepthDrawMayBeWorld(-1, -1), "unknown StateBlock depth was not conservative");
    Check(!D3D9DepthDrawMayBeWorld(0, 1) && !D3D9DepthDrawMayBeWorld(1, 0),
          "2D draw was misclassified as world geometry");
    Check(D3D9LoadingMaskRequired(true, 100, false), "active Bink did not request a mask");
    Check(D3D9LoadingMaskRequired(false, 19, false), "fewer than 20 depth-write draws did not request a mask");
    Check(!D3D9LoadingMaskRequired(false, 20, false), "ordinary 20+ depth-write gameplay was masked");
    Check(D3D9LoadingMaskRequired(false, 100, true), "late loading quad did not request a mask");

    const D3D9NdcBounds centered_16_9 {-0.8f, -1.0f, 0.8f, 1.0f};
    Check(D3D9LateLoadingQuad(centered_16_9, 1600, 720, 4, true), "1600x720 loading quad was not detected");
    Check(D3D9LateLoadingQuad(centered_16_9, 2400, 1080, 6, true), "2400x1080 loading quad was not detected");
    Check(!D3D9LateLoadingQuad(centered_16_9, 1600, 720, 4, false), "early quad was misclassified as late");
    Check(!D3D9LateLoadingQuad(centered_16_9, 1600, 720, 5, true), "non-quad was misclassified");
    Check(!D3D9LateLoadingQuad({-0.7f, -1.0f, 0.9f, 1.0f}, 1600, 720, 4, true),
          "off-centre full-height quad was misclassified");
    Check(!D3D9LateLoadingQuad({-1.0f, -1.0f, 1.0f, 1.0f}, 1600, 720, 4, true),
          "full-width 20:9 gameplay quad was misclassified");
    Check(!D3D9LateLoadingQuad({-0.8f, -0.9f, -0.6f, -0.7f}, 1600, 720, 4, true),
          "small health/UI quad was misclassified");

    D3D9Pillarboxes p {};
    Check(!D3D9WidePillarboxes(1280, 720, p), "16:9 must not be masked");
    Check(!D3D9WidePillarboxes(1280, 800, p), "narrow aspects must not be masked");
    Check(!D3D9WidePillarboxes(0, 720, p), "empty backbuffer was accepted");
    Check(D3D9WidePillarboxes(1600, 720, p), "20:9 pillarboxes missing");
    Check(p.left == 160 && p.right == 1440 && p.content == 1280, "1600x720 pillarbox rectangles changed");
    Check(D3D9WidePillarboxes(2400, 1080, p), "wide 1080p pillarboxes missing");
    Check(p.left == 240 && p.right == 2160 && p.content == 1920, "2400x1080 pillarbox rectangles changed");
    Check(D3D9WidePillarboxes(2712, 1220, p), "POCO screen pillarboxes missing");
    Check(p.left == 272 && p.right == 2440 && p.content == 2168, "integer pillarbox coverage is not deterministic");
}

}  // namespace

int main() {
    try {
        TestDxt();
        TestSamplerTransform();
        TestMinimapMatching();
        TestShaderTranslationAndRestoration();
        TestPretransformedVertexCopy();
        TestLoadingAndPillarboxes();
        std::puts("graphics tests passed (DXT, samplers, D3D9 minimap bounds/transforms/copies/restoration, loading masks)");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "graphics test failed: %s\n", e.what());
        return 1;
    }
}
