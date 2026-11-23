#pragma once

#include <map>
#include <string>

namespace native {

using ConfigSettings = std::map<std::string, std::string>;

inline constexpr int kDefaultFpsLimit = 30;
inline constexpr int kBatterySaverFpsLimit = 30;

// A configured zero is an explicit opt-out unless Battery saver is enabled. Other positive caps remain user-owned.
constexpr int ApplyBatterySaverFpsLimit(int requested, bool battery_saver) {
    return battery_saver && (requested == 0 || requested > kBatterySaverFpsLimit)
        ? kBatterySaverFpsLimit : requested;
}

// Apply a higher-priority configuration layer over lower-priority settings. Keys absent from the higher-priority
// layer remain available from the lower-priority configuration.
inline void MergeConfigLayer(ConfigSettings& effective, const ConfigSettings& layer) {
    for (const auto& entry : layer) effective[entry.first] = entry.second;
}

}  // namespace native
