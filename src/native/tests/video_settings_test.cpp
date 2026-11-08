// Verifies the exact GameDB keys, ordinary one-shot writes, and repeated FXAA enforcement with mocked guest memory.
#include <algorithm>
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
int g_aa = 3;
int g_shadow = -1;
int g_quality = 3;
Ctx* g_context = nullptr;
bool g_recurse = false;

}  // namespace

namespace native {

int SettingInt(const char* key, int def) {
    if (std::strcmp(key, "game_aa_level") == 0) return g_aa;
    if (std::strcmp(key, "game_shadow_detail") == 0) return g_shadow;
    if (std::strcmp(key, "game_graphic_quality") == 0) return g_quality;
    return def;
}
void Log(const char*, ...) {}
uint32_t GuestCallNested(Ctx*, uint32_t va, std::initializer_list<uint32_t> args, uint32_t ecx) {
    g_call.push_back(va);
    g_call.insert(g_call.end(), args.begin(), args.end());
    g_call.push_back(ecx);
    if (g_recurse) {
        g_recurse = false;
        RecompVideoSettingRead(g_context);
    }
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
    g_context = &c;
    c.ecx = 0x12345678u;
    c.esp = 0x4000u;
    W32(c.esp + 4, 0x8000u);  // key
    W32(c.esp + 8, 3u);       // profile
    W32(c.esp + 12, 2u);      // slot
    W32(0x8000, 0x93abc047u);
    W32(0x8004, 0x4af12b0cu);

    // FXAA maps to engine AA=0 and is enforced again on later reads. A recursive getter from the mocked setter must
    // not recurse or emit a second nested call.
    g_recurse = true;
    RecompVideoSettingRead(&c);
    const std::vector<uint32_t> expected_fxaa = {0x7c7f8680u, 0x8000u, 3u, 0u, 2u, 0x12345678u};
    Check(g_call == expected_fxaa, "FXAA did not map to GameDB AA=0 with the original profile indices");
    Check(!g_recurse, "recursive setter probe did not run");
    Check(c.esp == 0x4000u && c.ecx == 0x12345678u, "hook changed the active guest context");
    RecompVideoSettingRead(&c);
    Check(g_call.size() == expected_fxaa.size() * 2, "FXAA was not reapplied on a later game-menu read");
    Check(std::equal(expected_fxaa.begin(), expected_fxaa.end(), g_call.begin() + expected_fxaa.size()),
          "repeated FXAA write changed its arguments");

    // Switching the launch configuration to explicit AA=2 uses the original one-shot behavior on the next run/read.
    g_aa = 2;
    const size_t before_msaa = g_call.size();
    RecompVideoSettingRead(&c);
    const std::vector<uint32_t> expected_msaa = {0x7c7f8680u, 0x8000u, 3u, 2u, 2u, 0x12345678u};
    Check(g_call.size() == before_msaa + expected_msaa.size(), "explicit AA=2 did not use the engine setter");
    Check(std::equal(expected_msaa.begin(), expected_msaa.end(), g_call.begin() + before_msaa),
          "explicit AA=2 changed its value or profile indices");
    RecompVideoSettingRead(&c);
    Check(g_call.size() == before_msaa + expected_msaa.size(), "ordinary launcher value was applied more than once");

    // Game default and invalid values leave GameDB alone. FXAA's value 3 is AA-only and must not become valid quality.
    W32(0x8000, 0x1b2fcaeeu);
    W32(0x8004, 0x06154ee4u);
    RecompVideoSettingRead(&c);
    Check(g_call.size() == before_msaa + expected_msaa.size(), "Game default overwrote the engine value");
    W32(0x8000, 0x0b4660feu);
    W32(0x8004, 0xe5bba21cu);
    RecompVideoSettingRead(&c);
    Check(g_call.size() == before_msaa + expected_msaa.size(), "out-of-range quality value reached GameDB");

    // VSync and unrelated keys never enter the launcher write path.
    W32(0x8000, 0xa71b1c07u);
    W32(0x8004, 0xd29eb110u);
    RecompVideoSettingRead(&c);
    Check(g_call.size() == before_msaa + expected_msaa.size(), "VSync was intercepted");

    munmap(memory, kSize);
    std::puts("video settings GameDB, repeated-FXAA, recursion, and one-shot tests passed");
    return 0;
}
