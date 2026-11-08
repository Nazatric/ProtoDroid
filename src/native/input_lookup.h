// Compile-time indices for the fixed SDL/DirectInput keyboard map. One-based key indices keep zero as the empty
// value. A DIK can map to more than one physical key (NumLock/Pause in the established map), so its entries form a
// short chain rather than losing all but one key. DirectInput state is an OR over that chain, independent of order.
#pragma once

#include <cstddef>
#include <cstdint>

template <size_t N, size_t ScancodeCount>
struct InputLookup {
    static_assert(N > 0 && N <= 255, "key indices and chains must fit in one byte");
    uint8_t by_scancode[ScancodeCount] = {};  // one-based key index; zero is unmapped
    uint8_t dik_head[256] = {};               // one-based head of the duplicate-DIK chain
    uint8_t dik_next[N] = {};                 // one-based next key; zero terminates
};

template <size_t N, size_t ScancodeCount, typename Key>
constexpr InputLookup<N, ScancodeCount> MakeInputLookup(const Key (&keys)[N]) {
    InputLookup<N, ScancodeCount> result{};
    for (size_t i = 0; i < N; ++i) {
        const int scancode = int(keys[i].sdl);
        if (scancode >= 0 && size_t(scancode) < ScancodeCount)
            result.by_scancode[scancode] = uint8_t(i + 1);
        const uint8_t dik = uint8_t(keys[i].scan | (keys[i].extended ? 0x80 : 0));
        result.dik_next[i] = result.dik_head[dik];
        result.dik_head[dik] = uint8_t(i + 1);
    }
    return result;
}
