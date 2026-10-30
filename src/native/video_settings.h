// Launcher access to the game's own integer video settings in GameDB.
#pragma once

#include <cstdint>

#include "../recomp/runtime.h"

namespace native {

// Returns 0 = AA, 1 = shadow detail, 2 = graphic quality, or -1 for every unrelated GameDB key.
int VideoSettingIndex(uint32_t low, uint32_t high);

}  // namespace native

// Called by the recompiled GameDB integer getter before its original body. The hook may store a launcher value via
// the engine's own setter; it never replaces the getter or its return value.
extern "C" void RecompVideoSettingRead(Ctx* c);
