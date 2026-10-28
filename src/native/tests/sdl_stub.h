// Small SDL audio ABI stub for standalone bink tests. Production always includes SDL2/SDL.h; this lets the decoder
// and mixer run deterministically on a host without an audio device or SDL development package.
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

using Uint8 = uint8_t;
using SDL_AudioDeviceID = uint32_t;
using SDL_AudioFormat = uint16_t;
using SDL_AudioCallback = void (*)(void*, Uint8*, int);
#define SDLCALL
#define SDL_INIT_AUDIO 0x10u
#define AUDIO_F32SYS 0x8120u
#define SDL_AUDIO_ALLOW_SAMPLES_CHANGE 0x8u
struct SDL_AudioSpec {
    int freq = 0;
    SDL_AudioFormat format = 0;
    uint8_t channels = 0;
    uint8_t silence = 0;
    uint16_t samples = 0;
    uint16_t padding = 0;
    uint32_t size = 0;
    SDL_AudioCallback callback = nullptr;
    void* userdata = nullptr;
};

struct SDLTestAudioState {
    SDL_AudioSpec spec;
    bool paused = true;
    std::vector<uint8_t> last_output;
};
inline SDLTestAudioState g_sdl_test_audio;
inline int g_sdl_test_forced_rate = 0;
inline int SDL_InitSubSystem(uint32_t) { return 0; }
inline const char* SDL_GetError() { return "SDL test stub"; }
inline SDL_AudioDeviceID SDL_OpenAudioDevice(const char*, int, const SDL_AudioSpec* want, SDL_AudioSpec* got, int) {
    g_sdl_test_audio.spec = *want;
    g_sdl_test_audio.spec.freq = g_sdl_test_forced_rate ? g_sdl_test_forced_rate : want->freq;
    g_sdl_test_audio.spec.samples = want->samples;
    if (got) *got = g_sdl_test_audio.spec;
    return 1;
}
inline void SDL_PauseAudioDevice(SDL_AudioDeviceID, int pause) { g_sdl_test_audio.paused = pause != 0; }
inline void SDL_CloseAudioDevice(SDL_AudioDeviceID) { g_sdl_test_audio.spec.callback = nullptr; }
inline void SDLTestPumpAudio(int frames = 1024) {
    if (!g_sdl_test_audio.spec.callback) return;
    g_sdl_test_audio.last_output.assign(static_cast<size_t>(frames) * 2 * sizeof(float), 0);
    g_sdl_test_audio.spec.callback(nullptr, g_sdl_test_audio.last_output.data(), static_cast<int>(g_sdl_test_audio.last_output.size()));
}
inline bool SDLTestOutputNonzero() {
    for (uint8_t b : g_sdl_test_audio.last_output) if (b) return true;
    return false;
}
