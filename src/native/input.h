// Scripted input at the ordinary SDL -> Win32/DirectInput boundary. Used by the explicit diagnostic tour and tests.
#pragma once

#include <SDL2/SDL.h>

namespace native {

void InjectKey(SDL_Scancode sc, bool down);
void InjectMouseButton(uint8_t button, bool down);
void InjectMouseMotion(int32_t dx, int32_t dy);
void ReleaseInjectedInput();

}  // namespace native
