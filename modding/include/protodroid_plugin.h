#ifndef PROTODROID_PLUGIN_H
#define PROTODROID_PLUGIN_H

/* ProtoDroid native ARM64 plugin ABI v1/v2. This is not the Windows ASI ABI. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PD_PLUGIN_API_VERSION 2u
#define PD_PLUGIN_API_VERSION_V1 1u
#define PD_PLUGIN_INIT_SYMBOL "ProtoDroidPluginInit"
#define PD_PLUGIN_SHUTDOWN_SYMBOL "ProtoDroidPluginShutdown"

typedef struct PdGuestContext PdGuestContext;
typedef void (*PdGuestFunction)(PdGuestContext* context);
typedef void (*PdEventCallback)(void* userdata);

typedef enum PdLogLevel {
    PD_LOG_INFO = 0,
    PD_LOG_WARNING = 1,
    PD_LOG_ERROR = 2
} PdLogLevel;

typedef enum PdRegister {
    PD_REG_EAX = 0,
    PD_REG_ECX = 1,
    PD_REG_EDX = 2,
    PD_REG_EBX = 3,
    PD_REG_ESP = 4,
    PD_REG_EBP = 5,
    PD_REG_ESI = 6,
    PD_REG_EDI = 7,
    PD_REG_RET_TARGET = 8,
    PD_REG_FS_BASE = 9
} PdRegister;

typedef enum PdEvent {
    /* Once after each EngineService on the engine thread. Keep this callback inexpensive. */
    PD_EVENT_FRAME = 1,
    /* Ordinary safe shutdown boundary, before EngineTerminate. Not sent on a fatal process exit. */
    PD_EVENT_SHUTDOWN = 2
} PdEvent;

typedef struct PdHostApi {
    uint32_t abi_version;
    uint32_t struct_size;
    void* context;

    /* Process-lifetime UTF-8 strings. mod_directory is the current plugin's own package directory. */
    const char* engine_sha256;
    const char* game_directory;
    const char* mod_directory;

    void (*log)(void* context, PdLogLevel level, const char* message);
    bool (*read_guest)(void* context, uint32_t address, void* output, uint32_t size);
    bool (*write_guest)(void* context, uint32_t address, const void* input, uint32_t size);
    bool (*get_register)(void* context, PdGuestContext* guest, PdRegister reg, uint32_t* output);
    bool (*set_register)(void* context, PdGuestContext* guest, PdRegister reg, uint32_t value);

    /* rva is relative to the original engine image base (for example 0x006dcfe0). Arguments are pushed in C order.
       current may be NULL on the engine thread, or the detour context for a nested guest call. */
    uint32_t (*guest_call)(void* context, PdGuestContext* current, uint32_t rva,
                           const uint32_t* arguments, uint32_t argument_count, uint32_t ecx);

    /* Function-level detour. Later hooks wrap earlier hooks; *original calls the previous detour or real function.
       Register during ProtoDroidPluginInit only. The callback and original have process lifetime. */
    bool (*hook_engine)(void* context, uint32_t rva, PdGuestFunction replacement, PdGuestFunction* original);

    /* Map a C:\Game-relative file to a file inside this plugin's own mod directory. Original game files are untouched. */
    bool (*add_file_override)(void* context, const char* guest_relative, const char* mod_relative);

    /* Register during ProtoDroidPluginInit only. */
    bool (*on_event)(void* context, PdEvent event, PdEventCallback callback, void* userdata);

    /* Legacy process-wide engine INI lookup retained for ABI v1. Prefer the per-mod getters below in new plugins. */
    int (*get_config_int)(void* context, const char* key, int default_value);

    /* ABI v2: values from this plugin's own protodroid-settings.ini, never another mod or the engine INI.
       Missing/unparsable values return the supplied default. The string getter copies a UTF-8 value and returns false
       when output is null, too small, or output_size is zero. All getters are safe during init and frame events. */
    bool (*get_mod_boolean)(void* context, const char* key, bool default_value);
    double (*get_mod_number)(void* context, const char* key, double default_value);
    bool (*get_mod_string)(void* context, const char* key, const char* default_value,
                           char* output, uint32_t output_size);
} PdHostApi;

/* Exact byte size seen by existing ABI-v1 plugins. New fields are appended, never inserted. */
#define PD_HOST_API_V1_SIZE ((uint32_t)offsetof(PdHostApi, get_mod_boolean))

typedef struct PdPluginInfo {
    uint32_t abi_version;
    uint32_t struct_size;
    const char* name;     /* static/process-lifetime UTF-8 */
    const char* version;  /* static/process-lifetime UTF-8 */
} PdPluginInfo;

/* Required export. Return 0 on success; a nonzero result disables this plugin and is logged. */
int32_t ProtoDroidPluginInit(const PdHostApi* host, PdPluginInfo* info);
/* Optional export. Called at the same ordinary shutdown boundary as PD_EVENT_SHUTDOWN.
   Detours remain installed through teardown; callback, host and original-pointer state must remain valid. */
void ProtoDroidPluginShutdown(void);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* PROTODROID_PLUGIN_H */
