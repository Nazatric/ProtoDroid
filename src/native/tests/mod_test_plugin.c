// Host-only plugin used by mod_plugin_test.cpp. It deliberately wraps the public example's FOV detour.
#include <protodroid_plugin.h>

static const PdHostApi* host_api;
static PdGuestFunction original_fov;
static int called;

static void TestLog(PdLogLevel level, const char* text) {
    if (host_api) host_api->log(host_api->context, level, text);
}

static void TestFov(PdGuestContext* context) {
    uint32_t esp = 0;
    float fov = 0;
    if (host_api->get_register(host_api->context, context, PD_REG_ESP, &esp) &&
        host_api->read_guest(host_api->context, esp + 4, &fov, sizeof(fov))) {
        fov += 1.0f;
        host_api->write_guest(host_api->context, esp + 4, &fov, sizeof(fov));
        host_api->set_register(host_api->context, context, PD_REG_EDX, 0x44);
    }
    if (original_fov) original_fov(context);
}

static void TestFrame(void* userdata) {
    (void)userdata;
    if (called++) return;
    uint32_t arguments[2] = {0x11, 0x22};
    uint32_t result = host_api->guest_call(host_api->context, 0, 0x00001234, arguments, 2, 0x33);
    TestLog(result == 0x9876 ? PD_LOG_INFO : PD_LOG_ERROR,
            result == 0x9876 ? "test guest call ok" : "test guest call failed");
}

static void TestShutdownEvent(void* userdata) {
    (void)userdata;
    TestLog(PD_LOG_INFO, "test shutdown event");
}

int32_t ProtoDroidPluginInit(const PdHostApi* host, PdPluginInfo* info) {
    if (!host || !info || host->abi_version != PD_PLUGIN_API_VERSION_V1 || host->struct_size < PD_HOST_API_V1_SIZE) {
        return -1;
    }
    host_api = host;
    info->abi_version = PD_PLUGIN_API_VERSION_V1;
    info->struct_size = sizeof(PdPluginInfo);
    info->name = "Host API test";
    info->version = "1";
    if (host->get_config_int(host->context, "test_answer", 0) != 42) return -2;
    if (!host->add_file_override(host->context, "data/test.bin", "payload.bin")) return -3;
    if (!host->hook_engine(host->context, 0x006dcfe0, TestFov, &original_fov) || !original_fov) return -4;
    if (!host->on_event(host->context, PD_EVENT_FRAME, TestFrame, 0) ||
        !host->on_event(host->context, PD_EVENT_SHUTDOWN, TestShutdownEvent, 0)) return -5;
    TestLog(PD_LOG_INFO, "test plugin initialized");
    return 0;
}

void ProtoDroidPluginShutdown(void) {
    TestLog(PD_LOG_INFO, "test plugin export shutdown");
}
