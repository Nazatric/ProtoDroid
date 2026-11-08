// protodroid host: loaded into the game as binkw32.dll (a forwarding proxy, see binkw32.def).
// prototypeenginef.dll imports binkw32.dll, so by the time this DllMain runs the engine image is mapped and
// relocated; its own initialisation (EngineInitialize) has not run yet, so hooks are in place before any
// engine code executes.
#include <windows.h>

#include "difftest.h"
#include "hooks.h"
#include "probes.h"
#include "registry.h"

#include "../recomp/bridge.h"

namespace {

bool EngineBuildMatches() {
    auto* base = static_cast<uint8_t*>(protodroid::EngineAddress(0));
    if (!base) {
        protodroid::Log("prototypeenginef.dll is not loaded");
        return false;
    }
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    uint32_t stamp = nt->FileHeader.TimeDateStamp;
    protodroid::Log("engine loaded at %p, TimeDateStamp 0x%08x", base, stamp);
    if (stamp != protodroid::kEngineTimeDateStamp) {
        protodroid::Log("unsupported engine build (expected 0x%08x), no hooks installed", protodroid::kEngineTimeDateStamp);
        return false;
    }
    return true;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        protodroid::Log("protodroid host attached");
        // PROTODROID_NOHOOKS=1 runs the original engine through the same proxy, for A/B comparisons.
        if (GetEnvironmentVariableA("PROTODROID_NOHOOKS", nullptr, 0) != 0) {
            protodroid::Log("PROTODROID_NOHOOKS set, hooks disabled");
        } else if (EngineBuildMatches()) {
            size_t count = 0;
            protodroid::Hook* hooks = protodroid::Registry(&count);
            char recomp_mode[8] = "";
            GetEnvironmentVariableA("PROTODROID_RECOMP", recomp_mode, sizeof(recomp_mode));
            if (lstrcmpA(recomp_mode, "all") == 0) {
                // Whole engine: every enterable function runs its recompiled version; no hand hooks.
                protodroid::InstallAllRecompEntries();
            } else {
                int installed = protodroid::InstallHooks(hooks, count);
                protodroid::Log("%d of %u hooks installed", installed, static_cast<unsigned>(count));
            }
            if (GetEnvironmentVariableA("PROTODROID_DIFFTEST", nullptr, 0) != 0) {
                protodroid::InstallDiffTestTrigger();
            }
            protodroid::InstallProbes();
        }
    }
    return TRUE;
}
