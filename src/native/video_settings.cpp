// The engine hashes setting names to 64-bit GameDB keys. Its video initialization registers these three settings,
// and ProtoRenderManager reads them through 0x107f87d0 before calling the original AA/shadow/quality render setters.
// On the first read of a recognized key, write an explicitly configured launcher value through GameDB's own integer
// setter (0x107f8680). Subsequent reads, including changes made in the game's Video menu, use the engine unmodified.
#include <atomic>

#include "guest.h"
#include "video_settings.h"

namespace {

struct VideoOverride {
    uint32_t low, high;
    const char* ini_key;
    const char* label;
    std::atomic<bool> checked{false};
};

// Keys produced by the engine's 64-bit string hash (multiply by 0x1003f, then XOR each signed character).
VideoOverride kOverrides[] = {
    {0x93abc047u, 0x4af12b0cu, "game_aa_level", "Anti-Alias Level"},
    {0x1b2fcaeeu, 0x06154ee4u, "game_shadow_detail", "Shadow Detail"},
    {0x0b4660feu, 0xe5bba21cu, "game_graphic_quality", "Graphic Quality"},
};

constexpr uint32_t kGameDbSetInt = native::kEngineBase + 0x007f8680u;

}  // namespace

namespace native {

int VideoSettingIndex(uint32_t low, uint32_t high) {
    for (int i = 0; i < 3; ++i) {
        if (kOverrides[i].low == low && kOverrides[i].high == high) return i;
    }
    return -1;
}

}  // namespace native

extern "C" void RecompVideoSettingRead(Ctx* c) {
    const uint32_t key = native::Arg(c, 0);
    if (!key) return;
    const int index = native::VideoSettingIndex(R32(key), R32(key + 4));
    if (index < 0) return;

    VideoOverride& setting = kOverrides[index];
    // Also prevents recursion if a future engine setter reads the value while committing it.
    if (setting.checked.exchange(true)) return;
    const int value = native::SettingInt(setting.ini_key, -1);
    if (value < 0 || value > 2) return;  // Game default, absent key, or invalid input: leave GameDB untouched.

    // Getter arguments are (key, profile, slot). The setter takes (key, profile, value, slot) and updates GameDB's
    // active value and linked profile through the same routine used by the game.
    const uint32_t profile = native::Arg(c, 1);
    const uint32_t slot = native::Arg(c, 2);
    native::Log("video settings: %s = %d", setting.label, value);
    native::GuestCallNested(c, kGameDbSetInt, {key, profile, uint32_t(value), slot}, c->ecx);
}
