// Anti-aliasing compatibility policy shared by the GameDB and D3D9 boundaries.
#pragma once

#include <cstdint>
#include <string>

namespace native {

// game_aa_level keeps its original values. FXAA is appended so existing INI files and Android preference indices
// retain their meaning. The engine itself only understands 0/1/2; FXAA is a ProtoDroid present-pass mode.
constexpr int kAaGameDefault = -1;
constexpr int kAaOff = 0;
constexpr int kAaMsaa2x = 1;
constexpr int kAaMsaa4x = 2;
constexpr int kAaFxaa = 3;

inline bool AaOverrideValid(int value) {
    return value >= kAaOff && value <= kAaFxaa;
}

inline bool AaUsesFxaa(int value) {
    return value == kAaFxaa;
}

inline int AaGameDbValue(int value) {
    // Prototype's built-in MSAA path is associated with missing textures. FXAA must see a single-sampled engine
    // frame; original MSAA values continue through unchanged when explicitly selected.
    return AaUsesFxaa(value) ? kAaOff : value;
}

inline uint32_t AaPresentSampleType(int value, uint32_t requested) {
    return AaUsesFxaa(value) ? 0u : requested;
}

inline uint32_t AaPresentSampleQuality(int value, uint32_t requested) {
    return AaUsesFxaa(value) ? 0u : requested;
}

inline std::string AaDxvkConfig(int value, std::string config) {
    if (!AaUsesFxaa(value)) return config;
    // DXVK's d3d9.forceSwapchainMSAA runs after application present-parameter normalization. Append zero so a stale
    // user override cannot bypass FXAA. DXVK resolves duplicate environment options by the last value.
    for (char& c : config) {
        if (c == ',') c = ';';
    }
    const size_t end = config.find_last_not_of(" \t\r\n");
    if (end != std::string::npos && config[end] != ';') config += ';';
    config += "d3d9.forceSwapchainMSAA = 0";
    return config;
}

}  // namespace native
