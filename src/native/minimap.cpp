// Live placement of the existing protohud.gfx minimap movie clip. Only minimapHUD's root _x/_y are changed: all
// children (map/mask, icons, ring, north marker and glow) retain their own transforms. Other movies and the 3D world
// are never touched. Calls run on the engine-service thread, where the game's own GFx wrappers are used.
#include "guest.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>

#ifdef __ANDROID__
#include <jni.h>
#endif

namespace native {

namespace {

// Addresses in the statically recompiled Steam DLL (original 0x10....../0x11...... image -> 0x7c....../0x7d......).
constexpr uint32_t kMovieProvider = 0x7d315c68;       // provider returned by original 0x10864330
constexpr uint32_t kSetMovieValue = 0x7c885d40;       // this=provider+0x2c, (GFxValue by-value, path), ret 0x14
constexpr uint32_t kGetMovieValue = 0x7c885da0;       // this=provider+0x2c, (out, path), ret 8
constexpr uint32_t kNumberType = 4;                   // written by the game's own numeric getter wrapper

std::mutex g_config_mutex;
bool g_configured = false;
std::atomic<int> g_offset_x{0}, g_offset_y{0};
std::atomic<uint32_t> g_generation{0};

[[maybe_unused]] void SetPosition(int x, int y) {
    x = std::clamp(x, -2000, 2000);
    y = std::clamp(y, -2000, 2000);
    {
        std::lock_guard<std::mutex> lock(g_config_mutex);
        g_configured = true;  // Java may supply SharedPreferences before the engine reads protore.ini
        g_offset_x.store(x, std::memory_order_relaxed);
        g_offset_y.store(y, std::memory_order_relaxed);
        g_generation.fetch_add(1, std::memory_order_relaxed);
    }
    Log("minimap: live offset %d,%d logical HUD px", x, y);
}

void EnsureConfigured() {
    std::lock_guard<std::mutex> lock(g_config_mutex);
    if (g_configured) return;
    g_offset_x.store(std::clamp(SettingInt("minimap_x", 0), -2000, 2000), std::memory_order_relaxed);
    g_offset_y.store(std::clamp(SettingInt("minimap_y", 0), -2000, 2000), std::memory_order_relaxed);
    g_configured = true;
    g_generation.fetch_add(1, std::memory_order_relaxed);
}

bool ReadNumber(uint32_t movie, uint32_t out, uint32_t path, double& value) {
    GuestCall(kGetMovieValue, {out, path}, movie);
    if (R32(out) != kNumberType) return false;
    const uint64_t bits = R64(out + 8);
    std::memcpy(&value, &bits, sizeof(value));
    return std::isfinite(value);
}

void WriteNumber(uint32_t movie, uint32_t path, double value) {
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    // GFxValue is 16 bytes: type, reserved, double at +8. The fifth argument is the variable path.
    GuestCall(kSetMovieValue,
              {kNumberType, 0, uint32_t(bits), uint32_t(bits >> 32), path}, movie);
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
    static bool base_valid = false, ever_applied = false;
    static double base_x = 0, base_y = 0;
    static uint32_t seen_generation = 0xffffffffu;
    static uint64_t next_check = 0;
    const bool dirty = generation != seen_generation;
    seen_generation = generation;
    // Preserve the validated stock path exactly: no GFx calls while Bottom-Left is selected and nothing needs restoring.
    if (!ever_applied && offset_x == 0 && offset_y == 0) return;
    if (!dirty && frame < next_check) return;

    next_check = frame + (base_valid ? 60 : 15);  // cheap reload check; JNI changes are still seen on the next frame
    const uint32_t provider = R32(kMovieProvider);
    if (!provider) return;
    const uint32_t movie = provider + 0x2c;

    double x = 0, y = 0;
    if (!ReadNumber(movie, value, path_x, x) || !ReadNumber(movie, value, path_y, y)) return;
    if (!base_valid) {
        // Capture before the first possible write. The stock coordinates are stable across HUD movie reloads.
        base_x = x;
        base_y = y;
        base_valid = true;
        Log("minimap: stock root position %.2f,%.2f logical HUD px", base_x, base_y);
    }

    const double target_x = base_x + offset_x;
    const double target_y = base_y + offset_y;
    // A stock 0,0 configuration is a true no-op unless it is restoring a previously moved minimap.
    if (!ever_applied && target_x == base_x && target_y == base_y) return;
    bool changed = false;
    if (std::fabs(x - target_x) > 0.01) {
        WriteNumber(movie, path_x, target_x);
        changed = true;
    }
    if (std::fabs(y - target_y) > 0.01) {
        WriteNumber(movie, path_y, target_y);
        changed = true;
    }
    // Keep restoration active for one verification poll after a write; a failed write must not disable retries.
    ever_applied = target_x != base_x || target_y != base_y || changed;
    if (changed) Log("minimap: root position %.2f,%.2f", target_x, target_y);
}

}  // namespace native

#ifdef __ANDROID__
extern "C" JNIEXPORT void JNICALL Java_com_protore_game_TouchControls_nativeSetMinimapPosition(
    JNIEnv*, jclass, jint x, jint y) {
    native::SetPosition(int(x), int(y));
}
#endif
