#include <protodroid_plugin.h>

#include <string.h>

static const PdHostApi* g_host;
static PdGuestFunction g_original_adjust_fov;
static unsigned g_frames;
static float g_fov_offset = 5.0f;
static int g_log_frames = 1;

static void ExampleLog(PdLogLevel level, const char* message) {
    if (g_host) g_host->log(g_host->context, level, message);
}

static void ExampleFrame(void* userdata) {
    (void)userdata;
    ++g_frames;
    if (g_log_frames && g_frames == 300) ExampleLog(PD_LOG_INFO, "example plugin reached 300 engine frames");
}

static void ExampleAdjustFov(PdGuestContext* context) {
    uint32_t esp = 0;
    float fov = 0.0f;
    if (g_host && g_host->get_register(g_host->context, context, PD_REG_ESP, &esp) &&
        g_host->read_guest(g_host->context, esp + 4, &fov, sizeof(fov)) && fov == fov) {
        fov += g_fov_offset;
        if (fov < 30.0f) fov = 30.0f;
        if (fov > 150.0f) fov = 150.0f;
        g_host->write_guest(g_host->context, esp + 4, &fov, sizeof(fov));
    }
    if (g_original_adjust_fov) g_original_adjust_fov(context);
}

int32_t ProtoDroidPluginInit(const PdHostApi* host, PdPluginInfo* info) {
    if (!host || !info || host->abi_version != PD_PLUGIN_API_VERSION || host->struct_size < sizeof(PdHostApi)) {
        return -1;
    }
    g_host = host;
    info->abi_version = PD_PLUGIN_API_VERSION;
    info->struct_size = sizeof(PdPluginInfo);
    info->name = "ProtoDroid FOV example";
    info->version = "1.1";
    g_fov_offset = (float)host->get_mod_number(host->context, "fov_offset", 5.0);
    if (!host->get_mod_boolean(host->context, "enabled", true)) {
        ExampleLog(PD_LOG_INFO, "example disabled by its mod options");
        return 0;
    }
    char logging[32];
    g_log_frames = host->get_mod_string(host->context, "frame_logging", "Every 300 frames",
                                       logging, sizeof(logging)) &&
                   strcmp(logging, "Every 300 frames") == 0;
    char note[96];
    if (host->get_mod_string(host->context, "author_note", "", note, sizeof(note)) && note[0]) {
        ExampleLog(PD_LOG_INFO, note);
    }
    if (!host->hook_engine(host->context, 0x006dcfe0, ExampleAdjustFov, &g_original_adjust_fov) ||
        !g_original_adjust_fov) {
        ExampleLog(PD_LOG_ERROR, "could not hook Camera::AdjustFOV");
        return -2;
    }
    if (!host->on_event(host->context, PD_EVENT_FRAME, ExampleFrame, 0)) {
        ExampleLog(PD_LOG_ERROR, "could not register the frame event");
        return -3;
    }
    ExampleLog(PD_LOG_INFO, "example initialized with configurable FOV offset, clamped to 30..150");
    return 0;
}

void ProtoDroidPluginShutdown(void) {
    ExampleLog(PD_LOG_INFO, "example ordinary shutdown");
    /* Detours are process-lifetime: keep g_host and g_original_adjust_fov valid through engine teardown. */
}
