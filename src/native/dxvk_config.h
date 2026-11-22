#pragma once

#include <string>

namespace native {

// The native process is named app_process64/proto-native, so DXVK's built-in
// prototypef.exe profile never matches it. Apply the official Prototype
// workaround after user configuration: duplicate environment options are
// resolved by the last value. This disables broken DF16/DF24 format exposure
// without changing shadow resolution, texture quality, AA, or render scale.
// https://github.com/doitsujin/dxvk/pull/4011
inline std::string PrototypeDxvkConfig(std::string config) {
  for (char& c : config) {
    if (c == ',') c = ';';
  }
  size_t end = config.find_last_not_of(" \t\r\n");
  if (end != std::string::npos && config[end] != ';') config += ';';
  config += "d3d9.supportDFFormats = False";
  return config;
}

}  // namespace native
