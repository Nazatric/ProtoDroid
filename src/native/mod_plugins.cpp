#include "mod_platform.h"

#include <dlfcn.h>
#include <limits.h>
#include <sys/stat.h>

#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../../modding/include/protodroid_plugin.h"
#include "guest.h"
#include "mod_settings.h"
#include "mod_symbols.h"

namespace native {
namespace {

struct EventBinding {
    PdEvent event;
    PdEventCallback callback;
    void* userdata;
};

using PluginSettings = ModSettings;

struct Plugin {
    const ModEntry* mod = nullptr;
    void* handle = nullptr;
    PdHostApi api{};
    PdPluginInfo info{};
    std::vector<EventBinding> events;
    PluginSettings settings;
    void (*shutdown)() = nullptr;
    bool active = false;
};

std::vector<std::unique_ptr<Plugin>> g_plugins;
bool g_accepting_registration = false;
bool g_shutdown = false;

uint32_t* RegisterPointer(PdGuestContext* opaque, PdRegister reg) {
    Ctx* c = reinterpret_cast<Ctx*>(opaque);
    if (!c) return nullptr;
    switch (reg) {
        case PD_REG_EAX: return &c->eax;
        case PD_REG_ECX: return &c->ecx;
        case PD_REG_EDX: return &c->edx;
        case PD_REG_EBX: return &c->ebx;
        case PD_REG_ESP: return &c->esp;
        case PD_REG_EBP: return &c->ebp;
        case PD_REG_ESI: return &c->esi;
        case PD_REG_EDI: return &c->edi;
        case PD_REG_RET_TARGET: return &c->ret_target;
        case PD_REG_FS_BASE: return &c->fs_base;
        default: return nullptr;
    }
}

bool GuestRange(uint32_t address, uint32_t size) {
    return size == 0 || (address != 0 && uint64_t(address) + uint64_t(size) <= 0x100000000ULL);
}

bool SafePluginRelative(const char* value) {
    if (!value || !*value || value[0] == '/' || value[0] == '\\' || strstr(value, "..") || strchr(value, ':')) {
        return false;
    }
    for (const char* p = value; *p; ++p) {
        if (static_cast<unsigned char>(*p) < 32 || *p == '\\') return false;
    }
    return true;
}

void ApiLog(void* opaque, PdLogLevel level, const char* message) {
    auto* p = static_cast<Plugin*>(opaque);
    if (!p || !message) return;
    const char* tag = level == PD_LOG_ERROR ? "error" : level == PD_LOG_WARNING ? "warning" : "info";
    Log("mods[%s] %s: %s", p->mod->id.c_str(), tag, message);
}

bool ApiRead(void* opaque, uint32_t address, void* output, uint32_t size) {
    (void)opaque;
    if ((!output && size) || !GuestRange(address, size)) return false;
    if (size) memcpy(output, Ptr<void>(address), size);
    return true;
}

bool ApiWrite(void* opaque, uint32_t address, const void* input, uint32_t size) {
    (void)opaque;
    if ((!input && size) || !GuestRange(address, size)) return false;
    if (size) memcpy(Ptr<void>(address), input, size);
    return true;
}

bool ApiGetRegister(void* opaque, PdGuestContext* guest, PdRegister reg, uint32_t* output) {
    (void)opaque;
    uint32_t* value = RegisterPointer(guest, reg);
    if (!value || !output) return false;
    *output = *value;
    return true;
}

bool ApiSetRegister(void* opaque, PdGuestContext* guest, PdRegister reg, uint32_t value) {
    (void)opaque;
    uint32_t* target = RegisterPointer(guest, reg);
    if (!target) return false;
    *target = value;
    return true;
}

uint32_t ApiGuestCall(void* opaque, PdGuestContext* current, uint32_t rva, const uint32_t* arguments,
                      uint32_t argument_count, uint32_t ecx) {
    auto* p = static_cast<Plugin*>(opaque);
    if (!p || rva >= kEngineReserve || argument_count > 64 || (argument_count && !arguments)) return 0;
    uint32_t va = kEngineBase + rva;
    if (!RecompLookup(va)) {
        Log("mods[%s] guest call has no recompiled target: RVA %08x", p->mod->id.c_str(), rva);
        return 0;
    }
    return GuestCallArray(reinterpret_cast<Ctx*>(current), va, arguments, argument_count, ecx);
}

bool ApiHook(void* opaque, uint32_t rva, PdGuestFunction replacement, PdGuestFunction* original) {
    auto* p = static_cast<Plugin*>(opaque);
    if (!p || !g_accepting_registration || !replacement || !original || rva >= kEngineReserve) return false;
    return ModHookInstall(kEngineBase + rva, reinterpret_cast<ModHookFn>(replacement),
                          reinterpret_cast<ModHookFn*>(original));
}

bool ApiAddFileOverride(void* opaque, const char* guest_relative, const char* mod_relative) {
    auto* p = static_cast<Plugin*>(opaque);
    if (!p || !g_accepting_registration || !SafePluginRelative(mod_relative)) return false;
    std::string source = p->mod->root + "/" + mod_relative;
    char resolved[PATH_MAX], root[PATH_MAX];
    if (!realpath(source.c_str(), resolved) || !realpath(p->mod->root.c_str(), root)) return false;
    std::string root_string(root), candidate(resolved);
    if (candidate != root_string && candidate.rfind(root_string + "/", 0) != 0) return false;
    struct stat st{};
    if (stat(candidate.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
    return ModFilesAddAlias(p->mod->priority, guest_relative, candidate);
}

bool ApiOnEvent(void* opaque, PdEvent event, PdEventCallback callback, void* userdata) {
    auto* p = static_cast<Plugin*>(opaque);
    if (!p || !g_accepting_registration || !callback ||
        (event != PD_EVENT_FRAME && event != PD_EVENT_SHUTDOWN)) return false;
    p->events.push_back({event, callback, userdata});
    return true;
}

int ApiGetConfig(void* opaque, const char* key, int default_value) {
    auto* p = static_cast<Plugin*>(opaque);
    if (!p) return default_value;
    return SettingInt(key, default_value);
}

bool SafeConfigKey(const char* key) {
    if (!key) return false;
    size_t size = strnlen(key, 49);
    if (!size || size > 48 ||
        !(std::islower(static_cast<unsigned char>(key[0])) || std::isdigit(static_cast<unsigned char>(key[0])))) {
        return false;
    }
    for (size_t i = 0; i < size; ++i) {
        char c = key[i];
        if (!std::islower(static_cast<unsigned char>(c)) && !std::isdigit(static_cast<unsigned char>(c)) &&
            c != '.' && c != '_' && c != '-') return false;
    }
    return true;
}

const std::string* ConfigValue(Plugin* plugin, const char* key) {
    return plugin ? ModSettingsValue(plugin->settings, key) : nullptr;
}

bool EqualsIgnoreCase(const std::string& value, const char* expected) {
    size_t size = strlen(expected);
    if (value.size() != size) return false;
    for (size_t i = 0; i < size; ++i) {
        if (std::tolower(static_cast<unsigned char>(value[i])) !=
            std::tolower(static_cast<unsigned char>(expected[i]))) return false;
    }
    return true;
}

bool ApiGetModBoolean(void* opaque, const char* key, bool default_value) {
    auto* p = static_cast<Plugin*>(opaque);
    const std::string* value = ConfigValue(p, key);
    if (!value) return default_value;
    if (*value == "1" || EqualsIgnoreCase(*value, "true") || EqualsIgnoreCase(*value, "on") ||
        EqualsIgnoreCase(*value, "yes")) return true;
    if (*value == "0" || EqualsIgnoreCase(*value, "false") || EqualsIgnoreCase(*value, "off") ||
        EqualsIgnoreCase(*value, "no")) return false;
    return default_value;
}

double ApiGetModNumber(void* opaque, const char* key, double default_value) {
    auto* p = static_cast<Plugin*>(opaque);
    const std::string* value = ConfigValue(p, key);
    if (!value || value->empty()) return default_value;
    char* end = nullptr;
    errno = 0;
    double result = std::strtod(value->c_str(), &end);
    if (errno == ERANGE || !end || *end || !std::isfinite(result)) return default_value;
    return result;
}

bool ApiGetModString(void* opaque, const char* key, const char* default_value,
                     char* output, uint32_t output_size) {
    auto* p = static_cast<Plugin*>(opaque);
    if (!output || !output_size) return false;
    const std::string* saved = ConfigValue(p, key);
    const char* value = saved ? saved->c_str() : (default_value ? default_value : "");
    size_t size = saved ? saved->size() : strlen(value);
    if (size >= output_size) {
        output[0] = 0;
        return false;
    }
    memcpy(output, value, size + 1);
    return true;
}

bool ApiLookupSymbol(void*, const char* name, uint32_t ordinal, uint32_t* rva) {
    if (!name || !rva) return false;
    ModSymbolMatch match;
    if (!ModSymbolResolve(name, ordinal, &match)) return false;
    *rva = match.rva;
    return true;
}

bool ApiEmitEvent(void* opaque, const char* name, const uint32_t* arguments, uint32_t count) {
    auto* plugin = static_cast<Plugin*>(opaque);
    if (!plugin || !name || !ModScriptsEmit(name, arguments, count)) return false;
    Log("mods[%s] queued script event %s (%u arguments)", plugin->mod->id.c_str(), name, count);
    return true;
}

void Fail(const ModEntry& mod, const std::string& error) {
    Log("mods: plugin %s: %s", mod.id.c_str(), error.c_str());
    ModWriteLaunchError(mod, error);
}

}  // namespace

void ModPluginsInitialize(const std::vector<ModEntry>& mods) {
    const std::string& native_root = ModNativeRoot();
    for (const ModEntry& mod : mods) {
        if (mod.plugin.empty()) continue;
        if (native_root.empty()) {
            Fail(mod, "private native launch directory is unavailable");
            continue;
        }
        std::string path = native_root + "/" + mod.id + "/" + mod.plugin;
        dlerror();
        void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            const char* error = dlerror();
            Fail(mod, error ? error : "dlopen failed");
            continue;
        }
        auto init = reinterpret_cast<int32_t (*)(const PdHostApi*, PdPluginInfo*)>(
            dlsym(handle, PD_PLUGIN_INIT_SYMBOL));
        if (!init) {
            Fail(mod, "missing " PD_PLUGIN_INIT_SYMBOL " export");
            // Constructors may already have run during dlopen. Keep the handle/library alive just like a partially
            // initialized plugin; unloading here could invalidate constructor-installed code or data references.
            continue;
        }

        auto plugin = std::make_unique<Plugin>();
        plugin->mod = &mod;
        plugin->handle = handle;
        const uint32_t abi = static_cast<uint32_t>(mod.pluginApi);
        std::string settings_error;
        if (abi >= 2 && !ModSettingsLoad(mod, &plugin->settings, &settings_error)) {
            Fail(mod, settings_error.empty() ? "mod settings could not be read" : settings_error);
            g_plugins.push_back(std::move(plugin));
            continue;
        }
        plugin->api.abi_version = abi;
        plugin->api.struct_size = abi == PD_PLUGIN_API_VERSION_V1 ? PD_HOST_API_V1_SIZE
            : abi == PD_PLUGIN_API_VERSION_V2 ? PD_HOST_API_V2_SIZE : sizeof(PdHostApi);
        plugin->api.context = plugin.get();
        plugin->api.engine_sha256 = kModEngineSha256;
        plugin->api.game_directory = g_game_dir ? g_game_dir : "";
        plugin->api.mod_directory = mod.root.c_str();
        plugin->api.log = ApiLog;
        plugin->api.read_guest = ApiRead;
        plugin->api.write_guest = ApiWrite;
        plugin->api.get_register = ApiGetRegister;
        plugin->api.set_register = ApiSetRegister;
        plugin->api.guest_call = ApiGuestCall;
        plugin->api.hook_engine = ApiHook;
        plugin->api.add_file_override = ApiAddFileOverride;
        plugin->api.on_event = ApiOnEvent;
        plugin->api.get_config_int = ApiGetConfig;
        plugin->api.get_mod_boolean = ApiGetModBoolean;
        plugin->api.get_mod_number = ApiGetModNumber;
        plugin->api.get_mod_string = ApiGetModString;
        plugin->api.lookup_symbol = ApiLookupSymbol;
        plugin->api.emit_event = ApiEmitEvent;
        plugin->info.abi_version = abi;
        plugin->info.struct_size = sizeof(PdPluginInfo);
        plugin->shutdown = reinterpret_cast<void (*)()>(dlsym(handle, PD_PLUGIN_SHUTDOWN_SYMBOL));

        g_accepting_registration = true;
        int32_t result = init(&plugin->api, &plugin->info);
        g_accepting_registration = false;
        if (result != 0 || plugin->info.abi_version != abi ||
            plugin->info.struct_size < sizeof(PdPluginInfo) || !plugin->info.name) {
            Fail(mod, result != 0 ? "plugin initialization returned an error" : "plugin returned invalid ABI information");
            // Initialization may already have installed detours. Keep the library and context alive; never dlclose a
            // partially initialized plugin while a recompiled call could still enter it.
            g_plugins.push_back(std::move(plugin));
            continue;
        }
        plugin->active = true;
        Log("mods: plugin %s loaded: %s %s (API %u)", mod.id.c_str(), plugin->info.name,
            plugin->info.version ? plugin->info.version : "unknown", plugin->info.abi_version);
        g_plugins.push_back(std::move(plugin));
    }
}

void ModPluginsFrame() {
    if (g_shutdown) return;
    for (auto& plugin : g_plugins) {
        if (!plugin->active) continue;
        for (const EventBinding& event : plugin->events) {
            if (event.event == PD_EVENT_FRAME) event.callback(event.userdata);
        }
    }
}

void ModPluginsShutdown() {
    if (g_shutdown) return;
    g_shutdown = true;
    for (auto& plugin : g_plugins) {
        if (!plugin->active) continue;
        for (const EventBinding& event : plugin->events) {
            if (event.event == PD_EVENT_SHUTDOWN) event.callback(event.userdata);
        }
        if (plugin->shutdown) plugin->shutdown();
    }
    // Do not dlclose: Dobby trampolines and plugin callbacks can still be referenced during engine teardown.
}

}  // namespace native
