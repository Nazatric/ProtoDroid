#pragma once

#include <map>
#include <string>

namespace native {

using ConfigSettings = std::map<std::string, std::string>;

// Apply a higher-priority configuration layer over lower-priority settings. Keys absent from the higher-priority
// layer remain available from the lower-priority configuration.
inline void MergeConfigLayer(ConfigSettings& effective, const ConfigSettings& layer) {
    for (const auto& entry : layer) effective[entry.first] = entry.second;
}

}  // namespace native
