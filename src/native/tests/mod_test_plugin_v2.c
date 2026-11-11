// ABI-v2 compatibility fixture compiled against the new header but restricted to the exact old structure prefix.
#include <protodroid_plugin.h>

#include <string.h>

int32_t ProtoDroidPluginInit(const PdHostApi* host, PdPluginInfo* info) {
    if (!host || !info || host->abi_version != PD_PLUGIN_API_VERSION_V2 ||
        host->struct_size != PD_HOST_API_V2_SIZE) return -1;
    double number = host->get_mod_number(host->context, "number", 0.0);
    char text[32];
    if (number != 6.5 || !host->get_mod_string(host->context, "text", "", text, sizeof(text)) ||
        strcmp(text, "v2 saved") != 0) return -2;
    info->abi_version = PD_PLUGIN_API_VERSION_V2;
    info->struct_size = sizeof(PdPluginInfo);
    info->name = "ABI v2 compatibility fixture";
    info->version = "2";
    host->log(host->context, PD_LOG_INFO, "v2 plugin initialized with exact legacy host size and settings");
    return 0;
}
