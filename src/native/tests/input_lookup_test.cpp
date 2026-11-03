#include "input_lookup.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

struct Key {
    int sdl;
    uint8_t scan;
    bool extended;
};

constexpr Key kKeys[] = {
    {4, 0x45, false},   // first physical key for a duplicate DIK
    {7, 0x45, false},   // second physical key for the same DIK
    {9, 0x1c, false},   // ordinary Enter
    {10, 0x1c, true},   // extended keypad Enter is a different DIK
};
constexpr size_t kCount = sizeof(kKeys) / sizeof(kKeys[0]);
constexpr auto kLookup = MakeInputLookup<kCount, 512>(kKeys);

static_assert(kLookup.by_scancode[4] == 1 && kLookup.by_scancode[7] == 2 &&
                  kLookup.by_scancode[9] == 3 && kLookup.by_scancode[10] == 4,
              "scancode indices do not retain key order");
static_assert(kLookup.by_scancode[11] == 0, "unmapped scancode was not empty");
static_assert(kLookup.dik_head[0x45] == 2 && kLookup.dik_next[1] == 1 && kLookup.dik_next[0] == 0,
              "duplicate-DIK chain lost a physical key");
static_assert(kLookup.dik_head[0x1c] == 3 && kLookup.dik_head[0x9c] == 4,
              "extended and ordinary Enter mappings were merged");

bool Linear(uint8_t dik, const uint8_t* state, const uint8_t* injected) {
    for (const Key& key : kKeys) {
        if (uint8_t(key.scan | (key.extended ? 0x80 : 0)) == dik && (state[key.sdl] || injected[key.sdl]))
            return true;
    }
    return false;
}

bool Indexed(uint8_t dik, const uint8_t* state, const uint8_t* injected) {
    for (uint8_t index = kLookup.dik_head[dik]; index; index = kLookup.dik_next[index - 1]) {
        const Key& key = kKeys[index - 1];
        if (state[key.sdl] || injected[key.sdl]) return true;
    }
    return false;
}

void CheckAll(const uint8_t* state, const uint8_t* injected) {
    for (unsigned dik = 0; dik < 256; ++dik) {
        if (Linear(uint8_t(dik), state, injected) != Indexed(uint8_t(dik), state, injected)) {
            std::fprintf(stderr, "input lookup mismatch for DIK %02x\n", dik);
            std::abort();
        }
    }
}

}  // namespace

int main() {
    uint8_t state[512] = {}, injected[512] = {};
    CheckAll(state, injected);
    for (const Key& key : kKeys) {
        state[key.sdl] = 1;
        CheckAll(state, injected);
        state[key.sdl] = 0;
        injected[key.sdl] = 1;
        CheckAll(state, injected);
        injected[key.sdl] = 0;
    }
    // Explicitly cover the duplicate in both orders and the ordinary/extended distinction.
    state[4] = 1;
    if (!Indexed(0x45, state, injected)) return 1;
    state[4] = 0;
    state[7] = 1;
    if (!Indexed(0x45, state, injected)) return 1;
    state[7] = 0;
    state[9] = state[10] = 1;
    if (!Indexed(0x1c, state, injected) || !Indexed(0x9c, state, injected)) return 1;
    std::puts("Input lookup indices, pressed/injected state, duplicate DIKs, and extended keys passed");
}
