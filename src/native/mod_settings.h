#pragma once

#include <map>
#include <string>

#include "mod_platform.h"

namespace native {

using ModSettings = std::map<std::string, std::string, std::less<>>;

bool ModSettingsLoad(const ModEntry& mod, ModSettings* output, std::string* error);
const std::string* ModSettingsValue(const ModSettings& settings, const char* key);

}  // namespace native
