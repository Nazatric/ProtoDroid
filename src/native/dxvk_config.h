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

// Frame-pacing policy for the single-main-thread engine: a 24 ms engine frame that runs uncapped into a
// deep FIFO shows beat-frequency stutter on 120 Hz panels, and a deep queue adds input latency. One queued
// presenter frame (maxFrameLatency = 1, the setting DXVK itself ships for janky-frame-time D3D9 games)
// keeps the queue from stuffing; an explicit maxFrameRate equal to the host limiter keeps DXVK from adding
// a second, mismatched cap. fps_limit = 0 leaves the limiter to FIFO at the display refresh rate.
// Configured by main.cpp from dxvk_max_frame_latency (default 2 since 0.17: two frames in flight overlap
// the next frame's CPU work with the current one's GPU work, so the frame rate is max(CPU, GPU) instead of
// CPU+GPU; 0 = DXVK default behavior, 1 = the old minimum-latency serialization).
inline std::string FramePacingDxvkConfig(std::string config, int max_frame_latency, int fps_limit) {
  size_t end = config.find_last_not_of(" \t\r\n");
  if (end != std::string::npos && config[end] != ';') config += ';';
  if (max_frame_latency > 0) config += "d3d9.maxFrameLatency = " + std::to_string(max_frame_latency) + ";";
  if (fps_limit > 0) config += "d3d9.maxFrameRate = " + std::to_string(fps_limit) + ";";
  return config;
}

// Dynamic vertex/index/texture buffers: the engine locks roughly a hundred per frame (profile data), and
// mobile drivers often map HOST_COHERENT memory write-combined, which makes the engine's lock-unlock-memcpy
// pattern slow. cachedDynamicBuffers backs dynamic buffers with cached host memory instead. On by default
// (an upstream DXVK option, designed exactly for integrated GPUs); dxvk_cached_dynamic_buffers = 0 in
// protodroid.ini restores the previous behavior.
inline std::string DynamicBuffersDxvkConfig(std::string config, int cached_dynamic_buffers) {
  size_t end = config.find_last_not_of(" \t\r\n");
  if (end != std::string::npos && config[end] != ';') config += ';';
  if (cached_dynamic_buffers != 0)
    config += "d3d9.cachedDynamicBuffers = True;";
  else
    config += "d3d9.cachedDynamicBuffers = False;";
  return config;
}

}  // namespace native
