// Informational DirectInput keyboard-map microbenchmark. This measures mapping work only, not a whole game frame.
#include "input_lookup.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

struct Key {
    int sdl;
    uint8_t scan;
    bool extended;
};
constexpr size_t kCount = 104, kScancodes = 512;
using Keys = Key[kCount];

__attribute__((noinline)) bool Linear(const Keys& keys, uint8_t dik, const uint8_t* state,
                                      const uint8_t* injected) {
    for (const Key& key : keys) {
        if (uint8_t(key.scan | (key.extended ? 0x80 : 0)) == dik && (state[key.sdl] || injected[key.sdl]))
            return true;
    }
    return false;
}

__attribute__((noinline)) bool Indexed(const InputLookup<kCount, kScancodes>& lookup, const Keys& keys, uint8_t dik,
                                       const uint8_t* state, const uint8_t* injected) {
    for (uint8_t index = lookup.dik_head[dik]; index; index = lookup.dik_next[index - 1]) {
        const Key& key = keys[index - 1];
        if (state[key.sdl] || injected[key.sdl]) return true;
    }
    return false;
}

template <typename F>
double Measure(F&& run) {
    std::vector<double> batches;
    for (int batch = 0; batch < 7; ++batch) {
        const auto start = std::chrono::steady_clock::now();
        run();
        const auto end = std::chrono::steady_clock::now();
        batches.push_back(std::chrono::duration<double, std::micro>(end - start).count());
    }
    std::sort(batches.begin(), batches.end());
    return batches[batches.size() / 2];
}

}  // namespace

int main(int argc, char** argv) {
    Keys keys{};
    for (size_t i = 0; i < kCount; ++i) keys[i] = {int(i), uint8_t(i), false};
    keys[kCount - 1].scan = keys[kCount - 2].scan;  // exercise duplicate-DIK chains in every poll
    const auto lookup = MakeInputLookup<kCount, kScancodes>(keys);
    std::vector<uint8_t> state(kScancodes), injected(kScancodes);

    // Differential coverage includes every physical key as real and injected input, plus all-up keys.
    for (const Key& key : keys) {
        state[key.sdl] = 1;
        for (const Key& query : keys) {
            if (Linear(keys, query.scan, state.data(), injected.data()) !=
                Indexed(lookup, keys, query.scan, state.data(), injected.data()))
                return 1;
        }
        state[key.sdl] = 0;
        injected[key.sdl] = 1;
        for (const Key& query : keys) {
            if (Linear(keys, query.scan, state.data(), injected.data()) !=
                Indexed(lookup, keys, query.scan, state.data(), injected.data()))
                return 1;
        }
        injected[key.sdl] = 0;
    }

    state[7] = argc > 1;      // opaque to the optimizer; almost all keys remain up, as during normal play
    injected[103] = argc > 1;
    const int polls = argc > 1 ? std::atoi(argv[1]) : 10000;
    if (polls <= 0) return 2;
    uint64_t linear_sum = 0, indexed_sum = 0;
    auto linear = [&] {
        uint64_t sum = 0;
        for (int poll = 0; poll < polls; ++poll)
            for (const Key& key : keys) sum += Linear(keys, key.scan, state.data(), injected.data());
        linear_sum += sum;
    };
    auto indexed = [&] {
        uint64_t sum = 0;
        for (int poll = 0; poll < polls; ++poll)
            for (const Key& key : keys) sum += Indexed(lookup, keys, key.scan, state.data(), injected.data());
        indexed_sum += sum;
    };
    linear();
    indexed();
    double linear_us = 0, indexed_us = 0;
    for (int repetition = 0; repetition < 3; ++repetition) {
        linear_us += Measure(linear);
        indexed_us += Measure(indexed);
    }
    if (linear_sum != indexed_sum) {
        std::fprintf(stderr, "output mismatch: %llu != %llu\n", (unsigned long long)linear_sum,
                     (unsigned long long)indexed_sum);
        return 1;
    }
    std::printf(
        "polls=%d lookups=%llu linear=%.3f us/poll indexed=%.3f us/poll reduction=%.2f%% checksum=%llu\n", polls,
        (unsigned long long)polls * kCount, linear_us / 3 / polls, indexed_us / 3 / polls,
        100.0 * (1.0 - indexed_us / linear_us), (unsigned long long)indexed_sum);
}
