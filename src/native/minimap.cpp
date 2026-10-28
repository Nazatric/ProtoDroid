// Live placement of the existing protohud.gfx minimap movie clip. Only minimapHUD's root _x/_y are changed: all
// children (map/mask, icons, ring, north marker and glow) retain their own transforms. Other movies and the 3D world
// are never touched. Calls run on the engine-service thread, where the game's own GFx wrappers are used.
#include "guest.h"
#include "minimap_logic.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>

#ifdef __ANDROID__
#include <jni.h>
#endif

namespace native {

namespace {

// Addresses in the statically recompiled Steam DLL (original 0x10....../0x11...... image -> 0x7c....../0x7d......).
constexpr uint32_t kGetMovieProvider = 0x7c864330;    // game's own provider accessor, no raw global-pointer reads
constexpr uint32_t kSetMovieValue = 0x7c885d40;       // this=provider+0x2c, (GFxValue by-value, path), ret 0x14
constexpr uint32_t kGetMovieValue = 0x7c885da0;       // this=provider+0x2c, (out, path), ret 8

std::mutex g_config_mutex;
std::atomic<bool> g_configured{false};
std::atomic<int> g_offset_x{0}, g_offset_y{0};
std::atomic<uint32_t> g_generation{0};

[[maybe_unused]] void SetPosition(int x, int y) {
    x = std::clamp(x, -2000, 2000);
    y = std::clamp(y, -2000, 2000);
    {
        std::lock_guard<std::mutex> lock(g_config_mutex);
        g_offset_x.store(x, std::memory_order_relaxed);
        g_offset_y.store(y, std::memory_order_relaxed);
        g_configured.store(true, std::memory_order_release);  // Java preferences may precede protore.ini startup
        g_generation.fetch_add(1, std::memory_order_relaxed);
    }
    Log("minimap: live offset %d,%d logical HUD px", x, y);
}

void EnsureConfigured() {
    if (g_configured.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lock(g_config_mutex);
    if (g_configured.load(std::memory_order_relaxed)) return;
    g_offset_x.store(std::clamp(SettingInt("minimap_x", 0), -2000, 2000), std::memory_order_relaxed);
    g_offset_y.store(std::clamp(SettingInt("minimap_y", 0), -2000, 2000), std::memory_order_relaxed);
    g_configured.store(true, std::memory_order_release);
    g_generation.fetch_add(1, std::memory_order_relaxed);
}

bool ReadFloat(uint32_t movie, uint32_t out, uint32_t path, float& value) {
    GuestCall(kGetMovieValue, {out, path}, movie);
    // Generated getter f_7c884bd0 proves type 4 returns only the low payload word. Its wrapper's +12 word is
    // uninitialised stack data, so interpreting +8 as a double produces values such as 6.8e284.
    return MinimapDecodeFloat(R32(out), R32(out + 8), value) && MinimapCoordinateValid(value);
}

void WriteFloat(uint32_t movie, uint32_t path, float value) {
    // GFxValue is 16 bytes: type, reserved, 32-bit float payload at +8, reserved at +12. The path is argument five.
    GuestCall(kSetMovieValue, {kMinimapGFxFloatType, 0, MinimapEncodeFloat(value), 0, path}, movie);
}

bool ReadPosition(uint32_t movie, uint32_t scratch, uint32_t path_x, uint32_t path_y, float& x, float& y) {
    return ReadFloat(movie, scratch, path_x, x) && ReadFloat(movie, scratch, path_y, y);
}

}  // namespace

void MinimapService(uint64_t frame) {
    EnsureConfigured();
    static uint32_t path_x = 0, path_y = 0, value = 0;
    if (!path_x) {
        path_x = GuestStrdup("_level0.mcAnchor.minimapHUD._x");
        path_y = GuestStrdup("_level0.mcAnchor.minimapHUD._y");
        value = HeapAlloc(16);
    }

    const uint32_t generation = g_generation.load(std::memory_order_relaxed);
    const int offset_x = g_offset_x.load(std::memory_order_relaxed);
    const int offset_y = g_offset_y.load(std::memory_order_relaxed);
    static bool base_valid = false, moved = false, matched_logged = false, retry_logged = false;
    static float base_x = 0, base_y = 0;
    static uint32_t seen_generation = 0xffffffffu;
    static uint64_t next_check = 0;
    const bool dirty = generation != seen_generation;
    if (!dirty && frame < next_check) return;
    seen_generation = generation;

    // Preserve the stock path exactly: no GFx calls while Bottom-Left is selected and nothing needs restoring.
    if (!moved && offset_x == 0 && offset_y == 0) return;
    next_check = frame + (base_valid ? 60 : 15);  // reload check; JNI changes are still seen on the next frame

    const uint32_t provider = GuestCall(kGetMovieProvider, {});
    if (!provider) return;
    const uint32_t movie = provider + 0x2c;

    float x = 0, y = 0;
    if (!ReadPosition(movie, value, path_x, path_y, x, y)) {
        if (!matched_logged && !retry_logged) {
            Log("minimap: waiting for finite type-4 GFx root floats (invalid/stale values are not written)");
            retry_logged = true;
        }
        return;
    }
    if (!base_valid) {
        // Capture before the first possible write. Logical coordinates are stable across HUD movie reloads.
        base_x = x;
        base_y = y;
        base_valid = true;
        Log("minimap: stock root position %.2f,%.2f logical HUD px", base_x, base_y);
    }

    const float target_x = base_x + offset_x;
    const float target_y = base_y + offset_y;
    if (!MinimapCoordinateValid(target_x) || !MinimapCoordinateValid(target_y)) return;
    bool changed = false;
    if (!MinimapCoordinateNear(x, target_x)) {
        WriteFloat(movie, path_x, target_x);
        changed = true;
    }
    if (!MinimapCoordinateNear(y, target_y)) {
        WriteFloat(movie, path_y, target_y);
        changed = true;
    }

    if (changed) {
        // Read back the actual movie values, not merely the requested setting. A failed write remains retryable.
        if (!ReadPosition(movie, value, path_x, path_y, x, y) ||
            !MinimapCoordinateNear(x, target_x) || !MinimapCoordinateNear(y, target_y)) {
            next_check = frame + 1;
            if (!retry_logged) {
                Log("minimap: GFx root readback mismatch; retrying without touching other HUD elements");
                retry_logged = true;
            }
            return;
        }
        retry_logged = false;
    }

    moved = target_x != base_x || target_y != base_y;
    if (!matched_logged && (changed || moved)) {
        Log("minimap: live GFx render root verified %.2f,%.2f (offset %d,%d); all minimap children move together",
            x, y, offset_x, offset_y);
        matched_logged = true;
    } else if (changed) {
        Log("minimap: root position verified %.2f,%.2f", x, y);
    }
}

}  // namespace native

#ifdef __ANDROID__
extern "C" JNIEXPORT void JNICALL Java_com_protore_game_TouchControls_nativeSetMinimapPosition(
    JNIEnv*, jclass, jint x, jint y) {
    native::SetPosition(int(x), int(y));
}
#endif
