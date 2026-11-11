// Deterministic host throughput check for the production BC decoder. This is a CPU microbenchmark, not a device FPS
// measurement. Build without sanitizers or LTO, then compare the same command at two revisions on the same host:
//   g++ -std=c++17 -O3 -DNDEBUG -Isrc/native src/native/d3d9_dxt.cpp src/native/tests/dxt_benchmark.cpp -o /tmp/dxt-bench
//   /tmp/dxt-bench 400
#include "d3d9_dxt.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

namespace {

constexpr uint32_t FourCC(char a, char b, char c, char d) {
    return uint32_t(uint8_t(a)) | uint32_t(uint8_t(b)) << 8 | uint32_t(uint8_t(c)) << 16 |
           uint32_t(uint8_t(d)) << 24;
}

struct Case {
    const char* name;
    uint32_t format;
    uint32_t output;
    uint32_t pixel_bytes;
};

}  // namespace

int main(int argc, char** argv) {
    const int iterations = argc > 1 ? std::max(std::atoi(argv[1]), 1) : 400;
    constexpr uint32_t kDxt1 = FourCC('D', 'X', 'T', '1'), kDxt3 = FourCC('D', 'X', 'T', '3');
    constexpr uint32_t kDxt5 = FourCC('D', 'X', 'T', '5'), kBgra = FourCC('A', 'R', 'G', 'B');
    const Case cases[] = {
        {"DXT1 -> BGRA", kDxt1, kBgra, 4},
        {"DXT1 -> A1R5G5B5", kDxt1, 25, 2},
        {"DXT3 -> BGRA (regression control)", kDxt3, kBgra, 4},
        {"DXT5 -> BGRA (regression control)", kDxt5, kBgra, 4},
    };
    constexpr uint32_t w = 512, h = 512, bw = w / 4, bh = h / 4;
    constexpr int batches = 7;
    uint64_t digest = 0;
    for (const Case& test : cases) {
        const uint32_t block_bytes = test.format == kDxt1 ? 8 : 16, pitch = bw * block_bytes;
        std::vector<uint8_t> src(pitch * bh), dst(w * h * test.pixel_bytes + 64);
        std::mt19937 random(0x12345678u + test.format + test.output);
        for (uint8_t& value : src) value = uint8_t(random());
        for (int i = 0; i < 20; ++i)
            native::DecodeDxt(test.format, src.data(), pitch, w, h, dst.data(), w * test.pixel_bytes, test.output);

        std::vector<double> times;
        for (int batch = 0; batch < batches; ++batch) {
            const auto begin = std::chrono::steady_clock::now();
            for (int i = 0; i < iterations; ++i) {
                // Keep the workload deterministic but prevent a future benchmark-specific constant-input shortcut.
                src[(i * 977 + batch * 131) % src.size()] ^= uint8_t(i + batch);
                native::DecodeDxt(test.format, src.data(), pitch, w, h, dst.data(), w * test.pixel_bytes, test.output);
                digest += dst[(i * 619 + batch * 277) % dst.size()];
            }
            times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count());
        }
        std::sort(times.begin(), times.end());
        const double median_ms = times[batches / 2], best_ms = times.front();
        const double compressed_mib = double(src.size()) * iterations / (1024 * 1024);
        std::printf("%s: median=%.3fms best=%.3fms throughput=%.1f compressed-MiB/s decode=%.3fus digest=%llu\n",
                    test.name, median_ms, best_ms, compressed_mib / (median_ms / 1000), median_ms * 1000 / iterations,
                    static_cast<unsigned long long>(digest));
    }
    return 0;
}
