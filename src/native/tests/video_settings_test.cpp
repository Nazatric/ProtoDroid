// Verifies the exact GameDB keys and the launcher hook's one-shot write behavior with mocked guest memory.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>
#include <sys/mman.h>

#include "../video_settings.h"
#include "../guest.h"

namespace {

std::vector<uint32_t> g_call;

}  // namespace

namespace native {

int SettingInt(const char* key, int def) {
    if (std::strcmp(key, "game_aa_level") == 0) return 2;
    if (std::strcmp(key, "game_shadow_detail") == 0) return -1;
    if (std::strcmp(key, "game_graphic_quality") == 0) return 3;
    return def;
}
void Log(const char*, ...) {}
uint32_t GuestCallNested(Ctx*, uint32_t va, std::initializer_list<uint32_t> args, uint32_t ecx) {
    g_call.push_back(va);
    g_call.insert(g_call.end(), args.begin(), args.end());
    g_call.push_back(ecx);
    return 0;
}

}  // namespace native

static void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "video settings test: %s\n", message);
        std::exit(1);
    }
}

int main() {
    using native::VideoSettingIndex;
    Check(VideoSettingIndex(0x93abc047u, 0x4af12b0cu) == 0, "AA key mismatch");
    Check(VideoSettingIndex(0x1b2fcaeeu, 0x06154ee4u) == 1, "shadow key mismatch");
    Check(VideoSettingIndex(0x0b4660feu, 0xe5bba21cu) == 2, "graphic-quality key mismatch");
    Check(VideoSettingIndex(0xa71b1c07u, 0xd29eb110u) == -1, "VSync must remain on the original path");
    Check(VideoSettingIndex(0, 0) == -1, "empty key was accepted");
    Check(VideoSettingIndex(0x93abc047u, 0) == -1, "partial key was accepted");

    constexpr size_t kSize = 0x10000;
    void* memory = mmap(reinterpret_cast<void*>(native::kGuestBase), kSize, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    Check(memory != MAP_FAILED, "could not map mock guest memory");
    Ctx c{};
    c.ecx = 0x12345678u;
    c.esp = 0x4000u;
    W32(c.esp + 4, 0x8000u);  // key
    W32(c.esp + 8, 3u);       // profile
    W32(c.esp + 12, 2u);      // slot

    // Explicit AA=2 writes (setter, key, profile, value, slot, GameDB this) exactly once.
    W32(0x8000, 0x93abc047u);
    W32(0x8004, 0x4af12b0cu);
    RecompVideoSettingRead(&c);
    const std::vector<uint32_t> expected = {0x7c7f8680u, 0x8000u, 3u, 2u, 2u, 0x12345678u};
    Check(g_call == expected, "explicit AA did not use the engine setter with the original profile indices");
    Check(c.esp == 0x4000u && c.ecx == 0x12345678u, "hook changed the active guest context");
    RecompVideoSettingRead(&c);
    Check(g_call.size() == expected.size(), "launcher value was applied more than once");

    // Game default and invalid values leave GameDB alone.
    W32(0x8000, 0x1b2fcaeeu);
    W32(0x8004, 0x06154ee4u);
    RecompVideoSettingRead(&c);
    Check(g_call.size() == expected.size(), "Game default overwrote the engine value");
    W32(0x8000, 0x0b4660feu);
    W32(0x8004, 0xe5bba21cu);
    RecompVideoSettingRead(&c);
    Check(g_call.size() == expected.size(), "out-of-range value reached GameDB");

    // VSync and unrelated keys never enter the launcher write path.
    W32(0x8000, 0xa71b1c07u);
    W32(0x8004, 0xd29eb110u);
    RecompVideoSettingRead(&c);
    Check(g_call.size() == expected.size(), "VSync was intercepted");

    munmap(memory, kSize);
    std::puts("video settings GameDB key and write-hook tests passed");
    return 0;
}
