#include "mod_platform.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "guest.h"
#include "win.h"

namespace native {
namespace {

constexpr uint32_t kAdjustFov = kEngineBase + 0x006dcfe0;
constexpr uint32_t kShadowResolution = kEngineBase + 0x00715980;
constexpr uint32_t kFileOpenExecute = kEngineBase + 0x0064c820;
constexpr uint32_t kButtonStruggle = kEngineBase + 0x000cc6c0;
constexpr uint32_t kSetResult = kEngineBase + 0x0064c780;
constexpr uint32_t kDriveLastError = kEngineBase + 0x0064fb90;
constexpr uint32_t kDriveInfo = kEngineBase + 0x0064fbf0;

ModHookFn g_fov_original = nullptr;
ModHookFn g_shadow_original = nullptr;
ModHookFn g_file_open_original = nullptr;
ModHookFn g_button_original = nullptr;

void FovDetour(Ctx* c) {
    static const int offset = SettingInt("fov_offset", 0);
    uint32_t argument = c->esp + 4;
    float fov = SRF32(argument) + static_cast<float>(offset);
    fov = std::max(30.0f, std::min(150.0f, fov));
    WF32(argument, fov);
    g_fov_original(c);
}

void ShadowDetour(Ctx* c) {
    static const int selected = SettingInt("shadow_map_resolution", 0);
    const uint32_t entry_esp = c->esp;
    g_shadow_original(c);
    uint32_t width_pointer = SR32(entry_esp + 8);
    uint32_t height_pointer = SR32(entry_esp + 12);
    if (!width_pointer || !height_pointer) return;
    uint32_t width = R32(width_pointer), height = R32(height_pointer);
    if (!width || !height || width > 16384 || height > 16384) return;
    uint32_t largest = std::max(width, height);
    if (largest == static_cast<uint32_t>(selected)) return;
    double scale = static_cast<double>(selected) / largest;
    uint32_t new_width = static_cast<uint32_t>(std::max(256.0, std::min(4096.0, std::nearbyint(width * scale))));
    uint32_t new_height = static_cast<uint32_t>(std::max(256.0, std::min(4096.0, std::nearbyint(height * scale))));
    W32(width_pointer, new_width);
    W32(height_pointer, new_height);
    Log("mods: shadow resolution %ux%u -> %ux%u (ratio preserved)", width, height, new_width, new_height);
}

void FileOpenDetour(Ctx* c) {
    const uint32_t self = c->ecx;
    const uint32_t file = self ? R32(self + 8) : 0;
    const uint32_t search = self ? R32(self + 0x14) : 1;
    if (!file || search == 1 || !ModFilesHasLooseCandidate(Str(file + 0x28))) {
        g_file_open_original(c);
        return;
    }
    const uint32_t drive = R32(file + 0x0c);
    const uint32_t open = drive ? R32(R32(drive) + 10 * 4) : 0;
    if (!open || !RecompLookup(open) || !RecompLookup(kSetResult) || !RecompLookup(kDriveLastError)
        || !RecompLookup(kDriveInfo)) {
        Log("mods: loose-open fallback for %08x: incomplete guest drive table", open);
        g_file_open_original(c);
        return;
    }
    // FileRequest::file +0x08; File +0x28 name, +0x168 host handle, +0x134 size. These are the same fields used by
    // the reconstructed original, so no host-owned handle is exposed to the guest.
    const uint32_t saved_handle = R32(file + 0x168);
    const uint32_t saved_size = R32(file + 0x134);
    uint32_t result = GuestCallNested(c, open,
        {file + 0x28, R32(self + 0x0c), R8(self + 0x10), file + 0x168, file + 0x134}, drive);
    if (result != 1) {
        // A failed drive open may have written either output argument. Restore both before the untouched original
        // performs its archive search/open; other request fields and the outer register context were not changed.
        W32(file + 0x168, saved_handle);
        W32(file + 0x134, saved_size);
        g_file_open_original(c);
        return;
    }
    W8(file + 0x26, 1);
    uint32_t info = GuestCallNested(c, kDriveInfo, {}, drive);
    if (info) W32(file + 0x170, R32(info + 0x0c));
    uint32_t status = GuestCallNested(c, kDriveLastError, {}, drive);
    result = GuestCallNested(c, kSetResult, {result, status}, self);
    static unsigned logged = 0;
    if (logged++ < 256) Log("mods: loose-open %s", Str(file + 0x28));
    Return(c, result);
}

void ButtonDetour(Ctx* c) {
    const uint32_t action = c->ecx;
    const uint32_t argument = c->esp + 8;
    const float original = SRF32(argument);
    static thread_local uint32_t previous_action = 0;
    static thread_local std::chrono::steady_clock::time_point previous;
    const auto now = std::chrono::steady_clock::now();
    if (action != previous_action) {
        previous_action = action;
        previous = now;
        g_button_original(c);
        return;
    }
    double elapsed = std::chrono::duration<double>(now - previous).count();
    previous = now;
    // The original 15 ms gate yields 66.67 calls/sec. Preserve the original per-call amount over real time when that
    // gate is removed. This is deliberately a ratio, so the guest's milliseconds/seconds unit is not assumed.
    if (std::isfinite(original) && elapsed > 0.0 && elapsed < 0.25) {
        float scale = static_cast<float>(std::max(0.05, std::min(4.0, elapsed * (200.0 / 3.0))));
        WF32(argument, original * scale);
    }
    g_button_original(c);
}

void UnlockDeveloperMenu() {
    if (SettingInt("unlock_dev_menu", 0) == 0) return;
    constexpr char original[] = "RAD_FINAL";
    constexpr char replacement[] = "RAD_RELEASE";
    uint32_t found = 0;
    unsigned count = 0;
    if (g_engine_hi > g_engine_lo + 16) {
        for (uint32_t address = g_engine_lo; address + 16 < g_engine_hi; ++address) {
            if (R8(address) != 'R' || memcmp(Ptr<void>(address), original, sizeof(original)) != 0) continue;
            // RAD_RELEASE is two bytes longer. Require two further zero bytes so no adjacent data is overwritten.
            if (R8(address + 10) != 0 || R8(address + 11) != 0) continue;
            found = address;
            ++count;
        }
    }
    if (count != 1) {
        Log("mods: developer-menu patch refused; guarded RAD_FINAL candidates=%u", count);
        return;
    }
    memcpy(Ptr<void>(found), replacement, sizeof(replacement));
    Log("mods: developer-menu build type changed at %08x", found);
}

}  // namespace

void ModFeaturesInitialize() {
    if (SettingInt("fov_offset", 0) != 0) {
        ModHookInstall(kAdjustFov, FovDetour, &g_fov_original);
    }
    int shadow = SettingInt("shadow_map_resolution", 0);
    if (shadow == 1024 || shadow == 2048 || shadow == 4096) {
        ModHookInstall(kShadowResolution, ShadowDetour, &g_shadow_original);
    } else if (shadow != 0) {
        Log("mods: shadow_map_resolution=%d rejected; use 1024, 2048 or 4096", shadow);
    }
    if (ModFilesHaveOverlays() || SettingInt("prefer_loose_files", 0) != 0) {
        ModHookInstall(kFileOpenExecute, FileOpenDetour, &g_file_open_original);
    }
    if (SettingInt("remove_tick_rate_limit", 0) != 0) {
        Log("mods: 15 ms simulation gate disabled; button timing fix=%d",
            SettingInt("fix_button_tap_tick_rate", 1));
        if (SettingInt("fix_button_tap_tick_rate", 1) != 0) {
            ModHookInstall(kButtonStruggle, ButtonDetour, &g_button_original);
        }
    }
    UnlockDeveloperMenu();
}

}  // namespace native
