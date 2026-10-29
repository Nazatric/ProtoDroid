// Loading and checking a Turnip (or user-supplied Adreno) driver through libadrenotools: shared by the game
// (vulkan_driver.cpp) and the probe program that tries a driver in a fresh process first (turnip_probe.cpp).
// Nothing here logs: the probe reports through its output.
#pragma once

#include <dlfcn.h>

#include <adrenotools/driver.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "gpu_profile.h"

namespace native {

struct ProbeInfo {
    uint32_t api_version = 0;   // physical device's advertised API, for diagnostics
    uint32_t usable_api = 0;    // instance/device intersection, capped at the tested 1.3 path
    uint32_t instance_api = 0;
    uint32_t vendor_id = 0;
    uint32_t driver_id = 0;
};

// Creates a throwaway instance to see which GPU the driver drives; returns the device name ("" if none) and puts a
// line for the log in `report`. Vulkan 1.3 remains preferred, but a 1.2 loader/device gets a real negotiated instance
// so DXVK can use its extension-equivalent compatibility path instead of failing during instance creation.
inline std::string ProbeDevice(PFN_vkGetInstanceProcAddr gipa, const char* driver, std::string& report,
                               ProbeInfo* probe_info = nullptr) {
    char line[1024];
    ProbeInfo info;
    auto create = reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr, "vkCreateInstance"));
    if (!create) {
        snprintf(line, sizeof(line), "vulkan: %s has no vkCreateInstance", driver);
        report = line;
        return "";
    }

    uint32_t loader_api = VK_API_VERSION_1_0;
    auto instance_version = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(gipa(nullptr, "vkEnumerateInstanceVersion"));
    if (instance_version && instance_version(&loader_api) != VK_SUCCESS) loader_api = VK_API_VERSION_1_0;
    const uint32_t requested_api = std::min(loader_api, kVulkanApi13);
    info.instance_api = requested_api;
    if (requested_api < kVulkanApi12) {
        snprintf(line, sizeof(line), "vulkan: %s loader supports only Vulkan %u.%u (1.2 or newer is required)", driver,
                 VK_API_VERSION_MAJOR(loader_api), VK_API_VERSION_MINOR(loader_api));
        report = line;
        return "";
    }

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.apiVersion = requested_api;
    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    if (VkResult r = create(&ci, nullptr, &instance); r != VK_SUCCESS) {
        snprintf(line, sizeof(line), "vulkan: %s vkCreateInstance(%u.%u) -> %d", driver,
                 VK_API_VERSION_MAJOR(requested_api), VK_API_VERSION_MINOR(requested_api), r);
        report = line;
        return "";
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
                 "vulkan: %s sees %s (profile %s, device Vulkan %u.%u.%u, instance %u.%u, usable %u.%u, driver %08x, driverID %u, vendor %04x device %08x)",
                 driver, p.deviceName, profile.name, VK_API_VERSION_MAJOR(p.apiVersion),
                 VK_API_VERSION_MINOR(p.apiVersion), VK_API_VERSION_PATCH(p.apiVersion),
                 VK_API_VERSION_MAJOR(requested_api), VK_API_VERSION_MINOR(requested_api),
                 VK_API_VERSION_MAJOR(info.usable_api), VK_API_VERSION_MINOR(info.usable_api), p.driverVersion,
                 info.driver_id, p.vendorID, p.deviceID);
    } else {
        snprintf(line, sizeof(line), "vulkan: %s vkEnumeratePhysicalDevices -> %d, %u device(s)", driver, r, count);
    }
    report = line;
    if (probe_info) *probe_info = info;
    if (destroy) destroy(instance, nullptr);
    return name;
}

// A driver to try: adrenotools' hook libraries live in the APK's library folder (hook_dir), the driver in dir.
struct Driver {
    std::string hook_dir, dir, file;
    bool custom;  // the user's own driver: any device name is accepted (it may be a Qualcomm build)
};

inline PFN_vkGetInstanceProcAddr LoadTurnip(const Driver& d, std::string& device, std::string& report) {
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
    return gipa;
}

}  // namespace native
