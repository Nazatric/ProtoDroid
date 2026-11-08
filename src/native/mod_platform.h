#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct Ctx;

namespace native {

struct ModEntry {
    std::string id;
    std::string root;
    std::string files;
    std::string textures;
    std::string plugin;  // manifest path below lib/arm64-v8a/
    std::string script;  // validated absolute Lua entrypoint
    std::vector<std::string> dependencies;
    int priority = 100;
    int packageApi = 1;
    int pluginApi = 1;
    int scriptApi = 1;
};

// Active, validated packages in priority order (higher first). Populated once before engine startup.
const std::vector<ModEntry>& ModEntries();
const std::string& ModsRoot();
const std::string& ModNativeRoot();
constexpr const char* kModEngineSha256 = "d331be205bdde99897952ee88261ab7e63c5c65e36a4660e2a367886a3537071";

void ModPlatformInitialize();
void ModPlatformFrame();
void ModPlatformShutdown();
void ModWriteLaunchError(const ModEntry& mod, const std::string& message);

// Loose overlay services. All functions are inert with no active file mods and prefer_loose_files=0.
void ModFilesInitialize(const std::vector<ModEntry>& mods);
bool ModFilesHaveOverlays();
std::string ModFilesResolveReadPath(const char* guest_name, const std::string& original_host);
bool ModFilesHasLooseCandidate(const char* guest_name);
bool ModFilesAddAlias(int priority, const char* guest_relative, const std::string& host_file);

// Versioned original-content texture keys and DDS replacement through the existing D3D9 lock/unlock paths.
void ModTexturesInitialize(const std::vector<ModEntry>& mods);
bool ModTexturesEnabled();
void ModTexturesTrack(void* host, bool eligible);
void ModTexturesForget(void* host);
void ModTexturesReplace(void* host, uint32_t level, uint32_t format, uint32_t width, uint32_t height,
                        uint32_t pitch, void* data, bool write_back);

// Function detours are installed only before engine startup. Dobby failures are logged and returned.
using ModHookFn = void (*)(Ctx*);
bool ModHookInstall(uint32_t guest_va, ModHookFn replacement, ModHookFn* original);

// Versioned ARM64 plugin lifecycle.
void ModPluginsInitialize(const std::vector<ModEntry>& mods);
void ModPluginsFrame();
void ModPluginsShutdown();

// Sandboxed Lua packages. Scripts use the same ordered package/dependency graph as native plugins.
void ModScriptsInitialize(const std::vector<ModEntry>& mods);
void ModScriptsFrame();
void ModScriptsShutdown();
bool ModScriptsEmit(const std::string& name, const uint32_t* arguments, size_t count);
// Ask the script runtime to re-read one mod's protodroid-settings.ini on the next engine frame
// and notify that mod with a protodroid.settings_changed event. Thread-safe (the in-game
// settings panel calls it from the UI thread); every pd.settings read becomes live afterwards.
void ModScriptsRequestSettingsReload(const char* mod_id);

// Built-in optional fixes. No original function is hooked when its corresponding setting is off.
void ModFeaturesInitialize();

}  // namespace native
