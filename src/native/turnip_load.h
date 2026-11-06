// Loading and checking a bundled/custom Vulkan driver: shared by the game (vulkan_driver.cpp) and the
// isolated probe (turnip_probe.cpp). Turnip uses libadrenotools; PanVK-kbase uses its direct ICD entry point.
// Nothing here logs: the probe reports through its output.
#pragma once

#include <dlfcn.h>
#include <sys/stat.h>

#include <adrenotools/driver.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "gpu_profile.h"

namespace native {

struct ProbeInfo {
    uint32_t api_version = 0;   // physical device's advertised API, for diagnostics
    uint32_t usable_api = 0;    // instance/device intersection, capped at the tested 1.3 path
    uint32_t instance_api = 0;  // requested instance API (kept separate from the loader's advertised maximum)
    uint32_t loader_api = 0;
    uint32_t vendor_id = 0;
    uint32_t driver_id = 0;
    bool required_features_ok = false;
};

inline void AppendProbeReport(std::string& report, const std::string& line) {
    if (!report.empty()) report += '\n';
    report += line;
}

// Required by the patched DXVK 2.5.3 actually shipped here: D3D9DeviceEx::GetDeviceFeatures plus the unconditional
// core device enables in DxvkAdapter::createDevice. The Android patch makes BC optional (native DXT decoding) and
// clamps robustness2 (dummy resources replace null descriptors), so those are reported but not grounds for rejection.
// Query the effective wrapper device rather than inferring support from its base driver or advertised API number.
inline bool ProbeRequiredDxvkFeatures(PFN_vkGetInstanceProcAddr gipa, VkInstance instance, VkPhysicalDevice device,
                                      uint32_t effective_api, std::string& report) {
    if (effective_api < kVulkanApi12) {
        AppendProbeReport(report, "vulkan: wrapper effective device remains below Vulkan 1.2");
        return false;
    }
    auto enumerate_extensions = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
        gipa(instance, "vkEnumerateDeviceExtensionProperties"));
    auto get_features = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
        gipa(instance, "vkGetPhysicalDeviceFeatures2"));
    if (!enumerate_extensions || !get_features) {
        AppendProbeReport(report, "vulkan: wrapper capability query lacks extension enumeration or features2");
        return false;
    }

    uint32_t count = 0;
    if (enumerate_extensions(device, nullptr, &count, nullptr) < 0) {
        AppendProbeReport(report, "vulkan: wrapper device-extension enumeration failed");
        return false;
    }
    std::vector<VkExtensionProperties> extensions(count);
    const VkResult extension_result = count
        ? enumerate_extensions(device, nullptr, &count, extensions.data()) : VK_SUCCESS;
    if (extension_result < 0) {
        AppendProbeReport(report, "vulkan: wrapper device-extension list failed with " +
                                  std::to_string(extension_result));
        return false;
    }
    extensions.resize(count);
    auto has_extension = [&](const char* name) {
        for (const auto& extension : extensions) {
            if (strcmp(extension.extensionName, name) == 0) return true;
        }
        return false;
    };
    auto append_name = [](std::string& list, const char* name) {
        if (!list.empty()) list += ' ';
        list += name;
    };

    // Match DXVK's two actual query paths. In particular, a 1.2 device is not queried with the core-1.3 struct:
    // its KHR/EXT equivalents supply the bits that DxvkAdapter copies into its internal vk13 feature record.
    const bool compatibility12 = effective_api < kVulkanApi13;
    VkPhysicalDeviceFeatures2 core{};
    core.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    VkPhysicalDeviceVulkan11Features f11{};
    f11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    VkPhysicalDeviceVulkan12Features f12{};
    f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    core.pNext = &f11;
    f11.pNext = &f12;
    void** tail = &f12.pNext;
    auto append_feature = [&](auto* feature) {
        *tail = feature;
        tail = &feature->pNext;
    };

    VkPhysicalDeviceVulkan13Features f13{};
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamic12{};
    VkPhysicalDeviceSynchronization2FeaturesKHR synchronization12{};
    VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT demote12{};
    VkPhysicalDeviceMaintenance4FeaturesKHR maintenance12{};
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extended12{};
    if (!compatibility12) {
        f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        append_feature(&f13);
    } else {
        if (has_extension(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)) {
            dynamic12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
            append_feature(&dynamic12);
        }
        if (has_extension(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)) {
            synchronization12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
            append_feature(&synchronization12);
        }
        if (has_extension(VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME)) {
            demote12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT;
            append_feature(&demote12);
        }
        if (has_extension(VK_KHR_MAINTENANCE_4_EXTENSION_NAME)) {
            maintenance12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES_KHR;
            append_feature(&maintenance12);
        }
        if (has_extension(VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME)) {
            extended12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
            append_feature(&extended12);
        }
    }
    VkPhysicalDeviceRobustness2FeaturesEXT robust{};
    if (has_extension(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME)) {
        robust.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT;
        append_feature(&robust);
    }
    get_features(device, &core);

    const VkBool32 dynamic_rendering = compatibility12 ? dynamic12.dynamicRendering : f13.dynamicRendering;
    const VkBool32 synchronization2 = compatibility12 ? synchronization12.synchronization2 : f13.synchronization2;
    const VkBool32 demote = compatibility12 ? demote12.shaderDemoteToHelperInvocation : f13.shaderDemoteToHelperInvocation;
    const VkBool32 maintenance4 = compatibility12 ? maintenance12.maintenance4 : f13.maintenance4;
    struct Required { const char* name; VkBool32 supported; };
    const Required required[] = {
        {"robustBufferAccess", core.features.robustBufferAccess},
        {"multiDrawIndirect", core.features.multiDrawIndirect},
        {"geometryShader", core.features.geometryShader},
        {"imageCubeArray", core.features.imageCubeArray},
        {"depthClamp", core.features.depthClamp},
        {"depthBiasClamp", core.features.depthBiasClamp},
        {"fillModeNonSolid", core.features.fillModeNonSolid},
        {"sampleRateShading", core.features.sampleRateShading},
        {"shaderClipDistance", core.features.shaderClipDistance},
        {"shaderCullDistance", core.features.shaderCullDistance},
        {"occlusionQueryPrecise", core.features.occlusionQueryPrecise},
        {"multiViewport", core.features.multiViewport},
        {"independentBlend", core.features.independentBlend},
        {"fullDrawIndexUint32", core.features.fullDrawIndexUint32},
        {"shaderDrawParameters", f11.shaderDrawParameters},
        {"samplerMirrorClampToEdge", f12.samplerMirrorClampToEdge},
        {"vulkanMemoryModel", f12.vulkanMemoryModel},
        {"hostQueryReset", f12.hostQueryReset},
        {"timelineSemaphore", f12.timelineSemaphore},
        {"shaderDemoteToHelperInvocation", demote},
        {"synchronization2", synchronization2},
        {"dynamicRendering", dynamic_rendering},
        // maintenance4 is optional on Android: patched DXVK uses temporary buffer/image
        // memory-requirement queries when it is absent, on both the 1.2 and 1.3 paths.
        // Extended dynamic state is a required extension/feature only on that 1.2 path.
        {"extendedDynamicState", !compatibility12 || extended12.extendedDynamicState},
    };
    std::string missing;
    for (const auto& feature : required) {
        if (!feature.supported) append_name(missing, feature.name);
    }
    std::string missing_extensions;
    if (!has_extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME))
        append_name(missing_extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    if (compatibility12) {
        const char* required_extensions[] = {
            VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
            VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
            VK_KHR_COPY_COMMANDS_2_EXTENSION_NAME,
            VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
        };
        for (const char* name : required_extensions) {
            if (!has_extension(name)) append_name(missing_extensions, name);
        }
    }

    char line[1024];
    snprintf(line, sizeof(line),
             "vulkan: wrapper effective features: required=%s robustness2ext=%u robustBufferAccess2=%u "
             "nullDescriptor=%u robustImageAccess2=%u textureCompressionBC=%u maintenance4=%u",
             missing.empty() && missing_extensions.empty() ? "yes" : "NO",
             has_extension(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME) ? 1u : 0u, robust.robustBufferAccess2 ? 1u : 0u,
             robust.nullDescriptor ? 1u : 0u, robust.robustImageAccess2 ? 1u : 0u,
             core.features.textureCompressionBC ? 1u : 0u, maintenance4 ? 1u : 0u);
    AppendProbeReport(report, line);
    if (!robust.nullDescriptor)
        AppendProbeReport(report, "vulkan: nullDescriptor unavailable; patched DXVK will use its dummy-resource path");
    if (!core.features.textureCompressionBC)
        AppendProbeReport(report, "vulkan: BC formats unavailable; ProtoDroid's native DXT decode path remains necessary");
    if (!missing.empty()) AppendProbeReport(report, "vulkan: wrapper missing required features: " + missing);
    if (!missing_extensions.empty())
        AppendProbeReport(report, "vulkan: wrapper missing required device extensions: " + missing_extensions);
    return missing.empty() && missing_extensions.empty();
}

// Creates a throwaway instance to see which GPU the driver drives; returns the device name ("" if none) and puts a
// line for the log in `report`. Vulkan 1.3 remains preferred, but a 1.2 loader/device gets a real negotiated instance
// so DXVK can use its extension-equivalent compatibility path instead of failing during instance creation.
inline std::string ProbeDevice(PFN_vkGetInstanceProcAddr gipa, const char* driver, std::string& report,
                               ProbeInfo* probe_info = nullptr, bool compatibility_wrapper = false) {
    char line[1024];
    ProbeInfo info;
    report.clear();
    auto fail = [&](const std::string& text) {
        report = text;
        if (probe_info) *probe_info = info;
        return std::string();
    };

    auto create = reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr, "vkCreateInstance"));
    if (!create) {
        snprintf(line, sizeof(line), "vulkan: %s has no vkCreateInstance", driver);
        return fail(line);
    }

    uint32_t loader_api = VK_API_VERSION_1_0;
    auto instance_version = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(gipa(nullptr, "vkEnumerateInstanceVersion"));
    if (instance_version && instance_version(&loader_api) != VK_SUCCESS) loader_api = VK_API_VERSION_1_0;
    // A wrapper must see DXVK's intended 1.3 request to enable its lowering path, even when the base loader advertises
    // only 1.1. Direct drivers retain the old guard and never receive an over-versioned request.
    const uint32_t requested_api = compatibility_wrapper ? kVulkanApi13 : std::min(loader_api, kVulkanApi13);
    info.loader_api = loader_api;
    info.instance_api = requested_api;
    if (compatibility_wrapper && !SupportsVulkan11WrapperBase(loader_api)) {
        snprintf(line, sizeof(line),
                 "vulkan: %s base loader supports only Vulkan %u.%u (the compatibility route requires 1.1 or newer)",
                 driver, VK_API_VERSION_MAJOR(loader_api), VK_API_VERSION_MINOR(loader_api));
        return fail(line);
    }
    if (!compatibility_wrapper && requested_api < kVulkanApi12) {
        snprintf(line, sizeof(line), "vulkan: %s loader supports only Vulkan %u.%u (1.2 or newer is required)", driver,
                 VK_API_VERSION_MAJOR(loader_api), VK_API_VERSION_MINOR(loader_api));
        return fail(line);
    }

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "ProtoDroid";
    app.pEngineName = "DXVK";  // also selects the wrapper's DXVK lowering route
    app.apiVersion = requested_api;
    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    if (VkResult r = create(&ci, nullptr, &instance); r != VK_SUCCESS) {
        snprintf(line, sizeof(line), "vulkan: %s vkCreateInstance(%u.%u) -> %d (loader advertised %u.%u)", driver,
                 VK_API_VERSION_MAJOR(requested_api), VK_API_VERSION_MINOR(requested_api), r,
                 VK_API_VERSION_MAJOR(loader_api), VK_API_VERSION_MINOR(loader_api));
        return fail(line);
    }
    auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(gipa(instance, "vkEnumeratePhysicalDevices"));
    auto props = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(gipa(instance, "vkGetPhysicalDeviceProperties"));
    auto props2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(gipa(instance, "vkGetPhysicalDeviceProperties2"));
    auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(gipa(instance, "vkDestroyInstance"));
    std::string name;
    uint32_t count = 1;
    VkPhysicalDevice device = VK_NULL_HANDLE;
    VkResult r = enumerate ? enumerate(instance, &count, &device) : VK_ERROR_INITIALIZATION_FAILED;
    if (props && r >= 0 && count && device) {
        VkPhysicalDeviceProperties p{};
        props(device, &p);
        info.api_version = p.apiVersion;
        info.usable_api = NegotiateVulkanApi(requested_api, p.apiVersion);
        info.vendor_id = p.vendorID;

        // Driver properties are core in Vulkan 1.2. Query the base properties first so an older physical device never
        // receives a structure chain it does not support.
        if (props2 && p.apiVersion >= kVulkanApi12) {
            VkPhysicalDeviceDriverProperties driver_properties{};
            driver_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
            VkPhysicalDeviceProperties2 properties2{};
            properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            properties2.pNext = &driver_properties;
            props2(device, &properties2);
            p = properties2.properties;
            info.driver_id = driver_properties.driverID;
        }

        name = p.deviceName;
        const GpuProfile profile = GetGpuProfile(p.vendorID);
        snprintf(line, sizeof(line),
                 "vulkan: %s sees %s (profile %s, device Vulkan %u.%u.%u, requested %u.%u, loader %u.%u, usable %u.%u, driver %08x, driverID %u, vendor %04x device %08x)",
                 driver, p.deviceName, profile.name, VK_API_VERSION_MAJOR(p.apiVersion),
                 VK_API_VERSION_MINOR(p.apiVersion), VK_API_VERSION_PATCH(p.apiVersion),
                 VK_API_VERSION_MAJOR(requested_api), VK_API_VERSION_MINOR(requested_api),
                 VK_API_VERSION_MAJOR(loader_api), VK_API_VERSION_MINOR(loader_api),
                 VK_API_VERSION_MAJOR(info.usable_api), VK_API_VERSION_MINOR(info.usable_api), p.driverVersion,
                 info.driver_id, p.vendorID, p.deviceID);
        report = line;
        if (compatibility_wrapper) {
            info.required_features_ok = ProbeRequiredDxvkFeatures(gipa, instance, device, p.apiVersion, report);
            if (!info.required_features_ok) name.clear();
        }
    } else {
        snprintf(line, sizeof(line), "vulkan: %s vkEnumeratePhysicalDevices -> %d, %u device(s)", driver, r, count);
        report = line;
    }
    if (probe_info) *probe_info = info;
    if (destroy) destroy(instance, nullptr);
    return name;
}

// A driver to try: adrenotools' hook libraries live in the APK's library folder (hook_dir), the driver in dir.
struct Driver {
    std::string hook_dir, dir, file;
    bool custom;  // an explicitly imported driver: any vendor's Vulkan device may be accepted after validation
    bool direct_icd = false;  // PanVK-kbase/wrapper: bypass AdrenoTools and use vk_icdGetInstanceProcAddr
    bool compatibility_wrapper = false;  // request DXVK/1.3 and verify the wrapper's effective feature set
};

// AdrenoTools can silently return the system driver when a custom library fails to load. A successful Vulkan
// probe alone cannot distinguish that fallback from a real custom Qualcomm driver. Confirm that the imported
// inode is actually mapped in this process; the path may differ through /data/data and /data/user/0 symlinks.
inline bool CustomDriverMapped(const Driver& d) {
    struct stat expected = {};
    if (stat((d.dir + d.file).c_str(), &expected) != 0) return false;
    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps) return false;
    bool found = false;
    char line[1024];
    while (!found && fgets(line, sizeof(line), maps)) {
        unsigned long long inode = 0;
        if (sscanf(line, "%*s %*s %*s %*s %llu", &inode) == 1 && inode == expected.st_ino &&
            ContainsText(line, d.file.c_str())) {
            found = true;
        }
    }
    fclose(maps);
    return found;
}

inline PFN_vkGetInstanceProcAddr LoadDirectIcd(const Driver& d, std::string& device, std::string& report) {
    const std::string path = d.dir + d.file;
    // Keep the successful handle open for the lifetime of the process. DXVK receives this exact GIPA through
    // DXVK_VK_GIPA, so instance/device calls cannot accidentally jump back to Android's system loader.
    void* lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        const char* err = dlerror();
        report = std::string("vulkan: direct ICD could not load ") + d.file + " (" + (err ? err : "no dlerror") + ")";
        return nullptr;
    }
    auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(lib, "vk_icdGetInstanceProcAddr"));
    if (!gipa) {
        report = "vulkan: direct ICD has no vk_icdGetInstanceProcAddr";
        dlclose(lib);
        return nullptr;
    }
    // Vulkan's ICD interface negotiation is optional in a few older builds, but when present it must succeed.
    // Do not require a loader-only vkGetInstanceProcAddr export from the package.
    using Negotiate = VkResult (*)(uint32_t*);
    auto negotiate = reinterpret_cast<Negotiate>(dlsym(lib, "vk_icdNegotiateLoaderICDInterfaceVersion"));
    if (negotiate) {
        uint32_t version = 7;
        VkResult result = negotiate(&version);
        if (result != VK_SUCCESS || version == 0) {
            char line[256];
            snprintf(line, sizeof(line), "vulkan: direct ICD interface negotiation -> %d (version %u)", result, version);
            report = line;
            dlclose(lib);
            return nullptr;
        }
    }
    ProbeInfo info;
    device = ProbeDevice(gipa, d.file.c_str(), report, &info, d.compatibility_wrapper);
    if (!device.empty() && !info.usable_api) {
        report += " (needs Vulkan 1.2 with required extensions, or 1.3)";
        device.clear();
    }
    if (device.empty()) {
        dlclose(lib);
        return nullptr;
    }
    if (d.custom && !CustomDriverMapped(d)) {
        report += " (the direct ICD was not mapped; refusing an ambiguous fallback)";
        device.clear();
        dlclose(lib);
        return nullptr;
    }
    return gipa;
}

inline PFN_vkGetInstanceProcAddr LoadTurnip(const Driver& d, std::string& device, std::string& report) {
    if (d.direct_icd) return LoadDirectIcd(d, device, report);
    // adrenotools joins the directory and the file name as is, so the directories need the trailing slash.
    void* lib = adrenotools_open_libvulkan(RTLD_NOW, ADRENOTOOLS_DRIVER_CUSTOM, nullptr, d.hook_dir.c_str(),
                                           d.dir.c_str(), d.file.c_str(), nullptr, nullptr);
    if (!lib) {
        const char* err = dlerror();
        report = std::string("vulkan: adrenotools could not load Turnip (") + (err ? err : "no dlerror") + ")";
        return nullptr;
    }
    auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(lib, "vkGetInstanceProcAddr"));
    if (!gipa) {
        report = "vulkan: the adrenotools loader has no vkGetInstanceProcAddr";
        return nullptr;
    }
    ProbeInfo info;
    device = ProbeDevice(gipa, d.file.c_str(), report, &info);
    if (!device.empty() && !info.usable_api) {
        report += " (needs Vulkan 1.2 with required extensions, or 1.3)";
        device.clear();
        return nullptr;
    }
    if (device.empty() || (!d.custom && !IsTurnipIdentity(info.driver_id, device.c_str()))) {
        report += device.empty() ? "" : " (not Turnip: the driver did not load)";
        device.clear();
        return nullptr;
    }
    if (d.custom && !CustomDriverMapped(d)) {
        report += " (the imported library was not mapped; AdrenoTools fell back to the system driver)";
        device.clear();
        return nullptr;
    }
    return gipa;
}

}  // namespace native
