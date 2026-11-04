#include "sha1.h"
#include "sha1_util.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "sha1 padding test failed: %s\n", message);
        std::exit(1);
    }
}

std::vector<unsigned char> Data(size_t size) {
    std::vector<unsigned char> data(size);
    for (size_t i = 0; i < size; ++i)
        data[i] = static_cast<unsigned char>(i * 131 + size * 17 + 7 * 37);
    return data;
}

std::string Hex(const unsigned char* bytes, size_t size) {
    static const char digits[] = "0123456789abcdef";
    std::string result(2 * size, '0');
    for (size_t i = 0; i < size; ++i) {
        result[2 * i] = digits[bytes[i] >> 4];
        result[2 * i + 1] = digits[bytes[i] & 15];
    }
    return result;
}

void TestFips() {
    const struct { const char* text; const char* digest; } vectors[] = {
        {"", "da39a3ee5e6b4b0d3255bfef95601890afd80709"},
        {"abc", "a9993e364706816aba3e25717850c26c9cd0d89d"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "84983e441c3bd26ebaae4aa1f95129e5e54670f1"},
    };
    for (const auto& vector : vectors)
        Check(dxvk::Sha1Hash::compute(vector.text, std::strlen(vector.text)).toString() == vector.digest,
              "FIPS vector changed");
    const std::string million(1000000, 'a');
    Check(dxvk::Sha1Hash::compute(million.data(), million.size()).toString() ==
              "34aa973cd4c4daa4f61eeb2bdbad27316534016f",
          "million-a vector changed");
}

void TestBoundaries() {
    const std::pair<size_t, const char*> cases[] = {
        {0, "da39a3ee5e6b4b0d3255bfef95601890afd80709"},
        {1, "3ce0a1af90b6e7a3dd8d45e410884b588ea2d04c"},
        {2, "5e3cb2b6024cc10ed28eca6f7bc1bbd45a4e7a26"},
        {3, "c71aa29b2f44a8fce511f75ee962ec941cbad3fc"},
        {7, "8dc61b5d69a170c924e84863bebf2e9c53bbf870"},
        {8, "5ecf7f658d7dc494bf936e8cd2e35e3643279ca9"},
        {20, "b35a9ac600c8064a79a2ed675960e2166478f7c2"},
        {47, "3b9f098865bc65d6a3d403a4a86835c6f234619b"},
        {48, "56f39b28abb04ba93c12884abfad92779f0fe92d"},
        {55, "c0b8eae01d00ff79dc0b8e35f034edd6bfa54bd5"},
        {56, "65b6943a3d2612cb72b784325fca039291788277"},
        {57, "4790ec28eaaa05dbee668250e9263b73d14db7b0"},
        {63, "15f48cafea3d08098932e4775f01d3dd39a09e3c"},
        {64, "5a29ba67fd5b5a5980ad4205cd8579e7958a968d"},
        {65, "710e0f202b61b5151c7e637d4517a5ef7c11faef"},
        {119, "416192bd28ff087afba847a5f7e598d34f2546fe"},
        {120, "ee4f30b76d75861c51cd8ba79cf76b9aac1557e3"},
        {121, "62db7b190c5157c77c0c84cd70867a671035ef09"},
        {127, "52d29b4afbae00a4f788dcaef2402dc53ac56f64"},
        {128, "4d572ff3fcd33f61924a879988a6dad559218dba"},
        {129, "5702ed5a8c171de987d485eab5d976ae89db8469"},
        {255, "a97a94766d4d281c8c1b9270bf3c8eb5d72b1245"},
        {256, "4dfc81e3307797ad8f0b9242c50e67436d68742f"},
        {511, "20551dd00742249076fe29ef5747d5b47d0a643c"},
        {512, "0840810b703ab060166dc276fe8cf7b5ae1cc6bb"},
        {513, "20856c5ffef2ea22a2a0839ae80ef64dbd0ab738"},
        {1024, "ac597050963660f0f402fd6ee8a8ac9b12b8ce00"},
        {2048, "a9cf6fb69a7437316912c8364086c51733cdc4f3"},
        {4096, "217892178271fd12d1210adb9f696f9cff1e8ea4"},
    };
    for (const auto& [size, expected] : cases) {
        const auto data = Data(size);
        const void* input = size ? static_cast<const void*>(data.data()) : static_cast<const void*>("");
        Check(dxvk::Sha1Hash::compute(input, size).toString() == expected, "boundary digest changed");
        SHA1_CTX context;
        SHA1Init(&context);
        if (size) SHA1Update(&context, data.data(), size);
        unsigned char digest[SHA1_DIGEST_LENGTH];
        SHA1Final(digest, &context);
        Check(Hex(digest, sizeof(digest)) == expected, "direct-C boundary digest changed");
        const SHA1_CTX zero{};
        Check(std::memcmp(&context, &zero, sizeof(context)) == 0, "final context was not wiped");
    }
}

void TestChunksAndContext() {
    for (size_t size = 0; size <= 512; ++size) {
        const auto data = Data(size);
        const void* input = size ? static_cast<const void*>(data.data()) : static_cast<const void*>("");
        const auto single = dxvk::Sha1Hash::compute(input, size);
        for (size_t chunkSize : {size_t(1), size_t(7), size_t(63), size_t(64), size_t(65), size_t(127)}) {
            std::vector<dxvk::Sha1Data> chunks;
            SHA1_CTX context;
            SHA1Init(&context);
            for (size_t offset = 0; offset < size; offset += chunkSize) {
                const size_t count = std::min(chunkSize, size - offset);
                chunks.push_back({data.data() + offset, count});
                SHA1Update(&context, data.data() + offset, count);
            }
            Check(dxvk::Sha1Hash::compute(chunks.size(), chunks.data()) == single, "chunked digest changed");
            if (size > 256) continue;
            const uint64_t originalCount = context.count;
            const size_t used = (originalCount >> 3) & 63;
            const size_t padBytes = (used < 56 ? 64 : 128) - used;
            SHA1Pad(&context);
            Check(context.count == originalCount + padBytes * 8, "padding bit count changed");
            for (size_t i = 0; i < 8; ++i)
                Check(context.buffer[56 + i] == uint8_t(originalCount >> ((7 - i) * 8)),
                      "padding length encoding changed");
            if (used < 56) {
                Check(context.buffer[used] == 0x80, "padding marker changed");
                for (size_t i = used + 1; i < 56; ++i)
                    Check(context.buffer[i] == 0, "padding zero fill changed");
                for (size_t i = 0; i < used; ++i)
                    Check(context.buffer[i] == data[size - used + i], "padding retained message changed");
            } else {
                for (size_t i = 0; i < 56; ++i)
                    Check(context.buffer[i] == 0, "second padding block zero fill changed");
            }
        }
    }
}

}  // namespace

int main() {
    TestFips();
    TestBoundaries();
    TestChunksAndContext();
    std::puts("sha1 padding tests passed (FIPS, boundaries, chunks, context)");
    return 0;
}
